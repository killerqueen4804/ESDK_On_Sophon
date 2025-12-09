/**
 * @file PPYoloeDetector.cpp
 * @brief PP-YOLOE检测器实现
 * 
 * 核心算法实现:
 * 1. FastDeploy框架集成
 * 2. Letterbox预处理 (保持宽高比的缩放) - 使用 VisionUtils
 * 3. TPU推理加速
 * 4. 坐标变换与后处理 - 使用 VisionUtils
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-01
 * @update 2025-11-22 - 重构使用 VisionUtils 统一工具类
 */

#include "esdk_sophon/vision/PPYoloeDetector.h"
#include "esdk_sophon/vision/VisionUtils.h"  // 新增: 使用统一工具类
#include <fstream>
#include <chrono>

namespace esdk_sophon {
namespace vision {

// ==================== 构造和析构 ====================

/**
 * @brief 构造函数
 * 
 * 初始化列表说明:
 * - initialized_(false): 初始状态为未初始化
 * - logger_: 获取Logger单例引用
 * 
 * @note 面试要点:
 * Q: 为什么在构造函数中不直接加载模型?
 * A: 1. 构造函数不应抛出异常(C++最佳实践)
 *    2. 模型加载可能失败,需要返回错误状态
 *    3. 分离对象创建和初始化,符合RAII原则
 *    4. 支持延迟初始化和配置更新
 */
PPYoloeDetector::PPYoloeDetector()
    : initialized_(false)
    , logger_(core::Logger::getInstance()) {
    
    logger_.debug("PPYoloeDetector对象已创建");
}

/**
 * @brief 析构函数
 * 
 * RAII原则:
 * - unique_ptr自动释放model_和runtimeOption_
 * - 无需手动delete,异常安全
 * 
 * @note 面试要点:
 * Q: 析构函数需要做什么?
 * A: 1. 释放TPU内存(通过unique_ptr自动完成)
 *    2. 记录日志(可选)
 *    3. 通知其他组件(如果有观察者)
 *    智能指针让析构变得简单且安全
 */
PPYoloeDetector::~PPYoloeDetector() {
    if (initialized_) {
        logger_.info("PPYoloeDetector销毁,释放模型资源");
    }
    // unique_ptr自动释放资源
}

// ==================== 公共接口实现 ====================

/**
 * @brief 初始化检测器
 * 
 * 核心流程:
 * 1. 验证配置参数
 * 2. 检查模型文件存在
 * 3. 创建RuntimeOption配置TPU
 * 4. 加载PP-YOLOE模型
 * 5. 设置预处理参数
 * 6. 预热模型(可选)
 * 
 * @note 这是整个检测器最关键的初始化步骤
 */
bool PPYoloeDetector::initialize(const DetectorConfig& config) {
    logger_.info("开始初始化PP-YOLOE检测器...");
    
    // ------------------------------------------------
    // 步骤1: 保存配置参数
    // ------------------------------------------------
    modelPath_ = config.modelPath;
    configFile_ = config.configFile;
    classes_ = config.classes;
    confidenceThreshold_ = config.confidenceThreshold;
    inputWidth_ = config.inputWidth;
    inputHeight_ = config.inputHeight;
    
    logger_.info("配置参数:");
    logger_.info("  模型路径: " + modelPath_);
    if (!configFile_.empty()) {
        logger_.info("  配置文件: " + configFile_);
    }
    logger_.info("  类别数量: " + std::to_string(classes_.size()));
    logger_.info("  置信度阈值: " + std::to_string(confidenceThreshold_));
    logger_.info("  输入尺寸: " + std::to_string(inputWidth_) + "x" + 
                std::to_string(inputHeight_));
    
    // ------------------------------------------------
    // 步骤2: 检查模型文件是否存在
    // ------------------------------------------------
    std::ifstream modelFile(modelPath_);
    if (!modelFile.good()) {
        logger_.error("模型文件不存在: " + modelPath_);
        return false;
    }
    modelFile.close();
    
    logger_.info("模型文件检查通过");
    
    // ------------------------------------------------
    // 步骤3: 创建RuntimeOption (配置TPU后端)
    // ------------------------------------------------
    try {
        runtimeOption_ = std::make_unique<fastdeploy::RuntimeOption>();
        
        // 使用算能TPU (Sophon)
        runtimeOption_->UseSophgo();
        
        logger_.info("RuntimeOption创建成功,已配置Sophon TPU");
        
    } catch (const std::exception& e) {
        logger_.error("创建RuntimeOption失败: " + std::string(e.what()));
        return false;
    }
    
    // ------------------------------------------------
    // 步骤4: 加载PP-YOLOE模型
    // ------------------------------------------------
    try {
        // PP-YOLOE需要3个参数: model_file, params_file, config_file
        // bmodel格式: params_file为空, config_file是infer_cfg.yml
        std::string paramsFile = "";
        
        model_ = std::make_unique<fastdeploy::vision::detection::PPYOLOE>(
            modelPath_,
            paramsFile,
            configFile_,  // 使用配置中的configFile
            *runtimeOption_,
            fastdeploy::ModelFormat::SOPHGO
        );
        
        if (!model_->Initialized()) {
            logger_.error("模型初始化失败");
            return false;
        }
        
        logger_.info("PP-YOLOE模型加载成功");
        
    } catch (const std::exception& e) {
        logger_.error("加载模型异常: " + std::string(e.what()));
        return false;
    }
    
    // ------------------------------------------------
    // 步骤5: 配置预处理和后处理参数
    // ------------------------------------------------
    // PP-YOLOE的后处理配置方法
    // PaddleDetection系列使用ApplyNMS()方法
    model_->GetPostprocessor().ApplyNMS();
    
    // 注意: PaddleDet的阈值在infer_cfg.yml中配置
    // 如果需要动态修改,可以在后处理阶段手动过滤
    
    logger_.info("预处理和后处理参数配置完成");
    
    // ------------------------------------------------
    // 步骤6: 预热模型 (可选但推荐)
    // ------------------------------------------------
    logger_.info("开始预热模型...");
    cv::Mat dummyImage = cv::Mat::zeros(inputHeight_, inputWidth_, CV_8UC3);
    fastdeploy::vision::DetectionResult dummyResult;
    
    if (model_->Predict(dummyImage, &dummyResult)) {
        logger_.info("模型预热成功");
    } else {
        logger_.warning("模型预热失败,但不影响后续使用");
    }
    
    // ------------------------------------------------
    // 初始化完成
    // ------------------------------------------------
    initialized_ = true;
    logger_.info("✅ PP-YOLOE检测器初始化完成!");
    
    return true;
}

/**
 * @brief 检测目标 - 核心推理流程
 * 
 * 完整流程:
 * 1. 参数验证
 * 2. 图像预处理 (Letterbox变换)
 * 3. 模型推理 (TPU加速)
 * 4. 结果后处理 (坐标变换、置信度过滤)
 * 5. 性能统计
 * 
 * @note 这是整个检测器的核心方法,面试重点!
 */
DetectionResult PPYoloeDetector::detect(const cv::Mat& image) {
    DetectionResult result;
    
    // ------------------------------------------------
    // 步骤1: 参数验证
    // ------------------------------------------------
    if (!initialized_) {
        logger_.error("检测器未初始化");
        return result;
    }
    
    if (image.empty()) {
        logger_.error("输入图像为空");
        return result;
    }
    
    // 保存原始图像尺寸 (用于坐标还原)
    cv::Size originalSize = image.size();
    
    // 📌 诊断日志：使用 INFO 级别确保能看到
    static int detectCallCount = 0;
    detectCallCount++;
    if (detectCallCount == 1 || detectCallCount % 30 == 0) {
        logger_.debug("🎯 [PPYoloeDetector] 开始检测 (第 " + std::to_string(detectCallCount) + " 次): " + 
                    std::to_string(originalSize.width) + "x" + std::to_string(originalSize.height));
    }
    
    // ------------------------------------------------
    // 步骤2: 预处理 (计时)
    // ------------------------------------------------
    logger_.debug("📌 [步骤2] 开始预处理...");
    auto t1 = std::chrono::high_resolution_clock::now();
    
    cv::Mat preprocessedImage = preprocess(image);
    
    auto t2 = std::chrono::high_resolution_clock::now();
    result.preprocessTime = 
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    
    logger_.debug("✅ 预处理完成: " + std::to_string(preprocessedImage.cols) + "x" + 
                std::to_string(preprocessedImage.rows) + ", 耗时: " + 
                std::to_string(result.preprocessTime) + " ms");
    
    // ------------------------------------------------
    // 步骤3: 模型推理 (计时)
    // ------------------------------------------------
    logger_.debug("📌 [步骤3] 开始模型推理...");
    logger_.debug("   - 模型路径: " + modelPath_);
    logger_.debug("   - 输入尺寸: " + std::to_string(preprocessedImage.cols) + "x" + 
                std::to_string(preprocessedImage.rows));
    logger_.debug("   - 输入类型: " + std::to_string(preprocessedImage.type()) + " (期望: CV_8UC3=" + 
                std::to_string(CV_8UC3) + ")");
    logger_.debug("   - 输入通道数: " + std::to_string(preprocessedImage.channels()));
    
    fastdeploy::vision::DetectionResult fdResult;
    
    t1 = std::chrono::high_resolution_clock::now();
    
    bool success = model_->Predict(preprocessedImage, &fdResult);
    
    t2 = std::chrono::high_resolution_clock::now();
    result.inferenceTime = 
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    
    if (!success) {
        logger_.error("❌ 模型推理失败!");
        logger_.error("   可能原因:");
        logger_.error("   1. 输入图像尺寸不匹配: 期望 " + std::to_string(inputWidth_) + "x" + 
                     std::to_string(inputHeight_) + ", 实际 " + 
                     std::to_string(preprocessedImage.cols) + "x" + 
                     std::to_string(preprocessedImage.rows));
        logger_.error("   2. 输入图像格式不正确: 期望 CV_8UC3, 实际 type=" + 
                     std::to_string(preprocessedImage.type()));
        logger_.error("   3. 模型文件损坏或不兼容: " + modelPath_);
        logger_.error("   4. TPU 内存不足或设备忙");
        logger_.error("   5. FastDeploy 版本不兼容");
        return result;
    }
    
    logger_.debug("✅ 模型推理成功: 耗时 " + std::to_string(result.inferenceTime) + " ms, " +
                "检测到 " + std::to_string(fdResult.boxes.size()) + " 个候选框");
    
    // ------------------------------------------------
    // 步骤4: 后处理 (计时)
    // ------------------------------------------------
    logger_.debug("📌 [步骤4] 开始后处理...");
    t1 = std::chrono::high_resolution_clock::now();
    
    result = postprocess(fdResult, originalSize);
    
    t2 = std::chrono::high_resolution_clock::now();
    result.postprocessTime = 
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    
    // ------------------------------------------------
    // 步骤5: 日志输出
    // ------------------------------------------------
    logger_.debug("检测完成: " + 
                 std::to_string(result.boxes.size()) + " 个目标, " +
                 "耗时: " + std::to_string(result.getTotalTime()) + " ms " +
                 "(预处理:" + std::to_string(result.preprocessTime) + " + " +
                 "推理:" + std::to_string(result.inferenceTime) + " + " +
                 "后处理:" + std::to_string(result.postprocessTime) + ")");
    
    return result;
}

// ==================== 私有方法实现 ====================

/**
 * @brief 预处理图像 - Letterbox变换 (重构版)
 * 
 * 重构说明:
 * - 原实现: ~50 行内部实现 Letterbox 逻辑
 * - 新实现: ~5 行直接调用 VisionUtils::letterbox()
 * - 优势: 代码简洁、逻辑清晰、可复用、易维护
 * 
 * Letterbox原理:
 * 1. 计算缩放比例 (保持宽高比)
 * 2. 缩放图像
 * 3. 填充灰色边框到目标尺寸
 * 
 * 为什么要Letterbox?
 * - 保持图像宽高比,避免变形
 * - 统一输入尺寸,满足模型要求
 * - 填充区域不影响检测结果
 * 
 * @note 面试高频考点! 使用统一工具类是工程最佳实践
 */
cv::Mat PPYoloeDetector::preprocess(const cv::Mat& image) {
    // 使用 VisionUtils 统一工具类进行 Letterbox 预处理
    cv::Mat letterboxed = VisionUtils::letterbox(
        image,
        cv::Size(inputWidth_, inputHeight_),
        lastTransform_  // 记录变换参数,用于后处理坐标还原
    );
    
    logger_.debug("预处理完成: " + 
                 std::to_string(image.cols) + "x" + std::to_string(image.rows) + 
                 " → " + std::to_string(inputWidth_) + "x" + std::to_string(inputHeight_) +
                 " (scale=" + std::to_string(lastTransform_.scale) + ")");
    
    return letterboxed;
}

/**
 * @brief 后处理检测结果 (重构版)
 * 
 * 重构说明:
 * - 使用 lastTransform_ 记录的变换参数进行坐标还原
 * - 保持原有逻辑不变,只是使用统一的数据结构
 * 
 * 核心任务:
 * 1. 置信度过滤
 * 2. 坐标逆变换 (从模型输入坐标还原到原图坐标)
 * 3. 类别ID映射到类别名称
 * 
 * 坐标变换公式:
 * x_orig = (x_model - offsetX) / scale
 * y_orig = (y_model - offsetY) / scale
 */
DetectionResult PPYoloeDetector::postprocess(
    const fastdeploy::vision::DetectionResult& fdResult,
    const cv::Size& originalSize) {
    
    DetectionResult result;
    
    // 使用 lastTransform_ 中保存的变换参数
    float scale = lastTransform_.scale;
    int offsetX = lastTransform_.offsetX;
    int offsetY = lastTransform_.offsetY;
    
    // ------------------------------------------------
    // 步骤2: 遍历所有检测框
    // ------------------------------------------------
    float maxConfidence = 0.0f;
    int maxConfClassId = -1;

    for (size_t i = 0; i < fdResult.boxes.size(); ++i) {
        // 获取置信度
        float confidence = fdResult.scores[i];
        
        // 记录最大置信度用于调试
        if (confidence > maxConfidence) {
            maxConfidence = confidence;
            maxConfClassId = static_cast<int>(fdResult.label_ids[i]);
        }

        // 置信度过滤 (已在模型后处理中完成,这里二次检查)
        if (confidence < confidenceThreshold_) {
            continue;
        }
        
        // 获取类别ID
        int classId = static_cast<int>(fdResult.label_ids[i]);
        
        // 类别ID有效性检查
        if (classId < 0 || classId >= static_cast<int>(classes_.size())) {
            logger_.warning("无效的类别ID: " + std::to_string(classId));
            continue;
        }
        
        // 获取边界框 (模型输出坐标)
        const auto& box = fdResult.boxes[i];
        
        // ------------------------------------------------
        // 步骤3: 坐标逆变换 (模型坐标 → 原图坐标)
        // ------------------------------------------------
        DetectionBox detBox;
        detBox.classId = classId;
        detBox.className = classes_[classId];
        detBox.confidence = confidence;
        
        // 逆变换公式
        detBox.x = static_cast<int>((box[0] - offsetX) / scale);
        detBox.y = static_cast<int>((box[1] - offsetY) / scale);
        detBox.width = static_cast<int>((box[2] - box[0]) / scale);
        detBox.height = static_cast<int>((box[3] - box[1]) / scale);
        
        // 边界检查 (确保坐标在原图范围内)
        detBox.x = std::max(0, std::min(detBox.x, originalSize.width - 1));
        detBox.y = std::max(0, std::min(detBox.y, originalSize.height - 1));
        detBox.width = std::min(detBox.width, originalSize.width - detBox.x);
        detBox.height = std::min(detBox.height, originalSize.height - detBox.y);
        
        result.boxes.push_back(detBox);
    }
    
    logger_.debug("后处理完成,有效目标: " + std::to_string(result.boxes.size()) + 
                 " (最大置信度: " + std::to_string(maxConfidence) + 
                 ", 类别ID: " + std::to_string(maxConfClassId) + ")");
    
    return result;
}

}  // namespace vision
}  // namespace esdk_sophon
