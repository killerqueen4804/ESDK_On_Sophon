/**
 * @file PPYoloeDetector.h
 * @brief PP-YOLOE目标检测器实现
 * 
 * 基于FastDeploy推理框架,在算能TPU上运行PP-YOLOE模型
 * PP-YOLOE是百度PaddleDetection团队开发的高精度检测器
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-01
 * @update 2025-11-22 - 重构使用 VisionUtils 统一工具类
 */

#ifndef ESDK_SOPHON_VISION_PPYOLOE_DETECTOR_H_
#define ESDK_SOPHON_VISION_PPYOLOE_DETECTOR_H_

#include "esdk_sophon/vision/IDetector.h"
#include "esdk_sophon/vision/VisionUtils.h"  // 新增: 使用统一工具类
#include "esdk_sophon/core/Logger.h"

#include <fastdeploy/vision.h>
#include <memory>
#include <string>
#include <vector>

namespace esdk_sophon {
namespace vision {

/**
 * @brief PP-YOLOE检测器
 * 
 * 实现要点:
 * - 基于FastDeploy框架
 * - 支持算能TPU加速
 * - 支持.bmodel模型格式
 * - 实现PP-YOLOE的预处理和后处理
 * 
 * 性能特点:
 * - PP-YOLOE是Anchor-free + Distribution Focal Loss的检测器
 * - 性能优异,在COCO数据集上达到SOTA水平
 * - 支持多种backbone(CSPRepResNet)
 * - 需要NMS后处理
 * 
 * 面试要点:
 * Q: PP-YOLOE的核心特点和优势?
 * A: 1. Anchor-free设计 - 简化训练和部署,无需anchor调参
 *    2. CSPRepResNet backbone - 平衡速度与精度
 *    3. Task Alignment Learning (TAL) - 优化正负样本匹配策略
 *    4. Distribution Focal Loss - 提升小目标检测性能
 *    5. 国产框架支持 - PaddlePaddle生态完善,文档齐全
 * 
 * Q: PP-YOLOE vs YOLOv5/YOLOv8的区别?
 * A: 1. 来源不同: PP-YOLOE来自百度PaddleDetection, YOLO系列来自Ultralytics
 *    2. Anchor策略: PP-YOLOE是Anchor-free, YOLOv5需要anchor
 *    3. 损失函数: PP-YOLOE用DFL+VFL, YOLO用CIoU+BCE
 *    4. NMS策略: PP-YOLOE需要NMS, YOLOv10不需要
 * 
 * 使用示例:
 * @code
 * // 配置检测器
 * DetectorConfig config;
 * config.modelPath = "/path/to/ppyoloe_crn_m_300e_coco.bmodel";
 * config.classes = {"person", "car", "bicycle"};
 * config.confidenceThreshold = 0.5f;
 * config.inputWidth = 640;
 * config.inputHeight = 640;
 * 
 * // 创建检测器
 * PPYoloeDetector detector;
 * if (!detector.initialize(config)) {
 *     std::cerr << "初始化失败" << std::endl;
 *     return -1;
 * }
 * 
 * // 检测
 * cv::Mat image = cv::imread("test.jpg");
 * DetectionResult result = detector.detect(image);
 * 
 * // 处理结果
 * for (const auto& box : result.boxes) {
 *     cv::rectangle(image, box.toRect(), cv::Scalar(0, 255, 0), 2);
 * }
 * @endcode
 */
class PPYoloeDetector : public IDetector {
public:
    /**
     * @brief 构造函数
     */
    PPYoloeDetector();
    
    /**
     * @brief 析构函数
     * 
     * @note 自动释放模型资源和TPU内存
     */
    ~PPYoloeDetector() override;
    
    // ==================== IDetector接口实现 ====================
    
    /**
     * @brief 初始化检测器
     * 
     * @param config 检测器配置
     * @return true 初始化成功
     * @return false 初始化失败
     * 
     * @details
     * 初始化流程:
     * 1. 检查模型文件是否存在
     * 2. 创建FastDeploy runtime选项(指定TPU)
     * 3. 加载PP-YOLOE模型
     * 4. 设置预处理参数
     * 5. 预热模型(可选)
     */
    bool initialize(const DetectorConfig& config) override;
    
    /**
     * @brief 检测目标
     * 
     * @param image 输入图像 (BGR格式)
     * @return DetectionResult 检测结果
     * 
     * @details
     * 检测流程:
     * 1. 预处理: Letterbox变换、Normalize、HWC→CHW、BGR→RGB
     * 2. 推理: 调用model->Predict()
     * 3. 后处理: NMS、置信度过滤、坐标还原
     * 
     * @note PP-YOLOE需要NMS后处理
     */
    DetectionResult detect(const cv::Mat& image) override;
    
