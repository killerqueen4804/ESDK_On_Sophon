# Day 6: DJI MediaManager 集成完成记录

**日期**: 2025-11-10  
**模块**: MediaFileTask  
**类型**: 功能集成  
**状态**: ✅ 已完成 (编译待验证)

---

## 📌 改进概述

将 MediaFileTask 中的临时文件读取实现替换为真实的 **DJI MediaManager SDK**,实现从 DJI Dock 自动接收和处理航线任务产生的图片文件。

### 核心改进

1. ✅ 集成 DJI SDK `edge_sdk::MediaManager` 类
2. ✅ 实现 MediaFilesObserver 回调接收文件通知
3. ✅ 集成 MediaFilesReader 读取文件内容
4. ✅ 移除临时的 MediaFileInfo 结构,直接使用 SDK 的 MediaFile
5. ✅ 配置云端上传和自动删除策略

---

## 🔨 技术细节

### 1. 架构设计

**数据流向**:

```
DJI Dock (航线任务拍照)
    ↓
MediaManager (文件管理)
    ↓ (MediaFilesObserver)
onMediaFileUpdate() [回调线程]
    ↓ (std::queue + mutex)
文件队列
    ↓ (condition_variable)
execute() [工作线程]
    ↓
MediaFilesReader::Open/Read/Close
    ↓
cv::imdecode()
    ↓
检测器 → 上报
```

**线程模型**:

- **DJI SDK 回调线程**: 执行 `onMediaFileUpdate()`,将文件信息加入队列
- **工作线程** (`execute()`): 从队列获取文件,读取,解码,检测,上报

**并发控制**:

- `std::mutex queueMutex_`: 保护文件队列
- `std::condition_variable queueCv_`: 通知队列有新文件
- `std::atomic<bool> running_`: 停止信号

### 2. 头文件修改

#### MediaFileTask.h

**新增头文件**:

```cpp
#include "media_manager.h"  // edge_sdk::MediaManager
#include "media_file.h"     // edge_sdk::MediaFile
```

**移除临时定义**:

```cpp
// 已移除:
// struct MediaFileInfo {
//     std::string file_name;
//     std::string file_path;
//     int file_size;
// };
```

**新增/修改成员变量**:

```cpp
// DJI SDK
edge_sdk::MediaManager* mediaManager_{nullptr};  // 单例
std::shared_ptr<edge_sdk::MediaFilesReader> mediaReader_;  // 文件读取器

// 文件队列 (类型修改)
std::queue<edge_sdk::MediaFile> fileQueue_;  // 使用 SDK 的 MediaFile
```

**修改方法声明**:

```cpp
// Before
void onMediaFileUpdate(const MediaFileInfo& file);
bool readMediaFile(const MediaFileInfo& file, std::vector<uint8_t>& imageData);

// After
edge_sdk::ErrorCode onMediaFileUpdate(const edge_sdk::MediaFile& file);
bool readMediaFile(const edge_sdk::MediaFile& file, std::vector<uint8_t>& imageData);
```

### 3. 实现文件修改

#### MediaFileTask.cpp

##### (1) registerMediaFilesObserver() - 注册观察者

**Before** (临时实现):

```cpp
bool MediaFileTask::registerMediaFilesObserver() {
    logger_.warning("MediaFilesObserver 注册功能尚未集成 DJI SDK，使用临时实现");
    return true;  // 假装成功
}
```

**After** (真实实现):

```cpp
bool MediaFileTask::registerMediaFilesObserver() {
    logger_.info("注册 MediaFilesObserver: taskId=" + config_.taskId);

    // 1. 获取 MediaManager 单例
    mediaManager_ = edge_sdk::MediaManager::Instance();
    if (!mediaManager_) {
        logger_.error("获取 MediaManager 实例失败");
        return false;
    }

    // 2. 注册观察者回调 (Lambda 捕获 this)
    auto observer = [this](const edge_sdk::MediaFile& file) -> edge_sdk::ErrorCode {
        return this->onMediaFileUpdate(file);
    };

    edge_sdk::ErrorCode ret = mediaManager_->RegisterMediaFilesObserver(observer);
    if (ret != edge_sdk::kErrorCodeSuccess) {
        logger_.error("MediaFilesObserver 注册失败: ErrorCode=" + std::to_string(static_cast<int>(ret)));
        return false;
    }

    // 3. 创建文件读取器
    mediaReader_ = mediaManager_->CreateMediaFilesReader();
    if (!mediaReader_) {
        logger_.error("创建 MediaFilesReader 失败");
        return false;
    }

    // 4. 配置: 不上传到云端,不自动删除
    mediaManager_->SetDroneNestUploadCloud(false);
    mediaManager_->SetDroneNestAutoDelete(false);

    logger_.info("MediaFilesObserver 注册成功: taskId=" + config_.taskId);
    return true;
}
```

