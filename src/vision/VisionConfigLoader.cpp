/**
 * @file VisionConfigLoader.cpp
 * @brief 视觉模块配置加载器实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-01
 */

#include "esdk_sophon/vision/VisionConfigLoader.h"
#include <fstream>
#include <sstream>

namespace esdk_sophon {
namespace vision {

// ==================== 构造函数 ====================

VisionConfigLoader::VisionConfigLoader()
    : config_(core::Config::getInstance())
    , logger_(core::Logger::getInstance()) {
    
    logger_.debug("VisionConfigLoader已创建");
}

// ==================== 检测器配置加载 ====================

DetectorConfig VisionConfigLoader::loadDetectorConfig(const std::string& detectorType) const {
    logger_.info("加载检测器配置: " + detectorType);
    
    DetectorConfig config;
    
    // 构造配置键前缀: "detector.ppyoloe.xxx"
    std::string prefix = "detector." + detectorType + ".";
    
    // ------------------------------------------------
    // 1. 加载模型路径 (必需)
    // ------------------------------------------------
    config.modelPath = config_.getString(prefix + "model_path", "");
    if (config.modelPath.empty()) {
        logger_.warning("未配置检测器模型路径: " + prefix + "model_path");
        logger_.warning("将使用空路径,初始化时可能失败");
    } else {
        logger_.info("  模型路径: " + config.modelPath);
    }
    
    // ------------------------------------------------
    // 2. 加载配置文件路径 (PaddleDet需要, YOLO可选)
    // ------------------------------------------------
    config.configFile = config_.getString(prefix + "config_path", "");
    if (!config.configFile.empty()) {
        logger_.info("  配置文件: " + config.configFile);
    }
    
    // ------------------------------------------------
    // 3. 加载类别标签
    // ------------------------------------------------
    // ⚠️ 重要：不要在代码里“写死 COCO 80 类”。
    // 因为不同模型/数据集的类别集合不一样，必须以配置中的 labels_path 为准。
    std::string labelsPath = config_.getString(prefix + "labels_path", "");
    if (labelsPath.empty()) {
        // 这里不再默认回退到 COCO，直接报错提示配置缺失。
        logger_.error("未配置类别标签文件 labels_path: " + prefix + "labels_path");
        logger_.error("请在 config.json 中为该模型配置正确的 labels_path，避免类别不一致");
        config.classes.clear();
    } else {
        config.classes = loadClassLabels(labelsPath);
        logger_.info("  类别数量: " + std::to_string(config.classes.size()));
    }
    
    // ------------------------------------------------
    // 4. 加载检测参数
    // ------------------------------------------------
    config.confidenceThreshold = static_cast<float>(
        config_.getDouble(prefix + "confidence_threshold", 0.5)
    );
    logger_.info("  置信度阈值: " + std::to_string(config.confidenceThreshold));
    
    config.nmsThreshold = static_cast<float>(
        config_.getDouble(prefix + "nms_threshold", 0.45)
    );
    logger_.info("  NMS阈值: " + std::to_string(config.nmsThreshold));
    
    // ------------------------------------------------
    // 5. 加载输入尺寸
    // ------------------------------------------------
    // config.json中是数组 [640, 640], 需要读取第一个和第二个元素
    // 这里简化处理,假设width和height相同
    // TODO: 如果Config类支持数组,可以改进
    config.inputWidth = config_.getInt(prefix + "input_size[0]", 640);
    config.inputHeight = config_.getInt(prefix + "input_size[1]", 640);
    
    // 如果无法读取数组,尝试单独的width/height配置
    if (config.inputWidth == 640 && config_.has(prefix + "input_width")) {
        config.inputWidth = config_.getInt(prefix + "input_width", 640);
    }
    if (config.inputHeight == 640 && config_.has(prefix + "input_height")) {
        config.inputHeight = config_.getInt(prefix + "input_height", 640);
    }
    
    logger_.info("  输入尺寸: " + std::to_string(config.inputWidth) + "x" + 
                std::to_string(config.inputHeight));
    
    logger_.info("检测器配置加载完成");
    return config;
}

// ==================== 分割器配置加载 ====================

SegmentorConfig VisionConfigLoader::loadSegmentorConfig(const std::string& segmentorType) const {
    logger_.info("加载分割器配置: " + segmentorType);
    
    SegmentorConfig config;
    
    // 构造配置键前缀: "segmentation.paddleseg.xxx"
    std::string prefix = "segmentation." + segmentorType + ".";
    
    // ------------------------------------------------
    // 1. 检查是否启用
    // ------------------------------------------------
    config.enabled = config_.getBool(prefix + "enabled", true);
    if (!config.enabled) {
        logger_.info("  分割器已禁用");
        return config;
    }
    
    // ------------------------------------------------
    // 2. 加载模型路径 (必需)
    // ------------------------------------------------
    config.modelPath = config_.getString(prefix + "model_path", "");
    if (config.modelPath.empty()) {
        logger_.warning("未配置分割器模型路径: " + prefix + "model_path");
    } else {
        logger_.info("  模型路径: " + config.modelPath);
    }
    
    // ------------------------------------------------
    // 3. 加载配置文件路径 (PaddleSeg需要deploy.yaml)
    // ------------------------------------------------
    config.configPath = config_.getString(prefix + "config_path", "");
    if (!config.configPath.empty()) {
        logger_.info("  配置文件: " + config.configPath);
    }
    
    // ------------------------------------------------
    // 4. 加载分割参数
    // ------------------------------------------------
    config.numClasses = config_.getInt(prefix + "num_classes", 21);
    logger_.info("  类别数量: " + std::to_string(config.numClasses));
    
    // ------------------------------------------------
    // 5. 加载输入尺寸
    // ------------------------------------------------
    config.inputWidth = config_.getInt(prefix + "input_size[0]", 512);
    config.inputHeight = config_.getInt(prefix + "input_size[1]", 512);
    
    // 备用方案: 单独的width/height配置
    if (config.inputWidth == 512 && config_.has(prefix + "input_width")) {
        config.inputWidth = config_.getInt(prefix + "input_width", 512);
    }
    if (config.inputHeight == 512 && config_.has(prefix + "input_height")) {
        config.inputHeight = config_.getInt(prefix + "input_height", 512);
    }
    
    logger_.info("  输入尺寸: " + std::to_string(config.inputWidth) + "x" + 
                std::to_string(config.inputHeight));
    
    logger_.info("分割器配置加载完成");
    return config;
}

// ==================== 类别标签加载 ====================

std::vector<std::string> VisionConfigLoader::loadClassLabels(const std::string& labelsPath) const {
    logger_.debug("加载类别标签: " + labelsPath);
    
    std::vector<std::string> labels;
    std::ifstream file(labelsPath);
    
    if (!file.is_open()) {
        logger_.error("无法打开标签文件: " + labelsPath);
        logger_.error("类别标签文件是必须项（不同模型类别可能不一致），请检查 labels_path 是否正确");
        return {};
    }
    
    // 逐行读取标签
    std::string line;
    while (std::getline(file, line)) {
        // 去除首尾空格
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // 跳过空行和注释行
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        labels.push_back(line);
    }
    
    file.close();
    
    if (labels.empty()) {
        logger_.error("标签文件为空: " + labelsPath);
        logger_.error("类别标签文件为空会导致 classId->className 映射错误，请修复 labels 文件内容");
        return {};
    }
    
    logger_.info("成功加载 " + std::to_string(labels.size()) + " 个类别标签");
    return labels;
}

// ==================== 默认COCO标签 ====================

// 说明：不再提供默认 COCO 标签的兜底逻辑。
// 原因：不同模型/数据集类别顺序可能不同，继续兜底会“静默错配”，比直接失败更难排查。

}  // namespace vision
}  // namespace esdk_sophon

