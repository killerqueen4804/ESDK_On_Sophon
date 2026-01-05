# 八股 - SAM2 分割模型

**整理日期**: 2025-12-15  
**相关项目**: ESDK_On_Sophon (SAM2 实例分割)

---

## 📌 SAM2 基础概念

### 1. 什么是 SAM2?

**SAM2 (Segment Anything Model 2)** 是 Meta AI 在 2024 年发布的**通用分割模型**,是 SAM 的升级版。

**核心特点**:

- ✅ **Zero-Shot 分割**: 不需要针对特定类别训练,泛化能力强
- ✅ **Prompt 驱动**: 通过 Point/Box/Mask Prompt 指示分割目标
- ✅ **视频分割**: 支持视频序列的时序一致性分割 (Video SAM)
- ✅ **高效推理**: 相比 SAM v1 速度提升 6 倍

**与传统分割模型的区别**:
| 特性 | 传统分割 (如 Mask R-CNN) | SAM2 |
|------|----------------------|------|
| **训练方式** | 需要类别标注 | 无监督预训练 |
| **泛化能力** | 只能分割训练过的类别 | 可分割任意目标 |
| **输入** | 只有图像 | 图像 + Prompt |
| **应用** | 特定场景 | 通用分割 |

---

## 🎯 SAM2 架构设计

### 整体架构

```
输入图像 → Encoder → Image Embeddings → Decoder + Prompt → Masks
         (重量级)      (可复用)          (轻量级,快速)
```

### 1. Image Encoder

**作用**: 提取图像的高维特征表示

**架构**: **Vision Transformer (ViT-H)**

- 输入: `[1, 3, 1024, 1024]` (RGB 图像)
- 输出:
  - `image_embed`: `[1, 256, 64, 64]` (主特征)
  - `high_res_feats_0`: `[1, 32, 256, 256]` (高分辨率特征)
  - `high_res_feats_1`: `[1, 64, 128, 128]` (中分辨率特征)

**特点**:

- 计算量大,但**只需推理一次**
- 同一张图片可以共享 embeddings,分割多个目标

### 2. Prompt Encoder

**作用**: 将用户提示 (Point/Box/Mask) 编码为向量

**输入类型**:

- `point_coords`: `[1, N, 2]` - N 个点的坐标
- `point_labels`: `[1, N]` - 点的标签 (0=背景, 1=前景, 2=左上角, 3=右下角)
- `mask_input`: `[1, 1, 256, 256]` - 上一次的 mask (可选)

**编码方式**:

- Point/Box: 使用 **位置编码** (Positional Embedding)
- Mask: 使用 **卷积层** 提取特征

### 3. Mask Decoder

**作用**: 融合 Image Embeddings 和 Prompt Embeddings,生成分割掩码

**架构**: **轻量级 Transformer** (4 层)

- 输入:
  - Image embeddings (来自 Encoder)
  - Prompt embeddings (来自 Prompt Encoder)
- 输出:
  - `masks`: `[1, 4, 256, 256]` - 4 个候选 mask
  - `iou_predictions`: `[1, 4]` - 每个 mask 的质量分数

**特点**:

- 计算量小,**速度快** (10ms 级别)
- 输出多个候选,选择 IOU 最高的

---

## 💡 SAM2 Prompt 类型详解

### 1. Point Prompt (点提示)

**使用场景**: 用户交互式分割,点击目标上的点

```cpp
// 单个前景点
float pointCoords[4] = {x, y, 0, 0};
float pointLabels[2] = {1.0f, -1.0f};  // 1=前景, -1=忽略

// 前景点 + 背景点
float pointCoords[4] = {x1, y1, x2, y2};
float pointLabels[2] = {1.0f, 0.0f};   // 1=前景, 0=背景
```

**Label 定义**:

- `0`: 背景点 (告诉模型"这不是目标")
- `1`: 前景点 (告诉模型"这是目标")
- `-1`: 忽略 (填充用)

**优点**:

- 灵活,适合用户不确定边界的场景
- 可以迭代添加点来优化分割

**缺点**:

- 精度依赖点的位置
- 对于复杂形状需要多个点

**项目中的例子**:

