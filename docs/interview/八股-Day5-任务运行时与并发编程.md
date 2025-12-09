# 面试八股文 - Day 5: 任务运行时与并发编程

**日期**: 2025-11-10  
**主题**: C++ 并发编程、线程安全、设计模式  
**难度**: ⭐⭐⭐⭐ (高级)

---

## 📌 知识点 1: std::thread 线程管理

### 概念

`std::thread` 是 C++11 引入的线程类,用于创建和管理线程。

### 经典例子

```cpp
#include <thread>
#include <iostream>

void threadFunc(int id) {
    std::cout << "Thread " << id << " is running\n";
}

int main() {
    std::thread t1(threadFunc, 1);
    std::thread t2(threadFunc, 2);

    t1.join();  // 等待线程1完成
    t2.join();  // 等待线程2完成

    return 0;
}
```

### 项目中的例子

```cpp
// LiveStreamTask.cpp - start() 方法
bool LiveStreamTask::start() {
    // ... 初始化代码 ...

    // 创建工作线程
    workerThread_ = std::thread(&LiveStreamTask::processLoop, this);

    logger_.info("工作线程已启动: taskId=" + config_.taskId);
    return true;
}

// stop() 方法
void LiveStreamTask::stop() {
    shouldStop_.store(true);           // 设置停止标志
    pauseCondition_.notify_all();      // 唤醒可能在等待的线程

    if (workerThread_.joinable()) {
        workerThread_.join();          // 等待线程退出
    }
}
```

### 详细讲解

#### 线程创建

```cpp
// 方式1: 函数指针
std::thread t1(func, arg1, arg2);

// 方式2: Lambda
std::thread t2([] { std::cout << "Lambda\n"; });

// 方式3: 成员函数 (项目使用)
std::thread t3(&ClassName::methodName, this);
```

#### 线程同步

- **`join()`**: 阻塞当前线程,直到目标线程完成
- **`detach()`**: 分离线程,线程独立运行,不可再 join
- **`joinable()`**: 检查线程是否可 join

#### 注意事项

1. **必须 join 或 detach**: 线程对象销毁前必须调用其一,否则 `std::terminate()`
2. **不能多次 join**: 同一个线程只能 join 一次
3. **异常安全**: 使用 RAII 包装线程管理

### 面试要点

**Q1: 为什么要调用 join()?不调用会怎样?**

A:

- `join()` 确保主线程等待子线程完成,避免资源泄漏
- 如果不调用 `join()` 或 `detach()`,线程对象析构时会调用 `std::terminate()` 终止程序
- 项目中在 `stop()` 方法中调用 `join()` 确保线程正确退出

**Q2: join() 和 detach() 的区别?**

A:
| 特性 | join() | detach() |
|------|--------|----------|
| 主线程行为 | 阻塞等待 | 立即返回 |
| 子线程状态 | 可控 | 失控(后台运行) |
| 资源回收 | 主线程负责 | 操作系统负责 |
| 使用场景 | 需要结果/同步 | 不关心结果 |

项目中使用 `join()` 因为需要确保任务正确停止。

**Q3: joinable() 的作用?**

A:

- 检查线程是否可以 `join()`
- 已经 `join()` 或 `detach()` 的线程返回 `false`
- 项目中在 `stop()` 前检查 `joinable()` 避免重复 join

---

## 📌 知识点 2: std::atomic 原子操作

### 概念

`std::atomic` 提供无锁的原子操作,保证多线程环境下的数据一致性。

### 经典例子

```cpp
#include <atomic>
#include <thread>

std::atomic<int> counter{0};

void increment() {
    for (int i = 0; i < 1000; ++i) {
        counter++;  // 原子递增
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);

    t1.join();
    t2.join();

    std::cout << "Counter: " << counter << std::endl;  // 输出: 2000
    return 0;
}
```

### 项目中的例子

