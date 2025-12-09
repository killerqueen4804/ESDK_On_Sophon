/**
 * @file IZoomCaptureStrategy.h
 * @brief 变焦拍照策略接口
 * 
 * 定义变焦拍照的触发条件和参数计算的抽象接口，
 * 使用策略模式便于扩展不同的拍照策略
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-01
 */

#ifndef ESDK_SOPHON_VISION_STRATEGY_IZOOMCAPTURESTRATEGY_H_
#define ESDK_SOPHON_VISION_STRATEGY_IZOOMCAPTURESTRATEGY_H_

#include <vector>
#include <cstdint>
#include <opencv2/core.hpp>
#include "esdk_sophon/vision/tracker/ByteTracker.h"

namespace esdk_sophon {
namespace vision {
namespace strategy {

/**
 * @brief GPS 位置信息
 */
struct GpsInfo {
    double latitude = 0.0;    ///< 纬度
    double longitude = 0.0;   ///< 经度
    double altitude = 0.0;    ///< 高度（米）
    
    GpsInfo() = default;
    GpsInfo(double lat, double lon, double alt = 0.0)
        : latitude(lat), longitude(lon), altitude(alt) {}
};

/**
 * @brief 变焦拍照请求
 * 
 * 当策略判断需要拍照时，生成此请求传递给执行模块
 */
struct ZoomCaptureRequest {
    cv::Rect targetRegion;      ///< 目标区域（用于框选变焦，像素坐标）
    int targetCount = 0;        ///< 区域内目标数量
    GpsInfo gps;                ///< GPS 位置
    int64_t timestamp = 0;      ///< 时间戳（毫秒）
    
    // 归一化坐标（用于 DJI API）
    float normalizedX1 = 0.0f;  ///< 归一化左上角 x [0, 1]
    float normalizedY1 = 0.0f;  ///< 归一化左上角 y [0, 1]
    float normalizedX2 = 0.0f;  ///< 归一化右下角 x [0, 1]
    float normalizedY2 = 0.0f;  ///< 归一化右下角 y [0, 1]
};

/**
 * @brief 变焦拍照策略接口
 * 
 * 策略模式：定义变焦拍照的触发条件和参数计算
 * 
 * 使用示例：
 * @code
 * // 创建策略
 * auto strategy = std::make_unique<SmartZoomCaptureStrategy>(config);
 * 
 * // 每帧调用
 * if (strategy->process(trackedObjects, frameSize, gpsInfo)) {
 *     auto request = strategy->getCaptureRequest();
 *     // 执行变焦拍照...
 *     strategy->onCaptureComplete(true);
 * }
 * @endcode
 */
class IZoomCaptureStrategy {
public:
    virtual ~IZoomCaptureStrategy() = default;

    /**
     * @brief 处理追踪结果，判断是否需要拍照
     * @param trackedObjects 当前帧追踪结果
     * @param frameSize 帧尺寸（像素）
     * @param gpsInfo GPS 信息
     * @return 是否需要触发拍照
     */
    virtual bool process(
        const std::vector<tracker::TrackedObject>& trackedObjects,
        const cv::Size& frameSize,
        const GpsInfo& gpsInfo) = 0;

    /**
     * @brief 获取拍照请求
     * @return 拍照请求（process 返回 true 后调用）
     * @note 只有在 process() 返回 true 后调用才有效
     */
    virtual ZoomCaptureRequest getCaptureRequest() const = 0;

    /**
     * @brief 通知拍照完成
     * @param success 是否成功
     * 
     * 策略根据结果更新内部状态（如进入冷却期）
     */
    virtual void onCaptureComplete(bool success) = 0;

    /**
     * @brief 重置策略状态
     * 
     * 清空所有内部状态，用于任务重新开始
     */
    virtual void reset() = 0;
    
    /**
     * @brief 获取策略名称
     */
    virtual std::string getName() const = 0;
};

}  // namespace strategy
}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_STRATEGY_IZOOMCAPTURESTRATEGY_H_
