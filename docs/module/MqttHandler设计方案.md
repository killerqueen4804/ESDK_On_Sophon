# MqttHandler 设计方案

> **设计日期**: 2025-10-29  
> **模块位置**: `src/mqtt/MqttHandler.cpp`  
> **依赖模块**: MqttClient, TaskManager, types  
> **状态**: 🔧 设计中

---

## 📋 模块定位

### 职责边界

**MqttHandler** 是连接 **MQTT 通信层** 和 **业务逻辑层** 的**中间件**，负责：

```
MQTT Broker (云端)
      ↕ MQTT消息
  MqttClient (通信层)
      ↕ topic + JSON字符串
  MqttHandler (中间件) ⭐ 本模块
      ↕ 结构化数据 (TaskConfig等)
  TaskManager (业务层)
      ↕ 任务执行
  Vision/Device (执行层)
```

### 核心职责

| 职责            | 说明                          | 示例                               |
| --------------- | ----------------------------- | ---------------------------------- |
| **1. 指令解析** | 将 MQTT JSON 解析为结构化数据 | JSON → TaskConfig                  |
| **2. 指令路由** | 根据 method 调用不同处理函数  | `device_algorithm_sync` → 启动任务 |
| **3. 结果上报** | 将检测结果上报到 MQTT         | DetectionResult → JSON → MQTT      |
| **4. 应答发送** | 对云端指令进行应答            | `services_reply` topic             |
| **5. 错误处理** | 统一的错误日志和异常处理      | 解析失败、执行失败                 |

---

## 🏗️ 架构设计

### 类图

```
┌────────────────────────────────────────────┐
│     IMqttMessageObserver (接口)            │
├────────────────────────────────────────────┤
│ + onMessageReceived(topic, message)        │
│ + onConnectionLost(cause)                  │
└────────────────────────────────────────────┘
                   ▲
                   │ implements
                   │
┌──────────────────┴────────────────────────┐
│            MqttHandler                     │
├────────────────────────────────────────────┤
│ - mqttClient_: MqttClient&                 │
│ - taskManager_: TaskManager&               │
│ - logger_: Logger&                         │
│ - config_: Config&                         │
│ - deviceSn_: string                        │
├────────────────────────────────────────────┤
│ + getInstance(): MqttHandler&              │
│ + initialize(): bool                       │
│ + start(): bool                            │
│ + stop(): void                             │
│                                            │
│ # onMessageReceived(topic, msg) override   │
│ # onConnectionLost(cause) override         │
│                                            │
│ - handleServiceCommand(json): void         │
│ - handleAlgorithmSync(json): void          │
│ - handleAlgorithmEnable(json): void        │
│ - handleAlgorithmDisable(json): void       │
│ - handleTaskEnd(json): void                │
│ - handleReboot(json): void                 │
│                                            │
│ - sendServiceReply(method, result, taskId) │
│ - publishEvent(eventData): void            │
│ - publishDetectionResult(result): void     │
└────────────────────────────────────────────┘
```

---

## 🔌 接口设计

### 公共接口

```cpp
namespace esdk_sophon {
namespace mqtt {

/**
 * @brief MQTT消息处理器
 *
 * 职责：
 * 1. 实现IMqttMessageObserver接口，接收MQTT消息
 * 2. 解析云端指令JSON，路由到对应处理函数
 * 3. 调用TaskManager执行业务逻辑
 * 4. 将结果上报到MQTT
 */
class MqttHandler : public IMqttMessageObserver {
public:
    /**
     * @brief 获取单例实例 (Meyers单例)
     */
    static MqttHandler& getInstance();

    /**
     * @brief 初始化
     *
     * @return true 初始化成功
     * @return false 初始化失败
     *
     * @note 从Config读取deviceSn等配置
     */
    bool initialize();

    /**
     * @brief 启动消息处理
     *
     * @return true 启动成功
     * @return false 启动失败
     *
     * @details
     * 1. 注册为MqttClient的观察者
     * 2. 订阅云端指令topic
     */
    bool start();

    /**
     * @brief 停止消息处理
     *
     * @details
     * 1. 取消订阅topic
     * 2. 从MqttClient移除观察者
     */
    void stop();

    /**
     * @brief 上报检测结果
     *
     * @param result 检测结果数据
     *
     * @note 由Vision模块调用
     */
    void publishDetectionResult(const types::DetectionResult& result);

    // IMqttMessageObserver接口实现
    void onMessageReceived(const std::string& topic,
                          const std::string& message) override;
    void onConnectionLost(const std::string& cause) override;

private:
    MqttHandler();  // 单例模式
    ~MqttHandler();

    // 禁止拷贝
    MqttHandler(const MqttHandler&) = delete;
    MqttHandler& operator=(const MqttHandler&) = delete;

    // 内部成员变量
    MqttClient& mqttClient_;
    TaskManager& taskManager_;
    Logger& logger_;
    Config& config_;
    std::string deviceSn_;  // 设备序列号
};

}  // namespace mqtt
}  // namespace esdk_sophon
```