```cpp
// LiveStreamTask.h
class LiveStreamTask {
private:
    std::atomic<TaskState> state_{TaskState::IDLE};  // 任务状态
    std::atomic<bool> shouldStop_{false};            // 停止标志
    std::atomic<bool> isPaused_{false};              // 暂停标志
};

// LiveStreamTask.cpp - start() 方法
bool LiveStreamTask::start() {
    // 原子性状态转换: IDLE → RUNNING
    TaskState expected = TaskState::IDLE;
    if (!state_.compare_exchange_strong(expected, TaskState::RUNNING)) {
        logger_.error("无法启动任务: 当前状态不是 IDLE");
        return false;
    }
    // ...
}

// isRunning() 方法
bool LiveStreamTask::isRunning() const {
    return state_.load() == TaskState::RUNNING;  // 原子读取
}
```

### 详细讲解

#### 常用操作

```cpp
std::atomic<int> value{0};

// 读取
int v = value.load();              // 显式读取
int v2 = value;                    // 隐式读取

// 写入
value.store(10);                   // 显式写入
value = 20;                        // 隐式写入

// 读-改-写 (原子操作)
value++;                           // 原子递增
value--;                           // 原子递减
value += 5;                        // 原子加法
value.fetch_add(5);                // 显式原子加法

// 比较并交换 (CAS: Compare-And-Swap)
int expected = 0;
bool success = value.compare_exchange_strong(expected, 10);
// 如果 value == expected, 则设置 value = 10, 返回 true
// 如果 value != expected, 则设置 expected = value, 返回 false
```

#### 内存顺序 (Memory Order)

```cpp
std::atomic<int> value;

// 默认: memory_order_seq_cst (顺序一致性,最安全但最慢)
value.store(10);

// 放松顺序: memory_order_relaxed (最快但需要小心)
value.store(10, std::memory_order_relaxed);

// 获取-释放: memory_order_acquire/release (平衡性能和安全)
value.store(10, std::memory_order_release);
int v = value.load(std::memory_order_acquire);
```

### 面试要点

**Q1: 为什么用 atomic 而不是 mutex?**

A:
| 特性 | atomic | mutex |
|------|--------|-------|
| 性能 | 无锁,快速 | 有锁,可能阻塞 |
| 适用场景 | 简单类型(int, bool) | 复杂操作,保护多个变量 |
| 开销 | CPU 指令级 | 系统调用级 |
| 复杂度 | 低(单个变量) | 高(临界区) |

项目中:

- `state_`, `shouldStop_`, `isPaused_` 用 `atomic` (简单标志位)
- `stats_` (多个字段) 用 `mutex` 保护

**Q2: compare_exchange_strong 的作用?**

A:

```cpp
TaskState expected = TaskState::IDLE;
if (!state_.compare_exchange_strong(expected, TaskState::RUNNING)) {
    // 失败: state_ 不是 IDLE
    // expected 已被更新为 state_ 的实际值
    return false;
}
// 成功: state_ 从 IDLE 变为 RUNNING
```

**原理**: 原子性地完成"检查-设置",避免竞态条件 (race condition)。

**Q3: load() 和直接读取的区别?**

A:

```cpp
// 方式1: 显式 load
TaskState s = state_.load();

// 方式2: 隐式转换
TaskState s = state_;

// 实际效果相同,但显式 load 更清晰,可以指定内存顺序
TaskState s = state_.load(std::memory_order_relaxed);
```

---

## 📌 知识点 3: std::condition_variable 条件变量

### 概念

条件变量用于线程间通信,一个线程等待某个条件,另一个线程通知条件满足。

### 经典例子

```cpp
#include <mutex>
#include <condition_variable>
#include <queue>

std::queue<int> queue;
std::mutex mtx;
std::condition_variable cv;

// 生产者
void producer() {
    for (int i = 0; i < 10; ++i) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            queue.push(i);
        }
        cv.notify_one();  // 通知消费者
    }
}

// 消费者
void consumer() {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [] { return !queue.empty(); });

        int value = queue.front();
        queue.pop();
        lock.unlock();

        // 处理 value...
    }
}
```

### 项目中的例子

