# Day 5 快速参考手册

**用途**: 开发过程中快速查找关键代码片段  
**配合使用**: [Day5-任务运行时功能开发方案.md](./Day5-任务运行时功能开发方案.md)

---

## 📌 状态机状态转换

```
CREATED → start() → RUNNING
RUNNING → pause() → PAUSED
PAUSED → resume() → RUNNING
任何状态 → stop() → STOPPED
```

**无效转换**:

- CREATED → pause() ❌
- PAUSED → start() ❌
- STOPPED → resume() ❌

---

## 🔧 关键成员变量

```cpp
class LiveStreamTask : public ITask {
private:
    // 状态管理
    std::atomic<TaskState> state_;           // 当前状态 (线程安全)
    std::atomic<bool> shouldStop_;           // 停止标志
    std::atomic<bool> isPausedFlag_;         // 暂停标志

    // 同步原语
    std::mutex pauseMutex_;                  // 暂停锁
    std::condition_variable pauseCond_;      // 暂停条件变量
    std::mutex statsMutex_;                  // 统计信息锁

    // 线程和资源
    std::unique_ptr<std::thread> processThread_;    // 处理线程
    std::unique_ptr<cv::VideoCapture> capture_;     // 视频捕获
    std::shared_ptr<vision::IDetector> detector_;   // 检测器

    // 统计
    TaskStatistics stats_;                   // 统计信息
    std::chrono::steady_clock::time_point startTime_;  // 启动时间
};
```

---

## 📋 核心函数速查

### 1. start() 启动流程

```cpp
bool LiveStreamTask::start() {
    // 1️⃣ 检查状态
    if (state_ != TaskState::CREATED) return false;

    // 2️⃣ 重置标志
    shouldStop_ = false;
    isPausedFlag_ = false;

    // 3️⃣ 连接视频流
    if (!connectStream()) return false;

    // 4️⃣ 初始化检测器
    detector_ = DetectorFactory::createDetector(config_);
    if (!detector_->initialize()) return false;

    // 5️⃣ 启动线程
    processThread_ = std::make_unique<std::thread>(
        &LiveStreamTask::processLoop, this
    );

    // 6️⃣ 更新状态
    setState(TaskState::RUNNING);

    return true;
}
```

### 2. stop() 停止流程

```cpp
void LiveStreamTask::stop() {
    // 1️⃣ 检查状态
    if (state_ == TaskState::STOPPED) return;

    // 2️⃣ 设置标志
    shouldStop_ = true;

    // 3️⃣ 唤醒暂停的线程
    if (state_ == TaskState::PAUSED) {
        isPausedFlag_ = false;
        pauseCond_.notify_all();
    }

    // 4️⃣ 等待线程退出 (超时5秒)
    if (processThread_ && processThread_->joinable()) {
        auto future = std::async([this]() { processThread_->join(); });
        if (future.wait_for(5s) == std::future_status::timeout) {
            processThread_->detach();  // 避免 std::terminate
        }
    }

    // 5️⃣ 释放资源
    processThread_.reset();
    disconnectStream();
    detector_.reset();

    // 6️⃣ 更新状态
    setState(TaskState::STOPPED);
}
```

### 3. pause() 暂停

```cpp
void LiveStreamTask::pause() {
    // 检查状态
    if (state_ != TaskState::RUNNING) return;

    // 设置标志
    isPausedFlag_ = true;

    // 更新状态
    setState(TaskState::PAUSED);
}
```

### 4. resume() 恢复

```cpp
void LiveStreamTask::resume() {
    // 检查状态
    if (state_ != TaskState::PAUSED) return;

    // 清除标志
    isPausedFlag_ = false;

    // 唤醒线程
    pauseCond_.notify_one();

    // 更新状态
    setState(TaskState::RUNNING);
}
```

### 5. processLoop() 处理循环

