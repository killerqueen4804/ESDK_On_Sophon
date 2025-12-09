# Device 模块架构设计

> 📅 **创建日期**: 2025-10-27  
> 🎯 **版本**: v1.0  
> 📋 **状态**: 设计中  
> 🔗 **基于**: DJI Edge-SDK v1.2.0

---

## 📌 模块定位

Device 模块是**底层支撑模块**,不处理业务逻辑,职责是为上层模块(Vision)提供**统一的数据接口**。

### 核心价值

```
┌─────────────────────────────────────────────┐
│         上层模块 (业务层)                    │
│  ┌──────────────┐                           │
│  │ Vision Module│ (目标检测/分割)            │
│  └──────────────┘                           │
│         ↑                                    │
│         │ 提供 cv::Mat (统一格式)             │
│         │                                    │
├─────────────────────────────────────────────┤
│     Device Module (数据源抽象层)             │
│  ┌──────────┐        ┌──────────┐           │
│  │ Liveview │        │  Media   │           │
│  │ (视频流) │        │  (图片)   │           │
│  └──────────┘        └──────────┘           │
│         ↑                  ↑                 │
│         │ H264流           │ JPEG文件         │
├─────────────────────────────────────────────┤
│         Edge-SDK (DJI官方SDK)                │
│  ┌──────────┐  ┌───────────┐  ┌──────────┐ │
│  │ESDKInit  │  │ Liveview  │  │MediaMgr  │ │
│  └──────────┘  └───────────┘  └──────────┘ │
│         ↑                                    │
│         │ 私有协议                            │
├─────────────────────────────────────────────┤
│       DJI Dock + 无人机                      │
└─────────────────────────────────────────────┘
```

### 与 MQTT 模块的区别

| 模块       | 通信对象       | 协议      | 数据流向 | 职责              |
| ---------- | -------------- | --------- | -------- | ----------------- |
| **MQTT**   | SE7 ↔ 云端     | MQTT/TCP  | 双向     | 接收指令,上报结果 |
| **Device** | SE7 ↔ DJI Dock | ESDK 私有 | 主要下行 | 获取视频/图片     |

**重要**: 两个模块的连接状态**独立**!

---

## 🎯 功能需求(基于接口文档)

### 数据源 1: 实时视频流 (source=0)

**接口文档定义**:

```json
{
  "method": "device_algorithm_enable",
  "source": 0, // 0: 视频流分析
  "algorithm_type": "object_detection"
}
```

**Device 模块响应**:

```cpp
DeviceManager::startLiveview([](const cv::Mat& frame) {
    // 每一帧到达时调用此回调
    // frame: 1280x720 BGR格式 (已解码)
    VisionModule::detect(frame);
});
```

### 数据源 2: 媒体文件 (source=1)

**接口文档定义**:

```json
{
  "method": "device_algorithm_enable",
  "source": 1, // 1: 图片分析
  "algorithm_type": "segmentation"
}
```

**Device 模块响应**:

```cpp
auto files = DeviceManager::listMediaFiles();
for (auto& file : files) {
    DeviceManager::downloadMediaFile(file.id, [](const cv::Mat& image) {
        // 图片下载并解码后调用
        VisionModule::segment(image);
    });
}
```

---

## 🏗️ 类设计

### 1. IDeviceObserver.h (观察者接口)

