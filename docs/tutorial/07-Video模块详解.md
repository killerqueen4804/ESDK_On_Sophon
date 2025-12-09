# Video 模块详解 - H.264 解码器

> **模块定位**：Video 模块负责将 DJI SDK 的 H.264 视频流解码为 OpenCV 的 cv::Mat 格式。
> **关键词**：FFmpeg、AVCodecParserContext、H.264 NAL、YUV→BGR 转换

---

## 📁 模块结构

```
src/video/
├── CMakeLists.txt
└── H264Decoder.cpp        # H.264 解码器实现

include/esdk_sophon/video/
└── H264Decoder.h          # 解码器接口
```

---

## 🎬 H.264 解码器架构

### 为什么需要解码器？

```
DJI Liveview SDK                    我们的系统
    │                                   │
    │ H.264 视频流                      │
    │ (压缩数据)                        │ cv::Mat (BGR24)
    ▼                                   ▼
┌──────────────────┐              ┌──────────────────┐
│  onH264Data()    │  ─────────►  │   目标检测       │
│  uint8_t*, len   │   解码转换   │   cv::Mat        │
└──────────────────┘              └──────────────────┘
       ↑                                 │
       │                                 ▼
   压缩率 20:1                     ┌──────────────────┐
   1MB → 50KB                      │   RTMP 推流      │
                                   └──────────────────┘
```

### H.264 基础知识

```
┌─────────────────────────────────────────────────────────────────┐
│                      H.264 视频流结构                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  H.264 码流由 NAL 单元 (Network Abstraction Layer) 组成：        │
│                                                                 │
│  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐  │
│  │ SPS │ │ PPS │ │ SEI │ │ IDR(I帧) │ │  P 帧   │ │  P 帧   │  │
│  │Type7│ │Type8│ │Type6│ │  Type5  │ │  Type1  │ │  Type1  │  │
│  └─────┘ └─────┘ └─────┘ └─────────┘ └─────────┘ └─────────┘  │
│    ↑       ↑                ↑             ↑                    │
│    │       │                │             │                    │
│  解码参数  图片参数      关键帧(完整)   参考帧(差分)            │
│                                                                 │
│  关键概念：                                                     │
│  - SPS (Sequence Parameter Set): 序列参数，包含分辨率、帧率等   │
│  - PPS (Picture Parameter Set): 图片参数，包含编码方式等        │
│  - IDR (Instantaneous Decoder Refresh): 关键帧，可独立解码      │
│  - P 帧: 预测帧，依赖前面的帧                                   │
│                                                                 │
│  ⚠️ DJI 的问题：回调数据可能缺少 SPS/PPS 头！                   │
│     → FFmpeg 的 AVCodecParserContext 可以自动提取               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📌 H264Decoder 类详解

### 1. 类定义

```cpp
/**
 * @brief H.264 解码器类
 *
 * 核心特性:
 * - 使用 AVCodecParserContext 自动提取 SPS/PPS
 * - 多线程解码 (4 线程)
 * - 自动处理分辨率变化
 * - 输出 cv::Mat (BGR24 格式)
 */
class H264Decoder {
public:
    using DecodeCallback = std::function<void(const cv::Mat& frame)>;

    H264Decoder();
    ~H264Decoder();

    bool init(int width = 1920, int height = 1080);
    void deinit();
    bool decode(const uint8_t* data, size_t length, DecodeCallback callback);

    int getWidth() const { return decodeWidth_; }
    int getHeight() const { return decodeHeight_; }
    bool isInitialized() const { return initialized_; }

private:
    // FFmpeg 核心组件
    AVCodecContext* pCodecCtx_;        // 解码器上下文
    AVCodec* pCodec_;                  // H.264 解码器
    AVCodecParserContext* pParser_;    // ⭐ Parser 上下文（关键！）
    SwsContext* pSwsCtx_;              // 颜色空间转换

    AVFrame* pFrameYUV_;               // YUV 帧缓冲
    AVFrame* pFrameRGB_;               // RGB 帧缓冲
    uint8_t* pRgbBuffer_;              // RGB 数据缓冲区

    int decodeWidth_, decodeHeight_;
    bool initialized_;
    mutable std::mutex decodeMutex_;