```cpp
// ❌ 旧实现: 只用中心点 (精度不够)
float cx = (box.x + box.width / 2.0f) * scale;
float cy = (box.y + box.height / 2.0f) * scale;
float pointCoords[4] = {cx, cy, 0.0f, 0.0f};
float pointLabels[2] = {1.0f, -1.0f};
```

---

### 2. Box Prompt (框提示) ⭐ 本项目使用

**使用场景**: 已知目标的 Bounding Box (目标检测后的分割)

```cpp
// 方式 1: 使用 box 参数 (官方 Python API)
box = [x1, y1, x2, y2]  # 左上角 + 右下角

// 方式 2: 转换为 point_coords (本项目实现)
float pointCoords[4] = {x1, y1, x2, y2};
float pointLabels[2] = {2.0f, 3.0f};  // 2=左上角, 3=右下角
```

**Label 定义**:

- `2`: 左上角 (Top-Left)
- `3`: 右下角 (Bottom-Right)

**优点**:

- ✅ **精度高**: 提供完整边界信息 (4 个坐标)
- ✅ **鲁棒性强**: 不依赖单点位置
- ✅ **适合自动化**: 可直接使用检测器输出

**缺点**:

- 需要准确的检测框
- 无法处理检测框外的目标

**项目中的例子**:

```cpp
// ✅ 新实现: 用左上角和右下角
float scale = static_cast<float>(SAM2_INPUT_SIZE) / std::max(origH, origW);
float x1 = box.x * scale;
float y1 = box.y * scale;
float x2 = (box.x + box.width) * scale;
float y2 = (box.y + box.height) * scale;

float pointCoords[4] = {x1, y1, x2, y2};
float pointLabels[2] = {2.0f, 3.0f};
```

---

### 3. Mask Prompt (掩码提示)

**使用场景**: 迭代优化分割结果

```cpp
// 使用上一次的 mask 作为输入
std::vector<float> maskInput(1 * 1 * 256 * 256, 0.0f);  // 第一次全 0
// ... 或者使用上一次的输出 mask
```

**输入 Shape**: `[1, 1, 256, 256]`

**优点**:

- 可以多轮迭代优化
- 结合 Point/Box 提供更精细的控制

**缺点**:

- 计算量大 (需要多次 Decoder 推理)
- 首次分割仍需要 Point/Box

**本项目实现**:

```cpp
// Input 5: mask_input [1, 1, 256, 256] - 全 0 (不使用 Mask Prompt)
int maskInputSize = 1 * 1 * 256 * 256;
std::vector<float> maskInput(maskInputSize, 0.0f);
bm_memcpy_s2d(bmHandle, inputs[5].device_mem, maskInput.data());
```

---

## 🔧 SAM2 推理流程

### 完整流程

```cpp
// 1. 图像预处理
cv::Mat preprocessed = preprocess(frame, origH, origW);
// - Resize: 长边缩放到 1024
// - Padding: 右下角填充 0 到 1024x1024
// - Normalize: (x/255 - mean) / std

// 2. Encoder 推理 (重量级,只需一次)
std::vector<bm_tensor_t> embeddings;
runEncoder(preprocessed, embeddings);

// 3. 对每个检测框推理 Decoder (轻量级,可并行)
for (const auto& box : boxes) {
    // 3.1 构建 Prompt (Box → Point Coords)
    float scale = 1024.0f / max(origH, origW);
    float x1 = box.x * scale;
    float y1 = box.y * scale;
    float x2 = (box.x + box.width) * scale;
    float y2 = (box.y + box.height) * scale;

    // 3.2 Decoder 推理
    cv::Mat mask;
    float iou;
    runDecoder(embeddings, box, origH, origW, mask, iou);

    // 3.3 后处理
    // - 选择 IOU 最高的 mask
    // - Resize 回原图尺寸
    // - 提取轮廓
}
```

### 坐标变换细节 ⚠️ 重点

**问题**: 为什么要将坐标映射到 1024x1024?

**原因**: SAM2 Encoder 输入固定为 `[1, 3, 1024, 1024]`

**预处理流程**:

```cpp
// 1. 计算缩放比例
float scale = 1024.0f / std::max(origH, origW);

// 2. 等比例缩放
int newH = origH * scale;
int newW = origW * scale;
cv::resize(rgb, resized, cv::Size(newW, newH));

// 3. 右下角 Padding (左上角对齐!)
cv::Mat padded = cv::Mat::zeros(1024, 1024, CV_8UC3);
resized.copyTo(padded(cv::Rect(0, 0, newW, newH)));
```