```cpp
/**
 * @file IDeviceObserver.h
 * @brief Device模块观察者接口
 *
 * 用于监听设备连接状态变化、错误事件等。
 * 注意: 视频帧/图片数据通过Callback传递,不在Observer中。
 */

#ifndef ESDK_SOPHON_IDEVICE_OBSERVER_H_
#define ESDK_SOPHON_IDEVICE_OBSERVER_H_

#include <string>

namespace esdk_sophon {
namespace device {

/**
 * @brief 设备信息结构体
 */
struct DeviceInfo {
    std::string firmwareVersion;  ///< 固件版本
    std::string productName;      ///< 产品名称
    std::string serialNumber;     ///< 序列号
    std::string vendorName;       ///< 厂商名称
};

/**
 * @brief 设备观察者接口
 *
 * 实现此接口以接收设备状态通知。
 * 生命周期由外部管理,DeviceManager使用裸指针。
 */
class IDeviceObserver {
public:
    virtual ~IDeviceObserver() = default;

    /**
     * @brief 设备连接成功
     * @param info 设备信息
     */
    virtual void onDeviceConnected(const DeviceInfo& info) = 0;

    /**
     * @brief 设备断开连接
     * @param reason 断开原因(如"网络超时","手动断开")
     */
    virtual void onDeviceDisconnected(const std::string& reason) = 0;

    /**
     * @brief 视频流状态变化
     * @param available 是否可用
     * @param quality 当前流质量(如"720p")
     */
    virtual void onLiveviewStatusChanged(bool available,
                                        const std::string& quality) {}

    /**
     * @brief 设备错误
     * @param errorCode Edge-SDK错误码
     * @param message 错误消息
     */
    virtual void onDeviceError(int errorCode,
                              const std::string& message) = 0;
};

}  // namespace device
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_IDEVICE_OBSERVER_H_
```

### 2. DeviceManager.h (公共接口)

```cpp
/**
 * @file DeviceManager.h
 * @brief Device管理器公共接口
 *
 * 封装Edge-SDK,提供两种数据源:
 * 1. 实时视频流 (Liveview → cv::Mat)
 * 2. 媒体文件 (MediaManager → cv::Mat)
 */

#ifndef ESDK_SOPHON_DEVICE_MANAGER_H_
#define ESDK_SOPHON_DEVICE_MANAGER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

#include "esdk_sophon/device/IDeviceObserver.h"

namespace esdk_sophon {
namespace device {

/**
 * @brief 媒体文件信息
 */
struct MediaFileInfo {
    std::string fileId;       ///< 文件唯一标识(用于下载)
    std::string fileName;     ///< 文件名(如"DJI_20251027_143022.JPG")
    std::string filePath;     ///< Edge-SDK返回的文件路径
    size_t fileSize;          ///< 文件大小(字节)
    int32_t width;            ///< 图片宽度(像素)
    int32_t height;           ///< 图片高度(像素)
    time_t createTime;        ///< 创建时间(Unix时间戳)
    std::string cameraType;   ///< 相机类型("Wide"/"Zoom"/"IR")
};

/**
 * @brief Device管理器(单例)
 *
 * 线程安全,负责:
 * - SDK初始化和设备连接
 * - 提供实时视频流(H264 → cv::Mat)
 * - 提供媒体文件读取(JPEG → cv::Mat)
 * - 通知观察者状态变化
 *
 * @note Pimpl模式,隐藏Edge-SDK实现细节
 */
class DeviceManager {
public:
    /**
     * @brief 获取单例实例
     * @return DeviceManager引用
     */
    static DeviceManager& getInstance();

    /**
     * @brief 析构函数
     */
    ~DeviceManager();

    // 禁止拷贝和赋值
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    /* ========== SDK生命周期 ========== */

    /**
     * @brief 初始化SDK
     *
     * @return true 初始化成功
     * @return false 初始化失败(见日志)
     *
     * @note 从config.json读取Edge-SDK配置
     * @note 调用ESDKInit::Init()
     */
    bool initialize();

    /**
     * @brief 连接设备
     *
     * @return true 连接成功
     * @return false 连接失败
     *
     * @note 连接成功后触发onDeviceConnected()
     */
    bool connect();

    /**
     * @brief 断开设备连接
     *
     * @note 会停止所有视频流
     * @note 触发onDeviceDisconnected()
     */
    void disconnect();

    /**
     * @brief 检查设备是否已连接
     *
     * @return true 已连接DJI Dock
     * @return false 未连接
     *
     * @warning 这是设备连接,不是MQTT连接!
     */
    bool isConnected() const;

    /* ========== 数据源1: 实时视频流 ========== */

    /**
     * @brief 实时帧回调类型
     * @param frame 已解码的BGR图像(cv::Mat)
     */
    using LiveviewCallback = std::function<void(const cv::Mat& frame)>;

    /**
     * @brief 启动实时视频流
     *
     * @param callback 帧到达时的回调函数
     * @return true 启动成功
     * @return false 启动失败(设备未连接/重复启动)
     *
     * 工作流程:
     * 1. 从config.json读取相机类型(FPV/Payload)和分辨率(720p)
     * 2. 订阅H264流 (Liveview::StartH264Stream)
     * 3. 内部解码H264 → cv::Mat
     * 4. 调用callback传递给上层
     *
     * @note callback在内部解码线程执行,注意线程安全
     * @note 重复调用会返回false
     */
    bool startLiveview(LiveviewCallback callback);

    /**
     * @brief 停止实时视频流
     *
     * @note 调用Liveview::StopH264Stream()
     */
    void stopLiveview();

    /**
     * @brief 检查视频流是否正在运行
     *
     * @return true 正在接收视频帧
     * @return false 已停止
     */
    bool isLiveviewRunning() const;

    /* ========== 数据源2: 媒体文件 ========== */

    /**
     * @brief 列出所有媒体文件
     *
     * @return vector<MediaFileInfo> 文件列表
     *
     * @note 调用MediaFilesReader::FileList()
     * @note 只返回JPEG文件(过滤掉MP4)
     */
    std::vector<MediaFileInfo> listMediaFiles();

    /**
     * @brief 媒体文件回调类型
     * @param image 已解码的BGR图像(cv::Mat)
     */
    using MediaFileCallback = std::function<void(const cv::Mat& image)>;

    /**
     * @brief 下载并解码媒体文件
     *
     * @param fileId 文件ID(来自listMediaFiles())
     * @param callback 解码完成后的回调
     * @return true 下载成功
     * @return false 下载失败
     *
     * 工作流程:
     * 1. MediaFilesReader::Open(fileId)
     * 2. Read()读取全部内容到内存
     * 3. cv::imdecode()解码JPEG
     * 4. 调用callback传递cv::Mat
     * 5. Close()关闭文件
     *
     * @note callback在当前线程执行(同步操作)
     */
    bool downloadMediaFile(const std::string& fileId,
                          MediaFileCallback callback);

    /* ========== 观察者模式 ========== */

    /**
     * @brief 添加观察者
     * @param observer 观察者指针(生命周期由外部管理)
     *
     * @note 使用裸指针,不接管所有权
     * @note 线程安全(内部mutex保护)
     */
    void addObserver(IDeviceObserver* observer);

    /**
     * @brief 移除观察者
     * @param observer 观察者指针
     *
     * @note 必须在observer析构前调用
     */
    void removeObserver(IDeviceObserver* observer);

private:
    /**
     * @brief 私有构造函数(单例模式)
     */
    DeviceManager();

    /**
     * @brief Pimpl实现类(前向声明)
     */
    class Impl;
    std::unique_ptr<Impl> pImpl_;  ///< Pimpl指针(隐藏Edge-SDK细节)
};

}  // namespace device
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_DEVICE_MANAGER_H_
```

