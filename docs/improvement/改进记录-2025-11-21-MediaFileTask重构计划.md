# MediaFileTask 重构计划 - Day 10 开始

**日期**: 2025-11-21  
**版本**: v3.0  
**状态**: 🚧 开发中  
**相关文档**:

- [MediaFileTask 完整设计方案.md](../architecture/MediaFileTask完整设计方案.md)
- [MediaFileTask 设计方案更新说明-v3.0.md](../architecture/MediaFileTask设计方案更新说明-v3.0.md)

---

## 🎯 重构目标

将现有的 MediaFileTask 从 **v1.0（自己实现所有功能）** 升级到 **v3.0（使用聚合工具类）**。

### 核心变化对比

| 功能            | v1.0 旧设计                                  | v3.0 新设计 ✅                            |
| --------------- | -------------------------------------------- | ----------------------------------------- |
| **EXIF 解析**   | MediaFileTask 私有方法 `parseExif()`         | 使用 `ImageProcessor::parseExif()`        |
| **图片缩放**    | ❌ 未实现                                    | 使用 `ImageProcessor::resize()`           |
| **Base64 编码** | TaskService 提供                             | 使用 `ImageProcessor::encodeBase64()`     |
| **GPS 计算**    | MediaFileTask 私有方法 `calculateGpsBatch()` | 使用 `GeoUtils::convertGpsToPixelBatch()` |
| **事件缓存**    | MediaFileTask 私有方法 `cacheEvent()`        | 使用 `EventCache::publishEvent()`         |
| **任务结束**    | ❌ 没有 `onTaskEnd()` 方法                   | ✅ 实现 `onTaskEnd()` + 超时自动完成      |
| **监控线程**    | ❌ 没有超时检测                              | ✅ 添加 `monitorThread_` 监控超时         |

---

## 📋 重构任务清单 (Day 10)

### 1. 更新头文件依赖 ✅

**文件**: `include/esdk_sophon/task/MediaFileTask.h`

```cpp
// ❌ 移除的头文件（这些功能由工具类提供）
// #include <libexif/exif-data.h>

// ✅ 新增的头文件
#include "esdk_sophon/utils/ImageProcessor.h"   // 图片处理工具
#include "esdk_sophon/utils/GeoUtils.h"         // 地理信息工具
#include "esdk_sophon/core/EventCache.h"        // 事件缓存服务
```

### 2. 修改类成员变量

#### 2.1 移除的成员变量 ❌

```cpp
// ❌ 事件缓存（由 EventCache 替代）
std::vector<nlohmann::json> cachedEvents_;
std::mutex cacheMutex_;
std::string cacheFilePath_;
```

#### 2.2 新增的成员变量 ✅

```cpp
// ✅ 任务结束标志
std::atomic<bool> taskEnded_{false};  // 航线任务是否结束

// ✅ 监控线程
std::unique_ptr<std::thread> monitorThread_;  // 超时监控线程

// ✅ 超时控制
std::chrono::steady_clock::time_point lastFileTime_;  // 最后收到文件时间
std::mutex timerMutex_;                               // 定时器互斥锁
static constexpr int TIMEOUT_SECONDS = 60;            // 60秒超时
```

### 3. 新增方法声明

```cpp
public:
    /**
     * @brief 处理"航线任务结束"通知 ⭐
     *
     * 与 LiveStreamTask 的关键差异：
     * - LiveStreamTask: 立即调用 stop()
     * - MediaFileTask: 仅标记 taskEnded_ = true，继续等待文件传输
     *
     * @note 任务会在满足以下条件时自动完成：
     *       1. taskEnded_ == true
     *       2. 60秒无新文件
     */
    void onTaskEnd() override;

private:
    /**
     * @brief 监控线程
     *
     * 定期检查是否满足自动完成条件：
     * - 条件1: taskEnded_ == true
     * - 条件2: 60秒无新文件
     */
    void monitorLoop();

    /**
     * @brief 判断任务是否应该完成
     *
     * @return true 满足完成条件
     * @return false 不满足
     */
    bool shouldComplete();

    /**
     * @brief 完成任务
     *
     * 流程:
     * 1. 推送 device_task_analysis_result
     * 2. 清理本地文件
     * 3. 恢复 DJI 设置
     * 4. 等待 device_algorithm_disable
     */
    void completeTask();

    /**
     * @brief 推送"任务分析完成"消息
     */
    void publishTaskAnalysisResult(bool success);

    /**
     * @brief 清理本地文件
     */
    void cleanupFiles();

    /**
     * @brief 恢复 DJI 设置
     */
    void restoreDjiSettings();
```