**知识点**:

- `MediaManager::Instance()`: 单例模式,全局唯一
- `RegisterMediaFilesObserver()`: 注册回调,接收文件通知
- `CreateMediaFilesReader()`: 创建文件读取器
- `SetDroneNestUploadCloud(false)`: 禁止上传到云端 (边缘计算本地处理)
- `SetDroneNestAutoDelete(false)`: 禁止自动删除文件

##### (2) unregisterMediaFilesObserver() - 注销观察者

```cpp
void MediaFileTask::unregisterMediaFilesObserver() {
    logger_.info("注销 MediaFilesObserver: taskId=" + config_.taskId);

    // DJI SDK 通常不提供显式的注销方法
    // MediaManager 的观察者在 MediaManager 生命周期结束时自动清理

    // 释放 MediaFilesReader
    if (mediaReader_) {
        mediaReader_.reset();
        logger_.debug("MediaFilesReader 已释放");
    }

    logger_.info("MediaFilesObserver 已注销: taskId=" + config_.taskId);
}
```

**说明**:

- DJI SDK 的 MediaManager 不提供显式注销 API
- 观察者会在 MediaManager 生命周期结束时自动清理
- 手动释放 MediaFilesReader 资源

##### (3) onMediaFileUpdate() - 文件通知回调

**Before**:

```cpp
void MediaFileTask::onMediaFileUpdate(const MediaFileInfo& file) {
    if (file.file_name.find(".mp4") != std::string::npos) {
        return;  // 跳过视频
    }
    fileQueue_.push(file);
    queueCv_.notify_one();
}
```

**After**:

```cpp
edge_sdk::ErrorCode MediaFileTask::onMediaFileUpdate(const edge_sdk::MediaFile& file) {
    logger_.info("收到媒体文件更新通知: " + file.file_name);

    // 过滤视频文件（只处理图片）
    if (file.file_type == edge_sdk::MediaFile::kFileTypeMp4) {
        logger_.debug("跳过视频文件: " + file.file_name);
        return edge_sdk::kErrorCodeSuccess;  // 继续接收其他文件
    }

    // 将文件信息放入队列
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fileQueue_.push(file);
    }

    // 通知消费者线程
    queueCv_.notify_one();

    logger_.debug("文件已加入队列: " + file.file_name +
                 ", 队列大小=" + std::to_string(fileQueue_.size()));

    return edge_sdk::kErrorCodeSuccess;
}
```

**知识点**:

- **返回类型**: `edge_sdk::ErrorCode` (SDK 要求)
- **文件类型判断**: 使用 `file.file_type` 而非字符串匹配
- **线程安全**: 使用 `lock_guard` 保护队列
- **通知机制**: `notify_one()` 唤醒消费者线程

**MediaFile 结构**:

```cpp
struct MediaFile {
    enum FileType {
        kFileTypeJpeg = 0,  // JPEG 图片
        kFileTypeMp4 = 3,   // MP4 视频
    };

    std::string file_name;         // 文件名 (如 "DJI_0123.JPG")
    std::string file_path;         // 文件路径 (SDK 内部路径)
    size_t file_size;              // 文件大小
    FileType file_type;            // 文件类型
    double latitude, longitude;    // GPS 坐标
    double absolute_altitude;      // 绝对高度
    int32_t image_width, image_height;  // 图片尺寸
    time_t create_time;            // 创建时间
};
```

##### (4) readMediaFile() - 读取文件

**Before** (临时实现):

```cpp
bool MediaFileTask::readMediaFile(const MediaFileInfo& file,
                                  std::vector<uint8_t>& imageData) {
    // 从本地文件系统读取
    std::ifstream ifs(file.file_name, std::ios::binary);
    if (!ifs.is_open()) return false;

    ifs.seekg(0, std::ios::end);
    size_t fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    imageData.resize(fileSize);
    ifs.read(reinterpret_cast<char*>(imageData.data()), fileSize);
    ifs.close();

    return true;
}
```

