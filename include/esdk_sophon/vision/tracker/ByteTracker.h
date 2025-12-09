/**
 * @file ByteTracker.h
 * @brief ByteTrack 追踪器封装类
 * 
 * 封装 third_party/bytetrack 中的 ByteTrack 实现，
 * 直接使用项目已有的 DetectionBox 类型，无需额外转换
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-01
 */

#ifndef ESDK_SOPHON_VISION_TRACKER_BYTETRACKER_H_
#define ESDK_SOPHON_VISION_TRACKER_BYTETRACKER_H_

#include <memory>
#include <vector>
#include <set>
#include <opencv2/core.hpp>
#include "esdk_sophon/vision/IDetector.h"  // 使用项目已有的 DetectionBox

namespace esdk_sophon {
namespace vision {
namespace tracker {

/**
 * @brief 追踪目标 - 在 DetectionBox 基础上增加追踪信息
 * 
 * 继承自 DetectionBox，复用已有的检测结果字段，
 * 并添加追踪特有的 trackId 和 isNew 字段
 */
struct TrackedObject : public DetectionBox {
    int trackId = -1;     ///< 唯一追踪 ID（由 ByteTrack 分配）
    bool isNew = false;   ///< 是否为新目标（本帧首次出现）
    int trackletLen = 0;  ///< 追踪长度（连续追踪的帧数）
    
    TrackedObject() = default;
    
    /**
     * @brief 从 DetectionBox 构造
     */
    explicit TrackedObject(const DetectionBox& box) 
        : DetectionBox(box), trackId(-1), isNew(false), trackletLen(0) {}
};

/**
 * @brief ByteTracker 配置参数
 */
struct ByteTrackerConfig {
    float trackThresh = 0.5f;      ///< 高置信度追踪阈值
    float matchThresh = 0.8f;      ///< 匹配阈值（IoU）
    int frameRate = 30;            ///< 视频帧率
    int trackBuffer = 30;          ///< 追踪缓冲帧数（丢失后保持多少帧）
    int minBoxArea = 100;          ///< 最小目标面积（像素²）
};

/**
 * @brief ByteTrack 追踪器封装类
 * 
 * 基于论文 "ByteTrack: Multi-Object Tracking by Associating Every Detection Box"
 * 
 * 核心特性：
 * - 使用高低置信度两阶段匹配，提高召回率
 * - 卡尔曼滤波预测目标位置
 * - IoU 距离 + 匈牙利算法进行数据关联
 * 
 * 使用示例：
 * @code
 * ByteTrackerConfig config;
 * config.trackThresh = 0.5f;
 * config.trackBuffer = 30;
 * ByteTracker tracker(config);
 * 
 * // 检测
 * DetectionResult detResult = detector->detect(frame);
 * 
 * // 追踪（直接传入检测结果的 boxes）
 * auto trackedObjects = tracker.update(detResult.boxes);
 * 
 * // 获取统计信息
 * int uniqueCount = tracker.getUniqueCount();  // 不重复目标总数
 * int activeCount = tracker.getActiveCount();  // 当前活跃目标数
 * @endcode
 */
class ByteTracker {
public:
    /**
     * @brief 构造函数
     * @param config 追踪器配置
     */
    explicit ByteTracker(const ByteTrackerConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~ByteTracker();
    
    // 禁止拷贝
    ByteTracker(const ByteTracker&) = delete;
    ByteTracker& operator=(const ByteTracker&) = delete;
    
    // 允许移动
    ByteTracker(ByteTracker&&) noexcept;
    ByteTracker& operator=(ByteTracker&&) noexcept;

    /**
     * @brief 更新追踪状态
     * @param detections 当前帧的检测结果（直接使用 DetectionBox）
     * @return 追踪后的目标列表（包含 Track ID）
     */
    std::vector<TrackedObject> update(const std::vector<DetectionBox>& detections);

    /**
     * @brief 获取不重复目标总数
     * @return 从任务开始到现在出现过的不重复目标数量
     * 
     * @note 这是去重后的计数，用于最终统计
     */
    int getUniqueCount() const;

    /**
     * @brief 获取当前活跃目标数
     * @return 当前帧中正在追踪的目标数量
     */
    int getActiveCount() const;
    
    /**
     * @brief 获取当前帧ID
     * @return 从追踪开始到现在的帧数
     */
    int getFrameId() const;

    /**
     * @brief 重置追踪器状态
     * 
     * 清空所有追踪目标和统计信息，用于任务重新开始
     */
    void reset();

private:
    class Impl;  // Pimpl 模式，隐藏 ByteTrack 实现细节
    std::unique_ptr<Impl> impl_;
};

}  // namespace tracker
}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_TRACKER_BYTETRACKER_H_
