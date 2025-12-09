# 📊 Day 10 完成总结 - MediaFileTask v3.0 基础架构

**完成时间**: 2025-11-21  
**耗时**: 约 2 小时  
**状态**: ✅ 编译通过，架构完成

---

## 🎯 今天完成了什么？

我们成功将 **MediaFileTask** 从 v1.0 升级到 v3.0 的基础架构，主要添加了：

### 核心功能

1. **`onTaskEnd()` 方法** ⭐

   - 处理航线任务结束通知
   - **关键差异**: 仅标记 `taskEnded_ = true`，不停止任务
   - 原因: 飞机返航后，照片可能还在 4G 网络中传输

2. **监控线程** ⭐

   - 独立线程 `monitorLoop()`
   - 每 5 秒检查一次超时条件
   - 满足条件时自动完成任务

3. **超时自动完成** ⭐
   - 条件 1: `taskEnded_ == true`（收到 `device_task_end`）
   - 条件 2: 60 秒无新文件
   - 满足后调用 `completeTask()` 推送结果

---

## 📂 修改的文件

| 文件                                       | 修改内容               | 行数变化 |
| ------------------------------------------ | ---------------------- | -------- |
| `include/esdk_sophon/task/MediaFileTask.h` | 添加方法声明、成员变量 | +120 行  |
| `src/task/MediaFileTask.cpp`               | 实现新方法             | +150 行  |

---

## 🔧 关键代码片段

### 1. onTaskEnd() - 仅标记，不停止

```cpp
void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，标记航线结束，继续等待文件传输");

    // ⭐ 关键差异：仅标记，不停止任务
    taskEnded_.store(true);

    logger_.info("监控线程将在 60秒无新文件时自动完成任务");
}
```

**对比 LiveStreamTask**:

```cpp
// LiveStreamTask: 立即停止
void LiveStreamTask::onTaskEnd() {
    stop();  // ⭐ 立即停止任务
    publishTaskAnalysisResult(true);
}
```

---

### 2. 监控线程 - 定期检测超时

```cpp
void MediaFileTask::monitorLoop() {
    logger_.info("🔍 监控线程已启动");

    while (running_) {
        // 每隔 5 秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // 检查是否满足自动完成条件
        if (shouldComplete()) {
            completeTask();
            break;
        }
    }
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

    return elapsed >= TIMEOUT_SECONDS;  // 60 秒
}
```

---

### 3. 双线程架构

```
┌────────────────────────────────────────┐
│  WorkerThread (execute)                │
│  ┌──────────────────────────────────┐  │
│  │ while (running_) {               │  │
│  │   取文件 → 读取 → 检测 → 推送     │  │
│  │   更新 lastFileTime_ ⭐          │  │
│  │ }                                │  │
│  └──────────────────────────────────┘  │
└────────────────────────────────────────┘
                  ↕
┌────────────────────────────────────────┐
│  MonitorThread (monitorLoop) ⭐ 新增    │
│  ┌──────────────────────────────────┐  │
│  │ while (running_) {               │  │
│  │   sleep(5秒)                     │  │
│  │   if (shouldComplete()) {        │  │
│  │     completeTask();              │  │
│  │   }                              │  │
│  │ }                                │  │
│  └──────────────────────────────────┘  │
└────────────────────────────────────────┘
```

---

### 4. 时间戳管理 - 线程安全

```cpp
// ⭐ 文件接收时更新时间（DJI 回调线程）
edge_sdk::ErrorCode MediaFileTask::onMediaFileUpdate(const edge_sdk::MediaFile& file) {
    // 更新最后收到文件的时间
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    // 将文件入队
    fileQueue_.push(file);
    queueCv_.notify_one();

    return edge_sdk::kOk;
}
```

**线程安全保证**:
| 线程 | 操作 | 锁保护 |
|------|------|--------|
| DJI 回调线程 | 写入 `lastFileTime_` | `timerMutex_` |
| 监控线程 | 读取 `lastFileTime_` | `timerMutex_` |

---

## 📚 学到的知识点

### 1. 为什么需要独立的监控线程？

**问题**: 如果在工作线程中检测超时会怎样？

```cpp
// ❌ 错误做法
void MediaFileTask::execute() {
    while (running_) {
        // 等待队列有文件
        queueCv_.wait(lock, [this] {
            return !fileQueue_.empty() || !running_;
        });

        // 问题：如果队列为空，线程会一直阻塞在 wait()
        // 无法检测超时！❌
    }
}
```

**解决**: 独立的监控线程定期醒来检查

```cpp
// ✅ 正确做法
void MediaFileTask::monitorLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));  // 定期醒来
        if (shouldComplete()) {
            completeTask();
        }
    }
}
```

---

### 2. `std::atomic` vs `std::mutex`

