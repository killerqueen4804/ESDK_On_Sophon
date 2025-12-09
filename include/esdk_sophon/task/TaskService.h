/**
 * @file TaskService.h
 * @brief 任务服务层 - 业务逻辑编排核心
 * 
 * TaskService 是四层架构中的 Service Layer (业务编排层):
 * 
 * 架构定位:
 * @code
 * Layer 1: Access Layer (MQTT Handler) - 接收命令
 *          ↓
 * Layer 2: Manager Layer (Task Manager) - 任务管理
 *          ↓
 * Layer 3: Service Layer (Task Service) ⭐ [本文件] - 业务编排
 *          ↓
 * Layer 4: Utility Layer (Vision, MQTT, Utils) - 工具调用
 * @endcode
 * 
 * 核心职责:
 * 1. 帧处理流程编排 (processFrame)
 * 2. 目标检测调用 (detectObjects)
 * 3. 事件数据构建 (buildEvent)
 * 4. MQTT 事件发布 (publishEvent)
 * 5. GPS 坐标转换
 * 6. 图像编码和压缩
 * 
 * 设计原则:
 * - Single Responsibility: 只负责业务流程编排,不实现具体算法
 * - Dependency Inversion: 依赖抽象接口,不依赖具体实现
 * - Open-Closed: 对扩展开放 (新增事件类型),对修改封闭
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-02
 */

#ifndef ESDK_SOPHON_TASK_SERVICE_H_
#define ESDK_SOPHON_TASK_SERVICE_H_

#include "esdk_sophon/task/TaskTypes.h"
#include "esdk_sophon/core/Logger.h"
// #include "esdk_sophon/vision/Vision.h"  // TODO: Vision类已废弃，使用DetectorFactory
#include "esdk_sophon/Mqtt/MqttClient.h"

#include <memory>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

namespace esdk_sophon {
namespace task {

/**
 * @brief 任务服务 - 业务逻辑编排器
 * 
 * TaskService 是 **无状态的服务类**,可以被多个任务共享使用。
 * 采用 Pimpl 惯用法隐藏实现细节。
 * 
 * 典型使用流程:
 * @code
 * // 1. 创建服务 (通常在 TaskManager 中创建一次,全局共享)
 * auto service = std::make_shared<TaskService>(visionPtr, mqttPtr);
 * 
 * // 2. 任务执行线程中调用 (LiveStreamTask::execute())
 * while (!shouldStop) {
 *     cv::Mat frame = captureFrame();  // 获取帧
 *     service->processFrame(frame, taskConfig);  // 处理帧
 * }
 * @endcode
 * 
 * 线程安全性:
 * - processFrame() 是线程安全的,可以被多个任务并发调用
 * - 内部使用局部变量和参数传递,避免共享状态
 * - Vision 和 MQTT 模块本身应该是线程安全的
 * 
 * 面试要点:
 * - Pimpl 惯用法的优势 (编译隔离, ABI 稳定性)
 * - 无状态服务的设计 (函数式编程思想)
 * - 依赖注入 (Dependency Injection) 模式
 */
class TaskService {
public:
    /**
     * @brief 构造函数 - 依赖注入
     * 
     * 使用依赖注入 (DI) 模式,将依赖的模块通过构造函数传入。
     * 
     * 优势:
     * - 可测试性: 可以注入 Mock 对象进行单元测试
     * - 解耦: TaskService 不需要知道如何创建 Vision 和 MQTT
     * - 灵活性: 可以在运行时替换实现
     * 
     * @param vision 视觉检测模块 (已初始化)
     * @param mqtt MQTT 客户端 (已连接)
     * 
     * @throws std::invalid_argument 如果任一参数为 nullptr
     * 
     * @note 传入的指针应该在 TaskService 生命周期内有效
     * 
     * 面试要点:
     * - 依赖注入 vs 依赖查找
     * - 构造函数注入 vs Setter 注入 vs 接口注入
     */
    TaskService(
                std::shared_ptr<mqtt::MqttClient> mqtt);
    
