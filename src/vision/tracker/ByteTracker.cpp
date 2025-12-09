/**
 * @file ByteTracker.cpp
 * @brief ByteTrack 追踪器封装实现
 * 
 * 使用 Pimpl 模式封装 third_party/bytetrack，
 * 在内部完成 DetectionBox ↔ ByteTrackBox 的格式转换
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-01
 */

#include "esdk_sophon/vision/tracker/ByteTracker.h"
#include "BYTETracker.h"  // third_party/bytetrack

#include <set>
#include <algorithm>

namespace esdk_sophon {
namespace vision {
namespace tracker {

/**
 * @brief ByteTracker 内部实现类
 * 
 * 使用 Pimpl 模式隐藏 bytetrack 库的实现细节
 */
class ByteTracker::Impl {
public:
    explicit Impl(const ByteTrackerConfig& config) 
        : config_(config), activeCount_(0) {
        // 转换配置
        bytetrack::ByteTrackParams params;
        params.track_thresh = config.trackThresh;
        params.match_thresh = config.matchThresh;
        params.frame_rate = config.frameRate;
        params.track_buffer = config.trackBuffer;
        params.min_box_area = config.minBoxArea;
        
        tracker_ = std::make_unique<bytetrack::BYTETracker>(params);
    }
    
    std::vector<TrackedObject> update(const std::vector<DetectionBox>& detections) {
        // Step 1: DetectionBox → ByteTrackBox
        std::vector<bytetrack::ByteTrackBox> btBoxes;
        btBoxes.reserve(detections.size());
        
        for (const auto& det : detections) {
            bytetrack::ByteTrackBox box;
            box.x = static_cast<float>(det.x);
            box.y = static_cast<float>(det.y);
            box.width = static_cast<float>(det.width);
            box.height = static_cast<float>(det.height);
            box.score = det.confidence;
            box.class_id = det.classId;
            btBoxes.push_back(box);
        }
        
        // Step 2: 调用 ByteTrack 核心算法
        bytetrack::STracks outputTracks;
        tracker_->update(outputTracks, btBoxes);
        
        // Step 3: STrack → TrackedObject
        std::vector<TrackedObject> result;
        result.reserve(outputTracks.size());
        
        for (const auto& track : outputTracks) {
            TrackedObject obj;
            obj.trackId = track->track_id;
            obj.x = static_cast<int>(track->tlwh[0]);
            obj.y = static_cast<int>(track->tlwh[1]);
            obj.width = static_cast<int>(track->tlwh[2]);
            obj.height = static_cast<int>(track->tlwh[3]);
            obj.confidence = track->score;
            obj.classId = track->class_id;
            obj.trackletLen = track->tracklet_len;
            
            // 判断是否为新目标
            if (uniqueTrackIds_.find(track->track_id) == uniqueTrackIds_.end()) {
                obj.isNew = true;
                uniqueTrackIds_.insert(track->track_id);
            } else {
                obj.isNew = false;
            }
            
            // 设置类别名称（如果原始检测有的话，这里暂时留空）
            // obj.className 需要从外部检测结果中获取
            
            result.push_back(obj);
        }
        
        activeCount_ = static_cast<int>(result.size());
        return result;
    }
    
    int getUniqueCount() const {
        return static_cast<int>(uniqueTrackIds_.size());
    }
    
    int getActiveCount() const {
        return activeCount_;
    }
    
    int getFrameId() const {
        return tracker_->getFrameId();
    }
    
    void reset() {
        // 重新创建追踪器
        bytetrack::ByteTrackParams params;
        params.track_thresh = config_.trackThresh;
        params.match_thresh = config_.matchThresh;
        params.frame_rate = config_.frameRate;
        params.track_buffer = config_.trackBuffer;
        params.min_box_area = config_.minBoxArea;
        
        tracker_ = std::make_unique<bytetrack::BYTETracker>(params);
        uniqueTrackIds_.clear();
        activeCount_ = 0;
    }
    
private:
    ByteTrackerConfig config_;
    std::unique_ptr<bytetrack::BYTETracker> tracker_;
    std::set<int> uniqueTrackIds_;  ///< 记录所有出现过的 Track ID
    int activeCount_;               ///< 当前活跃目标数
};

// ============== ByteTracker 公共接口实现 ==============

ByteTracker::ByteTracker(const ByteTrackerConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
}

ByteTracker::~ByteTracker() = default;

ByteTracker::ByteTracker(ByteTracker&&) noexcept = default;
ByteTracker& ByteTracker::operator=(ByteTracker&&) noexcept = default;

std::vector<TrackedObject> ByteTracker::update(const std::vector<DetectionBox>& detections) {
    return impl_->update(detections);
}

int ByteTracker::getUniqueCount() const {
    return impl_->getUniqueCount();
}

int ByteTracker::getActiveCount() const {
    return impl_->getActiveCount();
}

int ByteTracker::getFrameId() const {
    return impl_->getFrameId();
}

void ByteTracker::reset() {
    impl_->reset();
}

}  // namespace tracker
}  // namespace vision
}  // namespace esdk_sophon
