# Segmentation Fault 调试指南 - 详细日志版本

**日期**: 2025 年 10 月 30 日  
**版本**: v2.0 - 增强日志版  
**目的**: 通过详细日志定位 Segmentation Fault 的精确位置

---

## 📋 问题现状

### 现象

```
[2025-10-30 17:57:11] [INFO ] 成功连接到MQTT代理: tcp://jiaoyujidi.work:1883
[2025-10-30 17:57:11] [INFO ] MQTT重连成功
Segmentation fault (core dumped)  ← 💥 重连成功后立即崩溃
```

**关键特征**:

- ✅ 任务启动成功（taskID=3889）
- ✅ 连接丢失检测正常
- ✅ 重连逻辑执行成功
- ❌ 重连成功后立即崩溃
- ❌ 没有异常日志，直接段错误

---

## 🔍 增强日志策略

### 已添加的详细日志

#### 1. MqttClient 观察者通知日志

**notifyConnected()** - 重点监控对象：

```cpp
logger_.debug("🔔 notifyConnected 开始，观察者数量: " + std::to_string(observers_.size()));
logger_.info("🎯 准备通知观察者: MQTT连接成功");

// 拷贝观察者列表
logger_.debug("✅ 观察者列表已拷贝，开始通知...");

// 对每个观察者：
logger_.info("📍 通知观察者 #" + std::to_string(index) + " 地址=" +
             std::to_string(reinterpret_cast<uintptr_t>(observer)));

logger_.info("➡️ 即将调用观察者 #" + std::to_string(index) + "->onConnected()");
observer->onConnected(serverUri);  // ← 这里可能崩溃
logger_.info("✅ 观察者 #" + std::to_string(index) + " 处理完成");

logger_.info("🏁 notifyConnected 完成，已通知 " + std::to_string(index) + " 个观察者");
```

**其他通知函数** (notifyConnectionLost, notifyMessageReceived, notifyDeliveryComplete):

- 类似的详细日志
- 每个观察者调用前后都有日志
- 打印观察者内存地址

#### 2. MqttHandler 回调日志

**onConnectionLost()**:

```cpp
logger_.warning("🔴 MqttHandler::onConnectionLost() 被调用");
// ... 处理逻辑 ...
logger_.debug("🔴 MqttHandler::onConnectionLost() 执行完毕");
```

**onConnected()** - 新增实现：

```cpp
logger_.info("🟢 MqttHandler::onConnected() 被调用");
logger_.info("MQTT连接成功: " + serverUri);
logger_.debug("🟢 MqttHandler::onConnected() 执行完毕");
```

**onDeliveryComplete()** - 新增实现：

```cpp
logger_.debug("🟡 MqttHandler::onDeliveryComplete() 被调用，token=" + std::to_string(token));
logger_.debug("🟡 MqttHandler::onDeliveryComplete() 执行完毕");
```

---

## 🧪 测试步骤

### 1. 部署新版本到 SE7

```bash
# 在宿主机（F:\CV\DJI\ESDK_On_Sophon\ESDK_On_Sophon）
# 可执行文件会自动同步到容器的 /workspace/build/bin/ESDK_Sophon

# SSH到SE7设备
ssh linaro@192.168.150.1

# 停止旧进程（如果还在运行）
pkill -9 ESDK_Sophon

# 拷贝新版本
cd /data/Edge-SDK/build/bin
cp /mnt/shared/ESDK_Sophon ./ESDK_Sophon
chmod +x ESDK_Sophon
```

### 2. 运行并等待重连

```bash
cd /data/Edge-SDK/build/bin
./ESDK_Sophon

# 等待自然连接丢失（网络波动）
# 或者手动触发：临时断网、重启MQTT代理等
```

### 3. 观察日志输出

**关键日志序列**（如果一切正常）:

