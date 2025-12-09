/**
 * @file test_tracker.cpp
 * @brief ByteTracker 和 SmartZoomCaptureStrategy 单元测试
 * 
 * 测试内容:
 * 1. ByteTracker 基本追踪功能
 * 2. ByteTracker 唯一计数功能
 * 3. ByteTracker 重置功能
 * 4. SmartZoomCaptureStrategy 状态机转换
 * 5. SmartZoomCaptureStrategy GPS 距离冷却
 * 6. SmartZoomCaptureStrategy 目标过滤
 * 
 * 📌 面试考点:
 * - 单元测试设计模式 (AAA: Arrange-Act-Assert)
 * - 边界条件测试
 * - 状态机测试策略
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-28
 */

#include "esdk_sophon/vision/tracker/ByteTracker.h"
#include "esdk_sophon/vision/strategy/SmartZoomCaptureStrategy.h"
#include "esdk_sophon/core/Logger.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>

using namespace esdk_sophon::vision;
using namespace esdk_sophon::vision::tracker;
using namespace esdk_sophon::vision::strategy;
using namespace esdk_sophon::core;

// ==================== 测试辅助函数 ====================

/**
 * @brief 打印测试结果
 */
void printTestResult(const std::string& testName, bool passed) {
    if (passed) {
        std::cout << "✅ [PASS] " << testName << std::endl;
    } else {
        std::cout << "❌ [FAIL] " << testName << std::endl;
    }
}

/**
 * @brief 创建测试用的检测框
 */
DetectionBox createTestBox(int x, int y, int width, int height, 
                           float confidence = 0.8f, int classId = 0) {
    DetectionBox box;
    box.x = x;
    box.y = y;
    box.width = width;
    box.height = height;
    box.confidence = confidence;
    box.classId = classId;
    box.className = "person";
    return box;
}

/**
 * @brief 创建测试用的追踪对象
 */
TrackedObject createTrackedObject(int x, int y, int width, int height,
                                  float confidence = 0.8f, int trackId = 1) {
    TrackedObject obj;
    obj.x = x;
    obj.y = y;
    obj.width = width;
    obj.height = height;
    obj.confidence = confidence;
    obj.classId = 0;
    obj.className = "person";
    obj.trackId = trackId;
    obj.isNew = false;
    return obj;
}

// ==================== ByteTracker 测试 ====================

/**
 * @brief 测试 ByteTracker 初始化
 */
void testByteTrackerInit() {
    ByteTrackerConfig config;
    config.trackThresh = 0.5f;
    config.matchThresh = 0.8f;
    config.frameRate = 30;
    config.trackBuffer = 30;
    
    ByteTracker tracker(config);
    
    // 初始状态检查
    bool passed = (tracker.getUniqueCount() == 0) &&
                  (tracker.getActiveCount() == 0) &&
                  (tracker.getFrameId() == 0);
    
    printTestResult("ByteTracker 初始化", passed);
}

/**
 * @brief 测试单目标追踪
 */
void testByteTrackerSingleTarget() {
    ByteTrackerConfig config;
    ByteTracker tracker(config);
    
    // 帧1：首次检测到目标
    std::vector<DetectionBox> detections1 = {
        createTestBox(100, 100, 50, 50)
    };
    auto result1 = tracker.update(detections1);
    
    bool passed = (result1.size() == 1) &&
                  (result1[0].trackId >= 0) &&
                  (result1[0].isNew == true);  // 首次出现应标记为新目标
    
    // 帧2：目标略微移动
    std::vector<DetectionBox> detections2 = {
        createTestBox(105, 102, 50, 50)  // 微小位移
    };
    auto result2 = tracker.update(detections2);
    
    passed = passed && 
             (result2.size() == 1) &&
             (result2[0].trackId == result1[0].trackId) &&  // 应保持相同 ID
             (result2[0].isNew == false);  // 不再是新目标
    
    // 检查唯一计数
    passed = passed && (tracker.getUniqueCount() == 1);
    
    printTestResult("ByteTracker 单目标追踪", passed);
}

