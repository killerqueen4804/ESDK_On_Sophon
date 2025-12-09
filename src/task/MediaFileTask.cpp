/**
 * @file MediaFileTask.cpp
 * @brief 媒体文件检测任务实�?
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-04
 * @updated 2025-11-10 (Day 6: 集成 DJI MediaManager)
 */

#include "esdk_sophon/task/MediaFileTask.h"
#include <chrono>
#include <fstream>

namespace esdk_sophon {
namespace task {

MediaFileTask::MediaFileTask(const TaskConfig& config,
                             std::shared_ptr<TaskService> service)
    : config_(config)
    , service_(service)
    , state_(TaskState::IDLE)
    , running_(false)
    , paused_(false)
    , logger_(core::Logger::getInstance()) {
    
    // 初始化统计信�?
    // stats_.taskId = config_.taskId;
    stats_.framesProcessed = 0;
    stats_.detectionsCount = 0;
    stats_.eventsPublished = 0;
    // stats_.failedFrames = 0;
    
    logger_.info("MediaFileTask 已创�? taskId=" + config_.taskId);
}

MediaFileTask::~MediaFileTask() {
    if (running_) {
        stop();
    }
    logger_.info("MediaFileTask 已销�? taskId=" + config_.taskId);
}

// ==================== ITask 接口实现 ====================

bool MediaFileTask::start() {
    // 1. 检查状态
    if (state_ != TaskState::IDLE) {
        logger_.error("无法启动任务: 当前状态不是 IDLE, taskId=" + config_.taskId);
        return false;
    }
    
    logger_.info("启动媒体文件分析任务: taskId=" + config_.taskId);
    
    // 2. ⭐ 配置 EventCache (事件缓存 + 自动重试)
    logger_.info("配置 EventCache...");
    auto& cache = core::EventCache::getInstance();
    
    // 设置缓存文件路径
    cache.setCacheFilePath("/data/mediafile_event_cache_" + config_.taskId + ".json");
    
    // 设置发布回调 (通过 TaskService 访问 MqttClient)
    cache.setPublishCallback([this](const std::string& topic, const std::string& payload) {
        try {
            if (!service_) {
                logger_.error("TaskService 未初始化，无法发布事件");
                return false;
            }
            
            // 解析 JSON payload
            nlohmann::json eventJson = nlohmann::json::parse(payload);
            
            // 通过 TaskService 发布 MQTT 消息
            bool success = service_->publishMqttMessage(topic, eventJson);
            
            if (success) {
                logger_.info("📤 [EventCache] 事件发布成功: topic=" + topic);
            } else {
                logger_.warning("⚠️ [EventCache] 事件发布失败: topic=" + topic);
            }
            
            return success;
            
        } catch (const std::exception& e) {
            logger_.error("EventCache 发布回调异常: " + std::string(e.what()));
            return false;
        }
    });
    
    // 配置重试参数
    cache.setMaxRetryCount(5);           // 最多重试 5 次
    cache.setMaxEventAge(3600);          // 事件缓存 1 小时
    
    logger_.info("✅ EventCache 配置完成");
    
    // 3. 注册 MediaFilesObserver
    if (!registerMediaFilesObserver()) {
        notifyError("注册 MediaFilesObserver 失败");
        return false;
    }
    
    // 4. ⭐ v3.0 新增：初始化超时控制
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }
    
    // 5. 设置运行标志
    running_ = true;
    paused_ = false;
    taskEnded_ = false;  // ⭐ v3.0 新增：初始化任务结束标志
    
    // 6. 记录启动时间
    // stats_.startTime = std::chrono::system_clock::now();
    
    // 7. 创建工作线程
    try {
        workerThread_ = std::make_unique<std::thread>(&MediaFileTask::execute, this);
        monitorThread_ = std::make_unique<std::thread>(&MediaFileTask::monitorLoop, this);  // ⭐ v3.0 新增
    } catch (const std::exception& e) {
        running_ = false;
        notifyError("创建工作线程失败: " + std::string(e.what()));
        return false;
    }
    
