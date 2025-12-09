/**
 * @file TaskService.cpp
 * @brief 任务服务层实现 - 业务逻辑编排核心
 * 
 * 实现要点:
 * 1. Pimpl 惯用法 - 隐藏实现细节
 * 2. 无状态服务 - 通过参数传递状态,支持并发
 * 3. 依赖注入 - 构造函数注入 Vision 和 MQTT
 * 4. 异常安全 - 使用 RAII 和智能指针
 * 5. 性能优化 - 避免不必要的拷贝和分配
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-02 (Day 2)
 */

#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/utils/HttpClient.h"  // HTTP 客户端（用于 GeoDecodeAPI）
#include "esdk_sophon/core/Config.h"       // 配置管理（用于获取 GeoDecodeAPI 配置）
#include "esdk_sophon/vision/DetectorFactory.h"  // 检测器工厂
#include "esdk_sophon/vision/IDetector.h"        // 检测器接口
#include "esdk_sophon/vision/VisionConfigLoader.h" // 配置加载器
#include "esdk_sophon/vision/VisionUtils.h"      // 可视化工具（绘制检测框）
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cmath>
#include <unordered_map>
#include <mutex>
#include <cstdio>  // std::remove (删除文件)

namespace esdk_sophon {
namespace task {

// ==================== Pimpl 实现类 ====================

/**
 * @brief TaskService 的私有实现类
 * 
 * 存储所有私有成员和内部状态。
 * 使用 Pimpl 惯用法,隐藏实现细节,减少头文件依赖。
 */
struct TaskService::Impl {
    // ===== 依赖模块 (通过构造函数注入) =====
    // std::shared_ptr<vision::Vision> vision;   ///< 视觉检测模块(已废弃)
    std::shared_ptr<mqtt::MqttClient> mqtt;   ///< MQTT 客户端
    
    // ===== 视觉检测器 =====
    std::unique_ptr<vision::IDetector> detector;  ///< 目标检测器
    std::mutex detectorMutex;                     ///< 保护检测器的互斥锁 (IDetector非线程安全)
    
    // ===== 上报时间记录 (用于限流) =====
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastReportTime;
    std::mutex reportTimeMutex;  ///< 保护 lastReportTime 的互斥锁
    
    // ===== Logger =====
    core::Logger& logger;
    
    /**
     * @brief 构造函数
     */
    Impl(std::shared_ptr<mqtt::MqttClient> m)
        : mqtt(m)
        , logger(core::Logger::getInstance())
    {
        // 初始化检测器
        try {
            vision::VisionConfigLoader loader;
            // 默认加载 PPYOLOE 配置
            // TODO: 可以从 Config 中读取 active_detector_type
            vision::DetectorConfig config = loader.loadDetectorConfig("ppyoloe");
            
            // 使用工厂创建检测器
            detector = vision::DetectorFactory::create(vision::DetectorType::PPYOLOE, config);
            
            logger.info("TaskService::Impl 检测器初始化成功: " + detector->getName());
        } catch (const std::exception& e) {
            logger.error("TaskService::Impl 检测器初始化失败: " + std::string(e.what()));
            // 不抛出异常，允许服务在无检测器的情况下运行（降级）
        }
        
        logger.info("TaskService::Impl 初始化完成");
    }
    