/**
 * @brief 测试多目标追踪
 */
void testByteTrackerMultiTarget() {
    ByteTrackerConfig config;
    ByteTracker tracker(config);
    
    // 帧1：检测到3个目标
    std::vector<DetectionBox> detections1 = {
        createTestBox(100, 100, 50, 50),
        createTestBox(300, 100, 50, 50),
        createTestBox(500, 100, 50, 50)
    };
    auto result1 = tracker.update(detections1);
    
    bool passed = (result1.size() == 3) &&
                  (tracker.getActiveCount() == 3);
    
    // 帧2：所有目标向右移动
    std::vector<DetectionBox> detections2 = {
        createTestBox(110, 100, 50, 50),
        createTestBox(310, 100, 50, 50),
        createTestBox(510, 100, 50, 50)
    };
    auto result2 = tracker.update(detections2);
    
    passed = passed && 
             (result2.size() == 3) &&
             (tracker.getUniqueCount() == 3);  // 仍然是3个不重复目标
    
    printTestResult("ByteTracker 多目标追踪", passed);
}

/**
 * @brief 测试目标丢失和重新出现
 */
void testByteTrackerTargetLoss() {
    ByteTrackerConfig config;
    config.trackBuffer = 5;  // 丢失后保持5帧
    ByteTracker tracker(config);
    
    // 帧1：检测到目标
    std::vector<DetectionBox> detections1 = {
        createTestBox(100, 100, 50, 50)
    };
    auto result1 = tracker.update(detections1);
    (void)result1[0].trackId;  // 记录原始 ID（用于调试）
    
    // 帧2-4：目标丢失（空检测）
    for (int i = 0; i < 3; i++) {
        std::vector<DetectionBox> empty;
        tracker.update(empty);
    }
    
    // 帧5：目标重新出现在附近位置
    std::vector<DetectionBox> detections5 = {
        createTestBox(110, 105, 50, 50)
    };
    auto result5 = tracker.update(detections5);
    
    // 由于在 trackBuffer 内，应该恢复相同的 ID
    bool passed = (result5.size() == 1) &&
                  (tracker.getUniqueCount() == 1);  // 应该仍是同一个目标
    
    printTestResult("ByteTracker 目标丢失重连", passed);
}

/**
 * @brief 测试新目标计数
 * 
 * 注意：ByteTrack 算法需要几帧来确认新轨迹，
 * 所以需要连续几帧检测到目标才能被计入唯一计数。
 */
void testByteTrackerNewTargetCount() {
    ByteTrackerConfig config;
    config.trackThresh = 0.3f;  // 降低阈值，更容易追踪
    ByteTracker tracker(config);
    
    // 帧1-3：发现2个目标并保持追踪
    for (int i = 0; i < 3; i++) {
        std::vector<DetectionBox> det = {
            createTestBox(100 + i*5, 100, 50, 50),
            createTestBox(200 + i*5, 100, 50, 50)
        };
        tracker.update(det);
    }
    int countAfterFirst = tracker.getUniqueCount();
    
    // 帧4-6：原目标继续移动 + 新发现1个目标
    for (int i = 0; i < 3; i++) {
        std::vector<DetectionBox> det = {
            createTestBox(115 + i*5, 100, 50, 50),
            createTestBox(215 + i*5, 100, 50, 50),
            createTestBox(500 + i*5, 200, 50, 50)  // 新目标
        };
        tracker.update(det);
    }
    int countAfterSecond = tracker.getUniqueCount();
    
    // 验证：应该有3个不重复目标（2个原始 + 1个新）
    bool passed = (countAfterFirst == 2) && (countAfterSecond == 3);
    
    printTestResult("ByteTracker 新目标计数", passed);
}

/**
 * @brief 测试重置功能
 */