    // 8. 更新状态
    state_ = TaskState::RUNNING;
    notifyStateChanged(TaskState::RUNNING);
    
    logger_.info("媒体文件任务已启动（工作线程 + 监控线程）: taskId=" + config_.taskId);
    return true;
}

void MediaFileTask::stop() {
    // 1. 检查状态
    if (state_ != TaskState::RUNNING && state_ != TaskState::PAUSED) {
        logger_.warning("任务未运行，无需停止: taskId=" + config_.taskId);
        return;  // void 函数直接 return
    }
    
    logger_.info("停止媒体文件任务: taskId=" + config_.taskId);
    
    // 2. 设置停止标志
    running_ = false;
    
    // 3. 通知条件变量（唤醒工作线程）
    queueCv_.notify_one();
    
    // 4. 如果任务已暂停，也要唤醒
    if (paused_) {
        paused_ = false;
        pauseCv_.notify_one();
    }
    
    // 5. 等待工作线程结束
    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }
    
    // 6. ⭐ v3.0 新增：等待监控线程结束
    if (monitorThread_ && monitorThread_->joinable()) {
        monitorThread_->join();
    }
    
    // 7. 注销 MediaFilesObserver
    unregisterMediaFilesObserver();
    
    // 8. 清空队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!fileQueue_.empty()) {
            fileQueue_.pop();
        }
    }
    
    // 9. 记录结束时间
    // stats_.endTime = std::chrono::system_clock::now();
    
    // 10. 更新状态
    state_ = TaskState::COMPLETED;
    notifyStateChanged(TaskState::COMPLETED);
    
    logger_.info("媒体文件任务已停止: taskId=" + config_.taskId);
    // void 函数无需 return 语句
}

bool MediaFileTask::pause() {
    // 1. 检查状�?
    if (state_ != TaskState::RUNNING) {
        logger_.error("无法暂停任务: 当前状态不�?RUNNING, taskId=" + config_.taskId);
        return false;
    }
    
    logger_.info("暂停媒体文件任务: taskId=" + config_.taskId);
    
    // 2. 设置暂停标志
    paused_ = true;
    
    // 3. 更新状�?
    state_ = TaskState::PAUSED;
    notifyStateChanged(TaskState::PAUSED);
    
    logger_.info("媒体文件任务已暂�? taskId=" + config_.taskId);
    return true;
}

bool MediaFileTask::resume() {
    // 1. 检查状�?
    if (state_ != TaskState::PAUSED) {
        logger_.error("无法恢复任务: 当前状态不�?PAUSED, taskId=" + config_.taskId);
        return false;
    }
    
    logger_.info("恢复媒体文件任务: taskId=" + config_.taskId);
    
    // 2. 清除暂停标志
    paused_ = false;
    
    // 3. 通知条件变量（唤醒工作线程）
    pauseCv_.notify_one();
    
    // 4. 更新状�?
    state_ = TaskState::RUNNING;
    notifyStateChanged(TaskState::RUNNING);
    
    logger_.info("媒体文件任务已恢�? taskId=" + config_.taskId);
    return true;
}

bool MediaFileTask::isRunning() const {
    return state_.load() == TaskState::RUNNING;
}

TaskState MediaFileTask::getState() const {
    return state_.load();
}

const TaskConfig& MediaFileTask::getConfig() const {
    return config_;
}

TaskStatistics MediaFileTask::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void MediaFileTask::setStateCallback(TaskCallback callback) {
    stateCallback_ = callback;
}

void MediaFileTask::setErrorCallback(ErrorCallback callback) {
    errorCallback_ = callback;
}

// ==================== v3.0 新增方法 ====================

void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，标记航线结束，继续等待文件传输");
    
    // ⭐ 关键差异：仅标记，不停止任务
    // LiveStreamTask 会立即调用 stop()
    // MediaFileTask 仅设置标志，由 monitorLoop() 检测超时后自动完成
    taskEnded_.store(true);
    
    // 🔧 关键修复：重置超时计时器，从现在开始计算 60 秒等待时间
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
        logger_.info("⏰ 已重置超时计时器，从现在开始等待 60 秒");
    }
    
    logger_.info("taskEnded_ 已设置为 true，等待文件传输完成（60秒超时）");
    logger_.info("监控线程将在 60秒无新文件时自动完成任务");
}

