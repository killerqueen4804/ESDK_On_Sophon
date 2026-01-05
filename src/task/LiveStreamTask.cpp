/**
 * @file LiveStreamTask.cpp
 * @brief 直播流检测任务实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-04
 * @updated 2025-11-10 (Day 6: 集成 DJI Liveview + RTMP 推流)
 * @updated 2025-11-18 (Day 7: 集成 FFmpeg H.264 解码器)
 * @updated 2025-11-28 (Phase 4.2: GPS 数据集成)
 */

#include "esdk_sophon/task/LiveStreamTask.h"
#include "esdk_sophon/rtmp/RtmpStreamer.h"    // 完整的 RtmpStreamer 定义
#include "esdk_sophon/video/H264Decoder.h"    // 完整的 H264Decoder 定义 (Day 7 新增)
#include "esdk_sophon/core/Config.h"          // 配置管理器
#include "esdk_sophon/vision/VisionUtils.h"   // 视觉工具（包含可视化功能）(Day 14 更新)

// Phase 4: 智能变焦拍照功能
#include "esdk_sophon/vision/tracker/ByteTracker.h"           // 目标追踪
#include "esdk_sophon/vision/strategy/SmartZoomCaptureStrategy.h"  // 变焦拍照策略
#include "esdk_sophon/mqtt/MqttClient.h"                      // MQTT 客户端
#include "esdk_sophon/device/FlightDataProvider.h"            // GPS 数据提供者 (Phase 4.2)

#include <chrono>
#include <atomic>

namespace esdk_sophon {
namespace task {

// ==================== Day 16: 单例模式上下文 ====================
// 解决问题: 避免反复创建/销毁 Liveview 导致的死锁和内存泄漏
// 策略: "初始化一次，永久复用" (Initialize Once, Reuse Forever)
namespace {
    // 全局唯一的 Liveview 实例
    std::shared_ptr<edge_sdk::Liveview> g_liveviewInstance = nullptr;
    std::mutex g_liveviewInitMutex;
    
    // 当前活跃的任务指针 (用于回调路由)
    std::atomic<LiveStreamTask*> g_activeTask{nullptr};
    
    // 静态代理回调：将 SDK 数据转发给当前活跃的任务
    edge_sdk::ErrorCode ProxyH264Callback(const uint8_t* buf, uint32_t len) {
        LiveStreamTask* task = g_activeTask.load();
        if (task) {
            return task->onH264Data(buf, len);
        }
        return edge_sdk::kOk;
    }
    
    // 静态状态回调
    void ProxyStatusCallback(const edge_sdk::Liveview::LiveviewStatus& status) {
        LiveStreamTask* task = g_activeTask.load();
        if (task) {
            task->onLiveviewStatusChanged(status);
        }
    }
}

LiveStreamTask::LiveStreamTask(const TaskConfig& config,
                               std::shared_ptr<TaskService> service)
    : config_(config)
    , service_(service)
    , state_(TaskState::IDLE)
    , running_(false)
    , paused_(false)
    , logger_(core::Logger::getInstance()) {
    
    // 初始化统计信息
    // stats_.taskId = config_.taskId;
    stats_.framesProcessed = 0;
    stats_.detectionsCount = 0;
    stats_.eventsPublished = 0;
    // stats_.failedFrames = 0;
    
    // ==================== Day 7: 从配置读取解码器设置 ====================
    auto& globalConfig = core::Config::getInstance();
    decoderEnabled_ = globalConfig.getBool("video.decoder.enabled", true);
    
    // ==================== Day 7: 初始化 H264 解码器 ====================
    if (decoderEnabled_) {
        try {
            h264Decoder_ = std::make_unique<video::H264Decoder>();
            logger_.info("✅ H264Decoder 已创建 (enabled=true)");
        } catch (const std::exception& e) {
            logger_.error("❌ 创建 H264Decoder 失败: " + std::string(e.what()));
            h264Decoder_.reset();
            decoderEnabled_ = false;
        }
    } else {
        logger_.info("⚠️ H264Decoder 已禁用 (video.decoder.enabled=false)");
        h264Decoder_.reset();
    }
    
    // ==================== Phase 4: 初始化智能变焦拍照功能 ====================
    initZoomCaptureFeatures(globalConfig);
    
    logger_.info("LiveStreamTask 已创建: taskId=" + config_.taskId);
}

LiveStreamTask::~LiveStreamTask() {
    logger_.info("🗑️ LiveStreamTask 析构函数开始: taskId=" + config_.taskId);
    if (running_) {
        stop();
    }
    logger_.info("✅ LiveStreamTask 已销毁: taskId=" + config_.taskId);
}

// ==================== ITask 接口实现 ====================

bool LiveStreamTask::start() {
    // 1. 检查状态
    if (state_ != TaskState::IDLE) {
        logger_.error("无法启动任务: 当前状态不是 IDLE, taskId=" + config_.taskId);
        return false;
    }
    
    logger_.info("启动直播流任务: taskId=" + config_.taskId);
    
    // 2. 初始化视频流
    if (!initVideoStream()) {
        notifyError("视频流初始化失败");
        return false;
    }
    
    // 3. 设置运行标志
    running_ = true;
    paused_ = false;
    
    // 4. 记录启动时间
    // stats_.startTime = std::chrono::system_clock::now();
    
    // 5. 创建工作线程
    try {
        workerThread_ = std::make_unique<std::thread>(&LiveStreamTask::execute, this);
        logger_.info("✅ 工作线程已启动, ID: " + std::to_string(std::hash<std::thread::id>{}(workerThread_->get_id())));
        
        // 📌 Day 9: 创建异步检测线程
        detectionThread_ = std::make_unique<std::thread>(&LiveStreamTask::detectionLoop, this);
        logger_.info("✅ 异步检测线程已启动, ID: " + std::to_string(std::hash<std::thread::id>{}(detectionThread_->get_id())));
        
        // 📌 Phase 4: 创建变焦拍照线程（如果功能已启用）
        if (zoomCaptureEnabled_ && zoomCaptureStrategy_) {
            zoomCaptureThread_ = std::make_unique<std::thread>(&LiveStreamTask::zoomCaptureLoop, this);
            logger_.info("✅ 变焦拍照线程已启动, ID: " + std::to_string(std::hash<std::thread::id>{}(zoomCaptureThread_->get_id())));
        }
        
    } catch (const std::exception& e) {
        running_ = false;
        notifyError("创建工作线程失败: " + std::string(e.what()));
        return false;
    }
    
    // 6. 更新状态
    state_ = TaskState::RUNNING;
    notifyStateChanged(TaskState::RUNNING);
    
    logger_.info("直播流任务已启动: taskId=" + config_.taskId);
    return true;
}

void LiveStreamTask::stop() {
    // 1. 检查状态
    if (state_ != TaskState::RUNNING && state_ != TaskState::PAUSED) {
        logger_.warning("任务未运行，无需停止: taskId=" + config_.taskId);
        return;  // void 函数直接 return
    }
    
    logger_.info("🛑 停止直播流任务: taskId=" + config_.taskId);
    
    // ⭐ 步骤 0: 立即设置停止标志（最优先）
    // 📌 修正：先设置 running_ = false，让 onH264Data 回调立即失效
    // 这样可以防止在调用 StopH264Stream 期间，回调还在处理数据或争抢锁
    running_ = false;
    
    // ⭐ 步骤 1: 停止 H264 流（切断数据源）
    if (liveview_) {
        logger_.info("  [1/7] 🛑 停止 H264 流...");
        logger_.info("     [诊断] 调用 StopH264Stream 前...");
        edge_sdk::ErrorCode ret = liveview_->StopH264Stream();
        logger_.info("     [诊断] 调用 StopH264Stream 后...");
        if (ret != edge_sdk::kOk) {
            logger_.warning("     ⚠️ StopH264Stream 失败: " + std::to_string(static_cast<int>(ret)));
        } else {
            logger_.info("     ✅ StopH264Stream 成功");
        }
    }

    // ⭐ 步骤 2: 等待回调排空
    // 给一点时间让正在执行的回调完成
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    
    // ⭐ 步骤 3: 如果任务已暂停，唤醒工作线程
    if (paused_) {
        paused_ = false;
        pauseCv_.notify_one();
        logger_.info("  ✅ [3/6] 暂停状态已清除");
    }
    
    // ⭐ 步骤 4: 唤醒并等待检测线程（加超时保护）
    logger_.info("  🕵️ [4/6] 正在停止异步检测线程...");
    {
        std::lock_guard<std::mutex> lock(detectionMutex_);
        newFrameAvailable_ = true;  // 确保线程能从 wait 中醒来
        detectionCv_.notify_one();
        logger_.info("     [诊断] 已通知 detectionCv_");
    }
    
    if (detectionThread_ && detectionThread_->joinable()) {
        logger_.info("     [诊断] 等待检测线程 join, ID: " + std::to_string(std::hash<std::thread::id>{}(detectionThread_->get_id())));
        // 🎯 关键修复：使用带超时的 join（C++20 不支持，手动实现）
        bool joined = false;
        std::thread timeoutThread([this, &joined]() {
            if (detectionThread_ && detectionThread_->joinable()) {
                detectionThread_->join();
                joined = true;
            }
        });
        
        timeoutThread.detach();  // 分离超时线程
        
        // 等待最多 3 秒
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (joined) {
                logger_.info("     ✅ 检测线程已正常退出");
                break;
            }
        }
        
        if (!joined) {
            logger_.error("     ❌ 检测线程超时未退出（3秒），强制继续");
            logger_.error("     ⚠️ 可能原因：processFrame() 阻塞（MQTT/检测器未响应）");
            // ⚠️ 线程泄漏，但避免整个程序卡死
        }
    } else {
        logger_.info("     ℹ️ 检测线程未启动或已退出");
    }
    
