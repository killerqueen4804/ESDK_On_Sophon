# Task 模块详解 ⭐ 核心业务模块

> **模块定位**：Task 模块是整个系统的业务核心，负责任务的创建、执行和管理。
> **关键词**：工厂模式、观察者模式、生产者-消费者模式、状态机

---

## 📁 模块结构

```
src/task/
├── CMakeLists.txt
├── TaskManager.cpp        # 任务管理器（单例 + 工厂）
├── TaskService.cpp        # 业务服务层（检测 + 事件发布）
├── MediaFileTask.cpp      # 媒体文件任务（照片分析）
├── LiveStreamTask.cpp     # 直播流任务（实时视频）
└── TaskTypes.cpp          # 类型定义

include/esdk_sophon/task/
├── TaskManager.h
├── TaskService.h
├── MediaFileTask.h
├── LiveStreamTask.h
├── ITask.h               # 任务接口（抽象基类）
└── TaskTypes.h           # 类型和枚举定义
```

---

## 🏗️ 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      TaskManager (单例)                          │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ tasks_: map<string, TaskPtr>                             │   │
│  │ taskService_: shared_ptr<TaskService>                    │   │
│  └─────────────────────────────────────────────────────────┘   │
│                           │                                     │
│                     createTask()                                │
│                           │                                     │
│            ┌──────────────┴──────────────┐                     │
│            ▼                              ▼                     │
│  ┌─────────────────┐           ┌─────────────────┐             │
│  │ LiveStreamTask  │           │ MediaFileTask   │             │
│  │  (视频流任务)    │           │  (照片分析任务)  │             │
│  └────────┬────────┘           └────────┬────────┘             │
│           │                              │                      │
│           └──────────┬───────────────────┘                     │
│                      ▼                                          │
│           ┌─────────────────────┐                              │
│           │    TaskService      │                              │
│           │ (检测 + 事件发布)   │                              │
│           └─────────────────────┘                              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📌 1. TaskManager - 任务管理器

### 1.1 设计模式：单例 + 工厂 + 注册表

```cpp
/**
 * TaskManager 融合了三种设计模式：
 * 1. 单例模式 (Singleton) - 全局唯一的管理器
 * 2. 工厂模式 (Factory) - 根据类型创建具体任务
 * 3. 注册表模式 (Registry) - 维护所有活跃任务的映射
 */
class TaskManager {
public:
    // ⭐ Meyers' Singleton - C++11 线程安全单例
    static TaskManager& getInstance() {
        static TaskManager instance;  // 局部静态变量，线程安全
        return instance;
    }

private:
    TaskManager();  // 私有构造函数

    // 禁止拷贝和赋值
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    // 任务注册表
    std::unordered_map<std::string, TaskPtr> tasks_;

    // 互斥锁保护
    mutable std::mutex mutex_;
};
```

### 1.2 工厂方法：createTask()

```cpp
TaskPtr TaskManager::createTask(const TaskConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 检查任务是否已存在
    if (tasks_.find(config.taskId) != tasks_.end()) {
        logger.warning("任务已存在: " + config.taskId);
        return tasks_[config.taskId];
    }

    // 2. ⭐ 工厂方法 - 根据类型创建具体任务
    TaskPtr task = nullptr;

    switch (config.type) {
        case TaskType::DETECTION_LIVESTREAM:
            task = std::make_shared<LiveStreamTask>(config, taskService_);
            break;

        case TaskType::DETECTION_MEDIAFILE:
            task = std::make_shared<MediaFileTask>(config, taskService_);
            break;

        default:
            logger.error("不支持的任务类型");
            return nullptr;
    }

    // 3. 注册到任务表
    tasks_[config.taskId] = task;

    return task;
}
```

### 📚 知识点：工厂模式 (Factory Pattern)

#### 是什么？

工厂模式是一种创建型设计模式，它将对象的创建逻辑封装在一个方法中，调用者只需要知道"我要什么类型"，而不需要知道"如何创建"。

#### 为什么用？

