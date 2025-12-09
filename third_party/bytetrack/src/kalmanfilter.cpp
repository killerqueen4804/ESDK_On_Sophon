//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-DEMO is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "kalmanfilter.h"

namespace bytetrack {

// Cholesky 分解
static void Cholesky(const cv::Mat& A, cv::Mat& S) {
    S = A.clone();
    cv::Cholesky((float*)S.ptr(), S.step, S.rows, NULL, 0, 0);
    S = S.t();
    for (int i = 1; i < S.rows; i++) {
        for (int j = 0; j < i; j++) {
            S.at<float>(i, j) = 0;
        }
    }
}

const double KalmanFilter::chi2inv95[10] = {
    0, 3.8415, 5.9915, 7.8147, 9.4877, 11.070, 12.592, 14.067, 15.507, 16.919
};

KalmanFilter::KalmanFilter() {
    std_weight_position_ = 1.0f / 20;
    std_weight_velocity_ = 1.0f / 160;

    opencv_kf_ = std::make_unique<cv::KalmanFilter>(8, 4);
    
    // 设置状态转移矩阵 (8x8)
    opencv_kf_->transitionMatrix = (cv::Mat_<float>(8, 8) << 
        1, 0, 0, 0, 1, 0, 0, 0,
        0, 1, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 0, 0, 0, 1, 0,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 1);

    // 设置测量矩阵 (4x8)
    opencv_kf_->measurementMatrix = (cv::Mat_<float>(4, 8) << 
        1, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 1, 0, 0, 0, 0);
}

KalmanFilter::~KalmanFilter() {}

std::pair<cv::Mat, cv::Mat> KalmanFilter::initiate(const cv::Mat& measurement) {
    cv::Mat mean_pos = measurement.clone();
    cv::Mat mean_vel = cv::Mat::zeros(1, 4, CV_32F);

    cv::Mat mean(1, 8, CV_32F);
    for (int i = 0; i < 8; i++) {
        if (i < 4)
            mean.at<float>(0, i) = mean_pos.at<float>(0, i);
        else
            mean.at<float>(0, i) = mean_vel.at<float>(0, i - 4);
    }

    cv::Mat std(1, 8, CV_32F);
    std.at<float>(0) = 2 * std_weight_position_ * measurement.at<float>(0, 3);
    std.at<float>(1) = 2 * std_weight_position_ * measurement.at<float>(0, 3);
    std.at<float>(2) = 1e-2f;
    std.at<float>(3) = 2 * std_weight_position_ * measurement.at<float>(0, 3);
    std.at<float>(4) = 10 * std_weight_velocity_ * measurement.at<float>(0, 3);
    std.at<float>(5) = 10 * std_weight_velocity_ * measurement.at<float>(0, 3);
    std.at<float>(6) = 1e-5f;
    std.at<float>(7) = 10 * std_weight_velocity_ * measurement.at<float>(0, 3);

    cv::Mat tmp = std.mul(std);
    cv::Mat var = cv::Mat::diag(tmp);

    return std::make_pair(mean, var);
}

std::pair<cv::Mat, cv::Mat> KalmanFilter::predict(const cv::Mat& mean,
                                                   const cv::Mat& covariance) {
    float std_pos = std_weight_position_ * mean.at<float>(3) *
                    std_weight_position_ * mean.at<float>(3);
    float std_vel = std_weight_velocity_ * mean.at<float>(3) *
                    std_weight_velocity_ * mean.at<float>(3);
    
    opencv_kf_->processNoiseCov = (cv::Mat_<float>(8, 8) << 
        std_pos, 0, 0, 0, 0, 0, 0, 0,
        0, std_pos, 0, 0, 0, 0, 0, 0,
        0, 0, 1e-4f, 0, 0, 0, 0, 0,
        0, 0, 0, std_pos, 0, 0, 0, 0,
        0, 0, 0, 0, std_vel, 0, 0, 0,
        0, 0, 0, 0, 0, std_vel, 0, 0,
        0, 0, 0, 0, 0, 0, 1e-10f, 0,
        0, 0, 0, 0, 0, 0, 0, std_vel);
    
    opencv_kf_->statePost = mean.t();
    opencv_kf_->errorCovPost = covariance;

    opencv_kf_->predict();

    return std::make_pair(opencv_kf_->statePost.t(), opencv_kf_->errorCovPost);
}

std::pair<cv::Mat, cv::Mat> KalmanFilter::update(const cv::Mat& mean,
                                                  const cv::Mat& covariance,
                                                  const cv::Mat& measurement) {
    opencv_kf_->statePre = mean.t();
    opencv_kf_->errorCovPre = covariance;
    
    float std_pos = std_weight_position_ * mean.at<float>(3) *
                    std_weight_position_ * mean.at<float>(3);
    opencv_kf_->measurementNoiseCov = (cv::Mat_<float>(4, 4) << 
        std_pos, 0, 0, 0,
        0, std_pos, 0, 0,
        0, 0, 1e-2f, 0,
        0, 0, 0, std_pos);

    opencv_kf_->correct(measurement.t());

    return std::make_pair(opencv_kf_->statePost.t(), opencv_kf_->errorCovPost);
}

cv::Mat KalmanFilter::gating_distance(const cv::Mat& mean,
                                       const cv::Mat& covariance,
                                       const std::vector<cv::Mat>& measurements,
                                       bool only_position) {
    if (only_position) {
        // 暂不实现仅位置的马氏距离
        return cv::Mat();
    }

    cv::Mat std(1, 4, CV_32F);
    std.at<float>(0) = std_weight_position_ * mean.at<float>(3);
    std.at<float>(1) = std_weight_position_ * mean.at<float>(3);
    std.at<float>(2) = 1e-1f;
    std.at<float>(3) = std_weight_position_ * mean.at<float>(3);

    cv::Mat mean1 = opencv_kf_->measurementMatrix * mean.t();
    cv::Mat covariance1 = opencv_kf_->measurementMatrix * covariance *
                          opencv_kf_->measurementMatrix.t();

    cv::Mat diag = cv::Mat::zeros(4, 4, CV_32F);
    diag.at<float>(0, 0) = std.at<float>(0) * std.at<float>(0);
    diag.at<float>(1, 1) = std.at<float>(1) * std.at<float>(1);
    diag.at<float>(2, 2) = std.at<float>(2) * std.at<float>(2);
    diag.at<float>(3, 3) = std.at<float>(3) * std.at<float>(3);

    covariance1 += diag;

    cv::Mat d(static_cast<int>(measurements.size()), 4, CV_32F);
    int pos = 0;
    for (const auto& box : measurements) {
        cv::Mat diff = box - mean1.t();
        diff.copyTo(d.row(pos++));
    }

    cv::Mat factor;
    Cholesky(covariance1, factor);

    cv::Mat cvZ = factor.inv(cv::DECOMP_CHOLESKY) * d.t();
    cv::Mat cvZZ = cvZ.mul(cvZ);
    cv::Mat cvSquareMaha = cv::Mat::zeros(1, cvZZ.cols, CV_32F);
    cv::reduce(cvZZ, cvSquareMaha, 0, cv::REDUCE_SUM);

    return cvSquareMaha;
}

}  // namespace bytetrack