```cpp
// LiveStreamTask.h
class LiveStreamTask {
private:
    std::mutex pauseMutex_;
    std::condition_variable pauseCondition_;
    std::atomic<bool> isPaused_{false};
};

// LiveStreamTask.cpp - processLoop() 工作循环
void LiveStreamTask::processLoop() {
    while (!shouldStop_.load()) {
        // ========== 暂停检查点 ==========
        {
            std::unique_lock<std::mutex> lock(pauseMutex_);
            pauseCondition_.wait(lock, [this] {
                return !isPaused_.load() || shouldStop_.load();
            });
        }

        if (shouldStop_.load()) break;

        // 处理帧...
    }
}

// pause() 方法
bool LiveStreamTask::pause() {
    if (state_.load() != TaskState::RUNNING) {
        return false;
    }

    isPaused_.store(true);
    state_.store(TaskState::PAUSED);

    // 不需要 notify,因为线程会在下一次循环时检查 isPaused_

    return true;
}

// resume() 方法
bool LiveStreamTask::resume() {
    if (state_.load() != TaskState::PAUSED) {
        return false;
    }

    isPaused_.store(false);
    state_.store(TaskState::RUNNING);
    pauseCondition_.notify_all();  // 唤醒工作线程

    return true;
}
```

### 详细讲解

#### 基本用法

```cpp
std::mutex mtx;
std::condition_variable cv;
bool ready = false;

// 等待线程
void waiter() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });  // 等待 ready 为 true
    // 继续执行...
}

// 通知线程
void notifier() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();  // 或 notify_all()
}
```

#### wait() 的内部逻辑

```cpp
cv.wait(lock, predicate);

// 等价于:
while (!predicate()) {
    cv.wait(lock);  // 释放锁,等待通知,醒来后重新获取锁
}
```

#### notify_one() vs notify_all()

```cpp
cv.notify_one();   // 唤醒一个等待线程
cv.notify_all();   // 唤醒所有等待线程 (项目使用)
```

### 面试要点

**Q1: 为什么 wait() 需要 unique_lock 而不是 lock_guard?**

A:

- `wait()` 内部会 **释放锁** → 等待 → **重新获取锁**
- `unique_lock` 支持 `unlock()` 和 `lock()` 操作
- `lock_guard` 只能在析构时释放锁,不支持手动释放

```cpp
// ✅ 正确: unique_lock
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock);  // 内部会调用 lock.unlock() 和 lock.lock()

// ❌ 错误: lock_guard
std::lock_guard<std::mutex> lock(mtx);
cv.wait(lock);  // 编译错误: lock_guard 没有 unlock() 方法
```

**Q2: 什么是虚假唤醒 (Spurious Wakeup)?如何避免?**

A:

- **虚假唤醒**: `wait()` 可能在没有 `notify()` 的情况下返回 (操作系统行为)
- **避免方法**: 使用带谓词 (predicate) 的 `wait()`

```cpp
// ❌ 错误: 没有谓词,可能虚假唤醒
cv.wait(lock);
if (!ready) {
    // 虚假唤醒!ready 仍然是 false
}

// ✅ 正确: 使用谓词
cv.wait(lock, [] { return ready; });
// 即使虚假唤醒,也会重新检查 ready
```

**Q3: 项目中为什么用 notify_all() 而不是 notify_one()?**

A:

```cpp
pauseCondition_.notify_all();  // 唤醒所有等待线程
```

原因:

1. **安全性**: `notify_all()` 确保所有等待线程都被唤醒,不会遗漏
2. **简单性**: 不需要关心有多少线程在等待
3. **性能**: 本项目只有一个工作线程,`notify_all()` 和 `notify_one()` 效果相同

**使用建议**:

- **单消费者**: `notify_one()` 更高效
- **多消费者**: `notify_all()` 更安全

---

## 📌 知识点 4: 单例模式与 shared_ptr 别名构造函数

### 概念

单例模式保证全局只有一个实例,但在依赖注入场景下需要 `shared_ptr`,如何兼容?

### 经典例子

