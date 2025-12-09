# 解决 H.264 SPS/PPS 缺失问题 - FFmpeg Parser 方案

## 📋 问题回顾

### **现象**

```log
WARN h264parse broken/invalid nal Type: 1 Slice will be dropped
WARN flvmux Codec data for video stream not found
[ERROR] 管道状态异常: state=PAUSED
```

### **根因**

DJI Liveview SDK 提供的 H.264 数据流**缺少 SPS/PPS 头信息**:

- **SPS** (Sequence Parameter Set): 视频序列参数 (分辨率、帧率、profile 等)
- **PPS** (Picture Parameter Set): 图像参数集

没有这些参数,GStreamer h264parse 无法解析视频流。

---

## 💡 **解决方案: 借鉴 DJI Edge-SDK**

### **核心发现**

查看 `Edge-SDK/examples/liveview/ffmpeg_stream_decoder.cc`:

```cpp
// 关键代码!!!
pCodecParserCtx = av_parser_init(AV_CODEC_ID_H264);  // 创建 Parser

// 解析 H.264 数据
processedLen = av_parser_parse2(
    pCodecParserCtx, pCodecCtx, &pkt.data, &pkt.size, pData,
    remainingLen, AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE);

// Parser 自动提取 SPS/PPS!
```

**`av_parser_parse2()` 的魔力**:

1. 自动识别 NAL 单元类型 (SPS=7, PPS=8, Slice=1, SEI=6)
2. 提取并缓存 SPS/PPS
3. 将连续字节流分割为完整帧

**这就是为什么 Edge-SDK 的 FFmpeg 解码器可以正常工作!**

---

## 🎯 **实现方案**

### **方案 A: 完全使用 FFmpeg 解码 (推荐用于视觉检测)**

#### **架构**

```
DJI H.264 数据
  ↓
FFmpeg Parser + Decoder
  ↓
cv::Mat (YUV → RGB)
  ↓
视觉检测
```

#### **优点**

- ✅ DJI 官方验证 (Edge-SDK 已证明可行)
- ✅ 自动处理 SPS/PPS
- ✅ 我们已经需要解码 (用于视觉检测)

#### **缺点**

- ❌ 不能直接推流 (需要重新编码)

#### **代码示例**

```cpp
class H264Decoder {
public:
    H264Decoder() {
        // 1. 查找 H.264 解码器
        pCodec = avcodec_find_decoder(AV_CODEC_ID_H264);

        // 2. 创建 Parser (关键!)
        pCodecParserCtx = av_parser_init(pCodec->id);

        // 3. 分配解码上下文
        pCodecCtx = avcodec_alloc_context3(pCodec);
        pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        pCodecCtx->width = 1920;
        pCodecCtx->height = 1080;
        pCodecCtx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;  // 显示所有帧
        pCodecCtx->thread_count = 4;

        // 4. 打开解码器
        avcodec_open2(pCodecCtx, pCodec, nullptr);
    }

    cv::Mat decode(const uint8_t* data, size_t length) {
        const uint8_t* pData = data;
        int remainingLen = length;

        AVPacket pkt;
        av_init_packet(&pkt);

        while (remainingLen > 0) {
            // 📌 关键: 使用 Parser 解析 H.264 流
            int processedLen = av_parser_parse2(
                pCodecParserCtx, pCodecCtx,
                &pkt.data, &pkt.size,
                pData, remainingLen,
                AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE
            );

            remainingLen -= processedLen;
            pData += processedLen;

            if (pkt.size > 0) {
                // 解码
                int gotPicture = 0;
                avcodec_decode_video2(pCodecCtx, pFrameYUV, &gotPicture, &pkt);

                if (gotPicture) {
                    // 转换 YUV → RGB → cv::Mat
                    sws_scale(pSwsCtx, ...);
                    cv::Mat mat(h, w, CV_8UC3, pFrameRGB->data[0]);
                    return mat;
                }
            }
        }

        return cv::Mat();
    }

private:
    AVCodecContext* pCodecCtx;
    AVCodec* pCodec;
    AVCodecParserContext* pCodecParserCtx;  // 关键!!!
    SwsContext* pSwsCtx;
    AVFrame* pFrameYUV;
    AVFrame* pFrameRGB;
};
```

---

