/**
 * @file TaskTypes.cpp
 * @brief 任务类型定义实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-09
 */

#include "esdk_sophon/task/TaskTypes.h"
#include "esdk_sophon/core/Config.h"  // 添加 Config 头文件
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
        // ⭐ COCO 类别名称到 ID 的映射表 (80 个类别)
        static const std::map<std::string, int> cocoClassMap = {
            {"person", 0}, {"bicycle", 1}, {"car", 2}, {"motorcycle", 3},
            {"airplane", 4}, {"bus", 5}, {"train", 6}, {"truck", 7},
            {"boat", 8}, {"traffic light", 9}, {"fire hydrant", 10}, {"stop sign", 11},
            {"parking meter", 12}, {"bench", 13}, {"bird", 14}, {"cat", 15},
            {"dog", 16}, {"horse", 17}, {"sheep", 18}, {"cow", 19},
            {"elephant", 20}, {"bear", 21}, {"zebra", 22}, {"giraffe", 23},
            {"backpack", 24}, {"umbrella", 25}, {"handbag", 26}, {"tie", 27},
            {"suitcase", 28}, {"frisbee", 29}, {"skis", 30}, {"snowboard", 31},
            {"sports ball", 32}, {"kite", 33}, {"baseball bat", 34}, {"baseball glove", 35},
            {"skateboard", 36}, {"surfboard", 37}, {"tennis racket", 38}, {"bottle", 39},
            {"wine glass", 40}, {"cup", 41}, {"fork", 42}, {"knife", 43},
            {"spoon", 44}, {"bowl", 45}, {"banana", 46}, {"apple", 47},
            {"sandwich", 48}, {"orange", 49}, {"broccoli", 50}, {"carrot", 51},
            {"hot dog", 52}, {"pizza", 53}, {"donut", 54}, {"cake", 55},
            {"chair", 56}, {"couch", 57}, {"potted plant", 58}, {"bed", 59},
            {"dining table", 60}, {"toilet", 61}, {"tv", 62}, {"laptop", 63},
            {"mouse", 64}, {"remote", 65}, {"keyboard", 66}, {"cell phone", 67},
            {"microwave", 68}, {"oven", 69}, {"toaster", 70}, {"sink", 71},
            {"refrigerator", 72}, {"book", 73}, {"clock", 74}, {"vase", 75},
            {"scissors", 76}, {"teddy bear", 77}, {"hair drier", 78}, {"toothbrush", 79}
        };
        
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
                        
                        // 查找类别名称对应的 ID
                        auto it = cocoClassMap.find(name);
                        if (it != cocoClassMap.end()) {
                            eventType.classIds.push_back(it->second);
                            // 📌 日志：显示映射结果
                            // logger.debug("类别映射: " + name + " -> " + std::to_string(it->second));
                        } else {
                            // 未找到映射，记录警告
                            // logger.warning("未知的类别名称: " + name);
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
