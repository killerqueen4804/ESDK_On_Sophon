# Day 6: DJI Liveview 集成完成记录

**日期**: 2025-11-10  
**模块**: LiveStreamTask  
**类型**: 功能集成  
**状态**: ✅ 已完成 (编译待验证)

---

## 📌 改进概述

将 LiveStreamTask 中的 OpenCV 临时实现替换为真实的 **DJI Liveview SDK**,实现从 DJI Dock 获取真实的 H.264 视频流。

### 核心改进

1. ✅ 集成 DJI SDK `edge_sdk::Liveview` 类
2. ✅ 实现 H264 数据回调接收
3. ✅ 实现生产者-消费者模式的 H264 队列
4. ✅ 添加 H264 解码框架 (预留 FFmpeg 实现)

---

## 🔨 技术细节

### 1. 架构设计

**数据流向**:

```
DJI Dock Camera
    ↓ (H.264 stream)
edge_sdk::Liveview
    ↓ (H264Callback)
onH264Data() [回调线程]
    ↓ (std::queue + mutex)
H264 数据队列
    ↓ (condition_variable)
getNextH264Data() [工作线程]
    ↓ (decode)
cv::Mat frame
    ↓ (process)
检测器 → 上报 → RTMP
```

**线程模型**:

- **DJI SDK 回调线程**: 执行 `onH264Data()`,快速将数据加入队列
- **工作线程** (`execute()`): 从队列获取数据,解码,检测,上报

**并发控制**:

- `std::mutex h264Mutex_`: 保护 H264 队列
- `std::condition_variable h264Condition_`: 通知队列有新数据
- `maxH264QueueSize_`: 限制队列长度,防止内存溢出

### 2. 头文件修改

#### LiveStreamTask.h

**新增头文件**:

```cpp
#include <queue>        // std::queue
#include <vector>       // std::vector<uint8_t>
#include "liveview.h"   // edge_sdk::Liveview
```

**新增成员变量**:

```cpp
// DJI Liveview 实例
std::shared_ptr<edge_sdk::Liveview> liveview_;

// H264 数据队列 (生产者-消费者)
std::queue<std::vector<uint8_t>> h264Queue_;
std::mutex h264Mutex_;
std::condition_variable h264Condition_;
size_t maxH264QueueSize_{10};  // 最大队列长度
```

**新增方法声明**:

```cpp
void deinitVideoStream();  // 反初始化视频流
edge_sdk::ErrorCode onH264Data(const uint8_t* buf, uint32_t len);  // H264 回调
bool getNextH264Data(std::vector<uint8_t>& h264Data);  // 获取 H264 数据
bool decodeH264ToMat(const std::vector<uint8_t>& h264Data, cv::Mat& frame);  // 解码
```

**移除变量**:

```cpp
// 已移除：
// cv::VideoCapture videoCapture_;  // 旧的 OpenCV 临时实现
```

### 3. 实现文件修改

#### LiveStreamTask.cpp

##### (1) initVideoStream() - Liveview 初始化

**Before** (临时实现):

```cpp
bool LiveStreamTask::initVideoStream() {
    logger_.warning("视频流连接功能尚未集成 Device 模块，使用临时实现");
    return true;  // 假装成功
}
```

**After** (真实实现):

```cpp
bool LiveStreamTask::initVideoStream() {
    logger_.info("初始化 DJI Liveview 视频流: taskId=" + config_.taskId);

    // 1. 创建 Liveview 实例
    liveview_ = edge_sdk::CreateLiveview();
    if (!liveview_) {
        logger_.error("创建 Liveview 实例失败");
        return false;
    }

    // 2. 配置 Liveview 参数
    edge_sdk::Liveview::Options options;
    options.camera = edge_sdk::Liveview::kCameraTypePayload;  // 负载相机
    options.quality = edge_sdk::Liveview::kStreamQuality720p; // 720p, 30fps
    options.callback = [this](const uint8_t* buf, uint32_t len) -> edge_sdk::ErrorCode {
        return this->onH264Data(buf, len);
    };

    // 3. 初始化 Liveview
    edge_sdk::ErrorCode ret = liveview_->Init(options);
    if (ret != edge_sdk::kErrorCodeSuccess) {
        logger_.error("Liveview 初始化失败: ErrorCode=" + std::to_string(static_cast<int>(ret)));
        liveview_.reset();
        return false;
    }

    // 4. 启动 H264 流
    ret = liveview_->StartH264Stream();
    if (ret != edge_sdk::kErrorCodeSuccess) {
        logger_.warning("StartH264Stream 失败: ErrorCode=" + std::to_string(static_cast<int>(ret)));
    }

    logger_.info("DJI Liveview 初始化成功: taskId=" + config_.taskId);
    return true;
}
```

