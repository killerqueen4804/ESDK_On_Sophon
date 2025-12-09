# 八股 - FFmpeg H.264 解码器与 Parser 机制

## 📚 核心概念

### **什么是 AVCodecParserContext？**

**定义**: FFmpeg 的编解码器解析器上下文,用于从连续字节流中**分割和识别**编码帧。

**作用**:

1. **帧边界检测**: 从连续字节流中找到完整帧的起始和结束
2. **NAL 单元识别**: 识别 H.264 NAL 单元类型 (SPS/PPS/Slice/SEI)
3. **参数提取**: 自动提取 SPS/PPS 到 `AVCodecContext->extradata`
4. **时间戳估算**: 根据帧类型估算 PTS/DTS

**为什么需要 Parser？**

| 场景         | 不用 Parser          | 用 Parser       |
| ------------ | -------------------- | --------------- |
| 从文件解码   | ✅ 文件已分帧        | ❌ 多余         |
| 从网络流解码 | ❌ 连续字节流,无边界 | ✅ 必需         |
| DJI 视频流   | ❌ 缺少 SPS/PPS      | ✅ **自动提取** |

---

## 🔑 核心 API: av_parser_parse2()

### **函数签名**

```cpp
int av_parser_parse2(
    AVCodecParserContext *s,  // Parser 上下文
    AVCodecContext *avctx,    // 解码器上下文(Parser 会填充 extradata)
    uint8_t **poutbuf,        // 输出: 完整帧数据指针
    int *poutbuf_size,        // 输出: 完整帧大小
    const uint8_t *buf,       // 输入: 原始字节流
    int buf_size,             // 输入: 字节流长度
    int64_t pts,              // PTS (presentation timestamp)
    int64_t dts,              // DTS (decode timestamp)
    int64_t pos               // 字节位置
);
```

### **返回值**

- **返回值**: 本次处理的字节数 (≤ buf_size)
- **poutbuf_size**: 如果 > 0,表示提取到完整帧; 如果 == 0,需要更多数据

### **工作流程**

```
输入: 连续 H.264 字节流 (可能包含多个 NAL 单元)
  ↓
扫描 NAL 起始码 (0x00 0x00 0x00 0x01)
  ↓
提取 NAL Header (1 字节)
  ├─ 低 5 位 = Type (1=Slice, 5=IDR, 7=SPS, 8=PPS)
  └─ 高 3 位 = F + NRI (重要性)
  ↓
根据 Type 处理:
  ├─ Type 7 (SPS): 解析序列参数 → 存入 avctx->extradata
  ├─ Type 8 (PPS): 解析图像参数 → 追加到 avctx->extradata
  ├─ Type 1/5 (Slice): 输出到 poutbuf (完整帧)
  └─ Type 6 (SEI): 跳过或缓存
  ↓
输出: poutbuf 指向完整帧, poutbuf_size = 帧大小
```

### **示例代码**

```cpp
const uint8_t* pData = h264Data;
int remainingLen = dataLen;

AVPacket pkt;
av_init_packet(&pkt);

while (remainingLen > 0) {
    // 📌 解析字节流,提取完整帧
    int processedLen = av_parser_parse2(
        pParser,          // Parser 上下文
        pCodecCtx,        // 解码器上下文
        &pkt.data,        // 输出: 帧数据指针
        &pkt.size,        // 输出: 帧大小
        pData,            // 输入: 原始数据
        remainingLen,     // 输入: 剩余长度
        AV_NOPTS_VALUE,   // 不关心 PTS
        AV_NOPTS_VALUE,   // 不关心 DTS
        AV_NOPTS_VALUE    // 不关心 POS
    );

    remainingLen -= processedLen;
    pData += processedLen;

    // 📌 如果提取到完整帧
    if (pkt.size > 0) {
        int gotPicture = 0;
        avcodec_decode_video2(pCodecCtx, pFrameYUV, &gotPicture, &pkt);

        if (gotPicture) {
            // 成功解码一帧!
            processFrame(pFrameYUV);
        }
    }
}
```

---

## 📊 H.264 NAL 单元类型

### **NAL Header 结构**

```
 7   6   5   4   3   2   1   0
+---+-------+-------------------+
| F |  NRI  |       Type        |
+---+-------+-------------------+
 1    2        5 位
```

- **F (Forbidden bit)**: 禁止位 (0=正常, 1=错误)
- **NRI (NAL Ref Idc)**: 重要性 (0=可丢弃, 3=最重要)
- **Type**: NAL 单元类型 (5 位, 0-31)

### **常见 NAL 类型表**