```cpp
// 单例类
class Singleton {
public:
    static Singleton& getInstance() {
        static Singleton instance;  // Meyers 单例
        return instance;
    }

private:
    Singleton() = default;
    ~Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
};

// 问题: 如何创建 shared_ptr<Singleton>?
// ❌ 错误: 构造函数是私有的
auto ptr = std::make_shared<Singleton>();

// ✅ 正确: 使用别名构造函数
Singleton& instance = Singleton::getInstance();
std::shared_ptr<Singleton> ptr(
    std::shared_ptr<Singleton>(),  // 空的控制块
    &instance                       // 指向单例的指针
);
```

### 项目中的例子

```cpp
// MqttClient 是单例
class MqttClient {
public:
    static MqttClient& getInstance();

private:
    MqttClient();   // 私有构造函数
    ~MqttClient();  // 私有析构函数
};

// TaskService 需要 shared_ptr<MqttClient>
class TaskService {
public:
    TaskService(std::shared_ptr<mqtt::MqttClient> mqtt);  // 不能为 nullptr
};

// 测试中的解决方案
std::shared_ptr<TaskService> createTestTaskService() {
    // 1. 获取单例引用
    mqtt::MqttClient& mqttInstance = mqtt::MqttClient::getInstance();

    // 2. 创建不拥有所有权的 shared_ptr
    std::shared_ptr<mqtt::MqttClient> mqttPtr(
        std::shared_ptr<mqtt::MqttClient>(),  // 空的控制块
        &mqttInstance                          // 指向单例的指针
    );

    // 3. 传递给 TaskService
    return std::make_shared<TaskService>(mqttPtr);
}
```

### 详细讲解

#### shared_ptr 的别名构造函数

```cpp
template<typename Y>
shared_ptr(const shared_ptr<Y>& r, element_type* p) noexcept;
```

**参数**:

- `r`: 提供引用计数控制块的 `shared_ptr` (可以为空)
- `p`: 实际指向的对象指针

**特点**:

- 新的 `shared_ptr` 与 `r` 共享控制块
- `get()` 返回 `p`
- 析构时只减少引用计数,不会删除 `p` (因为控制块为空)

#### 使用场景

1. **单例与依赖注入兼容** (项目使用)
2. **访问成员变量**: 通过对象的 `shared_ptr` 创建指向成员的 `shared_ptr`
3. **类型擦除**: 保持引用计数的同时改变指针类型

### 面试要点

**Q1: 为什么不能直接 make_shared 单例?**

A:

```cpp
// ❌ 错误
auto ptr = std::make_shared<MqttClient>();
// 编译错误: MqttClient::MqttClient() 是私有的
```

原因:

- `make_shared` 需要调用构造函数
- 单例的构造函数是私有的,外部无法访问
- 单例模式的核心就是防止外部创建实例

**Q2: 别名构造函数的 shared_ptr 会删除对象吗?**

A:

```cpp
std::shared_ptr<MqttClient> mqttPtr(
    std::shared_ptr<MqttClient>(),  // 空的控制块
    &mqttInstance
);

// mqttPtr 析构时:
// 1. 引用计数减少 (但控制块为空,计数始终为 0)
// 2. 不会调用 delete (因为没有真正的控制块)
// 3. mqttInstance 由单例自己管理生命周期
```

**答案**: 不会删除,因为控制块为空,没有真正的所有权。

**Q3: 这种方法的缺点是什么?**

A:
**缺点**:

1. **生命周期危险**: 如果单例被销毁,`shared_ptr` 成为悬挂指针
2. **违反 RAII**: `shared_ptr` 不再真正管理资源
3. **不适合生产环境**: 仅适用于测试或特殊场景

**更好的方案**:

- 使用 Mock 对象代替真实单例
- 重构单例为可注入的依赖

---

## 📌 知识点 5: RAII (Resource Acquisition Is Initialization)

### 概念

RAII 是 C++ 的核心设计理念:资源获取即初始化,资源释放即析构。

### 经典例子

