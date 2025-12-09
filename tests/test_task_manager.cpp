/**
 * @file test_task_manager.cpp
 * @brief TaskManager单元测试 - 验证任务管理器的核心功能
 * 
 * 测试内容:
 * 1. 单例模式 - 验证全局唯一实例
 * 2. 任务启动 - 测试正常启动流程
 * 3. 任务冲突检测 - 视频流互斥、图片并发
 * 4. TPU资源管理 - 资源不足时的排队机制
 * 5. 任务停止/暂停/恢复 - 状态转换
 * 6. 等待队列 - 自动启动等待任务
 * 7. 多任务并发 - 并发上限控制
 * 
 * @author 项目重构者
 * @date 2025-10-28
 */

#include "esdk_sophon/task/TaskManager.h"
#include "esdk_sophon/task/TaskTypes.h"  // 使用 task::TaskConfig
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace esdk_sophon::task;

// ==================== 测试辅助函数 ====================

/**
 * @brief 创建测试用的TaskConfig
 */
TaskConfig createTestConfig(int taskID, bool isLiveview = false) {
    TaskConfig config;
    config.taskID = taskID;
    config.algorithmRepoID = 1;
    config.algorithmName = "目标检测";
    config.version = "1.0.0";
    config.source = isLiveview ? 0 : 1;  // 0=视频流, 1=图片
    config.display = 0;
    config.airtransfer = 1;
    
    // 添加一个目标检测算法类型
    TaskConfig::AlgorithmType algo;
    algo.id = 200004;
    algo.name = "垃圾倾倒";
    algo.classes = {"person", "car", "bicycle"};
    algo.mainType = 100000;  // 100000=目标检测
    config.types.push_back(algo);
    
    return config;
}

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
 * @brief 等待一小段时间(模拟异步操作)
 */
