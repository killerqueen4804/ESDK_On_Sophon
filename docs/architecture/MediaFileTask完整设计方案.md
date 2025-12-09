# 📘 MediaFileTask 完整设计方案 (Day 10-14)

**创建日期**: 2025-11-20  
**架构方案**: 四层架构（遵循 `Task模块实施规划-四层架构.md`）  
**版本**: v3.0 (架构重构)  
**状态**: 设计完成，待实施 🚧

---

## ⚠️ 重要更新 (v3.0)

**本文档已根据类粒度优化进行重构，详见 [MediaFileTask 架构重构说明.md](MediaFileTask架构重构说明.md)**

### 核心变化

| 改动            | 原设计 (v1.0)                                | 新设计 (v3.0)                                |
| --------------- | -------------------------------------------- | -------------------------------------------- |
| **EXIF 解析**   | MediaFileTask 私有方法 `parseExif()`         | ✅ 使用 `ImageProcessor::parseExif()`        |
| **GPS 计算**    | MediaFileTask 私有方法 `calculateGpsBatch()` | ✅ 使用 `GeoUtils::convertGpsToPixelBatch()` |
| **事件缓存**    | MediaFileTask 私有方法 `cacheEvent()`        | ✅ 使用 `EventCache::publishEvent()`         |
| **图片缩放**    | ❌ 未设计                                    | ✅ 使用 `ImageProcessor::resize()`           |
| **Base64 编码** | TaskService 提供                             | ✅ 使用 `ImageProcessor::encodeBase64()`     |

**新增依赖**:

- `utils/ImageProcessor.h` - 图片处理工具（10+ 功能）
- `utils/GeoUtils.h` - 地理信息工具（15+ 功能）
- `core/EventCache.h` - 事件缓存服务

---

## 📋 目录