void testByteTrackerReset() {
    ByteTrackerConfig config;
    ByteTracker tracker(config);
    
    // 添加一些目标
    std::vector<DetectionBox> detections = {
        createTestBox(100, 100, 50, 50),
        createTestBox(200, 100, 50, 50)
    };
    tracker.update(detections);
    
    bool hasData = (tracker.getUniqueCount() > 0);
    
    // 重置
    tracker.reset();
    
    bool afterReset = (tracker.getUniqueCount() == 0) &&
                      (tracker.getActiveCount() == 0) &&
                      (tracker.getFrameId() == 0);
    
    printTestResult("ByteTracker 重置", hasData && afterReset);
}

// ==================== SmartZoomCaptureStrategy 测试 ====================

/**
 * @brief 测试策略初始化
 */
void testStrategyInit() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 3;
    config.stableFrameCount = 5;
    
    SmartZoomCaptureStrategy strategy(config);
    
    bool passed = (strategy.getState() == CaptureState::CRUISING) &&
                  (strategy.getStableCount() == 0);
    
    printTestResult("SmartZoomCaptureStrategy 初始化", passed);
}

/**
 * @brief 测试状态转换：CRUISING → TRACKING
 */
void testStateCruisingToTracking() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 2;
    config.stableFrameCount = 3;
    config.minConfidence = 0.5f;
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    // 检测到2个高置信度目标
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 50, 50, 0.8f, 1),
        createTrackedObject(200, 100, 50, 50, 0.9f, 2)
    };
    
    // 处理一帧
    strategy.process(targets, frameSize, gps);
    
    bool passed = (strategy.getState() == CaptureState::TRACKING) &&
                  (strategy.getStableCount() == 1);
    
    printTestResult("状态转换 CRUISING → TRACKING", passed);
}

/**
 * @brief 测试状态转换：TRACKING → CAPTURING
 */
void testStateTrackingToCapturing() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 2;
    config.stableFrameCount = 3;
    config.minConfidence = 0.5f;
    config.cooldownSeconds = 0.0;  // 禁用冷却便于测试
    config.cooldownDistanceMeters = 0.0;
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 50, 50, 0.8f, 1),
        createTrackedObject(200, 100, 50, 50, 0.9f, 2)
    };
    
    // 连续处理多帧直到稳定
    bool shouldCapture = false;
    for (int i = 0; i < 5; i++) {
        shouldCapture = strategy.process(targets, frameSize, gps);
        if (shouldCapture) break;
    }
    
    bool passed = shouldCapture &&
                  (strategy.getState() == CaptureState::CAPTURING);
    
    printTestResult("状态转换 TRACKING → CAPTURING", passed);
}

/**
 * @brief 测试时间冷却
 * 
 * 策略：触发拍照后调用 onCaptureComplete，进入 COOLDOWN 状态
 * 在冷却期间再次检测到目标不会触发新的拍照
 * 
 * 📌 状态机行为说明：
 * - 第1帧：CRUISING → TRACKING (stableCount=1)
 * - 第2帧：TRACKING 中检查 stableCount >= stableFrameCount，触发拍照
 */
void testTimeCooldown() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 1;
    config.stableFrameCount = 1;
    config.cooldownSeconds = 10.0;  // 10秒冷却（测试期间不会过期）
    config.cooldownDistanceMeters = 0.0;  // 禁用距离冷却
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 50, 50, 0.8f, 1)
    };
    
    // 第一帧：CRUISING → TRACKING
    strategy.process(targets, frameSize, gps);
    
    // 第二帧：应该触发拍照
    bool first = strategy.process(targets, frameSize, gps);
    
    if (first) {
        // 完成拍照，进入冷却
        strategy.onCaptureComplete(true);
    }
    
    // 再次处理（应该不会触发新拍照，因为在冷却中）
    bool second = strategy.process(targets, frameSize, gps);
    
    // 验证：首次触发成功，第二次不触发
    bool passed = first && !second;
    
    printTestResult("时间冷却机制", passed);
}

/**
 * @brief 测试 GPS 距离冷却
 * 
 * 📌 Haversine 公式测试
 * 当距离上次拍照位置小于 cooldownDistanceMeters 时，不触发新的拍照
 * 
 * 📌 状态机行为说明：
 * - 第1帧：CRUISING → TRACKING (stableCount=1)
 * - 第2帧：TRACKING 中检查 stableCount >= stableFrameCount，触发拍照
 */