---

## 📡 Topic 规则

### 订阅 Topic (下行 - 接收云端指令)

| Topic                                | 用途         | QoS | 示例               |
| ------------------------------------ | ------------ | --- | ------------------ |
| `thing/product/{device_sn}/services` | 云端服务调用 | 1   | 启动任务、停止任务 |

### 发布 Topic (上行 - 发送数据到云端)

| Topic                                      | 用途          | QoS | 示例              |
| ------------------------------------------ | ------------- | --- | ----------------- |
| `thing/product/{device_sn}/services_reply` | 服务调用应答  | 1   | 任务启动成功/失败 |
| `drone/{device_sn}/info/event`             | 推送检测结果  | 0   | 检测到垃圾倾倒    |
| `thing/product/{device_sn}/events`         | 上报进度/状态 | 1   | 任务进度更新      |

**其中** `{device_sn}` = `analysis_device_WRSE7001` (从 config.json 读取)

---

## 📨 消息格式

### 1. 云端指令 (services)

#### 1.1 算法同步 (启动任务)

```json
{
  "method": "device_algorithm_sync",
  "data": {
    "taskID": 1234,
    "algorithmRepoID": 1,
    "algorithmName": "目标检测",
    "version": "1.0.0",
    "type": [
      {
        "id": 200004,
        "name": "垃圾倾倒",
        "class": ["car", "person"],
        "mainType": 100000
      }
    ],
    "source": 1,
    "display": 0,
    "airtransfer": 1
  }
}
```

**处理逻辑**:

1. 解析 data 字段为 TaskConfig
2. 调用 `taskManager_.startTask(config)`
3. 发送应答到 `services_reply`

#### 1.2 关闭算法 (停止任务)

```json
{
  "method": "device_algorithm_disable",
  "data": {
    "taskID": 1234
  }
}
```

**处理逻辑**:

1. 提取 taskID
2. 调用 `taskManager_.stopTask(taskId)`
3. 发送应答

#### 1.3 任务结束

```json
{
  "method": "device_task_end",
  "data": {
    "taskID": 1234
  }
}
```

**处理逻辑**:

1. 停止任务
2. 清理任务资源
3. 发送应答

#### 1.4 设备重启

```json
{
  "method": "device_reboot"
}
```

**处理逻辑**:

1. 停止所有任务
2. 发送应答
3. 执行 `reboot` 命令

---

### 2. 设备应答 (services_reply)

```json
{
  "result": 0, // 0=成功, 1=错误, 255=未知错误
  "method": "device_algorithm_sync",
  "taskID": 1234,
  "message": "任务启动成功" // 可选，错误时提供详细信息
}
```

**result 错误码**:

- `0`: 成功
- `1`: 参数错误
- `2`: 任务不存在
- `3`: 资源不足
- `255`: 未知错误

---

### 3. 检测结果上报 (info/event)

```json
{
  "taskID": 1234,
  "eventType": 200004, // 算法类型ID (垃圾倾倒)
  "timestamp": "2025-10-29T14:30:00Z",
  "detections": [
    {
      "class": "car",
      "confidence": 0.95,
      "bbox": [100, 200, 300, 400]
    }
  ],
  "imageUrl": "http://example.com/image.jpg",
  "videoUrl": "http://example.com/video.mp4"
}
```

---

## 🔄 数据流设计

### 完整流程图

