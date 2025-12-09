# 八股知识点 - C++多线程篇

> **说明**: 本文档包含 C++ 多线程相关的八股知识点，所有示例均来自 DJI ESDK 项目的真实代码。
> 
> **最后更新**: 2025-10-29  
> **项目示例**: TaskManager单元测试全部通过 ✅

---

## 目录

1. [线程安全 (Thread Safety)](#线程安全)
2. [互斥锁 (Mutex)](#互斥锁)
3. [RAII锁管理 (Lock Guard)](#raii锁管理)
4. [mutable关键字](#mutable关键字)
5. [std::queue的线程安全用法](#stdqueue的线程安全用法) ⭐ 新增
6. [单元测试的隔离性](#单元测试的隔离性) ⭐ 新增

---

## 📌 线程安全 (Thread Safety)

### 知识点

**线程安全**是指多个线程同时访问共享数据时，程序能够正确执行，不会出现数据竞争(Data Race)、死锁(Deadlock)等问题。

**常见的线程安全问题**:

1. **数据竞争**: 多个线程同时读写同一数据
2. **死锁**: 多个线程互相等待对方释放锁
3. **活锁**: 线程不断重试但永远无法完成
4. **优先级反转**: 低优先级线程持有锁，阻塞高优先级线程

---

### 项目中的例子

**TaskManager 的线程安全设计**:

```cpp
// include/esdk_sophon/task/TaskManager.h
class TaskManager {
private:
    mutable std::mutex mutex_;  // 保护所有共享状态

    std::map<std::string, TaskState> runningTasks_;  // 运行中的任务
    std::queue<TaskConfig> waitingQueue_;            // 等待队列

public:
    /**
     * @brief 启动任务 - 线程安全
     */
    bool startTask(const TaskConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护

        // 访问共享数据 runningTasks_
        if (runningTasks_.find(config.taskId) != runningTasks_.end()) {
            return false;
        }

        runningTasks_.emplace(config.taskId, TaskState(...));
        return true;
    }

    /**
     * @brief 获取任务状态 - 线程安全（const 方法也要加锁）
     */
    TaskStatus getTaskStatus(const std::string& taskId) const {
        std::lock_guard<std::mutex> lock(mutex_);  // const 方法也要加锁

        auto it = runningTasks_.find(taskId);
        if (it == runningTasks_.end()) {
            return TaskStatus::CANCELLED;
        }

        return it->second.status;
    }

    /**
     * @brief 媒体文件更新 - 可能被多个线程调用
     */
    void onMediaFileUpdate(const std::string& mediaFile) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 遍历所有任务
        for (auto& pair : runningTasks_) {
            // ... 处理 ...
        }
    }
};
```

**为什么需要线程安全？**

1. **MQTT 线程**: 收到任务消息，调用 `startTask()`
2. **ESDK 回调线程**: 媒体文件更新，调用 `onMediaFileUpdate()`
3. **主线程**: 查询任务状态，调用 `getTaskStatus()`
4. **推理线程**: 处理完成，调用 `onTaskCompleted()`

**如果不加锁会怎样？**

```cpp
// 线程1: MQTT 线程
manager.startTask(config1);
// 正在执行: runningTasks_.emplace(...)

// 线程2: 主线程（同时执行）
auto status = manager.getTaskStatus("task1");
// 正在执行: runningTasks_.find(...)

// ❌ 数据竞争！
// - std::map 的内部红黑树结构被破坏
// - 可能崩溃、死循环、返回错误数据
```

---

### 详细讲解

#### mutable 关键字

**为什么需要 mutable mutex？**

```cpp
class TaskManager {
private:
    mutable std::mutex mutex_;  // 必须是 mutable

public:
    TaskStatus getTaskStatus(const std::string& taskId) const {
        //                                                ^^^^^ const 方法

        std::lock_guard<std::mutex> lock(mutex_);
        // ❌ 如果 mutex_ 不是 mutable，这里编译错误：
        //    不能在 const 方法中修改成员变量

        // ...
    }
};
```

**mutable 的含义**:

- `mutable` 成员变量可以在 `const` 方法中修改
- 用于"逻辑上不改变对象状态，但实现上需要修改"的场景
- 典型应用：锁、缓存、惰性求值

**示例对比**:

```cpp
// ❌ 不使用 mutable
class TaskManager {
private:
    std::mutex mutex_;

public:
    // ❌ 这个方法不能声明为 const
    TaskStatus getTaskStatus(const std::string& taskId) /* const */ {
        std::lock_guard<std::mutex> lock(mutex_);  // 编译错误
        // ...
    }
};

// ✅ 使用 mutable
class TaskManager {
private:
    mutable std::mutex mutex_;  // mutable

public:
    // ✅ 可以声明为 const
    TaskStatus getTaskStatus(const std::string& taskId) const {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 编译通过
        // ...
    }
};
```

**为什么 getTaskStatus 应该是 const？**

```cpp
// 语义上是只读操作
TaskStatus status = manager.getTaskStatus("task1");  // 不修改 manager

// 如果不是 const，无法用于 const 引用
void printStatus(const TaskManager& manager) {
    auto status = manager.getTaskStatus("task1");  // ❌ 编译错误（如果不是 const）
}
```

---

#### 数据竞争检测

**什么是数据竞争？**

当两个或多个线程满足以下条件时，就会发生数据竞争：

1. 访问同一内存位置
2. 至少一个是写操作
3. 没有使用同步机制（如锁）

**示例**:

```cpp
class Counter {
private:
    int count_ = 0;  // 共享数据

public:
    // ❌ 线程不安全
    void increment() {
        count_++;  // 读-修改-写，三个步骤
        // 1. 读取 count_
        // 2. 加 1
        // 3. 写回 count_
    }

    int get() const {
        return count_;  // 读操作
    }
};

// 两个线程同时调用 increment()
// 线程1: 读取 count_ = 0
// 线程2: 读取 count_ = 0  (还没写回)
// 线程1: count_ = 1
// 线程2: count_ = 1  (覆盖了线程1的结果)
// 最终 count_ = 1，但应该是 2！
```

**修复方式 1: 使用互斥锁**

```cpp
class Counter {
private:
    int count_ = 0;
    mutable std::mutex mutex_;

public:
    void increment() {
        std::lock_guard<std::mutex> lock(mutex_);
        count_++;  // 原子操作
    }

    int get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }
};
```

**修复方式 2: 使用原子变量**

```cpp
class Counter {
private:
    std::atomic<int> count_{0};  // 原子变量

public:
    void increment() {
        count_++;  // 硬件级原子操作，无需加锁
    }

    int get() const {
        return count_.load();
    }
};
```

---

### 经典例子（对比）

**教科书例子**（银行账户）:

```cpp
class BankAccount {
private:
    double balance_;
    mutable std::mutex mutex_;

public:
    void deposit(double amount) {
        std::lock_guard<std::mutex> lock(mutex_);
        balance_ += amount;
    }

    void withdraw(double amount) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (balance_ >= amount) {
            balance_ -= amount;
        }
    }

    double getBalance() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return balance_;
    }
};
```

**本项目例子**（TaskManager）:

```cpp
class TaskManager {
private:
    std::map<std::string, TaskState> runningTasks_;
    std::queue<TaskConfig> waitingQueue_;
    mutable std::mutex mutex_;

public:
    // 多个线程可能同时调用
    bool startTask(const TaskConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. 检查任务是否存在（读操作）
        if (runningTasks_.find(config.taskId) != runningTasks_.end()) {
            return false;
        }

        // 2. 添加新任务（写操作）
        runningTasks_.emplace(config.taskId, TaskState(...));

        return true;
    }

    // const 方法也需要加锁（读操作）
    const TaskState* getTaskInfo(const std::string& taskId) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = runningTasks_.find(taskId);
        if (it == runningTasks_.end()) {
            return nullptr;
        }

        return &(it->second);  // 返回指针
    }
};
```

**区别**:

- 教科书：简单的数值操作
- 项目：复杂的容器操作（std::map, std::queue）

---

### 面试要点

#### Q1: 什么是数据竞争？如何避免？

**A**:

**数据竞争**发生在:

1. 多个线程访问同一内存
2. 至少一个是写操作
3. 没有同步机制

**避免方法**:

1. **互斥锁** (`std::mutex`)
2. **原子变量** (`std::atomic`)
3. **无锁数据结构** (Lock-Free)
4. **线程本地存储** (`thread_local`)
5. **不可变数据** (Immutable Data)

---

#### Q2: const 方法为什么也需要加锁？

**A**:

```cpp
// 线程1: 调用 const 方法
TaskStatus status = manager.getTaskStatus("task1");  // 读

// 线程2: 调用非 const 方法
manager.stopTask("task1");  // 写 - 删除 runningTasks_["task1"]

// ❌ 如果不加锁:
// - 线程1 正在遍历 map
// - 线程2 删除了元素，破坏了 map 结构
// - 线程1 可能访问已删除的内存 -> 崩溃
```

**即使是只读操作，也需要保护**！

---

#### Q3: std::lock_guard vs std::unique_lock 的区别？

**A**:

| 特性        | std::lock_guard | std::unique_lock |
| ----------- | --------------- | ---------------- |
| 构造时加锁  | ✅ 自动         | ✅ 可选          |
| 析构时解锁  | ✅ 自动         | ✅ 自动          |
| 手动 unlock | ❌ 不支持       | ✅ 支持          |
| 延迟加锁    | ❌ 不支持       | ✅ 支持          |
| 条件变量    | ❌ 不支持       | ✅ 支持          |
| 移动语义    | ❌ 不可移动     | ✅ 可移动        |
| 开销        | 低              | 稍高             |

**std::lock_guard** - 简单场景:

```cpp
void simpleFunction() {
    std::lock_guard<std::mutex> lock(mutex_);
    // 临界区
}  // 自动解锁
```

**std::unique_lock** - 复杂场景:

```cpp
void complexFunction() {
    std::unique_lock<std::mutex> lock(mutex_);

    // 1. 手动解锁
    lock.unlock();
    // 做一些不需要锁的工作
    lock.lock();

    // 2. 配合条件变量
    cv.wait(lock, []{return ready;});
}
```

**本项目中的选择**: 使用 `lock_guard`，因为场景简单，不需要 `unique_lock` 的额外功能。

---

#### Q4: 如何避免死锁？

**A**:

**死锁的四个必要条件**:

1. 互斥条件：资源不能共享
2. 持有并等待：持有资源的同时等待其他资源
3. 不可抢占：资源不能被强制释放
4. 循环等待：A 等 B，B 等 A

**避免方法**:

1. **统一加锁顺序**:

```cpp
// ❌ 可能死锁
void thread1() {
    std::lock_guard<std::mutex> lock1(mutex1_);
    std::lock_guard<std::mutex> lock2(mutex2_);
}

void thread2() {
    std::lock_guard<std::mutex> lock2(mutex2_);  // 顺序反了！
    std::lock_guard<std::mutex> lock1(mutex1_);
}

// ✅ 统一顺序
void thread1() {
    std::lock_guard<std::mutex> lock1(mutex1_);  // 先 mutex1
    std::lock_guard<std::mutex> lock2(mutex2_);  // 后 mutex2
}

void thread2() {
    std::lock_guard<std::mutex> lock1(mutex1_);  // 先 mutex1
    std::lock_guard<std::mutex> lock2(mutex2_);  // 后 mutex2
}
```

2. **使用 std::lock 同时获取多个锁**:

```cpp
void safeFunction() {
    std::unique_lock<std::mutex> lock1(mutex1_, std::defer_lock);  // 延迟加锁
    std::unique_lock<std::mutex> lock2(mutex2_, std::defer_lock);

    std::lock(lock1, lock2);  // 原子地获取两个锁，避免死锁

    // 临界区
}
```

3. **最小化锁的范围**:

```cpp
// ❌ 锁的范围太大
void badFunction() {
    std::lock_guard<std::mutex> lock(mutex_);

    complexComputation();  // 耗时操作，不需要锁
    runningTasks_.emplace(...);  // 需要锁
}

// ✅ 最小化锁范围
void goodFunction() {
    auto result = complexComputation();  // 无锁

    {
        std::lock_guard<std::mutex> lock(mutex_);
        runningTasks_.emplace(...);  // 只在必要时加锁
    }
}
```

---

### 易错点

#### ❌ 错误 1: 返回临界区内数据的引用/指针

```cpp
// ❌ 危险！
const TaskState& getTaskInfo(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = runningTasks_.find(taskId);
    if (it != runningTasks_.end()) {
        return it->second;  // ❌ 返回引用
    }
    // ...
}  // 锁被释放

// 调用者使用返回的引用
const TaskState& state = manager.getTaskInfo("task1");
// ❌ 锁已经释放，但还在使用引用
// 其他线程可能正在修改 runningTasks_["task1"]
// 数据竞争！
```

**解决方案 1: 返回副本**

```cpp
// ✅ 安全
TaskState getTaskInfo(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = runningTasks_.find(taskId);
    if (it != runningTasks_.end()) {
        return it->second;  // ✅ 返回副本
    }
    // ...
}
```

**解决方案 2: 返回指针 + 使用者小心**

```cpp
// ⚠️ 返回指针（调用者需要理解风险）
const TaskState* getTaskInfo(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = runningTasks_.find(taskId);
    if (it != runningTasks_.end()) {
        return &(it->second);  // 返回指针
    }
    return nullptr;
}

// 使用者必须立即拷贝
if (auto* state = manager.getTaskInfo("task1")) {
    TaskState copy = *state;  // 立即拷贝
    // 使用 copy，不要使用 state
}
```

---

#### ❌ 错误 2: 长时间持有锁

```cpp
// ❌ 推理时持有锁，阻塞其他线程
void processMediaFile(const std::string& taskId, const std::string& file) {
    std::lock_guard<std::mutex> lock(mutex_);  // ❌ 不要这样！

    // 推理可能需要几十毫秒到几秒
    auto result = runInference(file);  // 阻塞其他所有线程！

    runningTasks_[taskId].processedMediaFiles++;
}

// ✅ 只在必要时持有锁
void processMediaFile(const std::string& taskId, const std::string& file) {
    // 1. 推理时不持有锁
    auto result = runInference(file);  // 其他线程可以访问 manager

    // 2. 只在更新状态时加锁
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = runningTasks_.find(taskId);
        if (it != runningTasks_.end()) {
            it->second.processedMediaFiles++;
        }
    }  // 锁立即释放
}
```

---

#### ❌ 错误 3: 递归加锁

```cpp
class TaskManager {
private:
    std::mutex mutex_;  // 注意：不是 recursive_mutex

    void helperFunction() {
        std::lock_guard<std::mutex> lock(mutex_);
        // ...
    }

public:
    void publicFunction() {
        std::lock_guard<std::mutex> lock(mutex_);  // 第一次加锁

        helperFunction();  // ❌ 死锁！尝试再次加锁
    }
};
```

**解决方案 1: 使用 recursive_mutex**

```cpp
class TaskManager {
private:
    std::recursive_mutex mutex_;  // 允许递归加锁

public:
    void publicFunction() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        helperFunction();  // ✅ 可以递归加锁
    }
};
```

**解决方案 2: 分离函数（推荐）**

```cpp
class TaskManager {
private:
    std::mutex mutex_;

    // 内部函数：假设已经持有锁
    void helperFunctionUnsafe() {
        // 不加锁，由调用者保证
    }

public:
    void publicFunction() {
        std::lock_guard<std::mutex> lock(mutex_);
        helperFunctionUnsafe();  // ✅ 调用不加锁的版本
    }
};
```

---

### 项目应用总结

**本项目中的线程安全设计**:

1. **TaskManager**: 所有公共方法都用 `lock_guard` 保护
2. **单一锁策略**: 一个 mutex 保护所有共享状态（简单、不易死锁）
3. **最小化锁范围**: 推理等耗时操作在锁外执行
4. **const 方法加锁**: 读操作也需要保护

**线程安全检查清单**:

- [ ] 所有访问共享数据的方法都加锁
- [ ] const 方法也加锁（如果访问共享数据）
- [ ] 不在锁内执行耗时操作
- [ ] 不返回临界区内数据的引用
- [ ] 统一的加锁顺序（如果有多个锁）
- [ ] 使用 RAII 锁（避免忘记解锁）

---

## 📌 std::mutex 与锁

### 知识点

**互斥锁 (Mutex - Mutual Exclusion)** 是最基本的同步原语，用于保护临界区。

**C++ 提供的锁类型**:

1. `std::mutex` - 基本互斥锁
2. `std::recursive_mutex` - 递归互斥锁
3. `std::timed_mutex` - 支持超时的互斥锁
4. `std::shared_mutex` - 读写锁 (C++17)

**RAII 锁包装器**:

1. `std::lock_guard` - 基本 RAII 锁
2. `std::unique_lock` - 灵活的 RAII 锁
3. `std::shared_lock` - 共享锁 (C++17)

---

### 项目中的例子

**std::lock_guard 的使用**:

```cpp
// src/task/TaskManager.cpp
bool TaskManager::startTask(const TaskConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);  // RAII: 构造时加锁

    // 临界区
    if (runningTasks_.find(config.taskId) != runningTasks_.end()) {
        return false;  // ✅ 提前返回，lock 自动解锁
    }

    runningTasks_.emplace(config.taskId, TaskState(...));

    return true;
}  // ✅ 离开作用域，lock 自动解锁
```

**为什么用 RAII 锁？**

```cpp
// ❌ 手动加锁 - 容易出错
bool startTask(const TaskConfig& config) {
    mutex_.lock();

    if (runningTasks_.find(config.taskId) != runningTasks_.end()) {
        // ❌ 忘记解锁就返回了！死锁！
        return false;
    }

    runningTasks_.emplace(...);

    // ❌ 如果这里抛出异常，锁永远不会释放！

    mutex_.unlock();
    return true;
}

// ✅ RAII 锁 - 自动管理
bool startTask(const TaskConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);  // 自动加锁

    if (...) return false;  // ✅ 自动解锁

    runningTasks_.emplace(...);  // ✅ 异常也会自动解锁

    return true;
}  // ✅ 自动解锁
```

---

### 详细讲解

#### 锁的粒度

**粗粒度锁** (Coarse-Grained Locking):

```cpp
class TaskManager {
private:
    std::mutex mutex_;  // 一个锁保护所有数据

    std::map<std::string, TaskState> runningTasks_;
    std::queue<TaskConfig> waitingQueue_;

public:
    void method1() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 访问 runningTasks_
    }

    void method2() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 访问 waitingQueue_
    }

    // method1 和 method2 会互斥，即使访问不同的数据
};
```

✅ **优点**: 简单、不易死锁  
❌ **缺点**: 并发性差

**细粒度锁** (Fine-Grained Locking):

```cpp
class TaskManager {
private:
    std::mutex runningTasksMutex_;  // 保护 runningTasks_
    std::mutex waitingQueueMutex_;  // 保护 waitingQueue_

    std::map<std::string, TaskState> runningTasks_;
    std::queue<TaskConfig> waitingQueue_;

public:
    void method1() {
        std::lock_guard<std::mutex> lock(runningTasksMutex_);
        // 访问 runningTasks_
    }

    void method2() {
        std::lock_guard<std::mutex> lock(waitingQueueMutex_);
        // 访问 waitingQueue_
    }

    // method1 和 method2 可以并发执行
};
```

✅ **优点**: 并发性好  
❌ **缺点**: 复杂、容易死锁

**本项目选择**: 粗粒度锁（一个 mutex 保护所有状态）

- 原因：TaskManager 的操作都很快，不需要高并发
- 优势：简单、易维护、不会死锁

---

#### 读写锁 (std::shared_mutex)

**场景**: 多读少写

```cpp
#include <shared_mutex>

class TaskManager {
private:
    mutable std::shared_mutex mutex_;  // C++17 读写锁
    std::map<std::string, TaskState> runningTasks_;

public:
    // 写操作：独占锁
    bool startTask(const TaskConfig& config) {
        std::unique_lock<std::shared_mutex> lock(mutex_);  // 独占
        runningTasks_.emplace(...);
        return true;
    }

    // 读操作：共享锁
    TaskStatus getTaskStatus(const std::string& taskId) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);  // 共享
        auto it = runningTasks_.find(taskId);
        // ...
    }

    // 多个 getTaskStatus 可以并发执行（共享锁）
    // 但 startTask 会等待所有读者（独占锁）
};
```

✅ **优点**: 提高读并发  
❌ **缺点**: 写操作可能饥饿（一直有读者）

---

### 经典例子（对比）

**教科书例子**（线程安全栈）:

```cpp
template<typename T>
class ThreadSafeStack {
private:
    std::stack<T> stack_;
    mutable std::mutex mutex_;

public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        stack_.push(value);
    }

    bool pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stack_.empty()) {
            return false;
        }
        value = stack_.top();
        stack_.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stack_.empty();
    }
};
```

**本项目例子**（TaskManager）:

```cpp
class TaskManager {
private:
    std::map<std::string, TaskState> runningTasks_;
    std::queue<TaskConfig> waitingQueue_;
    mutable std::mutex mutex_;

public:
    bool startTask(const TaskConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 复杂的逻辑
        if (runningTasks_.find(config.taskId) != runningTasks_.end()) {
            return false;
        }

        // 检查冲突
        TaskConflictAction action = checkTaskConflict(config);
        if (action == TaskConflictAction::QUEUE) {
            waitingQueue_.push(config);
            return true;
        }

        // 检查资源
        TpuResourceStatus tpuStatus = getTpuResourceStatus();
        if (!tpuStatus.hasEnoughResource(...)) {
            waitingQueue_.push(config);
            return true;
        }

        // 创建任务
        runningTasks_.emplace(config.taskId, TaskState(...));

        return true;
    }
};
```

**区别**:

- 教科书：简单的栈操作
- 项目：复杂的业务逻辑（冲突检测、资源管理、队列调度）

---

### 面试要点

#### Q1: std::lock_guard 的实现原理？

**A**:

```cpp
// 简化的实现
template<typename Mutex>
class lock_guard {
public:
    explicit lock_guard(Mutex& m) : mutex_(m) {
        mutex_.lock();  // 构造时加锁
    }

    ~lock_guard() {
        mutex_.unlock();  // 析构时解锁
    }

    // 禁止拷贝和移动
    lock_guard(const lock_guard&) = delete;
    lock_guard& operator=(const lock_guard&) = delete;

private:
    Mutex& mutex_;
};

// 使用
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 临界区
}  // lock 析构，自动解锁
```

**关键点**:

1. RAII 原则：构造获取资源，析构释放资源
2. 禁止拷贝：避免重复解锁
3. 栈对象：利用栈展开机制保证解锁

---

#### Q2: 什么时候用 unique_lock 而不是 lock_guard？

**A**:

| 场景                 | 推荐          |
| -------------------- | ------------- |
| 简单的临界区保护     | `lock_guard`  |
| 需要手动 unlock/lock | `unique_lock` |
| 配合条件变量         | `unique_lock` |
| 延迟加锁             | `unique_lock` |
| 需要移动语义         | `unique_lock` |
| 尝试加锁（try_lock） | `unique_lock` |

**示例**:

```cpp
// lock_guard: 简单场景
void simpleFunction() {
    std::lock_guard<std::mutex> lock(mutex_);
    // 临界区
}

// unique_lock: 复杂场景
void complexFunction() {
    std::unique_lock<std::mutex> lock(mutex_);

    // 1. 手动解锁
    lock.unlock();
    doSomethingWithoutLock();
    lock.lock();

    // 2. 条件变量
    cv.wait(lock, []{return ready;});

    // 3. 延迟加锁
    std::unique_lock<std::mutex> lock2(mutex2_, std::defer_lock);
    // ... 稍后加锁 ...
    lock2.lock();
}
```

---

#### Q3: 如何避免优先级反转？

**A**:

**优先级反转**：低优先级线程持有锁，阻塞高优先级线程

```
时间线:
0ms: 低优先级线程A 获取锁
1ms: 高优先级线程B 尝试获取锁，阻塞
2ms: 中优先级线程C 抢占A的CPU
3ms-10ms: C 一直运行
10ms: C 结束，A 恢复，释放锁
11ms: B 终于获取到锁

结果: 高优先级线程B 等待了 10ms！
```

**解决方案**:

1. **优先级继承** (Priority Inheritance):

   - 低优先级线程获取锁时，临时提升到等待者的最高优先级
   - POSIX: `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)`

2. **优先级上限** (Priority Ceiling):

   - 给锁设置一个优先级上限
   - 持有锁的线程提升到上限优先级

3. **避免优先级调度** (Avoid Priority Scheduling):
   - 使用时间片轮转调度
   - 本项目采用这种方式（没有严格的实时要求）

---

### 易错点

#### ❌ 错误 1: 锁的作用域错误

```cpp
// ❌ 错误：锁的作用域太小
{
    std::lock_guard<std::mutex> lock(mutex_);
}  // 锁已释放
auto it = runningTasks_.find(taskId);  // ❌ 没有锁保护！

// ✅ 正确：锁的作用域覆盖整个临界区
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = runningTasks_.find(taskId);  // ✅ 有锁保护
    // ...
}
```

---

#### ❌ 错误 2: 忘记 const 方法也要加锁

```cpp
class TaskManager {
private:
    std::mutex mutex_;  // ❌ 不是 mutable

public:
    TaskStatus getTaskStatus(const std::string& taskId) const {
        // ❌ 编译错误：不能在 const 方法中加锁
        std::lock_guard<std::mutex> lock(mutex_);
        // ...
    }
};

// ✅ 正确
class TaskManager {
private:
    mutable std::mutex mutex_;  // ✅ mutable

public:
    TaskStatus getTaskStatus(const std::string& taskId) const {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 可以加锁
        // ...
    }
};
```

---

#### ❌ 错误 3: 双重加锁

```cpp
// ❌ 错误
void outerFunction() {
    std::lock_guard<std::mutex> lock(mutex_);
    innerFunction();  // ❌ 死锁！
}

void innerFunction() {
    std::lock_guard<std::mutex> lock(mutex_);  // 再次加锁
    // ...
}

// ✅ 解决方案1: 使用 recursive_mutex
std::recursive_mutex mutex_;  // 允许递归加锁

// ✅ 解决方案2: 分离函数
void outerFunction() {
    std::lock_guard<std::mutex> lock(mutex_);
    innerFunctionUnsafe();  // 调用不加锁的版本
}

void innerFunctionUnsafe() {
    // 不加锁，假设调用者已持有锁
}
```

---

### 项目应用总结

**本项目中的锁策略**:

1. **单一锁**: 一个 `std::mutex` 保护所有共享状态
2. **RAII 锁**: 全部使用 `std::lock_guard`
3. **mutable**: 允许 const 方法加锁
4. **最小化范围**: 只在必要时持有锁

**代码模板**:

```cpp
class Manager {
private:
    mutable std::mutex mutex_;

    // 共享数据
    std::map<std::string, Data> data_;

public:
    // 写操作
    void modify(const std::string& key, const Data& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_[key] = value;
    }

    // 读操作（const方法）
    Data get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) {
            return it->second;  // 返回副本
        }
        return Data();
    }
};
```

---

## � std::queue的线程安全用法 ⭐ 新增

### 知识点

**std::queue** 本身**不是线程安全的**,在多线程环境下需要额外保护。更重要的是,使用queue时要注意操作顺序,避免未定义行为。

**关键原则**: **先复制,后删除**

---

### 经典例子

```cpp
#include <queue>
#include <mutex>
#include <string>

std::queue<std::string> taskQueue;
std::mutex queueMutex;

// ❌ 错误示例
void processTaskWrong() {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    if (!taskQueue.empty()) {
        auto task = taskQueue.front();  // 获取引用
        taskQueue.pop();                // 删除元素
        
        // task现在是悬垂引用! 可能已被销毁
        processData(task);  // ⚠️ 未定义行为!
    }
}

// ✅ 正确示例
void processTaskCorrect() {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    if (!taskQueue.empty()) {
        std::string task = taskQueue.front();  // 复制数据
        taskQueue.pop();                       // 再删除
        
        // task是独立副本,安全使用
        processData(task);  // ✅ 安全!
    }
}
```

---

### 项目中的例子

**TaskManager::processWaitingQueue() - 从队列启动任务**:

```cpp
// src/task/TaskManager.cpp
void TaskManager::processWaitingQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 遍历等待队列,尝试启动任务
    while (!waitingQueue_.empty() && 
           runningTasks_.size() < maxConcurrentTasks_) {
        
        // ⭐ 关键: 先复制所有需要的数据
        types::TaskConfig config = waitingQueue_.front();
        std::string taskIdStr = std::to_string(config.taskID);
        
        // 创建任务状态(使用复制的config)
        TaskState taskState(taskIdStr, config);
        
        // ⭐ 现在才删除队首元素
        waitingQueue_.pop();
        
        // ⭐ 使用已复制的数据,安全!
        logger_.info("从等待队列启动任务: " + taskIdStr);
        
        // 根据任务类型设置状态
        bool isLiveview = (config.source == 0);
        taskState.status = isLiveview ? 
            TaskStatus::PROCESSING : TaskStatus::WAITING_MEDIA;
        
        // 插入运行列表
        runningTasks_.emplace(taskIdStr, std::move(taskState));
    }
}
```

**为什么这样写?**

1. `waitingQueue_.front()` 返回的是**引用**
2. `waitingQueue_.pop()` 会**销毁**队首元素
3. 如果先 `pop()` 再访问 `config`,就是访问已销毁的对象 ⚠️
4. 所以必须**先复制后删除**

---

### 详细讲解

**std::queue的内部实现**:

```cpp
template<typename T>
class queue {
private:
    std::deque<T> container_;  // 默认用deque实现

public:
    T& front() { return container_.front(); }  // 返回引用!
    
    void pop() { 
        container_.pop_front();  // 销毁元素!
    }
};
```

**为什么front()返回引用?**

- 避免拷贝大对象
- 但需要程序员保证元素存活

**pop()为什么不返回值?**

- 异常安全:如果拷贝时抛异常,元素已被删除但未返回
- 设计哲学:删除和访问分离

---

### 面试要点

**Q1: 为什么 `auto data = queue.front(); queue.pop();` 可能出错?**

**A**: 
- `front()` 返回的是**引用**,不是副本
- 如果 `auto` 推导为引用类型,`pop()` 后引用失效
- 正确写法:显式指定类型 `T data = queue.front();` 或 `auto data = T(queue.front());`

**Q2: 如何设计线程安全的队列?**

**A**:
```cpp
template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;

public:
    // 弹出并返回值(异常安全)
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = queue_.front();  // 先复制
        queue_.pop();            // 再删除
        return true;
    }
    
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
    }
};
```

**Q3: 为什么STL的queue不合并pop()和front()?**

**A**: 
1. **异常安全**:如果返回值时抛异常,元素已删除无法恢复
2. **效率**:有时只需删除不需要值
3. **分离关注点**:访问和修改分离(单一职责原则)

---

### 易错点总结

| 写法 | 问题 | 后果 |
|------|------|------|
| `auto x = q.front(); q.pop();` | `auto`可能推导为引用 | 悬垂引用 ⚠️ |
| `q.pop(); auto x = q.front();` | 先删后访问 | 访问错误元素 ⚠️ |
| `T x = q.front(); q.pop();` | ✅ 显式类型,先复制后删 | 正确 ✅ |
| `T x(q.front()); q.pop();` | ✅ 拷贝构造 | 正确 ✅ |

---

## 📌 单元测试的隔离性 ⭐ 新增

### 知识点

**测试隔离性**是指每个测试用例应该**独立执行**,不依赖其他测试的状态,也不影响其他测试。

**测试隔离的重要性**:

1. **可重复性**:每次运行结果一致
2. **可调试性**:单独运行某个测试就能复现问题
3. **并行化**:测试可以并行执行
4. **维护性**:修改一个测试不影响其他测试

---

### 经典例子

```cpp
// ❌ 错误示例:测试之间相互影响
class CalculatorTests {
    static int globalCounter;  // 全局状态!

public:
    void test1() {
        globalCounter = 0;
        assert(calculate(5) == 5);
        globalCounter++;  // 修改全局状态
    }
    
    void test2() {
        // 依赖test1的执行! ⚠️
        assert(globalCounter == 1);
        assert(calculate(10) == 10);
    }
};

// ✅ 正确示例:每个测试独立
class CalculatorTests {
public:
    void test1() {
        int counter = 0;  // 局部状态
        assert(calculate(5) == 5);
    }
    
    void test2() {
        int counter = 0;  // 独立的局部状态
        assert(calculate(10) == 10);
    }
};
```

---

### 项目中的例子

**TaskManager测试 - 单例模式的隔离挑战**:

```cpp
// tests/test_task_manager.cpp

/**
 * @brief 测试3: 启动视频流任务
 * 
 * ❌ 原始版本 - 没有清理,影响后续测试
 */
void test_start_liveview_task_wrong() {
    TaskManager& manager = TaskManager::getInstance();
    
    TaskConfig config = createTestConfig(2001, true);
    bool started = manager.startTask(config);
    
    // 验证
    assert(started);
    
    // ⚠️ 没有清理! 任务2001会影响测试4
}

/**
 * @brief 测试4: 视频流任务冲突
 * 
 * ❌ 受测试3影响,失败!
 */
void test_liveview_conflict_wrong() {
    TaskManager& manager = TaskManager::getInstance();
    
    // 尝试启动第一个视频流任务
    TaskConfig config1 = createTestConfig(3001, true);
    bool started1 = manager.startTask(config1);
    
    // ⚠️ 因为测试3的2001还在运行,3001被加入队列!
    // 测试失败: started1==true 但 status==CANCELLED
}

/**
 * @brief ✅ 正确版本 - 加入清理
 */
void test_start_liveview_task_correct() {
    TaskManager& manager = TaskManager::getInstance();
    
    TaskConfig config = createTestConfig(2001, true);
    bool started = manager.startTask(config);
    
    assert(started);
    
    // ✅ 测试结束前清理
    manager.stopTask(std::to_string(config.taskID));
}

void test_liveview_conflict_correct() {
    TaskManager& manager = TaskManager::getInstance();
    
    // ✅ 测试开始前确保环境干净
    auto existingIds = manager.getRunningTaskIds();
    for (const auto& id : existingIds) {
        manager.stopTask(id);
    }
    
    // 现在可以正常测试
    TaskConfig config1 = createTestConfig(3001, true);
    bool started1 = manager.startTask(config1);
    
    assert(started1);
    
    // ✅ 测试结束后清理
    manager.stopTask(std::to_string(config1.taskID));
}
```

---

### 详细讲解

**单例模式下的测试隔离问题**:

1. **问题**:单例在整个程序生命周期中只有一个实例
2. **后果**:所有测试共享同一个实例的状态
3. **解决**:每个测试前后进行清理

**清理策略**:

| 策略 | 优点 | 缺点 | 适用场景 |
|------|------|------|----------|
| 测试前清理(setUp) | 保证环境干净 | 可能影响其他测试 | 可控的单例 |
| 测试后清理(tearDown) | 不影响当前测试 | 最后一个测试可能不清理 | 大多数场景 ✅ |
| 前后都清理 | 最安全 | 代码冗余 | 关键测试 |

---

### 面试要点

**Q1: 单例模式如何保证测试隔离?**

**A**:
1. **提供清理方法**:允许重置单例状态
2. **测试前后清理**:每个测试负责清理自己的修改
3. **考虑依赖注入**:测试时注入模拟对象

```cpp
class Singleton {
private:
    std::map<std::string, Data> state_;
    
public:
    static Singleton& getInstance();
    
    // ⭐ 提供测试用的清理方法
    void resetForTesting() {
        state_.clear();
    }
};

// 测试中使用
void test_something() {
    auto& instance = Singleton::getInstance();
    instance.resetForTesting();  // 清理
    
    // 测试...
    
    instance.resetForTesting();  // 再次清理
}
```

**Q2: 为什么测试隔离很重要?**

**A**:
1. **可调试性**:单独运行失败的测试就能复现问题
2. **并行执行**:隔离的测试可以并行,加快CI速度
3. **可维护性**:修改测试不会破坏其他测试
4. **可靠性**:测试顺序不影响结果

**Q3: 如何检测测试之间的依赖?**

**A**:
1. **随机顺序运行**:打乱测试执行顺序
2. **单独运行**:每个测试单独执行应该通过
3. **反向运行**:倒序执行测试
4. **并行运行**:多线程执行测试

---

### 项目应用总结

**本项目的测试隔离实践**:

```cpp
// 通用模板
void test_feature() {
    // 1. 准备:获取单例
    auto& manager = TaskManager::getInstance();
    
    // 2. 清理:确保环境干净(可选)
    auto existingIds = manager.getRunningTaskIds();
    for (const auto& id : existingIds) {
        manager.stopTask(id);
    }
    
    // 3. 执行:测试功能
    TaskConfig config = createTestConfig(1001);
    bool result = manager.startTask(config);
    
    // 4. 验证:检查结果
    assert(result == true);
    
    // 5. 清理:移除测试数据 ⭐ 关键!
    manager.stopTask("1001");
}
```

**教训**:
- ✅ 每个测试结束后清理自己创建的资源
- ✅ 关键测试开始前也进行清理
- ✅ 使用自动化工具检测测试依赖
- ✅ 文档化测试的前置条件

---

## �📚 参考资料

- 《C++ Concurrency in Action》 - Anthony Williams
- 《Effective Modern C++》条款 35-40: 并发
- C++ Reference: Thread Support Library
- 《xUnit Test Patterns》 - Gerard Meszaros (测试隔离)

---

**编写日期**: 2025-10-28  
**最后更新**: 2025-10-29  
**版本**: v1.1  
**适用项目**: DJI ESDK On Sophon