1. **解耦**：调用者不依赖具体类，只依赖接口
2. **扩展**：新增任务类型只需修改工厂方法
3. **集中管理**：所有创建逻辑在一处

#### 经典例子

```cpp
// ❌ 不使用工厂模式
void handleCommand(const std::string& type) {
    if (type == "stream") {
        auto task = new LiveStreamTask(...);  // 调用者依赖具体类
    } else if (type == "file") {
        auto task = new MediaFileTask(...);   // 修改需要改多处
    }
}

// ✅ 使用工厂模式
void handleCommand(const std::string& type) {
    auto task = TaskManager::getInstance().createTask(config);
    // 调用者只知道 TaskPtr，不知道具体类型
}
```

#### 面试要点

```
Q: 工厂模式有几种变体？
A: 1. 简单工厂 (Static Factory) - 一个方法，switch 分支
   2. 工厂方法 (Factory Method) - 子类重写创建方法
   3. 抽象工厂 (Abstract Factory) - 创建一族相关对象
   本项目使用的是简单工厂。

Q: 工厂模式和直接 new 的区别？
A: 1. 工厂可以返回接口类型，隐藏实现细节
   2. 工厂可以返回缓存对象（如单例）
   3. 工厂可以返回子类对象（多态）
   4. 工厂可以集中管理对象创建逻辑
```

---

## 📌 2. ITask - 任务接口

### 2.1 抽象基类定义

```cpp
/**
 * @brief 任务接口 - 所有任务的抽象基类
 *
 * 采用模板方法模式 (Template Method Pattern)：
 * - 公共方法定义骨架 (start/stop/pause/resume)
 * - 抽象方法由子类实现 (execute)
 */
class ITask {
public:
    virtual ~ITask() = default;  // 虚析构函数

    // ==================== 生命周期控制 ====================
    virtual bool start() = 0;    // 启动任务
    virtual void stop() = 0;     // 停止任务
    virtual bool pause() = 0;    // 暂停任务
    virtual bool resume() = 0;   // 恢复任务

    // ==================== 状态查询 ====================
    virtual bool isRunning() const = 0;
    virtual TaskState getState() const = 0;
    virtual const TaskConfig& getConfig() const = 0;
    virtual TaskStatistics getStatistics() const = 0;

    // ==================== 事件处理 ====================
    virtual void onTaskEnd() = 0;  // 航线结束回调

    // ==================== 回调设置 ====================
    virtual void setStateCallback(TaskCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;

protected:
    // ⭐ 模板方法 - 由子类实现
    virtual void execute() = 0;
    virtual void notifyStateChanged(TaskState newState) = 0;
    virtual void notifyError(const std::string& error) = 0;
};
```

### 📚 知识点：纯虚函数与抽象类

#### 定义

- **纯虚函数**：`virtual void foo() = 0;` 没有实现的虚函数
- **抽象类**：包含至少一个纯虚函数的类，不能实例化

#### 为什么需要？

```cpp
// ❌ 不使用抽象类
class TaskManager {
    void startTask(LiveStreamTask* task);  // 只能处理一种类型
    void startTask(MediaFileTask* task);   // 需要为每种类型写一个
};

// ✅ 使用抽象类
class TaskManager {
    void startTask(ITask* task);  // 处理所有任务类型
};
```

#### 面试要点

```
Q: 为什么析构函数必须是虚函数？
A: 当通过基类指针删除派生类对象时，如果析构函数不是虚函数，
   只会调用基类析构函数，导致派生类资源泄漏。

   // ❌ 内存泄漏
   ITask* task = new MediaFileTask();
   delete task;  // 只调用 ~ITask()，不调用 ~MediaFileTask()

   // ✅ 正确释放
   virtual ~ITask() = default;  // 基类析构函数为虚
   delete task;  // 先调用 ~MediaFileTask()，再调用 ~ITask()

Q: 纯虚函数可以有实现吗？
A: 可以！但必须在类外定义，子类仍需重写。
   常用于提供默认实现：
   class ITask {
       virtual void log() = 0;  // 纯虚函数
   };
   void ITask::log() { std::cout << "Default log"; }  // 类外实现
```