    void handleResolutionChange(int newWidth, int newHeight);
};
```

### 2. 初始化流程

```cpp
bool H264Decoder::init(int width, int height) {
    std::lock_guard<std::mutex> lock(decodeMutex_);

    // ==================== 步骤1: 查找 H.264 解码器 ====================
    pCodec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!pCodec_) {
        logger.error("❌ 找不到 H.264 解码器!");
        return false;
    }

    // ==================== 步骤2: 创建 Parser ⭐⭐⭐ 关键!!! ====================
    // Parser 的作用:
    // 1. 解析连续字节流,分割出完整的 NAL 单元
    // 2. 自动识别 SPS/PPS/Slice/SEI 等类型
    // 3. 提取 SPS/PPS 并存储到 pCodecCtx->extradata
    // 4. 这就是为什么 DJI 视频流缺少 SPS/PPS 也能解码!
    pParser_ = av_parser_init(pCodec_->id);
    if (!pParser_) {
        logger.error("❌ 创建 Parser 失败!");
        return false;
    }

    // ==================== 步骤3: 分配解码器上下文 ====================
    pCodecCtx_ = avcodec_alloc_context3(pCodec_);

    // ==================== 步骤4: 配置解码器参数 ====================
    pCodecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;  // 输出格式
    pCodecCtx_->width = width;
    pCodecCtx_->height = height;

    // SHOW_ALL: 显示所有帧（包括损坏的帧）
    // 对于不稳定的网络流很重要
    pCodecCtx_->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;

    // 多线程解码: 4 线程并行
    pCodecCtx_->thread_count = 4;

    // ==================== 步骤5: 打开解码器 ====================
    int ret = avcodec_open2(pCodecCtx_, pCodec_, nullptr);
    if (ret < 0) {
        char errBuf[128];
        av_strerror(ret, errBuf, sizeof(errBuf));
        logger.error("❌ 打开解码器失败: " + std::string(errBuf));
        return false;
    }

    // ==================== 步骤6: 分配帧缓冲区 ====================
    pFrameYUV_ = av_frame_alloc();
    pFrameRGB_ = av_frame_alloc();

    // SwsContext 和 RGB 缓冲区延迟创建（等解码出真实分辨率再创建）
    pSwsCtx_ = nullptr;
    pRgbBuffer_ = nullptr;

    initialized_ = true;
    return true;
}
```

### 📚 知识点：AVCodecParserContext 的核心作用

#### 问题背景

```
DJI Liveview 回调的数据格式：
┌────────────────────────────────────────────────────┐
│ 0x00 0x00 0x00 0x01 │ NAL 数据 ... │ 0x00 0x00 ...│
│     起始码          │              │               │
└────────────────────────────────────────────────────┘

问题：
1. 数据可能跨越多个回调（一个 NAL 被切成两次回调）
2. 可能缺少 SPS/PPS（DJI 只在流开始时发送一次）
3. 没有帧边界信息（不知道一帧从哪开始、到哪结束）
```

#### Parser 的解决方案

```cpp
// av_parser_parse2() 的工作原理
int av_parser_parse2(
    AVCodecParserContext* parser,  // Parser 上下文
    AVCodecContext* codecCtx,      // 解码器上下文
    uint8_t** outBuf,              // 输出：完整帧数据
    int* outSize,                  // 输出：帧大小
    const uint8_t* buf,            // 输入：原始数据
    int bufSize,                   // 输入：数据大小
    int64_t pts, int64_t dts, int64_t pos
);

// 内部工作流程：
// 1. 累积输入数据到内部缓冲区
// 2. 扫描起始码 (0x00 0x00 0x00 0x01)
// 3. 识别 NAL 类型：
//    - Type 7 (SPS): 存储到 codecCtx->extradata
//    - Type 8 (PPS): 存储到 codecCtx->extradata
//    - Type 5 (IDR): 标记为关键帧
//    - Type 1 (Slice): 普通帧
// 4. 当检测到完整帧时，输出到 outBuf/outSize
// 5. 返回已消费的输入字节数
```

#### 为什么这是关键？

```
没有 Parser 的情况：
┌──────────────────────────────────────────────────────────┐
│ 回调1: 半个 SPS + PPS + 半个 IDR                         │
│ 回调2: IDR 剩余部分 + P 帧 + 半个 P 帧                   │
│        ↓                                                 │
│ 直接送入解码器 → ❌ 解码失败（数据不完整）              │
└──────────────────────────────────────────────────────────┘