    /**
     * @brief 析构函数
     * 
     * Pimpl 需要在 .cpp 中定义析构函数,因为编译器需要知道
     * Impl 的完整定义才能生成删除代码。
     */
    ~TaskService();
    
    // 禁止拷贝和赋值 (服务类不应该被拷贝)
    TaskService(const TaskService&) = delete;
    TaskService& operator=(const TaskService&) = delete;
    
    // 允许移动 (用于返回值优化)
    TaskService(TaskService&&) noexcept;
    TaskService& operator=(TaskService&&) noexcept;
    
    // ==================== 核心业务接口 ====================
    
    /**
     * @brief 处理单帧图像 - 完整业务流程 ⭐ 核心方法
     * 
     * 这是 TaskService 的核心方法,编排了完整的帧处理流程:
     * 
     * 流程图:
     * @code
     * processFrame()
     *   ┣━━ 1. detectObjects()        // 目标检测
     *   ┃     └─ Vision::detect()     // 调用 YOLOv10
     *   ┃
     *   ┣━━ 2. filterByEventTypes()   // 过滤目标
     *   ┃     └─ 根据 config.eventTypes 过滤
     *   ┃
     *   ┣━━ 3. 检查上报间隔
     *   ┃     └─ 避免频繁上报
     *   ┃
     *   ┣━━ 4. buildEvent()           // 构建事件
     *   ┃     ┣─ 生成 UUID
     *   ┃     ┣─ 编码图片 (JPEG + Base64)
     *   ┃     ┣─ 转换 GPS 坐标
     *   ┃     └─ 填充 DetectionEvent
     *   ┃
     *   └━━ 5. publishEvent()         // 发布到 MQTT
     *         └─ MqttClient::publish()
     * @endcode
     * 
     * @param frame 输入图像 (OpenCV Mat, BGR格式)
     * @param config 任务配置 (包含检测参数、事件类型等)
     * @param[out] outBoxes 输出检测到的目标框（过滤后的结果）
     * @param fileName 可选参数，原始文件名（仅媒体文件任务使用）
     *                 - 媒体文件任务：传递 file.file_name（如 "DJI_0123.JPG"）
     *                 - 直播流任务：传递空字符串（默认值）
     * @return true 处理成功 (有目标且上报成功)
     * @return false 处理失败或无目标
     * 
     * @note 此方法是线程安全的,可以被多个任务并发调用
     * @note 如果检测到目标但未到上报间隔,也返回 false
     * 
     * 性能考虑:
     * - 检测: ~50ms (YOLOv10, BM1684X)
     * - 编码: ~10ms (JPEG 压缩)
     * - Base64: ~5ms
     * - MQTT: ~2ms (局域网)
     * - 总计: ~67ms (约 15 FPS)
     * 
     * 使用示例:
     * @code
     * // 媒体文件任务
     * std::vector<BoundingBox> boxes;
     * service.processFrame(frame, config, boxes, "DJI_0123.JPG");
     * 
     * // 直播流任务
     * std::vector<BoundingBox> boxes;
     * service.processFrame(frame, config, boxes);  // fileName 默认为 ""
     * @endcode
     */
    bool processFrame(const cv::Mat& frame, 
                     const TaskConfig& config,
                     std::vector<BoundingBox>& outBoxes,
                     const std::string& fileName = "");
    
    /**
     * @brief 目标检测 - 调用 Vision 模块
     * 
     * 封装了 Vision::detect() 调用,并应用配置参数。
     * 
     * @param frame 输入图像
     * @param config 任务配置 (confidenceThreshold, nmsThreshold)
     * @param[out] boxes 输出检测框列表
     * @return true 检测成功 (不管是否有目标)
     * @return false 检测失败 (模型异常)
     * 
     * @note boxes 可能为空 (没有检测到目标)
     */
    bool detectObjects(const cv::Mat& frame, 
                      const TaskConfig& config,
                      std::vector<BoundingBox>& boxes);
    
