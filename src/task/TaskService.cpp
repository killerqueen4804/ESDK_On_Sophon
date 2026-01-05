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
#include "esdk_sophon/vision/segmentation/Sam2Segmentor.h"  // SAM2 分割器
#include "esdk_sophon/vision/VisionConfigLoader.h" // 配置加载器
#include "esdk_sophon/vision/VisionUtils.h"      // 可视化工具（绘制检测框）
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cmath>
#include <limits>
#ifdef _WIN32
#include <direct.h>  // _mkdir
#include <sys/stat.h>  // _stat
#else
#include <sys/stat.h>   // mkdir
#include <sys/types.h>
#endif
#include <unordered_map>
#include <mutex>
#include <cstdio>  // std::remove (删除文件)
#include <fstream> // std::ifstream (文件检查)

#include "esdk_sophon/vision/VisionConfigLoader.h"  // 用于从labels_path读取类别名称(避免写死COCO)

namespace esdk_sophon {
namespace task {

// ============================================================================
// 平台兼容：Windows 与 Linux 在 stat/_stat 的类型与函数名上有差异
// ============================================================================
#ifdef _WIN32
using StatStruct = struct _stat;
static inline int statCompat(const char* path, StatStruct* st) { return ::_stat(path, st); }
#else
using StatStruct = struct stat;
static inline int statCompat(const char* path, StatStruct* st) { return ::stat(path, st); }
#endif

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
    