使用 Parser 的情况：
┌──────────────────────────────────────────────────────────┐
│ 回调1 → Parser 缓存                                      │
│ 回调2 → Parser 累积                                      │
│         Parser 检测到完整帧 → 输出完整 NAL 到解码器      │
│         ↓                                                │
│         解码器 → ✅ 解码成功                            │
└──────────────────────────────────────────────────────────┘
```

### 3. 解码流程

```cpp
bool H264Decoder::decode(const uint8_t* data, size_t length,
                         DecodeCallback callback) {
    if (!initialized_ || !data || length == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(decodeMutex_);

    const uint8_t* pData = data;
    int remainingLen = static_cast<int>(length);

    AVPacket pkt;
    av_init_packet(&pkt);

    // ==================== 循环处理所有数据 ====================
    // 一次 decode 调用可能包含多帧
    while (remainingLen > 0) {
        // ⭐⭐⭐ 核心 API: av_parser_parse2()
        int processedLen = av_parser_parse2(
            pParser_,           // Parser 上下文
            pCodecCtx_,         // 解码器上下文
            &pkt.data,          // 输出: 完整帧数据
            &pkt.size,          // 输出: 帧大小
            pData,              // 输入: 原始数据
            remainingLen,       // 输入: 剩余字节数
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE
        );

        // 更新指针
        remainingLen -= processedLen;
        pData += processedLen;

        // 如果 Parser 提取到了完整帧
        if (pkt.size > 0) {
            int gotPicture = 0;

            // H.264 解码 → YUV420P
            avcodec_decode_video2(pCodecCtx_, pFrameYUV_, &gotPicture, &pkt);

            if (gotPicture) {
                // 检查分辨率变化
                if (pFrameYUV_->width != decodeWidth_ ||
                    pFrameYUV_->height != decodeHeight_) {
                    handleResolutionChange(pFrameYUV_->width, pFrameYUV_->height);
                }

                // 延迟创建颜色转换器
                if (!pSwsCtx_) {
                    pSwsCtx_ = sws_getContext(
                        decodeWidth_, decodeHeight_, pCodecCtx_->pix_fmt,
                        decodeWidth_, decodeHeight_, AV_PIX_FMT_BGR24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr
                    );
                }

                // 延迟分配 RGB 缓冲区
                if (!pRgbBuffer_) {
                    rgbBufferSize_ = av_image_get_buffer_size(
                        AV_PIX_FMT_BGR24, decodeWidth_, decodeHeight_, 1);
                    pRgbBuffer_ = (uint8_t*)av_malloc(rgbBufferSize_);
                    av_image_fill_arrays(pFrameRGB_->data, pFrameRGB_->linesize,
                                        pRgbBuffer_, AV_PIX_FMT_BGR24,
                                        decodeWidth_, decodeHeight_, 1);
                }

                // ⭐ YUV → BGR 转换
                sws_scale(
                    pSwsCtx_,
                    pFrameYUV_->data,      // 源数据
                    pFrameYUV_->linesize,  // 源行大小
                    0, decodeHeight_,      // 扫描范围
                    pFrameRGB_->data,      // 目标数据
                    pFrameRGB_->linesize   // 目标行大小
                );

                // 创建 cv::Mat（零拷贝，引用 pFrameRGB 数据）
                cv::Mat bgrMat(
                    decodeHeight_, decodeWidth_,
                    CV_8UC3,
                    pFrameRGB_->data[0],
                    pFrameRGB_->linesize[0]
                );

                // 调用回调
                if (callback) {
                    callback(bgrMat);
                }
            }
        }

        av_packet_unref(&pkt);
    }

    return true;
}
```

### 📚 知识点：颜色空间转换 (YUV → BGR)

#### YUV420P 格式

```
┌─────────────────────────────────────────────────────────────────┐
│                      YUV420P 内存布局                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Y 平面 (亮度) - 每个像素一个字节                               │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┐                             │
│  │Y00│Y01│Y02│Y03│Y04│Y05│Y06│Y07│ ...                         │
│  ├───┼───┼───┼───┼───┼───┼───┼───┤                             │
│  │Y10│Y11│Y12│Y13│Y14│Y15│Y16│Y17│ ...                         │
│  └───┴───┴───┴───┴───┴───┴───┴───┘                             │
│                                                                 │
│  U 平面 (色度-蓝) - 4:2:0 采样，每 4 个 Y 共享一个 U            │
│  ┌───┬───┬───┬───┐                                             │
│  │U00│U01│U02│U03│ ...                                         │
│  ├───┼───┼───┼───┤                                             │
│  │U10│U11│U12│U13│ ...                                         │
│  └───┴───┴───┴───┘                                             │
│                                                                 │
│  V 平面 (色度-红) - 与 U 平面结构相同                           │
│  ┌───┬───┬───┬───┐                                             │
│  │V00│V01│V02│V03│ ...                                         │
│  └───┴───┴───┴───┘                                             │
│                                                                 │
│  内存大小: width * height * 1.5                                 │
│  例: 1920x1080 = 3,110,400 字节 ≈ 3 MB                         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### BGR24 格式 (OpenCV 默认)

```
┌─────────────────────────────────────────────────────────────────┐
│                      BGR24 内存布局                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  每个像素 3 字节 (B, G, R)，交织存储                            │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐             │
│  │B00│G00│R00│B01│G01│R01│B02│G02│R02│B03│G03│R03│ ...         │
│  └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘             │
│                                                                 │
│  内存大小: width * height * 3                                   │
│  例: 1920x1080 = 6,220,800 字节 ≈ 6 MB                         │
│                                                                 │
│  ⚠️ 注意：OpenCV 默认是 BGR，不是 RGB！                         │
│     cv::imread() 读取的图像是 BGR                              │
│     cv::imshow() 期望的也是 BGR                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### 转换公式

```cpp
// YUV → RGB 转换公式 (BT.601 标准)
R = 1.164 * (Y - 16) + 1.596 * (V - 128)
G = 1.164 * (Y - 16) - 0.813 * (V - 128) - 0.391 * (U - 128)
B = 1.164 * (Y - 16) + 2.018 * (U - 128)

// FFmpeg 的 sws_scale() 内部实现了这个转换
// 支持 SIMD 优化（SSE、AVX、NEON）
```

### 4. 资源释放

```cpp
void H264Decoder::deinit() {
    std::lock_guard<std::mutex> lock(decodeMutex_);

    if (!initialized_) return;

    // ⚠️ 释放顺序很重要！避免悬空指针和 double-free

    // 1. 先释放依赖其他资源的组件
    if (pSwsCtx_) {
        sws_freeContext(pSwsCtx_);
        pSwsCtx_ = nullptr;
    }

    // 2. 释放帧缓冲
    if (pFrameYUV_) {
        av_frame_free(&pFrameYUV_);
        pFrameYUV_ = nullptr;
    }

    if (pFrameRGB_) {
        av_frame_free(&pFrameRGB_);
        pFrameRGB_ = nullptr;
    }

    // 3. 释放数据缓冲区
    if (pRgbBuffer_) {
        av_free(pRgbBuffer_);
        pRgbBuffer_ = nullptr;
    }

    // 4. 关闭 Parser
    if (pParser_) {
        av_parser_close(pParser_);
        pParser_ = nullptr;
    }

    // 5. 关闭解码器上下文
    if (pCodecCtx_) {
        avcodec_close(pCodecCtx_);
        av_free(pCodecCtx_);
        pCodecCtx_ = nullptr;
    }

    // 6. 解码器本身由 FFmpeg 管理，不需要手动释放
    pCodec_ = nullptr;

    initialized_ = false;
}
```

### 📚 知识点：RAII 与资源管理

#### 问题

```cpp
// ❌ 错误的资源管理
void decode() {
    AVFrame* frame = av_frame_alloc();

    if (error1) return;  // 💥 内存泄漏！
    if (error2) return;  // 💥 内存泄漏！

    // ... 使用 frame ...

    av_frame_free(&frame);
}
```

#### RAII 解决方案

```cpp
// ✅ 使用 RAII
class FrameGuard {
    AVFrame* frame_;
public:
    FrameGuard() : frame_(av_frame_alloc()) {}
    ~FrameGuard() { if (frame_) av_frame_free(&frame_); }

    AVFrame* get() { return frame_; }
    operator AVFrame*() { return frame_; }
};

void decode() {
    FrameGuard frame;  // 自动管理生命周期

    if (error1) return;  // ✅ 自动释放
    if (error2) return;  // ✅ 自动释放

    // ... 使用 frame.get() ...
}  // ✅ 自动释放
```

---

## 📌 性能优化

### 1. 多线程解码

```cpp
// 设置解码器线程数
pCodecCtx_->thread_count = 4;

// FFmpeg 自动使用多线程解码：
// - 帧级并行：同时解码多个独立帧
// - 片级并行：同时解码一帧的多个切片
```

### 2. 零拷贝 cv::Mat

```cpp
// 零拷贝：直接引用 FFmpeg 的数据缓冲区
cv::Mat bgrMat(
    decodeHeight_, decodeWidth_,
    CV_8UC3,
    pFrameRGB_->data[0],      // 直接指向 FFmpeg 缓冲区
    pFrameRGB_->linesize[0]   // 行步长（可能有 padding）
);

// ⚠️ 注意：bgrMat 的数据在下一次 decode 调用后失效！
// 如果需要保留，必须深拷贝：
cv::Mat safeCopy = bgrMat.clone();
```

### 3. 延迟初始化

```cpp
// 延迟创建 SwsContext（等知道真实分辨率再创建）
if (!pSwsCtx_) {
    pSwsCtx_ = sws_getContext(...);
}

// 好处：
// 1. 配置文件中的分辨率可能与实际不符
// 2. 视频流可能动态变换分辨率
// 3. 避免创建后又销毁重建
```

---

## 📌 错误处理

### 常见错误及解决

| 错误                  | 原因                      | 解决方案                                   |
| --------------------- | ------------------------- | ------------------------------------------ |
| "找不到 H.264 解码器" | FFmpeg 编译时未启用 H.264 | 重新编译 FFmpeg with --enable-decoder=h264 |
| "打开解码器失败"      | 参数错误或资源不足        | 检查 pix_fmt、分辨率参数                   |
| 解码出全灰图像        | 缺少 SPS/PPS              | 确保 Parser 已创建                         |
| 解码出花屏            | 帧不完整或丢包            | 检查网络稳定性                             |
| 内存持续增长          | 资源未释放                | 检查 av_frame_free、av_packet_unref        |

### 错误码处理

```cpp
int ret = avcodec_open2(pCodecCtx_, pCodec_, nullptr);
if (ret < 0) {
    char errBuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errBuf, sizeof(errBuf));

    // 常见错误码：
    // AVERROR(ENOMEM): 内存不足
    // AVERROR(EINVAL): 参数无效
    // AVERROR_DECODER_NOT_FOUND: 解码器不存在

    logger.error("FFmpeg 错误: " + std::string(errBuf));
}
```

---

## 📚 面试高频题

### 1. H.264 关键帧

```
Q: 什么是 I 帧、P 帧、B 帧？
A: - I 帧 (Intra): 完整图像，可独立解码，最大
   - P 帧 (Predicted): 参考前面的帧，中等大小
   - B 帧 (Bidirectional): 参考前后帧，最小

   GOP (Group of Pictures): IBBPBBPBBI...