### 4. 移除的私有方法

```cpp
// ❌ 这些方法由工具类提供，不再需要
bool parseExif(const std::string& filePath, FileInfo& fileInfo);
bool calculateGpsBatch(std::vector<FileInfo>& fileInfos);
bool requestGpsCalculation(const nlohmann::json& exifData, nlohmann::json& gpsResults);
void cacheEvent(const nlohmann::json& event);
void retryCachedEvents();
bool publishEvent(const nlohmann::json& event);
```

---

## 🔧 重构实施步骤

### Step 1: 更新头文件 (5 分钟)

**任务**:

1. 在 `MediaFileTask.h` 中添加工具类头文件
2. 移除不再需要的头文件（如 `libexif`）

**验证**:

```bash
cd /workspace/build
make MediaFileTask -j4  # 编译验证
```

---

### Step 2: 修改类定义 (10 分钟)

**任务**:

1. 添加新成员变量（`taskEnded_`, `monitorThread_`, `lastFileTime_`）
2. 移除旧成员变量（`cachedEvents_`, `cacheMutex_`）
3. 添加新方法声明（`onTaskEnd()`, `monitorLoop()`, `shouldComplete()` 等）

**验证**:

```bash
make MediaFileTask -j4  # 编译验证
```

---

### Step 3: 实现 `start()` 方法重构 (15 分钟)

**文件**: `src/task/MediaFileTask.cpp`

```cpp
bool MediaFileTask::start() {
    logger_.info("启动媒体文件分析任务: " + config_.taskId);

    // 1. 配置 GeoUtils ⭐
    auto& geo = utils::GeoUtils::getInstance();
    if (!config_.gpsServiceUrl.empty()) {
        geo.setGpsServiceUrl(config_.gpsServiceUrl);  // 从配置读取
        geo.setHttpTimeout(5000);
        geo.setHttpRetryCount(3);
        geo.enableCache(true);  // 启用 LRU 缓存
    }

    // 2. 配置 EventCache ⭐
    auto& cache = core::EventCache::getInstance();
    cache.setCacheFilePath("/data/mediafile_event_cache.json");
    cache.setPublishCallback([this](const std::string& topic, const std::string& payload) {
        // TODO: 调用 MqttClient 发布
        // return mqttClient_->publish(topic, payload);
        logger_.info("EventCache 推送事件: " + topic);
        return true;  // 临时返回成功
    });

    // 3. 注册 MediaManager
    if (!registerMediaFilesObserver()) {
        return false;
    }

    // 4. 初始化超时控制 ⭐
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    // 5. 启动线程
    running_ = true;
    taskEnded_ = false;  // ⭐ 初始化标志

    workerThread_ = std::make_unique<std::thread>(&MediaFileTask::execute, this);
    monitorThread_ = std::make_unique<std::thread>(&MediaFileTask::monitorLoop, this);  // ⭐ 新增

    // 6. 更新状态
    state_ = TaskState::RUNNING;
    notifyStateChanged(TaskState::RUNNING);

    logger_.info("媒体文件任务已启动: " + config_.taskId);
    return true;
}
```

**验证**:

```bash
make MediaFileTask -j4
```

---

### Step 4: 实现 `onTaskEnd()` 方法 (10 分钟)

```cpp
void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，标记航线结束，继续等待文件传输");

    // ⭐ 关键差异：仅标记，不停止任务
    taskEnded_.store(true);

    // 任务继续运行，由 monitorLoop() 检测超时条件
    logger_.info("taskEnded_ 已设置为 true，等待文件传输完成（60秒超时）");
}
```

**对比** (重要):

```cpp
// LiveStreamTask 的 onTaskEnd()（立即停止）
void LiveStreamTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，立即停止视频流任务");
    stop();  // ⭐ 立即停止
    publishTaskAnalysisResult(true);
}

// MediaFileTask 的 onTaskEnd()（仅标记）
void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，标记航线结束，继续等待文件");
    taskEnded_.store(true);  // ⭐ 仅标记，不停止
}
```