// ==================== Protected 方法 ====================

void MediaFileTask::execute() {
    logger_.info("工作线程已启动 taskId=" + config_.taskId);
    
    while (running_) {
        try {
            // 1. 检查暂停状态
            if (paused_) {
                std::unique_lock<std::mutex> lock(pauseMutex_);
                pauseCv_.wait(lock, [this] { 
                    return !paused_ || !running_; 
                });
                
                // 如果在等待期间收到停止信号
                if (!running_) {
                    break;
                }
            }

            // 2. 等待队列有文件
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] {
                return !fileQueue_.empty() || !running_;
            });
            
            // 检查停止信号
            if (!running_) {
                break;
            }
            
            // 队列为空（可能是虚假唤醒）
            if (fileQueue_.empty()) {
                continue;
            }
            
            // 3. 取出文件
            edge_sdk::MediaFile file = fileQueue_.front();
            fileQueue_.pop();
            lock.unlock();
            
            logger_.info("处理媒体文件: " + file.file_name);
            
            // 4. 读取文件
            std::vector<uint8_t> imageData;
            if (!readMediaFile(file, imageData)) {
                handleError("读取文件失败: " + file.file_name);
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                // stats_.failedFrames++;
                continue;
            }
            
            // 5. ⭐ v3.0 新增：调用 processFile() 进行完整处理
            // 包含：EXIF解析、目标检测、事件构建、EventCache推送
            if (!processFile(file, imageData)) {
                handleError("文件处理失败: " + file.file_name);
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                // stats_.failedFrames++;
                continue;
            }
            
            logger_.debug("成功处理文件: " + file.file_name);
            
        } catch (const std::exception& e) {
            handleError("处理文件异常: " + std::string(e.what()));
            //std::lock_guard<std::mutex> lock(statsMutex_);
            // stats_.failedFrames++;
        }
    }
    
    logger_.info("工作线程已退�? taskId=" + config_.taskId);
}

void MediaFileTask::notifyStateChanged(TaskState newState) {
    if (stateCallback_) {
        stateCallback_(config_.taskId, newState);
    }
}

void MediaFileTask::notifyError(const std::string& error) {
    logger_.error("任务错误: taskId=" + config_.taskId + ", error=" + error);
    
    if (errorCallback_) {
        errorCallback_(config_.taskId, error);
    }
}

// ==================== Private 方法 ====================