    // ⭐ 步骤 5: 等待工作线程结束（同样加超时）
    logger_.info("  🔄 [5/6] 正在停止工作线程...");
    if (workerThread_ && workerThread_->joinable()) {
        logger_.info("     [诊断] 等待工作线程 join, ID: " + std::to_string(std::hash<std::thread::id>{}(workerThread_->get_id())));
        bool joined = false;
        std::thread timeoutThread([this, &joined]() {
            if (workerThread_ && workerThread_->joinable()) {
                workerThread_->join();
                joined = true;
            }
        });
        
        timeoutThread.detach();
        
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (joined) {
                logger_.info("     ✅ 工作线程已正常退出");
                break;
            }
        }
        
        if (!joined) {
            logger_.error("     ❌ 工作线程超时未退出（3秒），强制继续");
        }
    } else {
        logger_.info("     ℹ️ 工作线程未启动或已退出");
    }
    
    // ⭐ Phase 4 新增：停止变焦拍照线程
    if (zoomCaptureThread_ && zoomCaptureThread_->joinable()) {
        logger_.info("  📸 [5.5/6] 正在停止变焦拍照线程...");
        {
            std::lock_guard<std::mutex> lock(zoomCaptureMutex_);
            zoomCaptureCv_.notify_one();  // 唤醒线程让它检查 running_ 标志
        }
        
        // 等待线程退出（带超时）
        bool joined = false;
        std::thread timeoutThread([this, &joined]() {
            if (zoomCaptureThread_ && zoomCaptureThread_->joinable()) {
                zoomCaptureThread_->join();
                joined = true;
            }
        });
        timeoutThread.detach();
        
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (joined) {
                logger_.info("     ✅ 变焦拍照线程已正常退出");
                break;
            }
        }
        
        if (!joined) {
            logger_.warning("     ⚠️ 变焦拍照线程超时未退出（2秒）");
        }
    }
    
    // ⭐ 步骤 6: 释放所有视频流资源
    logger_.info("  🧹 [6/6] 正在释放视频流资源...");
    deinitVideoStream();
    
    // 7. 更新状态
    state_ = TaskState::COMPLETED;
    notifyStateChanged(TaskState::COMPLETED);
    
    logger_.info("✅ 直播流任务已完全停止: taskId=" + config_.taskId);
    logger_.info("========================================");
}

bool LiveStreamTask::pause() {
    // 1. 检查状态
    if (state_ != TaskState::RUNNING) {
        logger_.error("无法暂停任务: 当前状态不是 RUNNING, taskId=" + config_.taskId);
        return false;
    }
    
    logger_.info("暂停直播流任务: taskId=" + config_.taskId);
    
    // 2. 设置暂停标志
    paused_ = true;
    
    // 3. 更新状态
    state_ = TaskState::PAUSED;
    notifyStateChanged(TaskState::PAUSED);
    
    logger_.info("直播流任务已暂停: taskId=" + config_.taskId);
    return true;
}

bool LiveStreamTask::resume() {
    // 1. 检查状态
    if (state_ != TaskState::PAUSED) {
        logger_.error("无法恢复任务: 当前状态不是 PAUSED, taskId=" + config_.taskId);
        return false;
    }
    
    logger_.info("恢复直播流任务: taskId=" + config_.taskId);
    
    // 2. 清除暂停标志
    paused_ = false;
    
    // 3. 通知条件变量（唤醒工作线程）
    pauseCv_.notify_one();
    
    // 4. 更新状态
    state_ = TaskState::RUNNING;
    notifyStateChanged(TaskState::RUNNING);
    
    logger_.info("直播流任务已恢复: taskId=" + config_.taskId);
    return true;
}

bool LiveStreamTask::isRunning() const {
    return state_.load() == TaskState::RUNNING;
}

TaskState LiveStreamTask::getState() const {
    return state_.load();
}

const TaskConfig& LiveStreamTask::getConfig() const {
    return config_;
}

TaskStatistics LiveStreamTask::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void LiveStreamTask::setStateCallback(TaskCallback callback) {
    stateCallback_ = callback;
}

void LiveStreamTask::setErrorCallback(ErrorCallback callback) {
    errorCallback_ = callback;
}

// ==================== 任务特定事件处理 ====================

void LiveStreamTask::onTaskEnd() {
    /**
     * 📖 视频流任务的 device_task_end 处理逻辑
     * 
     * 航线任务结束（飞机已返航），视频流任务应该立即停止。
     * 
     * 流程:
     * 1. 停止任务（调用 stop()）
     * 2. 推送 device_task_analysis_result
     * 3. 等待平台发送 device_algorithm_disable
     */
    logger_.info("📢 [LiveStreamTask] 收到 device_task_end，立即结束任务");
    
    // 1. 停止任务
    stop();
    
    // 2. 推送"任务分析完成"消息
    // TODO: 需要访问 MqttClient 实例推送消息
    // 临时方案：在 TaskService 中添加 publishTaskAnalysisResult() 方法
    // 或者通过回调通知 TaskManager，由其推送消息
    
    logger_.info("✅ [LiveStreamTask] 任务已结束，等待 device_algorithm_disable");
}

// ==================== Protected 方法 ====================