    // ===== SAM2 分割器 =====
    std::unique_ptr<vision::Sam2Segmentor> sam2Segmentor;  ///< SAM2 语义分割器
    bool useSam2;                                          ///< 是否启用 SAM2
    std::mutex segmentorMutex;                             ///< 保护分割器的互斥锁
    
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
        , useSam2(false)
        , logger(core::Logger::getInstance())
    {
        // ===== 1. 初始化目标检测器 =====
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
        
        // ===== 2. 初始化 SAM2 分割器 =====
        try {
            auto& config = core::Config::getInstance();
            
            // 读取 SAM2 配置
            bool enabled = config.getBool("segmentation.sam2.enabled", false);
            
            logger.info("🔧 [SAM2 初始化] enabled=" + std::string(enabled ? "true" : "false"));
            
            if (enabled) {
                std::string encoderPath = config.getString("segmentation.sam2.encoder_model_path");
                std::string decoderPath = config.getString("segmentation.sam2.decoder_model_path");
                int deviceId = config.getInt("segmentation.sam2.device_id", 0);
                
                logger.info("🔧 [SAM2 初始化] 正在加载模型...");
                logger.info("  📦 Encoder: " + encoderPath);
                logger.info("  📦 Decoder: " + decoderPath);
                logger.info("  🔌 Device ID: " + std::to_string(deviceId));
                
                // 检查文件是否存在
                std::ifstream encoderFile(encoderPath);
                std::ifstream decoderFile(decoderPath);
                if (!encoderFile.good()) {
                    logger.error("❌ [SAM2 初始化] Encoder 模型文件不存在: " + encoderPath);
                    useSam2 = false;
                    logger.info("SAM2 分割器初始化完成 (已禁用)");
                    return;
                }
                if (!decoderFile.good()) {
                    logger.error("❌ [SAM2 初始化] Decoder 模型文件不存在: " + decoderPath);
                    useSam2 = false;
                    logger.info("SAM2 分割器初始化完成 (已禁用)");
                    return;
                }
                
                // 创建分割器
                sam2Segmentor = std::make_unique<vision::Sam2Segmentor>();
                
                // 初始化 (modelPath=encoder, configPath=decoder)
                logger.info("🚀 [SAM2 初始化] 正在调用 init()...");
                if (!sam2Segmentor->init(encoderPath, decoderPath)) {
                    logger.error("❌ [SAM2 初始化] init() 返回 false (模型加载失败)");
                    sam2Segmentor.reset();  // 释放资源
                    useSam2 = false;
                } else {
                    useSam2 = true;
                    logger.info("✅ [SAM2 初始化] 初始化成功! useSam2=" + std::string(useSam2 ? "true" : "false"));
                }
            } else {
                logger.info("ℹ️ [SAM2 初始化] 配置文件中已禁用 (segmentation.sam2.enabled=false)");
                useSam2 = false;
            }
        } catch (const std::exception& e) {
            logger.error("❌ [SAM2 初始化] 异常: " + std::string(e.what()));
            useSam2 = false;
        }
        
        logger.info("TaskService::Impl 初始化完成 (useSam2=" + std::string(useSam2 ? "true" : "false") + ")");
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
 * @brief 处理单帧图像 - 完整业务流程 (算法路由入口) ⭐
 * 
 * 这是 TaskService 的核心方法,根据算法类型路由到不同的处理函数。
 * 
 * 路由规则:
 * - main_type = 100000 → processDetection() (目标检测)
 * - main_type = 100001 → processSegmentation() (语义分割)
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
                              const std::string& fileName,
                              const device::GpsPosition* gpsSnapshot) {
    try {
        // 1. 检查配置有效性
        if (config.eventTypes.empty()) {
            impl_->logger.error("❌ [TaskService] eventTypes 为空: taskId=" + config.taskId);
            return false;
        }
        
        // 2. 获取算法类型
        int mainType = config.eventTypes[0].mainType;
        
        // 3. 根据算法类型路由到不同的处理函数
        if (mainType == 100000) {
            // 目标检测 (返回矩形框)
            // 说明：gpsSnapshot 仅用于直播流“帧时刻经纬度”填充。
            return processDetection(frame, config, outBoxes, fileName, gpsSnapshot);
            
        } else if (mainType == 100001) {
            // 语义分割 (返回多边形)
            return processSegmentation(frame, config, outBoxes, fileName, gpsSnapshot);
            
        } else {
            impl_->logger.error("❌ [TaskService] 未知的 main_type: " + 
                               std::to_string(mainType) + ", taskId=" + config.taskId);
            return false;
        }
        
    } catch (const std::exception& e) {
        impl_->logger.error("❌ [TaskService] processFrame 异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 处理检测任务 (main_type=100000)
 * 
 * 这是原 processFrame 的检测逻辑部分,
 * 负责目标检测、过滤、事件构建和上报。
 */
bool TaskService::processDetection(const cv::Mat& frame, 
                                   const TaskConfig& config,
                                   std::vector<BoundingBox>& outBoxes,
                                   const std::string& fileName,
                                   const device::GpsPosition* gpsSnapshot) {
    // 📌 诊断日志：确认函数被调用
    static int callCount = 0;
    callCount++;
    if (callCount == 1 || callCount % 30 == 0) {
        impl_->logger.info("🎬 [检测任务] processDetection 被调用 (第 " + std::to_string(callCount) + " 次): " +
                          "taskId=" + config.taskId + ", 帧尺寸=" + 
                          std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
                          ", fileName=" + fileName);
    }
    
    try {
        // 1. 目标检测
        impl_->logger.info("🔍 [检测任务] 正在调用检测器... (taskId=" + config.taskId + ")");
        std::vector<BoundingBox> boxes;
        if (!detectObjects(frame, config, boxes)) {
            impl_->logger.warning("❌ [检测任务] 检测器调用失败: taskId=" + config.taskId + ", fileName=" + fileName);
            return false;
        }
        
        impl_->logger.info("✅ [检测任务] 检测器返回 " + std::to_string(boxes.size()) + 
                          " 个原始检测框 (taskId=" + config.taskId + ", fileName=" + fileName + ")");
        
        // 打印所有检测框详情 (DEBUG级别)
        if (!boxes.empty()) {
            impl_->logger.debug("📋 [检测任务] 原始检测框详情:");
            for (size_t i = 0; i < boxes.size() && i < 10; ++i) {  // 最多打印10个
                const auto& box = boxes[i];
                impl_->logger.debug("  [" + std::to_string(i) + "] " + box.className + 
                                   " (ID=" + std::to_string(box.classId) + 
                                   ", conf=" + std::to_string(box.confidence) + 
                                   ", box=" + std::to_string(box.x) + "," + std::to_string(box.y) + 
                                   "," + std::to_string(box.w) + "x" + std::to_string(box.h) + ")");
            }
            if (boxes.size() > 10) {
                impl_->logger.debug("  ... 还有 " + std::to_string(boxes.size() - 10) + " 个检测框未显示");
            }
        }
        
        // 2. 过滤目标 (只保留关心的类别)
        // 说明：云端下发通常是类别“名称”，TaskConfig::fromJson 已经基于 labels_path 做过 name->id 映射。
        //      这里打印更友好的日志：优先输出类别名称（来自 labels_path/coco.names），并同时输出关注ID。
        impl_->logger.info("🔍 [检测任务] 正在过滤目标类别... (关注类别: " +
                          [&config]() {
                              // 收集关注ID
                              std::set<int> targetClassIds;
                              for (const auto& et : config.eventTypes) {
                                  for (int id : et.classIds) {
                                      targetClassIds.insert(id);
                                  }
                              }

                              if (targetClassIds.empty()) {
                                  return std::string("无");
                              }

                              // 读取labels(例如 coco.names)，把ID映射回名称用于展示
                              std::vector<std::string> classes;
                              try {
                                  vision::VisionConfigLoader loader;
                                  vision::DetectorConfig detCfg = loader.loadDetectorConfig("ppyoloe");
                                  classes = detCfg.classes;
                              } catch (...) {
                                  // 读取失败不影响主流程，只是日志里显示不出名称
                              }

                              std::string text;
                              for (int id : targetClassIds) {
                                  if (!text.empty()) text += ", ";
                                  if (!classes.empty() && id >= 0 && id < static_cast<int>(classes.size())) {
                                      text += classes[static_cast<size_t>(id)] + "(ID=" + std::to_string(id) + ")";
                                  } else {
                                      text += "ID=" + std::to_string(id);
                                  }
                              }
                              return text;
                          }() + ")");
        
        boxes = filterByEventTypes(boxes, config);
        
        impl_->logger.info("🔍 [检测任务] 过滤后剩余 " + std::to_string(boxes.size()) + 
                          " 个目标框 (taskId=" + config.taskId + ", fileName=" + fileName + ")");
        
        // 输出过滤后的结果供外部使用
        outBoxes = boxes;
        
        // 如果没有检测到目标,直接返回
        if (boxes.empty()) {
            impl_->logger.info("ℹ️ [检测任务] 未检测到关注的目标类别 (过滤后为空): taskId=" + 
                              config.taskId + ", fileName=" + fileName);
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
        
        impl_->logger.info("📦 [检测任务] 正在构建事件...");
        DetectionEvent event = buildEvent(frame, boxes, config, config.eventTypes[0], fileName);

        // 4.1 直播流：用“取帧时刻”的 GPS 覆盖事件顶层经纬度
        // 说明：用户明确只需要顶层 longitude/latitude，bbox 里的 location 不需要管。
        if (config.type == TaskType::DETECTION_LIVESTREAM) {
            const double kNull = std::numeric_limits<double>::quiet_NaN();
            const bool hasSnapshot = (gpsSnapshot != nullptr);
            const bool snapshotValid = (gpsSnapshot != nullptr && gpsSnapshot->isValid());
            impl_->logger.info(std::string("📍 [直播GPS] gpsSnapshot=") + (hasSnapshot ? "non-null" : "null") +
                               ", snapshotValid=" + (snapshotValid ? "true" : "false"));

            if (snapshotValid) {
                event.latitude = gpsSnapshot->latitude;
                event.longitude = gpsSnapshot->longitude;
            } else {
                // GPS 无效/未提供：按既定策略输出空值（序列化为 null）
                event.latitude = kNull;
                event.longitude = kNull;
            }

            if (std::isnan(event.latitude) || std::isnan(event.longitude)) {
                impl_->logger.info("📍 [直播GPS] event(lat/lon) -> null (NaN)");
            } else {
                impl_->logger.info("📍 [直播GPS] event(lat/lon)=" + std::to_string(event.latitude) +
                                   "," + std::to_string(event.longitude));
            }
        }
        
        // 5. 发布事件
        // ✅ 直播流任务也允许上报事件（受 shouldReportEvent 间隔限流保护），避免“视频流推事件被关掉”。
        // 媒体文件任务本来就需要每张图上报；直播流任务通常需要限流，避免刷屏。
        impl_->logger.info("📤 [检测任务] 正在发布事件到 MQTT... (taskType=" + std::to_string(static_cast<int>(config.type)) + ")");
        if (!publishEvent(event, config.deviceSn)) {
            impl_->logger.error("❌ [检测任务] 发布事件失败: taskId=" + config.taskId);
            return false;
        }
        impl_->logger.info("✅ [检测任务] 成功处理帧: taskId=" + config.taskId +
                          ", 检测到 " + std::to_string(boxes.size()) + " 个目标, fileName=" + fileName);
        return true;
        
    } catch (const std::exception& e) {
        impl_->logger.error("❌ [检测任务] processDetection 异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 处理分割任务 (main_type=100001)
 * 
 * 分割流程:
 * 1. 检测获取候选框
 * 2. SAM2 分割获取精细轮廓
 * 3. 构建事件 (polygons 字段)
 * 4. 上报 MQTT
 */
bool TaskService::processSegmentation(const cv::Mat& frame, 
                                      const TaskConfig& config,
                                      std::vector<BoundingBox>& outBoxes,
                                      const std::string& fileName,
                                      const device::GpsPosition* gpsSnapshot) {
    static int callCount = 0;
    callCount++;
    if (callCount == 1 || callCount % 30 == 0) {
        impl_->logger.info("🎨 [分割任务] processSegmentation 被调用 (第 " + std::to_string(callCount) + " 次): " +
                          "taskId=" + config.taskId + ", 帧尺寸=" + 
                          std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
                          ", fileName=" + fileName);
    }
    
    try {
        // 0. 检查 SAM2 是否可用
        impl_->logger.info("🔍 [分割任务] 检查 SAM2 状态... (useSam2=" + 
                          std::string(impl_->useSam2 ? "true" : "false") + ")");
        if (!impl_->useSam2) {
            impl_->logger.warning("❌ [分割任务] SAM2 未启用,无法执行分割任务: taskId=" + 
                                 config.taskId + ", fileName=" + fileName);
            impl_->logger.warning("💡 请检查: 1) config.json中segmentation.sam2.enabled是否为true; " +
                                 std::string("2) SAM2模型文件是否存在; 3) 程序启动日志中SAM2初始化是否成功"));
            return false;
        }
        
        // 1. 目标检测 (获取候选框)
        impl_->logger.info("🔍 [分割任务] 正在调用检测器获取候选框...");
        std::vector<BoundingBox> boxes;
        if (!detectObjects(frame, config, boxes)) {
            impl_->logger.warning("❌ [分割任务] 检测器调用失败: taskId=" + config.taskId + ", fileName=" + fileName);
            return false;
        }
        
        impl_->logger.info("✅ [分割任务] 检测器返回 " + std::to_string(boxes.size()) + 
                          " 个候选框 (taskId=" + config.taskId + ", fileName=" + fileName + ")");
        
        // 2. 过滤目标 (只保留关心的类别)
        boxes = filterByEventTypes(boxes, config);
        
        impl_->logger.info("🔍 [分割任务] 过滤后剩余 " + std::to_string(boxes.size()) + 
                          " 个候选框 (taskId=" + config.taskId + ", fileName=" + fileName + ")");
        
        // 输出过滤后的结果供外部使用
        outBoxes = boxes;
        
        // 如果没有检测到目标,直接返回
        if (boxes.empty()) {
            impl_->logger.info("ℹ️ [分割任务] 未检测到关注的目标类别: taskId=" + 
                              config.taskId + ", fileName=" + fileName);
            return false;
        }
        
        // 3. SAM2 分割
        impl_->logger.info("🎨 [分割任务] 正在调用 SAM2 进行分割...");
        std::vector<vision::SegmentationResult> segResults;
        {
            std::lock_guard<std::mutex> lock(impl_->segmentorMutex);
            
            // 转换 BoundingBox → DetectionBox
            std::vector<vision::DetectionBox> detBoxes;
            for (const auto& box : boxes) {
                vision::DetectionBox det;
                det.x = box.x;
                det.y = box.y;
                det.width = box.w;   // BoundingBox 使用 w/h
                det.height = box.h;  // DetectionBox 使用 width/height
                det.classId = box.classId;
                det.className = box.className;
                det.confidence = box.confidence;
                detBoxes.push_back(det);
            }
            
            segResults = impl_->sam2Segmentor->segmentWithPrompts(frame, detBoxes);
        }
        
        impl_->logger.info("✅ [分割任务] SAM2 分割返回 " + std::to_string(segResults.size()) + 
                          " 个结果 (taskId=" + config.taskId + ", fileName=" + fileName + ")");
        
        // 如果分割失败,返回
        if (segResults.empty()) {
            impl_->logger.warning("❌ [分割任务] SAM2 分割未返回有效结果: taskId=" + 
                                 config.taskId + ", fileName=" + fileName);
            return false;
        }
        
        // 4. 检查上报间隔
        if (config.type == TaskType::DETECTION_LIVESTREAM) {
            if (!shouldReportEvent(config.taskId, config.reportIntervalSec)) {
                impl_->logger.debug("上报间隔未到: taskId=" + config.taskId);
                return false;
            }
        }
        
        // 5. 构建分割事件
        impl_->logger.info("📦 [分割任务] 正在构建分割事件...");
        DetectionEvent event = buildSegmentationEvent(frame, segResults, config, config.eventTypes[0], fileName);

        // 5.1 直播流：用“取帧时刻”的 GPS 覆盖事件顶层经纬度
        // 说明：bboxes/polygons 里的 location 不要求填充。
        if (config.type == TaskType::DETECTION_LIVESTREAM) {
            const double kNull = std::numeric_limits<double>::quiet_NaN();
            if (gpsSnapshot != nullptr && gpsSnapshot->isValid()) {
                event.latitude = gpsSnapshot->latitude;
                event.longitude = gpsSnapshot->longitude;
            } else {
                event.latitude = kNull;
                event.longitude = kNull;
            }
        }
        
        // 6. 保存分割可视化图片到本地 (用于调试和验证)
        try {
            // 生成可视化图片 (在原图上绘制轮廓和bbox)
            cv::Mat visFrame = frame.clone();
            
            // 绘制每个分割对象
            for (size_t i = 0; i < segResults.size(); ++i) {
                const auto& result = segResults[i];
                
                // 🎨 绘制半透明彩色 mask
                if (!result.mask.empty() && result.mask.rows == frame.rows && result.mask.cols == frame.cols) {
                    // 生成随机颜色
                    cv::Scalar color(rand() % 200 + 55, rand() % 200 + 55, rand() % 200 + 55);
                    
                    // ⚠️ 重要: result.mask 可能是 CV_32F,需要转换为 CV_8U
                    cv::Mat mask8u;
                    if (result.mask.type() != CV_8U) {
                        result.mask.convertTo(mask8u, CV_8U, 255.0);  // [0,1] → [0,255]
                    } else {
                        mask8u = result.mask;
                    }
                    
                    // 创建彩色遮罩
                    cv::Mat coloredMask = cv::Mat::zeros(visFrame.size(), visFrame.type());
                    coloredMask.setTo(color, mask8u);
                    
                    // 半透明叠加 (alpha = 0.6)
                    cv::addWeighted(visFrame, 1.0, coloredMask, 0.6, 0.0, visFrame);
                    
                } else if (!result.contours.empty()) {
                    // 如果没有原始 mask,使用轮廓填充
                    cv::Scalar color(rand() % 200 + 55, rand() % 200 + 55, rand() % 200 + 55);
                    
                    cv::Mat tempMask = cv::Mat::zeros(visFrame.size(), CV_8UC1);
                    std::vector<std::vector<cv::Point>> contours = {result.contours};
                    cv::drawContours(tempMask, contours, 0, cv::Scalar(255), cv::FILLED);
                    
                    cv::Mat coloredMask = cv::Mat::zeros(visFrame.size(), visFrame.type());
                    coloredMask.setTo(color, tempMask);
                    
                    cv::addWeighted(visFrame, 1.0, coloredMask, 0.6, 0.0, visFrame);
                }
                
                // 绘制检测框 (绿色)
                cv::Rect rect(result.box.x, result.box.y, result.box.width, result.box.height);
                cv::rectangle(visFrame, rect, cv::Scalar(0, 255, 0), 2);
                
                // 绘制类别标签 (绿色文字)
                std::string label = result.className + " (" + std::to_string((int)(result.confidence * 100)) + "%)";
                cv::putText(visFrame, label, 
                           cv::Point(result.box.x, result.box.y - 10),
                           cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
            }
            
            // 保存到文件
            std::string outputDir = "/data/Edge-SDK/build/bin/";
            
            // 确保目录存在（跨平台创建目录）
            StatStruct info;
            if (statCompat(outputDir.c_str(), &info) != 0) {
                // 目录不存在,创建目录
                #ifdef _WIN32
                    ::_mkdir(outputDir.c_str());
                #else
                    mkdir(outputDir.c_str(), 0755);
                #endif
            }
            
            // 生成文件名: seg_<taskID>_<timestamp>.jpg
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            #ifdef _WIN32
                localtime_s(&tm, &now_c);
            #else
                localtime_r(&now_c, &tm);
            #endif
            
            std::ostringstream oss;
            oss << outputDir 
                << "seg_" << config.taskId  // 使用 taskId (string类型)
                << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S")
                << "_" << std::setfill('0') << std::setw(3) << ms.count()
                << ".jpg";
            std::string outputPath = oss.str();
            
            // 保存图片
            if (cv::imwrite(outputPath, visFrame)) {
                impl_->logger.info("🎨 [分割可视化] 已保存: " + outputPath + 
                                  " (共 " + std::to_string(segResults.size()) + " 个对象)");
            } else {
                impl_->logger.warning("⚠️ [分割可视化] 保存失败: " + outputPath);
            }
            
        } catch (const std::exception& e) {
            impl_->logger.error("❌ [分割可视化] 保存异常: " + std::string(e.what()));
            // 可视化保存失败不影响主流程,继续执行
        }
        
        // 7. 发布事件
        // ✅ 直播流分割任务也允许上报事件（仍受 shouldReportEvent 间隔限流保护）
        impl_->logger.info("📤 [分割任务] 正在发布事件到 MQTT... (taskType=" + std::to_string(static_cast<int>(config.type)) + ")");
        if (!publishEvent(event, config.deviceSn)) {
            impl_->logger.error("❌ [分割任务] 发布事件失败: taskId=" + config.taskId);
            return false;
        }
        impl_->logger.info("✅ [分割任务] 成功处理分割帧: taskId=" + config.taskId +
                          ", 分割 " + std::to_string(segResults.size()) + " 个目标, fileName=" + fileName);
        return true;
        
    } catch (const std::exception& e) {
        impl_->logger.error("❌ [分割任务] processSegmentation 异常: " + std::string(e.what()));
        return false;
        impl_->logger.error("processSegmentation 异常: " + std::string(e.what()));
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
        event.taskID = std::stoi(config.taskId);  // string → int 转换
        event.eventType = eventType.id;
        event.main_type = eventType.mainType;  // 字段名改为 main_type
        event.eventDescribe = eventType.eventDescribe;
        event.fileName = fileName; // 保存文件名

        // 2. 图像编码 - 已废弃 (接口更新：不再上报图片数据)
        // 为了节省性能，不再进行图片缩放、绘制和编码
        /*
        cv::Mat resizedFrame;
        const int MAX_WIDTH = 800;
        const int MAX_HEIGHT = 600;
        
        // ... (省略图片处理代码) ...
        
        std::vector<uint8_t> jpegBuffer;
        if (encodeImageToJPEG(resizedFrame, 85, jpegBuffer)) {
            event.pictureBase64 = encodeBase64(jpegBuffer);
            event.pictureCode = ".jpg";
            
            impl_->logger.debug("图片编码完成: JPEG=" + std::to_string(jpegBuffer.size()) + " bytes, " +
                              "Base64=" + std::to_string(event.pictureBase64.size()) + " chars");
        } else {
            impl_->logger.warning("图像编码失败,将不包含图片");
        }
        */
        impl_->logger.debug("接口更新: 跳过图片编码步骤");
        
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
            // ✅ 按需求：经纬度计算失败时直接返回“空值”，不要降级估算。
            // 原因：降级方案（pixelToGPS/假无人机坐标）会产生“看似合理但实际错误”的坐标，
            //      云端一旦用这些坐标做业务（告警/地图标注）风险更大。
            impl_->logger.warning("GPS 计算失败：将经纬度置为空值(不做降级估算)");

            // 约定：用 NaN 作为“空值哨兵”。
            // 后续在 eventToJson 序列化时，将 NaN 输出为 JSON null（更符合‘空值’含义）。
            const double kNull = std::numeric_limits<double>::quiet_NaN();

            event.latitude = kNull;
            event.longitude = kNull;
            event.points.clear();
            event.points.reserve(boxes.size());
            for (const auto& box : boxes) {
                BoundingBox eventBox = box;
                eventBox.lat = kNull;
                eventBox.lon = kNull;
                event.points.push_back(eventBox);
            }
        }
        // 注意：如果 calculateGPSCoordinates() 成功，
        // event.points 已经在函数内部填充了，这里不需要再处理
        
        // 5. 填充 objects 数组 (新接口要求)
        // 将 BoundingBox 转换为 DetectedObject
        event.objects.clear();
        event.objects.reserve(boxes.size());
        
        for (size_t i = 0; i < boxes.size(); ++i) {
            const auto& box = boxes[i];
            
            DetectionEvent::DetectedObject obj;
            obj.label = box.className;  // 类别名称
            
            // 填充 bbox
            obj.bbox.x = static_cast<int>(box.x);
            obj.bbox.y = static_cast<int>(box.y);
            obj.bbox.w = static_cast<int>(box.w);
            obj.bbox.h = static_cast<int>(box.h);
            
            // 填充 bbox 的 GPS location (如果已计算)
            if (i < event.points.size()) {
                obj.bbox.location.lon = event.points[i].lon;
                obj.bbox.location.lat = event.points[i].lat;
            } else {
                // 降级方案：使用事件的平均 GPS
                obj.bbox.location.lon = event.longitude;
                obj.bbox.location.lat = event.latitude;
            }
            
            // 目标检测任务没有 mask,留空
            obj.mask.clear();
            
            event.objects.push_back(obj);
        }
        
        impl_->logger.debug("已填充 objects 数组: " + std::to_string(event.objects.size()) + " 个对象");
        
        // 6. 生成 resultImage (Base64 编码)
        // ✅ 直播流推送事件必须包含“推理后可视化图片”，与媒体文件分析对齐：在图上画框再编码。
        // 这里统一使用 VisionUtils::drawDetections，避免两套绘制逻辑不一致。
        cv::Mat visFrame = frame.clone();

        // 6.1 先绘制（基于原图尺寸的 boxes 坐标）
        // 注意：vision::BBox 现在是 task::BoundingBox 的别名，可直接传 boxes
        vision::VisionUtils::drawDetections(visFrame, boxes);

        // 6.2 如果图片太大,再缩放（避免 MQTT 消息过大）
        // ⚠️ 重要：缩放之后，event.objects 里的 bbox 也要按比例缩放，否则云端看到的 bbox 与图片不一致。
        const int MAX_WIDTH = 1280;
        const int MAX_HEIGHT = 960;
        double scale = 1.0;
        if (visFrame.cols > MAX_WIDTH || visFrame.rows > MAX_HEIGHT) {
            scale = std::min(
                static_cast<double>(MAX_WIDTH) / visFrame.cols,
                static_cast<double>(MAX_HEIGHT) / visFrame.rows
            );

            int newWidth = static_cast<int>(visFrame.cols * scale);
            int newHeight = static_cast<int>(visFrame.rows * scale);

            cv::Mat resized;
            cv::resize(visFrame, resized, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);
            visFrame = resized;

            // 同步缩放 bbox（保证你说的“可视化图片”和检测结果完全一致）
            for (auto& obj : event.objects) {
                obj.bbox.x = static_cast<int>(std::lround(obj.bbox.x * scale));
                obj.bbox.y = static_cast<int>(std::lround(obj.bbox.y * scale));
                obj.bbox.w = static_cast<int>(std::lround(obj.bbox.w * scale));
                obj.bbox.h = static_cast<int>(std::lround(obj.bbox.h * scale));
            }

            impl_->logger.debug("图片已缩放: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
                               " → " + std::to_string(newWidth) + "x" + std::to_string(newHeight) +
                               ", scale=" + std::to_string(scale));
        }
        
        // JPEG 编码 (使用较低的质量)
        std::vector<uint8_t> jpegBuffer;
        int quality = 60;  // 🔧 降低质量: 85 → 60
        if (encodeImageToJPEG(visFrame, quality, jpegBuffer)) {
            event.resultImage = encodeBase64(jpegBuffer);
            impl_->logger.info("📦 resultImage 编码完成: JPEG=" + std::to_string(jpegBuffer.size()) + " bytes, " +
                               "Base64=" + std::to_string(event.resultImage.size()) + " chars, quality=" + std::to_string(quality));
        } else {
            impl_->logger.warning("resultImage 编码失败");
        }
        
        impl_->logger.debug("事件构建完成: UUID=" + event.uuid);
        
    } catch (const std::exception& e) {
        impl_->logger.error("buildEvent 异常: " + std::string(e.what()));
    }
    
    return event;
}

/**
 * @brief 构建分割事件 (main_type=100001)
 * 
 * 与 buildEvent 类似,但使用 polygons 字段而非 points 字段。
 * 
 * 流程:
 * 1. 生成 UUID
 * 2. 填充基本信息
 * 3. 转换 SAM2 mask → polygons
 * 4. 计算 GPS 坐标 (使用轮廓中心点)
 * 5. 时间戳格式化
 * 
 * @param frame 原始图像
 * @param segResults SAM2 分割结果列表
 * @param config 任务配置
 * @param eventType 事件类型
 * @param fileName 文件名 (可选)
 * @return DetectionEvent 完整事件 (填充 polygons 字段)
 */
DetectionEvent TaskService::buildSegmentationEvent(const cv::Mat& frame,
                                                   const std::vector<vision::SegmentationResult>& segResults,
                                                   const TaskConfig& config,
                                                   const EventType& eventType,
                                                   const std::string& fileName) {
    DetectionEvent event;
    
    try {
        // 1. 基本信息
        event.uuid = generateUUID();
        event.taskID = std::stoi(config.taskId);  // string → int 转换
        event.eventType = eventType.id;
        event.main_type = eventType.mainType;  // 字段名改为 main_type (应该是 100001)
        event.eventDescribe = eventType.eventDescribe;
        event.fileName = fileName;
        
        // 2. 图像编码 (已废弃,跳过)
        impl_->logger.debug("接口更新: 跳过图片编码步骤");
        
        // 3. 时间戳
        event.createTime = getCurrentTimeString();
        
        // 4. 转换 SAM2结果 → polygons
        std::vector<BoundingBox> tempBoxes;  // 用于 GPS 计算
        
        for (size_t i = 0; i < segResults.size(); ++i) {
            const auto& result = segResults[i];
            
            Polygon polygon;
            polygon.label = result.className;  // 类别名称
            
            // 如果有轮廓点,使用轮廓
            if (!result.contours.empty()) {
                // 简化轮廓 (减少顶点数)
                std::vector<cv::Point> approx;
                cv::approxPolyDP(result.contours, approx, 2.0, true);
                
                // 转换为 vertices
                for (const auto& pt : approx) {
                    Point2f vertex;
                    vertex.x = static_cast<float>(pt.x);
                    vertex.y = static_cast<float>(pt.y);
                    polygon.vertices.push_back(vertex);
                }
                
                impl_->logger.debug("分割对象 " + std::to_string(i) + " (" + result.className + "): " + 
                                   std::to_string(polygon.vertices.size()) + " 个顶点");
            } else if (!result.mask.empty()) {
                // 从 mask 提取轮廓
                std::vector<std::vector<cv::Point>> contours;
                cv::findContours(result.mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                
                if (!contours.empty()) {
                    // 取最大轮廓
                    auto maxContour = std::max_element(contours.begin(), contours.end(),
                        [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                            return cv::contourArea(a) < cv::contourArea(b);
                        });
                    
                    // 简化轮廓
                    std::vector<cv::Point> approx;
                    cv::approxPolyDP(*maxContour, approx, 2.0, true);
                    
                    // 转换为 vertices
                    for (const auto& pt : approx) {
                        Point2f vertex;
                        vertex.x = static_cast<float>(pt.x);
                        vertex.y = static_cast<float>(pt.y);
                        polygon.vertices.push_back(vertex);
                    }
                    
                    impl_->logger.debug("分割对象 " + std::to_string(i) + " (" + result.className + "): " + 
                                       std::to_string(polygon.vertices.size()) + " 个顶点 (从mask提取)");
                }
            }
            
            event.polygons.push_back(polygon);
            
            // 同时保存边界框用于 GPS 计算
            BoundingBox box;
            box.x = result.box.x;
            box.y = result.box.y;
            box.w = result.box.width;
            box.h = result.box.height;
            box.classId = result.classId;
            box.className = result.className;
            box.confidence = result.confidence;
            tempBoxes.push_back(box);
        }
        
        // 5. GPS 坐标计算
        // 使用边界框中心点计算 GPS
        std::string imagePath = fileName.empty() ? "" : fileName;
        
        if (!calculateGPSCoordinates(frame, tempBoxes, event, imagePath)) {
            // ✅ 按需求：经纬度计算失败时直接返回“空值”，不要降级估算。
            impl_->logger.warning("GPS 计算失败：将经纬度置为空值(不做降级估算)");

            const double kNull = std::numeric_limits<double>::quiet_NaN();
            event.latitude = kNull;
            event.longitude = kNull;
            event.points.clear();
            event.points.reserve(tempBoxes.size());
            for (const auto& box : tempBoxes) {
                BoundingBox eventBox = box;
                eventBox.lat = kNull;
                eventBox.lon = kNull;
                event.points.push_back(eventBox);
            }
        }
        
        // 6. 填充 objects 数组 (新接口要求)
        // 将 SAM2 分割结果转换为 DetectedObject (包含 mask 数据)
        event.objects.clear();
        event.objects.reserve(segResults.size());
        
        for (size_t i = 0; i < segResults.size(); ++i) {
            const auto& result = segResults[i];
            
            DetectionEvent::DetectedObject obj;
            obj.label = result.className;  // 类别名称
            
            // 填充 bbox
            obj.bbox.x = static_cast<int>(result.box.x);
            obj.bbox.y = static_cast<int>(result.box.y);
            obj.bbox.w = static_cast<int>(result.box.width);
            obj.bbox.h = static_cast<int>(result.box.height);
            
            // 填充 bbox 的 GPS location (如果已计算)
            if (i < event.points.size()) {
                obj.bbox.location.lon = event.points[i].lon;
                obj.bbox.location.lat = event.points[i].lat;
            } else {
                // 降级方案：使用事件的平均 GPS
                obj.bbox.location.lon = event.longitude;
                obj.bbox.location.lat = event.latitude;
            }
            
            // 填充 mask (从轮廓或 mask 提取)
            obj.mask.clear();
            
            if (!result.contours.empty()) {
                // 使用轮廓点 (激进简化以减小 JSON 大小)
                std::vector<cv::Point> approx;
                double epsilon = 5.0;  // 🔧 增大简化系数: 2.0 → 5.0 (减少点数)
                cv::approxPolyDP(result.contours, approx, epsilon, true);
                
                // 🔧 进一步降采样: 如果点数仍然太多,按固定间隔采样
                const int MAX_MASK_POINTS = 50;  // 最多保留 50 个点
                int step = (approx.size() > MAX_MASK_POINTS) ? 
                          (approx.size() / MAX_MASK_POINTS + 1) : 1;
                
                for (size_t j = 0; j < approx.size(); j += step) {
                    const auto& pt = approx[j];
                    DetectionEvent::MaskPoint maskPt;
                    maskPt.x = pt.x;
                    maskPt.y = pt.y;
                    
                    // TODO: 计算每个 mask 点的 GPS 坐标
                    // 当前简化处理：使用 bbox 中心的 GPS
                    maskPt.location.lon = obj.bbox.location.lon;
                    maskPt.location.lat = obj.bbox.location.lat;
                    
                    obj.mask.push_back(maskPt);
                }
            } else if (!result.mask.empty()) {
                // 从 mask 提取轮廓
                std::vector<std::vector<cv::Point>> contours;
                cv::findContours(result.mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                
                if (!contours.empty()) {
                    // 取最大轮廓
                    auto maxContour = std::max_element(contours.begin(), contours.end(),
                        [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                            return cv::contourArea(a) < cv::contourArea(b);
                        });
                    
                    // 激进简化轮廓
                    std::vector<cv::Point> approx;
                    double epsilon = 5.0;  // 🔧 增大简化系数
                    cv::approxPolyDP(*maxContour, approx, epsilon, true);
                    
                    // 🔧 降采样
                    const int MAX_MASK_POINTS = 50;
                    int step = (approx.size() > MAX_MASK_POINTS) ? 
                              (approx.size() / MAX_MASK_POINTS + 1) : 1;
                    
                    for (size_t j = 0; j < approx.size(); j += step) {
                        const auto& pt = approx[j];
                        DetectionEvent::MaskPoint maskPt;
                        maskPt.x = pt.x;
                        maskPt.y = pt.y;
                        
                        // TODO: 计算每个 mask 点的 GPS 坐标
                        maskPt.location.lon = obj.bbox.location.lon;
                        maskPt.location.lat = obj.bbox.location.lat;
                        
                        obj.mask.push_back(maskPt);
                    }
                }
            }
            
            event.objects.push_back(obj);
            
            impl_->logger.info("📌 分割对象 " + std::to_string(i) + " (" + result.className + "): " +
                               "mask=" + std::to_string(obj.mask.size()) + " 个点 (已优化)");
        }
        
        impl_->logger.info("✅ 已填充 objects 数组: " + std::to_string(event.objects.size()) + " 个对象");
        
        // 7. 生成 resultImage (Base64 编码)
        // 在原图上绘制分割轮廓,然后编码
        cv::Mat visFrame = frame.clone();
        
        // 说明：不在这里提前缩放。
        // 原因：分割可视化涉及 mask/contours 绘制，如果先缩放而不缩放 mask，会导致尺寸不匹配。
        // 统一在“编码前”进行缩放，并同步缩放 event.objects 的 bbox。
        
        // 绘制每个分割对象
        for (size_t i = 0; i < segResults.size(); ++i) {
            const auto& result = segResults[i];
            const auto& obj = event.objects[i];
            
            // 🎨 方法 1: 使用原始 mask 绘制半透明彩色遮罩
            if (!result.mask.empty() && result.mask.rows == visFrame.rows && result.mask.cols == visFrame.cols) {
                // 生成随机颜色 (每个对象不同颜色)
                cv::Scalar color(rand() % 200 + 55, rand() % 200 + 55, rand() % 200 + 55);
                
                // ⚠️ 重要: result.mask 可能是 CV_32F,需要转换为 CV_8U
                cv::Mat mask8u;
                if (result.mask.type() != CV_8U) {
                    result.mask.convertTo(mask8u, CV_8U, 255.0);  // [0,1] → [0,255]
                } else {
                    mask8u = result.mask;
                }
                
                // 创建彩色遮罩
                cv::Mat coloredMask = cv::Mat::zeros(visFrame.size(), visFrame.type());
                coloredMask.setTo(color, mask8u);
                
                // 半透明叠加 (alpha = 0.5)
                cv::addWeighted(visFrame, 1.0, coloredMask, 0.5, 0.0, visFrame);
                
                impl_->logger.debug("✅ 使用原始 mask 绘制半透明遮罩: " + result.className);
                
            } 
            // 🎨 方法 2: 如果没有原始 mask,使用轮廓填充
            else if (!result.contours.empty()) {
                // 生成随机颜色
                cv::Scalar color(rand() % 200 + 55, rand() % 200 + 55, rand() % 200 + 55);
                
                // 创建临时 mask
                cv::Mat tempMask = cv::Mat::zeros(visFrame.size(), CV_8UC1);
                std::vector<std::vector<cv::Point>> contours = {result.contours};
                cv::drawContours(tempMask, contours, 0, cv::Scalar(255), cv::FILLED);
                
                // 创建彩色遮罩
                cv::Mat coloredMask = cv::Mat::zeros(visFrame.size(), visFrame.type());
                coloredMask.setTo(color, tempMask);
                
                // 半透明叠加
                cv::addWeighted(visFrame, 1.0, coloredMask, 0.5, 0.0, visFrame);
                
                impl_->logger.debug("✅ 使用轮廓填充绘制半透明遮罩: " + result.className);
            }
            
            // 绘制 bbox (绿色框)
            cv::Rect rect(obj.bbox.x, obj.bbox.y, obj.bbox.w, obj.bbox.h);
            cv::rectangle(visFrame, rect, cv::Scalar(0, 255, 0), 2);
            
            // 绘制标签
            std::string label = obj.label;
            cv::putText(visFrame, label, 
                       cv::Point(obj.bbox.x, obj.bbox.y - 5),
                       cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
        }
        
        // 🔧 如果图片太大,先缩放（避免 MQTT 消息过大）
        // ⚠️ 同 buildEvent：缩放后同步缩放 objects 的 bbox，保证可视化图与 bbox 一致。
        const int MAX_WIDTH = 1280;
        const int MAX_HEIGHT = 960;
        double scale = 1.0;
        if (visFrame.cols > MAX_WIDTH || visFrame.rows > MAX_HEIGHT) {
            scale = std::min(
                static_cast<double>(MAX_WIDTH) / visFrame.cols,
                static_cast<double>(MAX_HEIGHT) / visFrame.rows
            );

            int newWidth = static_cast<int>(visFrame.cols * scale);
            int newHeight = static_cast<int>(visFrame.rows * scale);

            cv::Mat resized;
            cv::resize(visFrame, resized, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);
            visFrame = resized;

            for (auto& obj : event.objects) {
                obj.bbox.x = static_cast<int>(std::lround(obj.bbox.x * scale));
                obj.bbox.y = static_cast<int>(std::lround(obj.bbox.y * scale));
                obj.bbox.w = static_cast<int>(std::lround(obj.bbox.w * scale));
                obj.bbox.h = static_cast<int>(std::lround(obj.bbox.h * scale));
            }

            impl_->logger.debug("分割可视化图片已缩放: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
                               " → " + std::to_string(newWidth) + "x" + std::to_string(newHeight) +
                               ", scale=" + std::to_string(scale));
        }

        // JPEG 编码 (使用较低的质量以减小文件大小)
        std::vector<uint8_t> jpegBuffer;
        int quality = 60;  // 🔧 降低质量: 85 → 60 (减小文件大小)
        if (encodeImageToJPEG(visFrame, quality, jpegBuffer)) {
            event.resultImage = encodeBase64(jpegBuffer);
            impl_->logger.info("📦 resultImage 编码完成: JPEG=" + std::to_string(jpegBuffer.size()) + " bytes, " +
                               "Base64=" + std::to_string(event.resultImage.size()) + " chars, quality=" + std::to_string(quality));
        } else {
            impl_->logger.warning("resultImage 编码失败");
        }
        
        impl_->logger.debug("分割事件构建完成: UUID=" + event.uuid + 
                           ", polygons=" + std::to_string(event.polygons.size()));
        
    } catch (const std::exception& e) {
        impl_->logger.error("buildSegmentationEvent 异常: " + std::string(e.what()));
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

    if (targetClassIds.empty()) {
        // 典型原因：云端下发的是类别“名称”，但 labels_path 中找不到该名称，导致 TaskConfig::fromJson 映射为空。
        // 这时继续过滤会把所有目标都过滤掉（因为关注集合为空），所以直接返回空并给出指引更清晰。
        impl_->logger.warning("⚠️ [过滤] 关注类别集合为空：可能是 labels_path/coco.names 与云端下发的 class 名称不一致，"
                             "或 labels_path 配置缺失/读取失败。将返回空集合(不保留任何目标)。");
        return {};
    }

    // 读取labels(例如 coco.names)，用于日志展示名称，避免只看到数字ID难以排查
    std::vector<std::string> classes;
    try {
        vision::VisionConfigLoader loader;
        vision::DetectorConfig detCfg = loader.loadDetectorConfig("ppyoloe");
        classes = detCfg.classes;
    } catch (...) {
        // ignore
    }
    
    impl_->logger.debug("🔍 [过滤] 关注类别: [" +
                      [&targetClassIds, &classes]() {
                          std::string text;
                          for (int id : targetClassIds) {
                              if (!text.empty()) text += ", ";
                              if (!classes.empty() && id >= 0 && id < static_cast<int>(classes.size())) {
                                  text += classes[static_cast<size_t>(id)] + "(ID=" + std::to_string(id) + ")";
                              } else {
                                  text += "ID=" + std::to_string(id);
                              }
                          }
                          return text;
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
 * @brief DetectionEvent 转 JSON (新接口格式 v2.0)
 * 
 * 生成嵌套的 JSON 结构:
 * {
 *   "UUID": "...",
 *   "taskID": 123,
 *   "main_type": 100000,
 *   "result": {
 *     "image": "Base64图片数据",
 *     "objects": [
 *       {
 *         "label": "car",
 *         "bbox": {"x": 100, "y": 200, "w": 50, "h": 60, "location": {"lon": 121.5, "lat": 31.2}},
 *         "mask": [{"x": 105, "y": 205, "location": {"lon": 121.5, "lat": 31.2}}, ...]
 *       }
 *     ]
 *   }
 * }
 * 
 * 面试要点:
 * - JSON 嵌套结构设计: result 作为顶层容器,包含 image 和 objects
 * - 数组序列化: objects 和 mask 都是数组,需要正确处理
 * - GPS 坐标精度: 经度 longitude 在前,纬度 latitude 在后 (GeoJSON 标准)
 */
std::string TaskService::eventToJson(const DetectionEvent& event) {
    nlohmann::json j;
    
    // 1. 顶层基本信息
    j["UUID"] = event.uuid;
    j["taskID"] = event.taskID;  // int 类型
    j["eventType"] = event.eventType;
    j["main_type"] = event.main_type;  // int 类型
    j["eventDescribe"] = event.eventDescribe;
    j["fileName"] = event.fileName;
    j["createTime"] = event.createTime;
    
    // 保留旧字段 latitude/longitude 用于兼容 (事件平均 GPS)
    // ✅ 约定：若 GPS 计算失败，event.latitude/event.longitude 会是 NaN，这里序列化为 JSON null。
    auto toJsonNumberOrNull = [](double v) -> nlohmann::json {
        if (std::isnan(v)) return nullptr;
        return v;
    };
    j["latitude"] = toJsonNumberOrNull(event.latitude);
    j["longitude"] = toJsonNumberOrNull(event.longitude);
    
    // 2. 嵌套的 result 对象
    nlohmann::json result;
    
    // 2.1 result.image (Base64 编码的可视化图片)
    result["image"] = event.resultImage;
    
    // 2.2 result.objects (检测/分割对象数组)
    nlohmann::json objects = nlohmann::json::array();
    
    for (const auto& obj : event.objects) {
        nlohmann::json objJson;
        
        // 对象标签
        objJson["label"] = obj.label;
        
        // bbox 对象 (包含 location)
        nlohmann::json bbox;
        bbox["x"] = obj.bbox.x;
        bbox["y"] = obj.bbox.y;
        bbox["w"] = obj.bbox.w;
        bbox["h"] = obj.bbox.h;
        
    // bbox.location
    nlohmann::json bboxLocation;
    bboxLocation["lon"] = toJsonNumberOrNull(obj.bbox.location.lon);  // 经度在前
    bboxLocation["lat"] = toJsonNumberOrNull(obj.bbox.location.lat);  // 纬度在后
        bbox["location"] = bboxLocation;
        
        objJson["bbox"] = bbox;
        
        // mask 数组 (分割轮廓点)
        nlohmann::json mask = nlohmann::json::array();
        
        for (const auto& pt : obj.mask) {
            nlohmann::json maskPt;
            maskPt["x"] = pt.x;
            maskPt["y"] = pt.y;
            
            // maskPt.location
            nlohmann::json ptLocation;
            ptLocation["lon"] = toJsonNumberOrNull(pt.location.lon);
            ptLocation["lat"] = toJsonNumberOrNull(pt.location.lat);
            maskPt["location"] = ptLocation;
            
            mask.push_back(maskPt);
        }
        
        objJson["mask"] = mask;
        
        objects.push_back(objJson);
    }
    
    result["objects"] = objects;
    
    // 3. 将 result 添加到顶层 JSON
    j["result"] = result;
    
    // 4. 保留旧字段用于调试和兼容 (可选,后续可删除)
    // points 和 polygons 已被 result.objects 替代
    
    return j.dump();  // 转为字符串
}

}  // namespace task
}  // namespace esdk_sophon
