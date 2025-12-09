# Day 5 开发方案 - 任务运行时功能

**日期**: 2025-11-09  
**目标**: 实现任务的启动、停止、暂停、恢复功能  
**预计时间**: 6-8 小时  
**前置条件**: Day 4 已完成 (任务创建和配置解析)

---

## 📋 目录

1. [开发目标](#开发目标)
2. [架构设计](#架构设计)
3. [详细实现步骤](#详细实现步骤)
4. [测试方案](#测试方案)
5. [验收标准](#验收标准)
6. [常见问题](#常见问题)
7. [参考资料](#参考资料)

---

## 🎯 开发目标

### 核心功能

- ✅ **任务启动**: 连接视频源，开始处理帧
- ✅ **任务停止**: 断开连接，释放资源
- ✅ **任务暂停**: 暂停帧处理，保持连接
- ✅ **任务恢复**: 从暂停状态继续处理

### 非功能需求

- ✅ **线程安全**: 多线程环境下状态一致
- ✅ **资源管理**: RAII 自动释放资源
- ✅ **异常安全**: 错误时不泄漏资源
- ✅ **可测试性**: 便于单元测试和集成测试

---

## 🏗️ 架构设计

### 1. 状态机设计

```
状态转换图:
┌─────────────┐
│   CREATED   │ ← 初始状态 (构造函数)
└──────┬──────┘
       │
       │ start()
       ▼
┌─────────────┐
│   RUNNING   │ ← 正在处理视频帧
└──┬───────┬──┘
   │       │
pause()  stop()
   │       │
   ▼       │
┌─────────────┐  │
│   PAUSED    │  │
└──────┬──────┘  │
       │         │
   resume()     │
       │         │
       ▼         ▼
   (RUNNING) ┌─────────────┐
             │   STOPPED   │ ← 最终状态
             └─────────────┘

状态说明:
- CREATED:  任务已创建,未启动 (start() 前)
- RUNNING:  任务正在运行,处理视频帧
- PAUSED:   任务已暂停,线程等待
- STOPPED:  任务已停止,线程退出
```

### 2. 类设计

#### 2.1 任务状态枚举

```cpp
// include/esdk_sophon/task/TaskTypes.h
namespace esdk_sophon {
namespace task {

/**
 * @brief 任务状态
 *
 * 状态转换:
 * CREATED → RUNNING → PAUSED → RUNNING → STOPPED
 */
enum class TaskState {
    CREATED,    ///< 已创建,未启动
    RUNNING,    ///< 正在运行
    PAUSED,     ///< 已暂停
    STOPPED     ///< 已停止
};

/**
 * @brief 任务统计信息
 */
struct TaskStatistics {
    uint64_t processedFrames = 0;   ///< 已处理帧数
    uint64_t detectedObjects = 0;   ///< 检测到的目标数
    uint64_t reportedEvents = 0;    ///< 上报的事件数
    double averageFps = 0.0;        ///< 平均帧率
    std::chrono::milliseconds uptime{0};  ///< 运行时长
};

}  // namespace task
}  // namespace esdk_sophon
```

#### 2.2 ITask 接口定义

```cpp
// include/esdk_sophon/task/ITask.h
#pragma once

#include <string>
#include <atomic>
#include "TaskTypes.h"

namespace esdk_sophon {
namespace task {

/**
 * @brief 任务接口基类
 *
 * 定义了所有任务类型的通用接口:
 * - 生命周期管理: start(), stop(), pause(), resume()
 * - 状态查询: isRunning(), isPaused(), getState()
 * - 信息查询: getTaskId(), getStatistics()
 */
class ITask {
public:
    virtual ~ITask() = default;

    // ========== 生命周期管理 ==========

    /**
     * @brief 启动任务
     *
     * 启动视频处理线程,连接视频源,开始处理帧
     *
     * @return true 启动成功
     * @return false 启动失败 (已在运行/配置错误)
     *
     * @note 线程安全
     * @note 只能从 CREATED 状态调用
     */
    virtual bool start() = 0;

    /**
     * @brief 停止任务
     *
     * 停止视频处理线程,断开视频源,释放资源
     *
     * @note 线程安全
     * @note 阻塞直到线程退出 (最多等待5秒)
     * @note 可以从任何状态调用
     */
    virtual void stop() = 0;

    /**
     * @brief 暂停任务
     *
     * 暂停帧处理,但保持视频源连接
     *
     * @note 线程安全
     * @note 只能从 RUNNING 状态调用
     * @note LiveStreamTask: 暂停期间仍读取帧但不处理 (避免流断开)
     * @note MediaFileTask: 完全暂停,可以恢复到当前位置
     */
    virtual void pause() = 0;

    /**
     * @brief 恢复任务
     *
     * 从暂停状态恢复帧处理
     *
     * @note 线程安全
     * @note 只能从 PAUSED 状态调用
     */
    virtual void resume() = 0;

    // ========== 状态查询 ==========

    /**
     * @brief 检查任务是否正在运行
     *
     * @return true 任务正在运行 (状态 == RUNNING)
     * @return false 其他状态
     *
     * @note 线程安全
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief 检查任务是否已暂停
     *
     * @return true 任务已暂停 (状态 == PAUSED)
     * @return false 其他状态
     *
     * @note 线程安全
     */
    virtual bool isPaused() const = 0;

    /**
     * @brief 获取任务当前状态
     *
     * @return TaskState 当前状态
     *
     * @note 线程安全
     */
    virtual TaskState getState() const = 0;

    // ========== 信息查询 ==========

    /**
     * @brief 获取任务ID
     *
     * @return const std::string& 任务ID
     */
    virtual const std::string& getTaskId() const = 0;

    /**
     * @brief 获取任务统计信息
     *
     * @return TaskStatistics 统计信息 (拷贝)
     *
     * @note 线程安全
     */
    virtual TaskStatistics getStatistics() const = 0;
};

}  // namespace task
}  // namespace esdk_sophon
```

#### 2.3 LiveStreamTask 实现框架

```cpp
// include/esdk_sophon/task/LiveStreamTask.h
#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <opencv2/opencv.hpp>
#include "ITask.h"

namespace esdk_sophon {
namespace task {

/**
 * @brief 直播流检测任务
 *
 * 从 RTSP/RTMP 实时视频流中检测目标
 *
 * 特点:
 * - 实时流: 不能 seek,必须持续读取避免断流
 * - 暂停时: 继续读取帧但不处理 (丢弃)
 * - 断流重连: 自动重连机制
 */
class LiveStreamTask : public ITask {
public:
    /**
     * @brief 构造函数
     *
     * @param config 任务配置
     * @param service 任务服务 (用于MQTT通信)
     */
    LiveStreamTask(const TaskConfig& config,
                   std::shared_ptr<TaskService> service);

    /**
     * @brief 析构函数
     *
     * 自动调用 stop() 确保线程退出
     */
    ~LiveStreamTask() override;

    // 禁止拷贝和移动
    LiveStreamTask(const LiveStreamTask&) = delete;
    LiveStreamTask& operator=(const LiveStreamTask&) = delete;
    LiveStreamTask(LiveStreamTask&&) = delete;
    LiveStreamTask& operator=(LiveStreamTask&&) = delete;

    // ========== ITask 接口实现 ==========

    bool start() override;
    void stop() override;
    void pause() override;
    void resume() override;

    bool isRunning() const override;
    bool isPaused() const override;
    TaskState getState() const override;

    const std::string& getTaskId() const override;
    TaskStatistics getStatistics() const override;

private:
    // ========== 内部方法 ==========

    /**
     * @brief 视频处理线程主函数
     *
     * 循环执行:
     * 1. 读取视频帧
     * 2. 检查暂停标志
     * 3. 检测处理
     * 4. 上报结果
     */
    void processLoop();

    /**
     * @brief 连接视频流
     *
     * @return true 连接成功
     * @return false 连接失败
     */
    bool connectStream();

    /**
     * @brief 断开视频流
     */
    void disconnectStream();

    /**
     * @brief 处理单帧
     *
     * @param frame 视频帧
     * @return true 处理成功
     * @return false 处理失败
     */
    bool processFrame(const cv::Mat& frame);

    /**
     * @brief 设置任务状态
     *
     * @param newState 新状态
     *
     * @note 线程安全,使用原子操作
     */
    void setState(TaskState newState);

    // ========== 成员变量 ==========

    // 配置和服务
    TaskConfig config_;                          ///< 任务配置
    std::shared_ptr<TaskService> service_;       ///< 任务服务

    // 状态管理
    std::atomic<TaskState> state_;               ///< 任务状态 (原子变量,线程安全)
    std::atomic<bool> shouldStop_;               ///< 停止标志

    // 暂停/恢复控制
    std::mutex pauseMutex_;                      ///< 暂停互斥锁
    std::condition_variable pauseCond_;          ///< 暂停条件变量
    std::atomic<bool> isPausedFlag_;             ///< 暂停标志

    // 线程管理
    std::unique_ptr<std::thread> processThread_; ///< 处理线程

    // 视频流
    std::unique_ptr<cv::VideoCapture> capture_;  ///< OpenCV 视频捕获

    // 检测器
    std::shared_ptr<vision::IDetector> detector_;///< 目标检测器

    // 统计信息
    mutable std::mutex statsMutex_;              ///< 统计信息互斥锁
    TaskStatistics stats_;                       ///< 统计信息
    std::chrono::steady_clock::time_point startTime_;  ///< 启动时间
};

}  // namespace task
}  // namespace esdk_sophon
```

---

## 📝 详细实现步骤

### Step 1: 更新头文件 (30 分钟)

#### 1.1 更新 TaskTypes.h

```bash
# 编辑文件
vim include/esdk_sophon/task/TaskTypes.h
```

添加:

1. `TaskState` 枚举
2. `TaskStatistics` 结构体

#### 1.2 更新 ITask.h

```bash
vim include/esdk_sophon/task/ITask.h
```

添加接口方法 (参考上面的设计)

#### 1.3 更新 LiveStreamTask.h

```bash
vim include/esdk_sophon/task/LiveStreamTask.h
```

添加:

1. 成员变量 (线程、互斥锁、条件变量等)
2. 私有方法声明

---

### Step 2: 实现 LiveStreamTask::start() (2 小时)

#### 2.1 实现代码

```cpp
// src/task/LiveStreamTask.cpp

bool LiveStreamTask::start() {
    // 1. 检查状态
    TaskState expected = TaskState::CREATED;
    if (state_.load() != expected) {
        Logger::warn("任务 {} 已在运行或已停止,无法启动", config_.taskId);
        return false;
    }

    // 2. 重置标志
    shouldStop_.store(false);
    isPausedFlag_.store(false);

    // 3. 连接视频流
    if (!connectStream()) {
        Logger::error("任务 {} 连接视频流失败", config_.taskId);
        return false;
    }

    // 4. 初始化检测器
    try {
        detector_ = vision::DetectorFactory::createDetector(config_);
        if (!detector_ || !detector_->initialize()) {
            Logger::error("任务 {} 初始化检测器失败", config_.taskId);
            disconnectStream();
            return false;
        }
    } catch (const std::exception& e) {
        Logger::error("任务 {} 创建检测器异常: {}", config_.taskId, e.what());
        disconnectStream();
        return false;
    }

    // 5. 启动处理线程
    processThread_ = std::make_unique<std::thread>(&LiveStreamTask::processLoop, this);

    // 6. 更新状态
    setState(TaskState::RUNNING);
    startTime_ = std::chrono::steady_clock::now();

    Logger::info("任务 {} 启动成功", config_.taskId);
    return true;
}
```

#### 2.2 实现 connectStream()

```cpp
bool LiveStreamTask::connectStream() {
    // 1. 构造流 URL
    // config_.streamUrl 应该是 "rtsp://192.168.1.100:554/stream"
    const std::string& streamUrl = config_.streamUrl;

    Logger::info("任务 {} 连接视频流: {}", config_.taskId, streamUrl);

    // 2. 创建 VideoCapture
    capture_ = std::make_unique<cv::VideoCapture>();

    // 3. 设置参数 (可选)
    capture_->set(cv::CAP_PROP_BUFFERSIZE, 3);  // 减小缓冲,降低延迟

    // 4. 打开流
    if (!capture_->open(streamUrl)) {
        Logger::error("任务 {} 无法打开视频流: {}", config_.taskId, streamUrl);
        capture_.reset();
        return false;
    }

    // 5. 验证流已打开
    if (!capture_->isOpened()) {
        Logger::error("任务 {} 视频流未正确打开: {}", config_.taskId, streamUrl);
        capture_.reset();
        return false;
    }

    // 6. 获取流信息
    int width = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = capture_->get(cv::CAP_PROP_FPS);

    Logger::info("任务 {} 视频流信息: {}x{} @ {:.1f}fps",
                 config_.taskId, width, height, fps);

    return true;
}
```

#### 2.3 实现 processLoop() - 核心处理循环

```cpp
void LiveStreamTask::processLoop() {
    Logger::info("任务 {} 处理线程启动 (线程ID: {})",
                 config_.taskId, std::this_thread::get_id());

    cv::Mat frame;
    uint64_t frameCount = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();
    int fpsFrameCount = 0;

    while (!shouldStop_.load()) {
        // ========== 1. 检查暂停状态 ==========
        if (isPausedFlag_.load()) {
            // 暂停时等待恢复信号
            std::unique_lock<std::mutex> lock(pauseMutex_);
            pauseCond_.wait(lock, [this]() {
                return !isPausedFlag_.load() || shouldStop_.load();
            });

            // 被唤醒后检查是否需要停止
            if (shouldStop_.load()) {
                break;
            }

            Logger::info("任务 {} 从暂停恢复", config_.taskId);
            continue;
        }

        // ========== 2. 读取视频帧 ==========
        if (!capture_ || !capture_->read(frame)) {
            Logger::warn("任务 {} 读取帧失败,尝试重连...", config_.taskId);

            // 尝试重连 (实时流可能断开)
            disconnectStream();
            std::this_thread::sleep_for(std::chrono::seconds(2));

            if (!connectStream()) {
                Logger::error("任务 {} 重连失败,停止任务", config_.taskId);
                break;
            }

            continue;
        }

        // ========== 3. 验证帧有效性 ==========
        if (frame.empty()) {
            Logger::warn("任务 {} 读取到空帧,跳过", config_.taskId);
            continue;
        }

        // ========== 4. 处理帧 ==========
        try {
            if (processFrame(frame)) {
                frameCount++;
                fpsFrameCount++;

                // 更新统计信息
                {
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.processedFrames = frameCount;
                }
            }
        } catch (const std::exception& e) {
            Logger::error("任务 {} 处理帧异常: {}", config_.taskId, e.what());
        }

        // ========== 5. 计算 FPS (每秒更新一次) ==========
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastFpsTime
        ).count();

        if (elapsed >= 1) {
            double fps = static_cast<double>(fpsFrameCount) / elapsed;

            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.averageFps = fps;
            }

            Logger::debug("任务 {} FPS: {:.1f}", config_.taskId, fps);

            lastFpsTime = now;
            fpsFrameCount = 0;
        }

        // ========== 6. 控制帧率 (可选) ==========
        // 如果检测速度过快,可以限制帧率避免CPU占用过高
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Logger::info("任务 {} 处理线程退出", config_.taskId);
}
```

#### 2.4 实现 processFrame()

```cpp
bool LiveStreamTask::processFrame(const cv::Mat& frame) {
    // 1. 调用检测器
    auto results = detector_->detect(frame);

    if (results.empty()) {
        return true;  // 无检测结果也算处理成功
    }

    // 2. 更新统计
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.detectedObjects += results.size();
    }

    // 3. 过滤结果 (根据配置的事件类型)
    // TODO: 实现事件过滤逻辑

    // 4. 上报到 MQTT
    if (service_) {
        try {
            service_->reportDetectionResult(config_.taskId, results);

            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.reportedEvents++;
            }
        } catch (const std::exception& e) {
            Logger::error("任务 {} 上报结果失败: {}", config_.taskId, e.what());
            return false;
        }
    }

    return true;
}
```

---

### Step 3: 实现 LiveStreamTask::stop() (1 小时)

```cpp
void LiveStreamTask::stop() {
    TaskState currentState = state_.load();

    // 如果已经停止,直接返回
    if (currentState == TaskState::STOPPED) {
        Logger::warn("任务 {} 已处于停止状态", config_.taskId);
        return;
    }

    Logger::info("任务 {} 正在停止...", config_.taskId);

    // 1. 设置停止标志
    shouldStop_.store(true);

    // 2. 如果任务处于暂停状态,先唤醒线程
    if (currentState == TaskState::PAUSED) {
        isPausedFlag_.store(false);
        pauseCond_.notify_all();  // 唤醒所有等待线程
    }

    // 3. 等待处理线程退出 (最多5秒)
    if (processThread_ && processThread_->joinable()) {
        auto future = std::async(std::launch::async, [this]() {
            processThread_->join();
        });

        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            Logger::error("任务 {} 处理线程5秒内未退出,强制分离", config_.taskId);
            processThread_->detach();  // 避免 std::terminate
        }
    }

    // 4. 释放资源
    processThread_.reset();
    disconnectStream();
    detector_.reset();

    // 5. 更新状态
    setState(TaskState::STOPPED);

    Logger::info("任务 {} 已停止", config_.taskId);
}
```

#### 实现 disconnectStream()

```cpp
void LiveStreamTask::disconnectStream() {
    if (capture_) {
        capture_->release();
        capture_.reset();
        Logger::debug("任务 {} 视频流已断开", config_.taskId);
    }
}
```

---

### Step 4: 实现 pause/resume (1 小时)

#### 4.1 实现 pause()

```cpp
void LiveStreamTask::pause() {
    TaskState expected = TaskState::RUNNING;
    if (state_.load() != expected) {
        Logger::warn("任务 {} 不在运行状态,无法暂停", config_.taskId);
        return;
    }

    Logger::info("任务 {} 暂停", config_.taskId);

    // 设置暂停标志
    isPausedFlag_.store(true);

    // 更新状态
    setState(TaskState::PAUSED);
}
```

#### 4.2 实现 resume()

```cpp
void LiveStreamTask::resume() {
    TaskState expected = TaskState::PAUSED;
    if (state_.load() != expected) {
        Logger::warn("任务 {} 不在暂停状态,无法恢复", config_.taskId);
        return;
    }

    Logger::info("任务 {} 恢复", config_.taskId);

    // 清除暂停标志
    isPausedFlag_.store(false);

    // 唤醒处理线程
    pauseCond_.notify_one();

    // 更新状态
    setState(TaskState::RUNNING);
}
```

---

### Step 5: 实现状态查询方法 (30 分钟)

```cpp
bool LiveStreamTask::isRunning() const {
    return state_.load() == TaskState::RUNNING;
}

bool LiveStreamTask::isPaused() const {
    return state_.load() == TaskState::PAUSED;
}

TaskState LiveStreamTask::getState() const {
    return state_.load();
}

const std::string& LiveStreamTask::getTaskId() const {
    return config_.taskId;
}

TaskStatistics LiveStreamTask::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    // 计算运行时长
    TaskStatistics stats = stats_;
    if (state_.load() == TaskState::RUNNING || state_.load() == TaskState::PAUSED) {
        auto now = std::chrono::steady_clock::now();
        stats.uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime_
        );
    }

    return stats;
}

void LiveStreamTask::setState(TaskState newState) {
    state_.store(newState);
    Logger::debug("任务 {} 状态: {} → {}",
                  config_.taskId,
                  static_cast<int>(state_.load()),
                  static_cast<int>(newState));
}
```

---

### Step 6: 实现 MediaFileTask (1 小时)

MediaFileTask 与 LiveStreamTask 的主要区别:

```cpp
// MediaFileTask 的特殊实现

bool MediaFileTask::connectStream() {
    // 打开视频文件而非网络流
    const std::string& filePath = config_.mediaFilePath;

    Logger::info("任务 {} 打开视频文件: {}", config_.taskId, filePath);

    capture_ = std::make_unique<cv::VideoCapture>();

    if (!capture_->open(filePath)) {
        Logger::error("任务 {} 无法打开文件: {}", config_.taskId, filePath);
        capture_.reset();
        return false;
    }

    // 获取总帧数
    totalFrames_ = static_cast<uint64_t>(
        capture_->get(cv::CAP_PROP_FRAME_COUNT)
    );

    Logger::info("任务 {} 视频文件总帧数: {}", config_.taskId, totalFrames_);

    return true;
}

void MediaFileTask::processLoop() {
    // ... 与 LiveStreamTask 类似

    while (!shouldStop_.load()) {
        // ... 读取帧 ...

        // 特殊处理: 文件读完后自动停止
        if (frameCount >= totalFrames_) {
            Logger::info("任务 {} 处理完所有帧,自动停止", config_.taskId);
            break;
        }

        // ... 处理帧 ...
    }
}
```

---

### Step 7: 更新构造函数和析构函数 (30 分钟)

```cpp
// 构造函数
LiveStreamTask::LiveStreamTask(const TaskConfig& config,
                               std::shared_ptr<TaskService> service)
    : config_(config)
    , service_(service)
    , state_(TaskState::CREATED)
    , shouldStop_(false)
    , isPausedFlag_(false)
{
    Logger::info("创建 LiveStreamTask: {}", config_.taskId);
}

// 析构函数 - RAII 自动清理
LiveStreamTask::~LiveStreamTask() {
    Logger::info("销毁 LiveStreamTask: {}", config_.taskId);

    // 确保线程停止
    stop();
}
```

---

## 🧪 测试方案

### 测试文件: tests/test_task_runtime.cpp

```cpp
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "esdk_sophon/task/LiveStreamTask.h"
#include "esdk_sophon/task/MediaFileTask.h"
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/Mqtt/MqttClient.h"

using namespace esdk_sophon;
using namespace esdk_sophon::task;

// ========== 测试1: LiveStreamTask 生命周期 ==========
void test_livestream_lifecycle() {
    std::cout << "\n========== 测试1: LiveStreamTask 生命周期 ==========" << std::endl;

    // 1. 准备配置
    TaskConfig config;
    config.taskId = "test_livestream_001";
    config.type = TaskType::DETECTION_LIVESTREAM;
    config.streamUrl = "rtsp://192.168.1.100:554/stream";  // 替换为实际地址

    // 2. 准备服务
    auto mqtt = mqtt::MqttClient::getSharedInstance();
    auto service = std::make_shared<TaskService>(mqtt);

    // 3. 创建任务
    LiveStreamTask task(config, service);
    assert(task.getState() == TaskState::CREATED);
    std::cout << "✓ 任务创建成功,状态: CREATED" << std::endl;

    // 4. 启动任务
    assert(task.start() == true);
    assert(task.isRunning() == true);
    assert(task.getState() == TaskState::RUNNING);
    std::cout << "✓ 任务启动成功,状态: RUNNING" << std::endl;

    // 5. 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(3));

    auto stats = task.getStatistics();
    std::cout << "✓ 运行3秒: 处理帧数=" << stats.processedFrames
              << ", 平均FPS=" << stats.averageFps << std::endl;

    // 6. 暂停任务
    task.pause();
    assert(task.isPaused() == true);
    assert(task.getState() == TaskState::PAUSED);
    std::cout << "✓ 任务暂停成功,状态: PAUSED" << std::endl;

    // 7. 等待一段时间 (验证暂停期间不处理帧)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint64_t pausedFrames = task.getStatistics().processedFrames;
    std::cout << "✓ 暂停2秒: 帧数未增加 (仍为 " << pausedFrames << ")" << std::endl;

    // 8. 恢复任务
    task.resume();
    assert(task.isRunning() == true);
    assert(task.getState() == TaskState::RUNNING);
    std::cout << "✓ 任务恢复成功,状态: RUNNING" << std::endl;

    // 9. 再运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint64_t resumedFrames = task.getStatistics().processedFrames;
    assert(resumedFrames > pausedFrames);
    std::cout << "✓ 恢复后继续处理: 帧数增加到 " << resumedFrames << std::endl;

    // 10. 停止任务
    task.stop();
    assert(task.getState() == TaskState::STOPPED);
    std::cout << "✓ 任务停止成功,状态: STOPPED" << std::endl;

    std::cout << "✅ 测试1通过\n" << std::endl;
}

// ========== 测试2: MediaFileTask 生命周期 ==========
void test_mediafile_lifecycle() {
    std::cout << "\n========== 测试2: MediaFileTask 生命周期 ==========" << std::endl;

    // 准备配置
    TaskConfig config;
    config.taskId = "test_mediafile_001";
    config.type = TaskType::DETECTION_MEDIAFILE;
    config.mediaFilePath = "/path/to/test.mp4";  // 替换为实际文件

    auto mqtt = mqtt::MqttClient::getSharedInstance();
    auto service = std::make_shared<TaskService>(mqtt);

    // 创建并启动任务
    MediaFileTask task(config, service);
    assert(task.start() == true);

    // 运行
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 暂停
    task.pause();
    assert(task.isPaused() == true);

    // 恢复
    std::this_thread::sleep_for(std::chrono::seconds(1));
    task.resume();
    assert(task.isRunning() == true);

    // 停止
    task.stop();
    assert(task.getState() == TaskState::STOPPED);

    std::cout << "✅ 测试2通过\n" << std::endl;
}

// ========== 测试3: 异常情况处理 ==========
void test_error_handling() {
    std::cout << "\n========== 测试3: 异常情况处理 ==========" << std::endl;

    TaskConfig config;
    config.taskId = "test_error_001";
    config.type = TaskType::DETECTION_LIVESTREAM;
    config.streamUrl = "rtsp://invalid.url:554/stream";  // 无效地址

    auto mqtt = mqtt::MqttClient::getSharedInstance();
    auto service = std::make_shared<TaskService>(mqtt);

    LiveStreamTask task(config, service);

    // 启动失败应该返回 false
    assert(task.start() == false);
    assert(task.getState() == TaskState::CREATED);  // 保持初始状态
    std::cout << "✓ 连接失败时保持 CREATED 状态" << std::endl;

    // 尝试暂停未运行的任务 (应该被忽略)
    task.pause();
    assert(task.getState() == TaskState::CREATED);
    std::cout << "✓ 暂停未运行任务被正确忽略" << std::endl;

    // 停止未运行的任务 (应该安全)
    task.stop();
    assert(task.getState() == TaskState::STOPPED);
    std::cout << "✓ 停止未运行任务安全完成" << std::endl;

    std::cout << "✅ 测试3通过\n" << std::endl;
}

// ========== 测试4: RAII 自动清理 ==========
void test_raii() {
    std::cout << "\n========== 测试4: RAII 自动清理 ==========" << std::endl;

    {
        TaskConfig config;
        config.taskId = "test_raii_001";
        config.streamUrl = "rtsp://192.168.1.100:554/stream";

        auto mqtt = mqtt::MqttClient::getSharedInstance();
        auto service = std::make_shared<TaskService>(mqtt);

        LiveStreamTask task(config, service);
        task.start();

        std::this_thread::sleep_for(std::chrono::seconds(2));

        // 离开作用域时,析构函数应自动调用 stop()
        std::cout << "✓ 离开作用域,等待析构函数..." << std::endl;
    }  // ← task 析构,自动停止线程

    std::cout << "✓ 析构函数成功清理资源" << std::endl;
    std::cout << "✅ 测试4通过\n" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Day 5 任务运行时功能测试" << std::endl;
    std::cout << "========================================\n" << std::endl;

    try {
        test_livestream_lifecycle();
        test_mediafile_lifecycle();
        test_error_handling();
        test_raii();

        std::cout << "\n✅ 所有测试通过!" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ 测试失败: " << e.what() << std::endl;
        return 1;
    }
}
```

### 编译测试

```bash
# 1. 在 Docker 容器中编译
cd /workspace/ESDK_On_Sophon/build
make test_task_runtime

# 2. 部署到 SE7
scp bin/tests/test_task_runtime linaro@192.168.150.1:/home/linaro/

# 3. 在 SE7 上运行
ssh linaro@192.168.150.1
./test_task_runtime
```

---

## ✅ 验收标准

### 功能验收

- [ ] LiveStreamTask 可以启动、暂停、恢复、停止
- [ ] MediaFileTask 可以启动、暂停、恢复、停止
- [ ] 状态转换正确 (CREATED → RUNNING → PAUSED → RUNNING → STOPPED)
- [ ] 线程安全,无数据竞争 (使用 ThreadSanitizer 验证)
- [ ] 资源正确释放,无内存泄漏 (使用 Valgrind 验证)
- [ ] 异常情况正确处理 (连接失败、流断开等)

### 测试验收

- [ ] test_task_runtime.cpp 所有测试通过
- [ ] 在 SE7 设备上实际运行成功
- [ ] 日志输出清晰,便于调试
- [ ] 性能符合预期 (CPU < 50%, 内存稳定)

### 代码质量

- [ ] 遵循 C++17 标准
- [ ] 详细的中文注释
- [ ] 符合项目编码规范
- [ ] 通过 clang-tidy 静态检查

---

## ❓ 常见问题

### Q1: 为什么使用 std::atomic 而不是 mutex?

**答**:

- `std::atomic` 用于简单的标志变量 (如 `shouldStop_`, `isPausedFlag_`)
- 原子操作比互斥锁开销小,性能更好
- 对于复杂数据 (如 `stats_`) 仍然需要 `mutex` 保护

### Q2: 条件变量的虚假唤醒是什么?

**答**:

```cpp
// ❌ 错误: 虚假唤醒后可能继续运行
pauseCond_.wait(lock);

// ✅ 正确: 使用条件判断
pauseCond_.wait(lock, [this]() {
    return !isPausedFlag_.load() || shouldStop_.load();
});
```

条件变量可能在没有 notify 的情况下唤醒 (虚假唤醒),必须用 while 循环或 lambda 检查条件。

### Q3: 为什么 LiveStreamTask 暂停时仍要读取帧?

**答**:

- RTSP 实时流如果长时间不读取,服务器可能断开连接
- 暂停时读取帧但不处理 (丢弃),保持连接活跃
- MediaFileTask 可以完全暂停,因为文件不会"断开"

### Q4: 如何避免线程无法退出?

**答**:

```cpp
// 1. 使用超时等待
auto future = std::async(std::launch::async, [this]() {
    processThread_->join();
});

if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
    // 超时后 detach 避免 std::terminate
    processThread_->detach();
}

// 2. 确保唤醒所有等待线程
shouldStop_.store(true);
pauseCond_.notify_all();  // 唤醒所有等待的线程
```

### Q5: 如何调试多线程问题?

**答**:

```bash
# 1. 使用 ThreadSanitizer 检测数据竞争
export TSAN_OPTIONS="log_path=tsan.log"
./test_task_runtime

# 2. 使用 gdb 调试
gdb ./test_task_runtime
(gdb) info threads          # 查看所有线程
(gdb) thread 2              # 切换到线程2
(gdb) bt                    # 查看线程2的调用栈

# 3. 添加详细日志
Logger::debug("线程 {} 进入暂停等待", std::this_thread::get_id());
```

---

## 📚 参考资料

### C++ 多线程

- [八股-C++多线程.md](../interview/八股-C++多线程.md)
- [cppreference: std::thread](https://en.cppreference.com/w/cpp/thread/thread)
- [cppreference: std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable)

### OpenCV 视频处理

- [OpenCV VideoCapture 文档](https://docs.opencv.org/4.x/d8/dfe/classcv_1_1VideoCapture.html)
- RTSP 流处理最佳实践

### 设计模式

- 状态机模式 (State Pattern)
- 生产者-消费者模式

### 线程安全

- [C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
- ThreadSanitizer 使用指南

---

## 📝 开发日志模板

```markdown
## Day 5 开发日志

### 日期: 2025-11-09

#### 完成的功能

- [ ] Step 1: 更新头文件
- [ ] Step 2: 实现 start()
- [ ] Step 3: 实现 stop()
- [ ] Step 4: 实现 pause/resume
- [ ] Step 5: 状态查询方法
- [ ] Step 6: MediaFileTask
- [ ] Step 7: 测试

#### 遇到的问题

1. **问题**: ...
   **解决**: ...
2. **问题**: ...
   **解决**: ...

#### 新学到的知识点

- [ ] std::condition_variable 的使用
- [ ] 线程安全的停止机制
- [ ] ...

#### 明天计划

- ...
```

---

**文档版本**: v1.0  
**最后更新**: 2025-11-09  
**下次审查**: Day 5 完成后