---

## 📌 3. MediaFileTask - 媒体文件任务

### 3.1 工作流程

```
┌─────────────────────────────────────────────────────────────────┐
│                     MediaFileTask 工作流程                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  start()                                                        │
│    │                                                            │
│    ├── 1. 配置 EventCache (事件缓存)                            │
│    ├── 2. registerMediaFilesObserver() ← DJI SDK               │
│    ├── 3. 初始化超时控制 (lastFileTime_)                        │
│    ├── 4. 启动工作线程 → execute()                             │
│    └── 5. 启动监控线程 → monitorLoop()                         │
│                                                                 │
│  DJI SDK 回调线程                                               │
│    │                                                            │
│    └── onMediaFileUpdate(file)                                  │
│          │                                                      │
│          ├── 过滤视频文件 (只处理 JPG)                          │
│          ├── 更新 lastFileTime_ (超时重置)                      │
│          └── fileQueue_.push(file)  →  notify_one()            │
│                                      │                          │
│  工作线程 (execute)                  │                          │
│    │                                 │                          │
│    └── while (running_)              │                          │
│          │                           │                          │
│          └── wait_for(queueCv_) ←────┘                          │
│                │                                                │
│                ├── file = fileQueue_.pop()                      │
│                ├── readMediaFile(file, imageData)               │
│                └── processFile(file, imageData)                 │
│                      │                                          │
│                      ├── dumpMediaFile() (保存本地)             │
│                      ├── parseExif() (GPS、时间戳)              │
│                      ├── TaskService::processFrame()            │
│                      │     └── 检测 → 事件构建 → MQTT 发布     │
│                      └── 更新统计信息                           │
│                                                                 │
│  监控线程 (monitorLoop)                                         │
│    │                                                            │
│    └── while (running_)                                         │
│          │                                                      │
│          ├── sleep(5s)                                          │
│          └── if (shouldComplete())                              │
│                │                                                │
│                └── completeTask()                               │
│                      ├── publishTaskAnalysisResult()            │
│                      ├── cleanupFiles()                         │
│                      └── restoreDjiSettings()                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 生产者-消费者模式

```cpp
// ==================== 数据结构 ====================
std::queue<edge_sdk::MediaFile> fileQueue_;  // 文件队列
std::mutex queueMutex_;                       // 队列锁
std::condition_variable queueCv_;             // 队列条件变量

// ==================== 生产者：DJI SDK 回调 ====================
edge_sdk::ErrorCode MediaFileTask::onMediaFileUpdate(const edge_sdk::MediaFile& file) {
    // 过滤视频文件
    if (file.file_type == edge_sdk::MediaFile::kFileTypeMp4) {
        return edge_sdk::kOk;  // 跳过
    }

    // ⭐ 更新超时计时器
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    // ⭐ 生产：将文件放入队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fileQueue_.push(file);
    }

    // ⭐ 通知消费者
    queueCv_.notify_one();

    return edge_sdk::kOk;
}

// ==================== 消费者：工作线程 ====================
void MediaFileTask::execute() {
    while (running_) {
        // ⭐ 等待文件（消费）
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCv_.wait(lock, [this] {
            return !fileQueue_.empty() || !running_;
        });

        if (!running_) break;
        if (fileQueue_.empty()) continue;

        // 取出文件
        edge_sdk::MediaFile file = fileQueue_.front();
        fileQueue_.pop();
        lock.unlock();  // ⭐ 尽早释放锁

        // 处理文件（耗时操作在锁外进行）
        std::vector<uint8_t> imageData;
        if (readMediaFile(file, imageData)) {
            processFile(file, imageData);
        }
    }
}
```

### 📚 知识点：生产者-消费者模式

#### 是什么？

一种多线程协作模式：

- **生产者**：产生数据，放入缓冲区
- **消费者**：从缓冲区取出数据，处理
- **缓冲区**：解耦生产和消费的速度差异

#### 为什么用？

```
DJI SDK 回调          工作线程
    │                    │
    │ 文件1              │
    ├──────► Queue ──────┤
    │ 文件2     ▲        │ 处理文件1
    ├──────────┘         │
    │ 文件3              │ 处理文件2
    ├──────────►         │
                         │ 处理文件3