**知识点**:

- `edge_sdk::CreateLiveview()`: 工厂函数,返回 `shared_ptr<Liveview>`
- `options.callback`: Lambda 捕获 `this`,绑定成员函数
- `kCameraTypePayload`: 负载相机 (Payload Camera,云台相机)
- `kStreamQuality720p`: 1280x720, 30fps, 1Mbps

##### (2) deinitVideoStream() - 资源释放 (新增)

```cpp
void LiveStreamTask::deinitVideoStream() {
    if (!liveview_) return;

    logger_.info("反初始化 DJI Liveview: taskId=" + config_.taskId);

    // 1. 停止 H264 流
    liveview_->StopH264Stream();

    // 2. 反初始化 Liveview
    liveview_->DeInit();

    // 3. 释放资源
    liveview_.reset();

    // 4. 清空 H264 队列
    {
        std::lock_guard<std::mutex> lock(h264Mutex_);
        while (!h264Queue_.empty()) {
            h264Queue_.pop();
        }
    }

    logger_.info("DJI Liveview 已释放: taskId=" + config_.taskId);
}
```

**知识点**:

- RAII 原则: 析构时自动清理资源
- 清空队列: 防止内存泄漏
- `shared_ptr::reset()`: 释放智能指针

##### (3) onH264Data() - H264 回调 (新增)

```cpp
edge_sdk::ErrorCode LiveStreamTask::onH264Data(const uint8_t* buf, uint32_t len) {
    // 此函数在 DJI SDK 的回调线程中执行，需要快速返回

    if (!buf || len == 0) {
        logger_.warning("收到空的 H264 数据");
        return edge_sdk::kErrorCodeSuccess;
    }

    // 将 H264 数据加入队列 (生产者)
    {
        std::lock_guard<std::mutex> lock(h264Mutex_);

        // 检查队列长度，防止内存溢出
        if (h264Queue_.size() >= maxH264QueueSize_) {
            logger_.warning("H264 队列已满，丢弃旧数据");
            h264Queue_.pop();  // 丢弃最旧的数据
        }

        // 复制数据到队列
        h264Queue_.emplace(buf, buf + len);
    }

    // 通知消费者线程
    h264Condition_.notify_one();

    return edge_sdk::kErrorCodeSuccess;
}
```

**知识点**:

- **回调线程**: DJI SDK 在内部线程调用此函数,需要快速返回
- **生产者角色**: 将数据加入队列,唤醒消费者
- **内存拷贝**: `std::vector<uint8_t>(buf, buf + len)` 拷贝数据
- **队列溢出保护**: 限制队列长度,丢弃旧数据 (FIFO)
- **notify_one()**: 唤醒一个等待的消费者线程

##### (4) getNextH264Data() - 获取 H264 数据 (新增)

```cpp
bool LiveStreamTask::getNextH264Data(std::vector<uint8_t>& h264Data) {
    std::unique_lock<std::mutex> lock(h264Mutex_);

    // 等待 H264 数据 (消费者)
    auto timeout = std::chrono::milliseconds(1000);  // 1 秒超时

    bool dataAvailable = h264Condition_.wait_for(lock, timeout, [this] {
        return !h264Queue_.empty() || !running_;
    });

    // 如果收到停止信号
    if (!running_) {
        return false;
    }

    // 如果超时或队列为空
    if (!dataAvailable || h264Queue_.empty()) {
        logger_.warning("获取 H264 数据超时");
        return false;
    }

    // 取出数据
    h264Data = std::move(h264Queue_.front());
    h264Queue_.pop();

    return true;
}
```

**知识点**:

- **消费者角色**: 从队列取数据
- **wait_for()**: 等待条件变量,最多 1 秒超时
- **Lambda 谓词**: 检查队列非空 **或** 收到停止信号
- **std::move()**: 移动语义,避免拷贝大数据