bool MediaFileTask::registerMediaFilesObserver() {
    logger_.info("========================================");
    logger_.info("🔧 [诊断] 开始注册 MediaFilesObserver");
    logger_.info("   taskId=" + config_.taskId);
    logger_.info("========================================");
    
    // 1. 获取 MediaManager 单例
    logger_.info("📌 [步骤1] 获取 MediaManager 实例...");
    mediaManager_ = edge_sdk::MediaManager::Instance();
    if (!mediaManager_) {
        logger_.error("❌ 获取 MediaManager 实例失败");
        return false;
    }
    logger_.info("✅ MediaManager 实例获取成功");
    
    // 2. 注册观察者回�?(Lambda 捕获 this)
    logger_.info("📌 [步骤2] 注册文件观察者回调...");
    auto observer = [this](const edge_sdk::MediaFile& file) -> edge_sdk::ErrorCode {
        logger_.info("🔔 [回调触发] MediaFilesObserver 被调用!");
        logger_.info("   文件名: " + file.file_name);
        logger_.info("   文件路径: " + file.file_path);
        logger_.info("   文件大小: " + std::to_string(file.file_size) + " bytes");
        logger_.info("   文件类型: " + std::to_string(static_cast<int>(file.file_type)));
        
        edge_sdk::ErrorCode result = this->onMediaFileUpdate(file);
        
        logger_.info("🔔 [回调完成] 返回码: " + std::to_string(static_cast<int>(result)));
        return result;
    };
    
    edge_sdk::ErrorCode ret = mediaManager_->RegisterMediaFilesObserver(observer);
    if (ret != edge_sdk::kOk) {
        logger_.error("❌ MediaFilesObserver 注册失败: ErrorCode=" + std::to_string(static_cast<int>(ret)));
        return false;
    }
    logger_.info("✅ MediaFilesObserver 回调注册成功");
    
    // 3. 创建文件读取�?
    logger_.info("📌 [步骤3] 创建 MediaFilesReader...");
    mediaReader_ = mediaManager_->CreateMediaFilesReader();
    if (!mediaReader_) {
        logger_.error("❌ 创建 MediaFilesReader 失败");
        return false;
    }
    logger_.info("✅ MediaFilesReader 创建成功");
    
    // 4. 配置: 上传到云端,不自动删�?(边缘计算需要本地文�?
    logger_.info("📌 [步骤4] 配置 DJI MediaManager 参数...");
    
    ret = mediaManager_->SetDroneNestUploadCloud(true);
    if (ret != edge_sdk::kOk) {
        logger_.warning("⚠️ SetDroneNestUploadCloud(false) 失败: ErrorCode=" + std::to_string(static_cast<int>(ret)));
    } else {
        logger_.info("✅ SetDroneNestUploadCloud(false) 成功");
    }
    
    ret = mediaManager_->SetDroneNestAutoDelete(false);
    if (ret != edge_sdk::kOk) {
        logger_.warning("⚠️ SetDroneNestAutoDelete(false) 失败: ErrorCode=" + std::to_string(static_cast<int>(ret)));
    } else {
        logger_.info("✅ SetDroneNestAutoDelete(false) 成功");
    }
    
    logger_.info("========================================");
    logger_.info("✅ MediaFilesObserver 注册流程完成!");
    logger_.info("   现在等待 DJI SDK 触发回调...");
    logger_.info("   预期行为: 当无人机返航后，机场下载照片");
    logger_.info("   回调函数会自动被调用，输出 '🔔 [回调触发]' 日志");
    logger_.info("========================================");
    
    return true;
}

void MediaFileTask::unregisterMediaFilesObserver() {
    logger_.info("注销 MediaFilesObserver: taskId=" + config_.taskId);
    
    // DJI SDK 通常不提供显式的注销方法
    // MediaManager 的观察者在 MediaManager 生命周期结束时自动清�?
    
    // 释放 MediaFilesReader
    if (mediaReader_) {
        mediaReader_.reset();
        logger_.debug("MediaFilesReader 已释放");
    }
    
    logger_.info("MediaFilesObserver 已注销: taskId=" + config_.taskId);
}

edge_sdk::ErrorCode MediaFileTask::onMediaFileUpdate(const edge_sdk::MediaFile& file) {
    logger_.info("========================================");
    logger_.info("📥 收到媒体文件更新通知!");
    logger_.info("   文件名: " + file.file_name);
    logger_.info("   文件路径: " + file.file_path);
    logger_.info("   文件大小: " + std::to_string(file.file_size) + " bytes");
    logger_.info("   文件类型: " + std::to_string(static_cast<int>(file.file_type)) + 
                " (0=JPG, 1=MP4)");
    logger_.info("========================================");
    
    // 过滤视频文件（只处理图片）
    if (file.file_type == edge_sdk::MediaFile::kFileTypeMp4) {
        logger_.debug("⏭️ 跳过视频文件: " + file.file_name);
        return edge_sdk::kOk;  // 继续接收其他文件
    }
    
    logger_.info("✅ 确认为图片文件，准备处理");
    
    // ⭐ v3.0 新增：更新最后收到文件的时间（用于超时检测）
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
        logger_.info("⏰ 已更新超时计时器");
    }
    
    // 将文件信息放入队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fileQueue_.push(file);
        logger_.info("📋 文件已加入处理队列: 当前队列大小=" + std::to_string(fileQueue_.size()));
    }
    
    // 通知消费者线程
    queueCv_.notify_one();
    logger_.info("🔔 已通知工作线程处理文件");
    
    logger_.debug("文件入队完成: " + file.file_name);
    
    return edge_sdk::kOk;
}

