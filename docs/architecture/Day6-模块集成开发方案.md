# Day 6: 模块集成开发方案

**日期**: 2025-11-10  
**目标**: 替换临时实现,集成真实的 Device 模块、DJI SDK 和 RTMP 推流功能  
**前置条件**: Day 5 任务运行时功能已完成并测试通过

---

## 📋 需求分析

### 当前临时实现位置

#### 1. LiveStreamTask 临时实现

**位置**: `src/task/LiveStreamTask.cpp`

```cpp
// Line 266-276: initVideoStream()
bool LiveStreamTask::initVideoStream() {
    // 临时实现: 使用 OpenCV 的 VideoCapture
    logger_.warning("视频流连接功能尚未集成 Device 模块，使用临时实现");
    return true;
}

// Line 282-300: getNextFrame()
bool LiveStreamTask::getNextFrame(cv::Mat& frame) {
    // 临时实现：生成模拟帧（用于测试）
    frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(0, 0, 255));  // 红色帧
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
    return true;
}

// Line 307-312: pushToRTMP()
void LiveStreamTask::pushToRTMP(const cv::Mat& frame) {
    // 临时实现：仅记录日志
    logger_.debug("RTMP 推流功能尚未实现");
}
```

#### 2. MediaFileTask 临时实现

**位置**: `src/task/MediaFileTask.cpp`

```cpp
// Line 314-315: registerMediaFilesObserver()
void MediaFileTask::registerMediaFilesObserver() {
    // 临时实现：模拟注册成功
    logger_.warning("MediaFilesObserver 注册功能尚未集成 DJI SDK，使用临时实现");
}

// Line 380: readMediaFile()
void MediaFileTask::readMediaFile(const std::string& filePath) {
    // 临时实现：从本地文件系统读取
}
```

---

## 🎯 集成目标

### 目标 1: Device 模块集成 (LiveStreamTask)

**功能**: 替换 OpenCV 模拟实现,使用真实的 DJI 设备视频流

**涉及的 DJI SDK API**:

- `edge_sdk::Liveview` 类 (来自 `liveview.h`)
- `Liveview::Init()` - 初始化视频流订阅
- `Liveview::DeInit()` - 反初始化
- `H264Callback` - H264 流回调函数

**关键类**:

```cpp
class edge_sdk::Liveview {
public:
    struct Options {
        CameraType camera;         // FPV or Payload
        StreamQuality quality;     // 540p, 720p, 1080p 等
        H264Callback callback;     // H264 流回调
    };

    ErrorCode Init(const Options& option);
    ErrorCode DeInit();
    ErrorCode SetCameraSource(CameraSource source);
};
```

### 目标 2: DJI SDK MediaManager 集成 (MediaFileTask)

**功能**: 实现真实的媒体文件监听和读取

**涉及的 DJI SDK API**:

- `edge_sdk::MediaManager` 类 (来自 `media_manager.h`)
- `MediaManager::RegisterMediaFilesObserver()` - 注册观察者
- `MediaManager::CreateMediaFilesReader()` - 创建文件读取器
- `MediaFilesObserver` - 文件更新回调

**关键类**:

```cpp
class edge_sdk::MediaManager {
public:
    static MediaManager* Instance();

    // 注册文件通知回调
    ErrorCode RegisterMediaFilesObserver(MediaFilesObserver observer);

    // 创建文件读取器
    std::shared_ptr<MediaFilesReader> CreateMediaFilesReader();

    // 设置是否上传到云端
    ErrorCode SetDroneNestUploadCloud(bool enable);

    // 设置是否自动删除
    ErrorCode SetDroneNestAutoDelete(bool enable);
};
```

### 目标 3: RTMP 推流功能实现

**功能**: 将检测后的视频帧推流到 RTMP 服务器

**技术选型**: FFmpeg

- 编码器: libx264 (H.264 编码)
- 封装格式: FLV (RTMP 标准格式)
- 推流协议: RTMP