##### (5) decodeH264ToMat() - H264 解码 (框架)

```cpp
bool LiveStreamTask::decodeH264ToMat(const std::vector<uint8_t>& h264Data, cv::Mat& frame) {
    // TODO: 实现 H264 解码
    // 方案 1: 使用 FFmpeg 解码 (性能最好)
    // 方案 2: 使用 OpenCV 解码 (简单但性能较差)

    // 临时实现: 生成模拟帧
    logger_.debug("H264 解码功能尚未实现，使用模拟帧: 数据长度=" + std::to_string(h264Data.size()));

    frame = cv::Mat(720, 1280, CV_8UC3, cv::Scalar(0, 255, 0));  // 绿色帧

    return true;
}
```

**说明**:

- **临时实现**: 生成绿色帧,表示已接收到 H264 数据
- **预留接口**: 后续可替换为真实的 FFmpeg 解码

##### (6) getNextFrame() - 便利方法

```cpp
bool LiveStreamTask::getNextFrame(cv::Mat& frame) {
    // 1. 获取 H264 数据
    std::vector<uint8_t> h264Data;
    if (!getNextH264Data(h264Data)) {
        return false;
    }

    // 2. 解码为 cv::Mat
    if (!decodeH264ToMat(h264Data, frame)) {
        logger_.error("H264 解码失败");
        return false;
    }

    return true;
}
```

**说明**:

- 组合调用 `getNextH264Data()` + `decodeH264ToMat()`
- 保持与旧代码的接口兼容

##### (7) stop() - 修改资源释放

**Before**:

```cpp
// 释放视频流资源
if (videoCapture_.isOpened()) {
    videoCapture_.release();
}
```

**After**:

```cpp
// 释放视频流资源
deinitVideoStream();
```

---

## 📊 代码统计

### 修改文件

| 文件                                        | 修改类型 | 行数变化       |
| ------------------------------------------- | -------- | -------------- |
| `include/esdk_sophon/task/LiveStreamTask.h` | 修改     | +40 / -10      |
| `src/task/LiveStreamTask.cpp`               | 修改     | +120 / -30     |
| **总计**                                    |          | **+160 / -40** |

### 新增功能

- ✅ DJI Liveview 初始化/反初始化
- ✅ H264 回调接收
- ✅ H264 数据队列 (生产者-消费者)
- ✅ H264 解码框架

### 移除功能

- ❌ OpenCV VideoCapture 临时实现
- ❌ 模拟红色帧生成

---

## 🎓 面试知识点

### 1. 生产者-消费者模式

**Q: 如何在 C++ 中实现生产者-消费者模式?**

**A: 核心要素**:

1. **共享数据结构**: `std::queue` (先进先出)
2. **互斥锁**: `std::mutex` (保护队列)
3. **条件变量**: `std::condition_variable` (通知/等待)
4. **停止机制**: `std::atomic<bool>` (通知退出)

**项目中的例子**:

```cpp
// 生产者 (DJI SDK 回调线程)
edge_sdk::ErrorCode onH264Data(const uint8_t* buf, uint32_t len) {
    {
        std::lock_guard<std::mutex> lock(h264Mutex_);  // 1. 加锁
        h264Queue_.emplace(buf, buf + len);             // 2. 加入队列
    }  // 3. 自动解锁
    h264Condition_.notify_one();  // 4. 通知消费者
    return edge_sdk::kErrorCodeSuccess;
}

// 消费者 (工作线程)
bool getNextH264Data(std::vector<uint8_t>& h264Data) {
    std::unique_lock<std::mutex> lock(h264Mutex_);  // 1. 加锁

    // 2. 等待条件满足 (有数据 或 停止)
    h264Condition_.wait_for(lock, timeout, [this] {
        return !h264Queue_.empty() || !running_;
    });

    if (!running_) return false;  // 3. 检查停止信号

    h264Data = std::move(h264Queue_.front());  // 4. 取数据
    h264Queue_.pop();                           // 5. 移除
    return true;
}  // 6. 自动解锁
```

**易错点**:

- ❌ 忘记加锁: 导致数据竞争
- ❌ 死锁: `wait()` 前未持有锁
- ❌ 虚假唤醒: 未使用 Lambda 谓词检查条件
- ❌ 停止时未唤醒: 消费者永久等待

### 2. std::condition_variable

**Q: wait() 和 wait_for() 的区别?**

**A**:
| 方法 | 说明 | 返回值 |
|------|------|--------|
| `wait(lock, pred)` | 无限等待,直到 `pred()` 返回 true | void |
| `wait_for(lock, duration, pred)` | 最多等待 `duration` 时间 | `bool` (true=条件满足, false=超时) |

**为什么使用 wait_for()?**

- 避免无限等待 (如果生产者停止)
- 定期检查停止信号
- 超时后可以记录日志或重试

**项目中的使用**:

```cpp
auto timeout = std::chrono::milliseconds(1000);  // 1 秒超时
bool dataAvailable = h264Condition_.wait_for(lock, timeout, [this] {
    return !h264Queue_.empty() || !running_;
});
```

### 3. std::lock_guard vs std::unique_lock

**Q: 什么时候用 lock_guard,什么时候用 unique_lock?**

**A**:

| 类型               | 特点                                                             | 使用场景               |
| ------------------ | ---------------------------------------------------------------- | ---------------------- |
| `std::lock_guard`  | - 构造时加锁,析构时解锁<br>- **不可手动解锁**<br>- 性能更好      | 简单的临界区保护       |
| `std::unique_lock` | - 可手动 lock/unlock<br>- **可与条件变量配合**<br>- 可转移所有权 | 条件变量、复杂加锁逻辑 |

**项目中的选择**:

```cpp
// 生产者：简单加锁
{
    std::lock_guard<std::mutex> lock(h264Mutex_);  // 不需要 unlock
    h264Queue_.push(data);
}

// 消费者：需要与条件变量配合
std::unique_lock<std::mutex> lock(h264Mutex_);  // 必须用 unique_lock
h264Condition_.wait_for(lock, timeout, pred);
```

### 4. 回调函数的线程安全

**Q: 回调函数在不同线程执行,如何保证线程安全?**

**A: 本项目的解决方案**:

1. **识别线程上下文**:

   - `onH264Data()` 在 **DJI SDK 回调线程** 执行
   - `getNextH264Data()` 在 **工作线程** 执行

2. **使用互斥锁**:

   ```cpp
   std::lock_guard<std::mutex> lock(h264Mutex_);  // 保护共享队列
   ```

3. **快速返回**:

   - 回调函数应尽快返回,避免阻塞 SDK 内部线程
   - 只做数据拷贝和入队,不做耗时操作

4. **避免死锁**:
   - 不在回调中调用 SDK 的其他 API (可能死锁)

**反例** (错误):

```cpp
edge_sdk::ErrorCode onH264Data(...) {
    processFrame(...);  // ❌ 耗时操作,阻塞回调线程
    liveview_->SetCameraSource(...);  // ❌ 可能死锁
}
```

### 5. 内存拷贝 vs 移动语义

**Q: 什么时候使用 std::move()?**

**A: 移动语义的优势**:

- 避免大数据的深拷贝
- 转移资源所有权,而非复制

**项目中的使用**:

**生产者** (拷贝):

```cpp
// 必须拷贝,因为 buf 是外部数据
h264Queue_.emplace(buf, buf + len);  // 拷贝构造 vector
```

**消费者** (移动):

```cpp
// 可以移动,因为队列中的数据已经不需要了
h264Data = std::move(h264Queue_.front());  // 移动赋值,O(1)
h264Queue_.pop();
```

**对比**:

```cpp
// 拷贝 (慢)
h264Data = h264Queue_.front();  // 深拷贝,O(n)

// 移动 (快)
h264Data = std::move(h264Queue_.front());  // 浅拷贝,O(1)
```

---

## 🔍 测试计划

### 1. 编译测试

```bash
# 在 Docker 容器中编译
cd /workspace/ESDK_On_Sophon/build
cmake ..
make -j4
```

**预期**:

- ✅ 编译成功,无错误
- ⚠️ 可能的警告: 未使用的变量、未实现的 TODO

### 2. 运行时测试

**测试用例**:

#### 测试 1: Liveview 初始化

