/**
 * @file VisionConfigLoader.h
 * @brief 视觉模块配置加载器
 * 
 * 从config.json加载detector和segmentation配置
 * 避免代码中硬编码路径
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-01
 */

#ifndef ESDK_SOPHON_VISION_VISION_CONFIG_LOADER_H_
#define ESDK_SOPHON_VISION_VISION_CONFIG_LOADER_H_

#include "esdk_sophon/vision/IDetector.h"
#include "esdk_sophon/core/Config.h"
#include "esdk_sophon/core/Logger.h"
#include <string>
#include <vector>

namespace esdk_sophon {
namespace vision {

/**
 * @brief 分割器配置结构体
 * 
 * @details
 * 语义分割模型的配置参数
 */
struct SegmentorConfig {
    std::string modelPath;            ///< 模型文件路径
    std::string configPath;           ///< 配置文件路径 (deploy.yaml)
    int numClasses;                   ///< 分割类别数量
    int inputWidth;                   ///< 输入宽度
    int inputHeight;                  ///< 输入高度
    bool enabled;                     ///< 是否启用分割
    
    SegmentorConfig()
        : numClasses(21)
        , inputWidth(512)
        , inputHeight(512)
        , enabled(true) {}
};

/**
 * @brief 视觉配置加载器
 * 
 * @details
 * 从Config单例读取视觉模块配置,转换为对应的配置结构体
 * 
 * 【设计原则】
 * - 单一职责: 只负责配置加载
 * - 依赖注入: 依赖Config和Logger,便于测试
 * - 错误处理: 提供默认值,记录警告日志
 * 
 * 【知识点】配置管理最佳实践
 * 1. 配置集中管理 (config.json)
 * 2. 代码不硬编码路径
 * 3. 支持热更新 (reload配置)
 * 4. 提供合理默认值
 * 
 * @code
 * // 使用示例
 * VisionConfigLoader loader;
 * 
 * // 加载检测器配置
 * auto detectorConfig = loader.loadDetectorConfig("ppyoloe");
 * detector->initialize(detectorConfig);
 * 
 * // 加载分割器配置
 * auto segmentorConfig = loader.loadSegmentorConfig("paddleseg");
 * segmentor->initialize(segmentorConfig);
 * @endcode
 */
class VisionConfigLoader {
public:
    /**
     * @brief 构造函数
     * 
     * @details
     * 获取Config和Logger单例的引用
     */
    VisionConfigLoader();
    
    /**
     * @brief 加载检测器配置
     * 
     * @param detectorType 检测器类型 (如"ppyoloe", "yolov10")
     * @return DetectorConfig 检测器配置结构体
     * 
     * @details
     * 从config.json中读取:
     * - detector.{detectorType}.model_path
     * - detector.{detectorType}.config_path
     * - detector.{detectorType}.labels_path
     * - detector.{detectorType}.confidence_threshold
     * - detector.{detectorType}.nms_threshold
     * - detector.{detectorType}.input_size
     * 
     * @note 如果配置不存在,返回默认配置并记录警告
     * 
     * @code
     * VisionConfigLoader loader;
     * auto config = loader.loadDetectorConfig("ppyoloe");
     * // config.modelPath = "/data/Edge-SDK/models/PaddleDetection/ppyoloe_crn_m_300e_coco_289.bmodel"
     * // config.configFile = "/data/Edge-SDK/models/PaddleDetection/infer_cfg.yml"
     * @endcode
     */
    DetectorConfig loadDetectorConfig(const std::string& detectorType = "ppyoloe") const;
    
    /**
     * @brief 加载分割器配置
     * 
     * @param segmentorType 分割器类型 (如"paddleseg")
     * @return SegmentorConfig 分割器配置结构体
     * 
     * @details
     * 从config.json中读取:
     * - segmentation.{segmentorType}.model_path
     * - segmentation.{segmentorType}.config_path
     * - segmentation.{segmentorType}.num_classes
     * - segmentation.{segmentorType}.input_size
     * - segmentation.{segmentorType}.enabled
     * 
     * @code
     * VisionConfigLoader loader;
     * auto config = loader.loadSegmentorConfig("paddleseg");
     * // config.modelPath = "/data/Edge-SDK/models/paddleSeg/pp_liteseg.bmodel"
     * // config.configPath = "/data/Edge-SDK/models/paddleSeg/deploy.yaml"
     * @endcode
     */
    SegmentorConfig loadSegmentorConfig(const std::string& segmentorType = "paddleseg") const;
    
    /**
     * @brief 加载类别标签文件
     * 
     * @param labelsPath 标签文件路径
     * @return std::vector<std::string> 类别名称列表
     * 
     * @details
     * 读取.names或.txt文件,每行一个类别名称
     * 
     * @note 如果文件不存在,返回COCO 80类的默认标签
     * 
     * @code
     * auto classes = loader.loadClassLabels("models/coco.names");
     * // classes[0] = "person"
     * // classes[1] = "bicycle"
     * @endcode
     */
    std::vector<std::string> loadClassLabels(const std::string& labelsPath) const;
    
private:
    core::Config& config_;     ///< Config单例引用
    core::Logger& logger_;     ///< Logger单例引用
    
    /**
     * @brief 获取默认的COCO类别标签
     * 
     * @return std::vector<std::string> COCO 80类标签
     * 
     * @details
     * 提供COCO数据集的80个类别名称
     * 作为loadClassLabels()失败时的备用方案
     */
    std::vector<std::string> getDefaultCocoLabels() const;
};

}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_VISION_CONFIG_LOADER_H_

// ============================================================================
// 📚 使用场景
// ============================================================================
// 
// 1. Application初始化时加载检测器配置
//    VisionConfigLoader loader;
//    auto detectorConfig = loader.loadDetectorConfig("ppyoloe");
//    detector_->initialize(detectorConfig);
// 
// 2. 加载分割器配置
//    auto segmentorConfig = loader.loadSegmentorConfig("paddleseg");
//    segmentor_->initialize(segmentorConfig);
// 
// 3. 切换检测器 (如果config.json有多个检测器配置)
//    auto yolov10Config = loader.loadDetectorConfig("yolov10");
// 
// 4. 热更新配置
//    config_.reload();
//    auto newConfig = loader.loadDetectorConfig("ppyoloe");
//    detector_->initialize(newConfig);
// 
// ============================================================================
