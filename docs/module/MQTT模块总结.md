# MQTT 模块实现总结

> 📅 **完成日期**: 2025-10-27  
> 📊 **代码规模**: 1,550+ 行 (含注释)  
> ✅ **状态**: 编译通过,等待设备测试

---

## 📋 模块概述

基于 **paho.mqtt.c** 实现的 MQTT 通信模块,用于与云端服务器(`jiaoyujidi.work:1883`)进行消息交互。

### 核心特性

- ✅ **观察者模式** - 解耦消息生产者和消费者
- ✅ **Pimpl 模式** - 隐藏 paho 实现细节
- ✅ **单例模式** - 全局唯一客户端
- ✅ **配置驱动** - 从 config.json 读取所有参数
- ✅ **自动重连** - 断线后指数退避重试
- ✅ **线程安全** - 观察者列表用 mutex 保护
- ✅ **降级方案** - 配置失败使用硬编码默认值

---

## 📁 文件清单

| 文件路径                                          | 行数      | 说明                         |
| ------------------------------------------------- | --------- | ---------------------------- |
| `include/esdk_sophon/mqtt/IMqttMessageObserver.h` | 115       | 观察者接口定义               |
| `include/esdk_sophon/mqtt/MqttClient.h`           | 223       | MQTT 客户端公共接口          |
| `src/mqtt/MqttClient.cpp`                         | 718       | MQTT 客户端实现(Pimpl)       |
| `src/mqtt/CMakeLists.txt`                         | 182       | CMake 配置(含 90 行教学注释) |
| `tests/test_mqtt.cpp`                             | 432       | 集成测试(6 个测试用例)       |
| `docs/CMake高级特性图解.md`                       | 655       | CMake 教学文档               |
| **合计**                                          | **2,325** | **代码+文档**                |

---

## 🏗️ 架构设计

### 类图

```
┌────────────────────────────────────────┐
│      IMqttMessageObserver              │
│  ──────────────────────────────────    │
│  + onMessageReceived(topic, msg)       │
│  + onConnectionLost(cause)             │
│  + onConnected(serverUri)              │
│  + onDeliveryComplete(token)           │
└────────────────────────────────────────┘
                ▲
                │ implements
                │
   ┌────────────┴────────────┐
   │                         │
┌──────────┐          ┌──────────┐
│ Vision   │          │  Device  │
│ Module   │          │ Manager  │
└──────────┘          └──────────┘


┌────────────────────────────────────────┐
│          MqttClient                    │
│  ──────────────────────────────────    │
│  - pImpl_: unique_ptr<Impl>            │
│  ──────────────────────────────────    │
│  + getInstance(): MqttClient&          │
│  + initialize(): bool                  │
│  + connect(): bool                     │
│  + disconnect(): void                  │
│  + publish(topic, msg, qos): bool      │
│  + subscribe(topic, qos): bool         │
│  + addObserver(observer): void         │
│  + removeObserver(observer): void      │
└────────────────────────────────────────┘
                │
                │ uses
                ▼
┌────────────────────────────────────────┐
│       MqttClient::Impl                 │
│  ──────────────────────────────────    │
│  - pahoClient_: MQTTClient (C句柄)     │
│  - observers_: vector<Observer*>       │
│  - observersMutex_: mutex              │
│  - logger_: Logger&                    │
│  - config_: Config&                    │
│  ──────────────────────────────────    │
│  + static onConnectionLostCallback()   │
│  + static onMessageArrivedCallback()   │
│  - notifyMessageReceived()             │
│  - attemptReconnect()                  │
│  - loadConfig()                        │
└────────────────────────────────────────┘
```

---

## 💡 核心技术点

### 1. 观察者模式实现

