//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-DEMO is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#ifndef BYTETRACK_BYTETRACKER_H
#define BYTETRACK_BYTETRACKER_H

#include "lapjv.h"
#include "strack.h"
#include <vector>
#include <map>
#include <memory>

namespace bytetrack {

/**
 * @brief 检测框结构体 - ByteTrack 内部使用
 * 
 * 与项目的 DetectionBox 对应，用于内部计算
 */
struct ByteTrackBox {
    float x;        ///< 左上角 x
    float y;        ///< 左上角 y
    float width;    ///< 宽度
    float height;   ///< 高度
    float score;    ///< 置信度
    int class_id;   ///< 类别ID
};

/**
 * @brief ByteTrack 参数配置
 */
struct ByteTrackParams {
    float conf_thresh = 0.5f;     ///< 检测置信度阈值（未使用，由检测器控制）
    float nms_thresh = 0.5f;      ///< NMS阈值（未使用，由检测器控制）
    float track_thresh = 0.5f;    ///< 追踪高置信度阈值
    float match_thresh = 0.8f;    ///< 匹配阈值
    int frame_rate = 30;          ///< 帧率
    int track_buffer = 30;        ///< 追踪缓冲帧数
    int min_box_area = 100;       ///< 最小目标面积
};

/**
 * @brief ByteTrack 多目标追踪器
 * 
 * 实现论文: "ByteTrack: Multi-Object Tracking by Associating Every Detection Box"
 * 
 * 核心思想：
 * 1. 使用高置信度检测框进行第一次匹配
 * 2. 使用低置信度检测框进行第二次匹配（恢复被遮挡的目标）
 * 3. 使用卡尔曼滤波预测目标位置
 * 4. 使用 IoU 距离和匈牙利算法进行数据关联
 */
class BYTETracker {
public:
    explicit BYTETracker(const ByteTrackParams& params);
    ~BYTETracker();

    /**
     * @brief 更新追踪状态
     * @param output_stracks 输出的追踪结果
     * @param objects 当前帧检测结果
     */
    void update(STracks& output_stracks, const std::vector<ByteTrackBox>& objects);

    /**
     * @brief 获取当前帧ID
     */
    int getFrameId() const { return frame_id_; }

private:
    // 追踪列表操作
    void joint_stracks(STracks& tlista, STracks& tlistb, STracks& results);
    void sub_stracks(STracks& tlista, STracks& tlistb);
    void remove_duplicate_stracks(STracks& resa, STracks& resb, 
                                  STracks& stracksa, STracks& stracksb);

    // 匹配算法
    void linear_assignment(std::vector<std::vector<float>>& cost_matrix,
                          int cost_matrix_size, int cost_matrix_size_size,
                          float thresh, std::vector<std::vector<int>>& matches,
                          std::vector<int>& unmatched_a,
                          std::vector<int>& unmatched_b);

    // IoU 距离计算
    void iou_distance(const STracks& atracks, const STracks& btracks,
                     std::vector<std::vector<float>>& cost_matrix);
    void ious(std::vector<std::vector<float>>& atlbrs,
             std::vector<std::vector<float>>& btlbrs,
             std::vector<std::vector<float>>& results);

    // 匈牙利算法封装
    void lapjv(const std::vector<std::vector<float>>& cost,
              std::vector<int>& rowsol, std::vector<int>& colsol,
              bool extend_cost = false, float cost_limit = LONG_MAX,
              bool return_cost = true);

private:
    float track_thresh_;
    float match_thresh_;
    int frame_rate_;
    int track_buffer_;
    int min_box_area_;
    int frame_id_;
    int max_time_lost_;

    STracks tracked_stracks_;
    STracks lost_stracks_;
    STracks removed_stracks_;

    std::shared_ptr<KalmanFilter> kalman_filter_;
};

}  // namespace bytetrack

#endif  // BYTETRACK_BYTETRACKER_H