void testDistanceCooldown() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 1;
    config.stableFrameCount = 1;
    config.cooldownSeconds = 0.0;  // 禁用时间冷却
    config.cooldownDistanceMeters = 100.0;  // 100米距离冷却
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 50, 50, 0.8f, 1)
    };
    
    // 位置1：进入 TRACKING 状态
    GpsInfo gps1{22.500000, 113.900000, 50.0};
    strategy.process(targets, frameSize, gps1);
    
    // 位置1：触发第一次拍照
    bool first = strategy.process(targets, frameSize, gps1);
    if (first) {
        strategy.onCaptureComplete(true);
    }
    
    // 位置2：非常近的位置（约 10 米，应该被冷却）
    // 在纬度 22 度时，经度差 0.0001 约等于 10 米
    GpsInfo gps2{22.500000, 113.900100, 50.0};
    // 需要再处理两帧才能触发拍照请求
    strategy.process(targets, frameSize, gps2);  // COOLDOWN → COOLDOWN (因为距离不够)
    bool second = strategy.process(targets, frameSize, gps2);
    
    // 验证：第一次成功触发，第二次因距离冷却不触发
    bool passed = first && !second;
    
    printTestResult("GPS 距离冷却机制", passed);
}

/**
 * @brief 测试目标过滤（低置信度）
 */
void testTargetFilterLowConfidence() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 2;
    config.minConfidence = 0.6f;  // 置信度阈值 60%
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    // 3个目标，但只有1个超过阈值
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 50, 50, 0.8f, 1),   // 有效
        createTrackedObject(200, 100, 50, 50, 0.4f, 2),   // 低置信度
        createTrackedObject(300, 100, 50, 50, 0.5f, 3)    // 低置信度
    };
    
    strategy.process(targets, frameSize, gps);
    
    // 只有1个有效目标，不满足 minTargetCount=2，应保持 CRUISING
    bool passed = (strategy.getState() == CaptureState::CRUISING);
    
    printTestResult("目标过滤（低置信度）", passed);
}

/**
 * @brief 测试目标过滤（小面积）
 */
void testTargetFilterSmallArea() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 2;
    config.minBoxArea = 1000;  // 最小面积 1000 像素
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    // 3个目标，但只有1个面积足够
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 100, 100, 0.8f, 1),  // 面积 10000，有效
        createTrackedObject(200, 100, 10, 10, 0.8f, 2),    // 面积 100，太小
        createTrackedObject(300, 100, 20, 20, 0.8f, 3)     // 面积 400，太小
    };
    
    strategy.process(targets, frameSize, gps);
    
    // 只有1个有效目标，应保持 CRUISING
    bool passed = (strategy.getState() == CaptureState::CRUISING);
    
    printTestResult("目标过滤（小面积）", passed);
}

/**
 * @brief 测试拍照请求包含正确的目标区域
 * 
 * 📌 状态机行为说明：
 * - 第1帧：CRUISING → TRACKING (stableCount=1)
 * - 第2帧：TRACKING 中检查 stableCount >= stableFrameCount，触发拍照
 * 
 * 因此 stableFrameCount=1 时需要处理 2 帧才能触发
 */
void testCaptureRequestBoundingRegion() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 2;
    config.stableFrameCount = 1;
    config.cooldownSeconds = 0.0;
    config.cooldownDistanceMeters = 0.0;  // 禁用所有冷却
    config.regionPadding = 0.1f;  // 10% padding
    config.minConfidence = 0.5f;
    config.minBoxArea = 100;
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    // 两个目标：
    // 目标1: (100,100) 大小 50x50 -> 覆盖 [100,150] x [100,150]
    // 目标2: (200,200) 大小 50x50 -> 覆盖 [200,250] x [200,250]
    // 合并后包围框: [100,250] x [100,250] -> 宽高各 150
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 50, 50, 0.8f, 1),
        createTrackedObject(200, 200, 50, 50, 0.8f, 2)
    };
    
    // 第一帧：CRUISING → TRACKING
    strategy.process(targets, frameSize, gps);
    
    // 第二帧：TRACKING 状态，应该触发拍照
    bool shouldCapture = strategy.process(targets, frameSize, gps);
    
    bool passed = false;
    if (shouldCapture) {
        auto request = strategy.getCaptureRequest();
        
        // 验证目标数量
        passed = (request.targetCount == 2);
        
        // 验证包围框合理性（考虑 padding）
        // 原始范围 [100,250] x [100,250]，宽高 150
        // padding 10% 后，左右各扩展 15 像素
        passed = passed && (request.targetRegion.width >= 150);
        passed = passed && (request.targetRegion.height >= 150);
    }
    
    printTestResult("拍照请求包围区域", passed);
}