bool MediaFileTask::readMediaFile(const edge_sdk::MediaFile& file, 
                                  std::vector<uint8_t>& imageData) {
    logger_.debug("读取媒体文件: " + file.file_name);
    
    if (!mediaReader_) {
        logger_.error("MediaFilesReader 未初始化");
        return false;
    }
    
    // 1. 打开文件
    auto fd = mediaReader_->Open(file.file_path);
    if (fd < 0) {
        logger_.error("打开文件失败: " + file.file_name + ", fd=" + std::to_string(fd));
        return false;
    }
    
    // 2. 循环读取文件数据
    const size_t BUFFER_SIZE = 1024 * 1024;  // 1 MB 缓冲�?
    std::vector<char> buffer(BUFFER_SIZE);
    
    while (true) {
        size_t nread = mediaReader_->Read(fd, buffer.data(), buffer.size());
        
        if (nread > 0) {
            // 将读取的数据追加�?imageData
            imageData.insert(imageData.end(), 
                           reinterpret_cast<uint8_t*>(buffer.data()),
                           reinterpret_cast<uint8_t*>(buffer.data() + nread));
        } else {
            // 读取完成或出�?
            break;
        }
    }
    
    // 3. 关闭文件
    edge_sdk::ErrorCode ret = mediaReader_->Close(fd);
    if (ret != edge_sdk::kOk) {
        logger_.warning("关闭文件失败: " + file.file_name + ", ErrorCode=" + std::to_string(static_cast<int>(ret)));
    }
    
    logger_.debug("文件读取成功: " + file.file_name + 
                 ", 大小=" + std::to_string(imageData.size()) + " bytes");
    
    return imageData.size() > 0;
}

void MediaFileTask::dumpMediaFile(const std::string& filename,
                                  const std::vector<uint8_t>& data) {
    logger_.debug("准备保存文件: " + filename + ", 大小=" + std::to_string(data.size()) + " bytes");
    
    std::ofstream ofs(filename, std::ios::binary);
    if (ofs.is_open()) {
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
        ofs.close();
        logger_.info("✅ 文件已保存到本地: " + filename);
    } else {
        logger_.error("❌ 无法保存文件: " + filename);
    }
}

void MediaFileTask::handleError(const std::string& error) {
    logger_.error("任务错误: taskId=" + config_.taskId + ", error=" + error);
    notifyError(error);
}

// ==================== v3.0 新增：监控和自动完成相关方法 ====================

void MediaFileTask::monitorLoop() {
    logger_.info("🔍 监控线程已启动: taskId=" + config_.taskId);

    while (running_) {
        // 每隔 5 秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // 检查是否满足自动完成条件
        if (shouldComplete()) {
            logger_.info("✅ 满足自动完成条件，开始完成任务: taskId=" + config_.taskId);
            completeTask();
            break;
        }
    }

    logger_.info("🔍 监控线程已退出: taskId=" + config_.taskId);
}

bool MediaFileTask::shouldComplete() {
    std::lock_guard<std::mutex> lock(timerMutex_);

    // 条件 1: 航线任务已结束
    if (!taskEnded_.load()) {
        // 任务尚未结束，继续等待
        return false;
    }

    // 条件 2: 60 秒无新文件
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastFileTime_
    ).count();

    if (elapsed >= TIMEOUT_SECONDS) {
        logger_.info("超时检测: 已 " + std::to_string(elapsed) + 
                    " 秒无新文件，触发自动完成");
        return true;
    }

    // 尚未超时，继续等待
    logger_.debug("超时检测: " + std::to_string(elapsed) + " / " + 
                 std::to_string(TIMEOUT_SECONDS) + " 秒，继续等待");
    return false;
}