    /**
     * @brief 析构函数
     */
    ~Impl() {
        logger.info("TaskService::Impl 销毁");
    }
};

// ==================== 构造和析构 ====================

/**
 * @brief 构造函数 - 依赖注入
 * 
 * 面试要点:
 * - 依赖注入 (DI) 模式的优势
 * - 为什么用 shared_ptr? (共享所有权,多个任务共享同一个 MQTT)
 */
TaskService::TaskService(std::shared_ptr<mqtt::MqttClient> mqtt)
    : impl_(std::make_unique<Impl>(mqtt))
{
    // 参数验证
    if (!mqtt) {
        throw std::invalid_argument("TaskService: mqtt 参数不能为 nullptr");
    }
    
    impl_->logger.info("TaskService 创建成功");
}

/**
 * @brief 析构函数
 * 
 * 必须在 .cpp 中定义,因为 Impl 的完整定义在这里。
 * 如果在 .h 中用 = default,编译器会报错: incomplete type。
 */
TaskService::~TaskService() = default;

/**
 * @brief 移动构造函数
 */
TaskService::TaskService(TaskService&&) noexcept = default;

/**
 * @brief 移动赋值运算符
 */
TaskService& TaskService::operator=(TaskService&&) noexcept = default;

// ==================== 核心业务方法 ====================

/**
 * @brief 处理单帧图像 - 完整业务流程 ⭐
 * 
 * 这是 TaskService 的核心方法,编排了完整的帧处理流程。
 * 
 * 流程:
 * 1. 目标检测 (Vision::detect)
 * 2. 过滤目标 (只保留关心的类别)
 * 3. 检查上报间隔 (限流)
 * 4. 构建事件 (图像编码、GPS 转换等)
 * 5. 发布事件 (MQTT)
 * 
 * 性能分析:
 * - detectObjects: ~50ms (TPU 推理)
 * - filterByEventTypes: ~1ms (遍历)
 * - buildEvent: ~15ms (JPEG 编码 + Base64)
 * - publishEvent: ~2ms (MQTT)
 * - 总计: ~68ms (约 15 FPS)
 * 
 * @param frame 输入图像帧
 * @param config 任务配置
 * @param outBoxes [输出] 过滤后的检测框列表
 * @param fileName 文件名（可选，用于 GPS 计算）
 * @return true 处理成功，false 处理失败
 */
bool TaskService::processFrame(const cv::Mat& frame, 
                              const TaskConfig& config,
                              std::vector<BoundingBox>& outBoxes,
                              const std::string& fileName) {
    // 📌 诊断日志：确认函数被调用
    static int callCount = 0;
    callCount++;
    if (callCount == 1 || callCount % 30 == 0) {
        impl_->logger.info("🎬 [TaskService] processFrame 被调用 (第 " + std::to_string(callCount) + " 次): " +
                          "taskId=" + config.taskId + ", 帧尺寸=" + 
                          std::to_string(frame.cols) + "x" + std::to_string(frame.rows));
    }
    
    try {
        // 1. 目标检测
        std::vector<BoundingBox> boxes;
        if (!detectObjects(frame, config, boxes)) {
            impl_->logger.warning("❌ [TaskService] 检测器调用失败: taskId=" + config.taskId);
            return false;
        }
        
        impl_->logger.debug("✅ [TaskService] 检测器返回 " + std::to_string(boxes.size()) + " 个原始检测框");
        
        // 2. 过滤目标 (只保留关心的类别)
        boxes = filterByEventTypes(boxes, config);
        
        impl_->logger.debug("🔍 [TaskService] 过滤后剩余 " + std::to_string(boxes.size()) + " 个目标框");
        
        // 输出过滤后的结果供外部使用
        outBoxes = boxes;
        
        // 如果没有检测到目标,直接返回
        if (boxes.empty()) {
            impl_->logger.debug("ℹ️ [TaskService] 未检测到关注的目标类别 (过滤后为空): taskId=" + config.taskId);
            return false;  // 注意: 这不是错误，只是没有检测到目标
        }
        
        // 3. 检查上报间隔（仅直播流任务需要限流）
        // 媒体文件任务(DETECTION_MEDIAFILE)不需要间隔，每张图片都立即上报
        if (config.type == TaskType::DETECTION_LIVESTREAM) {
            if (!shouldReportEvent(config.taskId, config.reportIntervalSec)) {
                impl_->logger.debug("上报间隔未到: taskId=" + config.taskId);
                return false;
            }
        }
        
        // 4. 构建事件 (取第一个事件类型)
        if (config.eventTypes.empty()) {
            impl_->logger.error("eventTypes 为空: taskId=" + config.taskId);
            return false;
        }
        
        DetectionEvent event = buildEvent(frame, boxes, config, config.eventTypes[0], fileName);
        
        // 5. 发布事件
        // 📌 暂时注释掉视频流的事件推送功能
        // 目前视频流任务只做检测，不向云端推送事件
        // 如需恢复，取消下面的注释即可
        /*
        if (!publishEvent(event, config.deviceSn)) {
            impl_->logger.error("发布事件失败: taskId=" + config.taskId);
            return false;
        }
        */
        
        impl_->logger.debug("成功处理帧: taskId=" + config.taskId + 
                          ", 检测到 " + std::to_string(boxes.size()) + " 个目标 (事件推送已禁用)");
        return true;
        
    } catch (const std::exception& e) {
        impl_->logger.error("processFrame 异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 目标检测 - 调用 Vision 模块
 * 
 * 封装 Vision::detect() 调用,并将结果转换为 BoundingBox 格式。
 */
bool TaskService::detectObjects(const cv::Mat& frame, 
                                const TaskConfig& config,
                                std::vector<BoundingBox>& boxes) {
    try {
        // 清空结果列表
        boxes.clear();
        
        // 1. 检查检测器是否可用
        std::lock_guard<std::mutex> lock(impl_->detectorMutex);
        if (!impl_->detector) {
            // 降级模式：如果没有检测器，返回空列表（或者模拟数据）
            impl_->logger.error("❌ 检测器未初始化！请检查:");
            impl_->logger.error("   1. 模型文件是否存在？");
            impl_->logger.error("   2. config/vision.json 配置是否正确？");
            impl_->logger.error("   3. 启动日志中是否有 '✅ PP-YOLOE检测器初始化完成!' ?");
            impl_->logger.error("   4. 是否有 'TaskService::Impl 检测器初始化失败' 错误？");
            
            // 📌 [调试] 保持模拟检测框，以便在无模型环境下也能验证流程
            // BoundingBox dummyBox;
            // dummyBox.x = 100; dummyBox.y = 100; dummyBox.w = 200; dummyBox.h = 150;
            // dummyBox.classId = 0; dummyBox.className = "person"; dummyBox.confidence = 0.95f;
            // boxes.push_back(dummyBox);
            
            return false;
        }
        
        // 2. 执行检测
        vision::DetectionResult result = impl_->detector->detect(frame);
        
        // 3. 转换结果格式
        for (const auto& detBox : result.boxes) {
            BoundingBox box;
            box.x = static_cast<float>(detBox.x);
            box.y = static_cast<float>(detBox.y);
            box.w = static_cast<float>(detBox.width);
            box.h = static_cast<float>(detBox.height);
            box.classId = detBox.classId;
            box.className = detBox.className;
            box.confidence = detBox.confidence;
            
            // 经纬度暂时置0，后续由 calculateGPSCoordinates 填充
            box.lon = 0.0;
            box.lat = 0.0;
            
            boxes.push_back(box);
        }
        
        impl_->logger.debug("检测完成: 检测到 " + std::to_string(boxes.size()) + " 个目标, 耗时: " + 
                           std::to_string(result.getTotalTime()) + " ms");
        return true;
        
    } catch (const std::exception& e) {
        impl_->logger.error("detectObjects 异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 构建检测事件
 * 
 * 将检测结果封装为 DetectionEvent,包括:
 * - UUID 生成
 * - 图像编码 (JPEG + Base64)
 * - GPS 坐标转换
 * - 时间戳
 */
DetectionEvent TaskService::buildEvent(const cv::Mat& frame,
                                       const std::vector<BoundingBox>& boxes,
                                       const TaskConfig& config,
                                       const EventType& eventType,
                                       const std::string& fileName) {
    DetectionEvent event;
    
    try {
        // 1. 基本信息
        event.uuid = generateUUID();
        event.taskId = config.taskId;
        event.eventType = eventType.id;
        event.mainType = eventType.mainType;
        event.eventDescribe = eventType.eventDescribe;
        
        // 2. 图像编码（⭐ 关键优化：缩小图片尺寸避免 Base64 过大）
        // 原图 4032x3024 → 压缩到 800x600（约 100KB）
        // 避免 Base64 编码耗时过长导致 MQTT 心跳超时
        cv::Mat resizedFrame;
        const int MAX_WIDTH = 800;
        const int MAX_HEIGHT = 600;
        
        // 计算缩放比例
        double scale = 1.0;
        if (frame.cols > MAX_WIDTH || frame.rows > MAX_HEIGHT) {
            scale = std::min(
                static_cast<double>(MAX_WIDTH) / frame.cols,
                static_cast<double>(MAX_HEIGHT) / frame.rows
            );
            
            int newWidth = static_cast<int>(frame.cols * scale);
            int newHeight = static_cast<int>(frame.rows * scale);
            
            cv::resize(frame, resizedFrame, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);
            
            impl_->logger.debug("图片缩放: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) + 
                              " → " + std::to_string(newWidth) + "x" + std::to_string(newHeight));
        } else {
            resizedFrame = frame.clone();  // 克隆，因为后续要在上面画框
        }
        
        // ⭐ 绘制检测框可视化
        // 将检测框坐标按缩放比例调整后绘制
        if (!boxes.empty()) {
            std::vector<BoundingBox> scaledBoxes;
            scaledBoxes.reserve(boxes.size());
            
            for (const auto& box : boxes) {
                BoundingBox scaledBox = box;
                scaledBox.x = static_cast<float>(box.x * scale);
                scaledBox.y = static_cast<float>(box.y * scale);
                scaledBox.w = static_cast<float>(box.w * scale);
                scaledBox.h = static_cast<float>(box.h * scale);
                scaledBoxes.push_back(scaledBox);
            }
            
            // 调用 VisionUtils 的可视化方法
            vision::VisionUtils::drawDetections(resizedFrame, scaledBoxes);
            impl_->logger.debug("检测框可视化完成: 绘制了 " + std::to_string(boxes.size()) + " 个框");
        }
        
        std::vector<uint8_t> jpegBuffer;
        if (encodeImageToJPEG(resizedFrame, 85, jpegBuffer)) {
            event.pictureBase64 = encodeBase64(jpegBuffer);
            event.pictureCode = ".jpg";
            
            impl_->logger.debug("图片编码完成: JPEG=" + std::to_string(jpegBuffer.size()) + " bytes, " +
                              "Base64=" + std::to_string(event.pictureBase64.size()) + " chars");
        } else {
            impl_->logger.warning("图像编码失败,将不包含图片");
        }
        
        // 3. 时间戳（格式：yyyy-mm-dd hh:mm:ss）
        event.createTime = getCurrentTimeString();
        
        // 4. GPS 坐标计算（使用 GeoDecodeAPI）
        // event.latitude/longitude: 所有检测框中心点 GPS 坐标的平均值
        // event.points[i].lat/lon: 每个检测框中心点的实际 GPS 坐标
        // 
        // 图片路径处理：
        // - 媒体文件任务(MEDIAFILE): 使用原始文件名 fileName
        //   fileName 格式: "DJI_0123.JPG" (从 DJI MediaFile.file_name 获取)
        // - 直播流任务(LIVESTREAM): 传递空字符串，由 calculateGPSCoordinates 自动生成临时文件
        std::string imagePath;
        if (!fileName.empty()) {
            // 媒体文件任务：使用原始文件名（保留 DJI 原始命名）
            imagePath = fileName;
            impl_->logger.debug("使用原始文件名: " + fileName);
        } else {
            // 直播流任务：空字符串，自动生成临时文件
            imagePath = "";
        }
        
        if (!calculateGPSCoordinates(frame, boxes, event, imagePath)) {
            // GPS 计算失败，使用降级方案
            impl_->logger.warning("GPS 计算失败，使用降级方案（pixelToGPS）");
            
            // 降级方案：使用无人机GPS + 像素偏移估算
            // TODO: 从 device 模块获取真实的无人机 GPS 和高度
            double droneLatitude = 31.230391;   // 临时假设值
            double droneLongitude = 121.473701;
            double droneAltitude = 100.0;       // 假设飞行高度 100米
            double droneYaw = 0.0;              // 假设航向角 0度
            
            // 计算每个检测框的 GPS 坐标
            double sumLat = 0.0, sumLon = 0.0;
            int validCount = 0;
            
            for (const auto& box : boxes) {
                BoundingBox eventBox = box;
                
                // 计算检测框中心点的像素坐标
                float centerX = box.x + box.w / 2.0f;
                float centerY = box.y + box.h / 2.0f;
                
                // 转换为 GPS 坐标
                double targetLat, targetLon;
                if (pixelToGPS(centerX, centerY,
                              frame.cols, frame.rows,
                              droneLatitude, droneLongitude,
                              droneAltitude, droneYaw,
                              targetLat, targetLon)) {
                    eventBox.lat = targetLat;
                    eventBox.lon = targetLon;
                    
                    sumLat += targetLat;
                    sumLon += targetLon;
                    validCount++;
                }
                
                event.points.push_back(eventBox);
            }
            
            // 设置事件坐标为平均值
            if (validCount > 0) {
                event.latitude = sumLat / validCount;
                event.longitude = sumLon / validCount;
            } else {
                // 所有坐标都无效，使用无人机坐标
                event.latitude = droneLatitude;
                event.longitude = droneLongitude;
            }
        }
        // 注意：如果 calculateGPSCoordinates() 成功，
        // event.points 已经在函数内部填充了，这里不需要再处理
        
        impl_->logger.debug("事件构建完成: UUID=" + event.uuid);
        
    } catch (const std::exception& e) {
        impl_->logger.error("buildEvent 异常: " + std::string(e.what()));
    }
    
    return event;
}

/**
 * @brief 发布事件到 MQTT
 * 
 * 将 DetectionEvent 序列化为 JSON 并发布到 MQTT。
 */
bool TaskService::publishEvent(const DetectionEvent& event, const std::string& deviceSn) {
    try {
        // 1. 构建 topic
        std::string topic = "drone/" + deviceSn + "/info/event";
        
        // 2. 转换为 JSON
        std::string jsonPayload = eventToJson(event);
        
        // 3. ⭐ 异步发布到 MQTT（QoS=0，不等待确认）
        // 优点：不阻塞工作线程，提高并发性能
        // 缺点：可能丢消息（但我们有 EventCache 兜底重试）
        bool success = impl_->mqtt->publish(topic, jsonPayload, 
                                           0,      // QoS=0（异步，不等待确认）
                                           false); // retained=false
        
        if (success) {
            impl_->logger.info("事件已发布: UUID=" + event.uuid + ", topic=" + topic);
        } else {
            impl_->logger.error("事件发布失败: UUID=" + event.uuid);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        impl_->logger.error("publishEvent 异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 发布 JSON 事件到指定 MQTT 主题
 * 
 * 此方法用于 EventCache 回调和自定义事件发布。
 * 
 * ⭐ 注意：使用 QoS=0 (fire-and-forget) 模式
 * - 优点：不阻塞线程，高吞吐量
 * - 缺点：可能丢失消息（但网络稳定时丢失率 < 0.1%）
 * - 兜底：EventCache 提供定时重试机制
 */
bool TaskService::publishMqttMessage(const std::string& topic, const nlohmann::json& eventJson) {
    try {
        // 1. 序列化 JSON
        std::string jsonPayload = eventJson.dump();
        
        // 2. ⭐ 异步发布到 MQTT（QoS=0，不阻塞）
        bool success = impl_->mqtt->publish(topic, jsonPayload, 
                                           0,      // QoS=0（异步，不等待确认）
                                           false); // retained=false
        
        if (success) {
            impl_->logger.info("MQTT 消息已发布: topic=" + topic + 
                             ", payload_size=" + std::to_string(jsonPayload.size()));
        } else {
            impl_->logger.error("MQTT 消息发布失败: topic=" + topic);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        impl_->logger.error("publishMqttMessage 异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== 工具方法 ====================

/**
 * @brief 图像编码为 JPEG
 * 
 * 使用 OpenCV 的 imencode 函数。
 * 
 * 面试要点:
 * - JPEG 有损压缩,适合照片
 * - quality 参数: 1-100, 推荐 85 (质量与大小的平衡点)
 * - PNG 无损压缩,但文件更大,适合截图
 */
bool TaskService::encodeImageToJPEG(const cv::Mat& frame, 
                                    int quality,
                                    std::vector<uint8_t>& buffer) {
    try {
        // 设置 JPEG 编码参数
        std::vector<int> params;
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(quality);  // 85 是质量与大小的平衡点
        
        // 编码
        bool success = cv::imencode(".jpg", frame, buffer, params);
        
        if (success) {
            impl_->logger.debug("JPEG 编码成功: " + 
                               std::to_string(buffer.size()) + " 字节");
        } else {
            impl_->logger.error("JPEG 编码失败");
        }
        
        return success;
        
    } catch (const std::exception& e) {
        impl_->logger.error("encodeImageToJPEG 异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief Base64 编码
 * 
 * Base64 是一种将二进制数据编码为 ASCII 文本的方法。
 * 
 * 原理:
 * - 每 3 个字节 (24 bits) 编码为 4 个字符 (6 bits 每个)
 * - 字符集: A-Z, a-z, 0-9, +, / (共 64 个)
 * - 不足 3 字节时用 = 补齐
 * 
 * 例子:
 * - 输入: [0x48, 0x65, 0x6C] ("Hel")
 * - 输出: "SGVs"
 * 
 * 面试要点:
 * - 为什么需要 Base64? JSON 不支持二进制数据
 * - 编码后大小: 约为原始大小的 4/3 倍 (增加 33%)
 * - Base64 vs Base58 vs Base32
 */
std::string TaskService::encodeBase64(const std::vector<uint8_t>& data) {
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string encoded;
    encoded.reserve((data.size() + 2) / 3 * 4);  // 预分配内存,避免重分配
    
    size_t i = 0;
    while (i < data.size()) {
        // 读取 3 个字节 (24 bits)
        uint32_t b1 = i < data.size() ? data[i++] : 0;
        uint32_t b2 = i < data.size() ? data[i++] : 0;
        uint32_t b3 = i < data.size() ? data[i++] : 0;
        
        uint32_t triple = (b1 << 16) | (b2 << 8) | b3;
        
        // 分成 4 个 6-bit 组,编码为 4 个字符
        encoded.push_back(base64_chars[(triple >> 18) & 0x3F]);
        encoded.push_back(base64_chars[(triple >> 12) & 0x3F]);
        encoded.push_back(base64_chars[(triple >> 6) & 0x3F]);
        encoded.push_back(base64_chars[triple & 0x3F]);
    }
    
    // 处理填充 (=)
    size_t padding = (3 - data.size() % 3) % 3;
    for (size_t j = 0; j < padding; ++j) {
        encoded[encoded.size() - 1 - j] = '=';
    }
    
    return encoded;
}

/**
 * @brief 像素坐标转 GPS 坐标
 * 
 * 简化算法 (假设地面平坦,相机垂直向下):
 * 
 * 1. 计算像素偏移 (相对于图像中心)
 * 2. 根据飞行高度和相机 FOV 计算地面距离
 * 3. 根据航向角旋转坐标系
 * 4. 转换为经纬度偏移
 * 
 * 公式:
 * - 地面距离 = 像素偏移 * (高度 / 焦距)
 * - 经度偏移 = 东西距离 / (111320 * cos(纬度))
 * - 纬度偏移 = 南北距离 / 111320
 * 
 * @note 这是简化实现,实际项目需要考虑:
 *       1. 相机内参 (焦距、畸变系数)
 *       2. 云台角度 (pitch, roll, yaw)
 *       3. 地形高程 (DEM 数据)
 *       4. 地球曲率 (WGS84 椭球)
 * 纠正：计算实际GPS的模块在旧项目esdk_on_sophon_old中的esdk_on_sophon_old\src\yolov10\image_processor_yolov10.cpp可以找到
 */
bool TaskService::pixelToGPS(float pixelX, float pixelY,
                             int imageWidth, int imageHeight,
                             double droneLat, double droneLon,
                             double droneAlt, double droneYaw,
                             double& targetLat, double& targetLon) {
    try {
        // 1. 计算像素偏移 (相对于图像中心,单位:像素)
        float dx = pixelX - imageWidth / 2.0f;
        float dy = pixelY - imageHeight / 2.0f;
        
        // 2. 简化假设: 相机 FOV = 90度, 焦距 = 图像宽度
        // 地面距离 = 像素偏移 * (高度 / 焦距)
        float groundX = dx * droneAlt / imageWidth;   // 东西方向 (米)
        float groundY = dy * droneAlt / imageWidth;   // 南北方向 (米)
        
        // 3. 根据航向角旋转坐标系
        float yawRad = droneYaw * M_PI / 180.0;
        float rotatedX = groundX * cos(yawRad) - groundY * sin(yawRad);
        float rotatedY = groundX * sin(yawRad) + groundY * cos(yawRad);
        
        // 4. 转换为经纬度偏移
        // 经度: 1度 ≈ 111320 * cos(纬度) 米 (在赤道最大)
        // 纬度: 1度 ≈ 111320 米 (固定)
        double latRad = droneLat * M_PI / 180.0;
        double lonOffset = rotatedX / (111320.0 * cos(latRad));
        double latOffset = -rotatedY / 111320.0;  // Y轴向下为正,纬度向北为正
        
        targetLon = droneLon + lonOffset;
        targetLat = droneLat + latOffset;
        
        impl_->logger.debug("GPS 转换: pixel(" + std::to_string(pixelX) + "," + 
                           std::to_string(pixelY) + ") → GPS(" + 
                           std::to_string(targetLat) + "," + 
                           std::to_string(targetLon) + ")");
        
        return true;
        
    } catch (const std::exception& e) {
        impl_->logger.error("pixelToGPS 异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 计算检测框的 GPS 坐标（使用 GeoDecodeAPI）
 * 
 * 这是完整的 GPS 计算方案（方案A），调用本地 GeoDecodeAPI 服务。
 * GeoDecodeAPI 是一个 JAR 包服务，部署在设备上，通过 HTTP 访问。
 * 
 * 工作原理:
 * 1. 保存图片到临时文件（API 需要读取 EXIF 信息）
 * 2. 调用 API 获取每个检测框的 GPS 坐标
 * 3. 计算平均值作为事件坐标
 * 4. 填充 points 数组中每个框的坐标
 * 
 * 相比 pixelToGPS() 的优势:
 * - 精度更高：考虑了相机内参、云台角度、地形高程
 * - 数据来源可靠：直接从无人机 EXIF 数据计算
 * - 与旧项目一致：算法和精度保持一致
 */
bool TaskService::calculateGPSCoordinates(const cv::Mat& frame,
                                         const std::vector<BoundingBox>& boxes,
                                         DetectionEvent& event,
                                         const std::string& imagePath) {
    // ========================================
    // 直播流任务：暂不支持 GPS 计算
    // ========================================
    // 直播流抓取的帧没有 EXIF 信息，GeoDecodeAPI 无法计算
    // 后续可以从 Device 模块获取无人机实时位置
    if (imagePath.empty()) {
        impl_->logger.debug("直播流任务暂不支持 GPS 计算，使用降级方案");
        return false;  // 返回 false，由 buildEvent 使用 pixelToGPS 降级方案
    }
    
    // ========================================
    // 媒体文件任务：调用 GeoDecodeAPI
    // ========================================
    
    // 1. 从配置文件获取 GeoDecodeAPI 配置
    auto& config = core::Config::getInstance();
    std::string apiUrl = config.getString(
        "geo_decode.api_url", 
        "http://127.0.0.1:8122/geoDecode/arithmetic/getLonLatByPoints");
    std::string mediaFileDir = config.getString(
        "geo_decode.media_file_directory", 
        "/data/Edge-SDK/build/bin");
    int timeoutMs = config.getInt("geo_decode.timeout_ms", 5000);
    
    // 2. 构建绝对路径
    // imagePath 是文件名（如 "DJI_20251125135511_0004_V.jpeg"）
    // 拼接成绝对路径（如 "/data/Edge-SDK/build/bin/DJI_20251125135511_0004_V.jpeg"）
    std::string actualImagePath = mediaFileDir + "/" + imagePath;
    impl_->logger.debug("📍 GPS 计算: 图片绝对路径=" + actualImagePath);
    
    // 3. 构建 API 请求
    // GeoDecodeAPI 请求格式:
    // {
    //   "path": "/data/Edge-SDK/build/bin/DJI_0123.JPG",
    //   "points": [{"id": 1, "x": 550, "y": 300}, ...]  // 中心点坐标
    // }
    nlohmann::json request;
    request["path"] = actualImagePath;
    request["points"] = nlohmann::json::array();
    
    int boxId = 1;  // 检测框序号从 1 开始
    for (const auto& box : boxes) {
        nlohmann::json point;
        // ⚠️ API 需要的是检测框中心点坐标，不是左上角+宽高
        int centerX = static_cast<int>(box.x + box.w / 2.0f);
        int centerY = static_cast<int>(box.y + box.h / 2.0f);
        point["id"] = boxId++;
        point["x"] = centerX;
        point["y"] = centerY;
        request["points"].push_back(point);
    }
    
    impl_->logger.info("📍 GeoDecodeAPI 请求: 图片=" + actualImagePath + 
                      ", 检测框数=" + std::to_string(boxes.size()));
    
    // 4. 调用 GeoDecodeAPI（HTTP POST）
    nlohmann::json response;
    if (!utils::HttpClient::post(apiUrl, request, timeoutMs, response)) {
        impl_->logger.error("❌ GeoDecodeAPI 调用失败: " + 
                           utils::HttpClient::getLastError());
        return false;
    }
    
    // 4. 解析响应
    // GeoDecodeAPI 响应格式:
    // {
    //   "code": 200,
    //   "data": [{"longitude": 121.47, "latitude": 31.23}, ...]
    // }
    if (!response.contains("data") || !response["data"].is_array()) {
        impl_->logger.error("❌ GeoDecodeAPI 响应格式错误: 缺少 'data' 字段");
        impl_->logger.debug("响应内容: " + response.dump());
        return false;
    }
    
    auto dataArray = response["data"];
    if (dataArray.size() != boxes.size()) {
        impl_->logger.error("❌ GeoDecodeAPI 返回数量不匹配: 期望=" + 
                           std::to_string(boxes.size()) + 
                           ", 实际=" + std::to_string(dataArray.size()));
        return false;
    }
    
    // 5. 填充 GPS 坐标
    double sumLat = 0.0, sumLon = 0.0;
    int effectiveCount = 0;
    
    for (size_t i = 0; i < boxes.size(); ++i) {
        BoundingBox eventBox = boxes[i];
        
        // 检查 GPS 坐标是否有效（非 null）
        // API 返回 null 表示该点在图片边缘，无法计算
        if (!dataArray[i]["longitude"].is_null() && 
            !dataArray[i]["latitude"].is_null()) {
            
            double lon = dataArray[i]["longitude"].get<double>();
            double lat = dataArray[i]["latitude"].get<double>();
            
            eventBox.lon = lon;
            eventBox.lat = lat;
            
            sumLon += lon;
            sumLat += lat;
            effectiveCount++;
            
            impl_->logger.debug("  检测框[" + std::to_string(i) + "] GPS: (" + 
                               std::to_string(lat) + ", " + std::to_string(lon) + ")");
        } else {
            // GPS 坐标无效
            eventBox.lon = 0.0;
            eventBox.lat = 0.0;
            impl_->logger.warning("  检测框[" + std::to_string(i) + "] GPS 坐标无效（null）");
        }
        
        event.points.push_back(eventBox);
    }
    
    // 6. 设置事件经纬度为平均值
    if (effectiveCount > 0) {
        event.longitude = sumLon / effectiveCount;
        event.latitude = sumLat / effectiveCount;
        
        impl_->logger.info("✅ GPS 计算成功: 平均坐标=(" + 
                          std::to_string(event.latitude) + ", " +
                          std::to_string(event.longitude) + "), " +
                          "有效点数=" + std::to_string(effectiveCount) + "/" +
                          std::to_string(boxes.size()));
        return true;
    } else {
        // 所有检测框的 GPS 坐标都无效
        impl_->logger.warning("⚠️ 所有检测框的 GPS 坐标都无效");
        return false;
    }
}

/**
 * @brief 生成 UUID v4
 * 
 * UUID (Universally Unique Identifier) 全局唯一标识符。
 * 版本 4 使用随机数生成,格式: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 * 
 * 面试要点:
 * - UUID v1: 基于时间戳 + MAC 地址 (可能泄露隐私)
 * - UUID v4: 纯随机 (推荐)
 * - UUID v5: 基于 SHA-1 哈希
 * - 碰撞概率: 2^-122 ≈ 1.7×10^-37 (可以忽略)
 */
std::string TaskService::generateUUID() {
    // 随机数生成器 (thread_local 避免多线程竞争)
    thread_local std::random_device rd;
    thread_local std::mt19937_64 gen(rd());
    thread_local std::uniform_int_distribution<uint64_t> dis;
    
    // 生成 128 bits 随机数
    uint64_t high = dis(gen);
    uint64_t low = dis(gen);
    
    // 设置版本号 (v4) 和 variant 位
    high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;  // version = 4
    low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;   // variant = 10b
    
    // 格式化为字符串
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    // xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    oss << std::setw(8) << (high >> 32);          // 8 hex
    oss << '-';
    oss << std::setw(4) << ((high >> 16) & 0xFFFF);  // 4 hex
    oss << '-';
    oss << std::setw(4) << (high & 0xFFFF);       // 4 hex (包含 version)
    oss << '-';
    oss << std::setw(4) << (low >> 48);           // 4 hex (包含 variant)
    oss << '-';
    oss << std::setw(12) << (low & 0xFFFFFFFFFFFFULL);  // 12 hex
    
    return oss.str();
}

/**
 * @brief 获取当前时间字符串（格式：yyyy-mm-dd hh:mm:ss）
 * 
 * 平台要求的时间格式: "2025-11-02 10:30:00"
 * 注意：使用本地时间（非UTC），不带毫秒和时区标识
 *   
 * 面试要点:
 * - ISO 8601 vs 自定义格式: 可读性和平台兼容性的权衡
 * - localtime vs gmtime: 本地时间 vs UTC时间
 *   * localtime: 受系统时区影响（中国为 UTC+8）
 *   * gmtime: 始终为UTC（国际标准）
 * - 线程安全: localtime_r (POSIX) / localtime_s (Windows)
 *   * localtime() 不安全（返回静态缓冲区）
 *   * localtime_r/s 安全（调用者提供缓冲区）
 * - std::put_time: C++11 时间格式化（类似 strftime）
 * 
 * @return 格式化的时间字符串（例如："2025-11-03 14:25:30"）
 */
std::string TaskService::getCurrentTimeString() {
    // 获取当前时间
    auto now = std::chrono::system_clock::now();
    
    // 转换为 time_t (秒级时间戳)
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    
    // 转换为本地时间（使用系统时区）
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &now_c);  // Windows: 参数顺序为 (tm*, time_t*)
#else
    localtime_r(&now_c, &tm);  // POSIX:   参数顺序为 (time_t*, tm*)
#endif
    
    // 格式化为 yyyy-mm-dd hh:mm:ss
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    return oss.str();
}

/**
 * @brief 检查是否应该上报事件
 * 
 * 使用上次上报时间缓存,避免频繁上报。
 * 
 * 线程安全: 使用 mutex 保护共享的 lastReportTime 映射。
 */
bool TaskService::shouldReportEvent(const std::string& taskId, int intervalSec) {
    std::lock_guard<std::mutex> lock(impl_->reportTimeMutex);
    
    auto now = std::chrono::steady_clock::now();
    
    // 查找上次上报时间
    auto it = impl_->lastReportTime.find(taskId);
    if (it == impl_->lastReportTime.end()) {
        // 第一次上报,记录时间
        impl_->lastReportTime[taskId] = now;
        return true;
    }
    
    // 计算时间差
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - it->second).count();
    
    if (elapsed >= intervalSec) {
        // 距离上次上报已经超过间隔,可以上报
        impl_->lastReportTime[taskId] = now;
        return true;
    }
    
    // 还未到上报间隔
    return false;
}

// ==================== 私有辅助方法 ====================

/**
 * @brief 过滤检测结果
 * 
 * 只保留 config.eventTypes 中指定的类别。
 * 
 * 例如:
 * - eventTypes[0].classIds = {0, 1}  (person, bicycle)
 * - 检测到 {person, car, bicycle}
 * - 过滤后保留 {person, bicycle}
 */
std::vector<BoundingBox> TaskService::filterByEventTypes(
    const std::vector<BoundingBox>& boxes,
    const TaskConfig& config) {
    
    if (config.eventTypes.empty()) {
        impl_->logger.debug("⚠️ 配置中没有 eventTypes，返回所有检测框");
        return boxes;  // 没有配置事件类型,返回全部
    }
    
    // 收集所有关心的类别 ID
    std::set<int> targetClassIds;
    for (const auto& eventType : config.eventTypes) {
        for (int classId : eventType.classIds) {
            targetClassIds.insert(classId);
        }
    }
    
    impl_->logger.debug("🔍 [过滤] 关注的类别ID: [" + 
                      [&targetClassIds]() {
                          std::string ids;
                          for (int id : targetClassIds) {
                              if (!ids.empty()) ids += ", ";
                              ids += std::to_string(id);
                          }
                          return ids;
                      }() + "]");
    
    // 过滤
    std::vector<BoundingBox> filtered;
    for (const auto& box : boxes) {
        impl_->logger.debug("   - 检测框: classId=" + std::to_string(box.classId) + 
                          " (" + box.className + "), 置信度=" + std::to_string(box.confidence));
        
        if (targetClassIds.count(box.classId) > 0) {
            filtered.push_back(box);
            //impl_->logger.debug("     ✅ 保留 (匹配关注类别)");
        } else {
            //impl_->logger.debug("     ⏭️ 过滤 (不在关注类别中)");
        }
    }
    
    impl_->logger.debug("🔍 [过滤结果] " + std::to_string(boxes.size()) + 
                       " → " + std::to_string(filtered.size()));
    
    return filtered;
}

/**
 * @brief DetectionEvent 转 JSON
 * 
 * 使用 nlohmann/json 库序列化。
 * 
 * 面试要点:
 * - JSON vs XML vs Protocol Buffers
 * - JSON 优势: 可读性好、广泛支持、轻量级
 * - JSON 缺点: 没有 schema 验证、不支持二进制、解析开销大
 */
std::string TaskService::eventToJson(const DetectionEvent& event) {
    nlohmann::json j;
    
    // 基本信息
    j["UUID"] = event.uuid;
    j["taskID"] = event.taskId;
    j["eventType"] = event.eventType;
    j["main_type"] = event.mainType;
    j["eventDescribe"] = event.eventDescribe;
    
    // 图片
    j["picture"] = event.pictureBase64;
    j["pictureCode"] = event.pictureCode;
    
    // GPS
    j["latitude"] = event.latitude;
    j["longitude"] = event.longitude;
    
    // 时间
    j["createTime"] = event.createTime;
    
    // 检测框列表
    nlohmann::json points = nlohmann::json::array();
    for (const auto& box : event.points) {
        nlohmann::json point;
        point["x"] = box.x;
        point["y"] = box.y;
        point["w"] = box.w;
        point["h"] = box.h;
        point["lon"] = box.lon;
        point["lat"] = box.lat;
        point["classId"] = box.classId;
        point["className"] = box.className;
        point["confidence"] = box.confidence;
        points.push_back(point);
    }
    j["points"] = points;
    
    return j.dump();  // 转为字符串
}

}  // namespace task
}  // namespace esdk_sophon
