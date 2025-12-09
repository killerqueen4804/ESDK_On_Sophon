# Day 5 快速参考手册（简化版）

**用途**: 开发过程中快速查找关键代码片段  
**配合使用**: [Day5-任务运行时功能开发方案-简化版.md](./Day5-任务运行时功能开发方案-简化版.md)

> **💡 简化说明**: 本手册只包含 start/stop 功能，不包含 pause/resume。

---

## 📌 状态机状态转换（简化版）

```
CREATED → start() → RUNNING
RUNNING → stop() → STOPPED
CREATED → stop() → STOPPED (未启动也可安全停止)
```

**有效转换**:

- CREATED → start() ✅
- RUNNING → stop() ✅
- CREATED → stop() ✅

**无效转换**:

- RUNNING → start() ❌ (已在运行)
- STOPPED → start() ❌ (已停止，无法重启)

---

## 🔧 关键成员变量（简化版）

```cpp
class LiveStreamTask : public ITask {
private:
    // 状态管理
    std::atomic<TaskState> state_;           // 当前状态（线程安全）
    std::atomic<bool> shouldStop_;           // 停止标志

    // 线程和资源
    std::unique_ptr<std::thread> processThread_;    // 处理线程
    std::unique_ptr<cv::VideoCapture> capture_;     // 视频捕获
    std::shared_ptr<vision::IDetector> detector_;   // 检测器

    // 统计信息
    std::mutex statsMutex_;                  // 统计信息锁
    TaskStatistics stats_;                   // 统计信息
    std::chrono::steady_clock::time_point startTime_;  // 启动时间
};
```

**对比完整版的区别**:

- ❌ 移除: `std::mutex pauseMutex_`
- ❌ 移除: `std::condition_variable pauseCond_`
- ❌ 移除: `std::atomic<bool> isPausedFlag_`

---

## 📋 核心函数速查

### 1. start() 启动流程

```cpp
bool LiveStreamTask::start() {
    // 1️⃣ 检查状态
    if (state_ != TaskState::CREATED) return false;

    // 2️⃣ 重置标志
    shouldStop_ = false;

    // 3️⃣ 连接视频流
    if (!connectStream()) return false;

    // 4️⃣ 初始化检测器
    detector_ = DetectorFactory::createDetector(config_);
    if (!detector_->initialize()) {
        disconnectStream();
        return false;
    }

    // 5️⃣ 启动线程
    processThread_ = std::make_unique<std::thread>(
        &LiveStreamTask::processLoop, this
    );

    // 6️⃣ 更新状态
    setState(TaskState::RUNNING);
    startTime_ = std::chrono::steady_clock::now();

    return true;
}
```

**关键点**:

- 必须从 CREATED 状态启动
- 失败时回退（disconnectStream）
- 使用成员函数指针启动线程

### 2. stop() 停止流程

```cpp
void LiveStreamTask::stop() {
    // 1️⃣ 检查状态
    if (state_ == TaskState::STOPPED) return;

    // 2️⃣ 设置标志
    shouldStop_ = true;

    // 3️⃣ 等待线程退出（超时 5 秒）
    if (processThread_ && processThread_->joinable()) {
        auto future = std::async([this]() { processThread_->join(); });
        if (future.wait_for(5s) == std::future_status::timeout) {
            processThread_->detach();  // 避免 std::terminate
        }
    }

    // 4️⃣ 释放资源
    processThread_.reset();
    disconnectStream();
    detector_.reset();

    // 5️⃣ 更新状态
    setState(TaskState::STOPPED);
}
```

**关键点**:

- 幂等操作（多次调用安全）
- 超时保护（5 秒）
- detach 避免崩溃

### 3. processLoop() 处理循环（简化版）

```cpp
void LiveStreamTask::processLoop() {
    cv::Mat frame;

    while (!shouldStop_) {
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
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.processedFrames++;
        }
    }
}
```

**简化点**:

- ❌ 没有暂停检查
- ✅ 只检查 shouldStop\_
- ✅ 断流自动重连

### 4. connectStream() 连接视频流

