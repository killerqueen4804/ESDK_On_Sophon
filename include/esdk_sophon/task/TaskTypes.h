/**
 * @file TaskTypes.h
 * @brief Task 模块核心数据结构定义
 * 
 * 定义了任务系统中的所有基础数据类型，包括:
 * - 任务类型和状态枚举
 * - 任务配置结构
 * - 检测事件数据结构
 * - MQTT 消息格式
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-02
 */

#ifndef ESDK_SOPHON_TASK_TYPES_H_
#define ESDK_SOPHON_TASK_TYPES_H_
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <opencv2/opencv.hpp>     // OpenCV (BoundingBox 需要 cv::Rect)
#include <nlohmann/json.hpp>      // JSON 解析库 (fromJson 方法需要)

namespace esdk_sophon {
namespace task {

// ==================== 枚举类型 ====================

/**
 * @brief 任务类型枚举
 * 
 * 定义系统支持的任务类型。当前版本主要实现检测任务，
 * 其他类型预留供未来扩展。
 */
enum class TaskType {
    DETECTION_LIVESTREAM = 0,  ///< 直播流检测 (source=0)
    DETECTION_MEDIAFILE = 1,   ///< 媒体文件检测 (source=1)
    SEGMENTATION = 2,          ///< 语义分割 (预留)
    TRACKING = 3,              ///< 目标追踪 (预留)
    UNKNOWN = 99               ///< 未知类型
};

/**
 * @brief 任务状态枚举
 * 
 * 任务生命周期状态机:
 * IDLE → PENDING → RUNNING ⇄ PAUSED → COMPLETED/FAILED/CANCELLED
 */
enum class TaskState {
    IDLE = 0,       ///< 空闲 (未初始化)
    PENDING,        ///< 待启动 (已创建，等待资源)
    RUNNING,        ///< 运行中
    PAUSED,         ///< 暂停 (可恢复)
    COMPLETED,      ///< 完成 (正常结束)
    FAILED,         ///< 失败 (异常终止)
    CANCELLED       ///< 取消 (用户中止)
};

/**
 * @brief 数据源类型
 * 
 * 与DJI平台协议中的 source 字段对应
 */
enum class DataSource {
    LIVESTREAM = 0,  ///< 直播流 (实时视频)
    MEDIAFILE = 1    ///< 媒体文件 (图片/视频文件)
};

// ==================== 数据结构 ====================

/**
 * @brief 事件类型定义
 * 
 * 从平台下发的任务配置中解析而来，定义了需要检测的事件类型。
 * 
 * 示例:
 * @code
 * EventType fireEvent;
 * fireEvent.id = 1;
 * fireEvent.mainType = 1;
 * fireEvent.eventDescribe = "火灾检测";
 * fireEvent.classIds = {0};  // COCO person类
 * @endcode
 */
struct EventType {
    int id;                          ///< 事件ID (平台定义)
    int mainType;                    ///< 主类型ID
    std::string eventDescribe;       ///< 事件描述 (中文)
    std::vector<int> classIds;       ///< 关联的COCO类别ID列表
    
    EventType() : id(0), mainType(0) {}
};

/**
 * @brief 任务配置数据
 * 
 * 封装了执行一个任务所需的所有配置信息。
 * 从 MQTT 消息 (device_algorithm_enable) 中解析而来。
 * 
 * @note 所有字段都应该在创建任务前填充完整
 */
struct TaskConfig {
    // ===== 基本信息 =====
    std::string taskId;              ///< 任务唯一标识符 (平台分配)
    TaskType type;                   ///< 任务类型
    int algorithmId;                 ///< 算法ID (平台定义)
    
    // ===== 检测参数 =====
    std::vector<EventType> eventTypes;  ///< 需要检测的事件类型列表
    float confidenceThreshold;       ///< 置信度阈值 (0.0-1.0)
    float nmsThreshold;              ///< NMS阈值 (非极大值抑制)
    
    // ===== 数据源 =====
    DataSource source;               ///< 数据源类型
    std::string mediaPath;           ///< 媒体文件路径 (source=MEDIAFILE时使用)
    
    // ===== 执行控制 =====
    bool detectionEnabled;           ///< 是否启用视觉检测 (默认true)
    int reportIntervalSec;           ///< 事件上报间隔 (秒, 默认10)
    bool enableVisualization;        ///< 是否生成可视化图像
    int maxDetectionsPerFrame;       ///< 每帧最大检测数 (0=无限制)
    