```cpp
void LiveStreamTask::processLoop() {
    cv::Mat frame;

    while (!shouldStop_) {
        // 🔸 检查暂停
        if (isPausedFlag_) {
            std::unique_lock<std::mutex> lock(pauseMutex_);
            pauseCond_.wait(lock, [this]() {
                return !isPausedFlag_ || shouldStop_;
            });
            if (shouldStop_) break;
            continue;
        }

        // 🔸 读取帧
        if (!capture_->read(frame)) {
            // 重连逻辑
            disconnectStream();
            std::this_thread::sleep_for(2s);
            if (!connectStream()) break;
            continue;
        }

        // 🔸 验证帧
        if (frame.empty()) continue;

        // 🔸 处理帧
        processFrame(frame);

        // 🔸 更新统计
        // ...
    }
}
```

---

## 🧵 多线程同步模式

### 模式 1: 暂停/恢复

```cpp
// 暂停方 (主线程)
isPausedFlag_ = true;         // 设置标志
setState(TaskState::PAUSED);

// 恢复方 (主线程)
isPausedFlag_ = false;        // 清除标志
pauseCond_.notify_one();      // 唤醒工作线程
setState(TaskState::RUNNING);

// 等待方 (工作线程)
if (isPausedFlag_) {
    std::unique_lock<std::mutex> lock(pauseMutex_);
    pauseCond_.wait(lock, [this]() {
        return !isPausedFlag_ || shouldStop_;
    });
}
```

### 模式 2: 停止线程

```cpp
// 主线程
shouldStop_ = true;           // 设置停止标志
pauseCond_.notify_all();      // 唤醒所有等待线程

// 超时等待
auto future = std::async([this]() { thread_->join(); });
if (future.wait_for(5s) == std::future_status::timeout) {
    thread_->detach();
}

// 工作线程
while (!shouldStop_) {
    // ... 循环体 ...

    // 每次循环检查 shouldStop_
}
```

### 模式 3: 线程安全的统计更新

```cpp
// 写入统计 (工作线程)
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.processedFrames++;
}

// 读取统计 (主线程)
TaskStatistics LiveStreamTask::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;  // 拷贝返回
}
```

---

## ⚠️ 常见陷阱

### 陷阱 1: 条件变量虚假唤醒

```cpp
// ❌ 错误: 可能虚假唤醒后继续运行
pauseCond_.wait(lock);

// ✅ 正确: 使用条件判断
pauseCond_.wait(lock, [this]() {
    return !isPausedFlag_ || shouldStop_;
});
```

### 陷阱 2: 线程未 join 导致崩溃

```cpp
// ❌ 错误: 析构时线程仍在运行
~LiveStreamTask() {
    // 忘记调用 stop()
}
// → std::terminate: thread still joinable

// ✅ 正确: RAII 自动清理
~LiveStreamTask() {
    stop();  // 确保线程退出
}
```

### 陷阱 3: 死锁

```cpp
// ❌ 错误: 可能死锁
void stop() {
    std::lock_guard<std::mutex> lock1(statsMutex_);
    std::lock_guard<std::mutex> lock2(pauseMutex_);  // 如果线程顺序相反→死锁
}

// ✅ 正确: 统一锁顺序或使用 std::lock
void stop() {
    std::lock(statsMutex_, pauseMutex_);
    std::lock_guard<std::mutex> lock1(statsMutex_, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(pauseMutex_, std::adopt_lock);
}
```

### 陷阱 4: 原子变量的复合操作

```cpp
// ❌ 错误: 非原子的复合操作
if (state_ == TaskState::RUNNING) {  // ← 读取
    state_ = TaskState::PAUSED;      // ← 写入
}  // 两步之间可能被其他线程改变

// ✅ 正确: 使用 compare_exchange
TaskState expected = TaskState::RUNNING;
state_.compare_exchange_strong(expected, TaskState::PAUSED);
```

### 陷阱 5: LiveStreamTask 暂停期间断流