// ============================================================================
// 📚 知识点讲解
// ============================================================================
// 
// ### 1. 配置管理最佳实践
// 
// #### 为什么要避免硬编码?
// 
// **硬编码的问题**:
// ```cpp
// // ❌ 硬编码 - 不好的做法
// std::string modelPath = "/data/Edge-SDK/models/ppyoloe.bmodel";
// ```
// 
// 缺点:
// 1. 修改路径需要重新编译
// 2. 不同环境(开发/测试/生产)难以切换
// 3. 不利于团队协作(每个人路径可能不同)
// 4. 无法动态配置
// 
// **配置文件的优势**:
// ```cpp
// // ✅ 从配置文件读取 - 好的做法
// std::string modelPath = config_.getString("detector.ppyoloe.model_path");
// ```
// 
// 优点:
// 1. 无需重新编译即可修改
// 2. 支持多环境配置
// 3. 便于版本控制和部署
// 4. 支持热更新(reload)
// 
// #### 面试要点
// 
// **Q: 软件中的配置应该如何管理?**
// 
// A: 分层管理配置:
// 1. **编译时配置**: 宏定义、模板参数 (不可变)
// 2. **启动时配置**: 配置文件、环境变量 (可变但需重启)
// 3. **运行时配置**: 远程配置中心、数据库 (热更新)
// 
// 本项目采用启动时配置(config.json),平衡了灵活性和复杂度。
// 
// **Q: 如何处理配置缺失?**
// 
// A: 三种策略:
// 1. **严格模式**: 缺少必需配置时抛出异常/退出程序
// 2. **默认值**: 提供合理默认值,记录警告日志
// 3. **交互提示**: 询问用户提供缺失配置
// 
// 本项目采用默认值策略,适合自动化部署场景。
// 
// ### 2. 单例模式的应用
// 
// Config和Logger都是单例,VisionConfigLoader通过引用访问它们:
// 
// ```cpp
// VisionConfigLoader::VisionConfigLoader()
//     : config_(core::Config::getInstance())      // 获取Config单例
//     , logger_(core::Logger::getInstance()) {    // 获取Logger单例
//     // ...
// }
// ```
// 
// **为什么用引用而不是指针?**
// 
// 1. 单例保证一直存在,不会为nullptr
// 2. 引用语义更清晰(不可重新绑定)
// 3. 避免nullptr检查的开销
// 
// ### 3. 字符串拼接技巧
// 
// ```cpp
// std::string prefix = "detector." + detectorType + ".";
// config.modelPath = config_.getString(prefix + "model_path", "");
// ```
// 
// 这样可以灵活支持多种检测器类型:
// - detectorType = "ppyoloe" → "detector.ppyoloe.model_path"
// - detectorType = "yolov10" → "detector.yolov10.model_path"
// 
// ### 4. 数组配置的处理
// 
// JSON中的数组 `"input_size": [640, 640]` 如何读取?
// 
// **方案1**: 如果Config支持数组
// ```cpp
// auto sizes = config_.getArray("detector.ppyoloe.input_size");
// config.inputWidth = sizes[0];
// config.inputHeight = sizes[1];
// ```
// 
// **方案2**: 使用索引语法 (本项目采用)
// ```cpp
// config.inputWidth = config_.getInt("detector.ppyoloe.input_size[0]", 640);
// config.inputHeight = config_.getInt("detector.ppyoloe.input_size[1]", 640);
// ```
// 
// **方案3**: 分别配置width和height
// ```json
// {
//   "detector": {
//     "ppyoloe": {
//       "input_width": 640,
//       "input_height": 640
//     }
//   }
// }
// ```
// 
// 本项目同时支持方案2和方案3作为备用。
// 
// ============================================================================