1. 解耦：SDK 回调快速返回，不阻塞
2. 缓冲：应对突发大量文件
3. 并发：生产和消费可以并行
```

#### 条件变量三件套

```cpp
std::queue<T> queue;        // 1. 共享数据
std::mutex mutex;           // 2. 互斥锁（保护 queue）
std::condition_variable cv; // 3. 条件变量（通知/等待）

// 生产者
{
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(item);
}
cv.notify_one();  // 通知一个等待的消费者

// 消费者
{
    std::unique_lock<std::mutex> lock(mutex);  // ⚠️ 必须用 unique_lock
    cv.wait(lock, [&] { return !queue.empty(); });  // 等待条件
    auto item = queue.front();
    queue.pop();
}
```

#### 面试要点

```
Q: 为什么消费者必须用 unique_lock 而不是 lock_guard？
A: wait() 内部需要临时释放锁（让生产者能获取锁放数据），
   lock_guard 不支持手动解锁，unique_lock 支持。

Q: wait() 的第二个参数（lambda）有什么作用？
A: 防止虚假唤醒 (Spurious Wakeup)。
   即使被唤醒，也要检查条件是否真的满足。
   等价于：
   while (!condition) {
       cv.wait(lock);
   }

Q: notify_one() 和 notify_all() 的区别？
A: notify_one(): 唤醒一个等待的线程（性能好）
   notify_all(): 唤醒所有等待的线程（适合广播场景）
```

### 3.3 超时自动完成机制

```cpp
// ==================== 超时控制成员 ====================
std::chrono::steady_clock::time_point lastFileTime_;  // 最后收到文件的时间
std::mutex timerMutex_;
std::atomic<bool> taskEnded_{false};                  // 航线是否结束
static constexpr int TIMEOUT_SECONDS = 60;            // 60秒超时

// ==================== 航线结束回调 ====================
void MediaFileTask::onTaskEnd() {
    logger_.info("📢 收到 device_task_end，标记航线结束");

    // ⭐ 关键：只标记，不停止！
    // 原因：照片可能还在 4G 网络传输中
    taskEnded_.store(true);

    // 重置超时计时器
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }
}

// ==================== 监控线程 ====================
void MediaFileTask::monitorLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (shouldComplete()) {
            completeTask();
            break;
        }
    }
}

bool MediaFileTask::shouldComplete() {
    std::lock_guard<std::mutex> lock(timerMutex_);

    // 条件1：航线任务已结束
    if (!taskEnded_.load()) {
        return false;
    }

    // 条件2：60秒无新文件
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastFileTime_
    ).count();

    return elapsed >= TIMEOUT_SECONDS;
}
```

### 📚 知识点：std::atomic

#### 是什么？

原子类型，保证操作的原子性（不可分割），无需额外加锁。

#### 为什么用？

```cpp
// ❌ 非原子操作（有竞态条件）
bool flag = false;

// 线程A
flag = true;  // 写操作可能被分割

// 线程B
if (flag) { ... }  // 可能读到中间状态

// ✅ 原子操作
std::atomic<bool> flag{false};

// 线程A
flag.store(true);  // 原子写

// 线程B
if (flag.load()) { ... }  // 原子读
```

#### 常用操作

```cpp
std::atomic<int> counter{0};

counter.store(10);           // 原子写
int val = counter.load();    // 原子读
counter++;                   // 原子递增
counter.fetch_add(5);        // 原子加法
bool old = counter.exchange(20);  // 原子交换
```

#### 面试要点

```
Q: atomic 和 mutex 的区别？
A: 1. atomic 用于简单类型（bool, int, pointer）
   2. mutex 用于复杂操作或多个变量的组合操作
   3. atomic 通常用 CPU 硬件指令实现，性能更好
   4. mutex 有阻塞、唤醒开销