---

## 🔄 工作流程

### 流程 1: 实时视频流分析

```
云端下发指令:
{
  "method": "device_algorithm_enable",
  "source": 0,
  "algorithm_type": "object_detection"
}

         ↓

main.cpp 接收MQTT消息:
┌─────────────────────────────────────┐
│ CommandHandler::onMessageReceived()│
│  if (source == 0) {                 │
│    DeviceManager::startLiveview()   │
│  }                                   │
└─────────────────────────────────────┘

         ↓

DeviceManager 内部:
┌─────────────────────────────────────┐
│ 1. Liveview::Init(option)           │
│    - camera: kCameraTypePayload     │
│    - quality: kStreamQuality720p    │
│    - callback: onH264Callback       │
│                                      │
│ 2. Liveview::StartH264Stream()      │
│                                      │
│ 3. onH264Callback每帧触发:          │
│    - decodeH264ToMat(buf, len)      │
│    - liveviewCallback_(mat)         │
└─────────────────────────────────────┘

         ↓

Vision模块接收:
┌─────────────────────────────────────┐
│ callback(cv::Mat frame) {           │
│   auto results = detector_->detect()│
│   MqttClient::publish(results)      │
│ }                                    │
└─────────────────────────────────────┘
```

### 流程 2: 离线图片分析