```
云端 (MQTT Broker)
     │
     │ ① 发送指令 (services)
     ▼
MqttClient.onMessageArrived()
     │
     │ ② 通知观察者
     ▼
MqttHandler.onMessageReceived(topic, json)
     │
     ├─③ topic匹配
     │
     ├─④ 解析JSON
     │    ├─ method = "device_algorithm_sync"
     │    └─ data → TaskConfig
     │
     ├─⑤ 路由处理
     │    └─ handleAlgorithmSync(json)
     │
     ├─⑥ 调用业务层
     │    └─ taskManager_.startTask(config)
     │         │
     │         ├─ 创建TaskState
     │         ├─ 分配TPU资源
     │         └─ 启动检测线程
     │
     ├─⑦ 发送应答
     │    └─ sendServiceReply("device_algorithm_sync", 0, 1234)
     │         │
     │         └─ publish("services_reply", json)
     │
     └─⑧ 日志记录
          └─ logger_.info("任务1234启动成功")


(后台执行) Vision模块检测到目标
     │
     ├─⑨ 生成检测结果
     │    └─ DetectionResult { taskID, eventType, detections }
     │
     ├─⑩ 回调上报
     │    └─ mqttHandler_.publishDetectionResult(result)
     │
     └─⑪ 发送到MQTT
          └─ publish("info/event", json)
```

---

## 🧩 关键技术点

### 1. JSON 解析策略

使用 **nlohmann/json** 库进行 JSON 解析：

```cpp
void MqttHandler::handleAlgorithmSync(const nlohmann::json& data) {
    try {
        // 方法1: 直接用TaskConfig::fromJson()
        TaskConfig config = TaskConfig::fromJson(data);

        // 验证配置
        if (!config.isValid()) {
            logger_.error("任务配置无效: taskID=" +
                         std::to_string(config.taskID));
            sendServiceReply("device_algorithm_sync", 1, config.taskID);
            return;
        }

        // 启动任务
        bool success = taskManager_.startTask(config);

        // 发送应答
        int result = success ? 0 : 3;  // 3=资源不足
        sendServiceReply("device_algorithm_sync", result, config.taskID);

    } catch (const nlohmann::json::exception& e) {
        logger_.error("JSON解析错误: " + std::string(e.what()));
        sendServiceReply("device_algorithm_sync", 1, 0);
    }
}
```

**面试要点**:

- Q: 为什么用 try-catch 包裹？
- A: `at()` 访问不存在的键会抛出 `json::exception`，用 try-catch 捕获并发送错误应答

---

### 2. 指令路由机制

使用 **策略模式** + **函数指针映射**：

```cpp
class MqttHandler {
private:
    // 方法1: if-else (简单直观)
    void handleServiceCommand(const nlohmann::json& json) {
        std::string method = json.at("method").get<std::string>();
        const auto& data = json.at("data");

        if (method == "device_algorithm_sync") {
            handleAlgorithmSync(data);
        } else if (method == "device_algorithm_disable") {
            handleAlgorithmDisable(data);
        } else if (method == "device_task_end") {
            handleTaskEnd(data);
        } else if (method == "device_reboot") {
            handleReboot(data);
        } else {
            logger_.warning("未知指令: " + method);
        }
    }

    // 方法2: map映射 (可扩展性好) ⭐ 推荐
    using CommandHandler = std::function<void(const nlohmann::json&)>;
    std::map<std::string, CommandHandler> commandHandlers_;

    void initCommandHandlers() {
        commandHandlers_["device_algorithm_sync"] =
            [this](const nlohmann::json& data) {
                handleAlgorithmSync(data);
            };

        commandHandlers_["device_algorithm_disable"] =
            [this](const nlohmann::json& data) {
                handleAlgorithmDisable(data);
            };

        // ... 其他指令
    }

    void handleServiceCommand(const nlohmann::json& json) {
        std::string method = json.at("method").get<std::string>();

        auto it = commandHandlers_.find(method);
        if (it != commandHandlers_.end()) {
            it->second(json.at("data"));  // 调用对应处理函数
        } else {
            logger_.warning("未知指令: " + method);
        }
    }
};
```

**面试要点**:

- Q: map 映射的优势？
- A:
  1. 避免冗长的 if-else 链
  2. 易于添加新指令（开闭原则）
  3. 可以动态注册/注销指令
  4. 查找效率 O(log n)

---

### 3. Topic 匹配逻辑

```cpp
void MqttHandler::onMessageReceived(const std::string& topic,
                                    const std::string& message) {
    logger_.debug("收到消息 topic=" + topic);

    try {
        // 解析JSON
        nlohmann::json json = nlohmann::json::parse(message);

        // 根据topic路由
        std::string servicesTopicPattern =
            "thing/product/" + deviceSn_ + "/services";

        if (topic == servicesTopicPattern) {
            // 云端指令
            handleServiceCommand(json);
        } else {
            logger_.warning("未知topic: " + topic);
        }

    } catch (const nlohmann::json::exception& e) {
        logger_.error("JSON解析失败: " + std::string(e.what()));
    }
}
```