### **方案 B: FFmpeg Parser 提取 SPS/PPS + GStreamer 推流**

#### **架构**

```
DJI H.264 数据
  ↓
FFmpeg Parser (只解析,不解码)
  ├─ 提取 SPS
  ├─ 提取 PPS
  └─ 提取视频帧
  ↓
GStreamer 推流
  ├─ 首帧: 推 SPS + PPS
  └─ 后续: 推视频数据
```

#### **优点**

- ✅ 保留原始数据 (不重新编码)
- ✅ 低性能损耗
- ✅ GStreamer 可用 (有了 SPS/PPS)

#### **核心代码**

```cpp
class H264Parser {
public:
    H264Parser() {
        AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        parser_ = av_parser_init(codec->id);
        ctx_ = avcodec_alloc_context3(codec);
    }

    struct ParseResult {
        std::vector<uint8_t> sps;
        std::vector<uint8_t> pps;
        std::vector<uint8_t> frame;
        bool hasSPS = false;
        bool hasPPS = false;
    };

    ParseResult parse(const uint8_t* data, size_t length) {
        ParseResult result;

        const uint8_t* pData = data;
        int remainingLen = length;

        while (remainingLen > 0) {
            uint8_t* outData = nullptr;
            int outSize = 0;

            int processedLen = av_parser_parse2(
                parser_, ctx_,
                &outData, &outSize,
                pData, remainingLen,
                AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE
            );

            pData += processedLen;
            remainingLen -= processedLen;

            if (outSize > 0) {
                // 📌 检查 NAL 类型
                uint8_t nalType = outData[4] & 0x1F;  // 假设有起始码 00 00 00 01

                if (nalType == 7) {  // SPS
                    result.sps.assign(outData, outData + outSize);
                    result.hasSPS = true;
                } else if (nalType == 8) {  // PPS
                    result.pps.assign(outData, outData + outSize);
                    result.hasPPS = true;
                } else {
                    result.frame.assign(outData, outData + outSize);
                }
            }
        }

        return result;
    }

private:
    AVCodecParserContext* parser_;
    AVCodecContext* ctx_;
};
```

#### **使用示例**

```cpp
edge_sdk::ErrorCode LiveStreamTask::onH264Data(const uint8_t* buf, uint32_t len) {
    // 1. 解析 H.264
    auto parseResult = h264Parser_.parse(buf, len);

    // 2. 缓存 SPS/PPS (只在首次出现时)
    if (parseResult.hasSPS && sps_.empty()) {
        sps_ = parseResult.sps;
        logger_.info("✅ 提取到 SPS, 大小: " + std::to_string(sps_.size()));
    }
    if (parseResult.hasPPS && pps_.empty()) {
        pps_ = parseResult.pps;
        logger_.info("✅ 提取到 PPS, 大小: " + std::to_string(pps_.size()));
    }

    // 3. RTMP 推流
    if (rtmpStreamer_ && rtmpEnabled_) {
        // 首帧推送 SPS + PPS
        if (frameCount_ == 0) {
            if (!sps_.empty()) rtmpStreamer_->pushH264Frame(sps_);
            if (!pps_.empty()) rtmpStreamer_->pushH264Frame(pps_);
        }

        // 推送视频帧
        if (!parseResult.frame.empty()) {
            rtmpStreamer_->pushH264Frame(parseResult.frame);
        }
    }

    frameCount_++;
    return edge_sdk::kOk;
}
```

---

### **方案 C: 混合方案 (最佳实践)**

#### **架构**

```
DJI H.264 数据
  ↓
┌─────────────┬──────────────┐
│             │              │
FFmpeg Decoder    FFmpeg Parser
    ↓                ↓
cv::Mat        SPS/PPS + 帧
    ↓                ↓
视觉检测       GStreamer 推流
    ↓
绘制检测框
    ↓
(可选) FFmpeg 编码
    ↓
GStreamer 推流
```

#### **优点**

- ✅ 本地检测: FFmpeg 解码 (DJI 官方方案)
- ✅ 推流: GStreamer (高效,不重新编码)
- ✅ 可选: 推流带检测框的视频 (重新编码)

---

## 📊 **方案对比**

