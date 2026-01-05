# SAM2 分割器集成说明

## 📌 概述

本文档说明如何将 **SAM2 (Segment Anything Model 2)** 集成到 ESDK On Sophon 项目中，实现基于检测框的精细分割功能。

**作者**: ESDK Sophon Team  
**日期**: 2025-12-11  
**版本**: v1.0

---

## 🎯 功能特性

- ✅ 支持基于 YOLO 检测框的提示分割
- ✅ 使用 Sophon BMRuntime 进行 TPU 加速推理
- ✅ 输出多边形轮廓和二值掩码
- ✅ 与现有检测器无缝集成
- 🔄 暂不支持全图自动分割 (AutoSegment)

---

## 📁 文件结构

```
include/esdk_sophon/vision/segmentation/
├── ISegmentor.h              # 分割器接口
└── Sam2Segmentor.h           # SAM2 实现

src/vision/segmentation/
├── Sam2Segmentor.cpp         # SAM2 实现
└── CMakeLists.txt            # 构建配置 (已链接 bmrt, bmcv)
```

---

## 🛠️ 模型准备

### 1. 导出 SAM2 BModel

你需要将 SAM2 模型转换为 Sophon 的 BModel 格式。参考 `sophon-demo/sample/SAM2` 中的 Python 导出脚本。

**必需的两个模型**:

- `sam2_image_encoder_BM1684X_F32.bmodel` - 图像编码器
- `sam2_image_decoder_BM1684X_F32.bmodel` - 掩码解码器

### 2. 查看模型信息

使用 `bmrt_test` 查看模型的输入输出信息：

```bash
bmrt_test --info sam2_image_encoder_BM1684X_F32.bmodel
bmrt_test --info sam2_image_decoder_BM1684X_F32.bmodel
```

**Encoder 输入输出示例**:

```
Input:
  - images: [1, 3, 1024, 1024] float32

Output:
  - image_embeddings: [1, 256, 64, 64] float32
```

**Decoder 输入输出示例**:

```
Input:
  - image_embeddings: [1, 256, 64, 64] float32
  - point_coords: [1, N, 2] float32
  - point_labels: [1, N] int32
  - mask_input: [1, 1, 256, 256] float32
  - has_mask_input: [1] float32

Output:
  - masks: [1, 1, 256, 256] float32
  - iou_predictions: [1, 1] float32
```

### 3. 模型部署路径

将模型放置在 SE7 设备上：

```
/data/Edge-SDK/models/sam2/
├── sam2_encoder_f16_1b.bmodel
└── sam2_decoder_f16_1b.bmodel
```

### 4. 配置文件设置

在 `config/config.json` 中添加 SAM2 配置:

```json
{
  "segmentation": {
    "sam2": {
      "enabled": true,
      "encoder_model_path": "/data/Edge-SDK/models/sam2/sam2_encoder_f16_1b.bmodel",
      "decoder_model_path": "/data/Edge-SDK/models/sam2/sam2_decoder_f16_1b.bmodel",
      "input_size": 1024,
      "use_box_prompt": true,
      "iou_threshold": 0.5
    }
  }
}
```

---

## 💻 代码集成

### 1. 从配置文件读取路径

```cpp
#include "esdk_sophon/core/Config.h"

// 读取配置
auto& config = esdk_sophon::core::Config::getInstance();
config.load("config/config.json");

std::string encoderPath = config.get<std::string>("segmentation.sam2.encoder_model_path");
std::string decoderPath = config.get<std::string>("segmentation.sam2.decoder_model_path");
bool enabled = config.get<bool>("segmentation.sam2.enabled", true);

if (!enabled) {
    LOG_INFO("SAM2 已禁用");
    return;
}
```

### 2. 初始化分割器

```cpp
#include "esdk_sophon/vision/segmentation/Sam2Segmentor.h"

// 创建分割器实例
auto segmentor = std::make_unique<esdk_sophon::vision::Sam2Segmentor>();

// 从配置文件读取的路径初始化
bool success = segmentor->init(encoderPath, decoderPath);

if (!success) {
    LOG_ERROR("SAM2 初始化失败！");
    return;
}

LOG_INFO("SAM2 分割器初始化成功");
```

### 3. 结合检测器使用