```cpp
LiveStreamTask task(config, service);
bool success = task.start();
ASSERT_TRUE(success);  // 应该成功初始化 Liveview
```

**预期**:

- ✅ Liveview 初始化成功
- ✅ H264 回调被注册
- ✅ 开始接收 H264 数据

#### 测试 2: H264 数据接收

```cpp
std::this_thread::sleep_for(std::chrono::seconds(5));  // 等待接收数据
auto stats = task.getStatistics();
ASSERT_GT(stats.framesProcessed, 0);  // 应该处理了一些帧
```

**预期**:

- ✅ H264 数据持续接收
- ✅ 队列不会溢出
- ✅ 无内存泄漏

#### 测试 3: 暂停/恢复

```cpp
task.pause();
std::this_thread::sleep_for(std::chrono::seconds(2));
task.resume();
```

**预期**:

- ✅ 暂停时不再处理帧
- ✅ H264 数据仍在接收 (队列积累)
- ✅ 恢复后继续处理

#### 测试 4: 停止和清理

```cpp
task.stop();
```

**预期**:

- ✅ Liveview 正确反初始化
- ✅ H264 队列清空
- ✅ 工作线程退出
- ✅ 无资源泄漏

### 3. 性能测试

**指标**:

- **帧率**: 应接近 30 FPS (720p 标准)
- **队列长度**: 不应超过 `maxH264QueueSize_`
- **内存使用**: 稳定,无持续增长
- **CPU 占用**: 合理范围 (取决于解码器)

---

## 🚧 待完成事项

### 1. H264 解码器实现

**当前状态**: 生成绿色模拟帧

**计划**:

- [ ] 方案 A: 使用 FFmpeg 解码 (推荐)

  - 性能最好
  - 需要链接 FFmpeg 库
  - 代码复杂度中等

- [ ] 方案 B: 使用 OpenCV 解码

  - 简单易用
  - 性能较差
  - 可能不支持某些 H264 配置

- [ ] 方案 C: 直接推流 H264 (最优)
  - 无需解码
  - 性能最好
  - 需要修改 RTMP 推流器接口

### 2. 相机参数配置化

**当前**: 硬编码在代码中

```cpp
options.camera = edge_sdk::Liveview::kCameraTypePayload;
options.quality = edge_sdk::Liveview::kStreamQuality720p;
```

**改进**: 从配置文件读取

```json
{
  "liveview": {
    "cameraType": "payload",
    "quality": "720p",
    "source": "wide"
  }
}
```

### 3. 错误恢复机制

**当前**: 初始化失败直接返回 false

**改进**:

- [ ] 自动重试 (最多 3 次)
- [ ] 降级策略 (如 1080p 失败则尝试 720p)
- [ ] 重连机制 (断流后自动重连)

### 4. 队列溢出策略

**当前**: 丢弃最旧的数据

**改进**:

- [ ] 统计丢帧率
- [ ] 根据丢帧率动态调整检测间隔
- [ ] 告警机制 (丢帧率超过阈值)

---

## 📚 参考资料

### DJI SDK 文档

- `Edge-SDK/doc/` - SDK 文档
- `Edge-SDK/examples/liveview/` - 示例代码

### C++ 并发编程

- [cppreference: std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [cppreference: std::lock_guard](https://en.cppreference.com/w/cpp/thread/lock_guard)
- [cppreference: std::unique_lock](https://en.cppreference.com/w/cpp/thread/unique_lock)

### 设计模式

- [生产者-消费者模式](https://en.wikipedia.org/wiki/Producer%E2%80%93consumer_problem)

---

## ✅ 总结

本次改进成功将 LiveStreamTask 从临时实现升级为真实的 DJI Liveview 集成:

**收获**:

1. ✅ 学习了 DJI SDK Liveview API 的使用
2. ✅ 实践了生产者-消费者模式
3. ✅ 理解了回调函数的线程安全
4. ✅ 掌握了 std::condition_variable 的用法
5. ✅ 应用了移动语义优化性能

**下一步**:

1. 编译测试
2. 运行时测试
3. 实现 H264 解码器
4. 继续 Day 6 Task 3: MediaManager 集成

---

**创建日期**: 2025-11-10  
**更新日期**: 2025-11-10  
**版本**: v1.0  
**状态**: ✅ 集成完成,待测试