```cpp
// 观察者接口
class IMqttMessageObserver {
public:
    virtual void onMessageReceived(const std::string& topic,
                                   const std::string& message) = 0;
    virtual void onConnectionLost(const std::string& cause) = 0;
};

// MqttClient管理观察者列表
void MqttClient::Impl::notifyMessageReceived(const std::string& topic,
                                             const std::string& message) {
    std::lock_guard<std::mutex> lock(observersMutex_);  // 线程安全
    for (auto* observer : observers_) {
        try {
            observer->onMessageReceived(topic, message);
        } catch (const std::exception& e) {
            logger_.error("观察者异常: " + std::string(e.what()));
        }
    }
}
```

**要点:**

- 使用裸指针管理观察者(生命周期由外部控制)
- 异常隔离(一个观察者异常不影响其他)
- 线程安全(mutex 保护观察者列表)

### 2. Callback Bridge 模式

paho.mqtt.c 是 C 库,使用函数指针回调。我们用静态函数桥接到 C++成员函数:

```cpp
class MqttClient::Impl {
private:
    // 静态C回调(入口)
    static int onMessageArrivedCallback(void* context, char* topicName,
                                        int topicLen, MQTTClient_message* message) {
        auto* self = static_cast<Impl*>(context);  // 恢复this

        std::string topic(topicName, topicLen);
        std::string msg(static_cast<char*>(message->payload),
                       message->payloadlen);

        self->notifyMessageReceived(topic, msg);  // 调用C++方法

        MQTTClient_freeMessage(&message);  // 释放paho内存
        MQTTClient_free(topicName);
        return 1;
    }

public:
    bool initialize() {
        MQTTClient_create(&pahoClient_, ...);

        // 传递this作为context
        MQTTClient_setCallbacks(pahoClient_, this,  // ← this指针
                               onConnectionLostCallback,
                               onMessageArrivedCallback,
                               onDeliveryCompleteCallback);
    }
};
```

### 3. 自动重连机制

```cpp
void MqttClient::Impl::attemptReconnect() {
    if (currentRetries_ >= maxRetries_) {
        logger_.error("重连次数已达上限");
        return;
    }

    currentRetries_++;
    std::this_thread::sleep_for(std::chrono::milliseconds(retryIntervalMs_));

    if (connect()) {
        currentRetries_ = 0;  // 重置
    } else if (reconnectEnabled_ && currentRetries_ < maxRetries_) {
        attemptReconnect();  // 递归重试
    }
}
```

**配置:**

```json
{
  "mqtt": {
    "reconnect": {
      "enabled": true,
      "max_retries": 5,
      "retry_interval_ms": 3000
    }
  }
}
```

### 4. CMake 现代化配置

```cmake
# src/mqtt/CMakeLists.txt

# 创建静态库
add_library(mqtt STATIC MqttClient.cpp)
add_library(esdk_sophon::mqtt ALIAS mqtt)  # ALIAS命名空间

# PUBLIC vs PRIVATE链接
target_link_libraries(mqtt
    PUBLIC
        esdk_sophon::core      # MqttClient.h暴露Logger,需PUBLIC传递
    PRIVATE
        ThirdParty::PahoMqttC  # paho被Pimpl隐藏,用PRIVATE
)
```

**面试考点:**

- Q: 为什么 core 用 PUBLIC,paho 用 PRIVATE?
- A: MqttClient.h 包含 Logger.h(来自 core),链接 mqtt 的目标也需要 core → PUBLIC。paho 只在.cpp 中使用,被 Pimpl 隐藏 → PRIVATE。

---

## 📡 Topic 规则(基于接口文档)

### 完整 Topic 列表

| 方向    | Topic                                      | 用途          |
| ------- | ------------------------------------------ | ------------- |
| ⬆️ 上行 | `drone/{device_sn}/info/event`             | 推送检测结果  |
| ⬇️ 下行 | `thing/product/{device_sn}/services`       | 接收云端指令  |
| ⬆️ 上行 | `thing/product/{device_sn}/services_reply` | 响应云端指令  |
| ⬆️ 上行 | `thing/product/{device_sn}/events`         | 上报进度/状态 |