**需要实现的类**:

```cpp
class RtmpStreamer {
public:
    RtmpStreamer(const std::string& url, int width, int height, int fps);
    ~RtmpStreamer();

    bool initialize();
    bool pushFrame(const cv::Mat& frame);
    void close();

private:
    std::string rtmpUrl_;
    int width_, height_, fps_;

    // FFmpeg 相关
    AVFormatContext* formatContext_{nullptr};
    AVCodecContext* codecContext_{nullptr};
    AVStream* stream_{nullptr};
    AVFrame* frame_{nullptr};
    SwsContext* swsContext_{nullptr};
};
```

---

## 🔧 实现方案

### 方案 1: LiveStreamTask - Liveview 集成

#### 1.1 修改 LiveStreamTask.h

**添加成员变量**:

```cpp
#include "liveview.h"  // DJI SDK

class LiveStreamTask {
private:
    // DJI Liveview
    std::unique_ptr<edge_sdk::Liveview> liveview_;

    // H264 数据缓冲区
    std::mutex h264Mutex_;
    std::queue<std::vector<uint8_t>> h264Queue_;
    std::condition_variable h264Condition_;

    // H264 回调
    edge_sdk::ErrorCode onH264Data(const uint8_t* buf, uint32_t len);

    // H264 解码到 cv::Mat (如果需要本地检测)
    bool decodeH264ToMat(const std::vector<uint8_t>& h264Data, cv::Mat& frame);
};
```

#### 1.2 修改 initVideoStream()

```cpp
bool LiveStreamTask::initVideoStream() {
    logger_.info("初始化视频流连接: taskId=" + config_.taskId);

    // 创建 Liveview 实例
    liveview_ = std::make_unique<edge_sdk::Liveview>();

    // 配置 Liveview 参数
    edge_sdk::Liveview::Options options;
    options.camera = edge_sdk::Liveview::kCameraTypePayload;  // 负载相机
    options.quality = edge_sdk::Liveview::kStreamQuality720p; // 720p
    options.callback = [this](const uint8_t* buf, uint32_t len) {
        return this->onH264Data(buf, len);
    };

    // 初始化 Liveview
    edge_sdk::ErrorCode ret = liveview_->Init(options);
    if (ret != edge_sdk::kErrorCodeSuccess) {
        logger_.error("Liveview 初始化失败: " + std::to_string(static_cast<int>(ret)));
        return false;
    }

    logger_.info("Liveview 初始化成功: taskId=" + config_.taskId);
    return true;
}
```

#### 1.3 实现 H264 回调

```cpp
edge_sdk::ErrorCode LiveStreamTask::onH264Data(const uint8_t* buf, uint32_t len) {
    // 将 H264 数据加入队列
    {
        std::lock_guard<std::mutex> lock(h264Mutex_);
        h264Queue_.push(std::vector<uint8_t>(buf, buf + len));
    }
    h264Condition_.notify_one();

    return edge_sdk::kErrorCodeSuccess;
}
```

#### 1.4 修改 getNextFrame()

**方案 A: 直接推流 H264 (推荐)**

```cpp
bool LiveStreamTask::getNextFrame(std::vector<uint8_t>& h264Data) {
    std::unique_lock<std::mutex> lock(h264Mutex_);

    // 等待 H264 数据
    h264Condition_.wait(lock, [this] {
        return !h264Queue_.empty() || shouldStop_.load();
    });

    if (shouldStop_.load()) return false;

    if (!h264Queue_.empty()) {
        h264Data = std::move(h264Queue_.front());
        h264Queue_.pop();
        return true;
    }

    return false;
}
```

**方案 B: 解码为 cv::Mat (如果需要本地检测)**