Q: 什么情况下 atomic 不够用？
A: 复合操作（多个操作需要原子执行）：
   // ❌ 即使两个操作都是 atomic，组合也不是原子的
   if (counter.load() == 0) {  // 读
       counter.store(1);        // 写
   }  // 两个操作之间可能被其他线程插入

   // ✅ 使用 compare_exchange
   int expected = 0;
   counter.compare_exchange_strong(expected, 1);
```

---

## 📌 4. LiveStreamTask - 直播流任务

### 4.1 与 MediaFileTask 的差异

| 特性      | MediaFileTask       | LiveStreamTask       |
| --------- | ------------------- | -------------------- |
| 数据源    | 文件队列            | H.264 视频流         |
| 处理方式  | 逐张图片            | 实时帧 (30 FPS)      |
| GPS 来源  | EXIF 元数据         | 无人机实时数据       |
| 上报策略  | 每张立即上报        | 间隔上报（节省带宽） |
| onTaskEnd | 仅标记，等待超时    | 立即停止             |
| 线程模型  | 工作线程 + 监控线程 | 工作线程 + 检测线程  |

### 4.2 异步检测架构

```
┌─────────────────────────────────────────────────────────────────┐
│                     LiveStreamTask 线程模型                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  DJI SDK 回调线程                                               │
│    │                                                            │
│    └── onH264Data(buf, len)                                     │
│          │                                                      │
│          └── h264Queue_.push(data)  →  notify_one()            │
│                                      │                          │
│  工作线程 (execute)                  │                          │
│    │                                 │                          │
│    └── while (running_)              │                          │
│          │                           │                          │
│          ├── h264Data = h264Queue_.pop()                        │
│          ├── h264Decoder_.decode(h264Data)  → cv::Mat          │
│          │                                                      │
│          ├── ⭐ 尝试提交帧到检测线程 (try_lock)                 │
│          │     if (lock.owns_lock() && !newFrameAvailable_)    │
│          │         detectionInputFrame_ = frame                 │
│          │         newFrameAvailable_ = true                    │
│          │         detectionCv_.notify_one()                    │
│          │                                                      │
│          ├── ⭐ 获取最新检测结果 (lock)                         │
│          │     boxes = currentDetections_                       │
│          │                                                      │
│          ├── VisionUtils::drawDetections(frame, boxes)         │
│          └── rtmpStreamer_->pushFrame(frame)  → RTMP 推流      │
│                                                                 │
│  检测线程 (detectionLoop)                                       │
│    │                                                            │
│    └── while (running_)                                         │
│          │                                                      │
│          ├── wait_for(detectionCv_)                             │
│          │     if (newFrameAvailable_)                          │
│          │         processFrame = detectionInputFrame_          │
│          │         newFrameAvailable_ = false                   │
│          │                                                      │
│          ├── TaskService::processFrame(processFrame)            │
│          │     └── 检测 + 事件构建 + MQTT 发布                  │
│          │                                                      │
│          └── currentDetections_ = boxes  (更新结果)             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 4.3 异步检测的关键代码

```cpp
// ==================== 成员变量 ====================
std::unique_ptr<std::thread> detectionThread_;  // 检测线程
std::mutex detectionMutex_;                     // 输入帧锁
std::condition_variable detectionCv_;           // 条件变量
cv::Mat detectionInputFrame_;                   // 待检测帧（输入）
std::atomic<bool> newFrameAvailable_{false};    // 新帧标志

std::mutex resultMutex_;                        // 结果锁
std::vector<BoundingBox> currentDetections_;    // 检测结果（输出）

// ==================== 工作线程：提交帧 ====================
// 使用 try_lock 避免阻塞视频流
{
    std::unique_lock<std::mutex> lock(detectionMutex_, std::try_to_lock);
    if (lock.owns_lock() && !newFrameAvailable_) {
        frame.copyTo(detectionInputFrame_);  // 深拷贝
        newFrameAvailable_ = true;
        detectionCv_.notify_one();
    }
    // else: 检测线程忙碌，跳过当前帧
}

// ==================== 检测线程：处理帧 ====================
void LiveStreamTask::detectionLoop() {
    cv::Mat processFrame;

    while (running_) {
        // 1. 等待新帧
        {
            std::unique_lock<std::mutex> lock(detectionMutex_);
            detectionCv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return newFrameAvailable_ || !running_;
            });

            if (!running_) break;
            if (!newFrameAvailable_) continue;  // 超时或虚假唤醒

            detectionInputFrame_.copyTo(processFrame);
            newFrameAvailable_ = false;
        }

        // 2. 执行检测（耗时操作在锁外）
        std::vector<BoundingBox> boxes;
        service_->processFrame(processFrame, config_, boxes);

        // 3. 更新结果
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            currentDetections_ = boxes;
        }
    }
}
```

