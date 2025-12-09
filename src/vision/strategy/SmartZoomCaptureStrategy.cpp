/**
 * @file SmartZoomCaptureStrategy.cpp
 * @brief 智能变焦拍照策略实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-01
 */

#include "esdk_sophon/vision/strategy/SmartZoomCaptureStrategy.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace esdk_sophon {
namespace vision {
namespace strategy {

// 地球半径（米）
static constexpr double EARTH_RADIUS_METERS = 6371000.0;

SmartZoomCaptureStrategy::SmartZoomCaptureStrategy(const SmartZoomCaptureConfig& config)
    : config_(config)
    , state_(CaptureState::CRUISING)
    , stableCount_(0)
    , hasLastCapture_(false) {
}

SmartZoomCaptureStrategy::~SmartZoomCaptureStrategy() = default;

bool SmartZoomCaptureStrategy::process(
    const std::vector<tracker::TrackedObject>& trackedObjects,
    const cv::Size& frameSize,
    const GpsInfo& gpsInfo) {
    
    // 过滤有效目标
    auto validTargets = filterValidTargets(trackedObjects);
    
    // 记录最近的目标数量（用于稳定性检测）
    recentTargetCounts_.push_back(static_cast<int>(validTargets.size()));
    if (recentTargetCounts_.size() > static_cast<size_t>(config_.stableFrameCount * 2)) {
        recentTargetCounts_.pop_front();
    }
    
    // 状态机处理
    switch (state_) {
        case CaptureState::CRUISING: {
            // 检查是否有足够目标
            if (checkTriggerCondition(validTargets)) {
                state_ = CaptureState::TRACKING;
                stableCount_ = 1;
                currentTargets_ = validTargets;
                
                // 📌 开始收集期：初始化累积包围框
                accumulatedRegion_ = calculateBoundingRegion(validTargets, frameSize);
                maxTargetCount_ = static_cast<int>(validTargets.size());
            }
            break;
        }
        
        case CaptureState::TRACKING: {
            // 检查目标是否稳定
            if (checkTriggerCondition(validTargets)) {
                stableCount_++;
                currentTargets_ = validTargets;
                
                // 📌 累积包围框：合并当前帧的包围框
                cv::Rect currentRegion = calculateBoundingRegion(validTargets, frameSize);
                if (accumulatedRegion_.area() > 0) {
                    // 计算两个矩形的并集
                    int x1 = std::min(accumulatedRegion_.x, currentRegion.x);
                    int y1 = std::min(accumulatedRegion_.y, currentRegion.y);
                    int x2 = std::max(accumulatedRegion_.x + accumulatedRegion_.width,
                                      currentRegion.x + currentRegion.width);
                    int y2 = std::max(accumulatedRegion_.y + accumulatedRegion_.height,
                                      currentRegion.y + currentRegion.height);
                    accumulatedRegion_ = cv::Rect(x1, y1, x2 - x1, y2 - y1);
                } else {
                    accumulatedRegion_ = currentRegion;
                }
                
                // 📌 记录最大目标数量
                maxTargetCount_ = std::max(maxTargetCount_, static_cast<int>(validTargets.size()));
                
                // 达到稳定帧数，触发拍照
                if (stableCount_ >= config_.stableFrameCount) {
                    // 检查冷却
                    if (!hasLastCapture_ || checkCooldown(gpsInfo)) {
                        // 📌 使用累积的包围框，而不是当前帧的
                        currentRequest_.targetRegion = accumulatedRegion_;
                        currentRequest_.targetCount = maxTargetCount_;
                        currentRequest_.gps = gpsInfo;
                        currentRequest_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        
                        // 计算归一化坐标
                        currentRequest_.normalizedX1 = static_cast<float>(currentRequest_.targetRegion.x) / frameSize.width;
                        currentRequest_.normalizedY1 = static_cast<float>(currentRequest_.targetRegion.y) / frameSize.height;
                        currentRequest_.normalizedX2 = static_cast<float>(currentRequest_.targetRegion.x + currentRequest_.targetRegion.width) / frameSize.width;
                        currentRequest_.normalizedY2 = static_cast<float>(currentRequest_.targetRegion.y + currentRequest_.targetRegion.height) / frameSize.height;
                        
                        // 限制在 [0, 1] 范围内
                        currentRequest_.normalizedX1 = std::max(0.0f, std::min(1.0f, currentRequest_.normalizedX1));
                        currentRequest_.normalizedY1 = std::max(0.0f, std::min(1.0f, currentRequest_.normalizedY1));
                        currentRequest_.normalizedX2 = std::max(0.0f, std::min(1.0f, currentRequest_.normalizedX2));
                        currentRequest_.normalizedY2 = std::max(0.0f, std::min(1.0f, currentRequest_.normalizedY2));
                        
                        state_ = CaptureState::CAPTURING;
                        return true;  // 触发拍照
                    } else {
                        // 在冷却中，继续巡航
                        state_ = CaptureState::COOLDOWN;
                        stableCount_ = 0;
                    }
                }
            } else {
                // 目标不足，返回巡航状态
                state_ = CaptureState::CRUISING;
                stableCount_ = 0;
                
                // 📌 重置累积包围框
                accumulatedRegion_ = cv::Rect();
                maxTargetCount_ = 0;
            }
            break;
        }
        
        case CaptureState::CAPTURING: {
            // 等待 onCaptureComplete 调用
            break;
        }
        
        case CaptureState::COOLDOWN: {
            // 检查冷却是否完成
            if (checkCooldown(gpsInfo)) {
                state_ = CaptureState::CRUISING;
                stableCount_ = 0;
            }
            break;
        }
    }
    
    return false;
}

ZoomCaptureRequest SmartZoomCaptureStrategy::getCaptureRequest() const {
    return currentRequest_;
}

void SmartZoomCaptureStrategy::onCaptureComplete(bool success) {
    if (success) {
        // 记录本次拍照信息
        lastCaptureTime_ = std::chrono::steady_clock::now();
        lastCaptureGps_ = currentRequest_.gps;
        hasLastCapture_ = true;
    }
    
    // 进入冷却状态
    state_ = CaptureState::COOLDOWN;
    stableCount_ = 0;
    
    // 📌 重置累积包围框，为下次收集做准备
    accumulatedRegion_ = cv::Rect();
    maxTargetCount_ = 0;
}

void SmartZoomCaptureStrategy::reset() {
    state_ = CaptureState::CRUISING;
    stableCount_ = 0;
    recentTargetCounts_.clear();
    hasLastCapture_ = false;
    currentTargets_.clear();
    currentRequest_ = ZoomCaptureRequest();
    
    // 📌 重置累积包围框
    accumulatedRegion_ = cv::Rect();
    maxTargetCount_ = 0;
}

std::vector<tracker::TrackedObject> SmartZoomCaptureStrategy::filterValidTargets(
    const std::vector<tracker::TrackedObject>& trackedObjects) {
    
    std::vector<tracker::TrackedObject> validTargets;
    validTargets.reserve(trackedObjects.size());
    
    for (const auto& obj : trackedObjects) {
        // 过滤条件：
        // 1. 置信度 >= 阈值
        // 2. 面积 >= 最小面积
        // 3. trackId 有效
        if (obj.confidence >= config_.minConfidence &&
            obj.width * obj.height >= config_.minBoxArea &&
            obj.trackId > 0) {
            validTargets.push_back(obj);
        }
    }
    
    return validTargets;
}

bool SmartZoomCaptureStrategy::checkTriggerCondition(
    const std::vector<tracker::TrackedObject>& validTargets) {
    
    return static_cast<int>(validTargets.size()) >= config_.minTargetCount;
}

bool SmartZoomCaptureStrategy::checkCooldown(const GpsInfo& gpsInfo) {
    if (!hasLastCapture_) {
        return true;  // 没有历史记录，无需冷却
    }
    
    // 检查时间冷却（只有当 cooldownSeconds > 0 时才检查）
    if (config_.cooldownSeconds > 0) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
            now - lastCaptureTime_).count();
        
