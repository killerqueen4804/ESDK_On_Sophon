//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-DEMO is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#ifndef BYTETRACK_STRACK_H
#define BYTETRACK_STRACK_H

#include "kalmanfilter.h"
#include <vector>
#include <memory>

namespace bytetrack {

/**
 * @brief 追踪状态枚举
 */
enum TrackState { 
    New = 0,      ///< 新目标
    Tracked,      ///< 正在追踪
    Lost,         ///< 丢失
    Removed       ///< 已移除
};

/**
 * @brief 单目标追踪状态类
 * 
 * 维护单个目标的追踪状态、卡尔曼滤波状态、边界框等信息
 */
class STrack {
public:
    /**
     * @brief 构造函数
     * @param tlwh 边界框 [top, left, width, height]
     * @param score 置信度
     * @param class_id 类别ID
     */
    STrack(std::vector<float> tlwh, float score, int class_id);
    ~STrack();

    // 静态工具方法
    static std::vector<float> tlbr_to_tlwh(std::vector<float>& tlbr);
    static void multi_predict(std::vector<std::shared_ptr<STrack>>& stracks,
                              std::shared_ptr<KalmanFilter> kalman_filter);

    // 坐标转换
    void static_tlwh();
    void static_tlbr();
    std::vector<float> tlwh_to_xyah(std::vector<float> tlwh_tmp);
    std::vector<float> to_xyah();

    // 状态管理
    void mark_lost();
    void mark_removed();
    int next_id();
    int end_frame();

    // 追踪操作
    void activate(std::shared_ptr<KalmanFilter> kalman_filter, int frame_id);
    void re_activate(std::shared_ptr<KalmanFilter> kalman_filter,
                     std::shared_ptr<STrack> new_track, int frame_id,
                     bool new_id = false);
    void update(std::shared_ptr<KalmanFilter> kalman_filter,
                std::shared_ptr<STrack> new_track, int frame_id);

public:
    bool is_activated;      ///< 是否已激活
    int track_id;           ///< 追踪ID
    int state;              ///< 追踪状态

    std::vector<float> _tlwh;   ///< 原始边界框
    std::vector<float> tlwh;    ///< 当前边界框 [top, left, width, height]
    std::vector<float> tlbr;    ///< 当前边界框 [top, left, bottom, right]
    
    int frame_id;           ///< 当前帧ID
    int tracklet_len;       ///< 追踪长度
    int start_frame;        ///< 起始帧

    cv::Mat mean;           ///< 卡尔曼滤波均值
    cv::Mat covariance;     ///< 卡尔曼滤波协方差
    float score;            ///< 置信度
    int class_id;           ///< 类别ID
};

using STracks = std::vector<std::shared_ptr<STrack>>;

}  // namespace bytetrack

#endif  // BYTETRACK_STRACK_H