| Type  | 名称            | 描述           | 重要性 (NRI) | 可解码?     |
| ----- | --------------- | -------------- | ------------ | ----------- |
| **1** | Slice (Non-IDR) | P/B 帧数据     | 1-2          | ✅          |
| **5** | Slice (IDR)     | I 帧(关键帧)   | **3**        | ✅          |
| 6     | SEI             | 补充增强信息   | 0            | ❌ (元数据) |
| **7** | **SPS**         | **序列参数集** | **3**        | ❌ (参数)   |
| **8** | **PPS**         | **图像参数集** | **3**        | ❌ (参数)   |
| 9     | AUD             | 访问单元分隔符 | 0            | ❌          |

### **SPS (Type 7) 包含什么？**

```cpp
// SPS 示例 (十六进制)
00 00 00 01 67 42 00 1F AB 40 50 05 BB 01 10 00 00 03 00 10 ...
│           │  └────────────┬──────────────┘
起始码      Type=7          SPS 数据载荷

// 解析后的参数:
{
    profile_idc: 66 (Baseline Profile)
    level_idc: 31 (Level 3.1)
    width: 1920
    height: 1080
    fps: 30
    ...
}
```

**SPS 是视频流的"身份证"**:

- 分辨率 (width x height)
- 帧率 (fps)
- Profile/Level (编码复杂度)
- 色度格式 (YUV420P)

### **PPS (Type 8) 包含什么？**

```cpp
// PPS 示例
00 00 00 01 68 CE 3C 80
│           │  └─┬──┘
起始码      Type=8 PPS 数据

// 解析后的参数:
{
    pic_parameter_set_id: 0
    entropy_coding_mode: CABAC
    num_ref_frames: 4
    ...
}
```

**PPS 是图像编码参数**:

- 参考帧数量
- 熵编码模式 (CAVLC/CABAC)
- 去块滤波参数

---

## 🎯 DJI H.264 流的特殊性

### **问题: 为什么缺少 SPS/PPS？**

**正常 H.264 流** (从文件或 RTSP):

```
[起始码][SPS][起始码][PPS][起始码][IDR][起始码][P][起始码][P]...
```

**DJI Liveview 流**:

```
[起始码][IDR][起始码][P][起始码][P]...
```

**原因**:

1. **带宽优化**: SPS/PPS 只需要发一次,后续帧复用
2. **低延迟**: 减少首帧数据量,加快显示
3. **协议设计**: DJI 假设解码器会缓存参数

### **解决方案对比**

| 方案                     | 工作原理               | 优点                       | 缺点                |
| ------------------------ | ---------------------- | -------------------------- | ------------------- |
| **AVCodecParserContext** | 自动提取并缓存 SPS/PPS | ✅ 自动化<br>✅ DJI 官方用 | ❌ 需要 FFmpeg      |
| 手动构造 SPS/PPS         | 根据分辨率生成标准参数 | ✅ 简单<br>✅ 无依赖       | ❌ 可能不匹配       |
| GStreamer h264parse      | 类似 Parser 的功能     | ✅ GStreamer 生态          | ❌ 仍然需要 SPS/PPS |

---

## 💻 项目中的应用

### **H264Decoder 类设计**

```cpp
class H264Decoder {
public:
    bool init(int width = 1920, int height = 1080);
    bool decode(const uint8_t* data, size_t len,
                DecodeCallback callback);
    void deinit();

private:
    AVCodecParserContext* pParser_;  // ⭐ 关键组件!
    AVCodecContext* pCodecCtx_;
    SwsContext* pSwsCtx_;
    AVFrame *pFrameYUV_, *pFrameRGB_;
};
```

### **初始化流程**

```cpp
bool H264Decoder::init(int width, int height) {
    // 1. 查找 H.264 解码器
    pCodec_ = avcodec_find_decoder(AV_CODEC_ID_H264);

    // 2. ⭐ 创建 Parser (关键!)
    pParser_ = av_parser_init(pCodec_->id);

    // 3. 分配解码器上下文
    pCodecCtx_ = avcodec_alloc_context3(pCodec_);
    pCodecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    pCodecCtx_->width = width;
    pCodecCtx_->height = height;
    pCodecCtx_->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;  // 显示所有帧
    pCodecCtx_->thread_count = 4;  // 4 线程解码

    // 4. 打开解码器
    avcodec_open2(pCodecCtx_, pCodec_, nullptr);

    // 5. 分配帧缓冲
    pFrameYUV_ = av_frame_alloc();
    pFrameRGB_ = av_frame_alloc();

    return true;
}
```

### **解码流程**

