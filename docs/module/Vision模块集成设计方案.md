/\*\*

- @file Vision 模块集成设计方案.md
- @brief Vision 模块集成到 Main 程序的完整设计方案
- @date 2025-10-28
  \*/

# Vision 模块集成设计方案

> 📅 **创建日期**: 2025-10-28  
> 🎯 **目标**: 将 CompletionDetector、MediaFileFilter、PerformanceMonitor 集成到主程序  
> 📝 **状态**: 设计阶段

---

## 📋 目录

- [1. 系统架构设计](#1-系统架构设计)
- [2. 任务流程设计](#2-任务流程设计)

## 1. 系统架构设计

### 1.1 整体架构图

```

云端服务器 (jiaoyujidi.work)
↕ MQTT 协议
┌──────────────────────────────────────────────┐
│ SE7 设备 (Main 程序) │
│ │
│ ┌────────────────────────────────────────┐ │
│ │ main.cpp (启动器层 - 50 行) │ │
│ │ - 初始化日志 │ │
│ │ - 加载配置 │ │
│ │ - 创建 Application │ │
│ │ - 启动应用 │ │
│ └──────────┬─────────────────────────────┘ │
│ ↓ │
│ ┌────────────────────────────────────────┐ │
│ │ Application (应用层 - 150 行) │ │
│ │ - 协调各子系统 │ │
│ │ - 管理生命周期 │ │
│ │ - 主循环 │ │
│ └──────────┬─────────────────────────────┘ │
│ ↓ │
│ ┌────────────────────────────────────────┐ │
│ │ 业务层 │ │
│ │ ┌──────────────────────────────────┐ │ │
│ │ │ MqttHandler (200 行) │ │ │
│ │ │ - MQTT 连接管理 │ │ │
│ │ │ - 消息解析和分发 │ │ │
│ │ └──────────────────────────────────┘ │ │
│ │ ┌──────────────────────────────────┐ │ │
│ │ │ TaskManager (300 行) │ │ │
│ │ │ - 任务生命周期管理 │ │ │
│ │ │ - Vision 模块协调 │ │ │
│ │ └──────────────────────────────────┘ │ │
│ │ ┌──────────────────────────────────┐ │ │
│ │ │ DeviceManager (200 行) │ │ │
│ │ │ - Edge-SDK 封装 │ │ │
│ │ │ - 数据源管理 │ │ │
│ │ └──────────────────────────────────┘ │ │
│ └────────────────────────────────────────┘ │
│ ↓ │
│ ┌────────────────────────────────────────┐ │
│ │ Vision 核心模块 (Header-Only) │ │
│ │ ┌──────────────────────────────────┐ │ │
│ │ │ CompletionDetector.h │ │ │
│ │ │ (自适应任务完成检测) │ │ │
│ │ └──────────────────────────────────┘ │ │
│ │ ┌──────────────────────────────────┐ │ │
│ │ │ MediaFileFilter.h │ │ │
│ │ │ (时间戳文件过滤) │ │ │
│ │ └──────────────────────────────────┘ │ │
│ │ ┌──────────────────────────────────┐ │ │
│ │ │ PerformanceMonitor.h │ │ │
│ │ │ (性能监控和数据导出) │ │ │
│ │ └──────────────────────────────────┘ │ │
│ └────────────────────────────────────────┘ │
└──────────────────────────────────────────────┘

```

### 1.2 设计原则

#### 单一职责原则 (SRP)

- **main.cpp**: 只负责启动应用
- **Application**: 只负责协调子系统
- **MqttHandler**: 只负责 MQTT 通信
- **TaskManager**: 只负责任务管理
- **DeviceManager**: 只负责设备管理

#### 依赖倒置原则 (DIP)

- 高层模块不依赖低层模块
- 通过接口/指针传递依赖

#### 开闭原则 (OCP)

- 对扩展开放：新增算法类型无需修改核心代码
- 对修改关闭：已有代码稳定不变

---

## 2. 任务流程设计

### 2.1 视频流模式 (source=0)

```

1. 收到 MQTT: device_algorithm_enable (source=0)
   ↓
2. MqttHandler 解析消息
   - 提取 taskID, algorithmType, classes 等
     ↓
3. MqttHandler 调用 TaskManager::startTask()
   ↓
4. TaskManager 创建任务对象
   - 初始化 PerformanceMonitor
   - 创建算法检测器
     ↓
5. TaskManager 调用 DeviceManager::startLiveview()
   ↓
6. DeviceManager 启动视频流，每帧回调 TaskManager
   ↓
7. TaskManager 处理每一帧
   ┌─────────────────────────────────┐
   │ auto handle = perfMonitor. │
   │ startInference("liveview", │
   │ 1920, 1080); │
   │ │
   │ // 算法处理 │
   │ auto result = detector->detect( │
   │ frame); │
   │ handle.recordInferenceEnd(); │
   │ │
   │ // 如果检测到目标 │
   │ if (result.detections.size()>0){│
   │ publishEvent(result); │
   │ } │
   │ │
   │ handle.finish(); │
   └─────────────────────────────────┘
   ↓
8. 收到 MQTT: device_task_end
   ↓
9. TaskManager::onTaskEnd()
   - 停止视频流
   - 导出性能报告
   - 清理资源

```

### 2.2 图片模式 (source=1)

```

1. 收到 MQTT: device_algorithm_enable (source=1)
   ↓
2. MqttHandler 解析消息 → TaskManager::startTask()
   ↓
3. TaskManager 创建任务对象
   - 启用 fileFilter
   - 启动 completionDetector
   - 启用 perfMonitor
     ↓
4. 设置完成回调
   completionDetector.setCompletionCallback([&]{
   - 导出性能报告
   - MQTT 上报任务完成
   - 清理资源
     });
     ↓
5. 收到 MQTT: device_task_end
   - INFO("航线结束，等待媒体文件...")
   - (不做任何操作，继续等待)
     ↓
6. 媒体文件陆续到达 (Edge-SDK 回调)
   ┌──────────────────────────────────────┐
   │ DeviceManager::onMediaFileUpdate() { │
   │ TaskManager::onMediaFileUpdate( │
   │ file); │
   │ } │
   │ │
   │ TaskManager::onMediaFileUpdate() { │
   │ // 1. 文件过滤 │
   │ if (!fileFilter.shouldProcess()) │
   │ return; │
   │ │
   │ // 2. 通知完成检测器 │
   │ completionDetector. │
   │ onFileReceived(); │
   │ │
   │ // 3. 算法处理 │
   │ auto handle = perfMonitor. │
   │ startInference(...); │
   │ auto result = detector->detect( │
   │ image); │
   │ handle.finish(); │
   │ │
   │ // 4. 上报结果 │
   │ publishEvent(result); │
   │ │
   │ // 5. 通知处理完成 │
   │ completionDetector. │
   │ onFileProcessed(); │
   │ } │
   └──────────────────────────────────────┘
   ↓
7. 完成检测器触发 (超时无新文件)
   - 执行完成回调
   - 自动导出性能报告
   - MQTT 上报任务完成

```

---

## 3. 核心类设计

### 3.1 TaskConfig - 任务配置

```cpp
/**
 * @brief 从MQTT消息解析的任务配置
 * @note 位于 types/ 模块，可被多个模块复用
 */
namespace esdk_sophon {
namespace types {

struct TaskConfig {
    int taskID;
    int algorithmRepoID;
    std::string algorithmName;
    std::string version;

    struct AlgorithmType {
        int id;                          // 200004: 垃圾倾倒
        std::string name;                // "垃圾倾倒"
        std::vector<std::string> classes;  // ["car", "person"]
        int mainType;                    // 100000:目标检测
    };
    std::vector<AlgorithmType> types;    // 支持多个算法类型

    int source;                          // 0:视频流 / 1:图片
    int display;
    int airtransfer;

    /**
     * @brief 根据类别名称查找对应的算法类型ID
     * @param className 检测到的类别名称，如"car"
     * @return 对应的eventType，找不到返回-1
     *
     * @details
     * 示例：
     * types = [
     *   {id: 200004, classes: ["car", "person"]},
     *   {id: 200005, classes: ["boat", "car"]}  // car重复
     * ]
     * findEventType("car") → 200004 (返回第一个)
     */
    int findEventType(const std::string& className) const;

    /**
     * @brief 获取所有去重后的类别名称
     * @return 去重后的类别列表
     */
    std::vector<std::string> getAllClasses() const;

    /**
     * @brief 从JSON解析配置
     */
    static TaskConfig fromJson(const nlohmann::json& j);
};

}  // namespace types
}  // namespace esdk_sophon
```

**知识点**:

- std::set 自动去重
- std::find 算法使用
- 静态工厂方法模式

### 3.2 EventMessage - 事件推送

```cpp
/**
 * @brief 事件推送消息
 * @details 对应MQTT Topic: drone/{device_sn}/info/event
 * @note 位于 types/ 模块，可被多个模块使用
 */
namespace esdk_sophon {
namespace types {

struct EventMessage {
    std::string uuid;
    int taskID;
    int eventType;                // 根据TaskConfig::findEventType()确定
    int mainType;
    std::string createTime;
    std::string eventDescribe;
    std::string picture;          // base64编码
    std::string pictureCode;      // "jpg"
    float longitude;
    float latitude;
    std::string fileName;

    // 目标检测结果
    struct DetectionPoint {
        float lon, lat;
        int x, y, w, h;
    };
    std::vector<DetectionPoint> points;

    // 语义分割结果
    struct SegmentationMask {
        std::string label;
        struct Contour {
            struct Point { float lon, lat; };
            std::vector<Point> points;
        };
        std::vector<Contour> contours;
    };
    std::vector<SegmentationMask> mask;

    nlohmann::json toJson() const;
};
```

**知识点**:

- 嵌套结构体设计
- JSON 序列化
- Base64 编码

### 3.3 Application - 应用程序主类

```cpp
/**
 * @brief 应用程序主类 - 协调各个子系统
 */
class Application {
public:
    explicit Application(const Config& config);

    bool initialize();   // 初始化所有子系统
    void run();          // 运行主循环（阻塞式）
    void shutdown();     // 关闭应用

private:
    std::unique_ptr<MqttHandler> mqttHandler_;
    std::unique_ptr<DeviceManager> deviceManager_;
    std::unique_ptr<TaskManager> taskManager_;

    std::atomic<bool> running_;

    bool initializeMqtt();
    bool initializeDevice();
    bool initializeVision();
};
```

**知识点**:

- 依赖注入
- RAII 资源管理
- std::atomic 线程安全

### 3.4 MqttHandler - MQTT 消息处理器

```cpp
/**
 * @brief MQTT消息处理器
 */
class MqttHandler {
public:
    MqttHandler(const std::string& broker, int port);

    bool connect();
    void disconnect();
    bool reconnect();
    bool isConnected() const;

    void setTaskManager(TaskManager* taskManager);
    void publishEvent(const EventMessage& event);

private:
    std::unique_ptr<MqttClient> client_;
    TaskManager* taskManager_;
    std::string deviceSn_;

    void onMessage(const std::string& topic,
                   const std::string& payload);

    void handleAlgorithmEnable(const nlohmann::json& payload);
    void handleTaskEnd(const nlohmann::json& payload);
    void handleAlgorithmSync(const nlohmann::json& payload);
};
```

**知识点**:

- 观察者模式
- 回调函数设计
- 指针 vs 引用的选择

### 3.5 TaskManager - 任务管理器

```cpp
/**
 * @brief 任务管理器 - 管理算法任务的完整生命周期
 * @note 位于 task/ 模块，专注任务管理逻辑
 */
namespace esdk_sophon {
namespace task {

class TaskManager {
public:
    static TaskManager& getInstance();

    void startTask(const TaskConfig& config);
    void onTaskEnd(int taskID);
    void onMediaFileUpdate(const MediaFile& file);
    void onLiveviewFrame(const cv::Mat& frame);
    void stopTask(int taskID);
    void stopAllTasks();

    void setDeviceManager(DeviceManager* deviceMgr);
    void setMqttHandler(MqttHandler* mqttHandler);

private:
    TaskManager() = default;

    struct TaskState {
        int taskID;
        TaskConfig config;

        // Vision模块实例
        std::unique_ptr<CompletionDetector> completionDetector;
        std::unique_ptr<MediaFileFilter> fileFilter;
        std::unique_ptr<PerformanceMonitor> perfMonitor;

        // 算法处理器
        std::shared_ptr<IAlgorithmProcessor> processor;

        // 统计信息
        int filesProcessed;
        int detectionsTotal;
        std::chrono::steady_clock::time_point startTime;
    };

    std::map<int, TaskState> tasks_;
    std::mutex mutex_;

    DeviceManager* deviceManager_;
    MqttHandler* mqttHandler_;
};

}  // namespace task
}  // namespace esdk_sophon
```

**知识点**:

- 单例模式
- std::map 管理多任务
- 智能指针所有权管理

---

## 4. 文件结构

```
ESDK_On_Sophon/
├── src/
│   ├── main.cpp                      # ✅ 启动器 (50行)
│   ├── core/
│   │   ├── Application.h             # 🆕 应用程序主类
│   │   ├── Application.cpp
│   │   ├── Logger.cpp
│   │   └── Config.cpp
│   ├── mqtt/
│   │   ├── MqttHandler.h             # 🆕 MQTT处理器
│   │   ├── MqttHandler.cpp
│   │   └── MqttClient.cpp
│   ├── task/                         # 🆕 任务管理模块
│   │   ├── TaskManager.h             # 🆕 任务管理器
│   │   └── TaskManager.cpp
│   └── device/
│       ├── DeviceManager.h
│       └── DeviceManager.cpp
│
├── include/esdk_sophon/
│   ├── core/
│   │   ├── Application.h
│   │   ├── Logger.h
│   │   └── Config.h
│   ├── types/                        # 🆕 通用数据类型模块
│   │   ├── TaskConfig.h              # ✅ 已完成 (任务配置)
│   │   └── EventMessage.h            # ✅ 已完成 (事件消息)
│   ├── task/                         # 🆕 任务管理模块
│   │   └── TaskManager.h             # 🆕 待创建
│   ├── mqtt/
│   │   ├── MqttHandler.h
│   │   └── MqttClient.h
│   ├── vision/                       # 👁️ 视觉处理模块（专注）
│   │   ├── CompletionDetector.h      # ✅ 已完成
│   │   ├── MediaFileFilter.h         # ✅ 已完成
│   │   └── PerformanceMonitor.h      # ✅ 已完成
│   └── device/
│       └── DeviceManager.h
│
├── tests/
│   ├── test_vision_integration.cpp   # ✅ 已完成
│   ├── test_task_config.cpp          # ✅ 已完成
│   ├── test_task_manager.cpp         # 🆕 待创建
│   └── test_mqtt_handler.cpp         # 🆕 待创建
│
└── docs/
    ├── Vision模块集成设计方案.md      # 📝 本文档
    ├── 改进记录-2025-10.md
    └── 八股知识点.md
```

**架构重构说明** (2025-10-28):

重构前的问题：

- ❌ TaskConfig、EventMessage 放在 vision 模块，导致职责混乱
- ❌ vision 模块既做视觉处理，又管理数据结构
- ❌ 违反单一职责原则，模块臃肿

重构后的改进：

- ✅ **types/** - 通用数据类型，可被多个模块复用
- ✅ **task/** - 任务管理逻辑，独立于 vision
- ✅ **vision/** - 专注视觉算法辅助功能
- ✅ 清晰的职责划分，低耦合高内聚

---

## 5. 实现步骤

### 阶段 1: 创建数据结构 (第 1 天)

#### 步骤 1.1: 创建 TaskConfig.h

- [x] TaskConfig 结构体定义 ✅
- [x] AlgorithmType 嵌套结构体 ✅
- [x] findEventType()方法实现 ✅
- [x] getAllClasses()方法实现 ✅
- [x] fromJson()静态工厂方法 ✅

#### 步骤 1.2: 创建 EventMessage.h

- [x] EventMessage 结构体定义 ✅
- [x] DetectionPoint 嵌套结构体 ✅
- [x] SegmentationMask 嵌套结构体 ✅
- [x] toJson()序列化方法 ✅
- [x] Base64 编码工具函数 ✅

#### 步骤 1.3: 编写单元测试

- [x] test_task_config.cpp ✅
- [x] 测试 fromJson()解析 ✅
- [x] 测试 findEventType()逻辑 ✅
- [x] 测试 getAllClasses()去重 ✅

#### 步骤 1.4: 架构重构

- [x] 创建 types/模块 ✅
- [x] 移动 TaskConfig.h 到 types/ ✅
- [x] 移动 EventMessage.h 到 types/ ✅
- [x] 更新命名空间从 vision 到 types ✅
- [x] 更新所有引用路径 ✅
- [x] 重新编译验证 ✅

### 阶段 2: 创建 MqttHandler (第 2 天)

#### 步骤 2.1: 创建 MqttHandler.h

- [ ] MqttHandler 类定义
- [ ] 方法声明

#### 步骤 2.2: 实现 MqttHandler.cpp

- [ ] connect()/disconnect()
- [ ] onMessage()回调
- [ ] handleAlgorithmEnable()
- [ ] handleTaskEnd()
- [ ] publishEvent()

#### 步骤 2.3: 编写测试

- [ ] test_mqtt_handler.cpp
- [ ] 模拟 MQTT 消息

### 阶段 3: 创建 TaskManager (第 3-4 天)

#### 步骤 3.1: 创建 TaskManager.h

- [ ] TaskManager 类定义
- [ ] TaskState 结构体

#### 步骤 3.2: 实现 TaskManager.cpp

- [ ] startTask() - 创建任务
- [ ] onMediaFileUpdate() - 图片模式处理
- [ ] onLiveviewFrame() - 视频流处理
- [ ] onTaskEnd() - 任务结束
- [ ] stopTask() - 停止任务

#### 步骤 3.3: 集成 Vision 模块

- [ ] 实例化 CompletionDetector
- [ ] 实例化 MediaFileFilter
- [ ] 实例化 PerformanceMonitor
- [ ] 设置回调函数

### 阶段 4: 创建 Application (第 5 天)

#### 步骤 4.1: 创建 Application.h/cpp

- [ ] Application 类实现
- [ ] initialize() - 初始化子系统
- [ ] run() - 主循环
- [ ] shutdown() - 清理资源

#### 步骤 4.2: 重构 main.cpp

- [ ] 简化为启动器
- [ ] 信号处理
- [ ] 配置加载

### 阶段 5: 集成测试 (第 6 天)

#### 步骤 5.1: 完整流程测试

- [ ] test_full_integration.cpp
- [ ] 模拟视频流模式
- [ ] 模拟图片模式
- [ ] 验证 MQTT 上报

#### 步骤 5.2: Docker 编译

- [ ] 在容器中编译
- [ ] 解决编译错误

### 阶段 6: 运行验证 (第 7 天)

#### 步骤 6.1: SE7 设备运行

- [ ] 部署到设备
- [ ] 连接真实 MQTT
- [ ] 接收真实媒体文件
- [ ] 验证功能正确性

---

## 6. 知识点总结

### 6.1 设计模式

| 模式       | 应用位置               | 作用         |
| ---------- | ---------------------- | ------------ |
| 单例模式   | TaskManager            | 全局唯一实例 |
| 工厂模式   | TaskConfig::fromJson() | 对象创建封装 |
| 观察者模式 | MQTT 回调              | 事件通知     |
| RAII       | Application 子系统管理 | 资源自动清理 |
| 策略模式   | 算法处理器             | 算法替换     |

### 6.2 C++特性

| 特性            | 应用位置         | 知识点       |
| --------------- | ---------------- | ------------ |
| std::unique_ptr | Application 成员 | 独占所有权   |
| std::shared_ptr | 算法处理器       | 共享所有权   |
| std::atomic     | running\_标志    | 线程安全     |
| std::mutex      | tasks\_保护      | 互斥锁       |
| std::set        | 类别去重         | 自动排序去重 |
| lambda          | 回调函数         | 匿名函数     |

### 6.3 架构设计

| 原则       | 体现             |
| ---------- | ---------------- |
| 单一职责   | 每个类职责单一   |
| 依赖倒置   | 通过指针传递依赖 |
| 开闭原则   | 对扩展开放       |
| 接口隔离   | 接口最小化       |
| 迪米特法则 | 模块间低耦合     |

### 6.4 面试要点

**Q1: 为什么 main.cpp 只有 50 行？**

A: 遵循单一职责原则，main.cpp 只负责启动应用。真正的业务逻辑封装在 Application 和各个 Handler 中。这样做的好处是：

1. 易于测试（业务逻辑可以独立测试）
2. 易于维护（修改业务不影响启动流程）
3. 易于扩展（新增功能只需修改对应模块）

**Q2: TaskConfig 为什么用静态工厂方法？**

A: 使用静态工厂方法 fromJson()的好处：

1. 封装复杂的构造逻辑
2. 可以返回错误（构造函数不能）
3. 语义更清晰（fromJson 表明从 JSON 创建）
4. 符合工厂模式

**Q3: 为什么用指针而不是引用传递依赖？**

A: 使用指针的原因：

1. 可以为 nullptr（表示依赖未设置）
2. 可以重新赋值（运行时更换依赖）
3. 明确表示"不拥有所有权"
4. 避免循环依赖

**Q4: 如何处理多算法类型和类别重复？**

A:

1. 使用 std::vector 存储多个 AlgorithmType
2. 使用 std::set 自动去重类别
3. findEventType()返回第一个匹配的 eventType
4. getAllClasses()返回去重后的所有类别

**Q5: 为什么要将 TaskConfig 和 EventMessage 从 vision 模块移到 types 模块？**

A: 这是一次重要的架构重构，理由包括：

1. **单一职责原则**：

   - vision 模块应该专注于视觉算法辅助功能
   - TaskConfig 是通用的任务配置，不应绑定到特定领域
   - EventMessage 是 MQTT 消息格式，属于数据类型定义

2. **依赖管理**：

   - 避免循环依赖：task 模块依赖 types 模块
   - 如果 TaskConfig 在 vision，会出现 task→vision 的奇怪依赖
   - 正确的依赖关系：task→types ← vision

3. **可复用性**：

   - types 模块的数据结构可以被多个模块使用
   - 未来可能有非视觉任务（音频、文本）也需要 TaskConfig
   - EventMessage 可以被 MQTT、Storage 等模块使用

4. **模块内聚性**：

   - vision 模块内的类相互关联密切（都服务于视觉处理）
   - TaskConfig 和 EventMessage 是独立的数据定义
   - 分离后各模块职责更清晰

5. **可维护性**：
   - 减少模块间耦合
   - 便于单独测试和修改
   - 符合开闭原则（对扩展开放，对修改关闭）

---

## 📊 进度追踪

| 阶段   | 任务                        | 状态      | 完成日期   |
| ------ | --------------------------- | --------- | ---------- |
| 阶段 1 | TaskConfig.h (types 模块)   | ✅ 已完成 | 2025-10-28 |
| 阶段 1 | EventMessage.h (types 模块) | ✅ 已完成 | 2025-10-28 |
| 阶段 1 | 单元测试                    | ✅ 已完成 | 2025-10-28 |
| 阶段 1 | 架构重构(vision→types/task) | ✅ 已完成 | 2025-10-28 |
| 阶段 2 | MqttHandler                 | 🔲 待开始 | -          |
| 阶段 3 | TaskManager (task 模块)     | 🔲 待开始 | -          |
| 阶段 4 | Application                 | 🔲 待开始 | -          |
| 阶段 5 | 集成测试                    | 🔲 待开始 | -          |
| 阶段 6 | 运行验证                    | 🔲 待开始 | -          |

---

## 🎯 预期成果

完成后你将拥有：

1. ✅ **清晰的分层架构** - main → Application → Handlers → Modules
2. ✅ **完整的任务管理** - 支持视频流和图片两种模式
3. ✅ **自动化完成检测** - 基于自适应定时器
4. ✅ **智能文件过滤** - 基于 EXIF 时间戳
5. ✅ **完整的性能监控** - JSON/CSV 导出
6. ✅ **MQTT 事件上报** - 符合接口文档规范
7. ✅ **可测试的代码** - 单元测试+集成测试
8. ✅ **面试准备材料** - 丰富的技术亮点

---

**文档版本**: v1.0  
**最后更新**: 2025-10-28  
**作者**: GitHub Copilot + 你