    // ===== RTMP推流 (直播流任务) =====
    bool enableRTMP;                 ///< 是否启用RTMP推流
    std::string rtmpUrl;             ///< RTMP服务器地址
    int rtmpWidth;                   ///< 推流宽度
    int rtmpHeight;                  ///< 推流高度
    int rtmpFps;                     ///< 推流帧率
    
    // ===== 设备信息 (用于MQTT topic) =====
    std::string deviceSn;            ///< 设备序列号
    
    /**
     * @brief 默认构造函数 - 初始化合理的默认值
     */
    TaskConfig() 
        : taskId("")
        , type(TaskType::UNKNOWN)
        , algorithmId(0)
        , confidenceThreshold(0.5f)
        , nmsThreshold(0.45f)
        , source(DataSource::LIVESTREAM)
        , mediaPath("")
        , detectionEnabled(true)
        , reportIntervalSec(15)
        , enableVisualization(true)
        , maxDetectionsPerFrame(100)
        , enableRTMP(false)
        , rtmpUrl("")
        , rtmpWidth(800)
        , rtmpHeight(600)
        , rtmpFps(25)
        , deviceSn("")
    {}
    
    /**
     * @brief 验证配置是否有效
     * @return true 配置有效
     * 
     * @note deviceSn 可以为空，会在运行时从配置文件读取
     */
    bool isValid() const {
        if (taskId.empty()) {std::cout<<"1"<<std::endl; return false;}
        if (type == TaskType::UNKNOWN) {std::cout<<"2"<<std::endl; return false;}
        if (confidenceThreshold < 0.0f || confidenceThreshold > 1.0f) {std::cout<<"3"<<std::endl; return false;}
        // 📌 移除 baseFolder 强制要求：
        //    - 用户场景：图片已下载到程序运行目录
        //    - MediaFileTask 会自动扫描当前目录或指定目录
        //    - mediaPath 为空时，使用默认路径（程序运行目录）
        // if (source == DataSource::MEDIAFILE && mediaPath.empty()) {std::cout<<"4"<<std::endl; return false;}
        if (enableRTMP && rtmpUrl.empty()) {std::cout<<"5"<<std::endl; return false;}
        // deviceSn 不再强制要求：可以为空，会从配置文件 device.analysis_sn 读取
        // if (deviceSn.empty()) {std::cout<<"6"<<std::endl; return false;}
        return true;
    }
    
    /**
     * @brief 从 MQTT JSON 消息创建任务配置 🎯
     * 
     * 解析云端下发的启用算法指令(device_algorithm_enable),
     * 转换为内部任务配置格式。
     * 
     * @param json MQTT 消息的 JSON 对象
     * @return TaskConfig 任务配置对象
     * 
     * @throws nlohmann::json::exception JSON 解析失败
     * 
     * JSON 格式示例:
     * @code
     * {
     *   "taskID": 12345,
     *   "algorithmRepoID": 1,
     *   "algorithmName": "垃圾检测",
     *   "dataSource": "live",     // "live" 或 "file"
     *   "baseFolder": "/data/media",
     *   "confidence": 0.6,
     *   "reportInterval": 10,
     *   "deviceSn": "1ZNDH9I001234",
     *   "types": [
     *     {
     *       "id": 200004,
     *       "name": "垃圾倾倒",
     *       "classes": ["trash_bag", "container"],
     *       "mainType": 100000
     *     }
     *   ]
     * }
     * @endcode
     * 
     * @note 统一使用 task::TaskConfig,废弃 types::TaskConfig
     */
    static TaskConfig fromJson(const nlohmann::json& json);
};

/**
 * @brief 边界框 (带经纬度)
 * 
 * 包含图像坐标和地理坐标的检测框。
 * 用于事件上报时携带目标的空间位置信息。
 */
struct BoundingBox {
    // ===== 图像坐标 (像素) =====
    float x;                         ///< 左上角 X 坐标
    float y;                         ///< 左上角 Y 坐标
    float w;                         ///< 宽度
    float h;                         ///< 高度
    
    // ===== 地理坐标 (可选) =====
    double lon;                      ///< 经度 (WGS84)
    double lat;                      ///< 纬度 (WGS84)
    