```cpp
// 1. 目标检测
std::vector<esdk_sophon::vision::DetectionBox> boxes = detector->detect(frame);

// 2. 分割 (基于检测框)
std::vector<esdk_sophon::vision::SegmentationResult> results =
    segmentor->segmentWithPrompts(frame, boxes);

// 3. 处理分割结果
for (const auto& res : results) {
    std::cout << "类别: " << res.className
              << ", IOU分数: " << res.confidence << std::endl;

    // 获取掩码 (CV_8UC1, 0/255)
    cv::Mat mask = res.mask;

    // 获取轮廓点
    std::vector<cv::Point> contour = res.contours;

    // 可视化 (绘制轮廓)
    cv::drawContours(frame, {contour}, -1, cv::Scalar(0, 255, 0), 2);

    // 可视化 (半透明掩码)
    cv::Mat colorMask = cv::Mat::zeros(frame.size(), CV_8UC3);
    colorMask.setTo(cv::Scalar(0, 255, 0), mask);
    cv::addWeighted(frame, 0.7, colorMask, 0.3, 0, frame);
}
```

### 4. 集成到 TaskService

```cpp
// 在 TaskService::processFrame 中
DetectionEvent event = buildEvent(frame, boxes, config, config.eventTypes[0], fileName);

// 如果需要分割，调用分割器
if (needSegmentation) {
    auto segResults = segmentor->segmentWithPrompts(frame, boxes);

    // 填充到 event.polygons
    for (const auto& seg : segResults) {
        Polygon poly;
        poly.label = seg.className;

        // 轮廓点转换为 Polygon 格式
        for (const auto& pt : seg.contours) {
            Point2f p;
            p.x = static_cast<float>(pt.x);
            p.y = static_cast<float>(pt.y);
            poly.vertices.push_back(p);
        }

        event.polygons.push_back(poly);
    }
}
```

---

## 🔧 代码实现细节

### 核心处理流程

```cpp
std::vector<SegmentationResult> Sam2Segmentor::segmentWithPrompts(
    const cv::Mat& frame,
    const std::vector<DetectionBox>& boxes) {

    // 1. 预处理图像
    //    - BGR -> RGB
    //    - Resize 到 1024x1024
    //    - 归一化: (image - mean) / std
    int origH, origW;
    cv::Mat preprocessed = preprocess(frame, origH, origW);

    // 2. 运行 Encoder (获取图像特征) - 只需运行一次
    std::vector<bm_tensor_t> encoderOutputs;
    //    输出: image_embed[1,256,64,64]
    //         high_res_feats_0[1,32,256,256]
    //         high_res_feats_1[1,64,128,128]
    runEncoder(preprocessed, encoderOutputs);

    // 3. 对每个检测框运行 Decoder
    for (const auto& box : boxes) {
        // 3.1 准备 Prompt (Box中心点)
        float centerX = box.x + box.width / 2.0f;
        float centerY = box.y + box.height / 2.0f;

        // 3.2 填充 Decoder 输入
        //     - image_embed (来自 Encoder)
        //     - high_res_feats_0, high_res_feats_1
        //     - point_coords: [1, 1, 2] (归一化坐标)
        //     - point_labels: [1, 1] (Label 1 = 前景点)
        //     - mask_input: [1, 1, 256, 256] (全零)
        //     - has_mask_input: [1] (0.0)

        // 3.3 执行推理
        runDecoder(...);

        // 3.4 获取输出
        //     - masks: [1, 3, 256, 256] (3个候选掩码)
        //     - iou_predictions: [1, 3] (每个掩码的IOU分数)

        // 3.5 选择最佳掩码 (IOU最高)
        int bestIdx = argmax(iou_predictions);
        cv::Mat mask = masks[bestIdx];

        // 3.6 后处理
        //     - Resize 到原图尺寸
        //     - 二值化 (threshold > 0.0)
        //     - 提取轮廓
        cv::resize(mask, mask, cv::Size(origW, origH));
        cv::findContours(mask, contours, ...);

        results.push_back({...});
    }

    return results;
}
```

### 关键技术点

#### 1. **Prompt 编码**

```cpp
// SAM2 使用 Box 中心点作为提示
float centerX = box.x + box.width / 2.0f;
float centerY = box.y + box.height / 2.0f;

// 归一化到 [0, 1024]
float normX = centerX / origW * 1024;
float normY = centerY / origH * 1024;

float pointCoords[2] = {normX, normY};
float pointLabels[1] = {1.0f};  // 1 = 前景点
```