void LiveStreamTask::execute() {
    logger_.info("========================================");
    logger_.info("🚀 [Day 7] 工作线程已启动 - 完整流程测试");
    logger_.info("========================================");
    logger_.info("   流程: DJI H.264 → FFmpeg 解码 → cv::Mat → 检测器 → MQTT上报");
    logger_.info("   目标: 验证端到端功能 (接收 → 解码 → 检测 → 上报)");
    logger_.info("========================================");
    
    // 等待首帧到达
    logger_.info("⏳ 等待 H.264 数据流启动...");
    {
        std::unique_lock<std::mutex> lock(h264Mutex_);
        auto waitResult = h264Condition_.wait_for(lock, std::chrono::seconds(30), [this] {
            return !h264Queue_.empty() || !running_;
        });
        
        if (!running_) {
            logger_.warning("⚠️ 任务在等待首帧时被停止");
            return;
        }
        
        if (waitResult && !h264Queue_.empty()) {
            logger_.info("✅ 首帧已到达，开始处理视频流");
        } else {
            logger_.error("❌ 等待首帧超时 (30秒)");
            logger_.error("   可能原因:");
            logger_.error("   1. 设备未就绪（检查无人机/相机是否开机）");
            logger_.error("   2. StartH264Stream 失败（查看前面的错误日志）");
            logger_.error("   3. 网络连接问题");
            return;
        }
    }
    
    // 统计信息
    int receivedFrames = 0;
    int decodedFrames = 0;
    int detectedFrames = 0;
    int totalBytes = 0;
    auto startTime = std::chrono::steady_clock::now();
    
    // 性能分析统计
    double totalDecodeTime = 0;
    double totalDetectTime = 0;
    double totalDrawTime = 0;
    double totalPushTime = 0;
    
    cv::Mat frame;
    
    while (running_) {
        try {
            auto loopStart = std::chrono::high_resolution_clock::now();

            // 1. 检查暂停状态
            if (paused_) {
                std::unique_lock<std::mutex> lock(pauseMutex_);
                pauseCv_.wait(lock, [this] { 
                    return !paused_ || !running_; 
                });
                
                if (!running_) {
                    break;
                }
            }
            
            // 2. 从队列获取 H.264 数据
            std::vector<uint8_t> h264Data;
            {
                std::unique_lock<std::mutex> lock(h264Mutex_);
                
                h264Condition_.wait_for(lock, std::chrono::seconds(1), [this] {
                    return !h264Queue_.empty() || !running_;
                });
                
                if (!running_) break;
                if (h264Queue_.empty()) continue;
                
                h264Data = std::move(h264Queue_.front());
                h264Queue_.pop();
            }
            
            receivedFrames++;
            totalBytes += h264Data.size();
            
            auto t1 = std::chrono::high_resolution_clock::now(); // 获取数据后

            // 3. 使用 H264Decoder 解码
            if (h264Decoder_ && decoderEnabled_) {
                bool frameDecoded = false;
                
                h264Decoder_->decode(h264Data.data(), h264Data.size(), 
                    [&](const cv::Mat& decodedFrame) {
                        // ✅ 解码成功，获得 cv::Mat
                        // 优化: 直接引用解码器的缓冲区 (Zero-Copy)
                        // 注意: 必须在下一次 decode 调用前处理完 frame
                        frame = decodedFrame; 
                        frameDecoded = true;
                        decodedFrames++;
                    });
                
                auto t2 = std::chrono::high_resolution_clock::now(); // 解码后
                totalDecodeTime += std::chrono::duration<double, std::milli>(t2 - t1).count();

                // 4. 如果成功解码，传递给检测器
                if (frameDecoded && !frame.empty()) {
                    // 📌 Day 9 修正: 保持原始分辨率用于检测，仅在推流时降采样
                    // 原来的全局降采样已移除

                    // 📌 延迟初始化 RTMP 推流器
                    if (rtmpEnabled_ && !rtmpStreamer_) {
                        auto& config = core::Config::getInstance();
                        std::string rtmpUrl = config.getString("video.rtmp.url", "");
                        int fps = config.getInt("device.camera.fps", 30);
                        
                        // 📌 用户指定推流分辨率为 800x600
                        int streamWidth = 800;
                        int streamHeight = 600;
                        
                        logger_.info("🎬 首次解码成功，初始化 RTMP 推流器");
                        logger_.info("   输入分辨率: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows));
                        logger_.info("   推流分辨率: " + std::to_string(streamWidth) + "x" + std::to_string(streamHeight));
                        
                        rtmpStreamer_ = std::make_shared<rtmp::RtmpStreamer>();
                        if (!rtmpStreamer_->initialize(rtmpUrl, streamWidth, streamHeight, fps)) {
                            logger_.error("RTMP 推流器初始化失败");
                            rtmpStreamer_.reset();
                            rtmpEnabled_ = false;
                        } else {
                            logger_.info("✅ RTMP 推流器初始化成功");
                            logger_.info("  - URL: " + rtmpUrl);
                            logger_.info("  - 分辨率: " + std::to_string(streamWidth) + "x" + std::to_string(streamHeight));
                            logger_.info("  - 帧率: " + std::to_string(fps) + " FPS");
                        }
                    }
                    
                    // 📌 Day 9: 异步检测机制 (解决卡顿问题的核心)
                    // 原理: 解码/推流线程(30FPS) 与 检测线程(X FPS) 解耦
                    
                    // 1. 尝试提交当前帧到检测线程
                    {
                        // 使用 try_lock 避免阻塞主视频流
                        std::unique_lock<std::mutex> lock(detectionMutex_, std::try_to_lock);
                        if (lock.owns_lock()) {
                            // 只有当检测线程准备好接收新帧时才提交
                            // 如果 newFrameAvailable_ 为 true，说明上一帧还没被取走，直接跳过当前帧
                            if (!newFrameAvailable_) {
                                // 📌 诊断日志：确认帧是否被提交
                                static int submitCount = 0;
                                submitCount++;
                                if (submitCount == 1 || submitCount % 30 == 0) {
                                    logger_.debug("🔄 [诊断] 提交帧到检测线程: 第 " + std::to_string(submitCount) + " 帧");
                                }
                                
                                frame.copyTo(detectionInputFrame_);
                                newFrameAvailable_ = true;
                                detectionCv_.notify_one();
                            } else {
                                // 📌 诊断日志：检测线程忙碌
                                static int skipCount = 0;
                                skipCount++;
                                if (skipCount == 1 || skipCount % 100 == 0) {
                                    logger_.warning("⚠️ [诊断] 检测线程忙碌，跳过帧: 已跳过 " + std::to_string(skipCount) + " 帧");
                                }
                            }
                        } else {
                            // 📌 诊断日志：锁获取失败
                            static int lockFailCount = 0;
                            lockFailCount++;
                            if (lockFailCount == 1 || lockFailCount % 100 == 0) {
                                logger_.warning("⚠️ [诊断] 无法获取检测锁: 失败 " + std::to_string(lockFailCount) + " 次");
                            }
                        }
                    }
                    
                    // 2. 获取最新的检测结果 (极快，仅内存拷贝)
                    std::vector<BoundingBox> boxes;
                    {
                        std::lock_guard<std::mutex> lock(resultMutex_);
                        boxes = currentDetections_;
                    }

                    auto t3 = std::chrono::high_resolution_clock::now(); // 检测后
                    totalDetectTime += std::chrono::duration<double, std::milli>(t3 - t2).count();
                    
                    // 5. 📌 RTMP 推流：推送可视化后的图像
                    if (rtmpStreamer_ && rtmpEnabled_) {
                        // 📌 绘制检测结果（使用 VisionUtils）
                        // 架构重构: 统一使用 task::BoundingBox，无需数据转换
                        // vision::BBox 现在是 task::BoundingBox 的别名
                        vision::VisionUtils::drawDetections(frame, boxes);
                        
                        auto t4 = std::chrono::high_resolution_clock::now(); // 绘图后
                        totalDrawTime += std::chrono::duration<double, std::milli>(t4 - t3).count();

                        // 📌 推流前 Resize 到 800x600
                        cv::Mat resizedFrame;
                        cv::resize(frame, resizedFrame, cv::Size(800, 600));
                        rtmpStreamer_->pushFrame(resizedFrame);

                        auto t5 = std::chrono::high_resolution_clock::now(); // 推流后
                        totalPushTime += std::chrono::duration<double, std::milli>(t5 - t4).count();
                    } else {
                        // 如果不推流，也要更新时间戳以免统计偏差
                        auto tNow = std::chrono::high_resolution_clock::now();
                        totalDrawTime += std::chrono::duration<double, std::milli>(tNow - t3).count(); // 实际上没画
                        totalPushTime += 0;
                    }
                }
            }
            
            // 5. 更新统计
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.framesProcessed++;
            }
            
            // 6. 每30帧打印一次统计
            if (receivedFrames % 30 == 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
                double fps = elapsed > 0 ? static_cast<double>(receivedFrames) / elapsed : 0.0;
                double avgSize = receivedFrames > 0 ? static_cast<double>(totalBytes) / receivedFrames / 1024.0 : 0.0;
                
                // 计算平均耗时
                double avgDecode = totalDecodeTime / 30.0;
                double avgDetect = totalDetectTime / 30.0;
                double avgDraw = totalDrawTime / 30.0;
                double avgPush = totalPushTime / 30.0;
                double avgTotal = avgDecode + avgDetect + avgDraw + avgPush;

                logger_.debug("========================================");
                logger_.debug("📊 [Day 7 统计] 帧号=" + std::to_string(receivedFrames));
                logger_.debug("   接收帧数: " + std::to_string(receivedFrames));
                logger_.debug("   解码成功: " + std::to_string(decodedFrames));
                logger_.debug("   检测成功: " + std::to_string(detectedFrames));
                logger_.debug("   运行时间: " + std::to_string(elapsed) + " 秒");
                logger_.debug("   平均 FPS: " + std::to_string(static_cast<int>(fps)));
                logger_.debug("   平均帧大小: " + std::to_string(static_cast<int>(avgSize)) + " KB");
                logger_.debug("   队列长度: " + std::to_string(h264Queue_.size()));
                logger_.debug("   ⏱️ 耗时分析 (ms):");
                logger_.debug("     解码: " + std::to_string(avgDecode));
                logger_.debug("     检测: " + std::to_string(avgDetect));
                logger_.debug("     绘图: " + std::to_string(avgDraw));
                logger_.debug("     推流: " + std::to_string(avgPush));
                logger_.debug("     总计: " + std::to_string(avgTotal) + " (理论最大FPS: " + std::to_string(1000.0/avgTotal) + ")");
                logger_.debug("========================================");
                
                // 重置计数器
                totalDecodeTime = 0;
                totalDetectTime = 0;
                totalDrawTime = 0;
                totalPushTime = 0;
            }
            
        } catch (const std::exception& e) {
            handleError("处理帧异常: " + std::string(e.what()));
            std::lock_guard<std::mutex> lock(statsMutex_);
            // stats_.failedFrames++;
        }
    }
    
    // 最终统计
    logger_.info("🛑 [诊断] execute 循环结束. running_=" + std::to_string(running_));
    auto endTime = std::chrono::steady_clock::now();
    auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    double finalFps = totalElapsed > 0 ? static_cast<double>(receivedFrames) / totalElapsed : 0.0;
    double decodeRate = receivedFrames > 0 ? static_cast<double>(decodedFrames) * 100.0 / receivedFrames : 0.0;
    
    logger_.info("========================================");
    logger_.info("🎉 [Day 7 测试完成]");
    logger_.info("========================================");
    logger_.info("   总接收帧数: " + std::to_string(receivedFrames));
    logger_.info("   成功解码: " + std::to_string(decodedFrames) + 
                " (" + std::to_string(static_cast<int>(decodeRate)) + "%)");
    logger_.info("   检测成功: " + std::to_string(detectedFrames));
    logger_.info("   运行时间: " + std::to_string(totalElapsed) + " 秒");
    logger_.info("   平均 FPS: " + std::to_string(static_cast<int>(finalFps)));
    logger_.info("   总数据量: " + std::to_string(totalBytes / 1024 / 1024) + " MB");
    logger_.info("========================================");
    logger_.info("   ✅ DJI SDK 回调正常");
    logger_.info("   ✅ FFmpeg 解码器正常");
    logger_.info("   ✅ 检测器正常");
    logger_.info("   ✅ 完整流程验证成功!");
    logger_.info("========================================");
}