```
云端下发指令:
{
  "method": "device_algorithm_enable",
  "source": 1,
  "algorithm_type": "segmentation"
}

         ↓

main.cpp 接收MQTT消息:
┌─────────────────────────────────────┐
│ CommandHandler::onMessageReceived()│
│  if (source == 1) {                 │
│    auto files = listMediaFiles()    │
│    for (auto& file : files) {       │
│      downloadMediaFile(file.id)     │
│    }                                 │
│  }                                   │
└─────────────────────────────────────┘

         ↓

DeviceManager 内部:
┌─────────────────────────────────────┐
│ 1. MediaManager::CreateReader()     │
│ 2. reader->Init()                   │
│ 3. reader->FileList(list)           │
│                                      │
│ 4. 对每个文件:                       │
│    - fd = Open(filePath)            │
│    - Read(fd, buffer, size)         │
│    - cv::imdecode(buffer)           │
│    - mediaFileCallback_(mat)        │
│    - Close(fd)                      │
└─────────────────────────────────────┘

         ↓

Vision模块接收:
┌─────────────────────────────────────┐
│ callback(cv::Mat image) {           │
│   auto results = segmentor_->run()  │
│   MqttClient::publish(results)      │
│ }                                    │
└─────────────────────────────────────┘
```

---

## 🧩 Pimpl 实现(DeviceManager::Impl)