```cpp
bool LiveStreamTask::getNextFrame(cv::Mat& frame) {
    std::vector<uint8_t> h264Data;
    if (!getNextH264Data(h264Data)) {
        return false;
    }

    // 使用 FFmpeg 解码 H264 为 cv::Mat
    return decodeH264ToMat(h264Data, frame);
}
```

### 方案 2: MediaFileTask - MediaManager 集成

#### 2.1 修改 MediaFileTask.h

```cpp
#include "media_manager.h"  // DJI SDK

class MediaFileTask {
private:
    // MediaManager 相关
    edge_sdk::MediaManager* mediaManager_{nullptr};
    std::shared_ptr<edge_sdk::MediaFilesReader> mediaReader_;

    // 媒体文件观察者回调
    edge_sdk::ErrorCode onMediaFileUpdate(const edge_sdk::MediaFile& file);
};
```

#### 2.2 修改 registerMediaFilesObserver()

```cpp
void MediaFileTask::registerMediaFilesObserver() {
    logger_.info("注册 MediaFilesObserver: taskId=" + config_.taskId);

    // 获取 MediaManager 单例
    mediaManager_ = edge_sdk::MediaManager::Instance();

    // 注册观察者回调
    auto observer = [this](const edge_sdk::MediaFile& file) {
        return this->onMediaFileUpdate(file);
    };

    edge_sdk::ErrorCode ret = mediaManager_->RegisterMediaFilesObserver(observer);
    if (ret != edge_sdk::kErrorCodeSuccess) {
        logger_.error("MediaFilesObserver 注册失败: " + std::to_string(static_cast<int>(ret)));
        return;
    }

    // 创建文件读取器
    mediaReader_ = mediaManager_->CreateMediaFilesReader();
    if (!mediaReader_) {
        logger_.error("创建 MediaFilesReader 失败");
        return;
    }

    // 配置:不上传到云端,不自动删除
    mediaManager_->SetDroneNestUploadCloud(false);
    mediaManager_->SetDroneNestAutoDelete(false);

    logger_.info("MediaFilesObserver 注册成功: taskId=" + config_.taskId);
}
```

#### 2.3 实现 onMediaFileUpdate()

```cpp
edge_sdk::ErrorCode MediaFileTask::onMediaFileUpdate(const edge_sdk::MediaFile& file) {
    logger_.info("收到媒体文件通知: " + file.fileName);

    // 加入文件队列 (生产者)
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        fileQueue_.push(file.fileName);  // 或 file.filePath
    }
    queueCondition_.notify_one();

    return edge_sdk::kErrorCodeSuccess;
}
```

#### 2.4 修改 readMediaFile()

```cpp
void MediaFileTask::readMediaFile(const std::string& filePath) {
    logger_.info("开始读取媒体文件: " + filePath);

    if (!mediaReader_) {
        logger_.error("MediaFilesReader 未初始化");
        return;
    }

    // 使用 MediaFilesReader 读取文件
    std::vector<uint8_t> fileData;
    edge_sdk::ErrorCode ret = mediaReader_->ReadFile(filePath, fileData);

    if (ret != edge_sdk::kErrorCodeSuccess) {
        logger_.error("读取文件失败: " + filePath);
        return;
    }

    // 解析文件数据 (图片或视频)
    cv::Mat image = cv::imdecode(fileData, cv::IMREAD_COLOR);
    if (image.empty()) {
        logger_.error("解码图片失败: " + filePath);
        return;
    }

    // 处理图片 (检测、上报)
    processFrame(image);
}
```

### 方案 3: RTMP 推流器实现

#### 3.1 创建 RtmpStreamer.h