        if (elapsed < config_.cooldownSeconds) {
            // 时间冷却未完成
            // 如果没有启用距离冷却，返回 false（仍在冷却中）
            if (config_.cooldownDistanceMeters <= 0) {
                return false;
            }
            // 否则继续检查距离冷却
        } else {
            return true;  // 时间冷却完成
        }
    }
    
    // 检查距离冷却（只有当 cooldownDistanceMeters > 0 时才检查）
    if (config_.cooldownDistanceMeters > 0) {
        double distance = calculateGpsDistance(
            lastCaptureGps_.latitude, lastCaptureGps_.longitude,
            gpsInfo.latitude, gpsInfo.longitude);
        
        if (distance >= config_.cooldownDistanceMeters) {
            return true;  // 距离冷却完成
        }
        return false;  // 距离冷却未完成
    }
    
    // 如果两个冷却都禁用（cooldownSeconds <= 0 && cooldownDistanceMeters <= 0）
    // 则无需冷却
    return true;
}

cv::Rect SmartZoomCaptureStrategy::calculateBoundingRegion(
    const std::vector<tracker::TrackedObject>& targets,
    const cv::Size& frameSize) {
    
    if (targets.empty()) {
        return cv::Rect(0, 0, frameSize.width, frameSize.height);
    }
    
    // 计算所有目标的包围框
    int minX = frameSize.width;
    int minY = frameSize.height;
    int maxX = 0;
    int maxY = 0;
    
    for (const auto& target : targets) {
        minX = std::min(minX, target.x);
        minY = std::min(minY, target.y);
        maxX = std::max(maxX, target.x + target.width);
        maxY = std::max(maxY, target.y + target.height);
    }
    
    // 添加 padding
    int width = maxX - minX;
    int height = maxY - minY;
    int padX = static_cast<int>(width * config_.regionPadding);
    int padY = static_cast<int>(height * config_.regionPadding);
    
    minX = std::max(0, minX - padX);
    minY = std::max(0, minY - padY);
    maxX = std::min(frameSize.width, maxX + padX);
    maxY = std::min(frameSize.height, maxY + padY);
    
    return cv::Rect(minX, minY, maxX - minX, maxY - minY);
}

double SmartZoomCaptureStrategy::calculateGpsDistance(
    double lat1, double lon1, double lat2, double lon2) {
    
    // Haversine 公式
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    
    lat1 = lat1 * M_PI / 180.0;
    lat2 = lat2 * M_PI / 180.0;
    
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dLon / 2) * std::sin(dLon / 2);
    
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    
    return EARTH_RADIUS_METERS * c;
}

}  // namespace strategy
}  // namespace vision
}  // namespace esdk_sophon