### 📚 知识点：std::try_lock 和非阻塞锁

#### 为什么需要？

```
视频流：30 FPS = 33ms/帧
检测器：50-100ms/帧

如果用普通 lock()：
帧1 → 检测中 → 帧2 等待 → 帧3 等待 → 帧4 等待 → ...
                                               ↑
                                          队列堆积！

使用 try_lock：
帧1 → 检测中 → 帧2 跳过 → 帧3 跳过 → 帧4 提交 → 检测中
                                               ↑
                                          保持流畅！
```

#### 用法

```cpp
std::mutex mutex;

// 方式1：直接 try_lock
if (mutex.try_lock()) {
    // 成功获取锁
    // ... 操作 ...
    mutex.unlock();  // ⚠️ 必须手动解锁！
}

// 方式2：unique_lock + try_to_lock（推荐，RAII）
std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
if (lock.owns_lock()) {
    // 成功获取锁
    // ... 操作 ...
}  // 自动解锁
```

---

## 📌 5. TaskService - 业务服务层

### 5.1 Pimpl 惯用法

```cpp
// ==================== 头文件 (TaskService.h) ====================
class TaskService {
public:
    TaskService(std::shared_ptr<mqtt::MqttClient> mqtt);
    ~TaskService();  // ⚠️ 必须在 .cpp 中定义

    bool processFrame(const cv::Mat& frame, const TaskConfig& config,
                     std::vector<BoundingBox>& outBoxes,
                     const std::string& fileName = "");

private:
    struct Impl;                    // ⭐ 前向声明
    std::unique_ptr<Impl> impl_;    // ⭐ Pimpl 指针
};

// ==================== 源文件 (TaskService.cpp) ====================
struct TaskService::Impl {
    std::shared_ptr<mqtt::MqttClient> mqtt;
    std::unique_ptr<vision::IDetector> detector;
    std::mutex detectorMutex;
    core::Logger& logger;

    Impl(std::shared_ptr<mqtt::MqttClient> m)
        : mqtt(m), logger(core::Logger::getInstance()) {
        // 初始化检测器
        detector = vision::DetectorFactory::create(...);
    }
};

TaskService::TaskService(std::shared_ptr<mqtt::MqttClient> mqtt)
    : impl_(std::make_unique<Impl>(mqtt)) {
}

TaskService::~TaskService() = default;  // ⚠️ 必须在 .cpp 中定义！
```

### 📚 知识点：Pimpl 惯用法 (Pointer to Implementation)

#### 是什么？

将类的私有实现细节移到一个独立的 Impl 类中，主类只持有 Impl 的指针。

#### 为什么用？

```cpp
// ❌ 不使用 Pimpl
// TaskService.h
#include "Vision.h"      // 暴露依赖
#include "MqttClient.h"  // 修改 Vision.h 会导致所有 include TaskService.h 的文件重新编译

class TaskService {
private:
    Vision* vision_;          // 实现细节暴露在头文件
    MqttClient* mqtt_;
    std::map<...> cache_;     // ABI 不稳定
};

// ✅ 使用 Pimpl
// TaskService.h
class TaskService {
    struct Impl;              // 前向声明，不暴露细节
    std::unique_ptr<Impl> impl_;
};

// TaskService.cpp
#include "Vision.h"      // 依赖隐藏在 .cpp
#include "MqttClient.h"

struct TaskService::Impl { ... };
```

