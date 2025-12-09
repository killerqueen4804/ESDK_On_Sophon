/**
 * @file MediaFileTask.h
 * @brief 媒体文件检测任务实现 (v3.0 - 工具类重构版本)
 * 
 * 负责处理航线巡检任务中的历史媒体文件（JPEG照片）。
 * 
 * 核心功能:
 * - 监听 DJI MediaFilesObserver 回调
 * - 读取图片文件（通过 MediaFilesReader）
 * - 解析 EXIF 元数据（使用 ImageProcessor）⭐
 * - 批量目标检测（YOLOv10）
 * - GPS 坐标计算（使用 GeoUtils）⭐
 * - 事件上报（使用 EventCache，自动缓存+重试）⭐
 * - 超时自动完成（60秒无新文件）⭐
 * 
 * 与 LiveStreamTask 的关键差异:
 * - 数据源：文件队列 vs 视频流
 * - 处理方式：逐张处理 vs 实时流
 * - 上报策略：立即上报 vs 间隔上报
 * - 任务结束：onTaskEnd() 仅标记 vs 立即停止 ⭐
 * - 超时控制：60秒无新文件自动完成 vs 无超时 ⭐
 * 
 * 线程模型:
 * - 主线程：任务控制（start/stop/pause）
 * - 工作线程：文件队列处理（execute）
 * - 监控线程：超时检测（monitorLoop）⭐
 * - DJI 回调线程：MediaFilesUpdateCallback（由 SDK 触发）
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-04
 * @updated 2025-11-21 (v3.0 重构 - 使用工具类)
 */

#ifndef ESDK_SOPHON_TASK_MEDIAFILE_TASK_H_
#define ESDK_SOPHON_TASK_MEDIAFILE_TASK_H_

#include "esdk_sophon/task/ITask.h"
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/core/Logger.h"

// ⭐ v3.0 新增：工具类依赖
#include "esdk_sophon/utils/ImageProcessor.h"   // 图片处理（EXIF解析、缩放、Base64）
#include "esdk_sophon/utils/GeoUtils.h"         // 地理信息（GPS坐标计算）
#include "esdk_sophon/core/EventCache.h"        // 事件缓存（自动缓存+重试）

// 注意: types::DetectionResult 已废弃，直接使用 task::BoundingBox

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <chrono>  // ⭐ v3.0 新增：超时控制
#include <opencv2/opencv.hpp>

// DJI SDK
#include "media_manager/media_manager.h"        // edge_sdk::MediaManager
#include "media_manager/media_file.h"           // edge_sdk::MediaFile
#include "media_manager/media_files_reader.h"   // edge_sdk::MediaFilesReader (完整定义)

namespace esdk_sophon {
namespace task {

/**
 * @brief 媒体文件检测任务
 * 
 * 工作流程:
 * @code
 * start()
 *   ↓
 * 1. 注册 MediaFilesObserver
 * 2. 创建工作线程 → execute()
 *   ↓
 * while (running) {
 *   等待文件队列有数据
 *   ↓
 *   从队列取出 MediaFile
 *   ↓
 *   读取图片文件（MediaFilesReader）
 *   ↓
 *   cv::imdecode() 解码为 cv::Mat
 *   ↓
 *   调用 TaskService::processFrame(frame, config, file.file_name) ⭐
 *   ↓
 *   更新统计信息
 * }
 *   ↓
 * stop()
 *   ↓
 * 注销 MediaFilesObserver
 * 清理资源
 * @endcode
 * 
 * 文件队列机制:
 * - DJI SDK 回调 → MediaFilesUpdateCallback() → 文件入队
 * - 工作线程 → 从队列取文件 → 处理 → 出队
 * - 使用 std::queue + std::mutex + std::condition_variable 实现生产者-消费者模式
 */
class MediaFileTask : public ITask {
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
     * config.taskId = "task_002";
     * config.type = TaskType::DETECTION_MEDIAFILE;
     * config.source = DataSource::MEDIAFILE;
     * 
     * auto task = std::make_shared<MediaFileTask>(config, service);
     * task->start();
     * @endcode
     */
    MediaFileTask(const TaskConfig& config,
                  std::shared_ptr<TaskService> service);
    