其中 `{device_sn}` = `analysis_device_WRSE7001` (来自`config.json`)

### 云端指令(services)

```json
{
  "method": "device_algorithm_sync", // 算法同步
  "method": "device_algorithm_enable", // 启用算法
  "method": "device_algorithm_disable", // 关闭算法
  "method": "device_task_end", // 任务结束
  "method": "device_reboot" // 设备重启
}
```

### 设备应答(services_reply)

```json
{
  "result": 0, // 0=成功, 1=错误, 255=未知错误
  "method": "device_algorithm_sync",
  "taskID": 1234
}
```

---

## 🧪 测试程序

### 6 个测试用例

```cpp
// test_mqtt.cpp - 432行

1. test1_InitializeAndConfig()
   - 加载config.json
   - 验证MQTT配置读取正确

2. test2_Connect()
   - 连接到jiaoyujidi.work:1883
   - 验证连接成功

3. test3_Publish()
   - 发布QoS 0/1/2消息
   - 验证不同QoS级别

4. test4_Subscribe()
   - 订阅精确主题 test/mqtt/publish
   - 订阅通配符 test/mqtt/+ 和 test/mqtt/#
   - 验证订阅成功

5. test5_ObserverPattern()
   - 添加3个观察者
   - 发布消息,验证所有观察者接收
   - 移除观察者2
   - 再次发布,验证观察者2不再接收

6. test6_Disconnect()
   - 断开连接
   - 验证状态为未连接
```

### 编译输出

```bash
[ 97%] Linking CXX executable ../bin/tests/test_mqtt
[ 97%] Built target test_mqtt

-rwxr-xr-x 1 root root 234K Oct 27 01:03 test_mqtt
test_mqtt: ELF 64-bit LSB shared object, ARM aarch64
```

✅ 编译成功: 234KB ARM64 可执行文件

---

## 🐛 遇到的问题

### 问题 1: Logger API 命名

**症状**: `logger_.logInfo()` 报错无此方法

**原因**: Logger 实际 API 是 `logger_.info()`

**解决**: 批量替换 40+处调用

### 问题 2: 观察者接口签名

**症状**: `onConnected()` override 失败

**原因**: 接口定义要求参数 `const std::string& serverUri`

**解决**: 修正 test_mqtt.cpp 中 TestObserver 实现

### 问题 3: shared_ptr vs 裸指针

**症状**: `addObserver(observer1)` 类型不匹配

**原因**: 函数接受裸指针,传入了 shared_ptr

**解决**: 使用 `observer1.get()` 获取裸指针

---

## 📚 知识点索引

### 设计模式

- [x] 观察者模式 - 解耦消息生产/消费
- [x] Pimpl 模式 - 隐藏实现细节
- [x] 单例模式 - 全局唯一实例
- [x] Callback Bridge - C→C++桥接

### CMake

- [x] IMPORTED 库 - 第三方库集成
- [x] PUBLIC/PRIVATE 链接 - 依赖传递控制
- [x] ALIAS 目标 - 命名空间管理
- [x] Generator Expressions - BUILD_INTERFACE/INSTALL_INTERFACE

### MQTT 协议

- [x] QoS 等级 - 0/1/2 消息可靠性
- [x] Topic 通配符 - +和#的使用
- [x] 持久化会话 - cleansession 标志

### C++技术

- [x] unique_ptr - RAII 资源管理
- [x] mutex - 线程安全
- [x] static 局部变量 - 线程安全单例
- [x] 异常安全 - try-catch 隔离

---

## ✅ 完成度

- ✅ 架构设计
- ✅ 接口定义
- ✅ 核心实现
- ✅ CMake 配置
- ✅ 测试程序
- ✅ 编译通过
- ⏳ 设备测试(等待 SE7 恢复)
- ⏳ 文档补充(改进记录.md、八股知识点.md)

---

**下一步**: 开发 Device 模块(基于 Edge-SDK)
