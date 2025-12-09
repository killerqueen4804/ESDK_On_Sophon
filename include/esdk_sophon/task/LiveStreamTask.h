/**
 * @file LiveStreamTask.h
 * @brief 直播流检测任务实现
 * 
 * 负责从视频流（DJI 直播流）获取帧，进行实时目标检测，并上报结果。
 * 
 * 核心功能:
 * - 连接视频流源（RTSP/RTMP）
 * - 实时帧处理（30 FPS）
 * - 目标检测（YOLOv10）
 * - 事件上报（MQTT）
 * - RTMP 推流（可选）
 * 
 * 线程模型:
 * - 主线程：任务控制（start/stop/pause）
 * - 工作线程：视频流处理（execute）
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-04
 */

#ifndef ESDK_SOPHON_TASK_LIVESTREAM_TASK_H_
#define ESDK_SOPHON_TASK_LIVESTREAM_TASK_H_

#include "esdk_sophon/task/ITask.h"
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/core/Logger.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <opencv2/opencv.hpp>

// DJI SDK
#include "liveview.h"  // edge_sdk::Liveview

// 前向声明追踪器和策略
namespace esdk_sophon {
namespace core {
    class Config;
}
namespace vision {
namespace tracker {
    class ByteTracker;
    struct ByteTrackerConfig;
}
namespace strategy {
    class SmartZoomCaptureStrategy;
    struct SmartZoomCaptureConfig;
    struct ZoomCaptureRequest;
}
}
namespace mqtt {
    class MqttClient;
}
}

namespace esdk_sophon {

// 前向声明 rtmp 命名空间和类（避免暴露 FFmpeg 头文件）
namespace rtmp {
    class RtmpStreamer;
}

// 前向声明 video 命名空间和类（避免暴露 FFmpeg 头文件）
namespace video {
    class H264Decoder;
}

namespace task {

/**
 * @brief 直播流检测任务
 * 
 * 工作流程:
 * @code
 * start()
 *   ↓
 * 创建工作线程 → execute()
 *   ↓
 * while (running) {
 *   1. 从视频流获取帧
 *   2. 调用 TaskService::processFrame()
 *   3. 可选：RTMP 推流
 *   4. 更新统计信息
 * }
 *   ↓
 * stop()
 *   ↓
 * 清理资源
 * @endcode
 * 
 * 状态转换:
 * @code
 * IDLE → start() → RUNNING
 *      ↓ pause()
 *      PAUSED
 *      ↓ resume()
 *      RUNNING
 *      ↓ stop()
 *      COMPLETED
 * @endcode
 */
class LiveStreamTask : public ITask {
public:
    /**
     * @brief 构造函数
     * 
     * @param config 任务配置
     * @param service TaskService 实例（依赖注入）
     * 
     * 示例:
     * @code
     * TaskConfig config;
     * config.taskId = "task_001";
     * config.type = TaskType::DETECTION_LIVESTREAM;
     * config.source = DataSource::LIVESTREAM;
     * 
     * auto task = std::make_shared<LiveStreamTask>(config, service);
     * task->start();
     * @endcode
     */
    LiveStreamTask(const TaskConfig& config, 
                   std::shared_ptr<TaskService> service);
    
    /**
     * @brief 析构函数
     * 
     * 确保任务停止并释放资源。
     * 遵循 RAII 原则。
     */
    ~LiveStreamTask() override;
    
    // 禁止拷贝和赋值
    LiveStreamTask(const LiveStreamTask&) = delete;
    LiveStreamTask& operator=(const LiveStreamTask&) = delete;
    
    // ==================== ITask 接口实现 ====================
    
    /**
     * @brief 启动任务
     * 
     * 创建工作线程，开始处理视频流。
     * 
     * 流程:
     * 1. 检查状态（必须是 IDLE）
     * 2. 初始化视频流连接
     * 3. 创建工作线程
     * 4. 更新状态为 RUNNING
     * 5. 触发状态回调
     * 
     * @return true 启动成功
     * @return false 启动失败（状态错误、连接失败等）
     * 
     * @note 线程安全：由 TaskManager 加锁保护
     */
    bool start() override;
    
