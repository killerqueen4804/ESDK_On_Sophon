# MQTT 模块详解 - 通信层

> **学习目标**：掌握 MQTT 协议和观察者模式的实现
>
> **核心知识点**：
>
> - 观察者模式（Observer Pattern）
> - C++ 回调函数（Callback）
> - 异步编程（条件变量、后台线程）
> - 智能指针与生命周期管理
> - paho.mqtt.c 库的使用

---

## 📚 目录

1. [MQTT 协议简介](#mqtt-协议简介)
2. [模块架构](#模块架构)
3. [观察者模式详解](#观察者模式详解)
4. [MqttClient 实现详解](#mqttclient-实现详解)
5. [异步重连机制](#异步重连机制)
6. [面试高频考点](#面试高频考点)
7. [实战练习](#实战练习)

---

## MQTT 协议简介

### 什么是 MQTT？

**MQTT**（Message Queuing Telemetry Transport）是一种轻量级的发布/订阅消息传输协议，专为物联网（IoT）设计。

```
┌─────────────────────────────────────────────────────────┐
│                     MQTT 代理 (Broker)                   │
│                                                         │
│   订阅者A ◄────── topic/temperature ───────► 发布者1    │
│   订阅者B ◄────── topic/humidity ──────────► 发布者2    │
│   订阅者C ◄────── topic/# ─────────────────► 发布者3    │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### MQTT 核心概念

| 概念                  | 说明           | 项目中的应用                    |
| --------------------- | -------------- | ------------------------------- |
| **Broker（代理）**    | 消息中转站     | 通常是 EMQX、Mosquitto 等服务器 |
| **Topic（主题）**     | 消息分类标签   | 如 `esdk/command`, `esdk/event` |
| **Publish（发布）**   | 向主题发送消息 | 上报检测事件、状态信息          |
| **Subscribe（订阅）** | 监听主题消息   | 接收云端命令、任务指令          |
| **QoS（服务质量）**   | 消息可靠性级别 | 0/1/2 三个级别                  |

### QoS 服务质量等级

```cpp
enum QoS {
    QoS0 = 0,  // 最多一次 - 火andForget，不保证送达
    QoS1 = 1,  // 至少一次 - 可能重复，保证送达
    QoS2 = 2   // 恰好一次 - 最高可靠性，最高开销
};
```

**项目中的选择：**

- 命令消息：QoS 1（保证送达，允许重复处理）
- 事件上报：QoS 1（保证云端收到事件）
- 心跳/状态：QoS 0（不重要，丢失可接受）

### 主题通配符

```
+  : 单层通配符
#  : 多层通配符

示例：
esdk/device/+/status  → 匹配 esdk/device/1/status
                      → 匹配 esdk/device/2/status
                      → 不匹配 esdk/device/1/status/battery

esdk/#                → 匹配 esdk/所有子主题
```

---

## 模块架构

### 文件结构

```
src/Mqtt/
├── MqttClient.cpp         # MQTT客户端实现（底层通信）
├── MqttHandler.cpp        # MQTT消息处理器（业务路由）⭐ 核心

include/esdk_sophon/mqtt/
├── MqttClient.h           # MQTT客户端接口
├── MqttHandler.h          # 消息处理器接口 ⭐
└── IMqttMessageObserver.h # 观察者接口
```

### 模块职责划分

| 类名                     | 职责               | 设计模式      |
| ------------------------ | ------------------ | ------------- |
| **MqttClient**           | 底层 MQTT 通信封装 | 单例 + Pimpl  |
| **MqttHandler**          | 消息解析与业务路由 | 单例 + 观察者 |
| **IMqttMessageObserver** | 消息回调接口       | 观察者接口    |

### 类图（完整版）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MQTT 模块完整架构                               │
└─────────────────────────────────────────────────────────────────────────────┘

              云端平台
                 │
                 │ MQTT 消息
                 ▼
┌──────────────────────────────────────┐
│            MqttClient                │ ◄─── 底层通信
│  (单例模式 + Pimpl)                  │      封装 paho.mqtt.c
├──────────────────────────────────────┤
│ + getInstance() : MqttClient&        │
│ + initialize() : bool                │
│ + connect() : bool                   │
│ + publish() : bool                   │
│ + subscribe() : bool                 │
│ + addObserver() : void               │
│ + notifyObservers() : void           │
└─────────────────┬────────────────────┘
                  │
                  │ 观察者模式通知
                  ▼
┌──────────────────────────────────────┐
│       IMqttMessageObserver           │ ◄─── 接口定义
│           (接口类)                   │
├──────────────────────────────────────┤
│ + onMessageReceived() = 0            │
│ + onConnectionLost() = 0             │
│ + onConnected()                      │
│ + onDeliveryComplete()               │
└─────────────────┬────────────────────┘
                  │
                  │ 实现接口
                  ▼
┌──────────────────────────────────────┐
│           MqttHandler                │ ◄─── ⭐ 业务路由核心
│  (单例模式 + 策略模式)               │
├──────────────────────────────────────┤
│ + getInstance() : MqttHandler&       │
│ + initialize() : bool                │
│ + start() : bool                     │
│ + stop() : void                      │
│ + onMessageReceived() override       │ ◄─── 收到消息后解析路由
├──────────────────────────────────────┤
│ - commandHandlers_ : map<string,func>│ ◄─── 指令路由表
│ - handleAlgorithmEnable()            │      "device_algorithm_enable"
│ - handleAlgorithmDisable()           │      "device_algorithm_disable"
│ - handleTaskEnd()                    │      "device_task_end"
│ - handleReboot()                     │      "device_reboot"
└─────────────────┬────────────────────┘
                  │
                  │ 调用业务逻辑
                  ▼
┌──────────────────────────────────────┐
│          TaskManager                 │ ◄─── 任务管理
├──────────────────────────────────────┤
│ + startTask()                        │
│ + stopTask()                         │
│ + getTask()                          │
└──────────────────────────────────────┘
```

### 数据流向（接收消息）

```
云端平台
    │
    │ 1. MQTT 消息到达
    │    Topic: thing/product/{sn}/services
    │    Payload: {"method": "device_algorithm_enable", "data": {...}}
    ▼
┌─────────────────────────────────────┐
│ MqttClient (paho.mqtt.c 回调)       │
│                                     │
│ onMessageArrivedCallback() 被触发   │
│     │                               │
│     ▼                               │
│ notifyMessageReceived(topic, msg)   │
└─────────────────┬───────────────────┘
                  │
                  │ 2. 观察者模式通知
                  ▼
┌─────────────────────────────────────┐
│ MqttHandler::onMessageReceived()    │
│                                     │
│ 3. JSON 解析                        │
│    json = nlohmann::json::parse()   │
│                                     │
│ 4. 提取 method 字段                 │
│    method = "device_algorithm_enable"│
│                                     │
│ 5. 查找路由表                       │
│    commandHandlers_[method]         │
│                                     │
│ 6. 调用对应处理函数                 │
│    handleAlgorithmEnable(data)      │
└─────────────────┬───────────────────┘
                  │
                  │ 7. 业务处理
                  ▼
┌─────────────────────────────────────┐
│ TaskManager::startTask(config)      │
│                                     │
│ 创建任务、启动检测...               │
└─────────────────────────────────────┘
```

---

## 观察者模式详解

### 什么是观察者模式？

**观察者模式**（Observer Pattern）定义了对象间的一对多依赖关系，当一个对象状态改变时，所有依赖它的对象都会收到通知并自动更新。

```
┌─────────────┐          ┌─────────────┐
│   Subject   │◄────────►│  Observer   │
│   (主题)    │  观察    │  (观察者)   │
└─────────────┘          └─────────────┘
      │                        ▲
      │ 状态变化               │ 实现
      ▼                        │
┌─────────────┐          ┌─────────────┐
│  通知所有   │          │ ConcreteObs │
│  观察者     │          │ (具体观察者)│
└─────────────┘          └─────────────┘
```

### 项目中的观察者接口

```cpp
/**
 * @brief MQTT消息观察者接口
 *
 * 【知识点】纯虚函数 vs 虚函数
 * - 纯虚函数 (= 0): 必须被派生类实现
 * - 虚函数: 可以有默认实现，派生类可选择性重写
 */
class IMqttMessageObserver {
public:
    // 虚析构函数：接口类必须有！
    virtual ~IMqttMessageObserver() = default;

    // 纯虚函数：必须实现
    virtual void onMessageReceived(const std::string& topic,
                                   const std::string& message) = 0;
    virtual void onConnectionLost(const std::string& cause) = 0;

    // 虚函数：有默认实现，可选重写
    virtual void onConnected(const std::string& serverUri) {
        (void)serverUri;  // 避免未使用参数警告
    }

    virtual void onDeliveryComplete(int token) {
        (void)token;
    }
};
```

📌 **知识点：为什么接口类需要虚析构函数？**

```cpp
// ❌ 没有虚析构函数的问题
class Base {
public:
    ~Base() { std::cout << "Base析构\n"; }  // 非虚
};

class Derived : public Base {
public:
    ~Derived() { std::cout << "Derived析构\n"; }
    std::string* data = new std::string("test");  // 需要释放
};

Base* p = new Derived();
delete p;  // 只调用Base的析构！Derived的data内存泄漏！

// ✅ 有虚析构函数
class Base {
public:
    virtual ~Base() { std::cout << "Base析构\n"; }  // 虚
};

delete p;  // 先调用Derived析构，再调用Base析构 ✓
```

### 观察者的注册与通知

```cpp
// 添加观察者
void MqttClient::Impl::addObserver(IMqttMessageObserver* observer) {
    if (observer == nullptr) return;

    std::lock_guard<std::mutex> lock(observersMutex_);

    // 检查是否已存在，避免重复添加
    auto it = std::find(observers_.begin(), observers_.end(), observer);
    if (it != observers_.end()) return;

    observers_.push_back(observer);
}

// 通知所有观察者
void MqttClient::Impl::notifyMessageReceived(const std::string& topic,
                                              const std::string& message) {
    std::lock_guard<std::mutex> lock(observersMutex_);

    // ⭐ 关键：拷贝观察者列表，避免迭代器失效
    std::vector<IMqttMessageObserver*> observersCopy = observers_;

    for (auto* observer : observersCopy) {
        if (observer == nullptr) continue;

        try {
            observer->onMessageReceived(topic, message);
        } catch (const std::exception& e) {
            // 一个观察者出错不影响其他观察者
            logger_.error("观察者处理消息异常: " + std::string(e.what()));
        }
    }
}
```

📌 **知识点：为什么要拷贝观察者列表？**

```cpp
// ❌ 问题：直接迭代可能导致迭代器失效
for (auto* observer : observers_) {
    observer->onMessageReceived(topic, message);
    // 如果观察者在回调中调用 removeObserver()，
    // observers_ 被修改，迭代器失效！
    // → 未定义行为（可能崩溃）
}

// ✅ 解决：先拷贝，再迭代
std::vector<IMqttMessageObserver*> copy = observers_;
for (auto* observer : copy) {
    observer->onMessageReceived(topic, message);
    // 即使原列表被修改，copy 不变，安全迭代
}
```

---

## MqttClient 实现详解

### 1. paho.mqtt.c 回调函数

paho.mqtt.c 是 C 语言库，使用函数指针作为回调：

```cpp
// paho 的回调设置（C风格）
MQTTClient_setCallbacks(
    pahoClient_,              // 客户端句柄
    this,                     // 用户数据（context）
    onConnectionLostCallback, // 连接丢失回调
    onMessageArrivedCallback, // 消息到达回调
    onDeliveryCompleteCallback // 发送完成回调
);
```

📌 **知识点：C++ 中使用 C 风格回调**

```cpp
// C 风格回调函数必须是静态方法或全局函数
static void onConnectionLostCallback(void* context, char* cause) {
    // context 就是我们传入的 this 指针
    auto* self = static_cast<Impl*>(context);

    // 现在可以访问成员变量和方法了
    self->logger_.warning("连接丢失: " + std::string(cause));
    self->notifyConnectionLost(std::string(cause));
}

// 为什么必须是 static？
// 1. 普通成员函数有隐式的 this 指针参数
// 2. C 回调函数签名固定，不接受 this
// 3. static 方法没有 this，签名兼容
```

### 2. 消息发布

```cpp
bool MqttClient::Impl::publish(const std::string& topic,
                               const std::string& message,
                               int qos, bool retained) {
    // 1. 检查连接状态
    if (!isConnected()) {
        logger_.error("MQTT未连接，无法发布消息");
        return false;
    }

    // 2. 配置消息结构
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = const_cast<char*>(message.c_str());
    pubmsg.payloadlen = static_cast<int>(message.length());
    pubmsg.qos = qos;
    pubmsg.retained = retained ? 1 : 0;

    // 3. 发布消息
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(pahoClient_, topic.c_str(),
                                       &pubmsg, &token);

    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("发布消息失败, 错误码: " + std::to_string(rc));
        return false;
    }

    // 4. 等待确认（QoS > 0）
    if (qos > 0) {
        rc = MQTTClient_waitForCompletion(pahoClient_, token, timeoutMs_);
        if (rc != MQTTCLIENT_SUCCESS) {
            logger_.warning("等待消息确认超时");
        }
    }

    return true;
}
```

### 3. 消息订阅与重订阅

```cpp
bool MqttClient::Impl::subscribe(const std::string& topic, int qos) {
    if (!isConnected()) {
        logger_.error("MQTT未连接，无法订阅");
        return false;
    }

    // 调用 paho 订阅
    int rc = MQTTClient_subscribe(pahoClient_, topic.c_str(), qos);

    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("订阅失败: " + topic);
        return false;
    }

    // ⭐ 保存订阅信息（用于重连后自动重订阅）
    {
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);

        // 检查是否已存在
        auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                              [&topic](const SubscriptionInfo& info) {
                                  return info.topic == topic;
                              });

        if (it != subscriptions_.end()) {
            it->qos = qos;  // 更新 QoS
        } else {
            subscriptions_.push_back({topic, qos});  // 新增
        }
    }

    return true;
}

// 重连后自动重订阅
void MqttClient::Impl::resubscribeAll() {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);

    for (const auto& sub : subscriptions_) {
        int rc = MQTTClient_subscribe(pahoClient_, sub.topic.c_str(), sub.qos);

        if (rc == MQTTCLIENT_SUCCESS) {
            logger_.info("重新订阅成功: " + sub.topic);
        } else {
            logger_.error("重新订阅失败: " + sub.topic);
        }
    }
}
```

📌 **知识点：std::find_if 与 Lambda**

```cpp
// std::find_if：按条件查找元素
auto it = std::find_if(
    subscriptions_.begin(),
    subscriptions_.end(),
    // Lambda 表达式：[捕获列表](参数列表) -> 返回类型 { 函数体 }
    [&topic](const SubscriptionInfo& info) {  // &topic：引用捕获
        return info.topic == topic;
    }
);

if (it != subscriptions_.end()) {
    // 找到了
    it->qos = newQos;
}
```

---

## 异步重连机制

### 为什么需要异步重连？

```
问题：同步重连会阻塞 paho 回调
┌─────────────────────────────────────────────────────────┐
│ paho 内部线程                                           │
│                                                         │
│   onConnectionLost() 被调用                             │
│         │                                               │
│         ▼                                               │
│   ❌ 调用 connect() 尝试重连                            │
│         │                                               │
│         ▼                                               │
│   阻塞等待连接结果...                                   │
│         │                                               │
│   ⚠️ paho 内部线程被阻塞，无法处理其他消息！            │
│   ⚠️ 可能导致死锁或超时！                               │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 解决方案：后台重连线程

```
正确做法：异步重连
┌─────────────────────────────────────────────────────────┐
│ paho 内部线程                │  重连线程               │
│                              │                         │
│ onConnectionLost()           │                         │
│       │                      │                         │
│       ▼                      │                         │
│ shouldReconnect_ = true      │                         │
│ reconnectSignal_.notify()    │                         │
│       │                      │                         │
│       ▼                      │                         │
│ 快速返回 ✅                  │    被唤醒 ◄────────────│
│                              │       │                 │
│                              │       ▼                 │
│                              │  执行 connect()         │
│                              │       │                 │
│                              │       ▼                 │
│                              │  重连成功/失败          │
│                              │                         │
└─────────────────────────────────────────────────────────┘
```

### 实现代码

```cpp
// 成员变量
std::thread reconnectThread_;              // 重连线程
std::condition_variable reconnectSignal_;  // 条件变量
std::mutex reconnectMutex_;                // 保护条件变量
std::atomic<bool> shouldReconnect_;        // 重连信号
bool threadRunning_;                       // 线程运行标志

// 启动重连线程（构造函数中调用）
void MqttClient::Impl::startReconnectThread() {
    threadRunning_ = true;
    reconnectThread_ = std::thread(&Impl::reconnectThreadFunc, this);
}

// 重连线程函数
void MqttClient::Impl::reconnectThreadFunc() {
    logger_.info("重连线程启动");

    while (threadRunning_) {
        std::unique_lock<std::mutex> lock(reconnectMutex_);

        // 等待重连信号（或每秒超时检查是否退出）
        reconnectSignal_.wait_for(lock, std::chrono::seconds(1), [this] {
            return shouldReconnect_ || !threadRunning_;
        });

        // 检查是否应该退出
        if (!threadRunning_) break;

        // 检查是否需要重连
        if (shouldReconnect_) {
            shouldReconnect_ = false;
            lock.unlock();  // 释放锁再执行重连

            attemptReconnect();  // 执行重连（可能耗时）
        }
    }

    logger_.info("重连线程退出");
}

// 连接丢失回调（paho 内部线程调用）
void MqttClient::Impl::onConnectionLostCallback(void* context, char* cause) {
    auto* self = static_cast<Impl*>(context);

    // 防止在析构时执行
    if (!self->connected_) return;

    self->connected_ = false;
    self->notifyConnectionLost(std::string(cause));

    // ⭐ 触发异步重连（不在回调中执行重连）
    if (self->reconnectEnabled_) {
        std::lock_guard<std::mutex> lock(self->reconnectMutex_);
        self->shouldReconnect_ = true;
        self->reconnectSignal_.notify_one();  // 唤醒重连线程
    }

    // 快速返回，不阻塞 paho 线程
}
```

📌 **知识点：std::condition_variable**

```cpp
// 条件变量：用于线程间的同步
std::condition_variable cv;
std::mutex mtx;
bool ready = false;

// 等待线程
void waitingThread() {
    std::unique_lock<std::mutex> lock(mtx);

    // 等待条件满足
    cv.wait(lock, [&] { return ready; });
    // 等价于：
    // while (!ready) {
    //     cv.wait(lock);  // 释放锁，阻塞等待
    // }

    // 条件满足，继续执行
    doWork();
}

// 通知线程
void notifyingThread() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();  // 唤醒一个等待的线程
    // cv.notify_all();  // 唤醒所有等待的线程
}
```

### 停止重连线程

```cpp
void MqttClient::Impl::stopReconnectThread() {
    if (!threadRunning_) return;

    {
        std::lock_guard<std::mutex> lock(reconnectMutex_);
        threadRunning_ = false;
        reconnectSignal_.notify_one();  // 唤醒线程让它退出
    }

    if (reconnectThread_.joinable()) {
        reconnectThread_.join();  // 等待线程退出
    }
}

// 析构函数中调用
MqttClient::Impl::~Impl() {
    stopReconnectThread();  // 先停止重连线程
    disconnect();           // 再断开连接

    if (pahoClient_ != nullptr) {
        MQTTClient_destroy(&pahoClient_);
    }
}
```

📌 **知识点：std::thread::join() vs detach()**

```cpp
std::thread t(func);

// join(): 等待线程结束，阻塞当前线程
t.join();  // 线程结束后才继续

// detach(): 分离线程，不等待
t.detach();  // 线程在后台运行，不能再join

// ⚠️ 必须 join 或 detach，否则析构时 terminate()
```

---

## 面试高频考点

### 📌 观察者模式

**Q1: 什么是观察者模式？项目中如何应用？**

**A1:**

- **定义**：定义对象间一对多的依赖关系，当一个对象状态改变时，所有依赖它的对象都收到通知
- **项目应用**：MqttClient 是主题（Subject），Application 是观察者（Observer）
- **实现要点**：
  1. 定义接口（IMqttMessageObserver）
  2. 维护观察者列表（vector）
  3. 提供注册/注销方法（addObserver/removeObserver）
  4. 状态变化时通知所有观察者

**Q2: 通知观察者时为什么要先拷贝列表？**

**A2:**

```cpp
// 避免迭代器失效
// 观察者可能在回调中调用 removeObserver()
// 导致 observers_ 被修改，迭代器失效
std::vector<Observer*> copy = observers_;  // 先拷贝
for (auto* obs : copy) {  // 迭代拷贝
    obs->onEvent();  // 即使原列表被修改也安全
}
```

### 📌 虚函数与多态

**Q3: 为什么接口类需要虚析构函数？**

**A3:**

```cpp
// 没有虚析构函数时：
Base* p = new Derived();
delete p;  // 只调用 Base 析构，Derived 资源泄漏！

// 有虚析构函数时：
delete p;  // 先调用 Derived 析构，再调用 Base 析构
```

**Q4: 纯虚函数和虚函数的区别？**

**A4:**
| 特性 | 纯虚函数 (`= 0`) | 虚函数 |
|------|-----------------|--------|
| 实现 | 无实现（或有实现也必须重写） | 有默认实现 |
| 派生类 | 必须重写 | 可选重写 |
| 类 | 使类成为抽象类 | 不影响 |

### 📌 C++ 与 C 互操作

**Q5: C++ 中如何使用 C 风格回调？**

**A5:**

```cpp
// C 回调函数签名固定：void (*callback)(void* context, ...)
// C++ 成员函数有隐式 this 指针，不兼容

// 解决方案：使用静态方法
static void onCallback(void* context, ...) {
    auto* self = static_cast<MyClass*>(context);  // context 传 this
    self->memberMethod();  // 调用成员方法
}

// 注册回调时传入 this
register_callback(onCallback, this);
```

### 📌 线程同步

**Q6: 什么是条件变量？如何使用？**

**A6:**

```cpp
// 条件变量用于线程间的条件同步
std::condition_variable cv;
std::mutex mtx;
bool ready = false;

// 等待方
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, [&] { return ready; });  // 阻塞直到 ready 为 true

// 通知方
{
    std::lock_guard<std::mutex> lock(mtx);
    ready = true;
}
cv.notify_one();  // 唤醒等待的线程
```

**Q7: 为什么条件变量要配合 mutex 使用？**

**A7:**

1. **保护条件变量**：避免虚假唤醒（spurious wakeup）
2. **保护共享数据**：条件判断涉及的数据需要同步
3. **原子操作**：wait() 会原子地释放锁并进入等待

### 📌 MQTT 协议

**Q8: MQTT 三种 QoS 的区别？**

**A8:**
| QoS | 名称 | 保证 | 性能 | 使用场景 |
|-----|------|------|------|----------|
| 0 | 最多一次 | 不保证送达 | 最高 | 心跳、遥测 |
| 1 | 至少一次 | 保证送达，可能重复 | 中等 | 命令、事件 |
| 2 | 恰好一次 | 保证送达且不重复 | 最低 | 支付、交易 |

---

## MqttHandler 详解（消息路由核心）⭐

### 为什么需要 MqttHandler？

**MqttClient** 只负责底层通信（连接、收发消息），不关心消息内容。

**MqttHandler** 负责：

1. 解析 JSON 消息格式
2. 根据 `method` 字段路由到对应处理函数
3. 调用 TaskManager 执行业务逻辑
4. 发送应答消息给平台

```
┌─────────────────────────────────────────────────────────────────┐
│                     职责分离（单一职责原则）                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   MqttClient          MqttHandler           TaskManager         │
│   ───────────         ───────────           ───────────         │
│   底层通信            消息路由              业务执行             │
│   • 连接管理          • JSON 解析           • 创建任务           │
│   • 消息收发          • 指令分发            • 启动检测           │
│   • 重连机制          • 应答发送            • 事件上报           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### MqttHandler 初始化流程

```cpp
// MqttHandler::initialize()
bool MqttHandler::initialize() {
    // 1. 从配置读取设备序列号
    deviceSn_ = config_.getString("device.analysis_sn", "analysis_device_WRSE7001");

    // 2. 构建 Topic 名称
    servicesTopicSub_ = "thing/product/" + deviceSn_ + "/services";      // 订阅：接收指令
    servicesTopicPub_ = "thing/product/" + deviceSn_ + "/services_reply"; // 发布：应答
    eventsTopicPub_   = "thing/product/" + deviceSn_ + "/events";         // 发布：事件上报

    // 3. ⭐ 初始化指令路由表
    initCommandHandlers();

    return true;
}
```

### 指令路由表（策略模式的变体）

```cpp
// 路由表定义
std::map<std::string, std::function<void(const nlohmann::json&)>> commandHandlers_;

void MqttHandler::initCommandHandlers() {
    // 使用 lambda 表达式注册处理函数

    // 算法启用（启动任务）
    commandHandlers_["device_algorithm_enable"] =
        [this](const nlohmann::json& data) {
            handleAlgorithmEnable(data);
        };

    // 算法停用（停止任务）
    commandHandlers_["device_algorithm_disable"] =
        [this](const nlohmann::json& data) {
            handleAlgorithmDisable(data);
        };

    // 任务结束通知
    commandHandlers_["device_task_end"] =
        [this](const nlohmann::json& data) {
            handleTaskEnd(data);
        };

    // 设备重启
    commandHandlers_["device_reboot"] =
        [this](const nlohmann::json& data) {
            handleReboot(data);
        };
}
```

📌 **知识点：std::map + std::function 实现命令路由**

这是**策略模式**的一种变体实现：

| 传统策略模式         | 本项目实现                        |
| -------------------- | --------------------------------- |
| 定义抽象策略接口     | 使用 `std::function<void(json&)>` |
| 每个具体策略是一个类 | 每个具体策略是一个 lambda/函数    |
| 需要继承和多态       | 无需继承，更轻量                  |
| 运行时切换策略对象   | 通过 map 查找对应函数             |

**优点**：

1. **开闭原则**：添加新指令只需向 map 添加一项，无需修改现有代码
2. **解耦**：路由逻辑与处理逻辑分离
3. **可测试**：可以单独测试每个处理函数

### 消息处理流程

```cpp
// IMqttMessageObserver 接口实现
void MqttHandler::onMessageReceived(const std::string& topic,
                                    const std::string& message) {
    try {
        // 1. 解析 JSON
        nlohmann::json json = nlohmann::json::parse(message);

        // 2. 根据 topic 路由
        if (topic == servicesTopicSub_) {
            handleServiceCommand(json);
        }
    } catch (const nlohmann::json::exception& e) {
        logger_.error("JSON 解析失败: " + std::string(e.what()));
    }
}

void MqttHandler::handleServiceCommand(const nlohmann::json& json) {
    // 1. 提取 method 字段
    std::string method = json.at("method").get<std::string>();

    // 2. 提取 data 字段
    nlohmann::json data = json.at("data");

    // 3. ⭐ 查找路由表
    auto it = commandHandlers_.find(method);
    if (it != commandHandlers_.end()) {
        // 找到了，调用对应处理函数
        it->second(data);
    } else {
        logger_.warning("未知的 method: " + method);
        sendServiceReply(method, 255, 0);  // 255 = 未知错误
    }
}
```

### 具体指令处理示例

#### device_algorithm_enable（启动任务）

```cpp
void MqttHandler::handleAlgorithmEnable(const nlohmann::json& data) {
    logger_.info("处理启用算法指令...");

    try {
        // 1. 解析为 TaskConfig
        task::TaskConfig config = task::TaskConfig::fromJson(data);

        // 2. 验证配置
        if (!config.isValid()) {
            sendServiceReply("device_algorithm_enable", 1, config.taskId);
            return;
        }

        // 3. ⭐ 调用 TaskManager 启动任务
        bool success = taskManager_.startTask(config);

        // 4. 发送应答
        if (success) {
            sendServiceReply("device_algorithm_enable", 0, config.taskId);
        } else {
            sendServiceReply("device_algorithm_enable", 3, config.taskId);
        }
    } catch (const std::exception& e) {
        logger_.error("处理异常: " + std::string(e.what()));
        sendServiceReply("device_algorithm_enable", 255, 0);
    }
}
```

#### device_task_end（任务结束）

```cpp
void MqttHandler::handleTaskEnd(const nlohmann::json& data) {
    int taskId = data.at("taskID").get<int>();
    std::string taskIdStr = std::to_string(taskId);

    // 1. 先发送应答（快速响应）
    sendServiceReply("device_task_end", 0, taskId);

    // 2. 获取任务
    auto task = taskManager_.getTask(taskIdStr);
    if (!task) {
        logger_.warning("任务不存在: " + taskIdStr);
        return;
    }

    // 3. ⭐ 多态调用 onTaskEnd()
    //    不同任务类型有不同的结束行为：
    //    - LiveStreamTask: 立即停止
    //    - MediaFileTask: 仅标记结束，等待文件传输完成
    task->onTaskEnd();
}
```

### 应答消息发送

```cpp
void MqttHandler::sendServiceReply(const std::string& method,
                                   int result,
                                   int taskId) {
    // 1. 构建应答 JSON
    nlohmann::json reply;
    reply["result"] = result;     // 0=成功, 1=参数错误, 2=任务不存在, 3=资源不足
    reply["method"] = method;
    reply["taskID"] = taskId;

    // 2. 失败时添加错误描述
    if (result != 0) {
        switch (result) {
            case 1: reply["message"] = "参数错误"; break;
            case 2: reply["message"] = "任务不存在"; break;
            case 3: reply["message"] = "资源不足"; break;
            default: reply["message"] = "未知错误"; break;
        }
    }

    // 3. 发布到 MQTT
    std::string payload = reply.dump();
    mqttClient_.publish(servicesTopicPub_, payload, 0);  // QoS=0
}
```

### MqttHandler 面试要点

**Q1: MqttHandler 和 MqttClient 的职责区别？**

**A1:**
| 类 | 职责层次 | 关注点 |
|----|---------|--------|
| MqttClient | 通信层 | 连接、收发、重连 |
| MqttHandler | 业务层 | 解析、路由、调用业务 |

**Q2: 为什么用 map + function 而不是 switch-case？**

**A2:**

```cpp
// ❌ switch-case 的问题
switch (method) {
    case "device_algorithm_enable": handleAlgorithmEnable(); break;
    case "device_algorithm_disable": handleAlgorithmDisable(); break;
    // 每次新增指令都要修改这里...
}

// ✅ map + function 的优势
commandHandlers_["new_command"] = [this](auto& data) { ... };
// 只需添加一行，无需修改已有代码（开闭原则）
```

**Q3: 如何保证消息处理的线程安全？**

**A3:**

- `onMessageReceived()` 可能在 MQTT 内部线程调用
- 如果处理函数耗时，应该将消息放入队列，由工作线程处理
- 本项目中 TaskManager 内部有 mutex 保护

---

## 实战练习

### 练习 1：实现消息过滤

**需求**：在 MqttClient 中添加消息过滤功能，只有匹配的主题才通知观察者

```cpp
// 提示：使用主题匹配函数
bool matchTopic(const std::string& pattern, const std::string& topic);
```

### 练习 2：添加连接状态监控

**需求**：添加心跳机制，定期检查连接状态，超时则触发重连

```cpp
// 提示：使用定时器或后台线程
// 每 30 秒检查一次 isConnected()
```

### 练习 3：实现消息队列

**需求**：当连接断开时，将待发送的消息缓存到队列，重连后自动发送

```cpp
// 提示：使用 std::queue<Message>
// publish() 时检查连接状态，断开则入队
// onConnected() 回调中处理队列
```

---

## 总结

本节我们深入学习了 MQTT 模块的设计与实现：

| 技术点          | 内容                                |
| --------------- | ----------------------------------- |
| **MQTT 协议**   | 发布/订阅模型、QoS 等级、主题通配符 |
| **观察者模式**  | 接口定义、注册通知、迭代器安全      |
| **C++ 回调**    | 静态方法、context 传递 this         |
| **异步重连**    | 条件变量、后台线程、线程安全        |
| **paho.mqtt.c** | C 库封装、错误处理                  |

**核心收获**：

1. **观察者模式**：解耦消息生产者和消费者
2. **异步编程**：条件变量是线程同步的重要工具
3. **回调安全**：拷贝列表避免迭代器失效
4. **接口设计**：纯虚函数强制实现，虚函数提供默认行为

**下一节预告**：[05-Vision 模块详解](./05-Vision模块详解.md) - 学习工厂模式和策略模式在视觉处理中的应用

---

_创建日期：2025-10-26_
_适用版本：ESDK_On_Sophon v1.0_