    /**
     * @brief 析构函数
     * 
     * 确保任务停止并释放资源。
     */
    ~MediaFileTask() override;
    
    // 禁止拷贝和赋值
    MediaFileTask(const MediaFileTask&) = delete;
    MediaFileTask& operator=(const MediaFileTask&) = delete;
    
    // ==================== ITask 接口实现 ====================
    
    /**
     * @brief 启动任务
     * 
     * 流程:
     * 1. 检查状态（必须是 IDLE）
     * 2. 注册 MediaFilesObserver
     * 3. 初始化 MediaFilesReader
     * 4. 创建工作线程
     * 5. 更新状态为 RUNNING
     * 
     * @return true 启动成功
     * @return false 启动失败
     */
    bool start() override;
    
    /**
     * @brief 停止任务
     * 
     * 流程:
     * 1. 设置停止标志
     * 2. 通知条件变量（唤醒工作线程）
     * 3. 等待工作线程结束
     * 4. 注销 MediaFilesObserver
     * 5. 更新状态为 COMPLETED
     * 
     * @return true 停止成功
     * @return false 停止失败
     */
    void stop() override;
    
    /**
     * @brief 暂停任务
     * 
     * 暂停文件处理，但保持 Observer 注册。
     * 新文件仍会入队，但不会被处理。
     * 
     * @return true 暂停成功
     * @return false 暂停失败
     */
    bool pause() override;
    
    /**
     * @brief 恢复任务
     * 
     * 恢复文件处理，继续处理队列中的文件。
     * 
     * @return true 恢复成功
     * @return false 恢复失败
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
     */
    TaskState getState() const override;
    
    /**
     * @brief 获取任务配置
     */
    const TaskConfig& getConfig() const override;
    
    /**
     * @brief 获取统计信息
     * 
     * 统计内容:
     * - processedFrames: 处理的图片总数
     * - detectedObjects: 检测到的目标总数
     * - reportedEvents: 上报的事件总数
     * - failedFrames: 处理失败的图片数
     */
    TaskStatistics getStatistics() const override;
    
    /**
     * @brief 设置状态变更回调
     */
    void setStateCallback(TaskCallback callback) override;
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(ErrorCallback callback) override;

    /**
     * @brief 处理"航线任务结束"通知 ⭐ v3.0 新增
     * 
     * 与 LiveStreamTask 的关键差异：
     * - LiveStreamTask::onTaskEnd(): 立即调用 stop()，推送结果
     * - MediaFileTask::onTaskEnd(): 仅标记 taskEnded_ = true，继续运行
     * 
     * 业务场景：
     * - 飞机返航后，平台发送 device_task_end
     * - 但照片可能还在 4G 网络中传输
     * - 需要继续等待文件传输完成（60秒超时）
     * 
     * 任务会在满足以下条件时自动完成：
     * 1. taskEnded_ == true（收到 device_task_end）
     * 2. 60秒无新文件（文件传输完成）
     * 
     * 示例流程:
     * @code
     * // 时间轴
     * T0: 任务启动，飞机起飞
     * T1: 飞机拍摄照片，开始传输
     * T2: 飞机返航，平台发送 device_task_end
     *     → onTaskEnd() 被调用
     *     → taskEnded_ = true
     *     → 任务继续运行，等待文件传输
     * T3: 文件陆续传输完成
     * T4: 60秒无新文件
     *     → shouldComplete() 返回 true
     *     → completeTask() 自动完成任务
     *     → 推送 device_task_analysis_result
     * T5: 平台发送 device_algorithm_disable
     *     → stop() 被调用
     * @endcode
     * 
     * @note 此方法不会停止任务，只是标记航线已结束
     */
    void onTaskEnd() override;

protected:
    /**
     * @brief 工作线程主循环
     * 
     * 流程:
     * @code
     * while (running_) {
     *     // 1. 等待队列有文件
     *     std::unique_lock lock(queueMutex_);
     *     queueCv_.wait(lock, [this] {
     *         return !fileQueue_.empty() || !running_;
     *     });
     *     
     *     // 2. 取出文件
     *     MediaFileInfo file = fileQueue_.front();
     *     fileQueue_.pop();
     *     lock.unlock();
     *     
     *     // 3. 读取文件
     *     std::vector<uint8_t> imageData;
     *     if (!readMediaFile(file, imageData)) {
     *         continue;
     *     }
     *     
     *     // 4. 解码图片
     *     cv::Mat frame = cv::imdecode(imageData, cv::IMREAD_COLOR);
     *     
     *     // 5. 调用推理（⭐ 传递原始文件名）
     *     service_->processFrame(frame, config_, file.file_name);
     *     
     *     // 6. 更新统计
     *     stats_.processedFrames++;
     * }
     * @endcode
     */
    void execute() override;
    