```cpp
/**
 * @file RtmpStreamer.h
 * @brief RTMP 推流器类 - 基于 FFmpeg 实现
 *
 * 功能:
 * - H.264 编码
 * - FLV 封装
 * - RTMP 推流
 *
 * @author ESDK Sophon Team
 * @date 2025-11-10 (Day 6)
 */

#ifndef ESDK_SOPHON_RTMP_STREAMER_H_
#define ESDK_SOPHON_RTMP_STREAMER_H_

#include <string>
#include <opencv2/opencv.hpp>

// FFmpeg C API
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

namespace esdk_sophon {
namespace rtmp {

/**
 * @brief RTMP 推流器配置
 */
struct RtmpStreamerConfig {
    std::string url;      ///< RTMP 推流地址 (如: rtmp://server/live/stream)
    int width;            ///< 视频宽度
    int height;           ///< 视频高度
    int fps;              ///< 帧率
    int bitrate;          ///< 码率 (bps)
};

/**
 * @brief RTMP 推流器
 *
 * 使用 FFmpeg 实现 H.264 编码和 RTMP 推流。
 *
 * 使用示例:
 * @code
 * RtmpStreamerConfig config;
 * config.url = "rtmp://192.168.1.100/live/test";
 * config.width = 1280;
 * config.height = 720;
 * config.fps = 30;
 * config.bitrate = 2000000;  // 2 Mbps
 *
 * RtmpStreamer streamer(config);
 * if (streamer.initialize()) {
 *     while (running) {
 *         cv::Mat frame = getFrame();
 *         streamer.pushFrame(frame);
 *     }
 *     streamer.close();
 * }
 * @endcode
 */
class RtmpStreamer {
public:
    /**
     * @brief 构造函数
     * @param config 推流配置
     */
    explicit RtmpStreamer(const RtmpStreamerConfig& config);

    /**
     * @brief 析构函数 (RAII 自动清理资源)
     */
    ~RtmpStreamer();

    // 禁止拷贝和赋值
    RtmpStreamer(const RtmpStreamer&) = delete;
    RtmpStreamer& operator=(const RtmpStreamer&) = delete;

    /**
     * @brief 初始化推流器
     * @return true 成功, false 失败
     */
    bool initialize();

    /**
     * @brief 推送一帧视频
     * @param frame OpenCV 图像帧 (BGR 格式)
     * @return true 成功, false 失败
     */
    bool pushFrame(const cv::Mat& frame);

    /**
     * @brief 关闭推流器
     */
    void close();

    /**
     * @brief 检查推流器是否已初始化
     */
    bool isInitialized() const { return initialized_; }

private:
    /**
     * @brief 初始化编码器
     */
    bool initEncoder();

    /**
     * @brief 初始化输出格式
     */
    bool initOutputFormat();

    /**
     * @brief 编码并发送帧
     */
    bool encodeAndSend(AVFrame* frame);

    // 配置
    RtmpStreamerConfig config_;

    // 状态
    bool initialized_{false};
    int64_t frameCount_{0};

    // FFmpeg 组件
    AVFormatContext* formatContext_{nullptr};
    AVCodecContext* codecContext_{nullptr};
    AVStream* stream_{nullptr};
    AVFrame* frame_{nullptr};
    AVPacket* packet_{nullptr};
    SwsContext* swsContext_{nullptr};
};

}  // namespace rtmp
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_RTMP_STREAMER_H_
```

#### 3.2 RtmpStreamer 实现要点

**初始化流程**:

```cpp
bool RtmpStreamer::initialize() {
    // 1. 注册所有编解码器
    //av_register_all();  // FFmpeg 4.0+ 不需要
    avformat_network_init();

    // 2. 分配输出格式上下文
    avformat_alloc_output_context2(&formatContext_, nullptr, "flv", config_.url.c_str());

    // 3. 查找 H.264 编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);

    // 4. 分配编码器上下文
    codecContext_ = avcodec_alloc_context3(codec);
    codecContext_->width = config_.width;
    codecContext_->height = config_.height;
    codecContext_->time_base = {1, config_.fps};
    codecContext_->framerate = {config_.fps, 1};
    codecContext_->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext_->bit_rate = config_.bitrate;

    // 5. 打开编码器
    avcodec_open2(codecContext_, codec, nullptr);

    // 6. 创建流
    stream_ = avformat_new_stream(formatContext_, nullptr);
    avcodec_parameters_from_context(stream_->codecpar, codecContext_);

    // 7. 打开输出
    avio_open(&formatContext_->pb, config_.url.c_str(), AVIO_FLAG_WRITE);
    avformat_write_header(formatContext_, nullptr);

    // 8. 初始化 SWS 上下文 (BGR -> YUV420P)
    swsContext_ = sws_getContext(
        config_.width, config_.height, AV_PIX_FMT_BGR24,
        config_.width, config_.height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    initialized_ = true;
    return true;
}
```

