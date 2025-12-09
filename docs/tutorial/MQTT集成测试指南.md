# MQTT 集成测试指南

**日期**: 2025 年 10 月 30 日  
**目的**: 测试 MQTT 指令接收和任务管理功能

---

## 📋 测试准备

### 1. MQTT 工具推荐

**方式 1: MQTTX (推荐，图形界面)**

- 下载: https://mqttx.app/
- 跨平台：Windows/Mac/Linux

**方式 2: mosquitto_pub (命令行)**

```bash
# Ubuntu/Debian
sudo apt-get install mosquitto-clients

# 使用示例
mosquitto_pub -h jiaoyujidi.work -p 1883 \
  -t "thing/product/analysis_device_WRSE7001/services" \
  -m '{"method":"device_algorithm_sync","data":{...}}'
```

**方式 3: Python paho-mqtt**

```python
import paho.mqtt.client as mqtt
import json

client = mqtt.Client()
client.connect("jiaoyujidi.work", 1883)

message = {
    "method": "device_algorithm_sync",
    "data": {...}
}
client.publish("thing/product/analysis_device_WRSE7001/services",
               json.dumps(message))
```

---

## 🎯 测试用例

### 测试 1: 启动任务 (device_algorithm_sync)

#### MQTT 消息

```json
{
  "method": "device_algorithm_sync",
  "data": {
    "taskID": 1001,
    "algorithmName": "yolov10_detection",
    "enabled": true,
    "confidence": 0.5,
    "nmsThreshold": 0.45,
    "targetClasses": ["person", "car", "bicycle"],
    "detectionInterval": 30,
    "maxDetectionCount": 100
  }
}
```

#### 发送到 Topic

```
thing/product/analysis_device_WRSE7001/services
```

#### 期望响应 (在 services_reply Topic)

```json
{
  "result": 0,
  "method": "device_algorithm_sync",
  "taskID": 1001
}
```

- `result: 0` - 成功
- `result: 1` - 参数错误
- `result: 3` - 资源不足（已有 3 个任务在运行）

#### 期望日志

```
[INFO] 处理服务调用: method=device_algorithm_sync
[INFO] 处理算法同步指令...
[DEBUG] 解析TaskConfig: taskID=1001 algorithmName=yolov10_detection
[INFO] 任务启动成功: taskID=1001
[DEBUG] 发送应答成功: method=device_algorithm_sync result=0 taskID=1001
```

---

### 测试 2: 停止任务 (device_algorithm_disable)

#### MQTT 消息

```json
{
  "method": "device_algorithm_disable",
  "data": {
    "taskID": 1001
  }
}
```

#### 期望响应

```json
{
  "result": 0,
  "method": "device_algorithm_disable",
  "taskID": 1001
}
```

- `result: 0` - 成功
- `result: 2` - 任务不存在

#### 期望日志

```
[INFO] 处理服务调用: method=device_algorithm_disable
[INFO] 处理关闭算法指令...
[DEBUG] 关闭任务: taskID=1001
[INFO] 任务停止成功: taskID=1001
```

---

### 测试 3: 兼容格式测试

#### 格式 1: 使用 params 字段

```json
{
  "method": "device_algorithm_sync",
  "params": {
    "taskID": 1002,
    "algorithmName": "yolov10_detection"
  }
}
```

#### 格式 2: 扁平化格式（直接放在根节点）

```json
{
  "method": "device_algorithm_sync",
  "taskID": 1003,
  "algorithmName": "yolov10_detection",
  "enabled": true
}
```

**注意**: 修改后的代码支持这 3 种格式！

---

## 🔧 使用 MQTTX 测试步骤

### 1. 连接到 Broker

1. 打开 MQTTX
2. 点击 "New Connection"
3. 填写连接信息：
   - **Name**: ESDK Test
   - **Host**: mqtt://jiaoyujidi.work
   - **Port**: 1883
   - **Client ID**: test_client_001 (随意)
4. 点击 "Connect"

### 2. 订阅应答 Topic

1. 在 "Subscriptions" 中点击 "+"
2. 填写 Topic: `thing/product/analysis_device_WRSE7001/services_reply`
3. QoS: 1
4. 点击 "Subscribe"

### 3. 发送测试消息

1. 在消息输入框中输入 JSON
2. Topic: `thing/product/analysis_device_WRSE7001/services`
3. QoS: 1
4. 点击 "Send"

### 4. 观察结果

- **MQTTX 界面**: 查看 `services_reply` 的应答消息
- **SE7 终端**: 观察 `esdk_sophon.log` 或控制台输出

---

## 🐛 问题排查

### 问题 1: "服务调用缺少 data 字段"

**原因**: JSON 格式不正确

**检查点**:

1. 是否包含 `method` 字段？
2. 是否包含 `data`/`params` 字段？
3. JSON 格式是否正确（使用 https://jsonlint.com/ 验证）

**修复后支持**:

- ✅ 标准格式: `{"method": "...", "data": {...}}`
- ✅ 兼容格式: `{"method": "...", "params": {...}}`
- ✅ 扁平格式: `{"method": "...", "taskID": 123, ...}`

### 问题 2: "等待消息确认超时"

**原因**: QoS=1 时需要确认，可能网络慢或 broker 繁忙

**影响**: 不影响功能，只是 warning 日志

**解决**:

- 检查网络延迟
- 可以改用 QoS=0（不保证送达，但更快）