    /**
     * @brief 通知状态变更
     */
    void notifyStateChanged(TaskState newState) override;
    
    /**
     * @brief 通知错误
     */
    void notifyError(const std::string& error) override;

private:
    // ==================== 内部方法 ====================
    
    /**
     * @brief 注册 MediaFilesObserver
     * 
     * 向 DJI SDK 注册回调函数，监听新文件通知。
     * 
     * @return true 注册成功
     * @return false 注册失败
     */
    bool registerMediaFilesObserver();
    
    /**
     * @brief 注销 MediaFilesObserver
     */
    void unregisterMediaFilesObserver();
    
    /**
     * @brief MediaFiles 更新回调 (由 DJI SDK 调用)
     * 
     * 当有新文件时,DJI SDK 会调用此函数。
     * 
     * @param file 媒体文件信息
     * @return ErrorCode 处理结果
     */
    edge_sdk::ErrorCode onMediaFileUpdate(const edge_sdk::MediaFile& file);
    
    /**
     * @brief 读取媒体文件
     * 
     * 通过 MediaFilesReader 读取文件内容。
     * 
     * @param file 文件信息
     * @param[out] imageData 输出图片数据（JPEG 格式）
     * @return true 读取成功
     * @return false 读取失败
     */
    bool readMediaFile(const edge_sdk::MediaFile& file, std::vector<uint8_t>& imageData);
    
    /**
     * @brief 保存媒体文件到本地（调试用）
     * 
     * @param filename 文件名
     * @param data 文件数据
     * 
     * @note 可选功能，用于调试和验证
     */
    void dumpMediaFile(const std::string& filename, const std::vector<uint8_t>& data);
    
    /**
     * @brief 处理错误
     */
    void handleError(const std::string& error);

    // ==================== v3.0 新增方法 ====================

    /**
     * @brief 监控线程 ⭐
     * 
     * 定期检查是否满足自动完成条件：
     * - 条件1: taskEnded_ == true（收到 device_task_end）
     * - 条件2: 60秒无新文件（文件传输完成）
     * 
     * 实现:
     * @code
     * void MediaFileTask::monitorLoop() {
     *     while (running_) {
     *         std::this_thread::sleep_for(std::chrono::seconds(5));
     *         
     *         if (shouldComplete()) {
     *             completeTask();
     *             break;
     *         }
     *     }
     * }
     * @endcode
     * 
     * @note 每5秒检查一次，避免CPU占用过高
     */
    void monitorLoop();

    /**
     * @brief 判断任务是否应该完成 ⭐
     * 
     * @return true 满足完成条件（taskEnded_ == true && 60秒无新文件）
     * @return false 不满足完成条件
     * 
     * 线程安全: 使用 timerMutex_ 保护 lastFileTime_
     */
    bool shouldComplete();

    /**
     * @brief 完成任务 ⭐
     * 
     * 流程:
     * 1. 推送 device_task_analysis_result (result=0)
     * 2. 清理本地文件
     * 3. 恢复 DJI 设置（SetDroneNestAutoDelete(true)）
     * 4. 等待平台发送 device_algorithm_disable
     * 
     * @note 不调用 stop()，任务仍保持 RUNNING 状态
     */
    void completeTask();