**推送帧流程**:

```cpp
bool RtmpStreamer::pushFrame(const cv::Mat& frame) {
    // 1. 将 cv::Mat (BGR) 转换为 AVFrame (YUV420P)
    uint8_t* inData[1] = {frame.data};
    int inLinesize[1] = {static_cast<int>(frame.step[0])};

    sws_scale(swsContext_, inData, inLinesize, 0, frame.rows,
              frame_->data, frame_->linesize);

    // 2. 设置帧 PTS
    frame_->pts = frameCount_++;

    // 3. 编码
    int ret = avcodec_send_frame(codecContext_, frame_);
    if (ret < 0) return false;

    // 4. 接收编码后的数据包
    while (ret >= 0) {
        ret = avcodec_receive_packet(codecContext_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;

        // 5. 写入输出
        av_packet_rescale_ts(packet_, codecContext_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;
        av_interleaved_write_frame(formatContext_, packet_);
        av_packet_unref(packet_);
    }

    return true;
}
```

#### 3.3 在 LiveStreamTask 中集成 RtmpStreamer

```cpp
// LiveStreamTask.h
#include "esdk_sophon/rtmp/RtmpStreamer.h"

class LiveStreamTask {
private:
    std::unique_ptr<rtmp::RtmpStreamer> rtmpStreamer_;
};

// LiveStreamTask.cpp - start()
bool LiveStreamTask::start() {
    // ... 现有代码 ...

    // 初始化 RTMP 推流器 (如果配置了推流地址)
    if (!config_.rtmpUrl.empty()) {
        rtmp::RtmpStreamerConfig rtmpConfig;
        rtmpConfig.url = config_.rtmpUrl;
        rtmpConfig.width = 1280;  // 从配置读取
        rtmpConfig.height = 720;
        rtmpConfig.fps = 30;
        rtmpConfig.bitrate = 2000000;

        rtmpStreamer_ = std::make_unique<rtmp::RtmpStreamer>(rtmpConfig);
        if (!rtmpStreamer_->initialize()) {
            logger_.error("RTMP 推流器初始化失败");
            // 继续运行,但不推流
            rtmpStreamer_.reset();
        }
    }

    // ...
}

// LiveStreamTask.cpp - processLoop()
void LiveStreamTask::processLoop() {
    while (!shouldStop_.load()) {
        // ... 检查暂停 ...

        // 获取帧
        cv::Mat frame;
        if (!getNextFrame(frame)) continue;

        // 检测
        auto result = service_->processFrame(frame, config_);

        // 可视化
        if (config_.enableVisualization && result.hasDetection) {
            service_->drawDetections(frame, result.detections);
        }

        // 推流
        if (rtmpStreamer_ && rtmpStreamer_->isInitialized()) {
            rtmpStreamer_->pushFrame(frame);
        }

        // 更新统计...
    }
}
```

---

## 📝 实现步骤

### Step 1: 准备工作 ✅

- [x] 分析现有临时实现
- [x] 研究 DJI SDK 接口
- [x] 设计集成方案

### Step 2: Device 模块集成

- [ ] 修改 LiveStreamTask.h 添加 Liveview 成员
- [ ] 实现 initVideoStream() - Liveview 初始化
- [ ] 实现 onH264Data() - H264 回调
- [ ] 实现 getNextFrame() - H264 队列读取
- [ ] 测试视频流接收