1. [需求分析](#需求分析)
2. [核心差异分析](#核心差异分析)
3. [四层架构设计](#四层架构设计)
4. [MediaFileTask 类设计](#mediafiletask-类设计)
5. [MQTT 消息处理流程](#mqtt-消息处理流程)
6. [文件处理流程](#文件处理流程)
7. [GPS 坐标计算方案](#gps-坐标计算方案)
8. [事件缓存与重试机制](#事件缓存与重试机制)
9. [序列图](#序列图)
10. [状态机设计](#状态机设计)
11. [分阶段开发计划](#分阶段开发计划)
12. [测试方案](#测试方案)
13. [风险与注意事项](#风险与注意事项)

---

## 需求分析

### 1.1 业务场景

**航线巡检任务**:

1. 平台下发 `device_algorithm_enable` 启动任务
2. 飞机按照航线飞行，拍摄照片
3. 飞行过程中，照片通过 4G 传输到算能微服务器
4. 飞机返航后，平台发送 `device_task_end`
5. 算能微服务器继续接收剩余照片（可能延迟几分钟）
6. 60 秒无新照片 + `taskEnded_=true` → 自动推送完成消息
7. 平台发送 `device_algorithm_disable` 结束任务

### 1.2 功能需求

| 需求         | 描述                                       | 优先级 |
| ------------ | ------------------------------------------ | ------ |
| **文件接收** | 注册 DJI MediaManager 回调，接收 JPEG 文件 | P0     |
| **目标检测** | 对每张图片进行检测（YOLOv10）              | P0     |
| **GPS 计算** | 解析 EXIF，批量请求 HTTP 服务获取 GPS      | P0     |
| **事件推送** | 按算法类型分组，推送 MQTT 事件             | P0     |
| **超时控制** | 60 秒无新文件 + taskEnded\_ → 自动完成     | P0     |
| **事件缓存** | MQTT 断线时本地缓存，重连后重试            | P1     |
| **文件清理** | 任务结束后删除本地文件                     | P1     |
| **资源恢复** | 恢复 `SetDroneNestAutoDelete(true)`        | P1     |

### 1.3 非功能需求

- **性能**: 检测延迟 < 500ms/张
- **可靠性**: 事件不丢失（缓存+重试）
- **扩展性**: 支持多种算法类型
- **可维护性**: 清晰的日志和错误处理

---

## 核心差异分析

### 2.1 LiveStreamTask vs MediaFileTask 对比表

| 维度                       | LiveStreamTask (视频流)                                                                           | MediaFileTask (媒体文件)                           |
| -------------------------- | ------------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| **数据源**                 | DJI Liveview (实时 H.264 流)                                                                      | DJI MediaManager (历史 JPEG 文件)                  |
| **触发方式**               | 连续解码帧                                                                                        | 文件回调 + 队列处理                                |
| **检测频率**               | 异步检测（主线程 30FPS，检测线程按能力）                                                          | 每张图片必检测                                     |
| **发布策略**               | 实时推流 + 异步 MQTT 推送（检测到目标时）                                                         | 每张图片一次 MQTT 事件（按算法类型分组）           |
| **`device_task_end` 处理** | **立即结束任务** + 推送 `device_task_analysis_result`                                             | **仅标记 `taskEnded_`，不停止任务**                |
| **超时控制**               | 无（流式处理）                                                                                    | 60 秒无新文件 + `taskEnded_=true` → 自动完成       |
| **资源管理**               | 停止流、关闭 RTMP                                                                                 | 删除本地文件 + 恢复 `SetDroneNestAutoDelete(true)` |
| **图片处理**               | 原图检测 + Resize(800x600) 推流                                                                   | 原图检测 + Resize(800x600) MQTT 推送               |
| **GPS 计算**               | 无（实时流无 EXIF）                                                                               | HTTP 批量请求（基于 EXIF 元数据）                  |
| **事件缓存**               | 无                                                                                                | 本地缓存 + MQTT 重连重试                           |
| **共同点**                 | ✅ 都调用 `TaskService::processFrame()`<br>✅ 都通过 `TaskManager` 管理<br>✅ 都继承 `ITask` 接口 |

### 2.2 关键差异点详解

#### ⭐ 差异 1: `device_task_end` 处理逻辑

```cpp
// LiveStreamTask 的处理
void LiveStreamTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，视频流任务立即结束");

    // 1. 立即停止任务
    stop();

    // 2. 推送"任务分析完成"
    publishTaskAnalysisResult(true);  // result=0 成功

    // 3. 等待平台发送 device_algorithm_disable
}

// MediaFileTask 的处理
void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，标记航线结束，继续等待文件");

    // 1. 仅标记标志，不停止任务 ⭐
    taskEnded_.store(true);

    // 2. 任务继续运行，等待文件传输
    // 3. 当满足条件时自动完成：
    //    - taskEnded_ == true
    //    - 60秒无新文件
}
```

#### ⭐ 差异 2: 任务完成条件

```cpp
// LiveStreamTask: 外部控制结束
bool LiveStreamTask::shouldStop() {
    // 1. 收到 stop() 调用
    // 2. 收到 device_task_end
    // 3. 发生错误
    return !running_.load();
}

// MediaFileTask: 自动判断结束
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

    return elapsed >= TIMEOUT_SECONDS;
}
```

---

## 四层架构设计

### 3.1 层次关系

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 1: Access Layer (接入层)                              │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  MqttHandler                                          │  │
│  │  - handleAlgorithmEnable()                            │  │
│  │  - handleTaskEnd() ⭐                                 │  │
│  │  - handleAlgorithmDisable()                           │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                          ↓ MQTT消息
┌─────────────────────────────────────────────────────────────┐
│  Layer 2: Manager Layer (管理层)                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  TaskManager (Factory Pattern)                        │  │
│  │  - createTask(config) → MediaFileTask/LiveStreamTask  │  │
│  │  - startTask() / stopTask()                           │  │
│  │  - getTask() → TaskPtr                                │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                          ↓ 任务实例
┌─────────────────────────────────────────────────────────────┐
│  Layer 3: Service Layer (服务层)                             │
│  ┌────────────────────┐     ┌──────────────────────────┐   │
│  │  TaskService       │     │  MediaFileTask           │   │
│  │  (Business Logic)  │◄────│  - onFileReceived()      │   │
│  │                    │     │  - processFile()         │   │
│  │  - processFrame()  │     │  - onTaskEnd() ⭐        │   │
│  │  - detectObjects() │     │  - shouldComplete()      │   │
│  │  - buildEvent()    │     └──────────────────────────┘   │
│  │  - publishEvent()  │                                     │
│  └────────────────────┘                                     │
└─────────────────────────────────────────────────────────────┘
                          ↓ 调用工具层
┌─────────────────────────────────────────────────────────────┐
│  Layer 4: Utility Layer (工具层)                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
│  │  Vision  │  │  MQTT    │  │  HTTP    │  │  DJI SDK  │  │
│  │  Detector│  │  Client  │  │  Client  │  │  MediaMgr │  │
│  └──────────┘  └──────────┘  └──────────┘  └───────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 职责划分

| 层次        | 组件          | 职责             | 输入       | 输出             |
| ----------- | ------------- | ---------------- | ---------- | ---------------- |
| **Layer 1** | MqttHandler   | MQTT 消息路由    | MQTT JSON  | 调用 TaskManager |
| **Layer 2** | TaskManager   | 任务生命周期管理 | TaskConfig | 创建/管理 Task   |
| **Layer 3** | MediaFileTask | 文件处理逻辑     | 文件回调   | 调用 TaskService |
| **Layer 3** | TaskService   | 检测业务编排     | cv::Mat    | MQTT 事件        |
| **Layer 4** | MediaManager  | 文件下载         | 飞机照片   | 本地文件         |
| **Layer 4** | Detector      | 目标检测         | 图片       | BoundingBox[]    |
| **Layer 4** | HttpClient    | GPS 计算         | EXIF 数据  | GPS 坐标         |
| **Layer 4** | MqttClient    | 事件上报         | JSON       | 发布到 MQTT      |

---

## MediaFileTask 类设计

### 4.1 类声明

**文件**: `include/esdk_sophon/task/MediaFileTask.h`

```cpp
/**
 * @file MediaFileTask.h
 * @brief 媒体文件分析任务
 *
 * 负责处理航线巡检任务中的历史媒体文件（JPEG照片）。
 *
 * 核心功能:
 * - 接收 DJI MediaManager 文件回调
 * - 解析图片 EXIF 元数据
 * - 目标检测（YOLOv10）
 * - GPS 坐标计算（HTTP 批量请求）
 * - 事件推送（按算法类型分组）
 * - 超时控制（60秒无新文件自动完成）
 * - 事件缓存与重试
 *
 * 线程模型:
 * - 文件回调线程：DJI SDK 回调线程
 * - 工作线程：从队列取文件，调用检测
 * - 监控线程：检测超时条件，自动完成任务
 *
 * @author ESDK Sophon Team
 * @date 2025-11-20
 */

#ifndef ESDK_SOPHON_TASK_MEDIAFILE_TASK_H_
#define ESDK_SOPHON_TASK_MEDIAFILE_TASK_H_

#include "esdk_sophon/task/ITask.h"
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/core/Logger.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <opencv2/opencv.hpp>

// DJI SDK
#include "media_manager.h"  // edge_sdk::MediaManager

namespace esdk_sophon {
namespace task {

/**
 * @brief 文件信息结构体
 */
struct FileInfo {
    std::string filePath;     ///< 本地文件路径
    std::string fileName;     ///< 文件名
    uint64_t fileSize;        ///< 文件大小（字节）
    std::string timestamp;    ///< 拍摄时间戳

    // EXIF 元数据
    double latitude{0.0};     ///< 纬度（度）
    double longitude{0.0};    ///< 经度（度）
    double altitude{0.0};     ///< 海拔（米）
    double heading{0.0};      ///< 航向角（度）

    // GPS 计算结果
    double gpsX{0.0};         ///< GPS-X 坐标（像素）
    double gpsY{0.0};         ///< GPS-Y 坐标（像素）
    bool gpsCalculated{false}; ///< GPS 是否已计算
};

/**
 * @brief 媒体文件分析任务
 *
 * 工作流程:
 * @code
 * start()
 *   ↓
 * 注册 MediaManager 回调 → onFileReceived()
 *   ↓
 * while (running) {
 *   1. 从队列取文件 (fileQueue_)
 *   2. 解析 EXIF 元数据
 *   3. 调用 TaskService::processFrame()
 *   4. 按算法类型分组生成事件
 *   5. 批量计算 GPS
 *   6. 推送 MQTT 事件
 *   7. 更新 lastFileTime_
 *   8. 检查超时条件 (shouldComplete)
 * }
 *   ↓
 * stop()
 *   ↓
 * 清理资源
 * @endcode
 */
class MediaFileTask : public ITask {
public:
    /**
     * @brief 构造函数
     *
     * @param config 任务配置
     * @param service TaskService 实例（依赖注入）
     */
    MediaFileTask(const TaskConfig& config,
                  std::shared_ptr<TaskService> service);

    /**
     * @brief 析构函数
     */
    ~MediaFileTask() override;

    // 禁止拷贝和赋值
    MediaFileTask(const MediaFileTask&) = delete;
    MediaFileTask& operator=(const MediaFileTask&) = delete;

    // ==================== ITask 接口实现 ====================

    bool start() override;
    void stop() override;
    bool pause() override;
    bool resume() override;
    bool isRunning() const override;
    TaskState getState() const override;
    const TaskConfig& getConfig() const override;
    TaskStatistics getStatistics() const override;
    void setStateCallback(TaskCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;

    /**
     * @brief 处理"航线任务结束"通知 ⭐
     *
     * 媒体文件任务的特殊逻辑：
     * - 仅设置 taskEnded_ 标志为 true
     * - 任务继续运行，等待文件传输完成
     * - 当 60 秒无新文件时，自动完成任务
     */
    void onTaskEnd() override;

protected:
    /**
     * @brief 工作线程主循环
     *
     * 从文件队列取文件，进行检测和事件上报。
     */
    void execute() override;

    /**
     * @brief 监控线程
     *
     * 周期检查超时条件，自动完成任务。
     */
    void monitorLoop();

    void notifyStateChanged(TaskState newState) override;
    void notifyError(const std::string& error) override;

private:
    // ==================== DJI MediaManager 回调 ====================

    /**
     * @brief 文件下载完成回调
     *
     * 由 DJI SDK 在文件下载完成时调用。
     *
     * @param fileInfo 文件信息
     */
    void onFileReceived(const edge_sdk::MediaFileInfo& fileInfo);

    /**
     * @brief 注册 MediaManager 观察者
     */
    bool registerMediaObserver();

    /**
     * @brief 取消注册 MediaManager 观察者
     */
    void unregisterMediaObserver();

    // ==================== 文件处理 ====================

    /**
     * @brief 处理单个文件
     *
     * @param fileInfo 文件信息
     * @return true 处理成功
     */
    bool processFile(const FileInfo& fileInfo);

    /**
     * @brief 解析图片 EXIF 元数据
     *
     * @param filePath 图片路径
     * @param[out] fileInfo 输出文件信息
     * @return true 解析成功
     */
    bool parseExif(const std::string& filePath, FileInfo& fileInfo);

    /**
     * @brief 读取图片为 cv::Mat
     *
     * @param filePath 图片路径
     * @param[out] image 输出图像
     * @return true 读取成功
     */
    bool readImage(const std::string& filePath, cv::Mat& image);

    // ==================== GPS 计算 ====================

    /**
     * @brief 批量计算 GPS 坐标
     *
     * 收集多个文件的 EXIF 数据，批量请求 HTTP 服务。
     *
     * @param fileInfos 文件信息列表
     * @return true 计算成功
     */
    bool calculateGpsBatch(std::vector<FileInfo>& fileInfos);

    /**
     * @brief HTTP 请求 GPS 计算服务
     *
     * @param exifData EXIF 数据（JSON格式）
     * @param[out] gpsResults GPS 结果（JSON格式）
     * @return true 请求成功
     */
    bool requestGpsCalculation(const nlohmann::json& exifData,
                               nlohmann::json& gpsResults);

    // ==================== 事件生成与推送 ====================

    /**
     * @brief 按算法类型分组生成事件
     *
     * @param fileInfo 文件信息
     * @param detections 检测结果
     * @return nlohmann::json 事件 JSON
     */
    nlohmann::json buildEvent(const FileInfo& fileInfo,
                              const std::vector<BoundingBox>& detections);

    /**
     * @brief 推送事件到 MQTT
     *
     * @param event 事件 JSON
     * @return true 推送成功
     */
    bool publishEvent(const nlohmann::json& event);

    /**
     * @brief 缓存事件（MQTT 断线时）
     *
     * @param event 事件 JSON
     */
    void cacheEvent(const nlohmann::json& event);

    /**
     * @brief 重试缓存的事件
     *
     * MQTT 重连后调用。
     */
    void retryCachedEvents();

    // ==================== 任务完成判断 ====================

    /**
     * @brief 判断任务是否应该完成
     *
     * 条件:
     * - taskEnded_ == true（收到 device_task_end）
     * - 60 秒无新文件
     *
     * @return true 应该完成
     */
    bool shouldComplete();

    /**
     * @brief 完成任务
     *
     * 推送 device_task_analysis_result，等待 disable。
     */
    void completeTask();

    /**
     * @brief 推送"任务分析完成"消息
     *
     * @param success 是否成功
     */
    void publishTaskAnalysisResult(bool success);

    // ==================== 资源清理 ====================

    /**
     * @brief 删除本地文件
     */
    void cleanupFiles();

    /**
     * @brief 恢复 DJI 设置
     *
     * SetDroneNestAutoDelete(true)
     */
    void restoreDjiSettings();

private:
    // ==================== 成员变量 ====================

    TaskConfig config_;
    std::shared_ptr<TaskService> service_;

    // 状态管理
    std::atomic<TaskState> state_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<bool> taskEnded_;  ///< ⭐ 航线任务是否结束

    // 线程管理
    std::unique_ptr<std::thread> workerThread_;  ///< 工作线程
    std::unique_ptr<std::thread> monitorThread_; ///< 监控线程

    // 回调函数
    TaskCallback stateCallback_;
    ErrorCallback errorCallback_;

    // 统计信息
    TaskStatistics stats_;
    mutable std::mutex statsMutex_;

    // ==================== 文件队列 ====================

    std::queue<FileInfo> fileQueue_;  ///< 待处理文件队列
    std::mutex queueMutex_;           ///< 队列互斥锁
    std::condition_variable queueCv_; ///< 队列条件变量
    size_t maxQueueSize_{100};        ///< 最大队列长度

    // ==================== 超时控制 ====================

    std::chrono::steady_clock::time_point lastFileTime_;  ///< 最后收到文件的时间
    std::mutex timerMutex_;                               ///< 定时器互斥锁
    static constexpr int TIMEOUT_SECONDS = 60;            ///< 超时时间（秒）

    // ==================== 事件缓存 ====================

    std::vector<nlohmann::json> cachedEvents_;  ///< 缓存的事件
    std::mutex cacheMutex_;                     ///< 缓存互斥锁
    std::string cacheFilePath_;                 ///< 缓存文件路径

    // ==================== DJI MediaManager ====================

    std::shared_ptr<edge_sdk::MediaManager> mediaManager_;  ///< MediaManager 实例
    int observerId_{-1};                                    ///< 观察者 ID

    // ==================== 文件存储 ====================

    std::vector<std::string> downloadedFiles_;  ///< 已下载文件列表（用于清理）
    std::mutex filesMutex_;                     ///< 文件列表互斥锁

    // 日志
    core::Logger& logger_;
};

}  // namespace task
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_TASK_MEDIAFILE_TASK_H_
```

---

## MQTT 消息处理流程

### 5.1 device_algorithm_enable

```
Platform                MqttHandler           TaskManager          MediaFileTask
    │                        │                      │                     │
    ├─device_algorithm_enable→│                      │                     │
    │                        ├─publishReply(0)       │                     │
    │                        │  (立即回复成功)        │                     │
    │◄─────────────────────-─┤                      │                     │
    │                        ├─startTask(config)─────→│                     │
    │                        │                      ├─createTask()───────→│
    │                        │                      │  (Factory Pattern)   │
    │                        │                      │                     ├─start()
    │                        │                      │                     ├─SetDroneNestAutoDelete(false)
    │                        │                      │                     ├─RegisterMediaObserver()
    │                        │                      │                     ├─启动工作线程
    │                        │                      │                     ├─启动监控线程
    │                        │                      │◄────────────────────┤
    │                        │                      │  (任务已启动)         │
    │                        │◄─────────────────────┤                     │
    │                        │   (成功)              │                     │
```

### 5.2 device_task_end（核心差异）⭐

```
Platform                MqttHandler           TaskManager          MediaFileTask
    │                        │                      │                     │
    ├─device_task_end────────→│                      │                     │
    │                        ├─publishReply(0)       │                     │
    │                        │  (立即回复成功)        │                     │
    │◄───────────────────────┤                      │                     │
    │                        ├─getTask(taskId)───────→│                     │
    │                        │◄─────────────────────┤                     │
    │                        │  (返回 TaskPtr)       │                     │
    │                        ├─task->onTaskEnd()─────┼─────────────────→│
    │                        │                      │                   ├─taskEnded_.store(true)
    │                        │                      │                   │  ⭐ 仅标记，不停止
    │                        │                      │                   ├─logger: "标记结束，继续等待文件"
    │                        │                      │                   │
    │                        │                      │                   │  (任务继续运行)
    │                        │                      │                   │
    │                        │                      │                   ├─监控线程检测:
    │                        │                      │                   │  - taskEnded_ == true
    │                        │                      │                   │  - 60秒无新文件
    │                        │                      │                   │  → shouldComplete() == true
    │                        │                      │                   │
    │                        │                      │                   ├─completeTask()
    │◄─device_task_analysis_result───────────────────┼───────────────────┤
    │  { "result": 0 }        │                      │                   │
    │                        │                      │                   │
    ├─device_algorithm_disable→│                      │                   │
    │                        ├─stopTask()────────────→│                   │
    │                        │                      ├─task->stop()───────→│
    │                        │                      │                   ├─cleanupFiles()
    │                        │                      │                   ├─SetDroneNestAutoDelete(true)
    │                        │                      │                   ├─UnregisterMediaObserver()
    │                        │                      │                   │
    │◄─────publishReply(0)───┤                      │                   │
```

对比 LiveStreamTask 的 device_task_end:

```cpp
// LiveStreamTask: 立即停止
void LiveStreamTask::onTaskEnd() {
    stop();  // ⭐ 立即停止任务
    publishTaskAnalysisResult(true);
}

// MediaFileTask: 仅标记，继续等待
void MediaFileTask::onTaskEnd() {
    taskEnded_.store(true);  // ⭐ 仅设置标志
    // 任务继续运行，等待文件传输完成
}
```

---

## 文件处理流程

### 6.1 文件接收与处理序列图

```
DJI SDK            MediaFileTask          FileQueue          WorkerThread         TaskService
   │                    │                      │                     │                  │
   ├─onFileReceived()───→│                      │                     │                  │
   │                    ├─解析文件信息           │                     │                  │
   │                    ├─enqueue()─────────────→│                     │                  │
   │                    │                      ├─notify()─────────────→│                  │
   │                    │                      │                     ├─dequeue()──────→│
   │                    │                      │◄────────────────────┤                  │
   │                    │                      │   (FileInfo)         │                  │
   │                    │                      │                     ├─readImage()       │
   │                    │                      │                     ├─parseExif()       │
   │                    │                      │                     ├─processFrame()────→│
   │                    │                      │                     │                  ├─detectObjects()
   │                    │                      │                     │                  ├─buildEvent()
   │                    │                      │                     │                  ├─encodeBase64()
   │                    │                      │                     │◄─────────────────┤
   │                    │                      │                     ├─calculateGps()    │
   │                    │                      │                     ├─publishEvent()    │
   │                    │                      │                     ├─updateStats()     │
   │                    │                      │                     ├─updateLastFileTime()
   │                    │                      │                     │                  │
```

### 6.2 核心代码实现（伪代码）

```cpp
void MediaFileTask::execute() {
    logger_.info("工作线程已启动");

    while (running_) {
        // 1. 从队列取文件
        FileInfo fileInfo;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] {
                return !fileQueue_.empty() || !running_;
            });

            if (!running_) break;

            fileInfo = fileQueue_.front();
            fileQueue_.pop();
        }

        // 2. 处理文件
        try {
            processFile(fileInfo);
        } catch (const std::exception& e) {
            logger_.error("处理文件失败: " + std::string(e.what()));
        }
    }

    logger_.info("工作线程已退出");
}

bool MediaFileTask::processFile(const FileInfo& fileInfo) {
    // 1. 读取图片
    cv::Mat image;
    if (!readImage(fileInfo.filePath, image)) {
        return false;
    }

    // 2. 调用检测
    auto result = service_->processFrame(image, config_);
    if (!result.has_value()) {
        return false;
    }

    // 3. 生成事件
    auto event = buildEvent(fileInfo, result.value());

    // 4. 计算 GPS（批量）
    // TODO: 收集多个文件，批量请求

    // 5. 推送事件
    if (!publishEvent(event)) {
        cacheEvent(event);  // 失败时缓存
    }

    // 6. 更新统计
    stats_.processedFrames++;

    // 7. 更新最后收到文件时间
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    return true;
}
```

---

## GPS 坐标计算方案

### 7.1 EXIF 元数据解析

使用 `libexif` 库解析 JPEG 图片的 EXIF 数据：

```cpp
bool MediaFileTask::parseExif(const std::string& filePath, FileInfo& fileInfo) {
    ExifData* exifData = exif_data_new_from_file(filePath.c_str());
    if (!exifData) {
        logger_.error("无法读取 EXIF 数据: " + filePath);
        return false;
    }

    // 解析 GPS 坐标
    ExifEntry* latEntry = exif_data_get_entry(exifData, EXIF_TAG_GPS_LATITUDE);
    ExifEntry* lonEntry = exif_data_get_entry(exifData, EXIF_TAG_GPS_LONGITUDE);
    ExifEntry* altEntry = exif_data_get_entry(exifData, EXIF_TAG_GPS_ALTITUDE);

    if (latEntry && lonEntry) {
        fileInfo.latitude = parseGpsCoordinate(latEntry);
        fileInfo.longitude = parseGpsCoordinate(lonEntry);
    }

    if (altEntry) {
        fileInfo.altitude = parseGpsAltitude(altEntry);
    }

    // 解析拍摄时间
    ExifEntry* timeEntry = exif_data_get_entry(exifData, EXIF_TAG_DATE_TIME_ORIGINAL);
    if (timeEntry) {
        fileInfo.timestamp = std::string((char*)timeEntry->data);
    }

    exif_data_free(exifData);
    return true;
}
```

### 7.2 HTTP 批量 GPS 计算

```cpp
bool MediaFileTask::calculateGpsBatch(std::vector<FileInfo>& fileInfos) {
    // 1. 构建批量请求
    nlohmann::json request = nlohmann::json::array();

    for (const auto& fileInfo : fileInfos) {
        nlohmann::json item;
        item["latitude"] = fileInfo.latitude;
        item["longitude"] = fileInfo.longitude;
        item["altitude"] = fileInfo.altitude;
        item["heading"] = fileInfo.heading;
        request.push_back(item);
    }

    // 2. HTTP 请求
    nlohmann::json response;
    if (!requestGpsCalculation(request, response)) {
        logger_.error("GPS 计算请求失败");
        return false;
    }

    // 3. 解析结果
    if (!response.is_array() || response.size() != fileInfos.size()) {
        logger_.error("GPS 计算响应格式错误");
        return false;
    }

    for (size_t i = 0; i < fileInfos.size(); ++i) {
        fileInfos[i].gpsX = response[i]["x"].get<double>();
        fileInfos[i].gpsY = response[i]["y"].get<double>();
        fileInfos[i].gpsCalculated = true;
    }

    return true;
}

bool MediaFileTask::requestGpsCalculation(const nlohmann::json& exifData,
                                          nlohmann::json& gpsResults) {
    // 使用 HTTP Client 发送 POST 请求
    auto& httpClient = http::HttpClient::getInstance();

    std::string url = config_.gpsCalculationUrl;  // 从配置读取
    std::string requestBody = exifData.dump();

    auto response = httpClient.post(url, requestBody);
    if (!response.has_value()) {
        return false;
    }

    gpsResults = nlohmann::json::parse(response.value());
    return true;
}
```

---

## 事件缓存与重试机制

### 8.1 本地缓存设计

```cpp
void MediaFileTask::cacheEvent(const nlohmann::json& event) {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    // 1. 添加到内存缓存
    cachedEvents_.push_back(event);

    // 2. 持久化到文件（可选）
    std::ofstream ofs(cacheFilePath_, std::ios::app);
    if (ofs.is_open()) {
        ofs << event.dump() << "\n";
        ofs.close();
    }

    logger_.warning("事件已缓存: " + event.dump());
}

void MediaFileTask::retryCachedEvents() {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    logger_.info("重试缓存事件: " + std::to_string(cachedEvents_.size()) + " 条");

    for (auto it = cachedEvents_.begin(); it != cachedEvents_.end();) {
        if (publishEvent(*it)) {
            // 成功，移除
            it = cachedEvents_.erase(it);
        } else {
            // 失败，保留
            ++it;
        }
    }

    logger_.info("剩余缓存事件: " + std::to_string(cachedEvents_.size()) + " 条");
}
```

### 8.2 MQTT 重连监听

```cpp
void MediaFileTask::onMqttReconnected() {
    logger_.info("MQTT 重连成功，开始重试缓存事件");
    retryCachedEvents();
}
```

---

## 序列图

### 9.1 完整生命周期序列图

```
Platform    MqttHandler    TaskManager    MediaFileTask    DJI_SDK    TaskService    MQTT
   │             │              │                │             │            │          │
   ├─enable──────→│              │                │             │            │          │
   │             ├─startTask────→│                │             │            │          │
   │             │              ├─createTask──────→│             │            │          │
   │             │              │                ├─start()      │            │          │
   │             │              │                ├─Register─────→│            │          │
   │             │              │                ├─启动工作线程    │            │          │
   │             │              │                ├─启动监控线程    │            │          │
   │             │              │                │             │            │          │
   │             │              │                │◄─onFile─────┤            │          │
   │             │              │                ├─processFile  │            │          │
   │             │              │                ├─detect───────┼────────────→│          │
   │             │              │                │◄─────────────┼────────────┤          │
   │             │              │                ├─publish──────┼────────────┼──────────→│
   │             │              │                │             │            │          │
   ├─task_end────→│              │                │             │            │          │
   │             ├─onTaskEnd────┼────────────────→│             │            │          │
   │             │              │                ├─taskEnded=true            │          │
   │             │              │                │  (继续运行)    │            │          │
   │             │              │                │             │            │          │
   │             │              │                ├─60秒无新文件   │            │          │
   │             │              │                ├─complete─────┼────────────┼──────────→│
   │◄─analysis_result───────────┼────────────────┤             │            │          │
   │             │              │                │             │            │          │
   ├─disable─────→│              │                │             │            │          │
   │             ├─stopTask─────→│                │             │            │          │
   │             │              ├─stop()──────────→│             │            │          │
   │             │              │                ├─cleanup      │            │          │
   │             │              │                ├─Unregister───→│            │          │
   │             │              │                │             │            │          │
```

---

## 状态机设计

### 10.1 状态转换图

```
              start()
   IDLE ──────────────→ RUNNING
                            │
                            │ pause()
                            ↓
                         PAUSED
                            │
                            │ resume()
                            ↓
                         RUNNING
                            │
                            │ shouldComplete() == true
                            │ (taskEnded_ + 60秒无新文件)
                            ↓
                        COMPLETED
                            │
                            │ stop()
                            ↓
                         CANCELLED
```

### 10.2 状态代码实现

```cpp
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
        logger_.info("超时条件满足: taskEnded=true, 无新文件=" +
                    std::to_string(elapsed) + "秒");
        return true;
    }

    return false;
}

void MediaFileTask::monitorLoop() {
    logger_.info("监控线程已启动");

    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (shouldComplete()) {
            logger_.info("任务自动完成条件满足");
            completeTask();
            break;
        }
    }

    logger_.info("监控线程已退出");
}

void MediaFileTask::completeTask() {
    // 1. 推送完成消息
    publishTaskAnalysisResult(true);

    // 2. 更新状态
    state_ = TaskState::COMPLETED;
    notifyStateChanged(TaskState::COMPLETED);

    // 3. 停止工作线程
    running_ = false;
    queueCv_.notify_all();

    logger_.info("✅ 任务已自动完成，等待 device_algorithm_disable");
}
```

---

## 分阶段开发计划

### Day 10: 基础架构 ✅

**目标**: 创建 MediaFileTask 骨架，实现生命周期

**任务清单**:

- [x] 创建 `MediaFileTask.h`
- [x] 创建 `MediaFileTask.cpp`
- [x] 实现 `start()` / `stop()` / `onTaskEnd()`
- [x] 实现文件队列 (fileQueue\_)
- [x] 注册 MediaManager 回调
- [x] 在 TaskManager 中添加工厂分支
- [x] 编译验证

**验收标准**:

- ✅ 代码编译通过
- ✅ 可以创建 MediaFileTask 实例
- ✅ start() / stop() 正常工作
- ✅ 日志输出正确

---

### Day 11: 检测和事件生成

**目标**: 实现图片检测和事件构建

**任务清单**:

- [ ] 实现 `processFile()`
- [ ] 实现 EXIF 解析 (`parseExif()`)
- [ ] 调用 `TaskService::processFrame()`
- [ ] 实现 `buildEvent()` (按算法类型分组)
- [ ] 实现 base64 编码（复用 TaskService）
- [ ] 单元测试

**验收标准**:

- ✅ 可以读取图片并解析 EXIF
- ✅ 检测结果正确
- ✅ 事件 JSON 格式正确

---

### Day 12: GPS 计算和 MQTT 推送

**目标**: 实现 GPS 批量计算和事件推送

**任务清单**:

- [ ] 实现 `calculateGpsBatch()`
- [ ] 实现 `requestGpsCalculation()` (HTTP 请求)
- [ ] 实现 `publishEvent()`
- [ ] 实现 `shouldComplete()` 超时逻辑
- [ ] 实现 `completeTask()` 自动完成
- [ ] 集成测试

**验收标准**:

- ✅ GPS 计算准确
- ✅ MQTT 事件推送成功
- ✅ 60 秒无新文件自动完成

---

### Day 13: 事件缓存和重试机制

**目标**: 实现事件缓存和 MQTT 重连重试

**任务清单**:

- [ ] 实现 `cacheEvent()` 本地缓存
- [ ] 实现 `retryCachedEvents()` 重试逻辑
- [ ] 监听 MQTT 重连事件
- [ ] 实现 `cleanupFiles()` 文件清理
- [ ] 实现 `restoreDjiSettings()`
- [ ] 压力测试

**验收标准**:

- ✅ MQTT 断线时事件缓存
- ✅ MQTT 重连后自动重试
- ✅ 文件正确清理
- ✅ DJI 设置恢复

---

### Day 14: 集成测试和文档完善

**目标**: 端到端测试，完善文档

**任务清单**:

- [ ] 端到端测试（模拟完整航线任务）
- [ ] 性能测试（100 张图片）
- [ ] 异常测试（网络断线、文件损坏）
- [ ] 更新改进记录
- [ ] 更新八股文档
- [ ] Code Review

**验收标准**:

- ✅ 所有测试通过
- ✅ 文档完善
- ✅ 代码规范

---

## 测试方案

### 12.1 单元测试

```cpp
// test_mediafile_task.cpp
TEST(MediaFileTaskTest, Lifecycle) {
    auto config = createTestConfig();
    auto service = std::make_shared<TaskService>(...);

    auto task = std::make_shared<MediaFileTask>(config, service);

    // 测试启动
    ASSERT_TRUE(task->start());
    ASSERT_EQ(task->getState(), TaskState::RUNNING);

    // 测试 onTaskEnd
    task->onTaskEnd();
    ASSERT_TRUE(task->taskEnded_.load());
    ASSERT_EQ(task->getState(), TaskState::RUNNING);  // 仍在运行

    // 测试停止
    task->stop();
    ASSERT_EQ(task->getState(), TaskState::COMPLETED);
}

TEST(MediaFileTaskTest, FileProcessing) {
    auto task = createTask();
    task->start();

    // 模拟文件回调
    FileInfo fileInfo;
    fileInfo.filePath = "test.jpg";
    fileInfo.latitude = 23.123;
    fileInfo.longitude = 113.456;

    ASSERT_TRUE(task->processFile(fileInfo));
}
```

### 12.2 集成测试

```cpp
TEST(MediaFileTaskIntegrationTest, FullWorkflow) {
    // 1. 启动任务
    auto task = createTask();
    task->start();

    // 2. 模拟 10 张照片
    for (int i = 0; i < 10; ++i) {
        simulateFileReceived("photo_" + std::to_string(i) + ".jpg");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 3. 发送 task_end
    task->onTaskEnd();

    // 4. 等待超时（60秒）
    std::this_thread::sleep_for(std::chrono::seconds(65));

    // 5. 验证任务自动完成
    ASSERT_EQ(task->getState(), TaskState::COMPLETED);
    ASSERT_TRUE(taskAnalysisResultPublished_);
}
```

---

## 风险与注意事项

### 13.1 潜在风险

| 风险             | 影响                       | 缓解措施                       |
| ---------------- | -------------------------- | ------------------------------ |
| **文件传输延迟** | 任务提前完成，丢失后续照片 | 60 秒超时 + taskEnded\_ 双条件 |
| **MQTT 断线**    | 事件丢失                   | 本地缓存 + 重连重试            |
| **GPS 计算失败** | 坐标错误                   | HTTP 重试 + 降级处理           |
| **内存溢出**     | 队列无限增长               | maxQueueSize\_ 限制            |
| **线程安全**     | 数据竞争                   | 互斥锁 + 原子变量              |

### 13.2 注意事项

1. **文件清理时机**:

   - ✅ 在 `stop()` 中清理
   - ❌ 不在 `onTaskEnd()` 中清理（任务还在运行）

2. **DJI 设置恢复**:

   ```cpp
   // 启动时
   SetDroneNestAutoDelete(false);

   // 停止时
   SetDroneNestAutoDelete(true);
   ```

3. **线程安全**:

   - `taskEnded_`: 使用 `std::atomic<bool>`
   - `fileQueue_`: 使用 `std::mutex + std::condition_variable`
   - `lastFileTime_`: 使用 `std::mutex` 保护

4. **日志输出**:
   - 文件接收: `INFO`
   - 检测结果: `DEBUG`
   - GPS 计算: `INFO`
   - 超时检测: `INFO`
   - 错误: `ERROR`

---

**创建者**: AI Assistant  
**审核者**: TBD  
**最后更新**: 2025-11-20  
**版本**: v1.0  
**状态**: ✅ 设计完成，待实施