    /**
     * @brief 获取支持的类别列表
     */
    const std::vector<std::string>& getClasses() const override {
        return classes_;
    }
    
    /**
     * @brief 获取模型输入尺寸
     */
    cv::Size getInputSize() const override {
        return cv::Size(inputWidth_, inputHeight_);
    }
    
    /**
     * @brief 获取检测器名称
     */
    std::string getName() const override {
        return "PP-YOLOE";
    }
    
    /**
     * @brief 是否已初始化
     */
    bool isInitialized() const override {
        return initialized_;
    }

private:
    // ==================== 内部方法 ====================
    
    /**
     * @brief 预处理图像 - Letterbox变换 (重构版)
     * 
     * @param image 原始图像
     * @return cv::Mat 预处理后的图像
     * 
     * @details
     * 重构说明:
     * - 使用 VisionUtils::letterbox() 统一工具类
     * - 变换参数记录在 lastTransform_ 中
     * - 代码从 ~50 行简化到 ~5 行
     * 
     * Letterbox预处理步骤:
     * 1. 计算缩放比例 scale = min(targetW/imageW, targetH/imageH)
     * 2. 按比例缩放图像
     * 3. 创建目标尺寸画布,灰色填充(114,114,114)
     * 4. 将缩放后图像居中放置
     * 
     * @note 面试高频考点! 使用统一工具类是工程最佳实践
     * Q: 为什么选择min而不是max?
     * A: 选择min保证图像完整显示,不会裁剪任何内容。
     *    max会导致长边超出画布,需要裁剪。
     */
    cv::Mat preprocess(const cv::Mat& image);
    
    /**
     * @brief 后处理检测结果 (重构版)
     * 
     * @param fdResult FastDeploy检测结果
     * @param originalSize 原始图像尺寸
     * @return DetectionResult 标准检测结果
     * 
     * @details
     * 重构说明:
     * - 使用 lastTransform_ 中的变换参数进行坐标还原
     * - 保持原有逻辑不变
     * 
     * 后处理步骤:
     * 1. 置信度过滤(>threshold)
     * 2. 坐标逆变换(从模型输入坐标还原到原图坐标)
     *    公式: x_orig = (x_model - offsetX) / scale
     * 3. 边界检查(确保坐标在原图范围内)
     * 4. 类别ID映射到类别名称
     */
    DetectionResult postprocess(
        const fastdeploy::vision::DetectionResult& fdResult,
        const cv::Size& originalSize);

private:
    // ==================== 成员变量 ====================
    
    // 模型和运行时
    std::unique_ptr<fastdeploy::vision::detection::PPYOLOE> model_;  ///< PP-YOLOE模型
    std::unique_ptr<fastdeploy::RuntimeOption> runtimeOption_;       ///< 运行时选项
    
    // 配置参数
    std::string modelPath_;              ///< 模型文件路径
    std::string configFile_;             ///< 配置文件路径 (infer_cfg.yml)
    std::vector<std::string> classes_;   ///< 类别名称列表
    float confidenceThreshold_ = 0.5f;   ///< 置信度阈值 (默认0.5)
    int inputWidth_ = 640;               ///< 模型输入宽度 (默认640)
    int inputHeight_ = 640;              ///< 模型输入高度 (默认640)
    
    // 状态标志
    bool initialized_ = false;           ///< 是否已初始化 (默认false)
    
    // 变换参数
    LetterboxTransform lastTransform_;   ///< 最近一次 Letterbox 变换参数 (用于后处理坐标还原)
    
    // 日志器
    core::Logger& logger_;               ///< 日志记录器
    
    /**
     * @brief 面试要点 - 成员变量设计
     * 
     * Q: 为什么用unique_ptr而不是裸指针?
     * A: 1. RAII自动管理内存,析构时自动释放
     *    2. 异常安全,抛出异常也能正确清理
     *    3. 明确所有权语义(独占所有权)
     *    4. 防止内存泄漏
     * 
     * Q: 为什么logger_是引用而不是指针?
     * A: 1. Logger是单例,生命周期覆盖整个程序
     *    2. 引用语义更清晰(不可为空,不可重新绑定)
     *    3. 避免空指针检查
     * 
     * Q: PP-YOLOE模型的内存管理?
     * A: 1. unique_ptr管理FastDeploy模型对象
     *    2. 模型内部管理TPU内存
     *    3. 析构时自动释放,无需手动清理
     * 
     * Q: 为什么添加 lastTransform_ 成员变量?
     * A: 1. 记录预处理时的变换参数
     *    2. 后处理时直接使用,无需重新计算
     *    3. 避免重复计算,提升性能
     *    4. 使用 VisionUtils 统一数据结构
     */
};

}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_PPYOLOE_DETECTOR_H_