```cpp
// ❌ 错误: 暂停时不读取帧 → RTSP 流断开
if (isPausedFlag_) {
    std::this_thread::sleep_for(100ms);
    continue;  // 不读取帧,服务器超时断开
}

// ✅ 正确: 暂停时仍读取但不处理
capture_->read(frame);  // 保持连接
if (!isPausedFlag_) {   // 只在非暂停时处理
    processFrame(frame);
}
```

---

## 🐛 调试技巧

### 1. 查看线程状态

```bash
# gdb 调试
gdb ./test_task_runtime
(gdb) run
(gdb) info threads              # 查看所有线程
(gdb) thread 2                  # 切换到线程2
(gdb) bt                        # 查看线程2的调用栈
(gdb) thread apply all bt       # 所有线程的调用栈
```

### 2. 检测数据竞争

```bash
# 编译时启用 ThreadSanitizer
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" ..
make

# 运行
export TSAN_OPTIONS="log_path=tsan.log second_deadlock_stack=1"
./test_task_runtime

# 查看报告
cat tsan.log.*
```

### 3. 检测死锁

```bash
# 编译时启用死锁检测
export TSAN_OPTIONS="detect_deadlocks=1"
./test_task_runtime
```

### 4. 添加调试日志

```cpp
// 关键路径添加日志
Logger::debug("[线程 {}] 进入暂停等待", std::this_thread::get_id());
Logger::debug("[线程 {}] 从暂停唤醒, shouldStop={}",
              std::this_thread::get_id(), shouldStop_.load());
Logger::debug("[线程 {}] 退出处理循环", std::this_thread::get_id());
```

---

## 📊 性能优化

### 1. 减少锁竞争

```cpp
// ❌ 频繁加锁
for (int i = 0; i < 1000; i++) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.processedFrames++;
}

// ✅ 批量更新
int count = 0;
for (int i = 0; i < 1000; i++) {
    count++;
}
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.processedFrames += count;
}
```

### 2. 使用 atomic 代替 mutex

```cpp
// 简单计数用 atomic
std::atomic<uint64_t> frameCount_{0};
frameCount_++;  // 无需加锁

// 复杂结构仍用 mutex
struct ComplexStats {
    double fps;
    std::vector<int> history;
};
std::mutex statsMutex_;
ComplexStats stats_;  // 必须用锁保护
```

### 3. 避免帧缓冲堆积

```cpp
// 设置小缓冲避免延迟
capture_->set(cv::CAP_PROP_BUFFERSIZE, 1);  // 最新帧

// 跳帧处理
if (frameCount % 2 == 0) {  // 每隔一帧处理
    processFrame(frame);
}
```

---

## 📝 代码检查清单

开发完成后检查:

### 线程安全

- [ ] 所有共享变量都有保护 (atomic 或 mutex)
- [ ] 没有数据竞争 (ThreadSanitizer 验证)
- [ ] 没有死锁 (锁顺序一致)
- [ ] 条件变量使用条件判断 (防虚假唤醒)

### 资源管理

- [ ] 析构函数调用 stop()
- [ ] 线程正确 join (超时保护)
- [ ] VideoCapture 正确 release
- [ ] 智能指针管理检测器

### 状态管理

- [ ] 状态转换逻辑正确
- [ ] 非法状态转换被拒绝
- [ ] 日志记录状态变化

### 异常安全

- [ ] 连接失败正确处理
- [ ] 检测器初始化失败回退
- [ ] 线程异常不导致崩溃

### 测试

- [ ] 单元测试覆盖所有状态转换
- [ ] 异常情况测试
- [ ] 在 SE7 实际运行验证
- [ ] 无内存泄漏 (Valgrind)

---

## 🔗 快速跳转

- [完整开发方案](./Day5-任务运行时功能开发方案.md)
- [八股-C++多线程](../interview/八股-C++多线程.md)
- [改进记录-Day4](./改进记录-2025-11-09-Day4任务模块完成.md)

---

**最后更新**: 2025-11-09