```
[INFO ] MQTT连接丢失: Unknown
[INFO ] 尝试第 1/5 次重连...
[INFO ] 正在连接到MQTT代理...
[INFO ] 成功连接到MQTT代理: tcp://jiaoyujidi.work:1883

🔔 notifyConnected 开始，观察者数量: 1          ← 应该看到这行
🎯 准备通知观察者: MQTT连接成功                  ← 应该看到这行
✅ 观察者列表已拷贝，开始通知...                 ← 应该看到这行
📍 通知观察者 #0 地址=0x7f8c400010               ← 观察者地址
➡️ 即将调用观察者 #0->onConnected()            ← 准备调用
🟢 MqttHandler::onConnected() 被调用             ← 进入回调
🟢 MqttHandler::onConnected() 执行完毕           ← 回调完成
✅ 观察者 #0 处理完成                            ← 通知完成
🏁 notifyConnected 完成，已通知 1 个观察者       ← 全部完成
```

**如果崩溃，最后一条日志会告诉我们崩溃位置**:

#### 场景 A: 拷贝前崩溃

```
🔔 notifyConnected 开始，观察者数量: 1
Segmentation fault (core dumped)
```

**分析**: observers\_ 本身已损坏，访问 size() 就崩溃

#### 场景 B: 拷贝时崩溃

```
🔔 notifyConnected 开始，观察者数量: 1
✅ 观察者列表已拷贝，开始通知...
Segmentation fault (core dumped)
```

**分析**: 拷贝 vector 时访问了无效内存

#### 场景 C: 调用前崩溃

```
📍 通知观察者 #0 地址=0x7f8c400010
➡️ 即将调用观察者 #0->onConnected()
Segmentation fault (core dumped)
```

**分析**: observer 指针本身有效，但 onConnected()虚函数表损坏

#### 场景 D: 调用中崩溃

```
➡️ 即将调用观察者 #0->onConnected()
🟢 MqttHandler::onConnected() 被调用
Segmentation fault (core dumped)
```

**分析**: 进入回调函数后，访问 MqttHandler 成员变量时崩溃

#### 场景 E: 回调完成后崩溃

```
🟢 MqttHandler::onConnected() 执行完毕
✅ 观察者 #0 处理完成
Segmentation fault (core dumped)
```

**分析**: 回调返回后，继续处理时崩溃（可能是异常处理问题）

---

## 📊 日志分析表

| 最后一条日志             | 崩溃位置           | 可能原因             |
| ------------------------ | ------------------ | -------------------- |
| "notifyConnected 开始"   | observers\_.size() | observers\_本身损坏  |
| "观察者列表已拷贝"       | vector 拷贝        | observers\_指针无效  |
| "即将调用观察者"         | 虚函数调用         | 虚函数表损坏         |
| "onConnected() 被调用"   | 成员变量访问       | MqttHandler 已析构   |
| "onConnected() 执行完毕" | 返回后处理         | 异常处理或迭代器问题 |

---

## 🔧 根据日志的对症下药

### 如果是"即将调用观察者"后崩溃

**怀疑**: MqttHandler 对象已被删除，但指针还在列表中

**验证方法**:

```bash
# 使用gdb调试core文件
gdb ./ESDK_Sophon core

# 查看崩溃栈
bt

# 检查observer指针
p observer
p *observer  # 尝试解引用

# 查看虚函数表
info vtbl *observer
```

**解决方案**:

- 检查 MqttHandler 的生命周期
- 确保 removeObserver()在析构时被调用
- 考虑使用 shared_ptr 管理观察者

### 如果是"onConnected() 被调用"后崩溃

**怀疑**: MqttHandler 成员变量已损坏

**验证方法**:

```cpp
// 在 MqttHandler::onConnected() 中增加防御性检查
void MqttHandler::onConnected(const std::string& serverUri) {
    logger_.info("🟢 MqttHandler::onConnected() 被调用");

    // 检查成员变量
    logger_.debug("检查 mqttClient_ 地址: " +
                 std::to_string(reinterpret_cast<uintptr_t>(&mqttClient_)));
    logger_.debug("检查 taskManager_ 地址: " +
                 std::to_string(reinterpret_cast<uintptr_t>(&taskManager_)));

    logger_.info("MQTT连接成功: " + serverUri);
    logger_.debug("🟢 MqttHandler::onConnected() 执行完毕");
}
```

