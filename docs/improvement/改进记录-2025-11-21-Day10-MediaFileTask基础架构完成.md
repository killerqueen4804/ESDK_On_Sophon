# Day 10: MediaFileTask 基础架构完成 ✅

**日期**: 2025-11-21  
**版本**: v3.0  
**状态**: ✅ 已完成  
**用时**: 约 2 小时

---

## 🎯 目标达成

将 MediaFileTask 从 v1.0 升级到 v3.0，添加以下核心功能：

1. ✅ **`onTaskEnd()` 方法** - 处理航线任务结束通知
2. ✅ **监控线程** - 定期检测超时条件
3. ✅ **超时自动完成** - 60 秒无新文件自动完成任务
4. ✅ **工具类准备** - 预留 ImageProcessor、GeoUtils、EventCache 接口

---

## 📝 完成的任务清单

### Step 1: 更新头文件依赖 ✅

**文件**: `include/esdk_sophon/task/MediaFileTask.h`

**修改内容**:

```cpp
// ⭐ v3.0 新增：工具类依赖（暂时注释，Day 11-12 启用）
// TODO Day 11: 取消注释，使用 ImageProcessor 解析 EXIF
// #include "esdk_sophon/utils/ImageProcessor.h"

// TODO Day 12: 取消注释，使用 GeoUtils 计算 GPS
// #include "esdk_sophon/utils/GeoUtils.h"

// TODO Day 12: 取消注释，使用 EventCache 推送事件
// #include "esdk_sophon/core/EventCache.h"

#include <chrono>  // ⭐ 新增：超时控制
```

**原因**:

- ImageProcessor 依赖 libexif，Day 11 再处理
- 先完成基础架构，工具类逐步集成

---

### Step 2: 添加 `onTaskEnd()` 方法 ✅

#### 2.1 头文件声明

**文件**: `include/esdk_sophon/task/MediaFileTask.h`

```cpp
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
 */
void onTaskEnd() override;
```

#### 2.2 实现

**文件**: `src/task/MediaFileTask.cpp`

```cpp
void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，标记航线结束，继续等待文件传输");

    // ⭐ 关键差异：仅标记，不停止任务
    taskEnded_.store(true);

    logger_.info("taskEnded_ 已设置为 true，等待文件传输完成（60秒超时）");
    logger_.info("监控线程将在 60秒无新文件时自动完成任务");
}
```

**对比 LiveStreamTask**:

| 特性               | LiveStreamTask           | MediaFileTask                        |
| ------------------ | ------------------------ | ------------------------------------ |
| `onTaskEnd()` 行为 | 立即 `stop()`            | 仅标记 `taskEnded_ = true`           |
| 任务结束时机       | 收到 `device_task_end`   | 60 秒无新文件 + `taskEnded_ == true` |
| 推送结果时机       | `onTaskEnd()` 内立即推送 | `completeTask()` 自动推送            |
| 原因               | 实时流结束即停止         | 文件传输可能延迟                     |

---

### Step 3: 添加监控线程相关方法 ✅

#### 3.1 新增私有方法声明

```cpp
// ==================== v3.0 新增方法 ====================

/**
 * @brief 监控线程 ⭐
 * 定期检查是否满足自动完成条件
 */
void monitorLoop();

/**
 * @brief 判断任务是否应该完成 ⭐
 * @return true 满足完成条件（taskEnded_ == true && 60秒无新文件）
 */
bool shouldComplete();

/**
 * @brief 完成任务 ⭐
 * 流程: 推送结果 → 清理文件 → 恢复设置
 */
void completeTask();

/**
 * @brief 推送"任务分析完成"消息 ⭐
 */
void publishTaskAnalysisResult(bool success);

/**
 * @brief 清理本地文件 ⭐
 */
void cleanupFiles();

/**
 * @brief 恢复 DJI 设置 ⭐
 */
void restoreDjiSettings();
```

#### 3.2 实现监控逻辑

**文件**: `src/task/MediaFileTask.cpp`

```cpp
void MediaFileTask::monitorLoop() {
    logger_.info("🔍 监控线程已启动: taskId=" + config_.taskId);

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
        logger_.info("超时检测: 已 " + std::to_string(elapsed) +
                    " 秒无新文件，触发自动完成");
        return true;
    }

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

    logger_.info("✅ 任务已自动完成，等待平台发送 device_algorithm_disable");
}
```

---

### Step 4: 添加成员变量 ✅

**文件**: `include/esdk_sophon/task/MediaFileTask.h`

```cpp
private:
    // ==================== 状态管理 ====================
    std::atomic<bool> taskEnded_{false};   ///< ⭐ v3.0 新增：航线任务是否结束

    // ==================== 线程管理 ====================
    std::unique_ptr<std::thread> monitorThread_;  ///< ⭐ v3.0 新增：监控线程

    // ==================== 超时控制 ⭐ v3.0 新增 ====================
    std::chrono::steady_clock::time_point lastFileTime_;  ///< 最后收到文件的时间
    std::mutex timerMutex_;                               ///< 定时器互斥锁
    static constexpr int TIMEOUT_SECONDS = 60;            ///< 超时时间（秒）

    // ==================== 文件存储 ⭐ v3.0 新增 ====================
    std::vector<std::string> downloadedFiles_;  ///< 已下载文件列表（用于清理）
    std::mutex filesMutex_;                     ///< 文件列表互斥锁
```

