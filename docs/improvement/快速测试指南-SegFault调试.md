# ⚡ 快速测试指南 - Segmentation Fault 调试版本

**目的**: 通过详细日志快速定位崩溃位置

---

## 🚀 快速部署

### 1. 拷贝新版本到 SE7

```bash
# SSH到SE7
ssh linaro@192.168.150.1

# 停止旧进程
pkill -9 ESDK_Sophon

# 拷贝新版本（宿主机的文件应该已经通过docker挂载同步）
cd /data/Edge-SDK/build/bin
cp /mnt/shared/ESDK_Sophon ./ESDK_Sophon 2>/dev/null || echo "挂载路径可能不同"

# 如果上面失败，手动从容器拷贝
# 在宿主机: docker cp stream_lzy:/workspace/build/bin/ESDK_Sophon .
# 然后scp到SE7

chmod +x ESDK_Sophon
```

### 2. 启用 core dump（可选但推荐）

```bash
ulimit -c unlimited
echo "/tmp/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern
```

### 3. 运行程序

```bash
cd /data/Edge-SDK/build/bin
./ESDK_Sophon 2>&1 | tee /tmp/esdk_debug.log
```

---

## 👀 观察日志

### 重连成功时应该看到的日志序列

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
```

### 如果崩溃，记录最后一条日志

**最后一条日志会精确告诉我们崩溃位置！**

---

## 📋 快速判断表

| 最后看到的日志            | 崩溃位置                | 含义                         |
| ------------------------- | ----------------------- | ---------------------------- |
| "MQTT 重连成功"           | notifyConnected 入口    | observers\_本身损坏          |
| "🔔 notifyConnected 开始" | 访问 observers\_.size() | observers\_指针无效          |
| "✅ 观察者列表已拷贝"     | vector 拷贝             | 内存访问错误                 |
| "📍 通知观察者 #0"        | 准备调用                | 观察者指针看起来正常         |
| "➡️ 即将调用"             | 虚函数调用              | **虚函数表损坏或对象已析构** |
| "🟢 被调用"               | 函数内部                | **MqttHandler 成员变量损坏** |
| "🟢 执行完毕"             | 返回后                  | 异常处理或迭代器问题         |

---

## 💬 反馈格式

请提供以下信息：

```
1. 最后5-10条日志（从"MQTT重连成功"开始）
2. 是否生成了core文件（ls -lh /tmp/core.*）
3. 观察者地址是什么（例如：0x7f8c400010）
4. 崩溃是否每次都在相同位置
```

---

## 🔧 如果每次都在"➡️ 即将调用"后崩溃

**这是最可疑的场景**，说明：

- observer 指针看起来有效（能打印地址）
- 但调用虚函数时崩溃（对象可能已被析构）

**下一步**:

1. 用 gdb 分析 core 文件
2. 检查 MqttHandler 的生命周期
3. 确认 removeObserver()在析构时是否被调用
4. 可能需要改用 shared_ptr 管理观察者

---

**部署时间**: 2025-10-30  
**预计测试时间**: 5-10 分钟  
**预期结果**: 通过最后一条日志精确定位崩溃原因