```cpp
// ❌ 不使用 RAII (容易泄漏)
void processFile(const std::string& filename) {
    FILE* file = fopen(filename.c_str(), "r");
    if (!file) return;

    // 处理文件...

    if (error) {
        return;  // ❌ 忘记 fclose,内存泄漏!
    }

    fclose(file);
}

// ✅ 使用 RAII (自动清理)
class FileHandle {
public:
    explicit FileHandle(const std::string& filename)
        : file_(fopen(filename.c_str(), "r")) {}

    ~FileHandle() {
        if (file_) {
            fclose(file_);  // ✅ 自动释放
        }
    }

    FILE* get() const { return file_; }

private:
    FILE* file_;
};

void processFile(const std::string& filename) {
    FileHandle file(filename);
    if (!file.get()) return;

    // 处理文件...

    if (error) {
        return;  // ✅ 析构函数自动调用 fclose
    }

    // ✅ 函数结束时自动调用 fclose
}
```

### 项目中的例子

```cpp
// LiveStreamTask 的 RAII 设计
class LiveStreamTask {
public:
    LiveStreamTask(const TaskConfig& config, std::shared_ptr<TaskService> service)
        : config_(config), service_(std::move(service)) {
        // 构造函数: 初始化资源
        logger_.info("LiveStreamTask 已创建: taskId=" + config_.taskId);
    }

    ~LiveStreamTask() {
        // 析构函数: 自动释放资源
        stop();  // ✅ 确保线程停止
        logger_.info("LiveStreamTask 已销毁: taskId=" + config_.taskId);
    }

private:
    std::thread workerThread_;  // ✅ RAII 管理线程
};

// 使用示例 (测试6: RAII 资源自动清理)
void test_raii_resource_cleanup() {
    {
        // 在作用域内创建任务
        auto task = std::make_shared<LiveStreamTask>(config, service);
        task->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // ✅ 作用域结束,task 析构,自动调用 stop()
    }

    // ✅ 任务对象销毁,资源已自动清理
}
```

### 详细讲解

#### RAII 的核心思想

1. **获取资源 = 构造对象**: 在构造函数中获取资源 (内存、文件、锁、线程等)
2. **释放资源 = 析构对象**: 在析构函数中释放资源
3. **自动管理**: 利用 C++ 对象生命周期自动调用析构函数

#### 常见的 RAII 包装类

```cpp
// 1. 智能指针
std::unique_ptr<int> ptr = std::make_unique<int>(42);
// 析构时自动 delete

// 2. 锁管理
std::lock_guard<std::mutex> lock(mtx);
// 析构时自动 unlock

std::unique_lock<std::mutex> lock(mtx);
// 更灵活,支持手动 unlock/lock

// 3. 文件流
std::ifstream file("data.txt");
// 析构时自动关闭文件

// 4. 线程 (需要手动 join 或 detach)
std::thread t(func);
t.join();  // 必须显式调用
```

#### 项目中的 RAII 应用

```cpp
// 1. 智能指针管理对象
std::shared_ptr<TaskService> service;  // 自动删除

// 2. 互斥锁自动释放
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.framesProcessed++;
    // 作用域结束,自动 unlock
}

// 3. 线程生命周期管理
~LiveStreamTask() {
    stop();  // 确保线程停止
}
```

### 面试要点

**Q1: RAII 的优势是什么?**

A:
| 优势 | 说明 | 示例 |
|------|------|------|
| **异常安全** | 即使抛出异常,析构函数也会被调用 | 函数提前 return,锁自动释放 |
| **简化代码** | 不需要手动 try-finally | 不需要显式 unlock |
| **防止泄漏** | 编译器保证资源释放 | 忘记 delete 也不会泄漏 |
| **自动化** | 利用对象生命周期自动管理 | 作用域结束自动清理 |

**Q2: 为什么析构函数中调用 stop()?**

A:

```cpp
~LiveStreamTask() {
    stop();  // 幂等性操作
}
```

原因:

