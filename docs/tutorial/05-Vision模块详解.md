# Vision 模块详解 - 视觉处理

> **学习目标**：掌握工厂模式和策略模式在视觉处理中的应用
>
> **核心知识点**：
>
> - 工厂模式（Factory Pattern）
> - 策略模式（Strategy Pattern）
> - 纯虚函数与多态
> - Letterbox 预处理
> - 深度学习推理流程

---

## 📚 目录

1. [模块概述](#模块概述)
2. [设计模式详解](#设计模式详解)
3. [IDetector 接口分析](#idetector-接口分析)
4. [DetectorFactory 工厂实现](#detectorfactory-工厂实现)
5. [PPYoloeDetector 具体实现](#ppyoloedetector-具体实现)
6. [图像预处理 - Letterbox](#图像预处理---letterbox)
7. [面试高频考点](#面试高频考点)
8. [实战练习](#实战练习)

---

## 模块概述

### 文件结构

```
include/esdk_sophon/vision/
├── IDetector.h           # 检测器接口（策略模式）
├── DetectorFactory.h     # 检测器工厂（工厂模式）
├── PPYoloeDetector.h     # PP-YOLOE具体实现
└── VisionUtils.h         # 视觉工具类

src/vision/
├── detector/
│   ├── DetectorFactory.cpp
│   ├── PPYoloeDetector.cpp
│   └── IDetector.cpp
├── VisionUtils.cpp
└── VisionConfigLoader.cpp
```

### 类图

```
                    ┌─────────────────────────┐
                    │    DetectorFactory      │
                    │     (工厂类)            │
                    ├─────────────────────────┤
                    │ + create() : IDetector* │
                    │ + createFromString()    │
                    └───────────┬─────────────┘
                                │ 创建
                                ▼
                    ┌─────────────────────────┐
                    │      IDetector          │
                    │     (抽象接口)          │
                    ├─────────────────────────┤
                    │ + initialize() = 0      │
                    │ + detect() = 0          │
                    │ + getClasses() = 0      │
                    └───────────┬─────────────┘
                                │ 实现
           ┌────────────────────┼────────────────────┐
           ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ PPYoloeDetector │  │  Yolov8Detector │  │  Yolov5Detector │
│   (具体实现)    │  │    (未来扩展)   │  │    (未来扩展)   │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

### 数据流

```
输入图像 (cv::Mat)
      │
      ▼
┌─────────────────┐
│    preprocess   │ ← Letterbox 变换
│   (预处理)      │
└─────────────────┘
      │
      ▼
┌─────────────────┐
│    inference    │ ← FastDeploy + TPU
│   (模型推理)    │
└─────────────────┘
      │
      ▼
┌─────────────────┐
│   postprocess   │ ← 坐标还原、NMS、置信度过滤
│   (后处理)      │
└─────────────────┘
      │
      ▼
DetectionResult (检测结果)
```

---

## 设计模式详解

### 📌 策略模式（Strategy Pattern）

**定义**：定义一系列算法，把它们一个个封装起来，并且使它们可以相互替换。

```cpp
// 抽象策略（接口）
class IDetector {
public:
    virtual DetectionResult detect(const cv::Mat& image) = 0;
};

// 具体策略A
class PPYoloeDetector : public IDetector {
    DetectionResult detect(const cv::Mat& image) override {
        // PP-YOLOE 算法实现
    }
};

// 具体策略B
class Yolov8Detector : public IDetector {
    DetectionResult detect(const cv::Mat& image) override {
        // YOLOv8 算法实现
    }
};

// 上下文（使用策略的类）
class TaskService {
    std::unique_ptr<IDetector> detector_;
public:
    void setDetector(std::unique_ptr<IDetector> detector) {
        detector_ = std::move(detector);  // 策略可替换
    }

    void processFrame(const cv::Mat& image) {
        auto result = detector_->detect(image);  // 多态调用
        // ...
    }
};
```

**策略模式的优势：**

| 优势         | 说明                       |
| ------------ | -------------------------- |
| **算法替换** | 运行时可切换检测算法       |
| **解耦**     | TaskService 不依赖具体算法 |
| **扩展性**   | 添加新算法无需修改已有代码 |
| **可测试**   | 可以 Mock 接口进行单元测试 |

### 📌 工厂模式（Factory Pattern）

**定义**：定义一个创建对象的接口，让子类决定实例化哪个类。

```cpp
// 简单工厂模式
class DetectorFactory {
public:
    static std::unique_ptr<IDetector> create(DetectorType type,
                                              const DetectorConfig& config) {
        switch (type) {
            case DetectorType::PPYOLOE:
                return std::make_unique<PPYoloeDetector>();
            case DetectorType::YOLOV8:
                return std::make_unique<Yolov8Detector>();
            // ...
        }
    }
};

// 使用
auto detector = DetectorFactory::create(DetectorType::PPYOLOE, config);
detector->detect(image);
```

**工厂模式的优势：**

| 优势         | 说明                     |
| ------------ | ------------------------ |
| **封装创建** | 隐藏复杂的初始化逻辑     |
| **解耦**     | 使用者不需要知道具体类名 |
| **统一管理** | 集中处理配置、日志、错误 |
| **易于扩展** | 添加新类型只修改工厂     |

### 策略模式 vs 工厂模式

| 模式         | 关注点 | 解决问题         |
| ------------ | ------ | ---------------- |
| **策略模式** | 行为   | 如何**使用**算法 |
| **工厂模式** | 创建   | 如何**创建**对象 |

**项目中的组合使用：**

```cpp
// 工厂模式：创建检测器
auto detector = DetectorFactory::create(DetectorType::PPYOLOE, config);

// 策略模式：使用检测器
taskService.setDetector(std::move(detector));
```

---

## IDetector 接口分析

### 接口定义

```cpp
class IDetector {
public:
    virtual ~IDetector() = default;

    // 纯虚函数：子类必须实现
    virtual bool initialize(const DetectorConfig& config) = 0;
    virtual DetectionResult detect(const cv::Mat& image) = 0;
    virtual const std::vector<std::string>& getClasses() const = 0;
    virtual cv::Size getInputSize() const = 0;
    virtual std::string getName() const = 0;
    virtual bool isInitialized() const = 0;

protected:
    IDetector() = default;  // 保护构造函数

    // 禁止拷贝
    IDetector(const IDetector&) = delete;
    IDetector& operator=(const IDetector&) = delete;

    // 允许移动
    IDetector(IDetector&&) = default;
    IDetector& operator=(IDetector&&) = default;
};
```

### 关键设计决策

#### 1. 虚析构函数

```cpp
virtual ~IDetector() = default;
```

📌 **面试必考！**

**Q: 为什么基类析构函数必须是虚函数？**

**A:**

```cpp
// 没有虚析构函数时
IDetector* p = new PPYoloeDetector();
delete p;  // 只调用 IDetector 的析构，PPYoloeDetector 的资源泄漏！

// 有虚析构函数时
delete p;  // 先调用 PPYoloeDetector 析构，再调用 IDetector 析构 ✓
```

#### 2. 保护构造函数

```cpp
protected:
    IDetector() = default;
```

**为什么用 protected 而不是 public？**

- 防止直接实例化接口类
- 只能通过派生类或工厂创建对象
- 明确表达"这是一个接口"的设计意图

#### 3. 禁止拷贝，允许移动

```cpp
IDetector(const IDetector&) = delete;
IDetector& operator=(const IDetector&) = delete;

IDetector(IDetector&&) = default;
IDetector& operator=(IDetector&&) = default;
```

**为什么禁止拷贝？**

- 检测器内部持有模型资源（TPU 内存）
- 深拷贝代价高昂
- 通常使用 `unique_ptr` 管理所有权

### 数据结构

```cpp
// 单个检测框
struct DetectionBox {
    int classId;           // 类别ID
    std::string className; // 类别名称
    float confidence;      // 置信度 [0.0, 1.0]
    int x, y;              // 左上角坐标
    int width, height;     // 宽高

    cv::Point2f getCenter() const {
        return cv::Point2f(x + width / 2.0f, y + height / 2.0f);
    }

    cv::Rect toRect() const {
        return cv::Rect(x, y, width, height);
    }
};

// 检测结果集合
struct DetectionResult {
    std::vector<DetectionBox> boxes;
    double preprocessTime;   // 预处理耗时 (ms)
    double inferenceTime;    // 推理耗时 (ms)
    double postprocessTime;  // 后处理耗时 (ms)

    double getTotalTime() const {
        return preprocessTime + inferenceTime + postprocessTime;
    }

    bool hasDetections() const {
        return !boxes.empty();
    }
};

// 检测器配置
struct DetectorConfig {
    std::string modelPath;            // 模型文件路径
    std::string configFile;           // 配置文件路径
    std::vector<std::string> classes; // 类别列表
    float confidenceThreshold = 0.5f; // 置信度阈值
    float nmsThreshold = 0.45f;       // NMS 阈值
    int inputWidth = 640;             // 模型输入宽度
    int inputHeight = 640;            // 模型输入高度
};
```

---

## DetectorFactory 工厂实现

### 核心方法

```cpp
std::unique_ptr<IDetector> DetectorFactory::create(
    DetectorType type,
    const DetectorConfig& config) {

    std::unique_ptr<IDetector> detector;

    // 1. 根据类型创建具体检测器
    switch (type) {
        case DetectorType::PPYOLOE:
            detector = std::make_unique<PPYoloeDetector>();
            break;
        case DetectorType::YOLOV8:
            throw std::runtime_error("YOLOv8 not implemented");
        default:
            throw std::runtime_error("Unknown detector type");
    }

    // 2. 初始化检测器
    if (!detector->initialize(config)) {
        throw std::runtime_error("Failed to initialize detector");
    }

    return detector;
}
```

📌 **知识点：std::make_unique**

```cpp
// 推荐：使用 make_unique
auto p1 = std::make_unique<MyClass>(args...);

// 不推荐：直接 new
std::unique_ptr<MyClass> p2(new MyClass(args...));

// 为什么推荐 make_unique？
// 1. 异常安全：new 和 unique_ptr 构造是两步操作，make_unique 是原子操作
// 2. 代码简洁：不需要写类型两次
// 3. 性能：可能有更好的内存分配优化
```

### 从字符串创建

```cpp
DetectorType DetectorFactory::stringToType(const std::string& typeStr) {
    // 转换为小写
    std::string lower = typeStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // 匹配类型
    if (lower == "ppyoloe" || lower == "pp-yoloe" || lower == "ppyolo") {
        return DetectorType::PPYOLOE;
    } else if (lower == "yolov8") {
        return DetectorType::YOLOV8;
    }

    return DetectorType::UNKNOWN;
}
```

📌 **知识点：std::transform**

```cpp
#include <algorithm>
#include <cctype>

std::string str = "HeLLo";
std::transform(str.begin(), str.end(), str.begin(), ::tolower);
// str = "hello"

// transform 原理：
// for (auto it = begin; it != end; ++it) {
//     *dest++ = func(*it);
// }
```

### 禁止实例化工厂类

```cpp
class DetectorFactory {
private:
    DetectorFactory() = delete;
    ~DetectorFactory() = delete;
    DetectorFactory(const DetectorFactory&) = delete;
    DetectorFactory& operator=(const DetectorFactory&) = delete;
};
```

**为什么禁止实例化？**

- 工厂类只有静态方法，不需要实例
- 避免误用（`new DetectorFactory()` 没有意义）
- 明确表达设计意图：这是工具类

---

## PPYoloeDetector 具体实现

### 初始化流程

```cpp
bool PPYoloeDetector::initialize(const DetectorConfig& config) {
    // 1. 保存配置
    modelPath_ = config.modelPath;
    classes_ = config.classes;
    confidenceThreshold_ = config.confidenceThreshold;

    // 2. 检查模型文件
    std::ifstream modelFile(modelPath_);
    if (!modelFile.good()) {
        logger_.error("模型文件不存在: " + modelPath_);
        return false;
    }

    // 3. 配置 RuntimeOption（TPU 后端）
    runtimeOption_ = std::make_unique<fastdeploy::RuntimeOption>();
    runtimeOption_->UseSophgo();  // 使用算能 TPU

    // 4. 加载 PP-YOLOE 模型
    model_ = std::make_unique<fastdeploy::vision::detection::PPYOLOE>(
        modelPath_,
        "",           // params_file (bmodel 不需要)
        configFile_,  // infer_cfg.yml
        *runtimeOption_,
        fastdeploy::ModelFormat::SOPHGO
    );

    if (!model_->Initialized()) {
        logger_.error("模型初始化失败");
        return false;
    }

    // 5. 预热模型（可选但推荐）
    cv::Mat dummyImage = cv::Mat::zeros(inputHeight_, inputWidth_, CV_8UC3);
    fastdeploy::vision::DetectionResult dummyResult;
    model_->Predict(dummyImage, &dummyResult);

    initialized_ = true;
    return true;
}
```

### 检测流程

```cpp
DetectionResult PPYoloeDetector::detect(const cv::Mat& image) {
    DetectionResult result;

    // 1. 预处理
    auto t1 = std::chrono::high_resolution_clock::now();
    cv::Mat preprocessed = preprocess(image);
    auto t2 = std::chrono::high_resolution_clock::now();
    result.preprocessTime = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // 2. 推理
    fastdeploy::vision::DetectionResult fdResult;
    t1 = std::chrono::high_resolution_clock::now();
    bool success = model_->Predict(preprocessed, &fdResult);
    t2 = std::chrono::high_resolution_clock::now();
    result.inferenceTime = std::chrono::duration<double, std::milli>(t2 - t1).count();

    if (!success) {
        logger_.error("模型推理失败");
        return result;
    }

    // 3. 后处理
    t1 = std::chrono::high_resolution_clock::now();
    result = postprocess(fdResult, image.size());
    t2 = std::chrono::high_resolution_clock::now();
    result.postprocessTime = std::chrono::duration<double, std::milli>(t2 - t1).count();

    return result;
}
```

---

## 图像预处理 - Letterbox

### 什么是 Letterbox？

**Letterbox** 是一种保持宽高比的图像缩放方法，不足部分用灰色填充。

```
原图 (1920x1080)                 Letterbox 后 (640x640)
┌────────────────────────┐       ┌──────────────────────┐
│                        │       │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
│                        │       │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
│       原始图像         │  →    │      缩放后图像      │
│                        │       │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
│                        │       │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
└────────────────────────┘       └──────────────────────┘
                                 ▓▓ = 灰色填充区域
```

### 为什么需要 Letterbox？

| 方法            | 特点                | 问题                   |
| --------------- | ------------------- | ---------------------- |
| **直接 resize** | 简单快速            | 图像变形，影响检测精度 |
| **裁剪**        | 保持比例            | 丢失部分图像内容       |
| **Letterbox**   | 保持比例 + 完整内容 | 推荐！                 |

### 实现代码

```cpp
cv::Mat VisionUtils::letterbox(const cv::Mat& image,
                                cv::Size targetSize,
                                LetterboxTransform& transform) {
    int srcWidth = image.cols;
    int srcHeight = image.rows;
    int dstWidth = targetSize.width;
    int dstHeight = targetSize.height;

    // 1. 计算缩放比例（保持宽高比，取较小的比例）
    float scaleW = static_cast<float>(dstWidth) / srcWidth;
    float scaleH = static_cast<float>(dstHeight) / srcHeight;
    float scale = std::min(scaleW, scaleH);

    // 2. 计算缩放后的尺寸
    int newWidth = static_cast<int>(srcWidth * scale);
    int newHeight = static_cast<int>(srcHeight * scale);

    // 3. 计算填充的偏移量（居中）
    int offsetX = (dstWidth - newWidth) / 2;
    int offsetY = (dstHeight - newHeight) / 2;

    // 4. 保存变换参数（后处理坐标还原需要）
    transform.scale = scale;
    transform.offsetX = offsetX;
    transform.offsetY = offsetY;

    // 5. 缩放图像
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(newWidth, newHeight));

    // 6. 创建目标图像并填充灰色
    cv::Mat result = cv::Mat::zeros(dstHeight, dstWidth, image.type());
    result.setTo(cv::Scalar(114, 114, 114));  // YOLO 系列常用的灰色值

    // 7. 将缩放后的图像复制到目标图像中心
    resized.copyTo(result(cv::Rect(offsetX, offsetY, newWidth, newHeight)));

    return result;
}
```

### 坐标逆变换

检测结果的坐标是在 Letterbox 后的图像上的，需要还原到原图：

```cpp
// 逆变换公式
x_orig = (x_model - offsetX) / scale
y_orig = (y_model - offsetY) / scale
width_orig = width_model / scale
height_orig = height_model / scale

// 代码实现
DetectionBox detBox;
detBox.x = static_cast<int>((box[0] - offsetX) / scale);
detBox.y = static_cast<int>((box[1] - offsetY) / scale);
detBox.width = static_cast<int>((box[2] - box[0]) / scale);
detBox.height = static_cast<int>((box[3] - box[1]) / scale);

// 边界检查
detBox.x = std::max(0, std::min(detBox.x, originalWidth - 1));
detBox.y = std::max(0, std::min(detBox.y, originalHeight - 1));
```

---

## 面试高频考点

### 📌 设计模式

**Q1: 工厂模式和策略模式有什么区别？**

**A1:**
| 方面 | 工厂模式 | 策略模式 |
|------|---------|----------|
| 关注点 | 对象**创建** | 算法**使用** |
| 目的 | 封装实例化过程 | 封装算法变化 |
| 解决问题 | 如何创建对象 | 如何选择算法 |
| 项目应用 | DetectorFactory | IDetector 接口 |

**Q2: 为什么返回 unique_ptr 而不是 shared_ptr？**

**A2:**

- 检测器通常**独占所有权**，不需要共享
- `unique_ptr` 更轻量，无原子操作开销
- 明确表达"唯一所有权"的语义
- 如需共享，可以通过 `std::move` 转换

### 📌 多态与虚函数

**Q3: 虚函数表是什么？如何实现多态？**

**A3:**

```cpp
class IDetector {
    // 虚函数表指针 (vptr)
    virtual DetectionResult detect(const cv::Mat&) = 0;
};

// 对象内存布局:
// ┌──────────────┐
// │    vptr      │ → 指向虚函数表
// │  成员变量1   │
// │  成员变量2   │
// └──────────────┘
//
// 虚函数表:
// ┌──────────────────────────────┐
// │ PPYoloeDetector::detect()   │
// │ PPYoloeDetector::getName()  │
// └──────────────────────────────┘
```

**Q4: 为什么禁止拷贝检测器对象？**

**A4:**

- 检测器内部持有 TPU 内存资源
- 深拷贝代价高昂且易出错
- 使用 `unique_ptr` 管理所有权更安全

### 📌 图像处理

**Q5: 为什么用 Letterbox 而不是直接 resize？**

**A5:**

- **直接 resize**：图像变形，检测精度下降
- **Letterbox**：保持宽高比，填充灰色边框，检测精度高

**Q6: 后处理中为什么要做坐标逆变换？**

**A6:**

- 模型推理在 Letterbox 后的图像上进行
- 检测结果的坐标是相对于 640x640 的输入
- 需要还原到原始图像坐标，才能正确绘制检测框

### 📌 性能优化

**Q7: 如何优化检测器的推理速度？**

**A7:**

1. **模型量化**：FP32 → INT8，速度提升 2-4 倍
2. **批处理**：多帧一起推理，提高 GPU/TPU 利用率
3. **预处理优化**：使用 SIMD 指令加速 Letterbox
4. **异步推理**：预处理和推理并行执行
5. **模型剪枝**：减少模型参数量

---

## 实战练习

### 练习 1：添加 YOLOv8 检测器

**需求**：实现 `Yolov8Detector` 类，继承自 `IDetector`

**步骤**：

1. 创建 `Yolov8Detector.h` 和 `Yolov8Detector.cpp`
2. 实现所有纯虚函数
3. 在 `DetectorFactory::create()` 中添加创建逻辑

### 练习 2：实现检测结果可视化

**需求**：在 `VisionUtils` 中添加 `drawDetections()` 方法

```cpp
// 提示
void drawDetections(cv::Mat& image, const DetectionResult& result);
// 绘制检测框、类别名称、置信度
```

### 练习 3：添加多尺度检测

**需求**：对大图像进行分块检测，然后合并结果

```cpp
// 提示：滑动窗口 + NMS 合并
DetectionResult detectMultiScale(const cv::Mat& image,
                                  int windowSize, int stride);
```

---

## 总结

本节我们深入学习了 Vision 模块的设计与实现：

| 技术点        | 内容                           |
| ------------- | ------------------------------ |
| **策略模式**  | IDetector 接口定义，算法可替换 |
| **工厂模式**  | DetectorFactory 封装创建逻辑   |
| **纯虚函数**  | 强制派生类实现接口方法         |
| **Letterbox** | 保持宽高比的图像预处理         |
| **坐标变换**  | 预处理参数记录与后处理还原     |

**核心收获**：

1. **设计模式组合**：工厂负责创建，策略负责使用
2. **接口设计**：纯虚函数定义契约，派生类实现细节
3. **多态**：通过基类指针调用派生类方法
4. **图像预处理**：Letterbox 是目标检测的标准做法

**下一节预告**：[06-Task 模块详解](./06-Task模块详解.md) - 学习任务管理和事件上报的实现

---

_创建日期：2025-10-26_
_适用版本：ESDK_On_Sophon v1.0_