    /**
     * @brief 构建检测事件 - 数据封装
     * 
     * 将检测结果封装为 DetectionEvent 结构,包括:
     * - 生成 UUID (v4 随机)
     * - 图像编码 (JPEG + Base64)
     * - GPS 坐标转换 (像素 → 经纬度)
     * - 时间戳格式化 (ISO 8601)
     * 
     * @param frame 原始图像 (用于编码)
     * @param boxes 检测框列表
     * @param config 任务配置
     * @param eventType 事件类型配置
     * @param fileName 可选参数，原始文件名（用于 GPS 计算）
     *                 - 媒体文件任务：传递 file.file_name
     *                 - 直播流任务：传递空字符串（默认值）
     * @return DetectionEvent 完整的事件数据
     * 
     * @note 此方法会拷贝和编码图像,有一定开销
     */
    DetectionEvent buildEvent(const cv::Mat& frame,
                              const std::vector<BoundingBox>& boxes,
                              const TaskConfig& config,
                              const EventType& eventType,
                              const std::string& fileName = "");
    
    /**
     * @brief 发布事件到 MQTT - 上报平台
     * 
     * 将 DetectionEvent 序列化为 JSON 并发布到 MQTT topic:
     * Topic: drone/{device_sn}/info/event
     * 
     * JSON 格式:
     * @code
     * {
     *   "UUID": "550e8400-e29b-41d4-a716-446655440000",
     *   "taskID": "12345",
     *   "eventType": 1,
     *   "main_type": 1,
     *   "eventDescribe": "检测到目标",
     *   "picture": "/9j/4AAQSkZJRg...",
     *   "pictureCode": ".jpg",
     *   "latitude": 31.230391,
     *   "longitude": 121.473701,
     *   "createTime": "2025-11-02T10:30:00.000Z",
     *   "points": [
     *     {"x":100, "y":200, "w":50, "h":80, 
     *      "lon":121.47, "lat":31.23, 
     *      "classId":0, "className":"person", "confidence":0.95}
     *   ]
     * }
     * @endcode
     * 
     * @param event 事件数据
     * @param deviceSn 设备序列号 (用于构建 topic)
     * @return true 发布成功
     * @return false 发布失败 (MQTT 未连接或网络错误)
     */
    bool publishEvent(const DetectionEvent& event, const std::string& deviceSn);
    
    /**
     * @brief 发布 JSON 事件到指定 MQTT 主题
     * 
     * 直接发布 JSON 对象，适用于自定义事件格式。
     * 
     * @param topic MQTT 主题 (完整路径，如 "thing/product/{sn}/events")
     * @param eventJson 事件 JSON 对象
     * @return true 发布成功
     * @return false 发布失败 (MQTT 未连接或网络错误)
     * 
     * 使用场景:
     * - EventCache 回调发布
     * - 任务分析结果上报
     * - 进度通知消息
     * 
     * @note 此方法绕过 DetectionEvent 转换,直接发布原始 JSON
     */
    bool publishMqttMessage(const std::string& topic, const nlohmann::json& eventJson);
    
    // ==================== 工具方法 ====================
    
    /**
     * @brief 图像编码为 JPEG 格式
     * 
     * @param frame 输入图像 (BGR)
     * @param quality JPEG 质量 (1-100, 默认 85)
     * @param[out] buffer 输出字节流
     * @return true 编码成功
     */
    bool encodeImageToJPEG(const cv::Mat& frame, 
                           int quality,
                           std::vector<uint8_t>& buffer);
    
    /**
     * @brief Base64 编码
     * 
     * 将二进制数据编码为 Base64 字符串。
     * 
     * @param data 输入字节流
     * @return std::string Base64 字符串
     * 
     * @note 输出长度约为输入的 4/3 倍
     * 
     * 面试要点:
     * - Base64 编码原理 (3字节 → 4字符)
     * - 为什么要 Base64? (JSON 不支持二进制,需要文本编码)
     */
    std::string encodeBase64(const std::vector<uint8_t>& data);
    