    /**
     * @brief 推送"任务分析完成"消息 ⭐
     * 
     * MQTT 消息格式:
     * @code
     * Topic: thing/product/{sn}/events
     * Payload: {
     *   "tid": "task_001",
     *   "bid": "xxx-xxx-xxx",
     *   "timestamp": 1700000000,
     *   "data": {
     *     "result": 0  // 0=成功, 1=失败
     *   }
     * }
     * @endcode
     * 
     * @param success true=成功(result=0), false=失败(result=1)
     */
    void publishTaskAnalysisResult(bool success);

    /**
     * @brief 清理本地文件 ⭐
     * 
     * 删除任务期间下载的所有图片文件。
     * 
     * @note 从 downloadedFiles_ 列表中读取文件路径并删除
     */
    void cleanupFiles();

    /**
     * @brief 恢复 DJI 设置 ⭐
     * 
     * 恢复启动任务前修改的设置：
     * - SetDroneNestAutoDelete(true)  // 启动时设置为 false
     * 
     * @note 确保不影响其他任务或系统设置
     */
    void restoreDjiSettings();

    // ==================== Day 11: 检测和事件生成 ⭐ ====================

    /**
     * @brief 处理单个文件（检测 + EXIF 解析）⭐
     * 
     * Day 11 核心方法：整合 ImageProcessor 和 TaskService
     * 
     * 流程：
     * @code
     * 1. 使用 ImageProcessor::parseExif() 解析 EXIF
     *    → 获取经纬度（lat, lon）
     *    → 获取拍摄时间（timestamp）
     *    → 获取航向角（yaw）
     * 
     * 2. 调用 TaskService::processFrame() 进行目标检测
     *    → 返回 DetectionResult（包含所有检测框）
     * 
     * 3. 按算法类型分组检测结果
     *    → 调用 buildEvent() 构建事件
     * 
     * 4. 将事件推送到 EventCache
     *    → 使用 EventCache::addEvent() 异步推送
     * @endcode
     * 
     * 示例：
     * @code
     * edge_sdk::MediaFile file;
     * file.file_name = "DJI_20250101_120000.JPG";
     * 
     * // 读取文件
     * std::vector<uint8_t> imageData;
     * readMediaFile(file, imageData);
     * 
     * // 处理文件（包含EXIF解析、检测、事件上报）
     * processFile(file, imageData);
     * @endcode
     * 
     * @param file DJI 媒体文件信息
     * @param imageData 图片二进制数据（JPEG 格式）
     * @return true 处理成功
     * @return false 处理失败
     * 
     * @note 线程安全：可在工作线程中调用
     */
    bool processFile(const edge_sdk::MediaFile& file, 
                     const std::vector<uint8_t>& imageData);

