# 改进记录 — 2025-12-22：引入异步 MQTT 发布线程与后台重试

## 概要

为了解决在处理大体积事件（例如包含 base64 图片）时，调用线程被 MQTT 同步 publish 阻塞的问题，新增了一条后台发布线程（publish thread）用于处理 QoS=0 的异步发布，同时将 EventCache 的重试流程切换为后台线程执行，避免在重连或重试时阻塞主线程或任务工作线程。

## 变更文件

- src/Mqtt/MqttClient.cpp

  - 增加 PublishItem 结构体与发布队列、互斥、条件变量等成员。
  - 实现 `publishThreadFunc()`：后台消费队列并调用 paho 接口发布消息。
  - 实现 `startPublishThread()` / `stopPublishThread()` 并在构造/析构中启停线程。
  - 修改 `publish()`：当 QoS==0 时将消息入队并立即返回；当 QoS>0 时保持原有的同步发布语义（等待确认）。

- src/core/EventCache.cpp

  - 修改 `onMqttReconnected()`：触发重试时以后台线程执行 `retryAll()`，避免阻塞调用线程。

- docs/improvement/改进记录-2025-12-22-异步-MQTT-publish.md（新增）
  - 记录本次改动原因、实现与注意事项。

## 变更详情与原因（教学说明）

1. 问题说明

   - 之前的实现中，无论消息大小如何，`publish()` 在调用 paho 的 `MQTTClient_publishMessage()` 后，对于 QoS>0 会调用 `MQTTClient_waitForCompletion()` 等待服务端确认，这会阻塞调用线程。
   - EventCache 在重连后会同步遍历缓存并调用发布回调（同步），当回调内部进行同步发布时，会导致调用线程（例如任务线程或网络事件处理线程）长时间被阻塞，造成任务停止上报或延迟。

2. 解决方案要点

   - 对于 QoS=0 的消息，采用“入队 + 后台线程发布”的异步模式，调用者不再等待网络发送完成，从而避免因网络波动或消息大小导致的阻塞。
   - 对于 QoS>0 的消息（需要确认），保留同步语义，调用者仍会等待确认（以保证可靠性）。这符合 MQTT 的设计意图：QoS0 快速、尽力而为；QoS1/2 需要确认。
   - 将 EventCache 的重试逻辑放到后台线程执行，避免重试过程阻塞重连回调或调度线程。

3. 实现细节

   - 在 `MqttClient::Impl` 中增加 `publishQueue_`（deque）、`publishMutex_`、`publishCv_` 与 `publishThread_`。
   - `publish()` 对 QoS==0 的请求做入队操作；队列长度上限为 1024（防止内存无限增长），超过时会丢弃最旧消息并记录日志。
   - `publishThreadFunc()` 会取出队列项并调用 `MQTTClient_publishMessage()` 进行发布；若 QoS>0 则等待确认。若发布失败或未连接，线程会将消息重试放回队列并延迟。
   - `EventCache::onMqttReconnected()` 使用 `std::thread(...).detach()` 在后台执行 `retryAll()`，并在启动失败时退化为同步重试以保证事件不丢失。

4. 好处

   - 任务线程不会因为单个大消息或网络延迟而被阻塞，恢复了上报能力与系统响应性。
   - 保持 QoS>0 的可靠性语义。
   - EventCache 重试不会阻塞关键路径。

5. 需注意 / 后续改进建议
   - 目前 publish 队列的策略为丢弃最旧消息（当队列满时），可根据场景改为拒绝入队或持久化到磁盘。
   - 可以暴露队列长度与行为配置到 config.json（例如 mqtt.publish_queue_size）。
   - 增加发布失败报警、指标（队列长度、失败计数、重试次数）方便运维观察。
   - 在线程退出/程序关闭时，考虑等待队列清空或将未发消息持久化。

## 验证建议（如何测试）

1. 单元测试：模拟 broker 无响应场景，发送大量 QoS=0 消息，确认调用线程不被阻塞（返回迅速），且后台线程逐条发送。
2. 集成测试：在设备上使用真实 broker，启动 MediaFileTask，发布包含大 base64 图像的事件，观察任务是否继续运行并且消息最终到达云端。
3. 极限测试：将队列推到超过阈值（>1024），观察日志是否记录丢弃行为并且系统稳定。

## 变更时间线

- 2025-12-22：完成异步发布线程实现与 EventCache 后台重试改造。

---

_记录人：ESDK Sophon 开发与维护团队（自动生成）_