#### 优势

1. **编译隔离**：修改 Impl 不会触发依赖重新编译
2. **ABI 稳定**：类大小固定（只有一个指针），可用于动态库
3. **隐藏实现**：私有成员不暴露在头文件

#### 面试要点

```
Q: 为什么析构函数必须在 .cpp 中定义？
A: 因为 unique_ptr 需要知道 Impl 的完整定义才能调用析构函数。
   如果在 .h 中用 = default，编译器看不到 Impl 的定义会报错。

Q: Pimpl 的缺点？
A: 1. 额外的内存分配（new Impl）
   2. 额外的间接访问（指针解引用）
   3. 代码更复杂
   适用于：库开发、需要 ABI 稳定的场景
```

### 5.2 帧处理流程

```cpp
bool TaskService::processFrame(const cv::Mat& frame,
                              const TaskConfig& config,
                              std::vector<BoundingBox>& outBoxes,
                              const std::string& fileName) {
    // 1. 目标检测
    std::vector<BoundingBox> boxes;
    if (!detectObjects(frame, config, boxes)) {
        return false;
    }

    // 2. 过滤目标（只保留配置中关心的类别）
    boxes = filterByEventTypes(boxes, config);
    outBoxes = boxes;

    if (boxes.empty()) {
        return false;  // 无目标
    }

    // 3. 检查上报间隔（直播流任务限流）
    if (config.type == TaskType::DETECTION_LIVESTREAM) {
        if (!shouldReportEvent(config.taskId, config.reportIntervalSec)) {
            return false;
        }
    }

    // 4. 构建事件
    DetectionEvent event = buildEvent(frame, boxes, config,
                                     config.eventTypes[0], fileName);

    // 5. 发布事件
    return publishEvent(event, config.deviceSn);
}
```

---

## 📌 6. 关键知识点总结

### 设计模式

| 模式     | 位置                    | 作用             |
| -------- | ----------------------- | ---------------- |
| 单例     | TaskManager             | 全局唯一管理器   |
| 工厂     | TaskManager::createTask | 根据类型创建任务 |
| 观察者   | MediaFilesObserver      | DJI SDK 文件通知 |
| 模板方法 | ITask                   | 定义任务骨架     |
| Pimpl    | TaskService             | 隐藏实现细节     |

### 线程安全

| 技术                    | 用途               |
| ----------------------- | ------------------ |
| std::mutex              | 保护共享数据       |
| std::atomic             | 简单标志位         |
| std::condition_variable | 线程间通信         |
| mutable                 | const 方法中修改锁 |
| try_to_lock             | 非阻塞锁           |

### C++ 特性

| 特性        | 用途       |
| ----------- | ---------- |
| 纯虚函数    | 定义接口   |
| 虚析构函数  | 多态释放   |
| unique_ptr  | 独占所有权 |
| shared_ptr  | 共享所有权 |
| std::chrono | 时间管理   |

---

## 📚 面试高频题

### 1. 工厂模式 vs 直接 new

```
Q: 为什么用工厂模式而不是直接 new？
A: 解耦、扩展、集中管理。调用者不依赖具体类。
```

### 2. 生产者-消费者

```
Q: 为什么用条件变量而不是轮询？
A: 轮询浪费 CPU；条件变量让线程休眠，有数据时才唤醒。
```

### 3. 原子操作

```
Q: std::atomic<bool> 和 volatile bool 的区别？
A: volatile 只防止编译器优化，不保证原子性和内存顺序；
   atomic 保证原子性和可见性。
```

### 4. Pimpl

```
Q: Pimpl 的典型应用场景？
A: 库开发（隐藏实现）、ABI 稳定（类大小固定）、编译加速。
```

---

**创建日期**: 2025-11-27  
**适用版本**: ESDK Sophon v3.0+  
**依赖模块**: Core, MQTT, Vision, Video