---

### Step 5: 实现监控线程 (20 分钟)

```cpp
void MediaFileTask::monitorLoop() {
    logger_.info("🔍 监控线程已启动");

    while (running_) {
        // 每隔 5 秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // 检查是否满足自动完成条件
        if (shouldComplete()) {
            logger_.info("✅ 满足自动完成条件，开始完成任务");
            completeTask();
            break;
        }
    }

    logger_.info("🔍 监控线程已退出");
}

bool MediaFileTask::shouldComplete() {
    std::lock_guard<std::mutex> lock(timerMutex_);

    // 条件 1: 航线任务已结束
    if (!taskEnded_.load()) {
        return false;
    }

    // 条件 2: 60 秒无新文件
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastFileTime_
    ).count();

    if (elapsed >= TIMEOUT_SECONDS) {
        logger_.info("超时检测: 已 " + std::to_string(elapsed) + " 秒无新文件");
        return true;
    }

    logger_.debug("超时检测: " + std::to_string(elapsed) + " / " +
                  std::to_string(TIMEOUT_SECONDS) + " 秒");
    return false;
}

void MediaFileTask::completeTask() {
    logger_.info("🎯 开始自动完成任务: " + config_.taskId);

    // 1. 推送"任务分析完成"消息
    publishTaskAnalysisResult(true);

    // 2. 清理本地文件
    cleanupFiles();

    // 3. 恢复 DJI 设置
    restoreDjiSettings();

    // 4. 更新状态（不调用 stop，等待 device_algorithm_disable）
    logger_.info("✅ 任务已自动完成，等待 device_algorithm_disable");
}

void MediaFileTask::publishTaskAnalysisResult(bool success) {
    nlohmann::json message;
    message["tid"] = config_.taskId;
    message["bid"] = config_.taskId;  // TODO: 生成唯一 bid
    message["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    message["data"]["result"] = success ? 0 : 1;

    // TODO: 通过 MqttClient 发送到 thing/product/{sn}/events
    logger_.info("📤 推送任务分析结果: " + message.dump());
}

void MediaFileTask::cleanupFiles() {
    logger_.info("🗑️ 清理本地文件...");
    // TODO: 删除 downloadedFiles_ 中的文件
}

void MediaFileTask::restoreDjiSettings() {
    logger_.info("🔧 恢复 DJI 设置...");

    if (mediaManager_) {
        // 恢复 DJI 自动删除设置
        mediaManager_->SetDroneNestAutoDelete(true);
        logger_.info("已恢复 SetDroneNestAutoDelete(true)");
    }
}
```

---

### Step 6: 重构 `processFile()` 使用工具类 (30 分钟)

**关键改动**:

```cpp
bool MediaFileTask::processFile(const FileInfo& fileInfo) {
    // 1. 读取图片
    cv::Mat image;
    if (!readImage(fileInfo.filePath, image)) {
        return false;
    }

    // 2. 解析 EXIF（使用 ImageProcessor）⭐
    auto& processor = utils::ImageProcessor::getInstance();
    auto metadata = processor.parseExif(fileInfo.filePath);
    if (!metadata || !metadata->hasGps) {
        logger_.error("EXIF 解析失败或无 GPS 数据: " + fileInfo.filePath);
        return false;
    }

    // 3. 调用检测
    auto result = service_->processFrame(image, config_);
    if (!result.has_value()) {
        return false;
    }

    // 4. 生成事件
    nlohmann::json event = buildEvent(fileInfo, result.value());

    // 5. 添加图片元数据
    event["timestamp"] = metadata->timestamp;
    event["cameraModel"] = metadata->cameraModel;

    // 6. 生成缩略图（使用 ImageProcessor）⭐
    auto resized = processor.resize(image, 800, 600);
    event["thumbnail"] = processor.encodeBase64(resized);

    // 7. 计算 GPS（使用 GeoUtils）⭐
    auto& geo = utils::GeoUtils::getInstance();
    utils::GpsCoordinate gpsCoord{
        metadata->latitude,
        metadata->longitude,
        metadata->altitude,
        metadata->heading
    };
    auto pixelCoord = geo.convertGpsToPixel(gpsCoord);
    if (pixelCoord.success) {
        event["gpsX"] = pixelCoord.x;
        event["gpsY"] = pixelCoord.y;
    } else {
        logger_.warning("GPS 计算失败: " + pixelCoord.errorMsg);
    }

    // 8. 推送事件（使用 EventCache）⭐
    auto& cache = core::EventCache::getInstance();
    cache.publishEvent("drone/detection_event", event);  // 自动缓存+重试

    // 9. 更新统计
    stats_.processedFrames++;

    // 10. 更新最后收到文件时间 ⭐
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    return true;
}
```