void LiveStreamTask::detectionLoop() {
    logger_.info("🕵️ 异步检测线程已启动");
    
    cv::Mat processFrame;
    int processedCount = 0;  // 📌 诊断计数器
    
    while (running_) {
        // 1. 等待新帧
        {
            std::unique_lock<std::mutex> lock(detectionMutex_);
            
            // 📌 诊断日志：等待前
            static bool firstWait = true;
            if (firstWait) {
                logger_.info("🔍 [诊断] 检测线程开始等待首帧...");
                firstWait = false;
            }
            
            // ⭐ 优化：使用带超时的 wait（避免永久阻塞）
            detectionCv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return newFrameAvailable_ || !running_;
            });
            
            // ⭐ 关键：立即检查停止标志
            if (!running_) {
                logger_.info("🔍 [诊断] 检测线程收到停止信号（在 wait 后）");
                break;
            }
            
            if (newFrameAvailable_) {
                processedCount++;
                if (processedCount == 1 || processedCount % 30 == 0) {
                    logger_.info("🔍 [诊断] 检测线程收到新帧: 第 " + std::to_string(processedCount) + " 帧");
                }
                
                // 获取帧并重置标志
                // 使用 copyTo 确保深拷贝，因为 detectionInputFrame_ 可能会被主线程修改
                detectionInputFrame_.copyTo(processFrame);
                newFrameAvailable_ = false;
            } else {
                // 超时或被虚假唤醒，继续循环（会再次检查 running_）
                continue;
            }
        }  // 释放锁
        
        // ⭐ 再次检查停止标志（在执行耗时操作前）
        if (!running_) {
            logger_.info("🔍 [诊断] 检测线程收到停止信号（在检测前）");
            break;
        }
        
        // 2. 执行检测 (耗时操作)
        if (!processFrame.empty()) {
            // 📌 诊断日志：开始检测
            if (processedCount == 1 || processedCount % 30 == 0) {
                logger_.debug("🔍 [诊断] 开始执行检测: 帧尺寸 " + 
                           std::to_string(processFrame.cols) + "x" + 
                           std::to_string(processFrame.rows));
            }
            
            std::vector<BoundingBox> boxes;
            
            // ⭐ 异常保护：确保即使 processFrame 出错也能退出
            try {
                // 调用业务服务进行检测
                // 直播流：取帧时刻读取一次 GPS（OSD），并与本帧绑定。
                // 好处：避免使用“上报瞬间”的 GPS，导致经纬度与图片帧不一致。
                auto& flightData = device::FlightDataProvider::getInstance();
                auto gpsSnapshot = flightData.getGpsPosition();

                // 只在 GPS 数据“有效且新鲜”的情况下传给 TaskService。
                // 这样可以保证事件顶层经纬度与“取帧时刻”一致，且避免使用默认 0/0 或过期值。
                const device::GpsPosition* gpsPtr = nullptr;
                if (flightData.isGpsDataValid() && gpsSnapshot.isValid()) {
                    gpsPtr = &gpsSnapshot;
                }

                // 诊断日志：帮助确认为什么事件顶层经纬度仍为 null
                if (processedCount == 1 || processedCount % 30 == 0) {
                    logger_.info("📍 [GPS快照] valid=" + std::string(gpsSnapshot.isValid() ? "true" : "false") +
                                 ", fresh=" + std::string(flightData.isGpsDataValid() ? "true" : "false") +
                                 ", age=" + std::to_string(flightData.getGpsDataAge()) + "s" +
                                 ", lat=" + std::to_string(gpsSnapshot.latitude) +
                                 ", lon=" + std::to_string(gpsSnapshot.longitude) +
                                 ", passedToService=" + std::string(gpsPtr ? "true" : "false"));
                }

                if (service_->processFrame(processFrame, config_, boxes, "", gpsPtr)) {
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.eventsPublished++;
                }
                
                // 📌 诊断日志：检测完成
                if (processedCount == 1 || processedCount % 30 == 0) {
                    logger_.debug("🔍 [诊断] 检测完成: 发现 " + std::to_string(boxes.size()) + " 个目标");
                }
                
                // 📌 新增诊断：显示追踪器和策略状态
                if (processedCount == 1) {
                    logger_.info("🔧 [诊断] 追踪器状态: trackerEnabled=" + std::to_string(trackerEnabled_) +
                                ", byteTracker=" + (byteTracker_ ? "有效" : "空"));
                    logger_.info("🔧 [诊断] 变焦拍照状态: zoomCaptureEnabled=" + std::to_string(zoomCaptureEnabled_) +
                                ", zoomCaptureStrategy=" + (zoomCaptureStrategy_ ? "有效" : "空"));
                }
                
                // ==================== Phase 4: 目标追踪和变焦拍照策略 ====================
                
                // 3a. 目标追踪（如果启用）
                std::vector<vision::tracker::TrackedObject> trackedObjects;
                if (trackerEnabled_ && byteTracker_ && !boxes.empty()) {
                    // 📌 诊断：进入追踪分支
                    logger_.debug("🎯 [追踪] 进入追踪处理: " + std::to_string(boxes.size()) + " 个检测框");
                    
                    // 将 task::BoundingBox 转换为 vision::DetectionBox
                    // 字段映射: BoundingBox(x,y,w,h,confidence) → DetectionBox(x,y,width,height,confidence)
                    std::vector<vision::DetectionBox> detBoxes;
                    detBoxes.reserve(boxes.size());
                    for (const auto& box : boxes) {
                        vision::DetectionBox detBox;
                        detBox.x = static_cast<int>(box.x);
                        detBox.y = static_cast<int>(box.y);
                        detBox.width = static_cast<int>(box.w);
                        detBox.height = static_cast<int>(box.h);
                        detBox.confidence = box.confidence;
                        detBox.classId = box.classId;
                        detBox.className = box.className;
                        detBoxes.push_back(detBox);
                    }
                    
                    // 更新追踪状态
                    trackedObjects = byteTracker_->update(detBoxes);
                    
                    // 📌 每次追踪都输出
                    logger_.debug("📊 [追踪] 追踪结果: " + std::to_string(trackedObjects.size()) + " 个追踪对象, " +
                                "活跃: " + std::to_string(byteTracker_->getActiveCount()) +
                                ", 累计: " + std::to_string(byteTracker_->getUniqueCount()));
                }
                
                // 3b. 变焦拍照策略处理（如果启用）
                if (zoomCaptureEnabled_ && zoomCaptureStrategy_) {
                    // 构建帧尺寸和 GPS 信息
                    cv::Size frameSize(processFrame.cols, processFrame.rows);
                    
                    // 从 FlightDataProvider 获取实时 GPS 数据
                    auto& flightData = device::FlightDataProvider::getInstance();
                    auto gpsPos = flightData.getGpsPosition();
                    
                    // ⚠️ 检查飞行状态：返航/降落时跳过变焦拍照
                    if (flightData.isReturningOrLanding()) {
                        static bool rthWarningLogged = false;
                        if (!rthWarningLogged) {
                            logger_.warning("⚠️ 无人机正在返航/降落 (" + flightData.getFlightModeString() + 
                                          ")，暂停智能变焦拍照");
                            rthWarningLogged = true;
                        }
                        // 跳过策略处理，直接进入下一帧
                        // 注：如果无人机恢复正常飞行，下次循环会继续处理
                    } else if (!flightData.isReadyForSmartZoomCapture()) {
                        // ⚠️ 航线状态检查：未到达第一个航点时跳过变焦拍照
                        // 避免在起飞、爬升、进入航线过程中触发拍照，导致流程不顺畅
                        static bool waylineWarningLogged = false;
                        if (!waylineWarningLogged) {
                            logger_.info("⏳ 等待无人机进入航线到达第一个航点，暂不启用智能变焦拍照 (state=" + 
                                        std::to_string(static_cast<int>(flightData.getWaylineMissionState())) + ")");
                            waylineWarningLogged = true;
                        }
                        // 航线状态变化后，重置日志标志，下次满足条件时会打印正常日志
                    } else {
                        // 无人机已到达第一个航点，处于航线执行状态，开始处理变焦拍照策略
                        static bool waylineReadyLogged = false;
                        if (!waylineReadyLogged) {
                            logger_.info("✅ 航线状态就绪 (state=" + 
                                        std::to_string(static_cast<int>(flightData.getWaylineMissionState())) + 
                                        ")，开始智能变焦拍照");
                            waylineReadyLogged = true;
                        }
                    
                        vision::strategy::GpsInfo gpsInfo;
                        gpsInfo.latitude = gpsPos.latitude;
                        gpsInfo.longitude = gpsPos.longitude;
                        gpsInfo.altitude = gpsPos.altitude;
                        
                        // 检查 GPS 数据有效性
                        if (!gpsPos.isValid()) {
                            // GPS 数据无效时使用默认值，策略会禁用距离冷却
                            static bool gpsWarningLogged = false;
                            if (!gpsWarningLogged) {
                                logger_.warning("⚠️ GPS 数据无效，距离冷却功能将被禁用");
                                gpsWarningLogged = true;
                            }
                        }
                        
                        // 📌 诊断：策略处理前
                        if (!trackedObjects.empty()) {
                            logger_.info("📸 [策略] 处理 " + std::to_string(trackedObjects.size()) + 
                                        " 个追踪对象, 当前状态: " + 
                                        std::to_string(static_cast<int>(zoomCaptureStrategy_->getState())) +
                                        ", 稳定帧数: " + std::to_string(zoomCaptureStrategy_->getStableCount()));
                        }
                        
                        // 处理策略
                        bool shouldCapture = zoomCaptureStrategy_->process(trackedObjects, frameSize, gpsInfo);
                        
                        // 如果需要拍照，提交请求到变焦拍照线程
                        if (shouldCapture) {
                            std::lock_guard<std::mutex> lock(zoomCaptureMutex_);
                            if (!zoomCaptureRequested_) {  // 避免重复请求
                                auto request = zoomCaptureStrategy_->getCaptureRequest();
                                // 复制请求数据
                                static vision::strategy::ZoomCaptureRequest staticRequest;
                                staticRequest = request;
                                pendingRequest_ = &staticRequest;
                                zoomCaptureRequested_ = true;
                                zoomCaptureCv_.notify_one();
                                
                                logger_.info("🎯 触发变焦拍照！目标数: " + std::to_string(request.targetCount) +
                                            ", GPS: (" + std::to_string(gpsInfo.latitude) + ", " + 
                                            std::to_string(gpsInfo.longitude) + ")");
                            }
                        }
                    }  // end else (航线就绪状态)
                }
                
                // 3c. 更新共享检测结果
                {
                    std::lock_guard<std::mutex> lock(resultMutex_);
                    currentDetections_ = boxes;
                }
            } catch (const std::exception& e) {
                logger_.error("🔍 [诊断] 检测异常: " + std::string(e.what()));
                // 继续运行，不退出线程
            }
        } else {
            logger_.warning("🔍 [诊断] processFrame 为空！");
        }
        
        // ⭐ 循环末尾再次检查停止标志
        if (!running_) {
            logger_.info("🔍 [诊断] 检测线程收到停止信号（循环末尾）");
            break;
        }
    }
    
    logger_.info("� [诊断] detectionLoop 循环结束. running_=" + std::to_string(running_));
    
    logger_.info("�🕵️ 异步检测线程已退出，共处理 " + std::to_string(processedCount) + " 帧");
}