**知识点**:

- `std::chrono::steady_clock` - 单调时钟，不受系统时间调整影响
- `static constexpr` - 编译期常量，C++11 特性

---

### Step 5: 修改 `start()` 方法 ✅

**文件**: `src/task/MediaFileTask.cpp`

```cpp
bool MediaFileTask::start() {
    // ... 检查状态 ...

    // ⭐ v3.0 新增：初始化超时控制
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    // 设置运行标志
    running_ = true;
    paused_ = false;
    taskEnded_ = false;  // ⭐ v3.0 新增

    // 创建工作线程
    try {
        workerThread_ = std::make_unique<std::thread>(&MediaFileTask::execute, this);
        monitorThread_ = std::make_unique<std::thread>(&MediaFileTask::monitorLoop, this);  // ⭐ v3.0 新增
    } catch (const std::exception& e) {
        running_ = false;
        notifyError("创建工作线程失败: " + std::string(e.what()));
        return false;
    }

    logger_.info("媒体文件任务已启动（工作线程 + 监控线程）");
    return true;
}
```

**双线程架构**:

```
┌─────────────────────────────────────────────────┐
│  WorkerThread (execute)                         │
│  - 从队列取文件                                   │
│  - 读取图片                                      │
│  - 调用检测                                      │
│  - 推送事件                                      │
│  - 更新 lastFileTime_ ⭐                        │
└─────────────────────────────────────────────────┘
                    ↕
┌─────────────────────────────────────────────────┐
│  MonitorThread (monitorLoop) ⭐ v3.0 新增        │
│  - 每5秒检查一次                                  │
│  - shouldComplete()?                            │
│    ├─ taskEnded_ == true?                       │
│    └─ 60秒无新文件?                              │
│  - YES → completeTask()                         │
└─────────────────────────────────────────────────┘
```

---

### Step 6: 修改 `stop()` 方法 ✅

```cpp
void MediaFileTask::stop() {
    // ... 设置停止标志 ...

    // 等待工作线程结束
    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }

    // ⭐ v3.0 新增：等待监控线程结束
    if (monitorThread_ && monitorThread_->joinable()) {
        monitorThread_->join();
    }

    // ... 清理资源 ...
}
```

---

### Step 7: 更新文件接收回调 ✅

**文件**: `src/task/MediaFileTask.cpp`

```cpp
edge_sdk::ErrorCode MediaFileTask::onMediaFileUpdate(const edge_sdk::MediaFile& file) {
    logger_.info("收到媒体文件更新通知: " + file.file_name);

    // 过滤视频文件
    if (file.file_type == edge_sdk::MediaFile::kFileTypeMp4) {
        return edge_sdk::kOk;
    }

    // ⭐ v3.0 新增：更新最后收到文件的时间（用于超时检测）
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    // 将文件信息放入队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fileQueue_.push(file);
    }

    queueCv_.notify_one();
    return edge_sdk::kOk;
}
```

**关键点**:

- 每次收到新文件，更新 `lastFileTime_`
- 监控线程通过 `lastFileTime_` 判断是否超时

---

## 📊 架构对比

### v1.0 vs v3.0

| 特性                   | v1.0              | v3.0 ✅                          |
| ---------------------- | ----------------- | -------------------------------- |
| **线程数**             | 1 (WorkerThread)  | 2 (WorkerThread + MonitorThread) |
| **`onTaskEnd()` 方法** | ❌ 没有           | ✅ 仅标记，不停止                |
| **超时控制**           | ❌ 没有           | ✅ 60 秒无新文件自动完成         |
| **任务结束方式**       | 外部调用 `stop()` | 自动检测 + 外部 `stop()`         |
| **工具类集成**         | ❌ 没有           | 🔄 预留接口（Day 11-12 启用）    |

---

## 🎓 知识点总结

### 1. 为什么 MediaFileTask 不立即停止？

**业务场景**:

```
时间轴：
T0: 任务启动，飞机起飞拍摄
T1: 飞机返航，平台发送 device_task_end
    → onTaskEnd() 被调用
    → taskEnded_ = true
    → 任务继续运行 ⭐
T2: 照片陆续通过 4G 传输到服务器
    → onMediaFileUpdate() 更新 lastFileTime_
T3: 60秒无新文件
    → shouldComplete() 返回 true
    → completeTask() 推送结果
T4: 平台发送 device_algorithm_disable
    → stop() 被调用
```

**对比 LiveStreamTask**:

```
LiveStreamTask: device_task_end → 立即 stop() → 推送结果
MediaFileTask: device_task_end → 仅标记 → 继续等待 → 超时后自动完成
```

---

### 2. 双线程架构的必要性

**为什么需要独立的监控线程？**

**问题场景**:

```cpp
// ❌ 错误做法：在工作线程中检测超时
void MediaFileTask::execute() {
    while (running_) {
        // 等待队列有文件
        queueCv_.wait(lock, [this] {
            return !fileQueue_.empty() || !running_;
        });

        // 如果队列为空，工作线程会一直阻塞在 wait()
        // 无法检测超时条件！❌
    }
}
```

**正确做法**:

```cpp
// ✅ 独立的监控线程，定期检查
void MediaFileTask::monitorLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));  // 定期醒来

        if (shouldComplete()) {  // 检查超时条件
            completeTask();
            break;
        }
    }
}
```

---

### 3. 线程安全的时间戳管理

**问题**: `lastFileTime_` 被两个线程访问

| 线程             | 操作                                         |
| ---------------- | -------------------------------------------- |
| **DJI 回调线程** | `onMediaFileUpdate()` → 写入 `lastFileTime_` |
| **监控线程**     | `shouldComplete()` → 读取 `lastFileTime_`    |

**解决方案**: 使用 `std::mutex` 保护

```cpp
// ✅ 写入（DJI 回调线程）
{
    std::lock_guard<std::mutex> lock(timerMutex_);
    lastFileTime_ = std::chrono::steady_clock::now();
}

// ✅ 读取（监控线程）
{
    std::lock_guard<std::mutex> lock(timerMutex_);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastFileTime_
    ).count();
}
```

**面试考点**:

- Q: 为什么需要 `std::mutex` 保护 `lastFileTime_`？
- A: 多线程同时访问共享数据，需要互斥锁保护。即使是简单的读写操作，也可能出现数据竞争（C++ 标准未保证 `std::chrono::time_point` 的原子性）。

---

### 4. `std::atomic` vs `std::mutex`

| 类型              | 使用场景           | 示例                                    |
| ----------------- | ------------------ | --------------------------------------- |
| **`std::atomic`** | 简单标志位         | `std::atomic<bool> taskEnded_`          |
| **`std::mutex`**  | 复杂对象、多步操作 | `std::chrono::time_point lastFileTime_` |

**为什么 `taskEnded_` 用 `atomic`？**

```cpp
// ✅ 单个 bool 值，读写原子
taskEnded_.store(true);   // 写入
if (taskEnded_.load()) {  // 读取
    // ...
}
```

**为什么 `lastFileTime_` 用 `mutex`？**

```cpp
// ❌ time_point 不是原子类型，需要锁保护
auto elapsed = now - lastFileTime_;  // 多步操作，需要原子性
```

---

## 🐛 遇到的问题和解决

### 问题 1: libexif 头文件缺失

**错误**:

```
fatal error: libexif/exif-data.h: No such file or directory
```

**原因**:

- `ImageProcessor.h` 依赖 `libexif`
- 容器中未安装该库

**解决方案**:

```cpp
// 暂时注释工具类引用，Day 11 再处理
// #include "esdk_sophon/utils/ImageProcessor.h"
// #include "esdk_sophon/utils/GeoUtils.h"
// #include "esdk_sophon/core/EventCache.h"
```

**后续计划**:

- Day 11: 安装 libexif 或使用 OpenCV 替代
- Day 12: 启用 GeoUtils 和 EventCache

---

### 问题 2: 链接错误 - `onTaskEnd()` 未定义

**错误**:

```
undefined reference to 'esdk_sophon::task::MediaFileTask::onTaskEnd()'
```

**原因**:

- 头文件声明了 `onTaskEnd()` 但未实现

**解决方案**:

```cpp
// src/task/MediaFileTask.cpp
void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end");
    taskEnded_.store(true);
}
```

---

## ✅ 验收标准

### 编译验证

```bash
cd /workspace/build
make -j4
# 预期: 编译成功，无错误 ✅
```

### 功能验证（Day 11-13 完成）

- [ ] `start()` 成功启动任务（工作线程 + 监控线程）
- [ ] `onTaskEnd()` 正确标记 `taskEnded_`
- [ ] `monitorLoop()` 定期检查超时条件
- [ ] `shouldComplete()` 逻辑正确（60 秒 + taskEnded\_）
- [ ] `completeTask()` 推送结果消息
- [ ] `stop()` 正确停止所有线程

---

## 📚 下一步：Day 11

### 任务清单

1. **安装 libexif 或替代方案**

   - 方案 A: 在容器中安装 libexif
   - 方案 B: 使用 OpenCV 解析 EXIF（推荐）

2. **启用 ImageProcessor**

   - 取消注释 `#include "esdk_sophon/utils/ImageProcessor.h"`
   - 实现 `processFile()` 方法
   - 使用 `ImageProcessor::parseExif()` 解析元数据

3. **实现 buildEvent()**

   - 按算法类型分组生成事件
   - 生成缩略图（Base64 编码）

4. **测试**
   - 单元测试 `processFile()`
   - 验证 EXIF 解析正确性

---

**创建时间**: 2025-11-21  
**完成时间**: 2025-11-21  
**用时**: 约 2 小时  
**状态**: ✅ Day 10 基础架构完成，可以进入 Day 11