```cpp
bool H264Decoder::decode(const uint8_t* data, size_t len,
                         DecodeCallback callback) {
    const uint8_t* pData = data;
    int remainingLen = len;

    AVPacket pkt;
    av_init_packet(&pkt);

    while (remainingLen > 0) {
        // ⭐ Parser 解析字节流
        int processedLen = av_parser_parse2(
            pParser_, pCodecCtx_, &pkt.data, &pkt.size,
            pData, remainingLen,
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE
        );

        remainingLen -= processedLen;
        pData += processedLen;

        if (pkt.size > 0) {
            int gotPicture = 0;

            // 解码 H.264 → YUV420P
            avcodec_decode_video2(pCodecCtx_, pFrameYUV_, &gotPicture, &pkt);

            if (gotPicture) {
                // 转换 YUV → RGB
                sws_scale(pSwsCtx_, ...);

                // 转换 RGB → BGR (OpenCV)
                cv::Mat bgrMat;
                cv::cvtColor(rgbMat, bgrMat, cv::COLOR_RGB2BGR);

                // 回调用户
                callback(bgrMat);
            }
        }
    }

    return true;
}
```

### **资源清理**

```cpp
void H264Decoder::deinit() {
    // 释放顺序很重要!
    if (pSwsCtx_) sws_freeContext(pSwsCtx_);
    if (pFrameYUV_) av_frame_free(&pFrameYUV_);
    if (pFrameRGB_) av_frame_free(&pFrameRGB_);
    if (pRgbBuffer_) av_free(pRgbBuffer_);
    if (pParser_) av_parser_close(pParser_);  // ⭐ 关闭 Parser
    if (pCodecCtx_) {
        avcodec_close(pCodecCtx_);
        av_free(pCodecCtx_);
    }
}
```

---

## 🎓 面试高频问答

### **Q1: av_parser_parse2() 和 avcodec_decode_video2() 的区别？**

**A**:

- **Parser (解析器)**: 分析流的**结构**,提取 NAL 单元,不解码像素
- **Decoder (解码器)**: 将 H.264 **解码**为像素数据 (YUV)

| 功能        | Parser                   | Decoder           |
| ----------- | ------------------------ | ----------------- |
| 输入        | 连续字节流               | NAL 单元 (完整帧) |
| 输出        | NAL 单元 (SPS/PPS/Slice) | YUV 帧            |
| 是否解压缩? | ❌ 否 (只分析)           | ✅ 是 (解码像素)  |
| 性能        | 极低 (<1ms/帧)           | 高 (10-50ms/帧)   |

**举例**:

```cpp
// Parser: 分析结构,不解码
av_parser_parse2(...) → NAL 单元 (SPS/PPS/Slice)

// Decoder: 解码像素
avcodec_decode_video2(...) → YUV 帧 (可显示)
```

---

### **Q2: 为什么 DJI 视频流缺少 SPS/PPS 但仍能解码？**

**A**:

**关键原因**: `av_parser_parse2()` 会扫描整个字节流,当遇到 SPS/PPS NAL 单元时:

1. **解析** SPS/PPS 的内容 (分辨率、帧率等)
2. **存储** 到 `pCodecCtx->extradata`
3. **缓存** 在 Parser 内部
4. 后续 Slice 解码时**自动使用**缓存的参数

**证据**:

```cpp
// Edge-SDK 的代码
pCodecParserCtx = av_parser_init(AV_CODEC_ID_H264);

// 第一次解码
av_parser_parse2(pCodecParserCtx, pCodecCtx, ...);
// → 内部提取 SPS/PPS 到 pCodecCtx->extradata

// 后续解码
avcodec_decode_video2(pCodecCtx, ...);
// → 自动使用 extradata 中的 SPS/PPS
```

**DJI 视频流结构**:

```
帧 0: [SPS][PPS][IDR]  ← Parser 提取 SPS/PPS
帧 1: [P]              ← 解码器使用缓存的 SPS/PPS
帧 2: [P]              ← 解码器使用缓存的 SPS/PPS
...
```

---

### **Q3: SPS/PPS 存在哪里？如何使用？**

**A**:

**存储位置**:

```cpp
AVCodecContext* pCodecCtx;
pCodecCtx->extradata;       // uint8_t* 指针
pCodecCtx->extradata_size;  // 大小 (字节)
```

**内容格式** (avcc 格式):

```
+---+---+---+---+---+---+---+---+
| 01| PP| LL| FF| 03| SPS_SIZE  | SPS_DATA | 01| PPS_SIZE  | PPS_DATA |
+---+---+---+---+---+---+---+---+
  版本 Profile Level 保留  NumSPS   ...      NumPPS  ...
```

**如何使用**:

```cpp
// 自动使用 (推荐)
avcodec_decode_video2(pCodecCtx, ...);
// → FFmpeg 内部读取 extradata

// 手动提取 (用于推流)
if (pCodecCtx->extradata && pCodecCtx->extradata_size > 0) {
    std::vector<uint8_t> sps_pps(
        pCodecCtx->extradata,
        pCodecCtx->extradata + pCodecCtx->extradata_size
    );
    // → 推流时作为首帧发送
}
```