```cpp
// src/device/DeviceManager.cpp

class DeviceManager::Impl {
public:
    Impl()
        : logger_(Logger::getInstance()),
          config_(Config::getInstance()),
          connected_(false),
          liveviewRunning_(false),
          esdkInit_(nullptr),
          liveview_(nullptr),
          mediaManager_(nullptr),
          mediaReader_(nullptr) {}

    ~Impl() {
        disconnect();
    }

    bool initialize() {
        // 1. 获取ESDKInit单例
        esdkInit_ = edge_sdk::ESDKInit::Instance();
        if (!esdkInit_) {
            logger_.error("获取ESDKInit失败");
            return false;
        }

        // 2. 从config.json读取Options
        edge_sdk::Options options;
        if (!loadESDKOptions(options)) {
            logger_.error("加载ESDK配置失败,使用默认值");
            // 设置默认值...
        }

        // 3. 初始化SDK
        auto rc = esdkInit_->Init(options);
        if (rc != edge_sdk::kOk) {
            logger_.error("ESDKInit::Init失败: " + std::to_string(rc));
            return false;
        }

        logger_.info("Edge-SDK初始化成功");
        logger_.info("固件版本: " + esdkInit_->GetFirmwareVersion());
        logger_.info("产品名称: " + esdkInit_->GetProductName());

        return true;
    }

    bool connect() {
        if (connected_) {
            logger_.warn("设备已连接,无需重复连接");
            return true;
        }

        // Edge-SDK初始化后自动连接,无需额外connect()
        // 这里检查设备是否ready

        connected_ = true;

        // 通知观察者
        DeviceInfo info;
        info.firmwareVersion = esdkInit_->GetFirmwareVersion();
        info.productName = esdkInit_->GetProductName();
        info.serialNumber = esdkInit_->GetSerialNumber();
        info.vendorName = esdkInit_->GetVendorName();

        notifyDeviceConnected(info);

        return true;
    }

    void disconnect() {
        if (!connected_) return;

        stopLiveview();

        if (esdkInit_) {
            esdkInit_->DeInit();
        }

        connected_ = false;
        notifyDeviceDisconnected("用户主动断开");
    }

    bool startLiveview(LiveviewCallback callback) {
        if (!connected_) {
            logger_.error("设备未连接,无法启动视频流");
            return false;
        }

        if (liveviewRunning_) {
            logger_.warn("视频流已在运行");
            return false;
        }

        // 1. 创建Liveview实例
        liveview_ = edge_sdk::CreateLiveview();

        // 2. 保存用户回调
        liveviewCallback_ = callback;

        // 3. 定义H264回调(使用lambda捕获this)
        edge_sdk::Liveview::H264Callback h264Callback =
            [this](const uint8_t* buf, uint32_t len) -> edge_sdk::ErrorCode {
                return this->onH264StreamCallback(buf, len);
            };

        // 4. 从config读取参数
        auto cameraType = edge_sdk::Liveview::kCameraTypePayload;
        auto quality = edge_sdk::Liveview::kStreamQuality720p;
        // ... 从config_.get("device.camera_type")读取

        // 5. 初始化Liveview
        edge_sdk::Liveview::Options option = {
            .camera = cameraType,
            .quality = quality,
            .callback = h264Callback
        };

        auto rc = liveview_->Init(option);
        if (rc != edge_sdk::kOk) {
            logger_.error("Liveview初始化失败: " + std::to_string(rc));
            return false;
        }

        // 6. 启动H264流
        rc = liveview_->StartH264Stream();
        if (rc != edge_sdk::kOk) {
            logger_.error("启动H264流失败: " + std::to_string(rc));
            liveview_->DeInit();
            return false;
        }

        liveviewRunning_ = true;
        logger_.info("实时视频流已启动");

        return true;
    }

    void stopLiveview() {
        if (!liveviewRunning_) return;

        if (liveview_) {
            liveview_->StopH264Stream();
            liveview_->DeInit();
            liveview_ = nullptr;
        }

        liveviewRunning_ = false;
        logger_.info("视频流已停止");
    }

    std::vector<MediaFileInfo> listMediaFiles() {
        std::vector<MediaFileInfo> result;

        // 1. 获取MediaManager单例
        mediaManager_ = edge_sdk::MediaManager::Instance();
        if (!mediaManager_) {
            logger_.error("获取MediaManager失败");
            return result;
        }

        // 2. 创建MediaFilesReader
        mediaReader_ = mediaManager_->CreateMediaFilesReader();
        if (!mediaReader_) {
            logger_.error("创建MediaFilesReader失败");
            return result;
        }

        // 3. 初始化
        auto rc = mediaReader_->Init();
        if (rc != edge_sdk::kOk) {
            logger_.error("MediaFilesReader初始化失败");
            return result;
        }

        // 4. 获取文件列表
        edge_sdk::MediaFilesReader::MediaFileList fileList;
        int32_t count = mediaReader_->FileList(fileList);

        logger_.info("发现" + std::to_string(count) + "个媒体文件");

        // 5. 转换为MediaFileInfo
        for (auto& file : fileList) {
            if (file->file_type == edge_sdk::MediaFile::kFileTypeJpeg) {
                MediaFileInfo info;
                info.fileId = file->file_path;  // 使用file_path作为ID
                info.fileName = file->file_name;
                info.filePath = file->file_path;
                info.fileSize = file->file_size;
                info.width = file->image_width;
                info.height = file->image_height;
                info.createTime = file->create_time;

                // 转换相机类型
                switch (file->camera_attr) {
                    case edge_sdk::MediaFile::kCameraAttrWide:
                        info.cameraType = "Wide";
                        break;
                    case edge_sdk::MediaFile::kCameraAttrZoom:
                        info.cameraType = "Zoom";
                        break;
                    case edge_sdk::MediaFile::kCameraAttrInfrared:
                        info.cameraType = "IR";
                        break;
                    default:
                        info.cameraType = "Unknown";
                }

                result.push_back(info);
            }
        }

        return result;
    }

    bool downloadMediaFile(const std::string& fileId, MediaFileCallback callback) {
        if (!mediaReader_) {
            logger_.error("MediaFilesReader未初始化");
            return false;
        }

        // 1. 打开文件
        auto fd = mediaReader_->Open(fileId);
        if (fd < 0) {
            logger_.error("打开文件失败: " + fileId);
            return false;
        }

        // 2. 获取文件大小(需要从之前的list中查找)
        // 简化: 假设最大10MB
        const size_t MAX_SIZE = 10 * 1024 * 1024;
        std::vector<uint8_t> buffer(MAX_SIZE);

        // 3. 读取文件
        size_t bytesRead = mediaReader_->Read(fd, buffer.data(), MAX_SIZE);
        if (bytesRead == 0) {
            logger_.error("读取文件失败");
            mediaReader_->Close(fd);
            return false;
        }

        // 4. 关闭文件
        mediaReader_->Close(fd);

        // 5. 解码JPEG
        cv::Mat image = cv::imdecode(
            cv::Mat(1, bytesRead, CV_8UC1, buffer.data()),
            cv::IMREAD_COLOR
        );

        if (image.empty()) {
            logger_.error("解码JPEG失败");
            return false;
        }

        logger_.info("成功解码图片: " +
                    std::to_string(image.cols) + "x" +
                    std::to_string(image.rows));

        // 6. 调用用户回调
        callback(image);

        return true;
    }

    void addObserver(IDeviceObserver* observer) {
        std::lock_guard<std::mutex> lock(observersMutex_);
        observers_.push_back(observer);
    }

    void removeObserver(IDeviceObserver* observer) {
        std::lock_guard<std::mutex> lock(observersMutex_);
        observers_.erase(
            std::remove(observers_.begin(), observers_.end(), observer),
            observers_.end()
        );
    }

private:
    edge_sdk::ErrorCode onH264StreamCallback(const uint8_t* buf, uint32_t len) {
        try {
            // 解码H264 → cv::Mat
            cv::Mat frame = decodeH264ToMat(buf, len);

            if (frame.empty()) {
                logger_.warn("H264解码失败");
                return edge_sdk::kOk;  // 继续接收下一帧
            }

            // 调用用户回调
            if (liveviewCallback_) {
                liveviewCallback_(frame);
            }

            return edge_sdk::kOk;
        } catch (const std::exception& e) {
            logger_.error("H264回调异常: " + std::string(e.what()));
            return edge_sdk::kErrorInvalidOperation;
        }
    }

    cv::Mat decodeH264ToMat(const uint8_t* buf, uint32_t len) {
        // TODO: 使用FFmpeg解码H264
        // 参考Edge-SDK/examples/liveview/ffmpeg_stream_decoder.cc

        // 临时返回空Mat(待实现)
        return cv::Mat();
    }

    void notifyDeviceConnected(const DeviceInfo& info) {
        std::lock_guard<std::mutex> lock(observersMutex_);
        for (auto* observer : observers_) {
            try {
                observer->onDeviceConnected(info);
            } catch (const std::exception& e) {
                logger_.error("观察者异常: " + std::string(e.what()));
            }
        }
    }

    void notifyDeviceDisconnected(const std::string& reason) {
        std::lock_guard<std::mutex> lock(observersMutex_);
        for (auto* observer : observers_) {
            try {
                observer->onDeviceDisconnected(reason);
            } catch (const std::exception& e) {
                logger_.error("观察者异常: " + std::string(e.what()));
            }
        }
    }

    bool loadESDKOptions(edge_sdk::Options& options) {
        // TODO: 从config_.get("device.esdk.*")读取参数
        return false;
    }

private:
    Logger& logger_;
    Config& config_;

    std::atomic<bool> connected_;
    std::atomic<bool> liveviewRunning_;

    // Edge-SDK组件
    edge_sdk::ESDKInit* esdkInit_;
    std::shared_ptr<edge_sdk::Liveview> liveview_;
    edge_sdk::MediaManager* mediaManager_;
    std::shared_ptr<edge_sdk::MediaFilesReader> mediaReader_;

    // 回调
    LiveviewCallback liveviewCallback_;

    // 观察者
    std::vector<IDeviceObserver*> observers_;
    std::mutex observersMutex_;
};
```

---

## ⏭️ 下一步

1. **创建头文件** - IDeviceObserver.h, DeviceManager.h
2. **实现 DeviceManager.cpp** - 完成 Pimpl 实现
3. **H264 解码器** - 参考 Edge-SDK 示例,集成 FFmpeg
4. **CMake 配置** - 链接 Edge-SDK 库
5. **编写测试** - test_device.cpp (6 个测试用例)
6. **编译验证** - Docker 交叉编译

---

**文档结束**