**解决方案**:

- 检查 MqttHandler 构造/析构顺序
- 确保单例正确初始化
- 避免在回调中访问可能已销毁的对象

---

## 💡 额外调试技巧

### 1. 启用 core dump（如果未启用）

```bash
# 在SE7设备上
ulimit -c unlimited

# 设置core文件路径
echo "/tmp/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern

# 运行程序
./ESDK_Sophon

# 崩溃后检查
ls -lh /tmp/core.*
```

### 2. 使用 valgrind 检测内存错误

```bash
# 需要在SE7上安装valgrind（如果有）
valgrind --leak-check=full --show-leak-kinds=all \
         --track-origins=yes --verbose \
         ./ESDK_Sophon
```

### 3. 添加更多防御性代码

```cpp
// 在 MqttClient::Impl::notifyConnected() 中
for (auto* observer : observersCopy) {
    logger_.info("📍 观察者地址: " +
                 std::to_string(reinterpret_cast<uintptr_t>(observer)));

    if (observer == nullptr) {
        logger_.warning("⚠️ 空指针，跳过");
        continue;
    }

    // 尝试读取虚函数表（可能崩溃）
    try {
        logger_.debug("尝试读取观察者类型信息...");
        const std::type_info& ti = typeid(*observer);
        logger_.debug("观察者类型: " + std::string(ti.name()));
    } catch (...) {
        logger_.error("💥 读取类型信息失败，对象可能已损坏！");
        continue;
    }

    // 正常调用
    observer->onConnected(serverUri);
}
```

---

## 📝 测试清单

- [ ] 新版本已部署到 SE7
- [ ] ulimit -c unlimited 已设置
- [ ] 日志输出到文件（方便查看完整日志）
- [ ] 运行程序并触发重连
- [ ] 观察最后一条日志
- [ ] 如果崩溃，记录最后 5 条日志
- [ ] 查看 core 文件（如果生成）
- [ ] 用 gdb 分析崩溃栈
- [ ] 根据"日志分析表"判断崩溃位置
- [ ] 实施对应的解决方案

---

## 🎯 预期结果

### 最理想情况

```
[INFO ] 成功连接到MQTT代理: tcp://jiaoyujidi.work:1883
[INFO ] MQTT重连成功
[DEBUG] 🔔 notifyConnected 开始，观察者数量: 1
[INFO ] 🎯 准备通知观察者: MQTT连接成功
[DEBUG] ✅ 观察者列表已拷贝，开始通知...
[INFO ] 📍 通知观察者 #0 地址=0x7f8c400010
[INFO ] ➡️ 即将调用观察者 #0->onConnected()
[INFO ] 🟢 MqttHandler::onConnected() 被调用
[INFO ] MQTT连接成功: tcp://jiaoyujidi.work:1883
[DEBUG] 🟢 MqttHandler::onConnected() 执行完毕
[INFO ] ✅ 观察者 #0 处理完成
[INFO ] 🏁 notifyConnected 完成，已通知 1 个观察者
[INFO ] 订阅成功: thing/product/analysis_device_WRSE7001/services  ← 继续正常运行
```

### 如果仍然崩溃

**最后一条日志会精确告诉我们崩溃位置**，然后我们可以：

1. 根据位置判断问题类型
2. 增加更多防御性代码
3. 使用 gdb 深入分析
4. 考虑架构调整（如使用 shared_ptr）

---

## 📞 下一步行动

1. **立即**: 部署新版本到 SE7，运行测试
2. **观察**: 记录最后一条日志
3. **反馈**: 将完整日志（特别是最后 10 条）发给我
4. **深入**: 根据日志分析表确定下一步调试方向

---

**创建者**: ESDK Sophon Team  
**测试状态**: ⏳ 待现场测试  
**更新时间**: 2025-10-30 18:00