---

### **Q4: 如何判断 NAL 单元类型？**

**A**:

**方法 1: 手动解析 NAL Header**

```cpp
// 假设 H.264 数据以起始码开头: 00 00 00 01
const uint8_t* nalStart = data + 4;  // 跳过起始码
uint8_t nalHeader = nalStart[0];

// 提取类型 (低 5 位)
uint8_t nalType = nalHeader & 0x1F;

switch (nalType) {
    case 1:  std::cout << "Slice (Non-IDR)\n"; break;
    case 5:  std::cout << "Slice (IDR)\n"; break;
    case 7:  std::cout << "SPS\n"; break;
    case 8:  std::cout << "PPS\n"; break;
    default: std::cout << "Other\n"; break;
}

// 提取重要性 (NRI, bit 5-6)
uint8_t nalNri = (nalHeader >> 5) & 0x03;
```

**方法 2: 使用 Parser (推荐)**

```cpp
av_parser_parse2(pParser, pCodecCtx, &pkt.data, &pkt.size, ...);

// Parser 内部已经识别类型,可以通过以下方式判断:
if (pkt.size > 4) {
    uint8_t nalType = pkt.data[4] & 0x1F;
    // 同方法 1
}
```

---

### **Q5: YUV420P 是什么？为什么不直接输出 RGB？**

**A**:

**YUV420P 原理**:

- **Y**: 亮度 (Luminance) - 每个像素一个值
- **U/V**: 色度 (Chrominance) - 每 4 个像素共享一个值

**存储格式**:

```
1920x1080 图像:
- Y 平面: 1920 x 1080 = 2,073,600 字节
- U 平面: 960 x 540 = 518,400 字节  (宽高各减半)
- V 平面: 960 x 540 = 518,400 字节
总计: 3,110,400 字节

RGB24:
- 1920 x 1080 x 3 = 6,220,800 字节 (2 倍!)
```

**为什么 H.264 用 YUV？**

1. **人眼敏感度**: 对亮度敏感,对色度不敏感 (色度采样省空间)
2. **压缩效率**: YUV 更容易压缩 (Y/U/V 独立处理)
3. **标准兼容**: ITU-R BT.601/709 视频标准

**转换流程**:

```
H.264 解码 → YUV420P (FFmpeg 内部)
    ↓
sws_scale() → RGB24/BGR24 (显示/处理)
    ↓
cv::Mat → OpenCV 处理
```

---

### **Q6: 多线程解码如何实现？**

**A**:

**FFmpeg 多线程配置**:

```cpp
pCodecCtx->thread_count = 4;  // 4 线程
pCodecCtx->thread_type = FF_THREAD_FRAME;  // 帧级并行
```

**工作原理**:

```
主线程:
  ├─ Parser 解析 NAL 单元
  └─ 提交解码任务到线程池

工作线程 1: 解码帧 N
工作线程 2: 解码帧 N+1
工作线程 3: 解码帧 N+2
工作线程 4: 解码帧 N+3

主线程:
  └─ 按顺序输出解码结果
```

**性能提升**:

- **单线程**: 30 FPS → 约 80% CPU (单核)
- **4 线程**: 30 FPS → 约 25% CPU (每核)

**注意事项**:

- B 帧需要参考帧,并行度受限
- I/P 帧可以完全并行
- 内存占用增加 (每个线程缓冲帧)

---

## 📝 总结

### **核心要点**

1. **AVCodecParserContext** 是处理 DJI H.264 流的关键
2. **av_parser_parse2()** 自动提取 SPS/PPS,无需手动处理
3. **NAL 单元类型** 需要熟记 (1/5/7/8 最常用)
4. **YUV420P** 是 H.264 标准输出格式,需转换为 RGB/BGR
5. **多线程解码** 可以显著降低 CPU 占用

### **项目应用**

- **本地检测**: FFmpeg 解码 → cv::Mat → YOLOv10 检测
- **远程推流**: FFmpeg Parser 提取 SPS/PPS → GStreamer 推流
- **混合方案**: 解码 + 推流并行 (最佳实践)

### **面试加分项**

- 能解释 Parser vs Decoder 的区别
- 知道 SPS/PPS 的作用和存储位置
- 了解 YUV420P 的存储格式和优势
- 能画出 H.264 解码流程图
- 理解多线程解码的原理

---

**创建时间**: 2025-11-18  
**版本**: v1.0  
**标签**: #FFmpeg #H264 #Parser #解码器 #DJI #面试