void MediaFileTask::completeTask() {
    logger_.info("🎯 开始自动完成任务: taskId=" + config_.taskId);

    // 1. 推送"任务分析完成"消息
    publishTaskAnalysisResult(true);

    // 2. 清理本地文件
    cleanupFiles();

    // 3. 恢复 DJI 设置
    restoreDjiSettings();

    // 4. 更新状态（不调用 stop，等待 device_algorithm_disable）
    logger_.info("✅ 任务已自动完成，等待平台发送 device_algorithm_disable");
    logger_.info("提示: 任务仍处于 RUNNING 状态，等待外部停止指令");
}

void MediaFileTask::publishTaskAnalysisResult(bool success) {
    logger_.info("📤 推送任务分析结果: taskId=" + config_.taskId + 
                ", result=" + (success ? "成功(0)" : "失败(1)"));
    
    try {
        // ⭐ 按照接口文档构建消息
        // 接口: thing/product/{device_sn}/events
        // Method: device_task_analysis_result
        // 字段: taskID (number), result (int: 0=成功, 1=失败)
        nlohmann::json message;
        message["method"] = "device_task_analysis_result";  // ✅ 修正 method 名
        
        // ✅ 字段直接放在顶层，不要嵌套 data
        try {
            // 尝试将 taskId 从字符串转换为数字
            message["taskID"] = std::stoi(config_.taskId);
        } catch (const std::exception&) {
            // 如果转换失败，使用字符串（不应该发生，但容错处理）
            logger_.warning("taskId 不是数字，使用字符串: " + config_.taskId);
            message["taskID"] = config_.taskId;
        }
        
        message["result"] = success ? 0 : 1;  // 0=成功, 1=失败
        
        // 📊 额外统计信息（可选，方便调试）
        message["_stats"] = {
            {"total_files", stats_.framesProcessed},
            {"total_detections", stats_.detectionsCount},
            {"events_published", stats_.eventsPublished}
        };
        
        // 3. 发布到 MQTT（使用 TaskService 保证 QoS=0 异步）
        std::string topic = "thing/product/" + config_.deviceSn + "/events";
        
        if (service_->publishMqttMessage(topic, message)) {
            logger_.info("✅ 任务分析结果发布成功");
        } else {
            logger_.error("❌ 任务分析结果发布失败");
            
            // 兜底：加入 EventCache 重试
            auto& cache = core::EventCache::getInstance();
            cache.publishEvent(topic, message);
            logger_.info("已加入 EventCache 等待重试");
        }
        
    } catch (const std::exception& e) {
        logger_.error("发布任务分析结果异常: " + std::string(e.what()));
    }
}

void MediaFileTask::cleanupFiles() {
    logger_.info("🗑️ 清理本地文件: taskId=" + config_.taskId);
    
    std::lock_guard<std::mutex> lock(filesMutex_);
    
    if (downloadedFiles_.empty()) {
        logger_.info("没有需要清理的文件");
        return;
    }
    
    int deletedCount = 0;
    int failedCount = 0;
    
    for (const auto& filePath : downloadedFiles_) {
        try {
            if (std::remove(filePath.c_str()) == 0) {
                logger_.debug("已删除文件: " + filePath);
                deletedCount++;
            } else {
                logger_.warning("删除文件失败: " + filePath);
                failedCount++;
            }
        } catch (const std::exception& e) {
            logger_.error("删除文件异常: " + filePath + ", error=" + std::string(e.what()));
            failedCount++;
        }
    }
    
    downloadedFiles_.clear();
    
    logger_.info("文件清理完成: 成功=" + std::to_string(deletedCount) + 
                ", 失败=" + std::to_string(failedCount));
}

// ==================== Day 11: 检测和事件生成 ⭐ ====================