1. **防止遗漏**: 即使用户忘记调用 `stop()`,析构时也会自动停止
2. **异常安全**: 如果中途抛出异常,对象销毁时仍会正确清理
3. **RAII 原则**: 对象拥有线程资源,负责清理线程

**Q3: stop() 的幂等性是什么意思?**

A:
**幂等性** (Idempotence): 多次调用与一次调用效果相同

```cpp
void LiveStreamTask::stop() {
    TaskState currentState = state_.load();

    // ✅ 如果已经停止,直接返回 (幂等性)
    if (currentState != TaskState::RUNNING &&
        currentState != TaskState::PAUSED) {
        logger_.warn("任务未运行,无需停止");
        return;
    }

    // 停止线程...
}

// 可以多次调用
task->stop();
task->stop();  // ✅ 不会出错
task->stop();  // ✅ 不会出错
```

---

## 📌 知识点 6: 生产者-消费者模式

### 概念

生产者线程生产数据放入队列,消费者线程从队列取出数据处理。需要同步机制协调。

### 经典例子

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

std::queue<int> buffer;
std::mutex mtx;
std::condition_variable cv;
const size_t MAX_SIZE = 10;

// 生产者
void producer() {
    for (int i = 0; i < 100; ++i) {
        std::unique_lock<std::mutex> lock(mtx);

        // 等待队列有空间
        cv.wait(lock, [] { return buffer.size() < MAX_SIZE; });

        buffer.push(i);
        std::cout << "Produced: " << i << std::endl;

        cv.notify_all();  // 通知消费者
    }
}

// 消费者
void consumer() {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);

        // 等待队列有数据
        cv.wait(lock, [] { return !buffer.empty(); });

        int value = buffer.front();
        buffer.pop();
        std::cout << "Consumed: " << value << std::endl;

        cv.notify_all();  // 通知生产者
    }
}
```

### 项目中的例子

```cpp
// MediaFileTask.h
class MediaFileTask {
private:
    std::queue<std::string> fileQueue_;       // 文件队列
    std::mutex queueMutex_;                   // 队列互斥锁
    std::condition_variable queueCondition_;  // 条件变量
};

// MediaFileTask.cpp

// 生产者: DJI SDK 通知有新文件
void MediaFileTask::onMediaFileUpdate(const std::string& filePath) {
    logger_.info("收到文件通知: " + filePath);

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fileQueue_.push(filePath);  // 入队
    }

    queueCondition_.notify_one();  // 通知消费者
}

// 消费者: 工作线程处理文件
void MediaFileTask::processLoop() {
    logger_.info("工作线程已启动: taskId=" + config_.taskId);

    while (!shouldStop_.load()) {
        std::unique_lock<std::mutex> lock(queueMutex_);

        // 等待队列有数据
        queueCondition_.wait(lock, [this] {
            return !fileQueue_.empty() || shouldStop_.load();
        });

        if (shouldStop_.load()) break;

        // 取出文件
        if (!fileQueue_.empty()) {
            std::string filePath = fileQueue_.front();
            fileQueue_.pop();
            lock.unlock();  // 尽早释放锁

            // 处理文件 (不持有锁)
            readMediaFile(filePath);
        }
    }

    logger_.info("工作线程已退出: taskId=" + config_.taskId);
}
```

### 详细讲解

#### 核心组件

1. **共享队列**: `std::queue<T>` 存储数据
2. **互斥锁**: `std::mutex` 保护队列访问
3. **条件变量**: `std::condition_variable` 实现等待/通知

#### 流程图

```
生产者线程                       消费者线程
    |                               |
    v                               v
获取锁 ─────────┐             获取锁 ─────────┐
    |           |                 |           |
检查队列是否满? |            检查队列是否空? |
    |           |                 |           |
满 → wait()释放锁          空 → wait()释放锁
    |           |                 |           |
不满 → push()  |            不空 → pop()     |
    |           |                 |           |
notify_all()   |             notify_all()    |
    |           |                 |           |
