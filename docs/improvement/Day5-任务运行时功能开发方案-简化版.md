# Day 5 开发方案 - 任务运行时功能 (简化版)

**日期**: 2025-11-09  
**目标**: 实现任务的启动和停止功能  
**预计时间**: 4-5 小时  
**前置条件**: Day 4 已完成 (任务创建和配置解析)

> **💡 设计说明**: 本方案只实现 start/stop 功能，不实现 pause/resume。  
> **原因**: DJI ESDK 官方接口只有启动/停止，无人机无法控制视频流的暂停/恢复。  
> **扩展性**: 架构预留扩展接口，未来需要时可轻松添加 pause/resume（约 1 小时）。

---

## 📋 目录

1. [开发目标](#开发目标)
2. [架构设计](#架构设计)
3. [详细实现步骤](#详细实现步骤)
4. [测试方案](#测试方案)
5. [验收标准](#验收标准)
6. [常见问题](#常见问题)
7. [未来扩展](#未来扩展)
8. [参考资料](#参考资料)

---

## 🎯 开发目标

### 核心功能

- ✅ **任务启动**: 连接视频源，启动线程，开始处理帧
- ✅ **任务停止**: 停止线程，断开连接，释放资源

### 非功能需求

- ✅ **线程安全**: 多线程环境下状态一致
- ✅ **资源管理**: RAII 自动释放资源
- ✅ **异常安全**: 错误时不泄漏资源
- ✅ **可测试性**: 便于单元测试和集成测试
- ✅ **可扩展性**: 预留接口，未来可加 pause/resume

---

## 🏗️ 架构设计

### 1. 状态机设计（简化版）

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
└──────┬──────┘
       │
       │ stop()
       ▼
┌─────────────┐
│   STOPPED   │ ← 最终状态
└─────────────┘

状态说明:
- CREATED:  任务已创建，未启动 (start() 前)
- RUNNING:  任务正在运行，处理视频帧
- STOPPED:  任务已停止，线程退出

有效转换:
✅ CREATED → start() → RUNNING
✅ RUNNING → stop() → STOPPED
✅ CREATED → stop() → STOPPED (未启动也可以安全停止)

无效转换:
❌ RUNNING → start() (已在运行)
❌ STOPPED → start() (已停止，无法重启)
```

### 2. 类设计

#### 2.1 任务状态枚举（简化版）

```cpp
// include/esdk_sophon/task/TaskTypes.h
namespace esdk_sophon {
namespace task {

/**
 * @brief 任务状态（简化版）
 *
 * 状态转换:
 * CREATED → RUNNING → STOPPED
 *
 * @note 未来可扩展 PAUSED 状态
 */
enum class TaskState {
    CREATED,    ///< 已创建，未启动
    RUNNING,    ///< 正在运行
    STOPPED     ///< 已停止
    // PAUSED,  ///< 已暂停 (未来扩展)
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

#### 2.2 ITask 接口定义（简化版）

```cpp
// include/esdk_sophon/task/ITask.h
#pragma once

#include <string>
#include <atomic>
#include "TaskTypes.h"

namespace esdk_sophon {
namespace task {

/**
 * @brief 任务接口基类（简化版）
 *
 * 定义了所有任务类型的通用接口:
 * - 生命周期管理: start(), stop()
 * - 状态查询: isRunning(), getState()
 * - 信息查询: getTaskId(), getStatistics()
 *
 * @note 未来可扩展 pause(), resume() 方法
 */
class ITask {
public:
    virtual ~ITask() = default;

    // ========== 生命周期管理 ==========

    /**
     * @brief 启动任务
     *
     * 启动视频处理线程，连接视频源，开始处理帧
     *
     * @return true 启动成功
     * @return false 启动失败 (已在运行/配置错误)
     *
     * @note 线程安全
     * @note 只能从 CREATED 状态调用
     *
     * 启动流程:
     * 1. 检查状态（必须是 CREATED）
     * 2. 连接视频流（RTSP/文件）
     * 3. 初始化检测器
     * 4. 启动处理线程
     * 5. 更新状态为 RUNNING
     */
    virtual bool start() = 0;

    /**
     * @brief 停止任务
     *
     * 停止视频处理线程，断开视频源，释放资源
     *
     * @note 线程安全
     * @note 阻塞直到线程退出（最多等待 5 秒）
     * @note 可以从任何状态调用（幂等操作）
     *
     * 停止流程:
     * 1. 设置停止标志
     * 2. 等待线程退出（超时保护）
     * 3. 释放资源（视频流、检测器）
     * 4. 更新状态为 STOPPED
     */
    virtual void stop() = 0;

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
     * @brief 获取任务当前状态
     *
     * @return TaskState 当前状态
     *
     * @note 线程安全
     */
    virtual TaskState getState() const = 0;

    // ========== 信息查询 ==========

    /**
     * @brief 获取任务 ID
     *
     * @return const std::string& 任务 ID
     */
    virtual const std::string& getTaskId() const = 0;

    /**
     * @brief 获取任务统计信息
     *
     * @return TaskStatistics 统计信息（拷贝）
     *
     * @note 线程安全
     */
    virtual TaskStatistics getStatistics() const = 0;
};

}  // namespace task
}  // namespace esdk_sophon
```

#### 2.3 LiveStreamTask 实现框架（简化版）

```cpp
// include/esdk_sophon/task/LiveStreamTask.h
#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <opencv2/opencv.hpp>
#include "ITask.h"

namespace esdk_sophon {
namespace task {

/**
 * @brief 直播流检测任务（简化版）
 *
 * 从 RTSP/RTMP 实时视频流中检测目标
 *
 * 特点:
 * - 实时流: 持续读取处理，直到 stop()
 * - 断流重连: 自动重连机制
 * - RAII: 析构自动调用 stop()
 */
class LiveStreamTask : public ITask {
public:
    /**
     * @brief 构造函数
     *
     * @param config 任务配置
     * @param service 任务服务（用于 MQTT 通信）
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

    bool isRunning() const override;
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
     * 2. 检测处理
     * 3. 上报结果
     * 4. 检查停止标志
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
     * @note 线程安全，使用原子操作
     */
    void setState(TaskState newState);

    // ========== 成员变量 ==========

    // 配置和服务
    TaskConfig config_;                          ///< 任务配置
    std::shared_ptr<TaskService> service_;       ///< 任务服务

    // 状态管理
    std::atomic<TaskState> state_;               ///< 任务状态（原子变量，线程安全）
    std::atomic<bool> shouldStop_;               ///< 停止标志

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

### Step 1: 更新头文件（30 分钟）

#### 1.1 更新 TaskTypes.h

```bash
# 编辑文件
vim include/esdk_sophon/task/TaskTypes.h
```

添加:

1. `TaskState` 枚举（3 个状态：CREATED, RUNNING, STOPPED）
2. `TaskStatistics` 结构体

#### 1.2 创建 ITask.h

```bash
vim include/esdk_sophon/task/ITask.h
```

添加接口方法（参考上面的设计）

#### 1.3 更新 LiveStreamTask.h

```bash
vim include/esdk_sophon/task/LiveStreamTask.h
```

添加:

1. 成员变量（线程、原子变量、互斥锁等）
2. 私有方法声明

---

### Step 2: 实现 LiveStreamTask::start()（2 小时）

#### 2.1 实现代码

```cpp
// src/task/LiveStreamTask.cpp

bool LiveStreamTask::start() {
    // 1. 检查状态
    TaskState expected = TaskState::CREATED;
    if (state_.load() != expected) {
        Logger::warn("任务 {} 已在运行或已停止，无法启动", config_.taskId);
        return false;
    }

    // 2. 重置标志
    shouldStop_.store(false);

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

**🎓 知识点**:

- **std::make_unique**: C++14 智能指针创建
- **成员函数指针**: `&LiveStreamTask::processLoop`
- **异常安全**: 失败时回退，避免资源泄漏

#### 2.2 实现 connectStream()

```cpp
bool LiveStreamTask::connectStream() {
    // 1. 构造流 URL
    const std::string& streamUrl = config_.streamUrl;

    Logger::info("任务 {} 连接视频流: {}", config_.taskId, streamUrl);

    // 2. 创建 VideoCapture
    capture_ = std::make_unique<cv::VideoCapture>();

    // 3. 设置参数（可选）
    capture_->set(cv::CAP_PROP_BUFFERSIZE, 3);  // 减小缓冲，降低延迟

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

**🎓 知识点**:

- **OpenCV VideoCapture**: 视频流读取
- **CAP_PROP_BUFFERSIZE**: 控制缓冲区大小，减少延迟
- **RAII**: unique_ptr 自动管理 VideoCapture 生命周期

#### 2.3 实现 processLoop() - 核心处理循环

```cpp
void LiveStreamTask::processLoop() {
    Logger::info("任务 {} 处理线程启动（线程 ID: {}）",
                 config_.taskId, std::this_thread::get_id());

    cv::Mat frame;
    uint64_t frameCount = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();
    int fpsFrameCount = 0;

    while (!shouldStop_.load()) {
        // ========== 1. 读取视频帧 ==========
        if (!capture_ || !capture_->read(frame)) {
            Logger::warn("任务 {} 读取帧失败，尝试重连...", config_.taskId);

            // 尝试重连（实时流可能断开）
            disconnectStream();
            std::this_thread::sleep_for(std::chrono::seconds(2));

            if (!connectStream()) {
                Logger::error("任务 {} 重连失败，停止任务", config_.taskId);
                break;
            }

            continue;
        }

        // ========== 2. 验证帧有效性 ==========
        if (frame.empty()) {
            Logger::warn("任务 {} 读取到空帧，跳过", config_.taskId);
            continue;
        }

        // ========== 3. 处理帧 ==========
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

        // ========== 4. 计算 FPS（每秒更新一次）==========
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

        // ========== 5. 控制帧率（可选）==========
        // 如果检测速度过快，可以限制帧率避免 CPU 占用过高
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Logger::info("任务 {} 处理线程退出", config_.taskId);
}
```

**🎓 知识点**:

- **while (!shouldStop\_)**: 线程安全的停止机制
- **std::atomic<bool>**: 无需加锁的布尔标志
- **异常处理**: 捕获处理异常，避免线程崩溃
- **自动重连**: 网络流断开后自动重连

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

    // 3. 过滤结果（根据配置的事件类型）
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

**🎓 知识点**:

- **std::lock_guard**: RAII 风格的互斥锁
- **作用域锁**: 大括号 `{}` 控制锁的生命周期
- **异常传播**: catch 后返回 false，由调用者决定如何处理

---

### Step 3: 实现 LiveStreamTask::stop()（1 小时）

```cpp
void LiveStreamTask::stop() {
    TaskState currentState = state_.load();

    // 如果已经停止，直接返回
    if (currentState == TaskState::STOPPED) {
        Logger::warn("任务 {} 已处于停止状态", config_.taskId);
        return;
    }

    Logger::info("任务 {} 正在停止...", config_.taskId);

    // 1. 设置停止标志
    shouldStop_.store(true);

    // 2. 等待处理线程退出（最多 5 秒）
    if (processThread_ && processThread_->joinable()) {
        auto future = std::async(std::launch::async, [this]() {
            processThread_->join();
        });

        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            Logger::error("任务 {} 处理线程 5 秒内未退出，强制分离", config_.taskId);
            processThread_->detach();  // 避免 std::terminate
        }
    }

    // 3. 释放资源
    processThread_.reset();
    disconnectStream();
    detector_.reset();

    // 4. 更新状态
    setState(TaskState::STOPPED);

    Logger::info("任务 {} 已停止", config_.taskId);
}
```

**🎓 知识点**:

- **std::async**: 异步执行，用于超时等待
- **std::future::wait_for**: 超时等待机制
- **detach vs join**: detach 避免 std::terminate，但线程仍在后台运行
- **智能指针 reset()**: 释放资源

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

### Step 4: 实现状态查询方法（30 分钟）

```cpp
bool LiveStreamTask::isRunning() const {
    return state_.load() == TaskState::RUNNING;
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
    if (state_.load() == TaskState::RUNNING) {
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

**🎓 知识点**:

- **const 成员函数**: 保证不修改对象状态
- **mutable mutex**: const 函数中也可以加锁
- **拷贝返回**: 返回统计信息的拷贝，避免并发问题

---

### Step 5: 实现 MediaFileTask（1.5 小时）

⚠️ **重要**: MediaFileTask 不是处理本地视频文件，而是从 DJI 机场读取媒体文件！

**MediaFileTask 的实际工作流程**:

```
1. 注册观察者
   RegisterMediaFilesObserver()
   → 监听 DJI 机场的新文件通知

2. 收到回调
   onMediaFileUpdate(MediaFileInfo)
   → DJI SDK 推送文件信息（文件名、路径、大小）
   → 将文件信息放入队列

3. 工作线程处理
   → 从队列取出 MediaFileInfo
   → 使用 MediaFilesReader 读取文件内容到内存
   → cv::imdecode() 解码为 cv::Mat
   → 调用推理处理

4. 清理
   UnregisterMediaFilesObserver()
```

**MediaFileTask 与 LiveStreamTask 的核心区别**:

| 特性         | LiveStreamTask           | MediaFileTask                |
| ------------ | ------------------------ | ---------------------------- |
| **数据源**   | RTSP/RTMP 网络流         | DJI 机场媒体文件             |
| **连接方式** | cv::VideoCapture 打开流  | MediaFilesObserver 监听回调  |
| **数据获取** | capture->read() 实时读帧 | MediaFilesReader 读取文件    |
| **处理模式** | 持续处理，断流重连       | 队列处理，文件处理完继续等待 |
| **结束条件** | 手动 stop()              | 手动 stop() 或队列为空       |

**代码实现**:

```cpp
// MediaFileTask 的特殊实现

bool MediaFileTask::start() {
    // 1. 检查状态
    if (state_ != TaskState::CREATED) {
        Logger::error("无法启动任务: 当前状态不是 CREATED, taskId={}", config_.taskId);
        return false;
    }

    Logger::info("启动媒体文件任务: taskId={}", config_.taskId);

    // 2. ⭐ 注册 MediaFilesObserver（核心区别）
    if (!registerMediaFilesObserver()) {
        Logger::error("注册 MediaFilesObserver 失败");
        return false;
    }

    // 3. 设置运行标志
    shouldStop_ = false;

    // 4. 创建工作线程
    try {
        processThread_ = std::make_unique<std::thread>(&MediaFileTask::processLoop, this);
    } catch (const std::exception& e) {
        shouldStop_ = true;
        unregisterMediaFilesObserver();
        Logger::error("创建工作线程失败: {}", e.what());
        return false;
    }

    // 5. 更新状态
    state_ = TaskState::RUNNING;

    Logger::info("媒体文件任务已启动: taskId={}", config_.taskId);
    return true;
}

bool MediaFileTask::registerMediaFilesObserver() {
    Logger::info("注册 MediaFilesObserver: taskId={}", config_.taskId);

    // ⭐ 实际实现 (参考 Edge-SDK/examples/media_manager/sample_read_media_file.cc)
    auto rc = MediaManager::Instance()->RegisterMediaFilesObserver(
        std::bind(&MediaFileTask::onMediaFileUpdate, this, std::placeholders::_1)
    );

    if (rc != kOk) {
        Logger::error("注册 MediaFilesObserver 失败: rc={}", rc);
        return false;
    }

    return true;
}

void MediaFileTask::onMediaFileUpdate(const MediaFileInfo& file) {
    Logger::info("收到媒体文件更新通知: {}", file.file_name);

    // 过滤视频文件（只处理图片）
    if (file.file_name.find(".mp4") != std::string::npos ||
        file.file_name.find(".MP4") != std::string::npos) {
        Logger::debug("跳过视频文件: {}", file.file_name);
        return;
    }

    // 将文件信息放入队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fileQueue_.push(file);
        queueCv_.notify_one();  // 唤醒工作线程
    }

    Logger::debug("文件已加入队列: {}, 队列大小={}",
                  file.file_name, fileQueue_.size());
}

void MediaFileTask::processLoop() {
    Logger::info("工作线程已启动: taskId={}", config_.taskId);

    while (!shouldStop_.load()) {
        try {
            // 1. 等待队列有文件
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] {
                return !fileQueue_.empty() || shouldStop_.load();
            });

            // 检查停止信号
            if (shouldStop_.load()) {
                break;
            }

            // 队列为空（虚假唤醒）
            if (fileQueue_.empty()) {
                continue;
            }

            // 2. 取出文件
            MediaFileInfo file = fileQueue_.front();
            fileQueue_.pop();
            lock.unlock();

            Logger::info("处理媒体文件: {}", file.file_name);

            // 3. ⭐ 读取文件（使用 DJI MediaFilesReader）
            std::vector<uint8_t> imageData;
            if (!readMediaFile(file, imageData)) {
                Logger::error("读取文件失败: {}", file.file_name);
                updateStats([](TaskStatistics& stats) { stats.failedFrames++; });
                continue;
            }

            // 4. 解码图片
            cv::Mat frame = cv::imdecode(imageData, cv::IMREAD_COLOR);
            if (frame.empty()) {
                Logger::error("解码图片失败: {}", file.file_name);
                updateStats([](TaskStatistics& stats) { stats.failedFrames++; });
                continue;
            }

            // 5. 调用推理（⭐ 传递原始文件名，用于关联检测结果）
            if (service_->processFrame(frame, config_, file.file_name)) {
                updateStats([](TaskStatistics& stats) { stats.eventsPublished++; });
            }

            // 6. 更新统计
            updateStats([](TaskStatistics& stats) { stats.framesProcessed++; });

            Logger::debug("成功处理文件: {}", file.file_name);

        } catch (const std::exception& e) {
            Logger::error("处理文件异常: {}", e.what());
            updateStats([](TaskStatistics& stats) { stats.failedFrames++; });
        }
    }

    Logger::info("工作线程已退出: taskId={}", config_.taskId);
}

bool MediaFileTask::readMediaFile(const MediaFileInfo& file,
                                  std::vector<uint8_t>& imageData) {
    Logger::debug("读取媒体文件: {}", file.file_name);

    // ⭐ 使用 DJI MediaFilesReader 读取文件
    // 参考: Edge-SDK/examples/media_manager/sample_read_media_file.cc

    char buf[1024 * 1024];  // 1MB 缓冲区

    // 1. 打开文件
    auto fd = mediaFilesReader_->Open(file.file_path);
    if (fd < 0) {
        Logger::error("无法打开文件: {}", file.file_name);
        return false;
    }

    // 2. 读取文件内容到内存
    while (true) {
        auto nread = mediaFilesReader_->Read(fd, buf, sizeof(buf));
        if (nread > 0) {
            imageData.insert(imageData.end(),
                           reinterpret_cast<uint8_t*>(buf),
                           reinterpret_cast<uint8_t*>(buf + nread));
        } else {
            // 读取完成或出错
            mediaFilesReader_->Close(fd);
            break;
        }
    }

    Logger::debug("文件读取成功: {}, 大小={} bytes",
                  file.file_name, imageData.size());

    return !imageData.empty();
}

void MediaFileTask::stop() {
    if (state_ != TaskState::RUNNING) {
        Logger::warning("任务未运行，无需停止: taskId={}", config_.taskId);
        return;
    }

    Logger::info("停止媒体文件任务: taskId={}", config_.taskId);

    // 1. 设置停止标志
    shouldStop_ = true;

    // 2. 唤醒工作线程
    queueCv_.notify_one();

    // 3. 等待线程结束
    if (processThread_ && processThread_->joinable()) {
        processThread_->join();
    }

    // 4. ⭐ 注销观察者
    unregisterMediaFilesObserver();

    // 5. 清空队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!fileQueue_.empty()) {
            fileQueue_.pop();
        }
    }

    // 6. 更新状态
    state_ = TaskState::STOPPED;

    Logger::info("媒体文件任务已停止: taskId={}", config_.taskId);
}
```

**📚 MediaFileTask 需要的额外成员变量**:

```cpp
class MediaFileTask : public ITask {
private:
    // DJI SDK 相关
    std::shared_ptr<MediaFilesReader> mediaFilesReader_;  ///< DJI 文件读取器

    // 文件队列
    std::queue<MediaFileInfo> fileQueue_;  ///< 待处理文件队列
    std::mutex queueMutex_;                ///< 队列互斥锁
    std::condition_variable queueCv_;      ///< 队列条件变量

    // 其他成员与 LiveStreamTask 类似
    std::atomic<TaskState> state_;
    std::atomic<bool> shouldStop_;
    std::unique_ptr<std::thread> processThread_;
    // ...
};
```

**🎓 知识点**:

1. **观察者模式**: `RegisterMediaFilesObserver` 注册回调，DJI SDK 推送通知
2. **生产者-消费者模式**: 回调线程生产 `MediaFileInfo`，工作线程消费
3. **条件变量**: `queueCv_` 用于线程间通信，队列为空时工作线程等待
4. **内存管理**: 文件内容读取到 `std::vector<uint8_t>`，自动管理内存
5. **解码**: `cv::imdecode()` 直接从内存解码，无需临时文件

**⚠️ 常见误区**:

❌ **错误理解**: MediaFileTask 处理本地视频文件  
✅ **正确理解**: MediaFileTask 从 DJI 机场读取媒体文件（图片/视频）

❌ **错误实现**: `capture_->open(localFilePath)`  
✅ **正确实现**: `MediaFilesReader->Read()` + `cv::imdecode()`

**📖 参考代码**:

- `Edge-SDK/examples/media_manager/sample_read_media_file.cc`
- `ESDK_On_Sophon/src/task/MediaFileTask.cpp` (已实现)

---

### Step 6: 更新构造函数和析构函数（30 分钟）

```cpp
// 构造函数
LiveStreamTask::LiveStreamTask(const TaskConfig& config,
                               std::shared_ptr<TaskService> service)
    : config_(config)
    , service_(service)
    , state_(TaskState::CREATED)
    , shouldStop_(false)
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

**🎓 知识点**:

- **初始化列表**: 高效初始化成员变量
- **RAII**: 析构函数自动清理资源
- **异常安全**: 即使用户忘记调用 stop()，析构也会处理

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
    std::cout << "✓ 任务创建成功，状态: CREATED" << std::endl;

    // 4. 启动任务
    assert(task.start() == true);
    assert(task.isRunning() == true);
    assert(task.getState() == TaskState::RUNNING);
    std::cout << "✓ 任务启动成功，状态: RUNNING" << std::endl;

    // 5. 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(5));

    auto stats = task.getStatistics();
    std::cout << "✓ 运行 5 秒: 处理帧数=" << stats.processedFrames
              << ", 平均 FPS=" << stats.averageFps << std::endl;

    assert(stats.processedFrames > 0);  // 应该处理了一些帧

    // 6. 停止任务
    task.stop();
    assert(task.getState() == TaskState::STOPPED);
    std::cout << "✓ 任务停止成功，状态: STOPPED" << std::endl;

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

    // 停止未运行的任务（应该安全）
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

        // 离开作用域时，析构函数应自动调用 stop()
        std::cout << "✓ 离开作用域，等待析构函数..." << std::endl;
    }  // ← task 析构，自动停止线程

    std::cout << "✓ 析构函数成功清理资源" << std::endl;
    std::cout << "✅ 测试4通过\n" << std::endl;
}

// ========== 测试5: 重复 stop 调用 ==========
void test_repeated_stop() {
    std::cout << "\n========== 测试5: 重复 stop 调用 ==========" << std::endl;

    TaskConfig config;
    config.taskId = "test_repeated_001";
    config.streamUrl = "rtsp://192.168.1.100:554/stream";

    auto mqtt = mqtt::MqttClient::getSharedInstance();
    auto service = std::make_shared<TaskService>(mqtt);

    LiveStreamTask task(config, service);
    task.start();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 多次调用 stop 应该安全
    task.stop();
    task.stop();
    task.stop();

    assert(task.getState() == TaskState::STOPPED);
    std::cout << "✓ 重复调用 stop() 安全" << std::endl;
    std::cout << "✅ 测试5通过\n" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Day 5 任务运行时功能测试（简化版）" << std::endl;
    std::cout << "========================================\n" << std::endl;

    try {
        test_livestream_lifecycle();
        test_mediafile_lifecycle();
        test_error_handling();
        test_raii();
        test_repeated_stop();

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

- [ ] LiveStreamTask 可以启动、停止
- [ ] MediaFileTask 可以启动、停止
- [ ] 状态转换正确（CREATED → RUNNING → STOPPED）
- [ ] 线程安全，无数据竞争（使用 ThreadSanitizer 验证）
- [ ] 资源正确释放，无内存泄漏（使用 Valgrind 验证）
- [ ] 异常情况正确处理（连接失败、流断开等）

### 测试验收

- [ ] test_task_runtime.cpp 所有测试通过
- [ ] 在 SE7 设备上实际运行成功
- [ ] 日志输出清晰，便于调试
- [ ] 性能符合预期（CPU < 50%, 内存稳定）

### 代码质量

- [ ] 遵循 C++17 标准
- [ ] 详细的中文注释
- [ ] 符合项目编码规范
- [ ] 通过 clang-tidy 静态检查

---

## ❓ 常见问题

### Q1: 为什么使用 std::atomic 而不是 mutex?

**答**:

- `std::atomic` 用于简单的标志变量（如 `shouldStop_`）
- 原子操作比互斥锁开销小，性能更好
- 对于复杂数据（如 `stats_`）仍然需要 `mutex` 保护

**代码示例**:

```cpp
// ✅ 简单标志用 atomic
std::atomic<bool> shouldStop_{false};
shouldStop_.store(true);  // 线程安全，无需加锁

// ✅ 复杂数据用 mutex
std::mutex statsMutex_;
TaskStatistics stats_;
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.processedFrames++;  // 需要加锁保护
}
```

### Q2: 如何避免线程无法退出?

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

// 2. 确保线程循环检查停止标志
while (!shouldStop_.load()) {
    // ... 处理逻辑 ...
}
```

**面试考点**:

- 线程超时等待机制
- detach vs join 的区别
- std::terminate 的触发条件

### Q3: 为什么需要 RAII？

**答**:

```cpp
// ❌ 错误: 用户忘记调用 stop()
{
    LiveStreamTask task(config, service);
    task.start();
    // ... 使用 task ...
}  // ← 析构时线程仍在运行 → std::terminate

// ✅ 正确: 析构函数自动 stop()
LiveStreamTask::~LiveStreamTask() {
    stop();  // 确保线程退出
}
```

**RAII 好处**:

1. 自动资源管理，防止泄漏
2. 异常安全（异常时也会清理）
3. 简化用户代码（无需手动 stop）

### Q4: LiveStreamTask 如何处理断流？

**答**:

```cpp
// 读取失败时自动重连
if (!capture_->read(frame)) {
    Logger::warn("读取帧失败，尝试重连...");

    disconnectStream();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (!connectStream()) {
        Logger::error("重连失败，停止任务");
        break;  // 退出循环
    }
}
```

**策略**:

- 断流检测: read() 返回 false
- 延迟重连: 等待 2 秒避免频繁重连
- 失败退出: 多次重连失败则停止任务

### Q5: 如何调试多线程问题？

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
Logger::debug("[线程 {}] 进入处理循环", std::this_thread::get_id());
Logger::debug("[线程 {}] 退出处理循环", std::this_thread::get_id());
```

---

## 🔮 未来扩展

### 如何添加 pause/resume 功能？

如果未来 DJI 平台支持暂停/恢复，或者需要本地测试暂停功能，可以这样扩展：

#### 步骤 1: 扩展状态枚举（1 分钟）

```cpp
enum class TaskState {
    CREATED,
    RUNNING,
    PAUSED,    // ← 新增
    STOPPED
};
```

#### 步骤 2: 扩展接口（2 分钟）

```cpp
class ITask {
public:
    // ... 原有方法 ...

    // 新增方法
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool isPaused() const = 0;
};
```

#### 步骤 3: 添加成员变量（1 分钟）

```cpp
class LiveStreamTask : public ITask {
private:
    // 新增成员
    std::mutex pauseMutex_;
    std::condition_variable pauseCond_;
    std::atomic<bool> isPausedFlag_;
};
```

#### 步骤 4: 实现 pause/resume（20 分钟）

```cpp
void LiveStreamTask::pause() {
    if (state_ != TaskState::RUNNING) return;
    isPausedFlag_ = true;
    setState(TaskState::PAUSED);
}

void LiveStreamTask::resume() {
    if (state_ != TaskState::PAUSED) return;
    isPausedFlag_ = false;
    pauseCond_.notify_one();
    setState(TaskState::RUNNING);
}

// 修改 processLoop（唯一需要改的地方）
void LiveStreamTask::processLoop() {
    while (!shouldStop_) {
        // 新增: 检查暂停
        if (isPausedFlag_) {
            std::unique_lock<std::mutex> lock(pauseMutex_);
            pauseCond_.wait(lock, [this]() {
                return !isPausedFlag_ || shouldStop_;
            });
            if (shouldStop_) break;
            continue;
        }

        // 原有逻辑不变
        capture_->read(frame);
        processFrame(frame);
    }
}
```

**扩展成本**: ~1 小时  
**架构影响**: 无，完全向后兼容

---

## 📚 参考资料

### C++ 多线程

- [八股-C++多线程.md](../interview/八股-C++多线程.md)
- [cppreference: std::thread](https://en.cppreference.com/w/cpp/thread/thread)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)

### OpenCV 视频处理

- [OpenCV VideoCapture 文档](https://docs.opencv.org/4.x/d8/dfe/classcv_1_1VideoCapture.html)
- RTSP 流处理最佳实践

### 设计模式

- 状态机模式（State Pattern）
- RAII 原则

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
- [ ] Step 4: 状态查询方法
- [ ] Step 5: MediaFileTask
- [ ] Step 6: 测试

#### 遇到的问题

1. **问题**: ...
   **解决**: ...
2. **问题**: ...
   **解决**: ...

#### 新学到的知识点

- [ ] std::thread 生命周期管理
- [ ] std::atomic 原子操作
- [ ] 线程安全的停止机制
- [ ] RAII 资源管理
- [ ] OpenCV VideoCapture

#### 明天计划

- ...
```

---

**文档版本**: v2.0（简化版）  
**最后更新**: 2025-11-09  
**下次审查**: Day 5 完成后

---

## 📊 简化版 vs 完整版对比

| 特性         | 简化版                     | 完整版                  |
| ------------ | -------------------------- | ----------------------- |
| **状态数量** | 3 个                       | 4 个                    |
| **接口方法** | start/stop                 | start/stop/pause/resume |
| **开发时间** | 4-5 小时                   | 6-8 小时                |
| **复杂度**   | 低                         | 中                      |
| **学习价值** | 高（线程、原子变量、RAII） | 更高（+条件变量）       |
| **实用性**   | 对齐 DJI 接口              | 超出需求                |
| **扩展性**   | 易扩展                     | 已完整                  |
| **面试亮点** | 线程安全、RAII             | 条件变量、状态机        |

**推荐**: 先实现简化版，需要时再扩展 ✅