    /**
     * @brief 停止任务
     * 
     * 停止工作线程，清理资源。
     * 
     * 流程:
     * 1. 检查状态（必须是 RUNNING 或 PAUSED）
     * 2. 设置停止标志
     * 3. 通知条件变量（唤醒暂停的线程）
     * 4. 等待工作线程结束（join）
     * 5. 释放视频流资源
     * 6. 更新状态为 COMPLETED
     * 7. 触发状态回调
     * 
     * @return true 停止成功
     * @return false 停止失败
     * 
     * @note 阻塞调用：会等待工作线程结束
     */
    void stop() override;
    
    /**
     * @brief 暂停任务
     * 
     * 暂停视频流处理，但保持连接。
     * 
     * @return true 暂停成功
     * @return false 暂停失败（状态错误）
     * 
     * @note 实现方式：使用条件变量阻塞工作线程
     */
    bool pause() override;
    
    /**
     * @brief 恢复任务
     * 
     * 从暂停状态恢复，继续处理视频流。
     * 
     * @return true 恢复成功
     * @return false 恢复失败（状态错误）
     * 
     * @note 实现方式：通知条件变量唤醒工作线程
     */
    bool resume() override;
    
    /**
     * @brief 检查任务是否正在运行 (便利方法)
     * 
     * @return true 任务正在运行
     * @return false 任务未运行
     * 
     * @note 便利方法，等价于 getState() == TaskState::RUNNING
     */
    bool isRunning() const override;
    
    /**
     * @brief 获取当前状态
     * 
     * @return TaskState 当前任务状态
     * 
     * @note 线程安全：使用 atomic 或 mutex 保护
     */
    TaskState getState() const override;
    
    /**
     * @brief 获取任务配置
     * 
     * @return const TaskConfig& 任务配置引用
     */
    const TaskConfig& getConfig() const override;
    
    /**
     * @brief 获取统计信息
     * 
     * @return TaskStatistics 任务统计数据
     * 
     * 统计内容:
     * - processedFrames: 处理的总帧数
     * - detectedObjects: 检测到的目标总数
     * - reportedEvents: 上报的事件总数
     * - failedFrames: 处理失败的帧数
     * - startTime: 任务启动时间
     * - endTime: 任务结束时间
     */
    TaskStatistics getStatistics() const override;
    
    /**
     * @brief 设置状态变更回调
     * 
     * @param callback 回调函数
     * 
     * 示例:
     * @code
     * task->setStateCallback([](const std::string& taskId, TaskState state) {
     *     std::cout << "Task " << taskId << " changed to " 
     *               << taskStateToString(state) << std::endl;
     * });
     * @endcode
     */
    void setStateCallback(TaskCallback callback) override;
    
    /**
     * @brief 设置错误回调
     * 
     * @param callback 回调函数
     * 
     * 示例:
     * @code
     * task->setErrorCallback([](const std::string& taskId, const std::string& error) {
     *     std::cerr << "Task " << taskId << " error: " << error << std::endl;
     * });
     * @endcode
     */
    void setErrorCallback(ErrorCallback callback) override;
    
    // ==================== 任务特定事件处理 ====================
    
    /**
     * @brief 处理"航线任务结束"通知（device_task_end）⭐
     * 
     * 📖 视频流任务的特殊逻辑：
     * 当收到 device_task_end 消息时，表示航线任务已结束（飞机已返航），
     * 视频流任务应该**立即停止**并推送完成消息。
     * 
     * 处理流程:
     * 1. 立即停止任务（调用 stop()）
     * 2. 推送 device_task_analysis_result (任务分析完成)
     * 3. 等待平台发送 device_algorithm_disable
     * 
     * 状态转换: RUNNING → COMPLETED
     * 
     * @note 此方法会在 MQTT 线程中被调用
     * @note stop() 是阻塞调用，会等待工作线程结束
     */
    void onTaskEnd() override;

    // ==================== 公共回调 (供 Proxy 使用) ====================

    /**
     * @brief H264 数据回调 (由 DJI Liveview SDK 调用)
     * 
     * @param buf H264 数据缓冲区
     * @param len 数据长度
     * @return ErrorCode 处理结果
     */
    edge_sdk::ErrorCode onH264Data(const uint8_t* buf, uint32_t len);