    /**
     * @brief 像素坐标转换为 GPS 坐标
     * 
     * 根据相机参数和飞机位姿,将图像坐标转换为地理坐标。
     * 
     * @param pixelX 像素 X 坐标
     * @param pixelY 像素 Y 坐标
     * @param imageWidth 图像宽度
     * @param imageHeight 图像高度
     * @param droneLat 飞机纬度
     * @param droneLon 飞机经度
     * @param droneAlt 飞机海拔 (米)
     * @param droneYaw 飞机航向角 (度)
     * @param[out] targetLat 目标纬度
     * @param[out] targetLon 目标经度
     * @return true 转换成功
     * 
     * @note 当前版本使用简化算法,假设地面平坦
     * @note 实际项目中应该考虑:
     *       - 相机内参 (焦距, 畸变)
     *       - 云台角度 (pitch, roll, yaw)
     *       - 地形高程 (DEM 数据)
     */
    bool pixelToGPS(float pixelX, float pixelY,
                    int imageWidth, int imageHeight,
                    double droneLat, double droneLon,
                    double droneAlt, double droneYaw,
                    double& targetLat, double& targetLon);
    
    /**
     * @brief 计算检测框的 GPS 坐标（使用 GeoDecodeAPI）
     * 
     * 调用本地 GeoDecodeAPI 服务，将图像坐标转换为实际 GPS 坐标。
     * API 会根据图片的 EXIF 信息（无人机GPS、高度、姿态等）和检测框像素坐标，
     * 计算出每个检测框中心点的实际地理坐标。
     * 
     * @param frame 原始图像（需要保存为临时文件供 API 读取）
     * @param boxes 检测框列表（输入）
     * @param event 输出参数：填充 latitude、longitude 和 points 数组
     * @return true 成功获取所有检测框的 GPS 坐标
     * @return false 失败（API 错误、网络问题、响应格式错误）
     * 
     * API 说明:
     * - 地址: http://127.0.0.1:8122/geoDecode/arithmetic/getLonLatByPoints
     * - 方法: POST
     * - Content-Type: application/json
     * - 请求格式:
     *   @code
     *   {
     *     "path": "/tmp/frame_uuid.jpg",  // 图片路径（含 EXIF 信息）
     *     "points": [
     *       {"x": 100, "y": 200, "w": 50, "h": 80},  // 检测框像素坐标
     *       {"x": 300, "y": 400, "w": 60, "h": 90}
     *     ]
     *   }
     *   @endcode
     * - 响应格式:
     *   @code
     *   {
     *     "code": 200,
     *     "data": [
     *       {"longitude": 121.473701, "latitude": 31.230391},
     *       {"longitude": 121.473800, "latitude": 31.230500}
     *     ]
     *   }
     *   @endcode
     * 
     * 处理流程:
     * 1. 保存 frame 到临时文件（/tmp/frame_{uuid}.jpg）
     * 2. 构建 JSON 请求（包含图片路径和检测框坐标）
     * 3. 调用 HttpClient::post() 发送请求
     * 4. 解析 JSON 响应，提取每个检测框的 GPS 坐标
     * 5. 计算所有有效坐标的平均值，设置到 event.latitude/longitude
     * 6. 将每个检测框的 GPS 坐标填充到 event.points[i].lat/lon
     * 7. 删除临时文件
     * 
     * 错误处理:
     * - 临时文件保存失败 → 返回 false
     * - HTTP 请求失败（网络错误、超时）→ 返回 false
     * - 响应格式错误（缺少 "data" 字段）→ 返回 false
     * - GPS 坐标数量不匹配 → 返回 false
     * - 所有 GPS 坐标都无效（null）→ 返回 false
     * 
     * 性能考虑:
     * - 文件 I/O: ~5ms（保存临时 JPEG）
     * - HTTP 请求: ~10-50ms（本地服务，取决于图片大小和检测框数量）
     * - JSON 解析: ~1ms
     * - 总计: ~20-60ms
     * 
     * @note 临时文件会在函数结束时自动删除（无论成功或失败）
     * @note 线程安全（每次调用使用独立的临时文件）
     * @note 调用前必须确保 GeoDecodeAPI 服务已启动
     * 
     * 面试要点:
     * - RESTful API 调用：JSON 请求/响应格式
     * - 文件临时存储：UUID 避免文件名冲突
     * - 错误处理：多层校验（HTTP、JSON、数据完整性）
     * - RAII 资源管理：临时文件自动清理
     * 
     * @param imagePath 图片路径（媒体文件检测时使用原始路径，直播流检测时为空则自动生成）
     */
    bool calculateGPSCoordinates(const cv::Mat& frame,
                                 const std::vector<BoundingBox>& boxes,
                                 DetectionEvent& event,
                                 const std::string& imagePath = "");
    