    /**
     * @brief 构建事件消息（按算法类型分组）⭐
     * 
     * Day 11 核心方法：将检测结果转换为 MQTT 事件
     * 
     * 分组逻辑：
     * @code
     * DetectionResult 包含多个检测框：
     * [
     *   {class: "person", confidence: 0.85, bbox: [...]},
     *   {class: "person", confidence: 0.92, bbox: [...]},
     *   {class: "car", confidence: 0.78, bbox: [...]},
     *   {class: "dog", confidence: 0.65, bbox: [...]}
     * ]
     * 
     * 按算法类型分组后（假设 person 属于算法1，car/dog 属于算法2）：
     * {
     *   "algorithm_1": [person x2],
     *   "algorithm_2": [car x1, dog x1]
     * }
     * 
     * 每组生成一个独立的 MQTT 事件。
     * @endcode
     * 
     * MQTT 事件格式：
     * @code
     * {
     *   "tid": "task_001",
     *   "bid": "xxx-xxx-xxx",
     *   "need_reply": 0,
     *   "data": {
     *     "sn": "1581F5BKD2289S00AT0Y",
     *     "class": "person",            // 算法类型
     *     "latitude": 30.123456,        // 从EXIF解析
     *     "longitude": 120.654321,      // 从EXIF解析
     *     "high": 120.5,                // 从EXIF解析
     *     "device_model_key": "M30T",
     *     "lens_type": "zoom",
     *     "gimbal_yaw_degree": 45.3,    // 从EXIF解析
     *     "shoot_time": 1700000000,     // 从EXIF解析
     *     "picture_url": "data:image/jpeg;base64,/9j/4AAQ...",
     *     "result": [
     *       {
     *         "x": 100,
     *         "y": 200,
     *         "width": 50,
     *         "height": 80,
     *         "confidence": 0.85
     *       }
     *     ]
     *   }
     * }
     * @endcode
     * 
     * @param file DJI 媒体文件信息（用于获取文件名、时间戳等）
     * @param exifMetadata EXIF 元数据（经纬度、航向角、拍摄时间）
     * @param frame 图片帧（用于生成 Base64）
     * @param boundingBoxes 检测框列表
     * @return 事件列表（每个算法类型一个事件）
     * 
     * @note 使用 ImageProcessor::encodeBase64() 生成 picture_url
     */
    std::vector<nlohmann::json> buildEvent(
        const edge_sdk::MediaFile& file,
        const utils::ExifMetadata& exifMetadata,
        const cv::Mat& frame,
        const std::vector<BoundingBox>& boundingBoxes);

private:
    // ==================== 成员变量 ====================
    
    TaskConfig config_;                    ///< 任务配置
    std::shared_ptr<TaskService> service_; ///< TaskService 实例
    
    // ==================== 状态管理 ====================
    std::atomic<TaskState> state_;         ///< 当前状态
    std::atomic<bool> running_;            ///< 运行标志
    std::atomic<bool> paused_;             ///< 暂停标志
    std::atomic<bool> taskEnded_{false};   ///< ⭐ v3.0 新增：航线任务是否结束
    
    // ==================== 线程管理 ====================
    std::unique_ptr<std::thread> workerThread_;   ///< 工作线程（文件处理）
    std::unique_ptr<std::thread> monitorThread_;  ///< ⭐ v3.0 新增：监控线程（超时检测）
    std::mutex pauseMutex_;                ///< 暂停互斥锁
    std::condition_variable pauseCv_;      ///< 暂停条件变量
    
    // ==================== 文件队列（生产者-消费者模式）====================
    std::queue<edge_sdk::MediaFile> fileQueue_;  ///< 文件队列
    std::mutex queueMutex_;                ///< 队列互斥锁
    std::condition_variable queueCv_;      ///< 队列条件变量
    
    // ==================== 超时控制 ⭐ v3.0 新增 ====================
    std::chrono::steady_clock::time_point lastFileTime_;  ///< 最后收到文件的时间
    std::mutex timerMutex_;                               ///< 定时器互斥锁
    static constexpr int TIMEOUT_SECONDS = 60;            ///< 超时时间（秒）
    
    // ==================== 回调函数 ====================
    TaskCallback stateCallback_;           ///< 状态变更回调
    ErrorCallback errorCallback_;          ///< 错误回调
    
    // ==================== 统计信息 ====================
    TaskStatistics stats_;                 ///< 统计数据
    mutable std::mutex statsMutex_;        ///< 统计数据互斥锁
    
    // ==================== 文件存储 ⭐ v3.0 新增 ====================
    std::vector<std::string> downloadedFiles_;  ///< 已下载文件列表（用于清理）
    std::mutex filesMutex_;                     ///< 文件列表互斥锁
    
    // ==================== DJI SDK ====================
    edge_sdk::MediaManager* mediaManager_{nullptr};  ///< MediaManager 实例 (单例)
    std::shared_ptr<edge_sdk::MediaFilesReader> mediaReader_;  ///< 文件读取器
    
    // ==================== 日志 ====================
    core::Logger& logger_;                 ///< 日志实例
};

}  // namespace task
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_TASK_MEDIAFILE_TASK_H_