| 场景       | 使用                | 原因                 |
| ---------- | ------------------- | -------------------- |
| 简单标志位 | `std::atomic<bool>` | 单个布尔值，原子读写 |
| 复杂对象   | `std::mutex`        | 多步操作需要原子性   |

**示例**:

```cpp
// ✅ 简单标志位
std::atomic<bool> taskEnded_{false};
taskEnded_.store(true);   // 原子写入
if (taskEnded_.load()) {  // 原子读取

// ✅ 复杂对象（时间戳）
std::chrono::steady_clock::time_point lastFileTime_;
std::mutex timerMutex_;

{
    std::lock_guard<std::mutex> lock(timerMutex_);  // 需要锁保护
    lastFileTime_ = std::chrono::steady_clock::now();
}
```

---

### 3. `std::chrono::steady_clock` - 单调时钟

**为什么不用 `std::chrono::system_clock`？**

| 时钟类型       | 特点                     | 适用场景          |
| -------------- | ------------------------ | ----------------- |
| `system_clock` | 系统时间，**可被调整**   | 显示给用户的时间  |
| `steady_clock` | 单调递增，**不可被调整** | 计时、超时检测 ⭐ |

**示例**:

```cpp
// ❌ 错误：使用 system_clock
auto start = std::chrono::system_clock::now();
// 用户修改系统时间 -1小时
auto end = std::chrono::system_clock::now();
auto elapsed = end - start;  // 可能是负数！❌

// ✅ 正确：使用 steady_clock
auto start = std::chrono::steady_clock::now();
// 系统时间被修改也不影响
auto end = std::chrono::steady_clock::now();
auto elapsed = end - start;  // 始终 >= 0 ✅
```

---

### 4. 面试高频考点

#### Q1: 为什么 MediaFileTask 不立即停止？

**A**:
业务场景决定。飞机返航后，照片可能还在 4G 网络中传输，如果立即停止任务会丢失这些照片。因此需要等待一段时间（60 秒），确保所有照片都传输完成。

**对比**:

- **LiveStreamTask**: 实时流，`device_task_end` 表示流已结束，立即停止
- **MediaFileTask**: 文件队列，`device_task_end` 表示航线结束，但文件可能还在传输

---

#### Q2: 监控线程的作用是什么？

**A**:
定期检查是否满足自动完成条件：

1. 条件 1: `taskEnded_ == true`（平台发送了 `device_task_end`）
2. 条件 2: 60 秒无新文件（文件传输完成）

如果在工作线程中检测，当队列为空时工作线程会阻塞在 `queueCv_.wait()`，无法检测超时。

---

#### Q3: 为什么需要 `std::mutex` 保护 `lastFileTime_`？

**A**:
多线程同时访问共享数据，需要互斥锁保护。即使是简单的读写操作，也可能出现数据竞争。

**线程访问**:

- **DJI 回调线程**: 写入 `lastFileTime_`
- **监控线程**: 读取 `lastFileTime_`

C++ 标准未保证 `std::chrono::time_point` 的原子性，需要用 `std::mutex` 保护。

---

## 🚀 下一步：Day 11

### 任务清单

1. **解决 libexif 依赖**

   - 方案 A: 安装 libexif 库
   - 方案 B: 使用 OpenCV 解析 EXIF（推荐）

2. **启用 ImageProcessor**

   ```cpp
   // 取消注释
   #include "esdk_sophon/utils/ImageProcessor.h"

   // 使用
   auto& processor = ImageProcessor::getInstance();
   auto metadata = processor.parseExif(filePath);
   ```

3. **实现 processFile()**

   ```cpp
   bool MediaFileTask::processFile(const FileInfo& fileInfo) {
       // 1. 读取图片
       // 2. 解析 EXIF ⭐ 使用 ImageProcessor
       // 3. 调用检测
       // 4. 生成事件
       // 5. 生成缩略图 ⭐ 使用 ImageProcessor
   }
   ```

4. **实现 buildEvent()**
   - 按算法类型分组
   - 添加图片元数据（时间戳、相机型号）

---

## 📖 推荐阅读

### C++ 相关

- **线程**: C++11 `<thread>`, `<mutex>`, `<condition_variable>`
- **时间**: C++11 `<chrono>`
- **原子操作**: C++11 `<atomic>`

### 设计模式

- **Observer 模式**: DJI SDK 回调机制
- **Singleton 模式**: ImageProcessor、GeoUtils、EventCache

### 系统设计

- **生产者-消费者模式**: 文件队列
- **双线程架构**: 工作线程 + 监控线程

---

**总结**: 今天完成了 MediaFileTask 的核心架构，实现了超时自动完成机制。明天（Day 11）将集成 ImageProcessor，实现 EXIF 解析和图片处理功能。加油！💪

---

**文档创建**: 2025-11-21  
**下次更新**: Day 11 完成后