**After** (真实实现):

```cpp
bool MediaFileTask::readMediaFile(const edge_sdk::MediaFile& file,
                                  std::vector<uint8_t>& imageData) {
    logger_.debug("读取媒体文件: " + file.file_name);

    if (!mediaReader_) {
        logger_.error("MediaFilesReader 未初始化");
        return false;
    }

    // 1. 打开文件
    auto fd = mediaReader_->Open(file.file_path);
    if (fd < 0) {
        logger_.error("打开文件失败: " + file.file_name + ", fd=" + std::to_string(fd));
        return false;
    }

    // 2. 循环读取文件数据
    const size_t BUFFER_SIZE = 1024 * 1024;  // 1 MB 缓冲区
    std::vector<char> buffer(BUFFER_SIZE);

    while (true) {
        size_t nread = mediaReader_->Read(fd, buffer.data(), buffer.size());

        if (nread > 0) {
            // 将读取的数据追加到 imageData
            imageData.insert(imageData.end(),
                           reinterpret_cast<uint8_t*>(buffer.data()),
                           reinterpret_cast<uint8_t*>(buffer.data() + nread));
        } else {
            // 读取完成或出错
            break;
        }
    }

    // 3. 关闭文件
    edge_sdk::ErrorCode ret = mediaReader_->Close(fd);
    if (ret != edge_sdk::kErrorCodeSuccess) {
        logger_.warning("关闭文件失败: " + file.file_name +
                       ", ErrorCode=" + std::to_string(static_cast<int>(ret)));
    }

    logger_.debug("文件读取成功: " + file.file_name +
                 ", 大小=" + std::to_string(imageData.size()) + " bytes");

    return imageData.size() > 0;
}
```

**知识点**:

- **MediaFilesReader API**:
  - `Open(file_path)`: 打开文件,返回文件描述符
  - `Read(fd, buf, count)`: 读取数据,返回实际读取字节数
  - `Close(fd)`: 关闭文件
- **分块读取**: 使用 1MB 缓冲区,循环读取大文件
- **数据追加**: 使用 `vector::insert()` 追加数据

##### (5) execute() - 工作线程 (类型修改)

```cpp
// Before
MediaFileInfo file = fileQueue_.front();

// After
edge_sdk::MediaFile file = fileQueue_.front();
```

**说明**: 队列元素类型从 `MediaFileInfo` 改为 `edge_sdk::MediaFile`

---

## 📊 代码统计

### 修改文件

| 文件                                       | 修改类型 | 行数变化      |
| ------------------------------------------ | -------- | ------------- |
| `include/esdk_sophon/task/MediaFileTask.h` | 修改     | +15 / -25     |
| `src/task/MediaFileTask.cpp`               | 修改     | +65 / -50     |
| **总计**                                   |          | **+80 / -75** |

### 新增功能

- ✅ MediaManager 单例获取
- ✅ MediaFilesObserver 注册
- ✅ MediaFilesReader 文件读取
- ✅ 云端上传/自动删除配置

### 移除功能

- ❌ 临时的 MediaFileInfo 结构
- ❌ 本地文件系统读取

---

## 🎓 面试知识点

### 1. 单例模式 (MediaManager)

**Q: DJI SDK 为什么将 MediaManager 设计为单例?**

**A: 原因**:

1. **全局唯一性**: 整个应用只需要一个 MediaManager 实例
2. **资源集中管理**: 统一管理所有媒体文件操作
3. **避免冲突**: 防止多个实例同时操作同一文件

**项目中的使用**:

```cpp
// 获取单例
edge_sdk::MediaManager* mediaManager = edge_sdk::MediaManager::Instance();

// 注册观察者
mediaManager->RegisterMediaFilesObserver(observer);

// 创建读取器
auto reader = mediaManager->CreateMediaFilesReader();
```

**经典单例实现**:

```cpp
class MediaManager {
public:
    static MediaManager* Instance() {
        static MediaManager instance;  // 线程安全 (C++11)
        return &instance;
    }

    MediaManager(const MediaManager&) = delete;
    MediaManager& operator=(const MediaManager&) = delete;

private:
    MediaManager() {}  // 私有构造函数
    ~MediaManager() {}
};
```