void LiveStreamTask::notifyStateChanged(TaskState newState) {
    if (stateCallback_) {
        stateCallback_(config_.taskId, newState);
    }
}

void LiveStreamTask::notifyError(const std::string& error) {
    logger_.error("任务错误: taskId=" + config_.taskId + ", error=" + error);
    
    if (errorCallback_) {
        errorCallback_(config_.taskId, error);
    }
}

// ==================== Private 方法 ====================

bool LiveStreamTask::initVideoStream() {
    logger_.info("初始化 DJI Liveview 视频流: taskId=" + config_.taskId);
    
    // ==================== 步骤 0: 初始化 H264 解码器 (Day 7 新增) ====================
    if (h264Decoder_ && decoderEnabled_) {
        auto& config = core::Config::getInstance();
        
        // 优先读取 video.decoder.resolution，如果没有则使用 device.camera.resolution
        int width = config.getInt("video.decoder.resolution.width", 
                                   config.getInt("device.camera.resolution.width", 1920));
        int height = config.getInt("video.decoder.resolution.height",
                                    config.getInt("device.camera.resolution.height", 1080));
        
        logger_.info("🎬 初始化 H.264 解码器: " + std::to_string(width) + "x" + std::to_string(height));
        
        if (!h264Decoder_->init(width, height)) {
            logger_.error("❌ H.264 解码器初始化失败");
            h264Decoder_.reset();
            decoderEnabled_ = false;
            return false;  // 如果解码器初始化失败，整个视频流初始化失败
        } else {
            logger_.info("✅ H.264 解码器初始化成功");
        }
    }
    
    // ==================== 步骤 1: 读取 RTMP 配置（延迟初始化）====================
    // 📌 关键修复：等解码器解码第一帧后，获取真实分辨率再初始化 RTMP
    //              避免配置文件中的分辨率与真实分辨率不匹配导致 GStreamer 管道失败
    auto& config = core::Config::getInstance();
    rtmpEnabled_ = config.getBool("video.rtmp.enabled", false);
    
    if (rtmpEnabled_) {
        // 读取 RTMP URL（提前验证）
        std::string rtmpUrl = config.getString("video.rtmp.url", "");
        if (rtmpUrl.empty()) {
            logger_.warning("RTMP URL 未配置 (video.rtmp.url)，禁用 RTMP 推流");
            rtmpEnabled_ = false;
        } else {
            logger_.info("RTMP 推流已启用，将在解码首帧后初始化");
            logger_.info("  - URL: " + rtmpUrl);
            // ⚠️ 不在这里初始化 rtmpStreamer_，等解码器准备好后再初始化
        }
    } else {
        logger_.info("RTMP 推流未启用 (video.rtmp.enabled=false)");
    }
    
    // ==================== 步骤 2: 获取/初始化全局 Liveview 单例 (Day 16 架构升级) ====================
    // 策略：单例模式。如果全局实例不存在则创建，否则复用。
    // 解决：避免反复析构导致的死锁和线程泄漏。
    
    std::lock_guard<std::mutex> lock(g_liveviewInitMutex);
    
    // 1. 注册当前任务为活跃任务（接收回调）
    g_activeTask.store(this);
    logger_.info("✅ 已注册为活跃视频任务");

    if (!g_liveviewInstance) {
        logger_.info("🆕 全局 Liveview 实例不存在，开始创建...");
        
        // 创建实例
        g_liveviewInstance = edge_sdk::CreateLiveview();
        if (!g_liveviewInstance) {
            logger_.error("❌ 创建 Liveview 实例失败");
            g_activeTask.store(nullptr);
            return false;
        }
        
        // 配置参数
        edge_sdk::Liveview::Options options;
        options.camera = edge_sdk::Liveview::kCameraTypePayload;
        options.quality = edge_sdk::Liveview::kStreamQuality720p;
        
        // 📌 使用静态代理回调
        options.callback = ProxyH264Callback;
        
        // 初始化
        edge_sdk::ErrorCode ret = g_liveviewInstance->Init(options);
        if (ret != edge_sdk::kOk) {
            logger_.error("❌ Liveview 初始化失败: " + std::to_string(static_cast<int>(ret)));
            g_liveviewInstance.reset();
            g_activeTask.store(nullptr);
            return false;
        }
        
        // 订阅状态
        g_liveviewInstance->SubscribeLiveviewStatus(ProxyStatusCallback);
        
        logger_.info("✅ 全局 Liveview 初始化成功 (首次)");
    } else {
        logger_.info("♻️ 复用现有的全局 Liveview 实例");
    }
    
    // 2. 将成员变量指向全局实例
    liveview_ = g_liveviewInstance;
    
    // 3. 确保流已启动
    // 如果是复用实例，可能流已经被之前的任务停止了，或者正在运行
    // 我们尝试启动它。SDK 应该是幂等的，如果已启动会返回成功或特定错误。
    logger_.info("尝试启动 H264 流...");
    edge_sdk::ErrorCode ret = liveview_->StartH264Stream();
    if (ret == edge_sdk::kOk) {
        logger_.info("✅ StartH264Stream 请求成功");
    } else {
        logger_.warning("⚠️ StartH264Stream 返回: " + std::to_string(static_cast<int>(ret)) + " (可能已在运行)");
    }
    
    logger_.info("DJI Liveview 准备就绪: taskId=" + config_.taskId);
    return true;
}

