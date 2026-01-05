/**
 * @file Sam2Segmentor.h
 * @brief SAM2 (Segment Anything Model 2) 分割器实现
 * 
 * 基于 Sophon BMRuntime 接口实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-11
 */

#ifndef ESDK_SOPHON_VISION_SAM2_SEGMENTOR_H_
#define ESDK_SOPHON_VISION_SAM2_SEGMENTOR_H_

#include "esdk_sophon/vision/segmentation/ISegmentor.h"

namespace esdk_sophon {
namespace vision {

class Sam2Segmentor : public ISegmentor {
public:
    Sam2Segmentor();
    ~Sam2Segmentor() override;

    bool init(const std::string& modelPath, const std::string& configPath = "") override;
    
    // 自动分割模式 (Grid Prompt) - 暂未实现
    std::vector<SegmentationResult> segment(const cv::Mat& frame) override;

    // 基于检测框的分割模式 (Box Prompt)
    std::vector<SegmentationResult> segmentWithPrompts(const cv::Mat& frame, const std::vector<DetectionBox>& boxes) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vision
}  // namespace esdk_sophon

#endif // ESDK_SOPHON_VISION_SAM2_SEGMENTOR_H_