| 方案                      | 性能       | 复杂度     | 推流兼容性      | 推荐度     |
| ------------------------- | ---------- | ---------- | --------------- | ---------- |
| **A: 纯 FFmpeg**          | ⭐⭐       | ⭐⭐⭐     | ❌ (需重新编码) | ⭐⭐       |
| **B: Parser + GStreamer** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐   | ✅              | ⭐⭐⭐⭐   |
| **C: 混合方案**           | ⭐⭐⭐⭐   | ⭐⭐⭐⭐⭐ | ✅              | ⭐⭐⭐⭐⭐ |

---

## 🚀 **实施计划**

### **阶段 1: 实现 FFmpeg 解码器 (本地检测)**

**参考**: `Edge-SDK/examples/liveview/ffmpeg_stream_decoder.cc`

**任务**:

1. 创建 `H264Decoder` 类
2. 实现 `decode()` 方法
3. 集成到 `LiveStreamTask::onH264Data()`
4. 测试本地检测功能

**预期结果**:

- 视觉检测正常工作
- 无 SPS/PPS 警告 (FFmpeg 自动处理)

---

### **阶段 2: 提取 SPS/PPS 用于推流**

**任务**:

1. 使用 `av_parser_parse2()` 解析 H.264
2. 检测并提取 SPS (NAL type=7) 和 PPS (NAL type=8)
3. 缓存 SPS/PPS
4. 修改 RTMP 推流逻辑:
   - 首帧推送 SPS + PPS
   - 后续推送视频帧

**预期结果**:

- GStreamer 管道保持 PLAYING 状态
- RTMP 推流成功
- 播放器可以正常播放

---

### **阶段 3: 可选 - 推流带检测框的视频**

**任务**:

1. 解码 H.264 → cv::Mat
2. 运行检测,绘制检测框
3. 使用 FFmpeg 编码 cv::Mat → H.264
4. 推流编码后的视频

**预期结果**:

- 推流的视频包含检测结果
- 远程查看实时检测效果

---

## 🎓 **技术知识点**

### **FFmpeg Parser vs Decoder**

| 功能 | Parser                   | Decoder         |
| ---- | ------------------------ | --------------- |
| 作用 | 解析 H.264 流结构        | 解码为像素数据  |
| 输入 | H.264 字节流             | H.264 NAL 单元  |
| 输出 | NAL 单元 (SPS/PPS/Slice) | YUV 帧          |
| 性能 | 极低                     | 高 (需 CPU/GPU) |
| 用途 | 提取元数据,分割帧        | 显示,处理       |

### **H.264 NAL 单元类型**

| Type  | 名称      | 描述                |
| ----- | --------- | ------------------- |
| 1     | Slice     | 视频帧数据 (P/B 帧) |
| 5     | IDR Slice | I 帧 (关键帧)       |
| 6     | SEI       | 补充增强信息        |
| **7** | **SPS**   | **序列参数集** ⭐   |
| **8** | **PPS**   | **图像参数集** ⭐   |

### **GStreamer h264parse 的要求**

h264parse 需要 SPS/PPS 来:

1. **生成 codec_data**: FLV/MP4 容器需要的元数据
2. **验证帧**: 检查帧的合法性
3. **时间戳计算**: 根据 SPS 中的帧率信息

没有 SPS/PPS:

- h264parse 拒绝所有帧 (broken/invalid nal)
- flvmux 无法生成 FLV 头 (Codec data not found)
- 管道进入 PAUSED 状态

---

## 📝 **总结**

### **问题本质**

不是 GStreamer 有问题,而是:

1. DJI 的 H.264 流缺少 SPS/PPS
2. GStreamer 是**严格的**,拒绝无效流
3. FFmpeg 是**宽松的**,自动提取参数

### **解决思路**

**借鉴 DJI 官方方案**:

- Edge-SDK 使用 FFmpeg Parser
- `av_parser_parse2()` 自动提取 SPS/PPS
- 我们也可以用同样的方法!

### **最终方案**

**混合使用 FFmpeg + GStreamer**:

- FFmpeg: 解码 (视觉检测) + 提取 SPS/PPS
- GStreamer: 推流 (高效,有了 SPS/PPS 就能工作)

---

**创建时间**: 2025-11-18 16:10  
**状态**: 方案设计完成,待实施  
**下一步**: 实现 H264Decoder 类