    // ===== 检测信息 =====
    int classId;                     ///< COCO 类别 ID
    std::string className;           ///< 类别名称 (英文)
    float confidence;                ///< 置信度 (0.0-1.0)
    
    BoundingBox() 
        : x(0), y(0), w(0), h(0)
        , lon(0.0), lat(0.0)
        , classId(-1)
        , className("")
        , confidence(0.0f)
    {}
    
    // ===== 辅助方法 (为了兼容 vision::BBox) =====
    
    /**
     * @brief 转换为 cv::Rect (整数坐标)
     */
    cv::Rect toRect() const {
        return cv::Rect(static_cast<int>(x), static_cast<int>(y),
                       static_cast<int>(w), static_cast<int>(h));
    }
    
    /**
     * @brief 转换为 xyxy 格式 (x1, y1, x2, y2)
     * 
     * @note 用于 IoU 计算等算法
     */
    cv::Rect2f toXYXY() const {
        return cv::Rect2f(x, y, x + w, y + h);
    }
    
    /**
     * @brief 计算边界框面积
     */
    float area() const {
        return w * h;
    }
};

/**
 * @brief 多边形顶点
 */
struct Point2f {
    float x;
    float y;
};

/**
 * @brief 分割多边形
 */
struct Polygon {
    std::string label;
    std::vector<Point2f> vertices;
};

/**
 * @brief 检测事件数据
 * 
 * 完整的事件数据结构，用于上报到平台。
 * 对应 MQTT topic: drone/{device_sn}/info/event
 * 
 * **新接口格式** (2025-12-12更新):
 * @code
 * {
 *   "UUID": "...",
 *   "taskID": 1234,
 *   "eventType": 200009,
 *   "main_type": 100001,
 *   "createTime": "2025-12-12T10:30:00Z",
 *   "eventDescribe": "分割测试",
 *   "longitude": 121.47,
 *   "latitude": 31.23,
 *   "fileName": "test.jpg",
 *   "result": {
 *     "image": "base64_encoded_image_data",
 *     "objects": [
 *       {
 *         "label": "cat",
 *         "bbox": {
 *           "x": 54, "y": 182, "w": 152, "h": 195,
 *           "location": {"lon": 121.47, "lat": 31.23}
 *         },
 *         "mask": [
 *           {"x": 54, "y": 182, "location": {"lon": 121.47, "lat": 31.23}},
 *           {"x": 60, "y": 185, "location": {"lon": 121.47, "lat": 31.24}}
 *         ]
 *       }
 *     ]
 *   }
 * }
 * @endcode
 */
struct DetectionEvent {
    /**
     * @brief GPS 位置信息
     */
    struct GpsLocation {
        double lon;  ///< 经度
        double lat;  ///< 纬度
        
        GpsLocation() : lon(0.0), lat(0.0) {}
        GpsLocation(double longitude, double latitude) : lon(longitude), lat(latitude) {}
    };

    /**
     * @brief 掩码轮廓点 (分割结果)
     */
    struct MaskPoint {
        int x;                 ///< 像素 x 坐标
        int y;                 ///< 像素 y 坐标
        GpsLocation location;  ///< 对应的 GPS 坐标
        
        MaskPoint() : x(0), y(0) {}
        MaskPoint(int px, int py, GpsLocation loc = GpsLocation()) 
            : x(px), y(py), location(loc) {}
    };

    /**
     * @brief 检测框信息
     */
    struct BBox {
        int x;                 ///< 左上角 x 坐标
        int y;                 ///< 左上角 y 坐标
        int w;                 ///< 宽度
        int h;                 ///< 高度
        GpsLocation location;  ///< 中心点 GPS 坐标
        
        BBox() : x(0), y(0), w(0), h(0) {}
    };

    /**
     * @brief 单个检测对象
     */
    struct DetectedObject {
        std::string label;             ///< 类别标签 (如 "cat", "person")
        BBox bbox;                     ///< 检测框
        std::vector<MaskPoint> mask;   ///< 分割轮廓点列表 (可选,分割任务才有)
        
        DetectedObject() : label("") {}
        DetectedObject(const std::string& lbl) : label(lbl) {}
    };