    /**
     * @brief Liveview 状态回调
     * 
     * SDK 内部调用，通知 Liveview 就绪状态变化。
     * 当设备就绪时，自动启动 H264 流。
     * 
     * @param status Liveview 状态
     */
    void onLiveviewStatusChanged(const edge_sdk::Liveview::LiveviewStatus& status);

protected:
    /**
     * @brief 工作线程主循环
     * 
     * 核心执行逻辑，在独立线程中运行。
     * 
     * 流程:
     * @code
     * while (running_) {
     *     // 1. 检查暂停状态
     *     if (paused_) {
     *         std::unique_lock lock(pauseMutex_);
     *         pauseCv_.wait(lock, [this] { 
     *             return !paused_ || !running_; 
     *         });
     *     }
     *     
     *     // 2. 获取视频帧
     *     cv::Mat frame;
     *     if (!getNextFrame(frame)) {
     *         handleError("获取帧失败");
     *         continue;
     *     }
     *     
     *     // 3. 调用推理
     *     if (service_->processFrame(frame, config_)) {
     *         stats_.reportedEvents++;
     *     }
     *     
     *     // 4. RTMP 推流（自动在 onH264Data 回调中推送）
     *     // H.264 数据直接推送，无需显式调用
     *     
     *     // 5. 更新统计
     *     stats_.processedFrames++;
     * }
     * @endcode
     */
    void execute() override;
    
    /**
     * @brief 通知状态变更
     * 
     * @param newState 新状态
     */
    void notifyStateChanged(TaskState newState) override;
    
    /**
     * @brief 通知错误
     * 
     * @param error 错误信息
     */
    void notifyError(const std::string& error) override;

private:
    // ==================== 内部方法 ====================
    
    /**
     * @brief 初始化视频流连接
     * 
     * 连接到 DJI Liveview 视频流源。
     * 
     * @return true 连接成功
     * @return false 连接失败
     */
    bool initVideoStream();
    
    /**
     * @brief 反初始化视频流
     */
    void deinitVideoStream();
    
    /**
     * @brief 获取下一帧 H264 数据
     * 
     * 从 H264 队列读取一帧数据。
     * 
     * @param[out] h264Data 输出 H264 数据
     * @return true 读取成功
     * @return false 读取失败（流结束、超时等）
     */
    bool getNextH264Data(std::vector<uint8_t>& h264Data);
    
    /**
     * @brief 解码 H264 数据为 cv::Mat
     * 
     * 使用 OpenCV/FFmpeg 解码 H264 为图像帧。
     * 
     * @param h264Data H264 数据
     * @param[out] frame 输出图像帧
     * @return true 解码成功
     * @return false 解码失败
     * 
     * @note 临时方案：用于本地检测。最优方案是直接推流 H264
     */
    bool decodeH264ToMat(const std::vector<uint8_t>& h264Data, cv::Mat& frame);
    
    /**
     * @brief 获取下一帧 (便利方法)
     * 
     * 内部调用 getNextH264Data() + decodeH264ToMat()
     * 
     * @param[out] frame 输出帧
     * @return true 读取成功
     * @return false 读取失败
     */
    bool getNextFrame(cv::Mat& frame);
    
    /**
     * @brief 处理错误
     * 
     * 记录日志并触发错误回调。
     * 
     * @param error 错误信息
     */
    void handleError(const std::string& error);
    
    /**
     * @brief 更新统计信息
     */
    void updateStatistics();

private:
    // ==================== 成员变量 ====================
    
    TaskConfig config_;                    ///< 任务配置
    std::shared_ptr<TaskService> service_; ///< TaskService 实例
    
    // 状态管理
    std::atomic<TaskState> state_;         ///< 当前状态（原子变量）
    std::atomic<bool> running_;            ///< 运行标志
    std::atomic<bool> paused_;             ///< 暂停标志
    
    // 线程管理
    std::unique_ptr<std::thread> workerThread_;  ///< 工作线程
    std::mutex pauseMutex_;                ///< 暂停互斥锁
    std::condition_variable pauseCv_;      ///< 暂停条件变量
    
    // 回调函数
    TaskCallback stateCallback_;           ///< 状态变更回调
    ErrorCallback errorCallback_;          ///< 错误回调
    
    // 统计信息
    TaskStatistics stats_;                 ///< 统计数据
    mutable std::mutex statsMutex_;        ///< 统计数据互斥锁
    
    // ==================== DJI Liveview ====================
    
    std::shared_ptr<edge_sdk::Liveview> liveview_;  ///< DJI Liveview 实例
    std::atomic<int> liveviewStatus_{0};            ///< Liveview 状态（0=未就绪，非0=已就绪）
    std::atomic<bool> streamStarted_{false};        ///< H264 流是否已启动
    