**示例计算** (原图 `1920x1080`):

- `scale = 1024 / 1920 = 0.533`
- `newW = 1920 * 0.533 = 1024` (满的)
- `newH = 1080 * 0.533 = 576`
- **Padding**: 右边 0 像素,下边 `1024-576=448` 像素

**Prompt 坐标变换**:

```cpp
// 原图坐标 → 缩放后坐标
float x1 = box.x * scale;  // ✅ 正确!
float y1 = box.y * scale;

// 注意: 不需要考虑 padding!
// 因为 padding 在右下角,坐标系是左上角对齐
```

**Mask 后处理**:

```cpp
// 1. 去除 Padding (裁剪有效区域)
int validH = origH * scale;
int validW = origW * scale;
int maskValidH = validH / 4;  // Mask 尺寸是 256x256
int maskValidW = validW / 4;
cv::Mat maskCrop = mask256(cv::Rect(0, 0, maskValidW, maskValidH));

// 2. Resize 回原图尺寸
cv::resize(maskCrop, mask, cv::Size(origW, origH));
```

---

## 🎓 面试高频问题

### Q1: SAM2 为什么比传统分割模型泛化能力强?

**A**:

1. **训练数据规模**: SAM2 在 **SA-1B** 数据集 (11 亿张 mask) 上训练,覆盖各种场景和类别
2. **Prompt 驱动设计**: 不依赖固定类别,通过 Prompt 适应任意目标
3. **自监督学习**: 使用数据引擎迭代生成伪标签,不需要人工标注所有类别
4. **Zero-Shot Transfer**: 在训练时学习通用的"目标"概念,而非特定类别

**对比**:

- Mask R-CNN: 只能分割 COCO 80 类,遇到新类别需要重新训练
- SAM2: 可以分割任意类别,不需要重新训练

---

### Q2: SAM2 的 Encoder 为什么只需推理一次?

**A**:

- **Encoder 输出**: `image_embeddings` 是**图像的全局特征表示**,与具体目标无关
- **Decoder 输入**: 每个目标的 Prompt 不同,但共享同一份 embeddings
- **性能优化**:
  - Encoder 计算量占 95% (ViT-H 模型)
  - 分割 N 个目标,只需 1 次 Encoder + N 次 Decoder
  - 如果每次都重新推理 Encoder,速度会慢 N 倍

**项目中的实现**:

```cpp
// 1. Encoder 推理一次 (重量级)
std::vector<bm_tensor_t> embeddings;
runEncoder(preprocessed, embeddings);

// 2. Decoder 推理 N 次 (轻量级)
for (const auto& box : boxes) {
    runDecoder(embeddings, box, origH, origW, mask, iou);  // 复用 embeddings
}
```

---

### Q3: Box Prompt 和 Point Prompt 在代码层面有什么区别?

**A**:

**1. 数据结构相同,只是值不同**:

```cpp
// Point Prompt
float pointCoords[4] = {cx, cy, 0, 0};       // 中心点 + 填充
float pointLabels[2] = {1.0f, -1.0f};        // 前景点 + 忽略

// Box Prompt
float pointCoords[4] = {x1, y1, x2, y2};     // 左上角 + 右下角
float pointLabels[2] = {2.0f, 3.0f};         // 左上角 + 右下角
```

**2. 模型内部处理不同**:

- `label=1`: 位置编码 + "这是目标"的语义信息
- `label=2/3`: 位置编码 + "这是边界"的语义信息
- SAM2 Decoder 会根据 label 调整注意力机制

**3. 精度差异**:

- Point Prompt: 只知道目标"在哪",不知道"有多大"
- Box Prompt: 同时知道"在哪"和"有多大",精度更高

---

### Q4: 如果检测框不准确,如何提高 SAM2 分割质量?

**A**:

**方案 1: Box 扩展**

```cpp
// 给检测框加 margin (放大 10%)
float margin = 0.1f;
float x1 = box.x - box.width * margin;
float y1 = box.y - box.height * margin;
float x2 = box.x + box.width * (1 + margin);
float y2 = box.y + box.height * (1 + margin);
```

**方案 2: 多点 Prompt (Box + Point)**