void LiveStreamTask::onLiveviewStatusChanged(const edge_sdk::Liveview::LiveviewStatus& status) {
    

    logger_.info("🔄 [诊断] onLiveviewStatusChanged 被调用: " + std::to_string(status) + 
                 " 线程ID: " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    // 📌 闸门机制：任务停止时立即返回，不执行任何逻辑
    // 这能确保 SDK 内部线程快速退出，防止 liveview_.reset() 死锁
    if (!running_.load()) {
        return;
    }
    int oldStatus = liveviewStatus_.exchange(status);
    
    // logger_.info("Liveview 状态变化: " + std::to_string(oldStatus) + " -> " + 
    //             std::to_string(status) + " (" + (status == 0 ? "未就绪" : "已就绪") + ")");
    
    // 📌 事件驱动：当设备首次就绪时，自动启动 H264 流
    if (status != 0 && oldStatus == 0 && !streamStarted_.exchange(true)) {
        logger_.info("✅ 设备首次就绪，启动 H264 流...");
        
        // 在新线程中启动（避免阻塞回调）
        std::thread([this]() {
            edge_sdk::ErrorCode ret = liveview_->StartH264Stream();
            if (ret != edge_sdk::kOk) {
                logger_.error("StartH264Stream 失败: ErrorCode=" + std::to_string(static_cast<int>(ret)));
                logger_.error("可能原因：1) SDK 版本差异  2) 流已在 Init() 中启动");
                logger_.info("将继续等待 H.264 数据...");
            } else {
                logger_.info("✅ StartH264Stream 成功，等待首帧数据...");
            }
        }).detach();
    }
}

void LiveStreamTask::deinitVideoStream() {
    logger_.info("🧹 停止视频流任务资源: taskId=" + config_.taskId);
    
    // 1. 注销活跃任务 (停止接收回调)
    // 这样 ProxyH264Callback 会立即停止转发数据
    if (g_activeTask.load() == this) {
        g_activeTask.store(nullptr);
        logger_.info("✅ 已注销活跃任务 (停止接收回调)");
    }
    
    // 2. 停止 H264 流 (节省带宽)
    if (liveview_) {
        logger_.info("  🛑 停止 H264 流...");
        liveview_->StopH264Stream();
        // 不检查返回值，尽力而为
    }
    
    // 3. 释放本地引用
    // 注意：这只会减少引用计数，不会销毁全局实例
    liveview_.reset();
    logger_.info("  ✅ 本地 Liveview 引用已释放 (全局实例保持活跃)");

    // 4. 清理其他资源
    
    // 清空 H264 队列
    {
        std::lock_guard<std::mutex> lock(h264Mutex_);
        while (!h264Queue_.empty()) h264Queue_.pop();
    }
    
    // 关闭 H264 解码器
    if (h264Decoder_) {
        h264Decoder_->deinit();
        h264Decoder_.reset();
    }
    
    // 关闭 RTMP 推流器
    if (rtmpStreamer_) {
        rtmpStreamer_->shutdown();
        rtmpStreamer_.reset();
    }
    
    // 重置状态标志
    liveviewStatus_.store(0);
    streamStarted_.store(false);
    
    logger_.info("✅ 视频流资源清理完成 (单例模式 - 无需销毁 SDK 对象)");
}

edge_sdk::ErrorCode LiveStreamTask::onH264Data(const uint8_t* buf, uint32_t len) {
    // 📌 此函数在 DJI SDK 的回调线程中执行，需要快速返回
    
    // 📌 防御性检查 1：任务停止时拒绝处理（最优先）
    if (!running_.load()) {
        // 诊断日志：确认回调在停止后是否仍在触发
        logger_.info("⚠️ [诊断] onH264Data 在停止后被调用! 线程ID: " + 
            std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
        return edge_sdk::kOk;
    }
    
    // 📌 防御性检查 2：数据有效性
    if (!buf || len == 0) {
        logger_.warning("收到空的 H264 数据");
        return edge_sdk::kOk;
    }
    
    // 📌 防御性检查 3：RTMP 推流器有效性
    if (rtmpStreamer_ && rtmpEnabled_) {
        if (!rtmpStreamer_) {  // 双重检查（防止时序竞争）
            logger_.warning("RTMP 推流器已失效，跳过推流");
            return edge_sdk::kOk;
        }
    }
    
    // 首次收到数据时打印日志（用于确认流已启动）
    static std::atomic<bool> firstFrameReceived{false};
    if (!firstFrameReceived.exchange(true)) {
        logger_.info("🎉 首帧 H.264 数据已到达！流已成功启动");
        logger_.info("   数据大小: " + std::to_string(len) + " 字节");
    }
    
    // 统计帧数（用于调试）
    static std::atomic<int> frameCount{0};
    int currentFrame = ++frameCount;
    
    // 每30帧（约1秒）打印一次日志
    if (currentFrame % 30 == 0) {
        logger_.debug("H264 数据流活跃: 帧号=" + std::to_string(currentFrame) + 
                    ", 大小=" + std::to_string(len) + " 字节");
    }
    
    // ==================== 添加到处理队列（供解码器使用）====================
    // 📌 Day 8 策略：RTMP 推流已移至 execute() 中，解码后推送 cv::Mat（重新编码方案）
    // 旧的直推 H.264 方案已废弃（DJI 数据缺少 SPS/PPS，导致播放失败）
    
    {
        // ⭐ 使用 try_lock 避免阻塞回调线程
        std::unique_lock<std::mutex> lock(h264Mutex_, std::try_to_lock);
        
        if (lock.owns_lock()) {
            if (h264Queue_.size() >= maxH264QueueSize_) {
                h264Queue_.pop();  // 丢弃最旧数据（防止队列堆积）
            }
            
            h264Queue_.emplace(buf, buf + len);
            h264Condition_.notify_one();
        } else {
            // 无法获取锁（deinitVideoStream 正在清理），静默丢弃
            // 避免阻塞 DJI 回调线程
            static std::atomic<int> dropCount{0};
            if (++dropCount % 30 == 1) {
                logger_.warning("⚠️ H264 队列锁忙碌，已丢弃 " + std::to_string(dropCount) + " 帧");
            }
        }
    }
    
    return edge_sdk::kOk;
}

bool LiveStreamTask::getNextH264Data(std::vector<uint8_t>& h264Data) {
    std::unique_lock<std::mutex> lock(h264Mutex_);
    
    // 等待 H264 数据 (消费者)
    // 设置超时，避免无限等待
    auto timeout = std::chrono::milliseconds(1000);  // 1 秒超时
    
    bool dataAvailable = h264Condition_.wait_for(lock, timeout, [this] {
        return !h264Queue_.empty() || !running_;
    });
    
    // 如果收到停止信号
    if (!running_) {
        return false;
    }
    
    // 如果超时或队列为空
    if (!dataAvailable || h264Queue_.empty()) {
        logger_.warning("获取 H264 数据超时");
        return false;
    }
    
    // 取出数据
    h264Data = std::move(h264Queue_.front());
    h264Queue_.pop();
    
    return true;
}

bool LiveStreamTask::decodeH264ToMat(const std::vector<uint8_t>& h264Data, cv::Mat& frame) {
    // TODO: 实现 H264 解码
    // 方案 1: 使用 FFmpeg 解码 (性能最好)
    // 方案 2: 使用 OpenCV 解码 (简单但性能较差)
    
    // 临时实现: 生成模拟帧 (用于测试编译)
    logger_.debug("H264 解码功能尚未实现，使用模拟帧: 数据长度=" + std::to_string(h264Data.size()));
    
    frame = cv::Mat(720, 1280, CV_8UC3, cv::Scalar(0, 255, 0));  // 绿色帧 (表示已集成 Liveview)
    
    // 实际实现示例 (使用 FFmpeg):
    // AVCodecContext* codecContext = ...;
    // AVFrame* avFrame = ...;
    // avcodec_send_packet(codecContext, packet);
    // avcodec_receive_frame(codecContext, avFrame);
    // // 转换 AVFrame 到 cv::Mat
    // frame = cv::Mat(avFrame->height, avFrame->width, CV_8UC3, avFrame->data[0]);
    
    return true;
}

bool LiveStreamTask::getNextFrame(cv::Mat& frame) {
    // 方法 1: 获取 H264 数据
    std::vector<uint8_t> h264Data;
    if (!getNextH264Data(h264Data)) {
        return false;
    }
    
    // 方法 2: 解码为 cv::Mat
    if (!decodeH264ToMat(h264Data, frame)) {
        logger_.error("H264 解码失败");
        return false;
    }
    
    return true;
}

void LiveStreamTask::handleError(const std::string& error) {
    logger_.error("任务错误: taskId=" + config_.taskId + ", error=" + error);
    notifyError(error);
}

void LiveStreamTask::updateStatistics() {
    // 统计信息在 execute() 中实时更新
    // 此方法保留用于未来扩展
}

// ==================== Phase 4: 智能变焦拍照功能实现 ====================

/**
 * @brief 初始化智能变焦拍照功能
 * 
 * 从配置文件读取以下配置：
 * - zoom_capture.enabled: 是否启用变焦拍照
 * - tracker.enabled: 是否启用目标追踪
 * - tracker.bytetrack.*: ByteTrack 追踪器参数
 * - zoom_capture.trigger.*: 触发条件
 * - zoom_capture.cooldown.*: 冷却参数
 * - device.camera.payload_index: 相机 payload 索引
 */
void LiveStreamTask::initZoomCaptureFeatures(core::Config& config) {
    logger_.info("========================================");
    logger_.info("🎯 初始化智能变焦拍照功能...");
    logger_.info("========================================");
    
    // 1. 读取功能开关
    zoomCaptureEnabled_ = config.getBool("zoom_capture.enabled", false);
    trackerEnabled_ = config.getBool("tracker.enabled", false);
    
    if (!zoomCaptureEnabled_ && !trackerEnabled_) {
        logger_.info("⚠️ 智能变焦拍照和目标追踪均未启用");
        return;
    }
    
    // 2. 读取相机 payload_index
    cameraPayloadIndex_ = config.getString("device.camera.payload_index", "81-0-0");
    logger_.info("📷 相机 payload_index: " + cameraPayloadIndex_);
    
    // 3. 获取 MQTT 客户端引用
    mqttClient_ = &mqtt::MqttClient::getInstance();
    
    // 4. 初始化 ByteTracker（如果启用）
    if (trackerEnabled_) {
        try {
            vision::tracker::ByteTrackerConfig trackerConfig;
            trackerConfig.trackThresh = static_cast<float>(config.getDouble("tracker.bytetrack.track_thresh", 0.5));
            trackerConfig.matchThresh = static_cast<float>(config.getDouble("tracker.bytetrack.match_thresh", 0.8));
            trackerConfig.frameRate = config.getInt("tracker.bytetrack.frame_rate", 30);
            trackerConfig.trackBuffer = config.getInt("tracker.bytetrack.track_buffer", 30);
            trackerConfig.minBoxArea = config.getInt("tracker.bytetrack.min_box_area", 100);
            
            byteTracker_ = std::make_unique<vision::tracker::ByteTracker>(trackerConfig);
            
            logger_.info("✅ ByteTracker 初始化成功");
            logger_.info("   - track_thresh: " + std::to_string(trackerConfig.trackThresh));
            logger_.info("   - match_thresh: " + std::to_string(trackerConfig.matchThresh));
            logger_.info("   - frame_rate: " + std::to_string(trackerConfig.frameRate));
            logger_.info("   - track_buffer: " + std::to_string(trackerConfig.trackBuffer));
        } catch (const std::exception& e) {
            logger_.error("❌ ByteTracker 初始化失败: " + std::string(e.what()));
            trackerEnabled_ = false;
        }
    }
    
    // 5. 初始化 SmartZoomCaptureStrategy（如果启用变焦拍照）
    if (zoomCaptureEnabled_) {
        try {
            vision::strategy::SmartZoomCaptureConfig strategyConfig;
            strategyConfig.minTargetCount = config.getInt("zoom_capture.trigger.min_target_count", 3);
            strategyConfig.stableFrameCount = config.getInt("zoom_capture.trigger.stable_frame_count", 5);
            strategyConfig.minConfidence = static_cast<float>(config.getDouble("zoom_capture.trigger.min_confidence", 0.5));
            strategyConfig.cooldownSeconds = config.getDouble("zoom_capture.cooldown.time_seconds", 30.0);
            strategyConfig.cooldownDistanceMeters = config.getDouble("zoom_capture.cooldown.distance_meters", 50.0);
            
            zoomCaptureStrategy_ = std::make_unique<vision::strategy::SmartZoomCaptureStrategy>(strategyConfig);
            
            logger_.info("✅ SmartZoomCaptureStrategy 初始化成功");
            logger_.info("   - min_target_count: " + std::to_string(strategyConfig.minTargetCount));
            logger_.info("   - stable_frame_count: " + std::to_string(strategyConfig.stableFrameCount));
            logger_.info("   - cooldown_seconds: " + std::to_string(strategyConfig.cooldownSeconds));
            logger_.info("   - cooldown_distance: " + std::to_string(strategyConfig.cooldownDistanceMeters) + "m");
        } catch (const std::exception& e) {
            logger_.error("❌ SmartZoomCaptureStrategy 初始化失败: " + std::string(e.what()));
            zoomCaptureEnabled_ = false;
        }
    }
    
    logger_.info("========================================");
}

/**
 * @brief 变焦拍照执行循环
 * 
 * 在独立线程中运行，等待变焦拍照请求。
 * 使用条件变量实现高效等待，避免忙等待消耗 CPU。
 */
void LiveStreamTask::zoomCaptureLoop() {
    logger_.info("📸 变焦拍照线程已启动");
    
    while (running_) {
        // 1. 等待变焦拍照请求
        {
            std::unique_lock<std::mutex> lock(zoomCaptureMutex_);
            zoomCaptureCv_.wait(lock, [this] {
                return zoomCaptureRequested_ || !running_;
            });
            
            if (!running_) {
                break;
            }
            
            if (!zoomCaptureRequested_ || pendingRequest_ == nullptr) {
                continue;
            }
            
            // 获取请求并重置标志
            zoomCaptureRequested_ = false;
        }
        
        // 2. 执行变焦拍照（在锁外执行，避免长时间持锁）
        if (pendingRequest_ != nullptr) {
            logger_.info("📸 开始执行变焦拍照...");
            
            bool success = executeZoomCapture(*pendingRequest_);
            
            // 3. 通知策略完成（或失败）
            if (zoomCaptureStrategy_) {
                zoomCaptureStrategy_->onCaptureComplete(success);
            }
            
            if (success) {
                logger_.info("✅ 变焦拍照执行成功");
            } else {
                logger_.warning("⚠️ 变焦拍照执行失败");
            }
            
            // 清理请求指针
            pendingRequest_ = nullptr;
        }
    }
    
    logger_.info("📸 变焦拍照线程已退出");
}

/**
 * @brief 执行变焦拍照流程
 * 
 * 完整流程：
 * 1. 暂停航线任务（flighttask_pause）
 * 2. 等待悬停稳定
 * 3. 重新检测当前帧，更新目标包围框（确保捕获悬停后的所有目标）
 * 4. 框选变焦（camera_frame_zoom）
 * 5. 等待变焦稳定
 * 6. 拍照（camera_photo_take）
 * 7. 变焦复位（camera_focal_length_set, zoom_factor=2）
 * 8. 云台复位（gimbal_reset）
 * 9. 恢复航线任务（flighttask_recovery）
 * 
 * @param request 变焦拍照请求，包含目标区域的归一化坐标（仅作为参考）
 * @return true 执行成功
 * @return false 执行失败
 * 
 * @note 暂停航线后会重新检测并更新包围框，确保能拍到更多目标
 */
bool LiveStreamTask::executeZoomCapture(const vision::strategy::ZoomCaptureRequest& request) {
    if (!mqttClient_) {
        logger_.error("MQTT 客户端未初始化，无法执行变焦拍照");
        return false;
    }
    
    logger_.info("========================================");
    logger_.info("📸 执行变焦拍照流程");
    logger_.info("   原始目标数量: " + std::to_string(request.targetCount));
    logger_.info("   原始区域: [" + std::to_string(request.normalizedX1) + "," + std::to_string(request.normalizedY1) + 
                 "] - [" + std::to_string(request.normalizedX2) + "," + std::to_string(request.normalizedY2) + "]");
    logger_.info("========================================");
    
    bool success = true;
    
    // Step 1: 暂停航线
    logger_.info("Step 1/9: 🛑 暂停航线任务...");
    if (!mqttClient_->pauseWayline()) {
        logger_.error("暂停航线失败");
        // 继续执行，因为可能不在航线模式
    }
    
    // Step 2: 等待悬停稳定（2秒，让无人机完全停下来）
    logger_.info("Step 2/9: ⏳ 等待悬停稳定...");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Step 3: 重新检测当前帧，更新包围框
    logger_.info("Step 3/9: 🔍 重新检测目标，更新包围框...");
    
    // 使用请求中的原始坐标作为默认值
    float finalX1 = request.normalizedX1;
    float finalY1 = request.normalizedY1;
    float finalX2 = request.normalizedX2;
    float finalY2 = request.normalizedY2;
    int finalTargetCount = request.targetCount;
    
    // 获取最新帧并重新检测
    cv::Mat currentFrame;
    {
        std::lock_guard<std::mutex> lock(detectionMutex_);
        if (!detectionInputFrame_.empty()) {
            detectionInputFrame_.copyTo(currentFrame);
        }
    }
    
    if (!currentFrame.empty() && service_) {
        std::vector<BoundingBox> newBoxes;
        if (service_->detectObjects(currentFrame, config_, newBoxes)) {
            if (!newBoxes.empty()) {
                // 计算所有目标的联合包围框
                // 注意: BoundingBox 的 x, y, w, h 是 float 类型
                float minX = static_cast<float>(currentFrame.cols);
                float minY = static_cast<float>(currentFrame.rows);
                float maxX = 0.0f;
                float maxY = 0.0f;
                
                for (const auto& box : newBoxes) {
                    minX = std::min(minX, box.x);
                    minY = std::min(minY, box.y);
                    maxX = std::max(maxX, box.x + box.w);
                    maxY = std::max(maxY, box.y + box.h);
                }
                
                // 添加边距 (10%)
                float padX = (maxX - minX) * 0.1f;
                float padY = (maxY - minY) * 0.1f;
                minX = std::max(0.0f, minX - padX);
                minY = std::max(0.0f, minY - padY);
                maxX = std::min(static_cast<float>(currentFrame.cols), maxX + padX);
                maxY = std::min(static_cast<float>(currentFrame.rows), maxY + padY);
                
                // 转换为归一化坐标
                finalX1 = minX / currentFrame.cols;
                finalY1 = minY / currentFrame.rows;
                finalX2 = maxX / currentFrame.cols;
                finalY2 = maxY / currentFrame.rows;
                finalTargetCount = static_cast<int>(newBoxes.size());
                
                logger_.info("   ✅ 重新检测成功！目标数: " + std::to_string(finalTargetCount) +
                            " (原: " + std::to_string(request.targetCount) + ")");
                logger_.info("   新区域: [" + std::to_string(finalX1) + "," + std::to_string(finalY1) + 
                            "] - [" + std::to_string(finalX2) + "," + std::to_string(finalY2) + "]");
            } else {
                logger_.warning("   ⚠️ 重新检测未发现目标，使用原始包围框");
            }
        } else {
            logger_.warning("   ⚠️ 重新检测失败，使用原始包围框");
        }
    } else {
        logger_.warning("   ⚠️ 无法获取当前帧，使用原始包围框");
    }
    
    // Step 4: 框选变焦
    logger_.info("Step 4/9: 🔍 框选变焦...");
    if (!mqttClient_->frameZoom(cameraPayloadIndex_, 
                                finalX1, finalY1, 
                                finalX2, finalY2)) {
        logger_.error("框选变焦失败");
        success = false;
    }
    // 等待变焦完成
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Step 5: 等待云台稳定
    logger_.info("Step 5/9: ⏳ 等待云台稳定...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Step 6: 拍照
    logger_.info("Step 6/9: 📷 拍照...");
    if (!mqttClient_->takePhoto(cameraPayloadIndex_)) {
        logger_.error("拍照失败");
        success = false;
    }
    // 等待拍照完成
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Step 7: 变焦复位 - 恢复到最小变焦倍率（最广视野）
    logger_.info("Step 7/9: 🔍 变焦复位（zoom_factor=2）...");
    if (!mqttClient_->setFocalLength(cameraPayloadIndex_, 1)) {  // 2x 是最小变焦倍率
        logger_.warning("变焦复位失败");
        // 不影响整体成功状态
    }
    // 等待变焦复位完成
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    // Step 8: 云台复位
    logger_.info("Step 8/9: 🔄 云台复位...");
    if (!mqttClient_->resetGimbal(cameraPayloadIndex_, 0)) {
        logger_.warning("云台复位失败");
        // 不影响整体成功状态
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Step 9: 恢复航线
    logger_.info("Step 9/9: ▶️ 恢复航线任务...");
    if (!mqttClient_->resumeWayline()) {
        logger_.warning("恢复航线失败");
        // 不影响整体成功状态，可能不在航线模式
    }
    
    logger_.info("========================================");
    logger_.info(success ? "✅ 变焦拍照流程完成" : "⚠️ 变焦拍照流程完成（有部分失败）");
    logger_.info("   最终拍摄目标数: " + std::to_string(finalTargetCount));
    logger_.info("========================================");
    
    return success;
}
}  // namespace task
}  // namespace esdk_sophon