**线程安全**:

- C++11 的 `static` 局部变量初始化是线程安全的
- 编译器保证只初始化一次

### 2. 观察者模式 (MediaFilesObserver)

**Q: MediaFilesObserver 是什么设计模式?**

**A: 观察者模式 (Observer Pattern)**

**定义**:

- 定义对象间的一对多依赖关系
- 当一个对象状态改变时,所有依赖者都会收到通知

**项目中的应用**:

```cpp
// 主题 (Subject): MediaManager
class MediaManager {
public:
    using MediaFilesObserver = std::function<ErrorCode(const MediaFile&)>;

    ErrorCode RegisterMediaFilesObserver(MediaFilesObserver observer);
};

// 观察者 (Observer): MediaFileTask
class MediaFileTask {
    edge_sdk::ErrorCode onMediaFileUpdate(const edge_sdk::MediaFile& file) {
        // 收到通知,处理文件
        fileQueue_.push(file);
        return edge_sdk::kErrorCodeSuccess;
    }
};

// 注册观察者
auto observer = [this](const edge_sdk::MediaFile& file) {
    return this->onMediaFileUpdate(file);
};
mediaManager->RegisterMediaFilesObserver(observer);
```

**优点**:

- ✅ 解耦: MediaManager 不需要知道 MediaFileTask 的实现
- ✅ 扩展性: 可以注册多个观察者
- ✅ 动态订阅: 运行时注册/注销

**经典实现**:

```cpp
class Subject {
    std::vector<Observer*> observers_;
public:
    void Attach(Observer* observer) { observers_.push_back(observer); }
    void Notify() {
        for (auto* obs : observers_) {
            obs->Update();
        }
    }
};
```

### 3. 分块读取大文件

**Q: 为什么不一次性读取整个文件?**

**A: 内存优化**:

1. **大文件**: 图片可能有几十 MB,一次性读取占用大量内存
2. **内存碎片**: 频繁分配/释放大块内存导致碎片
3. **可控性**: 固定大小缓冲区,内存使用可预测

**项目中的实现**:

```cpp
const size_t BUFFER_SIZE = 1024 * 1024;  // 1 MB
std::vector<char> buffer(BUFFER_SIZE);   // 固定缓冲区

while (true) {
    size_t nread = mediaReader_->Read(fd, buffer.data(), buffer.size());
    if (nread > 0) {
        imageData.insert(imageData.end(), buffer.data(), buffer.data() + nread);
    } else {
        break;
    }
}
```

**对比**:

| 方案       | 内存占用   | 性能 | 风险     |
| ---------- | ---------- | ---- | -------- |
| 一次性读取 | 文件大小   | 快   | 内存不足 |
| 分块读取   | 缓冲区大小 | 稍慢 | 可控     |

### 4. Lambda 捕获与成员函数绑定

**Q: 为什么使用 Lambda 而不是 std::bind?**

**A: 现代 C++ 推荐 Lambda**

**Lambda 方式** (推荐):

```cpp
auto observer = [this](const edge_sdk::MediaFile& file) -> edge_sdk::ErrorCode {
    return this->onMediaFileUpdate(file);
};
```

**std::bind 方式** (不推荐):

```cpp
auto observer = std::bind(&MediaFileTask::onMediaFileUpdate,
                         this,
                         std::placeholders::_1);
```

**Lambda 优势**:

- ✅ 更简洁易读
- ✅ 编译器优化更好
- ✅ 支持移动捕获
- ✅ 类型推导更准确

**易错点**:

```cpp
// ❌ 错误: 捕获 this,但对象可能已销毁
auto observer = [this](...) { ... };  // this 可能失效

// ✅ 正确: 使用 shared_ptr 延长生命周期
auto observer = [self = shared_from_this()](...) {
    self->onMediaFileUpdate(...);
};
```

### 5. 文件类型枚举 vs 字符串匹配

**Q: 为什么使用枚举判断文件类型?**

**A: 类型安全 + 性能**

**枚举方式** (推荐):

```cpp
if (file.file_type == edge_sdk::MediaFile::kFileTypeMp4) {
    // 跳过视频
}
```