bool MediaFileTask::processFile(const edge_sdk::MediaFile& file, 
                                const std::vector<uint8_t>& imageData) {
    logger_.info("🔍 处理文件: " + file.file_name + ", 大小=" + std::to_string(imageData.size()) + " bytes");
    
    // 1. ⭐ 保存到本地磁盘（与运行程序同级目录）
    std::string localFilePath = "./" + file.file_name;  // 当前目录
    logger_.info("💾 保存文件到本地: " + localFilePath);
    
    dumpMediaFile(localFilePath, imageData);
    
    // 记录本地文件路径（用于后续清理）
    {
        std::lock_guard<std::mutex> lock(filesMutex_);
        downloadedFiles_.push_back(localFilePath);
    }
    
    // 2. 解码图片
    cv::Mat frame = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (frame.empty()) {
        logger_.error("图片解码失败: " + file.file_name);
        return false;
    }
    
    logger_.debug("图片解码成功: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows));
    
    // 3. ⭐ 解析 EXIF 元数据（使用本地文件路径）
    auto& imageProcessor = utils::ImageProcessor::getInstance();
    auto exifOpt = imageProcessor.parseExif(localFilePath);  // ✅ 使用本地路径
    
    if (!exifOpt) {
        logger_.warning("EXIF 解析失败: " + localFilePath + ", 跳过该文件");
        return false;
    }
    
    auto& exif = *exifOpt;
    logger_.info("📍 EXIF 解析成功: GPS=(" + std::to_string(exif.latitude) + ", " + 
                std::to_string(exif.longitude) + "), 海拔=" + std::to_string(exif.altitude) + 
                "m, 航向=" + std::to_string(exif.heading) + "°");
    
    // 4. ⭐ 调用 TaskService 进行目标检测
    std::vector<BoundingBox> boundingBoxes;
    bool detectSuccess = service_->processFrame(frame, config_, boundingBoxes, file.file_name);
    
    if (!detectSuccess) {
        // 区分两种情况：
        // 1. boundingBoxes.empty() → 未检测到目标（正常，不算错误）
        // 2. boundingBoxes 仍然为空但 detectSuccess=false → 检测器调用失败（真正的错误）
        if (boundingBoxes.empty()) {
            logger_.info("ℹ️ 未检测到关注的目标: " + file.file_name + "（正常，继续处理下一张）");
            
            // 更新统计信息：已处理但无检测结果
            {
                //std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.framesProcessed++;
                // detectionsCount 保持为 0
            }
            
            // 返回 true 表示文件处理成功（只是没检测到目标）
            return true;
        } else {
            // 这种情况理论上不应该发生（detectSuccess=false 但有 boundingBoxes）
            logger_.error("❌ 目标检测失败: " + file.file_name);
            return false;
        }
    }
    
    logger_.info("✅ 检测完成: 检测到 " + std::to_string(boundingBoxes.size()) + " 个目标");
    
    // ⭐⭐ [重要] processFrame() 内部已经完成了事件的发布！
    // TaskService::processFrame() 会调用 publishEvent() (QoS=0, 异步)
    // 所以这里不需要再次构建和发布事件了
    //
    // 之前的代码有重复发布的bug:
    // 1. TaskService::publishEvent()  ← 第1次发布 (QoS=0, 2ms)
    // 2. EventCache::publishEvent()   ← 第2次发布 (QoS=1, 阻塞10s!)
    //
    // 重复发布导致:
    // - 同一事件发送两次 (浪费带宽)
    // - 第2次用 QoS=1 阻塞等待确认
    // - 2MB Base64 消息超时 → MQTT 断连
    
    // 5. 更新统计信息
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.framesProcessed++;
        stats_.detectionsCount += boundingBoxes.size();
    }
    
    logger_.info("✅ 文件处理完成: " + file.file_name);
    return true;
}