---

### 4. 应答发送封装

```cpp
void MqttHandler::sendServiceReply(const std::string& method,
                                   int result,
                                   int taskId) {
    nlohmann::json reply;
    reply["result"] = result;
    reply["method"] = method;
    reply["taskID"] = taskId;

    // 错误时添加message字段
    if (result != 0) {
        std::string errorMsg;
        switch (result) {
            case 1: errorMsg = "参数错误"; break;
            case 2: errorMsg = "任务不存在"; break;
            case 3: errorMsg = "资源不足"; break;
            default: errorMsg = "未知错误"; break;
        }
        reply["message"] = errorMsg;
    }

    std::string topic = "thing/product/" + deviceSn_ + "/services_reply";
    std::string payload = reply.dump();

    mqttClient_.publish(topic, payload, 1);  // QoS 1

    logger_.info("发送应答 method=" + method +
                " result=" + std::to_string(result));
}
```

---

## 🧪 测试计划

### 单元测试用例

```cpp
// tests/test_mqtt_handler.cpp

void test1_ParseAlgorithmSync() {
    // 测试解析device_algorithm_sync指令
    std::string json = R"({
        "method": "device_algorithm_sync",
        "data": {
            "taskID": 1001,
            "source": 1,
            ...
        }
    })";

    // 验证解析成功
    // 验证TaskConfig正确
}

void test2_HandleStartTask() {
    // 测试启动任务流程
    // 验证调用了TaskManager::startTask()
    // 验证发送了正确的应答
}

void test3_HandleStopTask() {
    // 测试停止任务流程
}

void test4_PublishDetectionResult() {
    // 测试上报检测结果
    // 验证JSON格式正确
    // 验证发送到正确的topic
}

void test5_ErrorHandling() {
    // 测试各种错误场景
    // - JSON格式错误
    // - 缺少必需字段
    // - 任务不存在
    // 验证错误应答正确
}
```

---

## 📚 知识点总结

### 设计模式

| 模式       | 应用                      | 优势                       |
| ---------- | ------------------------- | -------------------------- |
| 观察者模式 | 实现 IMqttMessageObserver | 解耦 MqttClient 和业务逻辑 |
| 单例模式   | getInstance()             | 全局唯一实例               |
| 策略模式   | 指令路由 map              | 易于扩展新指令             |
| 工厂方法   | TaskConfig::fromJson()    | 封装复杂构造逻辑           |

### C++技术

| 技术          | 应用           | 说明             |
| ------------- | -------------- | ---------------- |
| std::function | CommandHandler | 函数对象包装     |
| std::map      | 指令路由表     | 快速查找处理函数 |
| lambda 表达式 | 注册命令处理器 | 简洁的回调定义   |
| try-catch     | JSON 解析      | 异常安全         |

### 面试高频

1. **Q: MqttHandler 为什么要单例？**

   - A: 保证只有一个消息处理实例，避免重复订阅 topic

2. **Q: 为什么用观察者模式？**

   - A: MqttClient 负责通信，MqttHandler 负责业务，解耦关注点

3. **Q: 如何保证消息处理的线程安全？**

   - A: onMessageReceived 可能在 MQTT 内部线程调用，访问共享资源需加锁

4. **Q: 如何扩展新的指令类型？**
   - A: 在 commandHandlers\_中添加新映射，实现对应处理函数

---

## ✅ 实现检查清单

- [ ] 创建 `include/esdk_sophon/mqtt/MqttHandler.h`
- [ ] 创建 `src/mqtt/MqttHandler.cpp`
- [ ] 实现单例模式
- [ ] 实现 IMqttMessageObserver 接口
- [ ] 实现指令路由机制
- [ ] 实现各个指令处理函数
- [ ] 实现应答发送函数
- [ ] 实现结果上报函数
- [ ] 添加完整注释
- [ ] 创建测试文件 `tests/test_mqtt_handler.cpp`
- [ ] 编译通过
- [ ] 单元测试通过
- [ ] 更新 CMakeLists.txt
- [ ] 更新文档

---

**下一步**: 开始实现 MqttHandler 类

**预计代码量**: 400-500 行 (含详细注释)