- ✅ 编译时检查
- ✅ O(1) 比较
- ✅ 类型安全

**字符串匹配** (不推荐):

```cpp
if (file.file_name.find(".mp4") != std::string::npos) {
    // 跳过视频
}
```

- ❌ 运行时比较
- ❌ O(n) 查找
- ❌ 大小写问题 (.MP4 vs .mp4)
- ❌ 假阳性 ("test.mp4.jpg")

---

## 🔍 测试计划

### 1. 编译测试

```bash
cd /workspace/ESDK_On_Sophon/build
cmake ..
make -j4
```

**预期**:

- ✅ 编译成功,无错误
- ⚠️ 可能的警告: 未使用的变量

### 2. 运行时测试

#### 测试 1: MediaFilesObserver 注册

```cpp
MediaFileTask task(config, service);
bool success = task.start();
ASSERT_TRUE(success);  // 应该成功注册
```

**预期**:

- ✅ MediaManager 初始化成功
- ✅ 观察者注册成功
- ✅ MediaFilesReader 创建成功

#### 测试 2: 文件通知接收

```cpp
// 等待 DJI Dock 航线任务拍照
std::this_thread::sleep_for(std::chrono::minutes(1));
auto stats = task.getStatistics();
ASSERT_GT(stats.framesProcessed, 0);  // 应该处理了一些文件
```

**预期**:

- ✅ 收到文件通知
- ✅ 文件加入队列
- ✅ 成功读取和解码

#### 测试 3: 文件读取

```cpp
// 模拟文件
edge_sdk::MediaFile file;
file.file_path = "/path/to/test.jpg";

std::vector<uint8_t> imageData;
bool success = task.readMediaFile(file, imageData);

ASSERT_TRUE(success);
ASSERT_GT(imageData.size(), 0);
```

**预期**:

- ✅ 文件打开成功
- ✅ 数据读取完整
- ✅ 文件关闭成功

---

## 🚧 待完成事项

### 1. 错误恢复机制

**当前**: 读取失败直接跳过

**改进**:

- [ ] 重试机制 (最多 3 次)
- [ ] 记录失败文件列表
- [ ] 定期重新尝试失败文件

### 2. 文件过滤优化

**当前**: 只处理 JPEG 图片

**改进**:

- [ ] 支持更多图片格式 (PNG, BMP)
- [ ] 根据相机类型过滤 (红外/可见光)
- [ ] 根据时间范围过滤

### 3. 队列溢出策略

**当前**: 无限制

**改进**:

- [ ] 限制队列长度
- [ ] 丢弃策略 (FIFO 或优先级)
- [ ] 告警机制

### 4. 文件元数据利用

**当前**: 只使用 file_name 和 file_path

**改进**:

- [ ] 利用 GPS 坐标 (latitude, longitude)
- [ ] 利用高度信息 (absolute_altitude)
- [ ] 利用时间戳 (create_time)
- [ ] 记录到检测结果中

---

## 📚 参考资料

### DJI SDK 文档

- `Edge-SDK/doc/` - SDK 文档
- `Edge-SDK/examples/media_manager/` - 示例代码

### 设计模式

- [观察者模式](https://refactoring.guru/design-patterns/observer)
- [单例模式](https://refactoring.guru/design-patterns/singleton)

### C++ 最佳实践

- [Lambda vs std::bind](https://herbsutter.com/2013/06/05/gotw-92-solution-value-semantics/)
- [大文件读取优化](https://en.cppreference.com/w/cpp/io/basic_istream/read)

---

## ✅ 总结

本次改进成功将 MediaFileTask 从临时实现升级为真实的 DJI MediaManager 集成:

**收获**:

1. ✅ 学习了 DJI SDK MediaManager API 的使用
2. ✅ 理解了观察者模式的实际应用
3. ✅ 掌握了单例模式的线程安全实现
4. ✅ 实践了分块读取大文件的技巧
5. ✅ 对比了 Lambda 和 std::bind 的差异

**下一步**:

1. Task 4: 实现 RTMP 推流器
2. Task 5: LiveStreamTask 集成 RTMP
3. Task 6: 编写集成测试
4. Task 7: 编译和调试

---

**创建日期**: 2025-11-10  
**更新日期**: 2025-11-10  
**版本**: v1.0  
**状态**: ✅ 集成完成,待测试