---

### Step 7: 更新 `stop()` 方法 (10 分钟)

```cpp
void MediaFileTask::stop() {
    if (state_ != TaskState::RUNNING && state_ != TaskState::PAUSED) {
        logger_.warning("任务未运行，无需停止: " + config_.taskId);
        return;
    }

    logger_.info("停止媒体文件任务: " + config_.taskId);

    // 1. 设置停止标志
    running_ = false;

    // 2. 通知条件变量
    queueCv_.notify_one();
    if (paused_) {
        paused_ = false;
        pauseCv_.notify_one();
    }

    // 3. 等待工作线程结束
    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }

    // 4. 等待监控线程结束 ⭐
    if (monitorThread_ && monitorThread_->joinable()) {
        monitorThread_->join();
    }

    // 5. 注销 MediaFilesObserver
    unregisterMediaFilesObserver();

    // 6. 清空队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!fileQueue_.empty()) {
            fileQueue_.pop();
        }
    }

    // 7. 更新状态
    state_ = TaskState::COMPLETED;
    notifyStateChanged(TaskState::COMPLETED);

    logger_.info("媒体文件任务已停止: " + config_.taskId);
}
```

---

## ✅ 验收标准

### 编译验证

```bash
cd /workspace/build
make MediaFileTask -j4
# 预期: 编译成功，无错误
```

### 功能验证

- [ ] `start()` 成功启动任务
- [ ] `onTaskEnd()` 正确标记 `taskEnded_`
- [ ] `monitorLoop()` 正确检测超时条件
- [ ] `shouldComplete()` 逻辑正确（60 秒 + taskEnded\_）
- [ ] `completeTask()` 正确推送消息
- [ ] `stop()` 正确停止所有线程

### 工具类集成验证

- [ ] ImageProcessor 正确解析 EXIF
- [ ] GeoUtils 正确计算 GPS
- [ ] EventCache 正确缓存和重试事件

---

## 📚 学习要点

### 1. 为什么 MediaFileTask 不立即停止？

**业务场景**:

- 飞机返航后，可能还有照片在 4G 网络中传输
- 如果立即停止任务，会丢失这些照片
- 因此需要等待一段时间（60 秒），确保所有照片都传输完成

**对比**:

- **LiveStreamTask**: 实时流，`device_task_end` 表示流已结束，立即停止
- **MediaFileTask**: 文件队列，`device_task_end` 表示航线结束，但文件可能还在传输

### 2. 监控线程的作用

**职责**:

- 定期（5 秒）检查是否满足自动完成条件
- 条件 1: `taskEnded_ == true`（平台发送了 `device_task_end`）
- 条件 2: 60 秒无新文件（文件传输完成）

**为什么不在文件处理线程中检测？**

- 文件处理线程可能阻塞在 `queueCv_.wait()`
- 如果队列为空，文件处理线程无法检测超时
- 需要独立的监控线程定期检查

### 3. 工具类的优势

**v1.0 问题**:

- MediaFileTask 类臃肿（500+ 行）
- EXIF 解析、GPS 计算、事件缓存逻辑混在一起
- 难以复用、难以测试

**v3.0 优势**:

- **单一职责**：MediaFileTask 只负责任务流程控制
- **高内聚**：工具类各司其职（ImageProcessor、GeoUtils、EventCache）
- **可复用**：工具类可在其他地方使用
- **可测试**：工具类已独立测试（如 EventCache 测试）

---

## 🎯 下一步

完成 Day 10 后，继续 Day 11-14 的开发：

- **Day 11**: 检测和事件生成
- **Day 12**: GPS 计算和 MQTT 推送
- **Day 13**: 集成测试
- **Day 14**: 文档完善

---

**创建时间**: 2025-11-21  
**预计完成**: Day 10 今天完成，Day 11-14 本周完成  
**状态**: 🚧 开发中
