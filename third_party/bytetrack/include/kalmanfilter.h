//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-DEMO is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#ifndef BYTETRACK_KALMANFILTER_H
#define BYTETRACK_KALMANFILTER_H

#include <opencv2/opencv.hpp>
#include <iostream>
#include <memory>
#include <vector>

namespace bytetrack {

/**
 * @brief 卡尔曼滤波器 - 用于目标状态预测
 * 
 * 使用 OpenCV 的 cv::KalmanFilter 实现，
 * 状态向量为 [cx, cy, aspect_ratio, height, vx, vy, va, vh]
 */
class KalmanFilter {
public:
    static const double chi2inv95[10];
    
    KalmanFilter();
    ~KalmanFilter();
    
    /**
     * @brief 初始化新目标的状态
     * @param measurement 测量值 [cx, cy, a, h]
     * @return <均值, 协方差>
     */
    std::pair<cv::Mat, cv::Mat> initiate(const cv::Mat& measurement);
    
    /**
     * @brief 预测下一帧状态
     */
    std::pair<cv::Mat, cv::Mat> predict(const cv::Mat& mean,
                                        const cv::Mat& covariance);
    
    /**
     * @brief 用新测量值更新状态
     */
    std::pair<cv::Mat, cv::Mat> update(const cv::Mat& mean,
                                       const cv::Mat& covariance,
                                       const cv::Mat& measurement);
    
    /**
     * @brief 计算马氏距离
     */
    cv::Mat gating_distance(const cv::Mat& mean, const cv::Mat& covariance,
                            const std::vector<cv::Mat>& measurements,
                            bool only_position = false);

private:
    std::unique_ptr<cv::KalmanFilter> opencv_kf_;
    float std_weight_position_;
    float std_weight_velocity_;
};

}  // namespace bytetrack

#endif  // BYTETRACK_KALMANFILTER_H