    /**
     * @brief 生成 UUID v4 (随机)
     * 
     * 生成符合 RFC 4122 标准的 UUID。
     * 
     * 格式: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
     * - x: 随机十六进制数字
     * - 4: 版本号 (v4)
     * - y: 8/9/a/b 之一 (variant 位)
     * 
     * @return std::string UUID 字符串
     * 
     * 示例: "550e8400-e29b-41d4-a716-446655440000"
     * 
     * 面试要点:
     * - UUID 的作用 (全局唯一标识符)
     * - 碰撞概率 (2^122 ≈ 5.3×10^36, 实际上不会碰撞)
     * - UUID v1 vs v4 (v1 基于时间+MAC, v4 纯随机)
     */
    std::string generateUUID();
    
    /**
     * @brief 获取当前时间字符串（平台要求格式）
     * 
     * 格式: "yyyy-mm-dd hh:mm:ss" (本地时间)
     * 示例: "2025-11-03 14:25:30"
     * 
     * @return std::string 格式化的时间字符串
     * 
     * @note 使用本地时间（受系统时区影响，中国为 UTC+8）
     */
    std::string getCurrentTimeString();
    
    /**
     * @brief 检查是否应该上报事件
     * 
     * 根据上报间隔限制,避免频繁上报。
     * 
     * @param taskId 任务 ID
     * @param intervalSec 上报间隔 (秒)
     * @return true 应该上报
     * @return false 距离上次上报时间未到间隔
     */
    bool shouldReportEvent(const std::string& taskId, int intervalSec);

private:
    /**
     * @brief Pimpl 实现类 (前向声明)
     * 
     * 使用 Pimpl (Pointer to Implementation) 惯用法:
     * 
     * 优势:
     * 1. **编译隔离**: 修改 Impl 不需要重新编译使用 TaskService.h 的代码
     * 2. **ABI 稳定性**: 类的内存布局不变 (只有一个指针成员)
     * 3. **隐藏实现**: 私有成员和依赖不暴露在头文件中
     * 4. **减少头文件依赖**: 不需要 #include Vision.h 的实现细节
     * 
     * 经典例子:
     * @code
     * // TaskService.h (接口层)
     * class TaskService {
     *     struct Impl;               // 前向声明
     *     std::unique_ptr<Impl> impl_;  // Pimpl 指针
     * };
     * 
     * // TaskService.cpp (实现层)
     * struct TaskService::Impl {
     *     std::shared_ptr<Vision> vision;
     *     std::shared_ptr<MqttClient> mqtt;
     *     std::unordered_map<std::string, uint64_t> lastReportTime;
     *     // ... 所有私有成员都在这里
     * };
     * @endcode
     * 
     * 面试要点:
     * - Q: Pimpl 的缺点?
     * - A: 1) 额外的内存分配 (new Impl)
     *      2) 额外的间接访问 (指针解引用)
     *      3) 代码复杂度增加
     *      适用于 API 稳定性要求高的库 (如 Qt, Boost)
     */
    struct Impl;
    std::unique_ptr<Impl> impl_;  ///< Pimpl 指针 (unique_ptr 保证独占所有权)
    
    /**
     * @brief 过滤检测结果 - 只保留关心的类别
     * 
     * 根据 config.eventTypes 过滤检测框。
     * 
     * @param boxes 原始检测框列表
     * @param config 任务配置
     * @return std::vector<BoundingBox> 过滤后的检测框
     */
    std::vector<BoundingBox> filterByEventTypes(
        const std::vector<BoundingBox>& boxes,
        const TaskConfig& config);
    
    /**
     * @brief DetectionEvent 转 JSON 字符串
     * 
     * @param event 事件数据
     * @return std::string JSON 字符串
     */
    std::string eventToJson(const DetectionEvent& event);
};

}  // namespace task
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_TASK_SERVICE_H_
