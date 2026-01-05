/**
 * @file ISegmentor.h
 * @brief 分割器接口 - 定义所有分割算法的统一接口
 * 
 * 设计模式: 策略模式 (Strategy Pattern)
 * 职责: 定义分割算法的抽象接口
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-11
 */

#ifndef ESDK_SOPHON_VISION_ISEGMENTOR_H_
#define ESDK_SOPHON_VISION_ISEGMENTOR_H_

#include <vector>
#include <string>
#include <memory>
#include <opencv2/opencv.hpp>
#include "esdk_sophon/vision/IDetector.h" // for DetectionBox

namespace esdk_sophon {
namespace vision {

/**
 * @brief 分割结果 - 单个目标的分割信息
 */
struct SegmentationResult {
    int classId;              ///< 类别ID
    std::string className;    ///< 类别名称
    float confidence;         ///< 置信度
    
    cv::Rect box;             ///< 边界框
    cv::Mat mask;             ///< 二值掩码 (CV_8UC1)
    std::vector<cv::Point> contours; ///< 轮廓点
};

/**
 * @brief 分割器接口类
 */
class ISegmentor {
public:
    virtual ~ISegmentor() = default;

    /**
     * @brief 初始化模型
     * @param modelPath 模型路径 (bmodel文件路径)
     * @param configPath 配置文件路径 (可选)
     * @return true 初始化成功
     */
    virtual bool init(const std::string& modelPath, const std::string& configPath = "") = 0;

    /**
     * @brief 执行分割 (全图/自动模式)
     * @param frame 输入图像
     * @return 分割结果列表
     */
    virtual std::vector<SegmentationResult> segment(const cv::Mat& frame) = 0;

    /**
     * @brief 执行分割 (基于提示框)
     * @param frame 输入图像
     * @param boxes 提示框列表 (通常来自目标检测)
     * @return 分割结果列表
     */
    virtual std::vector<SegmentationResult> segmentWithPrompts(const cv::Mat& frame, const std::vector<DetectionBox>& boxes) = 0;
};

}  // namespace vision
}  // namespace esdk_sophon

#endif // ESDK_SOPHON_VISION_ISEGMENTOR_H_