```cpp
bool LiveStreamTask::connectStream() {
    const std::string& streamUrl = config_.streamUrl;

    capture_ = std::make_unique<cv::VideoCapture>();
    capture_->set(cv::CAP_PROP_BUFFERSIZE, 3);  // 减小缓冲

    if (!capture_->open(streamUrl)) {
        capture_.reset();
        return false;
    }

    return capture_->isOpened();
}
```

### 5. disconnectStream() 断开视频流

```cpp
void LiveStreamTask::disconnectStream() {
    if (capture_) {
        capture_->release();
        capture_.reset();
    }
}
```

---

## 🧵 多线程同步模式（简化版）

### 模式 1: 停止线程

```cpp
// 主线程
shouldStop_ = true;           // 设置停止标志

// 超时等待
auto future = std::async([this]() { thread_->join(); });
if (future.wait_for(5s) == std::future_status::timeout) {
    thread_->detach();
}

// 工作线程
while (!shouldStop_) {
    // ... 循环体 ...
}
```

### 模式 2: 线程安全的统计更新

```cpp
// 写入统计（工作线程）
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.processedFrames++;
}

// 读取统计（主线程）
TaskStatistics LiveStreamTask::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;  // 拷贝返回
}
```

---

## ⚠️ 常见陷阱（简化版）

### 陷阱 1: 线程未 join 导致崩溃

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

### 陷阱 2: 原子变量的复合操作

```cpp
// ❌ 错误: 非原子的复合操作
if (state_ == TaskState::RUNNING) {  // ← 读取
    state_ = TaskState::STOPPED;      // ← 写入
}  // 两步之间可能被其他线程改变

// ✅ 正确: 直接赋值
state_.store(TaskState::STOPPED);  // 原子操作
```

### 陷阱 3: 忘记检查线程可 join

```cpp
// ❌ 错误: 直接 join 可能崩溃
processThread_->join();  // processThread_ 可能是 nullptr

// ✅ 正确: 先检查
if (processThread_ && processThread_->joinable()) {
    processThread_->join();
}
```

### 陷阱 4: 资源泄漏

```cpp
// ❌ 错误: 异常时资源泄漏
bool start() {
    connectStream();
    detector_->initialize();  // ← 抛异常，capture_ 泄漏
}

// ✅ 正确: 异常安全
bool start() {
    if (!connectStream()) return false;

    try {
        detector_->initialize();
    } catch (...) {
        disconnectStream();  // 回退
        throw;
    }
}
```

### 陷阱 5: 循环中频繁加锁

```cpp
// ❌ 错误: 每帧都加锁
while (!shouldStop_) {
    capture_->read(frame);

    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.processedFrames++;  // 每帧加锁，开销大
}

// ✅ 正确: 批量更新
while (!shouldStop_) {
    capture_->read(frame);
    frameCount++;  // 局部变量
}
// 循环结束后统一更新
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.processedFrames += frameCount;
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
(gdb) thread 2                  # 切换到线程 2
(gdb) bt                        # 查看线程 2 的调用栈
(gdb) thread apply all bt       # 所有线程的调用栈
```

### 2. 检测数据竞争

```bash
# 编译时启用 ThreadSanitizer
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" ..
make

# 运行
export TSAN_OPTIONS="log_path=tsan.log"
./test_task_runtime

# 查看报告
cat tsan.log.*
```

### 3. 检测内存泄漏

```bash
# Valgrind（宿主机）
valgrind --leak-check=full ./test_task_runtime

# AddressSanitizer（推荐，更快）
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -g" ..
make
./test_task_runtime
```

### 4. 添加调试日志