/**
 * @brief 测试策略重置
 */
void testStrategyReset() {
    SmartZoomCaptureConfig config;
    config.minTargetCount = 1;
    config.stableFrameCount = 1;
    
    SmartZoomCaptureStrategy strategy(config);
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    std::vector<TrackedObject> targets = {
        createTrackedObject(100, 100, 50, 50, 0.8f, 1)
    };
    
    // 触发状态变化
    strategy.process(targets, frameSize, gps);
    
    bool stateChanged = (strategy.getState() != CaptureState::CRUISING);
    
    // 重置
    strategy.reset();
    
    bool afterReset = (strategy.getState() == CaptureState::CRUISING) &&
                      (strategy.getStableCount() == 0);
    
    printTestResult("策略重置", stateChanged && afterReset);
}

// ==================== 集成测试 ====================

/**
 * @brief 测试 ByteTracker + Strategy 集成
 */
void testIntegration() {
    // 配置
    ByteTrackerConfig trackerConfig;
    trackerConfig.trackThresh = 0.5f;
    
    SmartZoomCaptureConfig strategyConfig;
    strategyConfig.minTargetCount = 2;
    strategyConfig.stableFrameCount = 3;
    strategyConfig.cooldownSeconds = 0.0;
    
    ByteTracker tracker(trackerConfig);
    SmartZoomCaptureStrategy strategy(strategyConfig);
    
    cv::Size frameSize(1920, 1080);
    GpsInfo gps{22.5, 113.9, 50.0};
    
    bool captureTriggered = false;
    
    // 模拟5帧
    for (int frame = 0; frame < 5; frame++) {
        // 模拟检测结果
        std::vector<DetectionBox> detections = {
            createTestBox(100 + frame * 5, 100, 50, 50),
            createTestBox(200 + frame * 5, 100, 50, 50)
        };
        
        // 追踪
        auto trackedObjects = tracker.update(detections);
        
        // 策略处理
        if (strategy.process(trackedObjects, frameSize, gps)) {
            captureTriggered = true;
            (void)strategy.getCaptureRequest();  // 获取请求（实际使用中会处理）
            strategy.onCaptureComplete(true);
        }
    }
    
    bool passed = captureTriggered && (tracker.getUniqueCount() == 2);
    
    printTestResult("Tracker + Strategy 集成", passed);
}

// ==================== 主函数 ====================

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  ByteTracker & Strategy 单元测试" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // ByteTracker 测试
    std::cout << "--- ByteTracker 测试 ---\n" << std::endl;
    testByteTrackerInit();
    testByteTrackerSingleTarget();
    testByteTrackerMultiTarget();
    testByteTrackerTargetLoss();
    testByteTrackerNewTargetCount();
    testByteTrackerReset();
    
    // SmartZoomCaptureStrategy 测试
    std::cout << "\n--- SmartZoomCaptureStrategy 测试 ---\n" << std::endl;
    testStrategyInit();
    testStateCruisingToTracking();
    testStateTrackingToCapturing();
    testTimeCooldown();
    testDistanceCooldown();
    testTargetFilterLowConfidence();
    testTargetFilterSmallArea();
    testCaptureRequestBoundingRegion();
    testStrategyReset();
    
    // 集成测试
    std::cout << "\n--- 集成测试 ---\n" << std::endl;
    testIntegration();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  测试完成" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    return 0;
}
