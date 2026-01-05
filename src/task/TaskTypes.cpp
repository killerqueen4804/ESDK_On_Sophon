/**
 * @file TaskTypes.cpp
 * @brief 任务类型定义实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-09
 */

#include "esdk_sophon/task/TaskTypes.h"
#include "esdk_sophon/core/Config.h"  // 添加 Config 头文件
#include "esdk_sophon/vision/VisionConfigLoader.h"
#include <stdexcept>

namespace esdk_sophon {
namespace task {

/**
 * @brief 从 MQTT JSON 消息创建任务配置
 * 
 * 将云端下发的 device_algorithm_enable 消息转换为内部任务配置。
 */
TaskConfig TaskConfig::fromJson(const nlohmann::json& json) {
    TaskConfig config;

    // ================================================================
    // ⭐ 重要设计：动态类别映射（避免写死 COCO）
    //
    // 云端下发的 type[].class 通常是“类别名称”，我们需要把它映射为 classId。
    // 但不同模型/不同数据集的 labels 文件可能完全不同（类别集合/顺序都可能变化）。
    // 因此：必须从当前模型对应的 labels_path 读取类别列表，并按“名称->索引”映射。
    // ================================================================
    std::unordered_map<std::string, int> classNameToId;
    try {
        vision::VisionConfigLoader loader;
        // 目前任务默认使用 PPYOLOE 检测器（TaskService 同样如此），因此这里保持一致。
        // 如果未来支持动态 detectorType，可把 detectorType 放到 TaskConfig 或全局配置里。
        vision::DetectorConfig detCfg = loader.loadDetectorConfig("ppyoloe");
        for (size_t i = 0; i < detCfg.classes.size(); ++i) {
            classNameToId[detCfg.classes[i]] = static_cast<int>(i);
        }
    } catch (...) {
        // 这里不抛异常：允许任务配置继续构建，但 class 映射会失败并导致 classIds 为空。
        // 运行时过滤会打印“关注类别ID: 无”，提醒你排查 labels 配置。
    }
    
    // ===== 基本信息 =====
    config.taskId = std::to_string(json.at("taskID").get<int>());
    config.algorithmId = json.value("algorithmRepoID", 0);
    
    // ===== 数据源类型 =====
    // 支持两种字段格式：
    // 1. "dataSource": "live" | "file" (字符串格式)
    // 2. "source": 0 | 1 (数字格式, 0=live, 1=file)
    std::string dataSource = "live";  // 默认值
    
    if (json.contains("dataSource")) {
        // 优先使用 dataSource 字段（字符串）
        dataSource = json["dataSource"].get<std::string>();
    } else if (json.contains("source")) {
        // 兼容 source 字段（数字）
        int sourceValue = json["source"].get<int>();
        dataSource = (sourceValue == 1) ? "file" : "live";
    }
    
    if (dataSource == "live" || dataSource == "0") {
        config.type = TaskType::DETECTION_LIVESTREAM;
        config.source = DataSource::LIVESTREAM;
    } else {
        config.type = TaskType::DETECTION_MEDIAFILE;
        config.source = DataSource::MEDIAFILE;
        // 📌 baseFolder 可选：
        //    - 如果提供，使用指定路径
        //    - 如果为空，MediaFileTask 使用默认路径（程序运行目录）
        config.mediaPath = json.value("baseFolder", "");
    }
    
    // ===== 检测参数 =====
    config.confidenceThreshold = json.value("confidence", 0.5f);
    config.nmsThreshold = 0.45f;  // 默认值
    
    // 转换事件类型 (从 types 数组中提取)
    // JSON 格式: {"id": 200004, "name": "垃圾倾倒", "class": ["car", "person"], "main_type": 100000}
    if (json.contains("type") && json["type"].is_array()) {
        for (const auto& typeObj : json["type"]) {
            EventType eventType;
            eventType.id = typeObj.value("id", 0);
            eventType.mainType = typeObj.value("main_type", 0);
            eventType.eventDescribe = typeObj.value("name", "");
            
            // ⭐ 解析 class 数组并转换为 COCO 类别 ID
            if (typeObj.contains("class") && typeObj["class"].is_array()) {
                for (const auto& className : typeObj["class"]) {
                    if (className.is_string()) {
                        std::string name = className.get<std::string>();
                        
                        // 查找类别名称对应的 ID（基于 labels_path 的动态映射）
                        auto it = classNameToId.find(name);
                        if (it != classNameToId.end()) {
                            eventType.classIds.push_back(it->second);
                        }
                    }
                }
            }
            
            config.eventTypes.push_back(eventType);
        }
    }
    
    // ===== 执行控制 =====
    config.reportIntervalSec = json.value("reportInterval", 10);
    config.enableVisualization = false;  // 默认不可视化
    config.maxDetectionsPerFrame = 0;    // 0=无限制
    
    // ===== RTMP 推流 =====
    config.enableRTMP = false;  // 默认不推流
    config.rtmpUrl = "";
    config.rtmpWidth = 800;
    config.rtmpHeight = 600;
    config.rtmpFps = 25;
    
    // ===== 设备信息 =====
    // 优先使用 MQTT 消息中的 deviceSn，如果没有则从配置文件读取
    if (json.contains("deviceSn") && !json["deviceSn"].get<std::string>().empty()) {
        config.deviceSn = json["deviceSn"].get<std::string>();
    } else {
        // 从配置文件读取默认设备 SN
        auto& globalConfig = core::Config::getInstance();
        config.deviceSn = globalConfig.getString("device.analysis_sn", "");
    }
    
    return config;
}

}  // namespace task
}  // namespace esdk_sophon