### 问题 3: 任务启动失败

**检查日志中的错误信息**:

```bash
# 实时查看日志
tail -f esdk_sophon.log | grep -E "(ERROR|WARN|taskID)"
```

**常见错误码**:

- `result: 1` - 参数错误（检查 JSON 格式）
- `result: 2` - 任务不存在（停止不存在的任务）
- `result: 3` - 资源不足（超过最大并发数 3）

---

## 📊 完整测试流程

### 1. 启动程序

```bash
# 在SE7设备上
cd /data/Edge-SDK/build/bin
./ESDK_Sophon
```

### 2. 发送启动任务命令

```json
{
  "method": "device_algorithm_sync",
  "data": {
    "taskID": 1001,
    "algorithmName": "yolov10_detection",
    "enabled": true
  }
}
```

### 3. 验证任务运行

- 查看日志: `[INFO] 任务启动成功: taskID=1001`
- 查看应答: `{"result":0,"method":"device_algorithm_sync","taskID":1001}`

### 4. 发送停止任务命令

```json
{
  "method": "device_algorithm_disable",
  "data": {
    "taskID": 1001
  }
}
```

### 5. 验证任务停止

- 查看日志: `[INFO] 任务停止成功: taskID=1001`
- 查看应答: `{"result":0,"method":"device_algorithm_disable","taskID":1001}`

### 6. 测试并发（可选）

连续发送 3 个任务启动命令（taskID: 1001, 1002, 1003）

- 前 3 个应该成功
- 第 4 个应该失败（资源不足）

---

## 📝 Python 测试脚本

```python
#!/usr/bin/env python3
"""
ESDK Sophon MQTT测试脚本
"""
import paho.mqtt.client as mqtt
import json
import time

# 配置
BROKER = "jiaoyujidi.work"
PORT = 1883
DEVICE_SN = "analysis_device_WRSE7001"

TOPIC_CMD = f"thing/product/{DEVICE_SN}/services"
TOPIC_REPLY = f"thing/product/{DEVICE_SN}/services_reply"

def on_connect(client, userdata, flags, rc):
    print(f"连接成功: {rc}")
    # 订阅应答topic
    client.subscribe(TOPIC_REPLY, qos=1)
    print(f"已订阅: {TOPIC_REPLY}")

def on_message(client, userdata, msg):
    print(f"\n收到应答:")
    print(f"  Topic: {msg.topic}")
    print(f"  Payload: {msg.payload.decode()}")

    try:
        data = json.loads(msg.payload.decode())
        result = data.get("result", -1)
        method = data.get("method", "unknown")
        task_id = data.get("taskID", 0)

        if result == 0:
            print(f"  ✅ 成功: {method}, taskID={task_id}")
        else:
            print(f"  ❌ 失败: {method}, result={result}, taskID={task_id}")
            if "message" in data:
                print(f"     错误信息: {data['message']}")
    except:
        pass

def send_start_task(client, task_id):
    """发送启动任务命令"""
    message = {
        "method": "device_algorithm_sync",
        "data": {
            "taskID": task_id,
            "algorithmName": "yolov10_detection",
            "enabled": True,
            "confidence": 0.5,
            "nmsThreshold": 0.45,
            "targetClasses": ["person", "car"],
            "detectionInterval": 30,
            "maxDetectionCount": 100
        }
    }

    payload = json.dumps(message)
    print(f"\n发送启动任务: taskID={task_id}")
    print(f"  Payload: {payload[:100]}...")

    client.publish(TOPIC_CMD, payload, qos=1)

def send_stop_task(client, task_id):
    """发送停止任务命令"""
    message = {
        "method": "device_algorithm_disable",
        "data": {
            "taskID": task_id
        }
    }

    payload = json.dumps(message)
    print(f"\n发送停止任务: taskID={task_id}")
    print(f"  Payload: {payload}")

    client.publish(TOPIC_CMD, payload, qos=1)

def main():
    # 创建客户端
    client = mqtt.Client(client_id="esdk_test_client")
    client.on_connect = on_connect
    client.on_message = on_message

    # 连接broker
    print(f"连接到 {BROKER}:{PORT}...")
    client.connect(BROKER, PORT, 60)

    # 开始循环
    client.loop_start()

    try:
        # 测试1: 启动任务
        time.sleep(2)
        send_start_task(client, 1001)

        # 等待5秒
        time.sleep(5)

        # 测试2: 停止任务
        send_stop_task(client, 1001)

        # 等待应答
        time.sleep(3)

        print("\n测试完成！")

    except KeyboardInterrupt:
        print("\n中断测试")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    main()
```

**使用方法**:

```bash
# 安装依赖
pip3 install paho-mqtt

# 运行脚本
python3 mqtt_test.py
```

---

## ✅ 成功标志

1. **启动任务**:

   - ✅ 收到应答: `result: 0`
   - ✅ 日志显示: `任务启动成功`
   - ✅ TaskManager 中有运行的任务

2. **停止任务**:

   - ✅ 收到应答: `result: 0`
   - ✅ 日志显示: `任务停止成功`
   - ✅ TaskManager 中任务已移除

3. **日志清晰**:
   - ✅ 能看到完整的消息内容
   - ✅ 能追踪处理流程
   - ✅ 错误信息详细

---

**编写时间**: 2025 年 10 月 30 日  
**作者**: GitHub Copilot  
**状态**: 待测试