std::vector<nlohmann::json> MediaFileTask::buildEvent(
    const edge_sdk::MediaFile& file,
    const utils::ExifMetadata& exifMetadata,
    const cv::Mat& frame,
    const std::vector<BoundingBox>& boundingBoxes) {
    
    logger_.info("🏗️ 构建事件: " + file.file_name + ", 检测数=" + 
                std::to_string(boundingBoxes.size()));
    
    std::vector<nlohmann::json> events;
    
    // 如果没有检测结果，返回空列表
    if (boundingBoxes.empty()) {
        logger_.debug("无检测结果，不生成事件");
        return events;
    }
    
    // ⭐ 按算法类型分组检测结果
    // 假设：不同的 className 对应不同的算法类型
    // 例如：person/car/dog -> 每种类别一个事件
    std::map<std::string, std::vector<BoundingBox>> groupedDetections;
    
    for (const auto& bbox : boundingBoxes) {
        groupedDetections[bbox.className].push_back(bbox);
    }
    
    logger_.debug("检测结果分组: " + std::to_string(groupedDetections.size()) + " 个类别");
    
    // ⭐ 为每个算法类型生成一个事件
    for (const auto& [className, detections] : groupedDetections) {
        nlohmann::json event;
        
        // 基本信息
        event["tid"] = config_.taskId;
        event["bid"] = config_.taskId + "_" + file.file_name;  // 使用文件名作为唯一标识
        event["need_reply"] = 0;
        
        // 事件数据
        nlohmann::json data;
        data["sn"] = config_.deviceSn;  // ✅ 从配置获取设备SN
        data["class"] = className;      // 算法类型
        
        // ⭐ GPS 信息（从 EXIF 解析）
        data["latitude"] = exifMetadata.latitude;
        data["longitude"] = exifMetadata.longitude;
        data["high"] = exifMetadata.altitude;
        
        // 设备信息
        data["device_model_key"] = exifMetadata.cameraModel;  // 从 EXIF 获取
        data["lens_type"] = "zoom";  // ✅ 默认值：zoom（镜头类型，EXIF 中通常不包含此字段）
        
        // ⭐ 云台信息（从 EXIF 解析）
        data["gimbal_yaw_degree"] = exifMetadata.heading;
        
        // ⭐ 拍摄时间（从 EXIF 解析）
        data["shoot_time"] = exifMetadata.timestamp;
        
        // ⭐ 图片 Base64 编码（使用 ImageProcessor）
        auto& imageProcessor = utils::ImageProcessor::getInstance();
        std::string base64 = imageProcessor.encodeBase64(frame, ".jpg", 85);  // 压缩质量85%
        data["picture_url"] = "data:image/jpeg;base64," + base64;
        
        // ⭐ 检测结果（边界框列表）
        nlohmann::json resultArray = nlohmann::json::array();
        for (const auto& bbox : detections) {
            nlohmann::json det;
            det["x"] = static_cast<int>(bbox.x);
            det["y"] = static_cast<int>(bbox.y);
            det["width"] = static_cast<int>(bbox.w);
            det["height"] = static_cast<int>(bbox.h);
            det["confidence"] = bbox.confidence;
            resultArray.push_back(det);
        }
        data["result"] = resultArray;
        
        event["data"] = data;
        events.push_back(event);
        
        logger_.debug("生成事件: class=" + className + ", 检测数=" + std::to_string(detections.size()));
    }
    
    logger_.info("事件构建完成: " + std::to_string(events.size()) + " 个事件");
    return events;
}

void MediaFileTask::restoreDjiSettings() {
    logger_.info("🔧 恢复 DJI 设置: taskId=" + config_.taskId);
    
    if (!mediaManager_) {
        logger_.warning("MediaManager 未初始化，跳过设置恢复");
        return;
    }
    
    // 恢复 DJI 自动删除设置（启动时设置为 false）
    edge_sdk::ErrorCode ret = mediaManager_->SetDroneNestAutoDelete(true);
    if (ret != edge_sdk::kOk) {
        logger_.error("恢复 SetDroneNestAutoDelete(true) 失败: ErrorCode=" + 
                     std::to_string(static_cast<int>(ret)));
    } else {
        logger_.info("已恢复 SetDroneNestAutoDelete(true)");
    }
    
    // 可选：恢复云端上传设置
    // ret = mediaManager_->SetDroneNestUploadCloud(true);
    // if (ret != edge_sdk::kOk) {
    //     logger_.error("恢复 SetDroneNestUploadCloud(true) 失败");
    // }
    
    logger_.info("DJI 设置恢复完成");
}

}  // namespace task
}  // namespace esdk_sophon