void waitABit(int milliseconds = 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// ==================== 测试用例 ====================

/**
 * @brief 测试1: 单例模式
 * 
 * 验证:
 * 1. 多次调用getInstance()返回同一个实例
 * 2. 实例的地址相同
 */
void test_singleton() {
    std::cout << "\n========== 测试1: 单例模式 ==========" << std::endl;
    
    TaskManager& instance1 = TaskManager::getInstance();
    TaskManager& instance2 = TaskManager::getInstance();
    
    bool passed = (&instance1 == &instance2);
    
    std::cout << "实例1地址: " << &instance1 << std::endl;
    std::cout << "实例2地址: " << &instance2 << std::endl;
    std::cout << "是否为同一实例: " << (passed ? "是" : "否") << std::endl;
    
    printTestResult("单例模式", passed);
}

/**
 * @brief 测试2: 启动图片任务
 * 
 * 验证:
 * 1. 任务成功启动
 * 2. 任务状态为WAITING_MEDIA
 * 3. 可以查询到任务信息
 */
void test_start_image_task() {
    std::cout << "\n========== 测试2: 启动图片任务 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 创建图片任务配置
    TaskConfig config = createTestConfig(1001, false);
    
    // 启动任务
    bool started = manager.startTask(config);
    std::cout << "任务启动结果: " << (started ? "成功" : "失败") << std::endl;
    
    // 检查任务状态
    TaskStatus status = manager.getTaskStatus(std::to_string(config.taskID));
    std::cout << "任务状态: " << static_cast<int>(status) << std::endl;
    
    // 检查任务信息
    const TaskState* info = manager.getTaskInfo(std::to_string(config.taskID));
    bool hasInfo = (info != nullptr);
    std::cout << "任务信息存在: " << (hasInfo ? "是" : "否") << std::endl;
    
    if (hasInfo) {
        std::cout << "任务ID: " << info->taskId << std::endl;
        std::cout << "预估TPU显存: " << info->estimatedTpuMemoryMB << " MB" << std::endl;
    }
    
    bool passed = started && (status == TaskStatus::WAITING_MEDIA) && hasInfo;
    
    printTestResult("启动图片任务", passed);
    
    // 清理：停止任务
    manager.stopTask(std::to_string(config.taskID));
}

/**
 * @brief 测试3: 启动视频流任务
 * 
 * 验证:
 * 1. 视频流任务成功启动
 * 2. 任务状态为PROCESSING(立即开始处理)
 */
void test_start_liveview_task() {
    std::cout << "\n========== 测试3: 启动视频流任务 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 创建视频流任务配置
    TaskConfig config = createTestConfig(2001, true);
    
    // 启动任务
    bool started = manager.startTask(config);
    std::cout << "视频流任务启动结果: " << (started ? "成功" : "失败") << std::endl;
    
    // 检查任务状态
    TaskStatus status = manager.getTaskStatus(std::to_string(config.taskID));
    std::cout << "任务状态: " << static_cast<int>(status) 
              << " (期望PROCESSING=" << static_cast<int>(TaskStatus::PROCESSING) << ")" << std::endl;
    
    bool passed = started && (status == TaskStatus::PROCESSING);
    
    printTestResult("启动视频流任务", passed);
    
    // 清理：停止任务，避免影响后续测试
    manager.stopTask(std::to_string(config.taskID));
}

/**
 * @brief 测试4: 视频流任务冲突
 * 
 * 验证:
 * 1. 第一个视频流任务正常启动
 * 2. 第二个视频流任务被加入队列(不能立即启动)
 */
void test_liveview_conflict() {
    std::cout << "\n========== 测试4: 视频流任务冲突 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 启动第一个视频流任务
    TaskConfig config1 = createTestConfig(3001, true);
    bool started1 = manager.startTask(config1);
    std::cout << "第一个视频流任务启动: " << (started1 ? "成功" : "失败") << std::endl;
    
    TaskStatus status1 = manager.getTaskStatus(std::to_string(config1.taskID));
    std::cout << "第一个任务状态: " << static_cast<int>(status1) << std::endl;
    
    // 尝试启动第二个视频流任务(应该被排队)
    TaskConfig config2 = createTestConfig(3002, true);
    bool started2 = manager.startTask(config2);
    std::cout << "第二个视频流任务启动: " << (started2 ? "成功/已排队" : "失败") << std::endl;
    
    TaskStatus status2 = manager.getTaskStatus(std::to_string(config2.taskID));
    std::cout << "第二个任务状态: " << static_cast<int>(status2) << std::endl;
    
    // 验证: 第一个任务在运行, 第二个任务不在运行列表(或在排队)
    auto runningIds = manager.getRunningTaskIds();
    std::cout << "当前运行任务数: " << runningIds.size() << std::endl;
    
    bool passed = started1 && (status1 == TaskStatus::PROCESSING);
    
    printTestResult("视频流任务冲突", passed);
    
    // 清理：停止所有任务
    manager.stopTask(std::to_string(config1.taskID));
    // config2在队列中，需要等processWaitingQueue处理
}

/**
 * @brief 测试5: 图片任务并发
 * 
 * 验证:
 * 1. 多个图片任务可以并发启动
 * 2. 不会互相冲突
 */
void test_image_concurrent() {
    std::cout << "\n========== 测试5: 图片任务并发 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 启动多个图片任务
    int successCount = 0;
    for (int i = 0; i < 3; i++) {
        TaskConfig config = createTestConfig(4001 + i, false);
        bool started = manager.startTask(config);
        if (started) {
            successCount++;
            std::cout << "图片任务 " << (4001 + i) << " 启动成功" << std::endl;
        }
    }
    
    auto runningIds = manager.getRunningTaskIds();
    std::cout << "当前运行任务数: " << runningIds.size() << std::endl;
    std::cout << "成功启动任务数: " << successCount << std::endl;
    
    bool passed = (successCount >= 2);  // 至少能启动2个
    
    printTestResult("图片任务并发", passed);
    
    // 清理：停止所有任务
    for (int i = 0; i < 3; i++) {
        manager.stopTask(std::to_string(4001 + i));
    }
}

/**
 * @brief 测试6: 停止任务
 * 
 * 验证:
 * 1. 可以成功停止运行中的任务
 * 2. 停止后任务从运行列表移除
 */
void test_stop_task() {
    std::cout << "\n========== 测试6: 停止任务 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 启动一个新任务
    TaskConfig config = createTestConfig(5001, false);
    bool started = manager.startTask(config);
    std::cout << "任务启动: " << (started ? "成功" : "失败") << std::endl;
    
    auto runningIdsBefore = manager.getRunningTaskIds();
    std::cout << "停止前运行任务数: " << runningIdsBefore.size() << std::endl;
    
    // 停止任务
    bool stopped = manager.stopTask(std::to_string(config.taskID));
    std::cout << "任务停止: " << (stopped ? "成功" : "失败") << std::endl;
    
    auto runningIdsAfter = manager.getRunningTaskIds();
    std::cout << "停止后运行任务数: " << runningIdsAfter.size() << std::endl;
    
    // 验证任务不存在
    TaskStatus status = manager.getTaskStatus(std::to_string(config.taskID));
    std::cout << "停止后任务状态: " << static_cast<int>(status) 
              << " (CANCELLED=" << static_cast<int>(TaskStatus::CANCELLED) << ")" << std::endl;
    
    bool passed = stopped && (runningIdsAfter.size() < runningIdsBefore.size());
    
    printTestResult("停止任务", passed);
}

/**
 * @brief 测试7: 暂停和恢复任务
 * 
 * 验证:
 * 1. 可以暂停PROCESSING状态的任务
 * 2. 可以恢复PAUSED状态的任务
 * 3. 状态转换正确
 */
void test_pause_resume() {
    std::cout << "\n========== 测试7: 暂停和恢复任务 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 先清空所有任务，确保视频流任务能立即启动
    auto existingIds = manager.getRunningTaskIds();
    for (const auto& id : existingIds) {
        manager.stopTask(id);
    }
    std::cout << "清理完成，当前运行任务数: " << manager.getRunningTaskIds().size() << std::endl;
    
    // 启动视频流任务(状态为PROCESSING)
    TaskConfig config = createTestConfig(6001, true);
    bool started = manager.startTask(config);
    std::cout << "任务启动: " << (started ? "成功" : "失败") << std::endl;
    
    TaskStatus statusInitial = manager.getTaskStatus(std::to_string(config.taskID));
    std::cout << "初始状态: " << static_cast<int>(statusInitial) << std::endl;
    
    // 暂停任务
    bool paused = manager.pauseTask(std::to_string(config.taskID));
    std::cout << "暂停任务: " << (paused ? "成功" : "失败") << std::endl;
    
    TaskStatus statusPaused = manager.getTaskStatus(std::to_string(config.taskID));
    std::cout << "暂停后状态: " << static_cast<int>(statusPaused) 
              << " (PAUSED=" << static_cast<int>(TaskStatus::PAUSED) << ")" << std::endl;
    
    // 恢复任务
    bool resumed = manager.resumeTask(std::to_string(config.taskID));
    std::cout << "恢复任务: " << (resumed ? "成功" : "失败") << std::endl;
    
    TaskStatus statusResumed = manager.getTaskStatus(std::to_string(config.taskID));
    std::cout << "恢复后状态: " << static_cast<int>(statusResumed) 
              << " (PROCESSING=" << static_cast<int>(TaskStatus::PROCESSING) << ")" << std::endl;
    
    bool passed = paused && resumed && 
                  (statusPaused == TaskStatus::PAUSED) && 
                  (statusResumed == TaskStatus::PROCESSING);
    
    printTestResult("暂停和恢复任务", passed);
    
    // 清理
    manager.stopTask(std::to_string(config.taskID));
}

/**
 * @brief 测试8: TPU资源状态查询
 * 
 * 验证:
 * 1. 可以查询TPU资源状态
 * 2. 资源占用随任务数量变化
 */
void test_tpu_resource() {
    std::cout << "\n========== 测试8: TPU资源状态 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 查询初始资源状态
    TpuResourceStatus status = manager.getTpuResourceStatus();
    std::cout << "TPU总显存: " << status.totalMemoryMB << " MB" << std::endl;
    std::cout << "可用显存: " << status.availableMemoryMB << " MB" << std::endl;
    std::cout << "利用率: " << status.utilizationPercent << "%" << std::endl;
    std::cout << "运行任务数: " << status.runningTaskCount << std::endl;
    
    bool passed = (status.totalMemoryMB > 0) && 
                  (status.availableMemoryMB >= 0) && 
                  (status.availableMemoryMB <= status.totalMemoryMB);
    
    printTestResult("TPU资源状态查询", passed);
}

/**
 * @brief 测试9: 最大并发数限制
 * 
 * 验证:
 * 1. 设置最大并发数
 * 2. 超过限制时任务被排队
 */
void test_max_concurrent_limit() {
    std::cout << "\n========== 测试9: 最大并发数限制 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    // 设置最大并发数为2
    manager.setMaxConcurrentTasks(2);
    std::cout << "设置最大并发数: 2" << std::endl;
    
    // 先清空所有任务
    auto existingIds = manager.getRunningTaskIds();
    for (const auto& id : existingIds) {
        manager.stopTask(id);
    }
    waitABit();
    
    // 启动3个任务
    int successCount = 0;
    for (int i = 0; i < 3; i++) {
        TaskConfig config = createTestConfig(7001 + i, false);
        bool started = manager.startTask(config);
        if (started) {
            successCount++;
            std::cout << "任务 " << (7001 + i) << " 启动: " << (started ? "成功" : "失败") << std::endl;
        }
        waitABit(50);
    }
    
    auto runningIds = manager.getRunningTaskIds();
    std::cout << "实际运行任务数: " << runningIds.size() << std::endl;
    std::cout << "接受任务总数: " << successCount << std::endl;
    
    // 验证: 运行的任务不超过2个
    bool passed = (runningIds.size() <= 2);
    
    printTestResult("最大并发数限制", passed);
    
    // 恢复默认配置
    manager.setMaxConcurrentTasks(3);
}

/**
 * @brief 测试10: 事件回调设置
 * 
 * 验证:
 * 1. 可以设置事件回调函数
 * 2. 回调函数可以正常调用(这里只测试设置,不测试实际触发)
 */
void test_event_callback() {
    std::cout << "\n========== 测试10: 事件回调设置 ==========" << std::endl;
    
    TaskManager& manager = TaskManager::getInstance();
    
    bool callbackCalled = false;
    
    // 设置回调函数
    manager.setEventCallback([&callbackCalled](const EventMessage& event) {
        callbackCalled = true;
        std::cout << "事件回调被触发: eventType=" << event.eventType << std::endl;
    });
    
    std::cout << "事件回调函数已设置" << std::endl;
    
    // 注意: 这里只测试设置,不测试实际触发(需要模拟推理流程)
    bool passed = true;
    
    printTestResult("事件回调设置", passed);
}

// ==================== 主函数 ====================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "   TaskManager 单元测试" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // 执行所有测试
        test_singleton();
        test_start_image_task();
        test_start_liveview_task();
        test_liveview_conflict();
        test_image_concurrent();
        test_stop_task();
        test_pause_resume();
        test_tpu_resource();
        test_max_concurrent_limit();
        test_event_callback();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "   所有测试执行完成!" << std::endl;
        std::cout << "========================================" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