    // H264 数据队列 (生产者-消费者模式)
    std::queue<std::vector<uint8_t>> h264Queue_;    ///< H264 数据队列
    std::mutex h264Mutex_;                          ///< H264 队列互斥锁
    std::condition_variable h264Condition_;         ///< H264 队列条件变量
    size_t maxH264QueueSize_{10};                   ///< 最大队列长度 (防止内存溢出)
    
    // ==================== RTMP 推流 ====================
    
    std::shared_ptr<rtmp::RtmpStreamer> rtmpStreamer_;  ///< RTMP 推流器
    bool rtmpEnabled_{false};                           ///< RTMP 推流是否启用
    
    // ==================== H264 解码器 (Day 7 新增) ====================
    
    std::unique_ptr<video::H264Decoder> h264Decoder_;   ///< FFmpeg H.264 解码器
    bool decoderEnabled_{true};                         ///< 解码器是否启用
    
    // ==================== 性能优化成员 (Day 8) ====================
    std::vector<BoundingBox> lastBoxes_;    ///< 上一帧检测结果 (用于视觉暂留)
    int frameCounter_{0};                   ///< 帧计数器 (用于跳帧检测)
    
    // ==================== 异步检测 (Day 9) ====================
    std::unique_ptr<std::thread> detectionThread_;  ///< 独立检测线程
    std::mutex detectionMutex_;                     ///< 检测输入互斥锁
    std::condition_variable detectionCv_;           ///< 检测条件变量
    cv::Mat detectionInputFrame_;                   ///< 待检测的帧 (输入)
    std::atomic<bool> newFrameAvailable_{false};    ///< 是否有新帧待检测
    
    std::mutex resultMutex_;                        ///< 检测结果互斥锁
    std::vector<BoundingBox> currentDetections_;    ///< 最新检测结果 (输出)
    
    /**
     * @brief 异步检测循环
     * 
     * 在独立线程中运行，从 detectionInputFrame_ 获取图像，
     * 调用推理服务，并将结果更新到 currentDetections_。
     */
    void detectionLoop();
    
    // ==================== 智能变焦拍照功能 (Phase 4 新增) ====================
    
    std::unique_ptr<vision::tracker::ByteTracker> byteTracker_;           ///< ByteTrack 追踪器
    std::unique_ptr<vision::strategy::SmartZoomCaptureStrategy> zoomCaptureStrategy_;  ///< 变焦拍照策略
    
    bool zoomCaptureEnabled_{false};            ///< 是否启用变焦拍照功能
    bool trackerEnabled_{false};                ///< 是否启用目标追踪
    std::string cameraPayloadIndex_;            ///< 相机 payload_index (如 "81-0-0")
    
    // 变焦拍照线程
    std::unique_ptr<std::thread> zoomCaptureThread_;  ///< 变焦拍照执行线程
    std::mutex zoomCaptureMutex_;                     ///< 变焦拍照互斥锁
    std::condition_variable zoomCaptureCv_;           ///< 变焦拍照条件变量
    std::atomic<bool> zoomCaptureRequested_{false};   ///< 是否有待处理的变焦拍照请求
    vision::strategy::ZoomCaptureRequest* pendingRequest_{nullptr};  ///< 待处理的请求指针
    
    mqtt::MqttClient* mqttClient_{nullptr};           ///< MQTT 客户端引用（用于发送控制命令）
    
    /**
     * @brief 变焦拍照执行循环
     * 
     * 在独立线程中运行，等待变焦拍照请求并执行：
     * 1. 暂停航线
     * 2. 框选变焦
     * 3. 拍照
     * 4. 云台复位
     * 5. 恢复航线
     */
    void zoomCaptureLoop();
    
    /**
     * @brief 执行变焦拍照流程
     * 
     * @param request 变焦拍照请求
     * @return true 执行成功
     * @return false 执行失败
     */
    bool executeZoomCapture(const vision::strategy::ZoomCaptureRequest& request);
    
    /**
     * @brief 初始化智能变焦拍照功能
     * 
     * 从配置文件读取参数，创建追踪器和策略实例。
     * 
     * @param config 全局配置对象
     */
    void initZoomCaptureFeatures(core::Config& config);

    // ==================== 旧的临时实现 (将被移除) ====================
    // cv::VideoCapture videoCapture_;  // 已移除
    
    // 日志
    core::Logger& logger_;                 ///< 日志实例
};

}  // namespace task
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_TASK_LIVESTREAM_TASK_H_