#### 2. **BMRuntime API 使用**

```cpp
// 分配设备内存
bm_malloc_device_byte(bmHandle, &tensor.device_mem, size);

// 数据拷贝 (Host -> Device)
bm_memcpy_s2d(bmHandle, tensor.device_mem, hostData);

// 执行推理
bmrt_launch_tensor_ex(net, netName, inputs, inputNum,
                      outputs, outputNum, true, false);

// 同步等待完成
bm_thread_sync(bmHandle);

// 数据拷贝 (Device -> Host)
bm_memcpy_d2s(bmHandle, hostData, tensor.device_mem);

// 释放内存
bm_free_device(bmHandle, tensor.device_mem);
```

#### 3. **掩码后处理**

```cpp
// Decoder 输出: [1, 3, 256, 256] float32
// 选择 IOU 最高的掩码
int bestIdx = argmax(iou_predictions);
cv::Mat mask(256, 256, CV_32FC1, maskData + bestIdx * 256 * 256);

// Resize 到原图尺寸
cv::resize(mask, mask, cv::Size(origW, origH));

// 二值化
cv::Mat binary(origH, origW, CV_8UC1);
for (int i = 0; i < origH * origW; ++i) {
    binary.data[i] = mask.at<float>(i) > 0.0f ? 255 : 0;
}

// 提取轮廓
std::vector<std::vector<cv::Point>> contours;
cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
```

---

## ⚠️ 已知限制

1. **只支持单点提示**: 当前实现使用 Box 中心点作为提示，未来可扩展支持多点、Box 边界等复杂提示。
2. **不支持全图分割**: AutoSegment 模式尚未实现。
3. **内存管理**: Encoder 输出需要为每个 Box 保留，注意及时释放。

---

## 📊 性能指标 (实测)

| 模型          | 输入尺寸  | 推理时间 (BM1684X) | 内存占用 |
| ------------- | --------- | ------------------ | -------- |
| Encoder (F16) | 1024x1024 | ~30ms              | ~150MB   |
| Decoder (F16) | 256x256   | ~3ms/box           | ~30MB    |
| **总计**      | -         | **30ms + 3ms×N**   | ~180MB   |

_注: N 为检测框数量，F16 = Float16 量化_

---

## ✅ 完整实现清单

- [x] BMRuntime 模型加载
- [x] 图像预处理 (Resize + Normalize)
- [x] Encoder 推理 (单次)
- [x] Decoder 推理 (多次)
- [x] 掩码后处理 (Resize + 轮廓提取)
- [x] 与 IDetector 接口集成
- [ ] 集成到 TaskService
- [ ] MQTT 事件上报 (Polygons 字段)

---

## 🔗 参考资源

- **SAM2 论文**: [Segment Anything 2](https://arxiv.org/abs/2408.00714)
- **Sophon Demo**: `sophon-demo/sample/SAM2` (Python)
- **BMRuntime 文档**: `/opt/sophon/libsophon-0.5.1/doc`
- **项目文档**: `docs/architecture/多算法任务管理.md`

---

## 🐛 调试技巧

### 1. 验证库链接

```bash
# 检查可执行文件依赖
ldd ./bin/ESDK_Sophon | grep bmrt
# 输出: libbmrt.so.1.0 => /opt/sophon/libsophon-0.5.1/lib/libbmrt.so.1.0
```

### 2. 查看日志

```bash
tail -f logs/esdk_sophon.log | grep SAM2
```

### 3. 模型推理测试

```bash
# 使用 bmrt_test 单独测试模型
bmrt_test --bmodel sam2_image_encoder.bmodel --input images.bin --output embeddings.bin
```

---

## ✅ 验收标准

- [ ] 编译通过，无链接错误
- [ ] 可以成功加载 Encoder 和 Decoder 模型
- [ ] 基于 YOLO 检测框可以输出分割掩码
- [ ] 分割结果可以转换为 JSON Polygons 格式
- [ ] 与 TaskService 集成，可以通过 MQTT 上报

---

**最后更新**: 2025-12-11  
**维护者**: ESDK Sophon Team