```cpp
// 6 个点: 左上、右下、中心、4个边中点
float pointCoords[12] = {
    x1, y1,              // 左上角
    x2, y2,              // 右下角
    cx, cy,              // 中心点
    cx, y1,              // 上边中点
    cx, y2,              // 下边中点
    x1, cy,              // 左边中点
    x2, cy               // 右边中点
};
float pointLabels[6] = {2, 3, 1, 1, 1, 1};
```

**方案 3: 迭代优化 (Mask Prompt)**

```cpp
// 第一轮: 使用 Box Prompt
cv::Mat mask1 = sam2.segment(frame, box);

// 第二轮: 使用 mask1 作为输入,refine
cv::Mat mask2 = sam2.segmentWithMask(frame, box, mask1);
```

**方案 4: 提高检测器精度**

- 使用更强的检测模型 (YOLOv10 → YOLOv11)
- 增加 NMS 阈值,减少误检
- 针对特定场景 fine-tune 检测器

---

### Q5: SAM2 的 4 个候选 Mask 是如何生成的?

**A**:

**1. 多尺度预测**:

- Decoder 在不同的 attention 阶段生成多个 mask
- 每个 mask 对应不同的"粒度"

**2. 示例**:

```
Mask 0: 最精细,严格按 Box 边界
Mask 1: 稍微扩展,包含目标的阴影
Mask 2: 更粗略,可能包含背景
Mask 3: 最粗略,可能是整个物体群
```

**3. 选择策略**:

```cpp
// 计算每个 mask 的 IOU 分数
std::vector<float> ious(4);
bm_memcpy_d2s(bmHandle, ious.data(), outputs[1].device_mem);

// 选择 IOU 最高的
int bestIdx = std::max_element(ious.begin(), ious.end()) - ious.begin();
```

**4. IOU 预测**:

- 模型输出的 IOU 是**预测值**,不是真实 IOU
- 表示模型对该 mask 的"置信度"
- 通常最精细的 mask (Mask 0) 有最高的 IOU

---

## 🔍 易错点

### 1. ❌ 坐标系混淆

```cpp
// ❌ 错误: 忘记缩放坐标
float x1 = box.x;  // 原图坐标,不匹配 1024x1024

// ✅ 正确: 必须映射到 1024x1024
float scale = 1024.0f / std::max(origH, origW);
float x1 = box.x * scale;
```

### 2. ❌ Encoder 输出顺序和 Decoder 输入顺序不一致

```cpp
// ❌ 错误: 直接使用 Encoder 输出
inputs[0] = embeddings[0];  // 实际是 high_res_0,不是 image_embed!

// ✅ 正确: 重新映射
// Encoder 输出: [high_res_0, high_res_1, image_embed]
// Decoder 输入: [image_embed, high_res_0, high_res_1]
inputs[0] = embeddings[2];  // image_embed
inputs[1] = embeddings[0];  // high_res_0
inputs[2] = embeddings[1];  // high_res_1
```

### 3. ❌ Mask 后处理忘记去除 Padding

```cpp
// ❌ 错误: 直接 resize 回原图
cv::resize(mask256, mask, cv::Size(origW, origH));  // 包含 padding 区域!

// ✅ 正确: 先裁剪有效区域
int validH = origH * scale;
int validW = origW * scale;
cv::Mat maskCrop = mask256(cv::Rect(0, 0, validW/4, validH/4));
cv::resize(maskCrop, mask, cv::Size(origW, origH));
```

### 4. ❌ Point Label 使用错误

```cpp
// ❌ 错误: Box Prompt 用了 Point Label
float pointLabels[2] = {1.0f, 1.0f};  // 两个前景点,不是 Box!

// ✅ 正确: Box Prompt 专用 Label
float pointLabels[2] = {2.0f, 3.0f};  // 2=左上角, 3=右下角
```

---

## 📚 参考资料

- [SAM2 官方 GitHub](https://github.com/facebookresearch/segment-anything-2)
- [SAM2 论文](https://arxiv.org/abs/2408.00714)
- [算能 SAM2 Demo](https://github.com/sophon-ai-algo/sophon-demo/tree/main/sample/SAM2)
- [Meta AI Blog - SAM2 介绍](https://ai.meta.com/blog/segment-anything-2/)

---

**整理人**: AI Assistant  
**审核人**: 待审核  
**最后更新**: 2025-12-15