```

### 2. SPS/PPS

```
Q: 为什么 SPS/PPS 很重要？
A: SPS 包含：分辨率、帧率、Profile/Level
   PPS 包含：熵编码类型、切片组数

   没有它们，解码器不知道如何解释视频数据。
```

### 3. YUV 4:2:0

```
Q: 为什么视频普遍使用 YUV 4:2:0？
A: 1. 人眼对亮度敏感，对色度不敏感
   2. 4:2:0 比 RGB 节省 50% 空间
   3. 1920x1080:
      - RGB: 6.2 MB
      - YUV420: 3.1 MB
```

### 4. FFmpeg 线程安全

```
Q: FFmpeg 的解码器是线程安全的吗？
A: 单个 AVCodecContext 不是线程安全的。
   每个线程应该有自己的 context。

   但是可以配置 thread_count 让 FFmpeg 内部多线程解码。
```

---

## 📌 数据流总结

```
┌─────────────────────────────────────────────────────────────────┐
│                      H.264 解码完整流程                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  DJI SDK 回调                                                   │
│       │                                                         │
│       ▼                                                         │
│  ┌─────────────────────────────────────────┐                   │
│  │ onH264Data(uint8_t* buf, uint32_t len)  │                   │
│  │ 原始 H.264 字节流（可能跨帧）            │                   │
│  └─────────────────────────────────────────┘                   │
│       │                                                         │
│       ▼                                                         │
│  ┌─────────────────────────────────────────┐                   │
│  │ av_parser_parse2()                       │                   │
│  │ 解析 NAL、提取 SPS/PPS、输出完整帧       │                   │
│  └─────────────────────────────────────────┘                   │
│       │                                                         │
│       ▼                                                         │
│  ┌─────────────────────────────────────────┐                   │
│  │ avcodec_decode_video2()                  │                   │
│  │ H.264 → YUV420P                         │                   │
│  └─────────────────────────────────────────┘                   │
│       │                                                         │
│       ▼                                                         │
│  ┌─────────────────────────────────────────┐                   │
│  │ sws_scale()                              │                   │
│  │ YUV420P → BGR24                         │                   │
│  └─────────────────────────────────────────┘                   │
│       │                                                         │
│       ▼                                                         │
│  ┌─────────────────────────────────────────┐                   │
│  │ cv::Mat (零拷贝)                         │                   │
│  │ 供目标检测和 RTMP 推流使用               │                   │
│  └─────────────────────────────────────────┘                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

**创建日期**: 2025-11-27  
**适用版本**: ESDK Sophon v3.0+  
**依赖库**: FFmpeg 4.x+, OpenCV 4.x+
