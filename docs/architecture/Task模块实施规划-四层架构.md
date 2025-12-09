# Task 模块实施规划 - 四层架构方案

**创建日期**: 2025-11-02  
**架构方案**: 四层架构 (接入层 → 管理层 → 服务层 → 工具层)  
**状态**: 规划中 🚧  
**优先级**: ⭐⭐⭐⭐⭐

---

## 📋 目录

1. [架构总览](#架构总览)
2. [核心设计理念](#核心设计理念)
3. [详细模块设计](#详细模块设计)
4. [实现步骤](#实现步骤)
5. [代码示例](#代码示例)
6. [测试计划](#测试计划)
7. [风险评估](#风险评估)

---

## 架构总览

### 🏗️ 四层架构图

```
┌──────────────────────────────────────────────────────┐
│                 ESDK_On_Sophon                       │
│                                                      │
│  ┌────────────────────────────────────────────────┐ │
│  │         1. 接入层 (Access Layer)               │ │
│  │  ┌──────────────────────────────────────────┐ │ │
│  │  │  MQTT Handler                            │ │ │
│  │  │  - 接收平台命令 (services topic)          │ │ │
│  │  │  - 解析 JSON 消息                        │ │ │
│  │  │  - 路由到 Task Manager                   │ │ │
│  │  │  - 发送响应 (services_reply)             │ │ │
│  │  └──────────────────────────────────────────┘ │ │
│  └─────────────────────┬────────────────────────── │
│                        ↓                            │
│  ┌────────────────────────────────────────────────┐ │
│  │         2. 管理层 (Manager Layer)              │ │
│  │  ┌──────────────────────────────────────────┐ │ │
│  │  │  Task Manager (单例)                     │ │ │
│  │  │  - 任务注册表 (taskId → Task)            │ │ │
│  │  │  - 创建任务 (工厂方法)                   │ │ │
│  │  │  - 生命周期管理 (start/stop/pause)       │ │ │
│  │  │  - 资源分配和释放                        │ │ │
│  │  │  - 状态监控和回调                        │ │ │
│  │  └──────────────────────────────────────────┘ │ │
│  │  ┌──────────────────────────────────────────┐ │ │
│  │  │  Task (具体任务类)                       │ │ │
│  │  │  - LiveStreamTask  (直播流检测)          │ │ │
│  │  │  - MediaFileTask   (文件检测)            │ │ │
│  │  │  - 控制执行流程                          │ │ │
│  │  │  - 管理工作线程                          │ │ │
│  │  │  - 维护任务状态                          │ │ │
│  │  └──────────────────────────────────────────┘ │ │
│  └─────────────────────┬────────────────────────── │
│                        ↓                            │
│  ┌────────────────────────────────────────────────┐ │
│  │         3. 服务层 (Service Layer) ⭐           │ │
│  │  ┌──────────────────────────────────────────┐ │ │
│  │  │  Task Service (核心业务逻辑)             │ │ │
│  │  │  - processFrame() 处理单帧               │ │ │
│  │  │  - detectObjects() 目标检测              │ │ │
│  │  │  - buildEvent() 构造事件                 │ │ │
│  │  │  - publishEvent() 发布结果               │ │ │
│  │  │  - 统一的业务流程编排                     │ │ │
│  │  └──────────────────────────────────────────┘ │ │
│  └─────────────────────┬────────────────────────── │
│                        ↓                            │
│  ┌────────────────────────────────────────────────┐ │
│  │         4. 工具层 (Utility Layer)              │ │
│  │  ┌─────────────┬──────────────┬─────────────┐ │ │
│  │  │ Vision      │ MqttClient   │ Utils       │ │ │
│  │  │ - Detector  │ - publish()  │ - Base64    │ │ │
│  │  │ - detect()  │ - subscribe()│ - UUID      │ │ │
│  │  │             │              │ - GPS       │ │ │
│  │  └─────────────┴──────────────┴─────────────┘ │ │
│  └────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

### 🎯 数据流向

```
平台命令 (MQTT)
    ↓
[1. MQTT Handler]
    ↓ 解析JSON
    ↓ 构造 TaskConfig
    ↓
[2. Task Manager]
    ↓ createTask(config)
    ↓
[2. Task (LiveStreamTask)]
    ↓ start() → 启动工作线程
    ↓ 循环: getFrame()
    ↓
[3. Task Service]
    ↓ processFrame(frame, config)
    ↓   → detectObjects() (调用 Vision)
    ↓   → buildEvent() (构造事件)
    ↓   → publishEvent() (调用 MqttClient)
    ↓
[4. 工具层]
    ↓ Vision.detect() → 检测结果
    ↓ MqttClient.publish() → 发送到平台
    ↓
平台接收 (MQTT)
```

---

## 核心设计理念

### 📐 设计原则

#### 1. 单一职责原则 (SRP)

```
MQTT Handler:    只负责消息收发和解析
Task Manager:    只负责任务生命周期管理
Task Service:    只负责业务逻辑编排
Task (具体类):   只负责执行流程控制
工具层:          只负责具体功能实现
```

#### 2. 依赖倒置原则 (DIP)

```cpp
// 高层模块不依赖低层模块，都依赖抽象
Task → ITaskService (接口)
TaskService → IDetector (接口)
TaskService → IMqttClient (接口)
```

#### 3. 开闭原则 (OCP)

```cpp
// 对扩展开放，对修改关闭
新增任务类型:     继承 ITask，复用 TaskService
新增检测算法:     实现 IDetector，不影响 TaskService
新增发布渠道:     修改 TaskService，不影响 Task
```

### 💡 关键设计决策

| 问题                     | 决策                    | 理由                             |
| ------------------------ | ----------------------- | -------------------------------- |
| Task Executor 去哪了?    | 合并到 Task 类中        | Task 自己管理执行线程，更简洁    |
| Result Processor 去哪了? | 合并到 Task Service     | 检测和结果处理是一个完整业务流程 |
| MQTT Publisher 去哪了?   | 作为工具被 Service 调用 | 只是消息发送工具，不需要独立模块 |
| 为什么需要 Task Service? | 统一业务逻辑            | 避免每个 Task 类重复实现检测流程 |

---

## 详细模块设计

### 📦 模块 1: TaskTypes.h (数据结构定义)

**文件路径**: `include/esdk_sophon/task/TaskTypes.h`

**功能**: 定义所有数据结构和枚举

```cpp
/**
 * @brief 任务类型枚举
 */
enum class TaskType {
    DETECTION_LIVESTREAM = 0,  // 直播流检测
    DETECTION_MEDIAFILE = 1,   // 文件检测
    SEGMENTATION = 2,          // 分割 (预留)
    TRACKING = 3               // 追踪 (预留)
};

/**
 * @brief 任务状态
 */
enum class TaskState {
    IDLE = 0,      // 空闲
    PENDING,       // 待启动
    RUNNING,       // 运行中
    PAUSED,        // 暂停
    COMPLETED,     // 完成
    FAILED,        // 失败
    CANCELLED      // 取消
};

/**
 * @brief 任务配置
 */
struct TaskConfig {
    std::string taskId;              // 任务ID
    TaskType type;                   // 任务类型
    int algorithmId;                 // 算法ID

    // 检测参数
    std::vector<EventType> eventTypes;
    float confidenceThreshold;
    float nmsThreshold;

    // 数据源
    DataSource source;               // LIVESTREAM / MEDIAFILE
    std::string mediaPath;           // 文件路径(可选)

    // 执行参数
    int reportIntervalSec;           // 上报间隔(秒)
    bool enableVisualization;
    bool enableRTMP;
    std::string rtmpUrl;

    bool isValid() const;            // 验证配置
};

/**
 * @brief 检测事件
 */
struct DetectionEvent {
    std::string uuid;
    std::string taskId;
    int eventType;
    std::string eventDescribe;

    std::string pictureBase64;
    double latitude;
    double longitude;
    std::string createTime;

    std::vector<BoundingBox> points;
};

/**
 * @brief 任务统计
 */
struct TaskStatistics {
    uint64_t framesProcessed;
    uint64_t detectionsCount;
    uint64_t eventsPublished;
    double avgProcessingTimeMs;
    // ...
};
```

---

### 📦 模块 2: ITask.h (任务接口)

**文件路径**: `include/esdk_sophon/task/ITask.h`

**功能**: 定义任务抽象接口

```cpp
/**
 * @brief 任务抽象接口
 *
 * 所有任务类型的基类
 */
class ITask {
public:
    virtual ~ITask() = default;

    // ===== 生命周期管理 =====
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;

    // ===== 状态查询 =====
    virtual TaskState getState() const = 0;
    virtual std::string getTaskId() const = 0;
    virtual TaskType getTaskType() const = 0;
    virtual const TaskConfig& getConfig() const = 0;
    virtual float getProgress() const = 0;
    virtual TaskStatistics getStatistics() const = 0;
    virtual std::string getLastError() const = 0;

    // ===== 回调设置 =====
    virtual void setStateCallback(TaskCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    virtual void setEventCallback(EventCallback callback) = 0;
};

using TaskPtr = std::shared_ptr<ITask>;
```

---

### 📦 模块 3: TaskService.h (核心服务层) ⭐

**文件路径**: `include/esdk_sophon/task/TaskService.h`

**功能**: 统一的业务逻辑处理

```cpp
/**
 * @brief 任务服务层
 *
 * 封装检测、事件构造、结果发布的完整业务流程。
 * 被所有 Task 类共享使用。
 *
 * 职责:
 * 1. 调用 Vision 模块执行检测
 * 2. 将检测结果转换为 DetectionEvent
 * 3. 编码图片为 Base64
 * 4. 获取 GPS 信息
 * 5. 通过 MQTT 发布事件
 */
class TaskService {
public:
    TaskService();
    ~TaskService();

    /**
     * @brief 初始化服务
     * @param config 任务配置
     * @return true 成功
     */
    bool initialize(const TaskConfig& config);

    /**
     * @brief 处理单帧图像 (完整流程)
     *
     * 执行流程:
     * 1. 调用检测器
     * 2. 过滤结果
     * 3. 构造事件
     * 4. 发布事件
     *
     * @param frame 输入图像
     * @param needPublish 是否需要发布 (定时控制)
     * @return DetectionEvent 事件数据
     */
    DetectionEvent processFrame(
        const cv::Mat& frame,
        bool needPublish = true
    );

    /**
     * @brief 仅执行检测 (不发布)
     * @param frame 输入图像
     * @return DetectionResult 原始检测结果
     */
    vision::DetectionResult detectObjects(const cv::Mat& frame);

    /**
     * @brief 构造检测事件
     * @param frame 原始图像
     * @param result 检测结果
     * @return DetectionEvent 事件数据
     */
    DetectionEvent buildEvent(
        const cv::Mat& frame,
        const vision::DetectionResult& result
    );

    /**
     * @brief 发布事件到平台
     * @param event 事件数据
     * @return true 发布成功
     */
    bool publishEvent(const DetectionEvent& event);

    /**
     * @brief 设置检测器
     * @param detector 检测器实例
     */
    void setDetector(std::shared_ptr<vision::IDetector> detector);

    /**
     * @brief 设置 MQTT 客户端
     * @param client MQTT 客户端
     */
    void setMqttClient(mqtt::MqttClient* client);

    /**
     * @brief 释放资源
     */
    void cleanup();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};
```

**TaskService::Impl 实现细节**:

```cpp
class TaskService::Impl {
public:
    // 配置
    TaskConfig config_;

    // 依赖组件
    std::shared_ptr<vision::IDetector> detector_;
    mqtt::MqttClient* mqttClient_;

    // 工具类
    utils::ImageEncoder imageEncoder_;
    utils::UuidGenerator uuidGenerator_;

    // 缓存
    cv::Mat lastVisFrame_;  // 最后一帧可视化图像

    // 统计
    uint64_t frameCount_ = 0;
    uint64_t eventCount_ = 0;

    // 核心方法实现
    DetectionEvent processFrameImpl(const cv::Mat& frame, bool needPublish);
    vision::DetectionResult detectImpl(const cv::Mat& frame);
    DetectionEvent buildEventImpl(const cv::Mat& frame,
                                   const vision::DetectionResult& result);
    bool publishEventImpl(const DetectionEvent& event);

    // 辅助方法
    cv::Mat visualizeDetections(const cv::Mat& frame,
                                 const vision::DetectionResult& result);
    std::string encodeImageToBase64(const cv::Mat& image);
    std::string getCurrentTimeISO8601();
    void getGpsInfo(double& lat, double& lon);
};
```

---

### 📦 模块 4: TaskManager.h (任务管理器)

**文件路径**: `include/esdk_sophon/task/TaskManager.h`

**功能**: 任务生命周期管理 (单例)

```cpp
/**
 * @brief 任务管理器 (单例)
 *
 * 职责:
 * 1. 创建任务 (工厂方法)
 * 2. 维护任务注册表
 * 3. 管理任务生命周期
 * 4. 资源分配和释放
 */
class TaskManager {
public:
    static TaskManager& getInstance();

    /**
     * @brief 创建任务
     * @param config 任务配置
     * @return TaskPtr 任务智能指针 (nullptr表示失败)
     */
    TaskPtr createTask(const TaskConfig& config);

    /**
     * @brief 获取任务
     * @param taskId 任务ID
     * @return TaskPtr 任务指针 (不存在返回nullptr)
     */
    TaskPtr getTask(const std::string& taskId);

    /**
     * @brief 移除任务
     * @param taskId 任务ID
     * @return true 成功
     */
    bool removeTask(const std::string& taskId);

    /**
     * @brief 获取所有任务
     */
    std::vector<TaskPtr> getAllTasks() const;

    /**
     * @brief 停止所有任务
     */
    void stopAllTasks();

    /**
     * @brief 获取任务统计
     */
    std::map<std::string, TaskStatistics> getTaskStatistics() const;

private:
    TaskManager();
    ~TaskManager();

    class Impl;
    std::unique_ptr<Impl> pImpl_;
};
```

**TaskManager::Impl**:

```cpp
class TaskManager::Impl {
public:
    // 任务注册表
    std::unordered_map<std::string, TaskPtr> tasks_;
    mutable std::mutex tasksMutex_;

    // 共享服务层 (所有任务共享)
    std::unique_ptr<TaskService> taskService_;

    // 依赖组件
    mqtt::MqttClient* mqttClient_;

    // 工厂方法
    TaskPtr createTaskImpl(const TaskConfig& config);
    TaskPtr createLiveStreamTask(const TaskConfig& config);
    TaskPtr createMediaFileTask(const TaskConfig& config);
};
```

---

### 📦 模块 5: LiveStreamTask (具体任务类)

**文件路径**: `src/task/tasks/LiveStreamTask.h`

**功能**: 直播流检测任务

```cpp
/**
 * @brief 直播流检测任务
 *
 * 从无人机获取实时视频流，进行目标检测，定时上报结果。
 *
 * 执行流程:
 * 1. start() - 连接视频流，启动工作线程
 * 2. 工作线程循环:
 *    - getFrame() 获取一帧
 *    - taskService_->processFrame() 处理
 *    - 定时发布 (每N秒)
 * 3. stop() - 停止线程，释放资源
 */
class LiveStreamTask : public ITask {
public:
    explicit LiveStreamTask(const TaskConfig& config, TaskService* service);
    ~LiveStreamTask() override;

    // ITask 接口实现
    bool start() override;
    void stop() override;
    bool pause() override;
    bool resume() override;

    TaskState getState() const override;
    std::string getTaskId() const override;
    TaskType getTaskType() const override;
    const TaskConfig& getConfig() const override;
    float getProgress() const override;
    TaskStatistics getStatistics() const override;
    std::string getLastError() const override;

    void setStateCallback(TaskCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;
    void setEventCallback(EventCallback callback) override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};
```

**LiveStreamTask::Impl**:

```cpp
class LiveStreamTask::Impl {
public:
    // 配置和状态
    TaskConfig config_;
    std::atomic<TaskState> state_{TaskState::IDLE};
    std::string lastError_;

    // 服务层引用 (不拥有所有权)
    TaskService* taskService_;

    // 执行控制
    std::atomic<bool> running_{false};
    std::thread workThread_;

    // 视频流 (从 device 模块获取)
    device::VideoStream* videoStream_;

    // RTMP 推流 (可选)
    std::unique_ptr<RTMPStreamer> rtmpStreamer_;

    // 定时器 (控制上报频率)
    std::chrono::steady_clock::time_point lastPublishTime_;

    // 统计
    TaskStatistics statistics_;

    // 回调
    TaskCallback stateCallback_;
    ErrorCallback errorCallback_;
    EventCallback eventCallback_;

    // 核心方法
    void workLoop();                    // 工作线程主循环
    cv::Mat getFrame();                 // 获取一帧
    bool shouldPublish();               // 是否到达上报时间
    void changeState(TaskState newState);
    void onError(const std::string& error);
};
```

---

### 📦 模块 6: MediaFileTask (文件检测任务)

**文件路径**: `src/task/tasks/MediaFileTask.h`

**功能**: 媒体文件检测任务

```cpp
/**
 * @brief 媒体文件检测任务
 *
 * 处理图片或视频文件，执行目标检测。
 *
 * 流程:
 * 1. 读取文件列表
 * 2. 逐帧/逐图处理
 * 3. 有检测结果时上报
 * 4. 处理完成后标记完成
 */
class MediaFileTask : public ITask {
public:
    explicit MediaFileTask(const TaskConfig& config, TaskService* service);
    ~MediaFileTask() override;

    // ITask 接口实现 (同 LiveStreamTask)
    bool start() override;
    void stop() override;
    // ... 其他接口

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};
```

---

## 实现步骤

### 🗓️ 第一阶段: 基础框架 (2 天)

#### Day 1: 数据结构和接口

**上午 (4h)**:

1. ✅ 创建 `TaskTypes.h` - 所有数据结构定义
2. ✅ 创建 `ITask.h` - 任务抽象接口
3. ✅ 编写单元测试: `test_task_types.cpp`

**下午 (4h)**: 4. ✅ 创建 `TaskService.h` - 服务层接口定义 5. ✅ 创建 `TaskManager.h` - 管理器接口定义 6. ✅ 完善文档注释

**产出**:

- `include/esdk_sophon/task/TaskTypes.h`
- `include/esdk_sophon/task/ITask.h`
- `include/esdk_sophon/task/TaskService.h`
- `include/esdk_sophon/task/TaskManager.h`
- `tests/test_task_types.cpp`

#### Day 2: 核心实现

**上午 (4h)**:

1. ✅ 实现 `TaskService.cpp` - 服务层核心逻辑
   - detectObjects()
   - buildEvent()
   - publishEvent()

**下午 (4h)**: 2. ✅ 实现 `TaskManager.cpp` - 管理器

- createTask()
- getTask()
- removeTask()

3. ✅ 单元测试: `test_task_service.cpp`, `test_task_manager.cpp`

**产出**:

- `src/task/TaskService.cpp`
- `src/task/TaskManager.cpp`
- `tests/test_task_service.cpp`
- `tests/test_task_manager.cpp`

---

### 🗓️ 第二阶段: 具体任务实现 (2 天)

#### Day 3: LiveStreamTask

**上午 (4h)**:

1. ✅ 创建 `LiveStreamTask.h/cpp`
2. ✅ 实现生命周期方法 (start/stop)
3. ✅ 实现工作线程循环

**下午 (4h)**: 4. ✅ 集成 Vision 检测器 5. ✅ 集成 RTMP 推流 (可选) 6. ✅ 单元测试: `test_livestream_task.cpp`

**产出**:

- `src/task/tasks/LiveStreamTask.h`
- `src/task/tasks/LiveStreamTask.cpp`
- `tests/test_livestream_task.cpp`

#### Day 4: MediaFileTask + 工具类

**上午 (4h)**:

1. ✅ 创建 `MediaFileTask.h/cpp`
2. ✅ 实现文件处理逻辑

**下午 (4h)**: 3. ✅ 实现工具类:

- `utils/ImageEncoder.h/cpp` (Base64)
- `utils/UuidGenerator.h/cpp` (UUID)

4. ✅ 单元测试: `test_mediafile_task.cpp`

**产出**:

- `src/task/tasks/MediaFileTask.h/cpp`
- `src/utils/ImageEncoder.h/cpp`
- `src/utils/UuidGenerator.h/cpp`
- `tests/test_mediafile_task.cpp`

---

### 🗓️ 第三阶段: MQTT 集成 (1 天)

#### Day 5: MQTT 命令处理

**上午 (4h)**:

1. ✅ 扩展 `MqttHandler` 处理任务命令
   - device_algorithm_enable
   - device_task_end
2. ✅ 实现 JSON → TaskConfig 转换

**下午 (4h)**: 3. ✅ 实现命令响应逻辑

- services_reply
- device_task_analysis_result

4. ✅ 集成测试: `test_mqtt_task_integration.cpp`

**产出**:

- 修改 `src/Mqtt/MqttHandler.cpp`
- 新增 `src/Mqtt/TaskCommandHandler.h/cpp`
- `tests/test_mqtt_task_integration.cpp`

---

### 🗓️ 第四阶段: 端到端测试 (1 天)

#### Day 6: 完整流程验证

**上午 (4h)**:

1. ✅ 模拟平台发送命令
2. ✅ 验证任务创建和执行
3. ✅ 验证结果上报

**下午 (4h)**: 4. ✅ 性能测试 5. ✅ 稳定性测试 6. ✅ 编写测试报告

**产出**:

- `tests/test_e2e_task_flow.cpp`
- `docs/测试报告-Task模块.md`

---

## 代码示例

### 示例 1: TaskService 使用方式

```cpp
// 在 LiveStreamTask 中使用 TaskService
void LiveStreamTask::Impl::workLoop() {
    core::Logger& logger = core::Logger::getInstance();

    while (running_) {
        try {
            // 1. 获取一帧
            cv::Mat frame = getFrame();
            if (frame.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // 2. 判断是否需要发布
            bool needPublish = shouldPublish();

            // 3. 调用服务层处理 (一行搞定!)
            DetectionEvent event = taskService_->processFrame(frame, needPublish);

            // 4. 触发回调 (可选)
            if (eventCallback_ && event.points.size() > 0) {
                eventCallback_(event);
            }

            // 5. 更新统计
            statistics_.framesProcessed++;
            if (needPublish) {
                statistics_.eventsPublished++;
                lastPublishTime_ = std::chrono::steady_clock::now();
            }

        } catch (const std::exception& e) {
            onError("处理帧失败: " + std::string(e.what()));
            break;
        }
    }

    logger.info("任务 " + config_.taskId + " 工作线程退出");
}

bool LiveStreamTask::Impl::shouldPublish() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastPublishTime_
    ).count();

    return elapsed >= config_.reportIntervalSec;
}
```

### 示例 2: TaskManager 创建任务

```cpp
// 在 MqttHandler 中调用 TaskManager
void MqttHandler::handleAlgorithmEnable(const Json::Value& data) {
    // 1. 解析 JSON 构造配置
    TaskConfig config;
    config.taskId = data["taskID"].asString();
    config.algorithmId = data["algorithmId"].asInt();
    config.source = intToDataSource(data["source"].asInt());

    // 解析事件类型
    const Json::Value& types = data["type"];
    for (const auto& type : types) {
        EventType et;
        et.id = type["id"].asInt();
        et.mainType = type["main_type"].asInt();
        et.eventDescribe = type["eventDescribe"].asString();
        config.eventTypes.push_back(et);
    }

    // 2. 创建任务
    auto& taskMgr = TaskManager::getInstance();
    TaskPtr task = taskMgr.createTask(config);

    if (!task) {
        logger_.error("创建任务失败: " + config.taskId);
        sendReply("device_algorithm_enable", config.taskId, 1); // 失败
        return;
    }

    // 3. 启动任务
    if (!task->start()) {
        logger_.error("启动任务失败: " + config.taskId);
        taskMgr.removeTask(config.taskId);
        sendReply("device_algorithm_enable", config.taskId, 1);
        return;
    }

    // 4. 响应平台
    logger_.info("任务启动成功: " + config.taskId);
    sendReply("device_algorithm_enable", config.taskId, 0); // 成功
}
```

### 示例 3: TaskService 完整实现片段

```cpp
DetectionEvent TaskService::Impl::processFrameImpl(
    const cv::Mat& frame,
    bool needPublish
) {
    DetectionEvent event;

    // 1. 执行检测
    auto result = detectImpl(frame);

    // 2. 过滤结果 (根据置信度和事件类型)
    vision::DetectionResult filtered = filterResults(result);

    // 3. 可视化 (如果启用)
    cv::Mat visFrame = frame;
    if (config_.enableVisualization) {
        visFrame = visualizeDetections(frame, filtered);
        lastVisFrame_ = visFrame.clone();
    }

    // 4. 只有检测到目标才构造事件
    if (filtered.boxes.size() > 0) {
        event = buildEventImpl(visFrame, filtered);

        // 5. 发布事件 (如果需要)
        if (needPublish) {
            publishEventImpl(event);
            eventCount_++;
        }
    }

    frameCount_++;
    return event;
}

vision::DetectionResult TaskService::Impl::detectImpl(const cv::Mat& frame) {
    if (!detector_) {
        throw std::runtime_error("检测器未初始化");
    }

    return detector_->detect(frame);
}

DetectionEvent TaskService::Impl::buildEventImpl(
    const cv::Mat& frame,
    const vision::DetectionResult& result
) {
    DetectionEvent event;

    // 基本信息
    event.uuid = uuidGenerator_.generate();
    event.taskId = config_.taskId;
    event.eventType = config_.eventTypes[0].id;  // 简化版
    event.mainType = config_.eventTypes[0].mainType;
    event.eventDescribe = config_.eventTypes[0].eventDescribe;

    // 图片编码
    event.pictureBase64 = encodeImageToBase64(frame);
    event.pictureCode = ".jpg";

    // GPS 信息
    getGpsInfo(event.latitude, event.longitude);

    // 时间
    event.createTime = getCurrentTimeISO8601();

    // 检测框
    for (size_t i = 0; i < result.boxes.size(); ++i) {
        BoundingBox box;
        box.x = result.boxes[i].x;
        box.y = result.boxes[i].y;
        box.w = result.boxes[i].width;
        box.h = result.boxes[i].height;
        box.classId = result.boxes[i].classId;
        box.className = result.boxes[i].className;
        box.confidence = result.boxes[i].confidence;

        // TODO: 根据图像坐标计算经纬度 (调用地理解码API)
        box.lon = 0.0;
        box.lat = 0.0;

        event.points.push_back(box);
    }

    return event;
}

bool TaskService::Impl::publishEventImpl(const DetectionEvent& event) {
    if (!mqttClient_) {
        throw std::runtime_error("MQTT客户端未初始化");
    }

    // 构造 JSON
    Json::Value json;
    json["UUID"] = event.uuid;
    json["taskID"] = event.taskId;
    json["eventType"] = event.eventType;
    json["main_type"] = event.mainType;
    json["eventDescribe"] = event.eventDescribe;
    json["picture"] = event.pictureBase64;
    json["pictureCode"] = event.pictureCode;
    json["latitude"] = event.latitude;
    json["longitude"] = event.longitude;
    json["createTime"] = event.createTime;

    Json::Value points(Json::arrayValue);
    for (const auto& box : event.points) {
        Json::Value point;
        point["x"] = box.x;
        point["y"] = box.y;
        point["w"] = box.w;
        point["h"] = box.h;
        point["lon"] = box.lon;
        point["lat"] = box.lat;
        points.append(point);
    }
    json["points"] = points;

    // 发布到 MQTT
    std::string topic = "drone/" + config_.deviceSn + "/info/event";
    return mqttClient_->publish(topic, json.toStyledString());
}
```

---

## 测试计划

### 🧪 单元测试

#### test_task_service.cpp

```cpp
#include <gtest/gtest.h>
#include "esdk_sophon/task/TaskService.h"

using namespace esdk_sophon::task;

class TaskServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 Mock 检测器和 MQTT 客户端
        mockDetector_ = std::make_shared<MockDetector>();
        mockMqttClient_ = new MockMqttClient();

        // 配置
        config_.taskId = "test_001";
        config_.source = DataSource::LIVESTREAM;
        config_.reportIntervalSec = 10;

        // 初始化服务
        service_ = std::make_unique<TaskService>();
        service_->setDetector(mockDetector_);
        service_->setMqttClient(mockMqttClient_);
        service_->initialize(config_);
    }

    TaskConfig config_;
    std::unique_ptr<TaskService> service_;
    std::shared_ptr<MockDetector> mockDetector_;
    MockMqttClient* mockMqttClient_;
};

TEST_F(TaskServiceTest, ProcessFrame_WithDetections) {
    // 准备测试数据
    cv::Mat frame = cv::Mat::zeros(640, 640, CV_8UC3);

    // Mock 检测器返回结果
    vision::DetectionResult mockResult;
    mockResult.boxes.push_back({100, 200, 50, 80, 0, "person", 0.95f});
    EXPECT_CALL(*mockDetector_, detect(_))
        .WillOnce(Return(mockResult));

    // 执行
    auto event = service_->processFrame(frame, true);

    // 验证
    EXPECT_EQ(event.taskId, "test_001");
    EXPECT_EQ(event.points.size(), 1);
    EXPECT_EQ(event.points[0].classId, 0);
    EXPECT_FLOAT_EQ(event.points[0].confidence, 0.95f);
    EXPECT_FALSE(event.pictureBase64.empty());
}

TEST_F(TaskServiceTest, ProcessFrame_NoDetections) {
    cv::Mat frame = cv::Mat::zeros(640, 640, CV_8UC3);

    vision::DetectionResult emptyResult;
    EXPECT_CALL(*mockDetector_, detect(_))
        .WillOnce(Return(emptyResult));

    auto event = service_->processFrame(frame, true);

    EXPECT_TRUE(event.points.empty());
}

TEST_F(TaskServiceTest, PublishEvent_Success) {
    DetectionEvent event;
    event.taskId = "test_001";
    event.uuid = "test-uuid";

    EXPECT_CALL(*mockMqttClient_, publish(_, _))
        .WillOnce(Return(true));

    bool result = service_->publishEvent(event);

    EXPECT_TRUE(result);
}
```

#### test_livestream_task.cpp

```cpp
TEST(LiveStreamTask, StartStop) {
    TaskConfig config;
    config.taskId = "test_livestream";
    config.type = TaskType::DETECTION_LIVESTREAM;
    config.source = DataSource::LIVESTREAM;

    MockTaskService mockService;
    LiveStreamTask task(config, &mockService);

    // 启动
    EXPECT_TRUE(task.start());
    EXPECT_EQ(task.getState(), TaskState::RUNNING);

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 停止
    task.stop();
    EXPECT_EQ(task.getState(), TaskState::COMPLETED);

    // 验证统计
    auto stats = task.getStatistics();
    EXPECT_GT(stats.framesProcessed, 0);
}

TEST(LiveStreamTask, PauseResume) {
    TaskConfig config;
    // ... 配置

    LiveStreamTask task(config, &mockService);
    task.start();

    // 暂停
    EXPECT_TRUE(task.pause());
    EXPECT_EQ(task.getState(), TaskState::PAUSED);

    auto stats1 = task.getStatistics();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto stats2 = task.getStatistics();

    // 暂停期间不处理帧
    EXPECT_EQ(stats1.framesProcessed, stats2.framesProcessed);

    // 恢复
    EXPECT_TRUE(task.resume());
    EXPECT_EQ(task.getState(), TaskState::RUNNING);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto stats3 = task.getStatistics();

    // 恢复后继续处理
    EXPECT_GT(stats3.framesProcessed, stats2.framesProcessed);
}
```

### 🔗 集成测试

#### test_e2e_task_flow.cpp

```cpp
TEST(E2E, CompleteDetectionFlow) {
    // 1. 模拟 MQTT 命令
    std::string json = R"({
        "method": "device_algorithm_enable",
        "data": {
            "taskID": "e2e_test_001",
            "algorithmId": 16,
            "source": 0,
            "type": [{
                "id": 1,
                "main_type": 1,
                "eventDescribe": "检测到目标"
            }]
        }
    })";

    // 2. 解析并创建任务
    Json::Value parsed;
    Json::Reader reader;
    reader.parse(json, parsed);

    TaskConfig config = parseTaskConfig(parsed["data"]);

    auto& taskMgr = TaskManager::getInstance();
    auto task = taskMgr.createTask(config);

    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->getTaskId(), "e2e_test_001");

    // 3. 启动任务
    EXPECT_TRUE(task->start());
    EXPECT_EQ(task->getState(), TaskState::RUNNING);

    // 4. 运行 15 秒 (等待事件发布)
    std::this_thread::sleep_for(std::chrono::seconds(15));

    // 5. 验证统计
    auto stats = task->getStatistics();
    EXPECT_GT(stats.framesProcessed, 0);
    EXPECT_GT(stats.eventsPublished, 0);  // 至少发布一次

    // 6. 停止任务
    task->stop();
    EXPECT_EQ(task->getState(), TaskState::COMPLETED);

    // 7. 清理
    taskMgr.removeTask("e2e_test_001");
}
```

---

## 风险评估

### ⚠️ 潜在风险

| 风险                  | 影响 | 概率 | 缓解措施                             |
| --------------------- | ---- | ---- | ------------------------------------ |
| Vision 模块接口不稳定 | 高   | 中   | 先完成 Vision 模块测试，确保接口稳定 |
| MQTT 消息格式变化     | 中   | 低   | 与平台确认协议，编写详细文档         |
| 线程安全问题          | 高   | 中   | 充分的并发测试，使用 ThreadSanitizer |
| 内存泄漏              | 中   | 低   | 使用智能指针，Valgrind 检测          |
| 性能不达标            | 中   | 中   | 及早进行性能测试，优化热点代码       |

### ✅ 质量保证

1. **代码审查**: 每个模块完成后 Code Review
2. **单元测试**: 覆盖率 > 80%
3. **集成测试**: 覆盖主要流程
4. **性能测试**: 帧率 ≥ 25 FPS, 延迟 < 500ms
5. **内存检测**: Valgrind, AddressSanitizer
6. **并发测试**: ThreadSanitizer, 压力测试

---

## 总结

### ✨ 架构优势

1. **职责清晰**

   - Task Manager: 管任务
   - Task Service: 管业务
   - Task 类: 管执行

2. **易于维护**

   - 模块独立，低耦合
   - 接口明确，易测试
   - 文档完善，易理解

3. **便于扩展**

   - 新增任务类型简单
   - 新增检测算法无影响
   - 新增发布渠道灵活

4. **符合原则**
   - 单一职责 (SRP)
   - 开闭原则 (OCP)
   - 依赖倒置 (DIP)

### 📅 时间估算

- 第一阶段 (基础框架): 2 天
- 第二阶段 (具体任务): 2 天
- 第三阶段 (MQTT 集成): 1 天
- 第四阶段 (测试验证): 1 天

**总计**: 6 个工作日

---

**创建者**: AI Assistant  
**审核者**: TBD  
**最后更新**: 2025-11-02  
**状态**: ✅ 规划完成，待实施