    // ========== 顶层字段 ==========
    std::string uuid;                  ///< 事件唯一ID (UUID v4)
    int taskID;                        ///< 任务ID (整数)
    int eventType;                     ///< 事件类型ID
    int main_type;                     ///< 主类型ID (100000=检测, 100001=分割)
    std::string createTime;            ///< 创建时间 (ISO 8601格式)
    std::string eventDescribe;         ///< 事件描述
    double longitude;                  ///< 经度 (无人机位置)
    double latitude;                   ///< 纬度 (无人机位置)
    std::string fileName;              ///< 文件名称

    // ========== result 字段 ==========
    std::string resultImage;           ///< 结果图片 Base64 编码
    std::vector<DetectedObject> objects;  ///< 检测对象列表
    
    // ========== 内部临时字段 (不序列化到 JSON,仅用于 GPS 计算) ==========
    std::vector<BoundingBox> points;   ///< 临时字段: 检测框列表 (用于 GPS 计算)
    std::vector<Polygon> polygons;     ///< 临时字段: 多边形列表 (用于分割 GPS 计算)
    
    DetectionEvent()
        : uuid("")
        , taskID(0)
        , eventType(0)
        , main_type(0)
        , createTime("")
        , eventDescribe("")
        , longitude(0.0)
        , latitude(0.0)
        , fileName("")
        , resultImage("")
    {}
};

/**
 * @brief 任务统计信息
 * 
 * 用于监控和调试，记录任务执行过程中的统计数据。
 */
struct TaskStatistics {
    uint64_t framesProcessed;        ///< 已处理帧数
    uint64_t detectionsCount;        ///< 检测到的目标总数
    uint64_t eventsPublished;        ///< 已上报的事件数
    
    double avgProcessingTimeMs;      ///< 平均处理时间 (毫秒)
    double avgInferenceTimeMs;       ///< 平均推理时间 (毫秒)
    
    uint64_t startTimestamp;         ///< 任务开始时间 (Unix timestamp)
    uint64_t endTimestamp;           ///< 任务结束时间
    
    TaskStatistics()
        : framesProcessed(0)
        , detectionsCount(0)
        , eventsPublished(0)
        , avgProcessingTimeMs(0.0)
        , avgInferenceTimeMs(0.0)
        , startTimestamp(0)
        , endTimestamp(0)
    {}
    
    /**
     * @brief 获取任务运行时长 (秒)
     */
    double getDurationSeconds() const {
        if (startTimestamp == 0) return 0.0;
        uint64_t endTime = (endTimestamp == 0) ? 
            std::time(nullptr) : endTimestamp;
        return static_cast<double>(endTime - startTimestamp);
    }
    
    /**
     * @brief 获取处理帧率 (FPS)
     */
    double getProcessingFps() const {
        double duration = getDurationSeconds();
        if (duration < 1.0) return 0.0;
        return framesProcessed / duration;
    }
};

// ==================== 辅助函数 ====================

/**
 * @brief 将 TaskType 转换为字符串
 */
inline std::string taskTypeToString(TaskType type) {
    switch (type) {
        case TaskType::DETECTION_LIVESTREAM: return "DetectionLiveStream";
        case TaskType::DETECTION_MEDIAFILE:  return "DetectionMediaFile";
        case TaskType::SEGMENTATION:         return "Segmentation";
        case TaskType::TRACKING:             return "Tracking";
        case TaskType::UNKNOWN:              return "Unknown";
        default:                             return "Invalid";
    }
}

/**
 * @brief 将 TaskState 转换为字符串
 */
inline std::string taskStateToString(TaskState state) {
    switch (state) {
        case TaskState::IDLE:       return "Idle";
        case TaskState::PENDING:    return "Pending";
        case TaskState::RUNNING:    return "Running";
        case TaskState::PAUSED:     return "Paused";
        case TaskState::COMPLETED:  return "Completed";
        case TaskState::FAILED:     return "Failed";
        case TaskState::CANCELLED:  return "Cancelled";
        default:                    return "Invalid";
    }
}

/**
 * @brief 将 source 整数转换为 DataSource 枚举
 */
inline DataSource intToDataSource(int source) {
    return (source == 0) ? DataSource::LIVESTREAM : DataSource::MEDIAFILE;
}

/**
 * @brief 将 DataSource 枚举转换为整数
 */
inline int dataSourceToInt(DataSource source) {
    return (source == DataSource::LIVESTREAM) ? 0 : 1;
}

}  // namespace task
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_TASK_TYPES_H_
