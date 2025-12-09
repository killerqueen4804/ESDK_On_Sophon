/**
 * @file DetectorFactory.cpp
 * @brief 检测器工厂实现
 * @author ESDK_Sophon Team
 * @date 2025-10-31
 */

#include "esdk_sophon/vision/DetectorFactory.h"
#include "esdk_sophon/vision/PPYoloeDetector.h"  // 具体检测器
// #include "esdk_sophon/vision/Yolov8Detector.h"   // 未来扩展
#include "esdk_sophon/core/Logger.h"

#include <algorithm>
#include <stdexcept>
#include <cctype>

namespace esdk_sophon {
namespace vision {

// ==================== 公共接口实现 ====================

/**
 * @brief 创建检测器
 * 
 * 实现要点:
 * - 使用switch-case匹配类型
 * - 使用std::make_unique创建对象
 * - 调用initialize()初始化模型
 * - 初始化失败抛出异常
 */
std::unique_ptr<IDetector> DetectorFactory::create(
    DetectorType type,
    const DetectorConfig& config) {
    
    auto& logger = getLogger();  // 使用私有辅助方法获取Logger
    
    std::unique_ptr<IDetector> detector;
    
    // 1. 根据类型创建具体检测器
    switch (type) {
        case DetectorType::PPYOLOE:
            logger.info("创建PP-YOLOE检测器");
            detector = std::make_unique<PPYoloeDetector>();
            break;
            
        case DetectorType::YOLOV8:
            logger.error("YOLOv8检测器尚未实现");
            throw std::runtime_error("YOLOv8 detector not implemented yet");
            
        case DetectorType::YOLOV5:
            logger.error("YOLOv5检测器尚未实现");
            throw std::runtime_error("YOLOv5 detector not implemented yet");
            
        case DetectorType::UNKNOWN:
        default:
            logger.error("不支持的检测器类型");
            throw std::runtime_error("Unsupported detector type");
    }
    
    // 2. 初始化检测器
    if (!detector->initialize(config)) {
        logger.error("检测器初始化失败: " + config.modelPath);
        throw std::runtime_error("Failed to initialize detector");
    }
    
    logger.info("检测器创建成功: " + detector->getName());
    return detector;
}

/**
 * @brief 从字符串创建检测器
 * 
 * 实现要点:
 * - 调用stringToType()解析字符串
 * - 委托给create()方法
 */
std::unique_ptr<IDetector> DetectorFactory::createFromString(
    const std::string& typeStr,
    const DetectorConfig& config) {
    
    DetectorType type = stringToType(typeStr);
    
    if (type == DetectorType::UNKNOWN) {
        throw std::runtime_error("Unknown detector type: " + typeStr);
    }
    
    return create(type, config);
}

/**
 * @brief 从模型路径自动推断类型并创建
 * 
 * 实现要点:
 * - 提取文件名(basename)
 * - 转换为小写比较
 * - 按优先级匹配(yolov10 > yolov8 > yolov5)
 */
std::unique_ptr<IDetector> DetectorFactory::createFromModelPath(
    const std::string& modelPath,
    const DetectorConfig& config) {
    
    auto& logger = getLogger();  // 使用私有辅助方法获取Logger
    
    // 1. 提取文件名
    size_t lastSlash = modelPath.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) 
        ? modelPath.substr(lastSlash + 1) 
        : modelPath;
    
    // 2. 转换为小写
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), 
                   lowerFilename.begin(), ::tolower);
    
    // 3. 推断类型
    DetectorType type = DetectorType::UNKNOWN;
    
    if (lowerFilename.find("ppyoloe") != std::string::npos ||
        lowerFilename.find("ppyolo") != std::string::npos ||
        lowerFilename.find("pp_yoloe") != std::string::npos) {
        type = DetectorType::PPYOLOE;
    } else if (lowerFilename.find("yolov8") != std::string::npos) {
        type = DetectorType::YOLOV8;
    } else if (lowerFilename.find("yolov5") != std::string::npos) {
        type = DetectorType::YOLOV5;
    }
    
    if (type == DetectorType::UNKNOWN) {
        logger.error("无法从模型路径推断检测器类型: " + modelPath);
        throw std::runtime_error("Cannot infer detector type from model path");
    }
    
    logger.info("从模型路径推断检测器类型: " + typeToString(type));
    
    // 4. 更新配置中的模型路径
    DetectorConfig newConfig = config;
    newConfig.modelPath = modelPath;
    
    return create(type, newConfig);
}

/**
 * @brief 检查是否支持指定类型
 */
bool DetectorFactory::isSupported(DetectorType type) {
    switch (type) {
        case DetectorType::PPYOLOE:
            return true;
        case DetectorType::YOLOV8:
        case DetectorType::YOLOV5:
            return false;  // 暂未实现
        default:
            return false;
    }
}

/**
 * @brief 获取所有支持的类型列表
 */
std::vector<DetectorType> DetectorFactory::getSupportedTypes() {
    return {
        DetectorType::PPYOLOE
        // DetectorType::YOLOV8,   // 未来添加
        // DetectorType::YOLOV5    // 未来添加
    };
}

/**
 * @brief 获取类型的字符串表示
 */
std::string DetectorFactory::typeToString(DetectorType type) {
    switch (type) {
        case DetectorType::PPYOLOE:  return "PP-YOLOE";
        case DetectorType::YOLOV8:   return "YOLOv8";
        case DetectorType::YOLOV5:   return "YOLOv5";
        case DetectorType::UNKNOWN:
        default:                     return "Unknown";
    }
}

/**
 * @brief 从字符串解析类型
 * 
 * 实现要点:
 * - 不区分大小写
 * - 支持多种写法: "ppyoloe", "PPYOLOE", "PP-YOLOE", "ppyolo", "ppyoloe+"
 */
DetectorType DetectorFactory::stringToType(const std::string& typeStr) {
    // 转换为小写
    std::string lower = typeStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // 匹配类型
    if (lower == "ppyoloe" || lower == "ppyolo" || lower == "pp-yoloe" ||
        lower == "pp_yoloe" || lower == "ppyoloe+" || lower == "pp yoloe") {
        return DetectorType::PPYOLOE;
    } else if (lower == "yolov8" || lower == "yolo v8" || lower == "yolo_v8") {
        return DetectorType::YOLOV8;
    } else if (lower == "yolov5" || lower == "yolo v5" || lower == "yolo_v5") {
        return DetectorType::YOLOV5;
    }
    
    return DetectorType::UNKNOWN;
}

}  // namespace vision
}  // namespace esdk_sophon