释放锁 ←───────┘             释放锁 ←───────┘
```

#### 关键技巧

```cpp
// 1. 尽早释放锁
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    fileQueue_.push(filePath);
}  // 锁自动释放
queueCondition_.notify_one();  // 不持有锁时通知

// 2. 最小锁粒度
std::unique_lock<std::mutex> lock(queueMutex_);
std::string filePath = fileQueue_.front();
fileQueue_.pop();
lock.unlock();  // ✅ 尽早释放锁

readMediaFile(filePath);  // ✅ 不持有锁时处理文件

// 3. 停止条件
queueCondition_.wait(lock, [this] {
    return !fileQueue_.empty() || shouldStop_.load();
});
//                              ^^^^^^^^^^^^^^^^^^^^^^
//                              停止条件,避免死等
```

### 面试要点

**Q1: 为什么要在 wait() 的谓词中检查 shouldStop\_?**

A:

```cpp
// ❌ 错误: 没有停止条件
queueCondition_.wait(lock, [this] {
    return !fileQueue_.empty();
});
// 问题: 如果队列始终为空,线程永远等待,无法停止

// ✅ 正确: 添加停止条件
queueCondition_.wait(lock, [this] {
    return !fileQueue_.empty() || shouldStop_.load();
});
// 即使队列为空,shouldStop_ 为 true 时也会退出等待
```

**Q2: 为什么在处理文件前要释放锁?**

A:

```cpp
// ❌ 不好: 持有锁时处理文件
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    std::string filePath = fileQueue_.front();
    fileQueue_.pop();

    readMediaFile(filePath);  // ❌ 耗时操作,阻塞生产者
}

// ✅ 更好: 尽早释放锁
{
    std::unique_lock<std::mutex> lock(queueMutex_);
    std::string filePath = fileQueue_.front();
    fileQueue_.pop();
    lock.unlock();  // ✅ 立即释放锁
}
readMediaFile(filePath);  // ✅ 不持有锁,生产者可以继续入队
```

**原因**:

- 减少锁持有时间,提高并发性能
- 避免死锁 (如果 `readMediaFile` 需要其他锁)
- `readMediaFile` 是耗时操作,不应阻塞队列

**Q3: notify_one() 还是 notify_all()?**

A:
| 场景 | 选择 | 原因 |
|------|------|------|
| 单生产者-单消费者 | `notify_one()` | 只有一个线程需要唤醒 |
| 单生产者-多消费者 | `notify_one()` | 一次只需要唤醒一个消费者 |
| 多生产者-单消费者 | `notify_one()` | 只有一个消费者 |
| 多生产者-多消费者 | `notify_all()` | 可能需要唤醒多个线程 |

项目中: **单生产者(DJI SDK)-单消费者(工作线程)** → 用 `notify_one()`

---

## 🎯 综合面试题

### 题目 1: 如何实现一个线程安全的任务停止机制?

**答案**:

```cpp
class Task {
public:
    void start() {
        shouldStop_.store(false);
        workerThread_ = std::thread(&Task::processLoop, this);
    }

    void stop() {
        // 1. 设置停止标志
        shouldStop_.store(true);

        // 2. 唤醒可能在等待的线程
        condition_.notify_all();

        // 3. 等待线程退出
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
    }

private:
    void processLoop() {
        while (!shouldStop_.load()) {
            // 处理任务...
        }
    }

    std::atomic<bool> shouldStop_{false};
    std::condition_variable condition_;
    std::thread workerThread_;
};
```

**要点**:

1. 使用 `atomic` 标志位保证线程安全
2. `notify_all()` 唤醒等待线程
3. `join()` 确保线程完全退出
4. 析构函数中调用 `stop()` (RAII)

---

### 题目 2: 对比 mutex 和 atomic 的使用场景

**答案**:

| 特性         | `std::atomic`                     | `std::mutex`                |
| ------------ | --------------------------------- | --------------------------- |
| **适用数据** | 简单类型(int, bool, 指针)         | 任意类型,多个变量           |
| **性能**     | 无锁,CPU 指令级                   | 有锁,可能阻塞               |
| **复杂度**   | 单个变量的读写                    | 临界区,保护多个操作         |
| **用法**     | `state_.load()`, `state_.store()` | `lock_guard`, `unique_lock` |
| **开销**     | 极小 (几个 CPU 指令)              | 较大 (系统调用)             |

**项目示例**:

```cpp
// ✅ 用 atomic
std::atomic<TaskState> state_;      // 单个状态变量
std::atomic<bool> shouldStop_;      // 简单标志位