```cpp
// 关键路径添加日志
void LiveStreamTask::start() {
    Logger::debug("[start] 任务 {} 开始启动", config_.taskId);
    // ...
    Logger::debug("[start] 任务 {} 启动成功", config_.taskId);
}

void LiveStreamTask::processLoop() {
    Logger::info("[processLoop] 线程 {} 启动", std::this_thread::get_id());

    while (!shouldStop_) {
        // ...
    }

    Logger::info("[processLoop] 线程 {} 退出", std::this_thread::get_id());
}
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
// ✅ 简单计数用 atomic
std::atomic<uint64_t> frameCount_{0};
frameCount_++;  // 无需加锁

// ✅ 复杂结构仍用 mutex
struct TaskStatistics {
    double fps;
    std::vector<int> history;  // 复杂数据
};
std::mutex statsMutex_;
TaskStatistics stats_;  // 必须用锁保护
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

- [ ] 所有共享变量都有保护（atomic 或 mutex）
- [ ] 没有数据竞争（ThreadSanitizer 验证）
- [ ] shouldStop\_ 使用 std::atomic
- [ ] stats\_ 访问时加锁

### 资源管理

- [ ] 析构函数调用 stop()
- [ ] 线程正确 join（超时保护）
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
- [ ] 无内存泄漏（Valgrind）

---

## 🆚 简化版 vs 完整版对比

### 移除的功能

| 功能            | 完整版 | 简化版 |
| --------------- | ------ | ------ |
| **pause()**     | ✅     | ❌     |
| **resume()**    | ✅     | ❌     |
| **isPaused()**  | ✅     | ❌     |
| **PAUSED 状态** | ✅     | ❌     |
| **条件变量**    | ✅     | ❌     |

### 保留的功能

| 功能            | 完整版 | 简化版 |
| --------------- | ------ | ------ |
| **start()**     | ✅     | ✅     |
| **stop()**      | ✅     | ✅     |
| **isRunning()** | ✅     | ✅     |
| **getState()**  | ✅     | ✅     |
| **线程管理**    | ✅     | ✅     |
| **RAII**        | ✅     | ✅     |

### 学习价值对比

| 知识点                      | 简化版 | 完整版 |
| --------------------------- | ------ | ------ |
| **std::thread**             | ✅     | ✅     |
| **std::atomic**             | ✅     | ✅     |
| **std::mutex**              | ✅     | ✅     |
| **RAII**                    | ✅     | ✅     |
| **std::condition_variable** | ❌     | ✅     |
| **虚假唤醒**                | ❌     | ✅     |

**结论**: 简化版已包含 80% 的核心知识点！

---

## 🔮 未来扩展指南

### 添加 pause/resume 的步骤

如果未来需要添加暂停/恢复功能：

**1. 扩展状态枚举**（1 分钟）

```cpp
enum class TaskState {
    CREATED,
    RUNNING,
    PAUSED,    // ← 新增
    STOPPED
};
```

**2. 扩展接口**（2 分钟）

```cpp
virtual void pause() = 0;
virtual void resume() = 0;
virtual bool isPaused() const = 0;
```

**3. 添加成员变量**（1 分钟）

```cpp
std::mutex pauseMutex_;
std::condition_variable pauseCond_;
std::atomic<bool> isPausedFlag_;
```

**4. 修改 processLoop**（20 分钟）

```cpp
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

    // 原有逻辑
    capture_->read(frame);
    processFrame(frame);
}
```

**总成本**: ~1 小时  
**风险**: 低（不影响现有功能）

---

## 🔗 快速跳转

- [完整开发方案（简化版）](./Day5-任务运行时功能开发方案-简化版.md) ⭐
- [八股-C++多线程](../interview/八股-C++多线程.md)
- [改进记录-Day4](./改进记录-2025-11-09-Day4任务模块完成.md)

---

**最后更新**: 2025-11-09  
**版本**: v2.0（简化版）

---

## 💡 快速提示

### 开发流程

1. 📖 阅读开发方案的"架构设计"
2. 💻 按 Step 1-6 逐步实现
3. 🧪 运行测试验证
4. ✅ 对照检查清单

### 遇到问题时

1. 🔍 先查本手册的"常见陷阱"
2. 🐛 使用调试技巧定位问题
3. 📚 查阅八股文档深入理解
4. 💬 记录问题和解决方案

### 学习建议

- ✅ 理解每段代码的"为什么"
- ✅ 尝试写出自己的实现
- ✅ 对比参考代码找差异
- ✅ 总结知识点到八股文档
