# FastDeploy API 修正记录

**日期**: 2025-11-01  
**问题**: 编译错误 - FastDeploy API 调用不正确

---

## 🔧 编译错误分析

### 错误 1: SetSophgoDevice() 不存在

```
error: 'struct fastdeploy::RuntimeOption' has no member named 'SetSophgoDevice'
```

**原因**: FastDeploy 的 Sophon 后端不需要单独设置设备 ID  
**解决**: 只调用 `UseSophgo()` 即可

```cpp
// ❌ 错误
runtimeOption_->UseSophgo();
runtimeOption_->SetSophgoDevice(0);

// ✅ 正确
runtimeOption_->UseSophgo();
```

---

### 错误 2: PPYOLOE 构造函数参数错误

```
error: no matching function for call to 'fastdeploy::vision::detection::PPYOLOE::PPYOLOE(
    std::string&, const char [1], fastdeploy::RuntimeOption&, fastdeploy::ModelFormat)'
```

**原因**: PP-YOLOE 需要 3 个文件路径参数: model_file, params_file, config_file  
**解决**: 正确传递 3 个文件路径

```cpp
// ❌ 错误 (缺少config_file参数)
model_ = std::make_unique<fastdeploy::vision::detection::PPYOLOE>(
    modelPath_,
    "",
    *runtimeOption_,
    fastdeploy::ModelFormat::SOPHGO
);

// ✅ 正确 (3个文件路径 + RuntimeOption + ModelFormat)
model_ = std::make_unique<fastdeploy::vision::detection::PPYOLOE>(
    modelPath_,          // model_file: ppyoloe_xxx.bmodel
    "",                  // params_file: 空字符串 (bmodel不需要)
    configFile_,         // config_file: infer_cfg.yml
    *runtimeOption_,
    fastdeploy::ModelFormat::SOPHGO
);
```

**FastDeploy API 签名**:

```cpp
PPYOLOE(
    const std::string& model_file,      // 模型文件
    const std::string& params_file,     // 参数文件 (可选)
    const std::string& config_file,     // 配置文件 (infer_cfg.yml)
    const RuntimeOption& option,
    const ModelFormat& format
);
```

---

### 错误 3: SetConfThreshold() 和 SetNMSThreshold() 不存在

```
error: 'class fastdeploy::vision::detection::PaddleDetPostprocessor' has no member named 'SetConfThreshold'
error: 'class fastdeploy::vision::detection::PaddleDetPostprocessor' has no member named 'SetNMSThreshold'
```

**原因**: PaddleDetection 系列的后处理器 API 不同于 YOLO 系列  
**解决**: 使用 `ApplyNMS()` 方法

```cpp
// ❌ 错误 (YOLO系列的API)
model_->GetPostprocessor().SetConfThreshold(0.5f);
model_->GetPostprocessor().SetNMSThreshold(0.45f);

// ✅ 正确 (PaddleDet系列的API)
model_->GetPostprocessor().ApplyNMS();
```

**说明**:

- PaddleDetection 的置信度阈值和 NMS 阈值在`infer_cfg.yml`中配置
- `ApplyNMS()`方法会读取配置文件中的参数
- 如需动态修改阈值,可在后处理阶段手动过滤结果

---

## 📝 修改的文件

### 1. `include/esdk_sophon/vision/IDetector.h`

添加 `configFile` 字段到 `DetectorConfig`:

```cpp
struct DetectorConfig {
    std::string modelPath;            ///< 模型文件路径
    std::string configFile;           ///< 配置文件路径 (PaddleDet需要infer_cfg.yml) ⭐ 新增
    std::vector<std::string> classes; ///< 类别名称列表
    float confidenceThreshold;        ///< 置信度阈值
    float nmsThreshold;               ///< NMS阈值
    int inputWidth;                   ///< 模型输入宽度
    int inputHeight;                  ///< 模型输入高度

    DetectorConfig()
        : confidenceThreshold(0.5f)
        , nmsThreshold(0.45f)
        , inputWidth(640)
        , inputHeight(640) {}
};
```

---

### 2. `include/esdk_sophon/vision/PPYoloeDetector.h`

添加 `configFile_` 成员变量:

```cpp
private:
    // 配置参数
    std::string modelPath_;              ///< 模型文件路径
    std::string configFile_;             ///< 配置文件路径 (infer_cfg.yml) ⭐ 新增
    std::vector<std::string> classes_;   ///< 类别名称列表
    float confidenceThreshold_;          ///< 置信度阈值
    int inputWidth_;                     ///< 模型输入宽度
    int inputHeight_;                    ///< 模型输入高度
```

---

### 3. `src/vision/detector/PPYoloeDetector.cpp`

#### 修改 1: 移除 SetSophgoDevice()

```cpp
// 步骤3: 创建RuntimeOption
runtimeOption_ = std::make_unique<fastdeploy::RuntimeOption>();
runtimeOption_->UseSophgo();  // 只需要这一行
// ❌ 删除: runtimeOption_->SetSophgoDevice(0);
```

#### 修改 2: 正确的 PPYOLOE 构造

```cpp
// 步骤4: 加载PP-YOLOE模型
std::string paramsFile = "";

model_ = std::make_unique<fastdeploy::vision::detection::PPYOLOE>(
    modelPath_,
    paramsFile,
    configFile_,     // ⭐ 使用配置中的configFile
    *runtimeOption_,
    fastdeploy::ModelFormat::SOPHGO
);
```

#### 修改 3: 使用 ApplyNMS()