// ✅ 用 mutex
struct Statistics {
    uint64_t framesProcessed;
    uint64_t eventsReported;
    double avgFps;
};
std::mutex statsMutex_;             // 保护多个字段
Statistics stats_;
```

---

### 题目 3: 如何避免死锁 (Deadlock)?

**答案**:

**死锁四个必要条件**:

1. 互斥 (Mutual Exclusion)
2. 持有并等待 (Hold and Wait)
3. 不可抢占 (No Preemption)
4. 循环等待 (Circular Wait)

**避免方法**:

1. **固定加锁顺序** (破坏循环等待)

```cpp
// ❌ 可能死锁
void transfer(Account& from, Account& to, int amount) {
    std::lock_guard<std::mutex> lock1(from.mtx);
    std::lock_guard<std::mutex> lock2(to.mtx);
    // ...
}
// 线程1: transfer(A, B)  锁顺序: A → B
// 线程2: transfer(B, A)  锁顺序: B → A  ← 死锁!

// ✅ 固定顺序
void transfer(Account& from, Account& to, int amount) {
    Account& first = (&from < &to) ? from : to;
    Account& second = (&from < &to) ? to : from;

    std::lock_guard<std::mutex> lock1(first.mtx);
    std::lock_guard<std::mutex> lock2(second.mtx);
    // ...
}
```

2. **使用 std::lock** (同时锁定多个锁)

```cpp
std::lock(mtx1, mtx2);  // 同时锁定,避免死锁
std::lock_guard<std::mutex> lock1(mtx1, std::adopt_lock);
std::lock_guard<std::mutex> lock2(mtx2, std::adopt_lock);
```

3. **最小锁粒度** (尽早释放锁)

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);
    // 只在这里持有锁
}  // 锁释放
// 后续操作不持有锁
```

4. **使用 unique_lock 的 try_lock**

```cpp
std::unique_lock<std::mutex> lock1(mtx1, std::defer_lock);
if (!lock1.try_lock()) {
    return false;  // 获取锁失败,避免等待
}
```

---

## 📚 总结

### 本次学习的核心技术

1. ✅ `std::thread` - 线程创建与管理
2. ✅ `std::atomic` - 无锁原子操作
3. ✅ `std::mutex` - 互斥锁保护共享资源
4. ✅ `std::condition_variable` - 线程间通信
5. ✅ `std::unique_lock` / `std::lock_guard` - RAII 锁管理
6. ✅ RAII 模式 - 自动资源管理
7. ✅ 生产者-消费者模式
8. ✅ 状态机设计
9. ✅ 单例模式与依赖注入

### 面试高频问题

1. ❓ join() 和 detach() 的区别?
2. ❓ 为什么 condition_variable 需要 unique_lock?
3. ❓ 什么是虚假唤醒?如何避免?
4. ❓ atomic 和 mutex 的区别?
5. ❓ 如何避免死锁?
6. ❓ RAII 的优势是什么?
7. ❓ compare_exchange_strong 的作用?
8. ❓ 生产者-消费者模式如何实现?

### 项目中的最佳实践

1. ✅ 状态用 `atomic`,复杂数据用 `mutex`
2. ✅ 条件变量必须用谓词,防止虚假唤醒
3. ✅ 尽早释放锁,最小锁粒度
4. ✅ 析构函数中释放资源 (RAII)
5. ✅ 停止线程前设置标志位并 notify
6. ✅ join() 前检查 joinable()

---

**日期**: 2025-11-10  
**状态**: Day 5 完成 ✅  
**下一步**: Day 6 - Device 模块集成