### Step 3: DJI SDK MediaManager 集成

- [ ] 修改 MediaFileTask.h 添加 MediaManager 成员
- [ ] 实现 registerMediaFilesObserver() - 注册观察者
- [ ] 实现 onMediaFileUpdate() - 文件通知回调
- [ ] 实现 readMediaFile() - 使用 MediaFilesReader
- [ ] 测试文件通知和读取

### Step 4: RTMP 推流器实现

- [ ] 创建 RtmpStreamer.h/cpp
- [ ] 实现 initialize() - FFmpeg 初始化
- [ ] 实现 pushFrame() - 编码推流
- [ ] 实现 close() - 资源清理
- [ ] 单元测试 RTMP 推流

### Step 5: LiveStreamTask 集成 RTMP

- [ ] 在 start() 中初始化 RtmpStreamer
- [ ] 在 processLoop() 中调用 pushFrame()
- [ ] 在 stop() 中关闭 RtmpStreamer
- [ ] 测试完整流程

### Step 6: 测试和优化

- [ ] 编译所有代码
- [ ] 运行集成测试
- [ ] 性能测试 (FPS, CPU, 内存)
- [ ] 错误处理完善
- [ ] 文档更新

---

## ⚠️ 注意事项

### 1. H264 流处理

**问题**: DJI Liveview 返回的是 H264 编码流,而现有代码使用 cv::Mat (BGR)

**解决方案**:

- **方案 A**: 直接推流 H264 (最高效,推荐)

  - 不解码,直接将 H264 数据推流到 RTMP
  - 需要修改检测流程,在云端检测

- **方案 B**: 解码为 cv::Mat (兼容现有代码)
  - 使用 FFmpeg 解码 H264 为 cv::Mat
  - 本地检测后再编码推流
  - 性能开销大

### 2. 线程安全

- H264 数据队列需要互斥锁保护
- 使用条件变量实现等待/通知
- 注意死锁问题 (停止时唤醒等待线程)

### 3. 资源管理

- FFmpeg 资源必须正确释放 (RAII)
- Liveview/MediaManager 的生命周期管理
- 异常情况下的资源清理

### 4. 错误处理

- DJI SDK 返回 ErrorCode,需要检查
- FFmpeg 函数返回负数表示错误
- 网络异常 (断流、推流失败) 的处理

### 5. 性能优化

- H264 解码/编码的 CPU 占用
- RTMP 推流的网络带宽
- 内存使用 (大量 AVFrame 分配)

---

## 📚 参考资料

### DJI SDK 文档

- `Edge-SDK/doc/` - DJI SDK 文档
- `Edge-SDK/examples/liveview/` - 视频流示例
- `Edge-SDK/examples/media_manager/` - 媒体管理示例

### FFmpeg 文档

- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [FFmpeg RTMP 推流教程](https://trac.ffmpeg.org/wiki/StreamingGuide)
- [libx264 编码器参数](https://trac.ffmpeg.org/wiki/Encode/H.264)

### OpenCV 文档

- [OpenCV VideoWriter](https://docs.opencv.org/master/dd/d9e/classcv_1_1VideoWriter.html)
- [OpenCV imdecode](https://docs.opencv.org/master/d4/da8/group__imgcodecs.html#ga26a67788faa58ade337f8d28ba0eb19e)

---

## 🎯 预期成果

完成 Day 6 后应达到:

1. ✅ LiveStreamTask 使用真实的 DJI 视频流
2. ✅ MediaFileTask 使用真实的 DJI 媒体文件监听
3. ✅ RTMP 推流功能正常工作
4. ✅ 临时实现全部移除
5. ✅ 所有测试通过
6. ✅ 文档更新完整

---

**创建日期**: 2025-11-10  
**版本**: v1.0  
**下一步**: 开始 Step 2 - Device 模块集成