```cpp
// 步骤5: 配置后处理
model_->GetPostprocessor().ApplyNMS();  // ⭐ PaddleDet的API

// ❌ 删除:
// model_->GetPostprocessor().SetConfThreshold(confidenceThreshold_);
// model_->GetPostprocessor().SetNMSThreshold(0.45f);
```

---

### 4. `tests/test_detector.cpp`

更新测试配置添加 configFile:

```cpp
DetectorConfig createTestConfig(bool withValidModel = false) {
    DetectorConfig config;

    if (withValidModel) {
        config.modelPath = "/workspace/models/ppyoloe_crn_m_300e_coco.bmodel";
        config.configFile = "/workspace/models/infer_cfg.yml";  // ⭐ 新增
    } else {
        config.modelPath = "/path/to/ppyoloe.bmodel";
        config.configFile = "/path/to/infer_cfg.yml";  // ⭐ 新增
    }

    // ... 其他配置
    return config;
}
```

---

## 🎯 旧项目的正确用法

参考 `esdk_on_sophon_old/src/yolov10/image_processor_yolov10.cpp:159-177`:

```cpp
const std::string& model_file = "/data/Edge-SDK/models/PaddleDetection/ppyoloe_crn_m_300e_coco_289.bmodel";
const std::string& config_file = "/data/Edge-SDK/models/PaddleDetection/infer_cfg.yml";
std::string params_file;  // 空字符串

auto option = fastdeploy::RuntimeOption();
option.UseSophgo();  // 只需要这一行,不需要SetSophgoDevice

auto format = fastdeploy::ModelFormat::SOPHGO;

model = new fastdeploy::vision::detection::PPYOLOE(
    model_file,
    params_file,    // 空字符串
    config_file,    // infer_cfg.yml
    option,
    format
);

model->GetPostprocessor().ApplyNMS();  // PaddleDet的NMS方法
```

---

## 📚 知识点总结

### 1. FastDeploy 不同模型系列的 API 差异

| 模型系列                      | 后处理 API                                  | 配置方式           |
| ----------------------------- | ------------------------------------------- | ------------------ |
| **YOLO 系列** (YOLOv5/v8/v10) | `SetConfThreshold()`<br>`SetNMSThreshold()` | 代码中设置         |
| **PaddleDet 系列** (PPYOLOE)  | `ApplyNMS()`                                | infer_cfg.yml 文件 |

### 2. infer_cfg.yml 配置文件示例

```yaml
mode: paddle
arch: YOLO
min_subgraph_size: 3
Preprocess:
  - interp: 2
    keep_ratio: false
    target_size:
      - 640
      - 640
    type: Resize
  - is_scale: true
    mean:
      - 0.485
      - 0.456
      - 0.406
    std:
      - 0.229
      - 0.224
      - 0.225
    type: NormalizeImage
  - type: Permute
Postprocess:
  name: PPYOLOEPostProcess
  nms:
    nms_threshold: 0.45 # ⭐ NMS阈值在这里配置
  score_threshold: 0.5 # ⭐ 置信度阈值在这里配置
```

### 3. 面试要点

**Q: 为什么 PPYOLOE 需要额外的 config_file 参数?**

A:

1. **PaddleDetection 生态要求**: PaddleDet 模型在训练和导出时会生成配置文件
2. **预处理和后处理参数**: infer_cfg.yml 包含 resize 策略、归一化参数、NMS 阈值等
3. **模型一致性**: 确保推理时的参数与训练时一致
4. **灵活性**: 可以不修改代码只修改配置文件来调整参数

**Q: 能否不使用 config_file?**

A:

- 理论上可以传空字符串,但会导致后处理使用默认参数
- 推荐使用配置文件以保证最佳性能
- 如果没有配置文件,可能需要手动实现后处理逻辑

**Q: FastDeploy 为什么不需要 SetSophgoDevice()?**

A:

- Sophon SDK 的设计:默认使用设备 0
- 如需多设备,在 RuntimeOption 构造时通过环境变量或其他方式指定
- 简化 API,减少用户配置负担

---

## ✅ 验证编译

```bash
cd /workspace/ESDK_On_Sophon/build
make detector
```

**预期结果**: 编译成功,无错误无警告

---

## 📁 文件准备

### 模型文件位置 (示例)

```
/workspace/models/
├── ppyoloe_crn_m_300e_coco.bmodel    # PP-YOLOE模型
└── infer_cfg.yml                      # 配置文件
```

### 如何获取 infer_cfg.yml

1. **方式 1**: 从 PaddleDetection 导出时自动生成

   ```bash
   python tools/export_model.py \
       -c configs/ppyoloe/ppyoloe_crn_m_300e_coco.yml \
       -o weights=ppyoloe_crn_m_300e_coco.pdparams
   ```

2. **方式 2**: 使用默认配置 (见上面的示例)

3. **方式 3**: 从旧项目复制
   ```bash
   cp /data/Edge-SDK/models/PaddleDetection/infer_cfg.yml /workspace/models/
   ```

---

## 🎓 经验总结

1. **参考旧项目代码**: 遇到 API 问题,先看旧项目的正确用法
2. **阅读 FastDeploy 文档**: 不同模型系列 API 可能不同
3. **注意模型格式**: bmodel、onnx、paddle 等格式的 API 调用略有差异
4. **配置文件重要性**: PaddleDetection 生态依赖配置文件

---

**修改人**: GitHub Copilot  
**验证状态**: 待编译验证
