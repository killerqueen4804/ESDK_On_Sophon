/**
 * @file SmartZoomCaptureStrategy.h
 * @brief 智能变焦拍照策略
 * 
 * 实现基于目标密度的智能拍照触发：
 * 1. 检测到 N 个以上目标
 * 2. 连续 M 帧保持稳定
 * 3. 距离上次拍照位置超过 D 米或时间超过 T 秒
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-01
 */

#ifndef ESDK_SOPHON_VISION_STRATEGY_SMARTZOOMCAPTURESTRATEGY_H_
#define ESDK_SOPHON_VISION_STRATEGY_SMARTZOOMCAPTURESTRATEGY_H_

#include "IZoomCaptureStrategy.h"
#include <chrono>
#include <deque>

namespace esdk_sophon {
namespace vision {
namespace strategy {

/**
 * @brief 智能变焦拍照配置
 */
struct SmartZoomCaptureConfig {
    // 触发条件
    int minTargetCount = 3;           ///< 最小目标数量
    int stableFrameCount = 5;         ///< 连续稳定帧数
    
    // 冷却机制
    double cooldownSeconds = 30.0;    ///< 时间冷却（秒）
    double cooldownDistanceMeters = 50.0;  ///< 距离冷却（米）
    
    // 目标过滤
    float minConfidence = 0.5f;       ///< 最小置信度
    int minBoxArea = 100;             ///< 最小目标面积（像素²）
    
    // 区域计算
    float regionPadding = 0.1f;       ///< 目标区域扩展比例 [0, 1]
};

/**
 * @brief 策略状态枚举
 */
enum class CaptureState {
    CRUISING,   ///< 巡航中，等待目标
    TRACKING,   ///< 追踪中，等待稳定
    CAPTURING,  ///< 拍照中
    COOLDOWN    ///< 冷却中
};

/**
 * @brief 将状态枚举转换为字符串
 */
inline std::string captureStateToString(CaptureState state) {
    switch (state) {
        case CaptureState::CRUISING:  return "CRUISING";
        case CaptureState::TRACKING:  return "TRACKING";
        case CaptureState::CAPTURING: return "CAPTURING";
        case CaptureState::COOLDOWN:  return "COOLDOWN";
        default: return "UNKNOWN";
    }
}

/**
 * @brief 智能变焦拍照策略
 * 
 * 状态机：
 * ```
 * CRUISING → (目标数>=N) → TRACKING → (稳定M帧) → CAPTURING
 *     ↑                        ↓                      ↓
 *     └──────── (冷却完成) ←── COOLDOWN ←─ (拍照完成) ─┘
 * ```
 * 
 * 使用示例：
 * @code
 * SmartZoomCaptureConfig config;
 * config.minTargetCount = 3;
 * config.stableFrameCount = 5;
 * config.cooldownSeconds = 30.0;
 * 
 * SmartZoomCaptureStrategy strategy(config);
 * 
 * // 在视频帧处理循环中
 * if (strategy.process(trackedObjects, frameSize, gpsInfo)) {
 *     auto request = strategy.getCaptureRequest();
 *     executeZoomCapture(request);  // 异步执行
 *     strategy.onCaptureComplete(true);
 * }
 * @endcode
 */
class SmartZoomCaptureStrategy : public IZoomCaptureStrategy {
public:
    /**
     * @brief 构造函数
     * @param config 策略配置
     */
    explicit SmartZoomCaptureStrategy(const SmartZoomCaptureConfig& config);
    
    ~SmartZoomCaptureStrategy() override;

    // IZoomCaptureStrategy 接口实现
    bool process(
        const std::vector<tracker::TrackedObject>& trackedObjects,
        const cv::Size& frameSize,
        const GpsInfo& gpsInfo) override;

    ZoomCaptureRequest getCaptureRequest() const override;
    void onCaptureComplete(bool success) override;
    void reset() override;
    std::string getName() const override { return "SmartZoomCaptureStrategy"; }

    // 状态查询
    CaptureState getState() const { return state_; }
    int getStableCount() const { return stableCount_; }
    const SmartZoomCaptureConfig& getConfig() const { return config_; }

private:
    /**
     * @brief 过滤有效目标
     * @param trackedObjects 追踪结果
     * @return 过滤后的目标列表
     */
    std::vector<tracker::TrackedObject> filterValidTargets(
        const std::vector<tracker::TrackedObject>& trackedObjects);

    /**
     * @brief 检查是否满足触发条件
     * @param validTargets 有效目标列表
     * @return 是否满足触发条件
     */
    bool checkTriggerCondition(
        const std::vector<tracker::TrackedObject>& validTargets);

    /**
     * @brief 检查冷却状态
     * @param gpsInfo GPS 信息
     * @return 是否冷却完成
     */
    bool checkCooldown(const GpsInfo& gpsInfo);

    /**
     * @brief 计算目标群体包围框
     * @param targets 目标列表
     * @param frameSize 帧尺寸
     * @return 包围框（已添加 padding）
     */
    cv::Rect calculateBoundingRegion(
        const std::vector<tracker::TrackedObject>& targets,
        const cv::Size& frameSize);

    /**
     * @brief 计算两个 GPS 坐标之间的距离（米）
     * 
     * 使用 Haversine 公式计算球面距离
     */
    static double calculateGpsDistance(
        double lat1, double lon1, double lat2, double lon2);

private:
    SmartZoomCaptureConfig config_;
    CaptureState state_ = CaptureState::CRUISING;
    
    // 稳定检测
    int stableCount_ = 0;
    std::deque<int> recentTargetCounts_;
    
    // 冷却管理
    std::chrono::steady_clock::time_point lastCaptureTime_;
    GpsInfo lastCaptureGps_;
    bool hasLastCapture_ = false;
    
    // 当前请求
    ZoomCaptureRequest currentRequest_;
    std::vector<tracker::TrackedObject> currentTargets_;
    
    // 📌 目标收集期：累积包围框
    // 在 TRACKING 状态期间，累积所有检测到的目标，
    // 最终拍照时使用累积的最大包围框，避免漏拍
    cv::Rect accumulatedRegion_;   ///< 累积的目标区域
    int maxTargetCount_ = 0;       ///< 收集期间最大目标数量
};

}  // namespace strategy
}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_STRATEGY_SMARTZOOMCAPTURESTRATEGY_H_
