# SAM2 集成到 TaskService 改进记录

**日期**: 2025-12-11  
**改进类型**: 功能新增 + 架构优化  
**涉及模块**: TaskService, Sam2Segmentor  
**改进人员**: ESDK Sophon Team

---

## 📋 改进背景

### 问题现状

1. **单一算法支持**: TaskService 目前只支持目标检测 (YOLOv10/PP-YOLOE)
2. **缺少分割能力**: 无法处理需要精细轮廓的场景 (如垃圾清理、目标追踪)
3. **接口文档要求**: 云平台通过 `main_type` 字段区分算法类型:
   - `main_type = 100000`: 目标检测 (使用 `points` 字段上报矩形框)
   - `main_type = 100001`: 语义分割 (使用 `polygons` 字段上报多边形)

### 改进目标

✅ 集成 SAM2 分割器到 TaskService  
✅ 实现基于 `main_type` 的算法路由  
✅ 保持向后兼容 (已有检测功能不受影响)  
✅ 配置驱动 (通过 config.json 启用/禁用 SAM2)

---

## 🔧 改进内容

### 1. TaskService::Impl 结构体扩展

**文件**: `src/task/TaskService.cpp`

**新增成员变量**:

```cpp
struct TaskService::Impl {
    // ... 原有成员 ...

    // ===== SAM2 分割器 =====
    std::unique_ptr<vision::Sam2Segmentor> sam2Segmentor;  ///< SAM2 语义分割器
    bool useSam2;                                          ///< 是否启用 SAM2
    std::mutex segmentorMutex;                             ///< 保护分割器的互斥锁
};
```

**设计说明**:

- **智能指针管理**: 使用 `unique_ptr` 自动管理 SAM2 资源生命周期
- **线程安全**: 添加 `segmentorMutex` 保护并发访问 (与 `detectorMutex` 并行)
- **启用标志**: `useSam2` 控制运行时是否调用分割器 (避免空指针检查)

---

### 2. 构造函数初始化 SAM2

**初始化流程**:

```cpp
Impl(std::shared_ptr<mqtt::MqttClient> m)
    : mqtt(m)
    , useSam2(false)  // 默认关闭
    , logger(core::Logger::getInstance())
{
    // ===== 1. 初始化目标检测器 (不变) =====
    // ... 已有代码 ...

    // ===== 2. 初始化 SAM2 分割器 =====
    try {
        auto& config = core::Config::getInstance();

        // 读取配置
        bool enabled = config.get<bool>("segmentation.sam2.enabled", false);

        if (enabled) {
            std::string encoderPath = config.get<std::string>("segmentation.sam2.encoder_model_path");
            std::string decoderPath = config.get<std::string>("segmentation.sam2.decoder_model_path");
            int deviceId = config.get<int>("segmentation.sam2.device_id", 0);

            // 创建并初始化
            sam2Segmentor = std::make_unique<vision::Sam2Segmentor>(
                encoderPath, decoderPath, deviceId);

            if (!sam2Segmentor->init()) {
                logger.error("SAM2 初始化失败");
                sam2Segmentor.reset();
                useSam2 = false;
            } else {
                useSam2 = true;
                logger.info("SAM2 初始化成功");
            }
        }
    } catch (const std::exception& e) {
        logger.error("SAM2 初始化异常: " + std::string(e.what()));
        useSam2 = false;
    }
}
```

**设计亮点**:

1. **配置驱动**: 通过 `segmentation.sam2.enabled` 控制是否加载
2. **异常安全**: 初始化失败时优雅降级,不影响主程序
3. **详细日志**: 输出模型路径、设备 ID 等关键信息,便于调试
4. **资源管理**: 失败时调用 `reset()` 释放部分初始化的资源

---

### 3. 算法路由逻辑 (待实现)

**设计方案**: 根据 `EventType::mainType` 路由到不同算法

```cpp
bool TaskService::processFrame(const cv::Mat& frame,
                              const TaskConfig& config,
                              std::vector<BoundingBox>& outBoxes,
                              const std::string& fileName) {
    try {
        // 获取算法类型
        int mainType = config.eventTypes[0].mainType;

        // ===== 算法路由 =====
        if (mainType == 100000) {
            // 目标检测 (已有逻辑)
            return processDetection(frame, config, outBoxes, fileName);

        } else if (mainType == 100001) {
            // 语义分割 (新增逻辑)
            return processSegmentation(frame, config, outBoxes, fileName);

        } else {
            impl_->logger.error("未知的 main_type: " + std::to_string(mainType));
            return false;
        }

    } catch (const std::exception& e) {
        impl_->logger.error("processFrame 异常: " + std::string(e.what()));
        return false;
    }
}
```

**核心思想**:

- **单一入口**: `processFrame` 作为统一入口
- **策略模式**: 根据 `mainType` 调用不同的处理函数
- **扩展性**: 未来可轻松添加新算法类型 (如 `100002` 表示目标追踪)

---

### 4. 分割处理函数 (待实现)

```cpp
bool TaskService::processSegmentation(const cv::Mat& frame,
                                      const TaskConfig& config,
                                      std::vector<BoundingBox>& outBoxes,
                                      const std::string& fileName) {
    // 1. 检查 SAM2 是否可用
    if (!impl_->useSam2) {
        impl_->logger.warning("SAM2 未启用,无法执行分割任务");
        return false;
    }

    // 2. 先用检测器获取目标框
    std::vector<BoundingBox> detectedBoxes;
    if (!detectObjects(frame, config, detectedBoxes)) {
        return false;
    }

    // 3. 调用 SAM2 进行分割
    std::lock_guard<std::mutex> lock(impl_->segmentorMutex);
    vision::SegmentationResult result = impl_->sam2Segmentor->segmentWithPrompts(
        frame, detectedBoxes, DataSource::LIVESTREAM);

    // 4. 构建事件 (使用 polygons 字段)
    DetectionEvent event = buildSegmentationEvent(frame, result, config, fileName);

    // 5. 发布事件
    return publishEvent(event, config.deviceSn);
}
```

**关键点**:

- **两阶段处理**: 先检测获取候选框,再用 SAM2 精细分割
- **线程安全**: 使用 `segmentorMutex` 保护 SAM2 调用
- **结果转换**: 将 `SegmentationResult` 转换为 `DetectionEvent`

---

## 📊 数据流变化

### 原有流程 (仅检测)

```
processFrame()
    ↓
detectObjects()  (调用 YOLO/PP-YOLOE)
    ↓
filterByEventTypes()  (过滤类别)
    ↓
buildEvent()  (填充 points 字段)
    ↓
publishEvent()  (MQTT 上报)
```

### 新流程 (支持检测+分割)

```
processFrame()
    ↓
判断 main_type
    ↓
┌─────────────────────┬─────────────────────┐
│  main_type=100000   │  main_type=100001   │
│  (目标检测)          │  (语义分割)          │
└─────────────────────┴─────────────────────┘
    ↓                        ↓
detectObjects()        detectObjects() (获取候选框)
    ↓                        ↓
filterByEventTypes()   segmentWithPrompts() (SAM2分割)
    ↓                        ↓
buildEvent()           buildSegmentationEvent()
(填充 points)          (填充 polygons)
    ↓                        ↓
publishEvent()         publishEvent()
```

---

## 📝 配置文件变化

### config.json 新增字段

```json
{
  "segmentation": {
    "sam2": {
      "enabled": true,
      "encoder_model_path": "/data/models/sam2_encoder_f16_1b.bmodel",
      "decoder_model_path": "/data/models/sam2_decoder_f16_1b.bmodel",
      "device_id": 0,
      "input_size": 1024,
      "iou_threshold": 0.88
    }
  }
}
```

**字段说明**:

- `enabled`: 总开关,设为 `false` 可快速禁用 SAM2
- `encoder_model_path`: Encoder BModel 路径
- `decoder_model_path`: Decoder BModel 路径
- `device_id`: TPU 设备 ID (0=第一块芯片)
- `iou_threshold`: IOU 阈值,用于筛选高质量 mask

---

## 🎯 接口兼容性

### MQTT 事件上报格式

**检测事件** (main_type=100000):

```json
{
  "UUID": "xxx",
  "taskID": 12345,
  "eventType": 1,
  "main_type": 100000,
  "eventDescribe": "检测到目标",
  "fileName": "test.jpg",
  "latitude": 31.230391,
  "longitude": 121.473701,
  "createTime": "2025-11-02T10:30:00.000Z",
  "points": [{ "x": 100, "y": 200, "w": 50, "h": 80 }],
  "polygons": []
}
```

**分割事件** (main_type=100001):

```json
{
  "UUID": "xxx",
  "taskID": 12345,
  "eventType": 1,
  "main_type": 100001,
  "eventDescribe": "分割到目标",
  "fileName": "test.jpg",
  "latitude": 31.230391,
  "longitude": 121.473701,
  "createTime": "2025-11-02T10:30:00.000Z",
  "points": [],
  "polygons": [
    {
      "index": 0,
      "vertices": [
        { "x": 100, "y": 200 },
        { "x": 150, "y": 200 },
        { "x": 150, "y": 280 }
      ]
    }
  ]
}
```

**关键区别**:

- 检测使用 `points` (矩形框), 分割使用 `polygons` (多边形)
- `main_type` 字段明确区分算法类型

---

## ✅ 测试验证

### 1. 单元测试 (已有)

- `tests/test_sam2.cpp`: SAM2 独立功能测试

### 2. 集成测试 (待补充)

```bash
# 1. 修改 config.json 启用 SAM2
{
  "segmentation": {
    "sam2": {
      "enabled": true,
      "encoder_model_path": "/data/models/sam2_encoder_f16_1b.bmodel",
      "decoder_model_path": "/data/models/sam2_decoder_f16_1b.bmodel"
    }
  }
}

# 2. 下发分割任务 (main_type=100001)
# MQTT 消息示例:
{
  "taskID": 67890,
  "algorithmName": "垃圾分割",
  "dataSource": "live",
  "types": [
    {
      "id": 2,
      "main_type": 100001,  # 分割类型
      "eventDescribe": "分割检测",
      "classIDs": [0]
    }
  ]
}

# 3. 观察日志
tail -f /data/Edge-SDK/logs/latest.log | grep SAM2
```

### 3. 性能测试

- 单帧处理时间: < 200ms (含检测+分割)
- 内存占用: < 500MB (额外增加)
- TPU 利用率: < 80%

---

## 📖 知识点讲解

### 📌 策略模式 (Strategy Pattern)

#### 知识点

策略模式定义了一系列算法,将每个算法封装起来,使它们可以互相替换。
算法的变化独立于使用算法的客户端。

#### 经典例子

```cpp
// 抽象策略
class SortStrategy {
public:
    virtual void sort(std::vector<int>& data) = 0;
};

// 具体策略1
class BubbleSort : public SortStrategy {
public:
    void sort(std::vector<int>& data) override { /* 冒泡排序 */ }
};

// 具体策略2
class QuickSort : public SortStrategy {
public:
    void sort(std::vector<int>& data) override { /* 快速排序 */ }
};

// 上下文
class Sorter {
private:
    std::unique_ptr<SortStrategy> strategy_;
public:
    void setStrategy(std::unique_ptr<SortStrategy> s) {
        strategy_ = std::move(s);
    }

    void performSort(std::vector<int>& data) {
        strategy_->sort(data);
    }
};
```

#### 项目中的例子

本次改进中,我们使用**简化版策略模式**:

```cpp
bool TaskService::processFrame(...) {
    int mainType = config.eventTypes[0].mainType;

    // 根据策略选择算法
    if (mainType == 100000) {
        return processDetection(...);  // 策略1: 检测
    } else if (mainType == 100001) {
        return processSegmentation(...);  // 策略2: 分割
    }
}
```

**为什么不用继承?**

- 策略数量少 (仅 2 个)
- 策略切换频繁 (每帧可能不同)
- 避免虚函数开销 (性能敏感场景)

#### 面试要点

**Q: 策略模式和状态模式有什么区别?**  
A:

- **策略模式**: 客户端主动选择策略,策略之间平等独立
- **状态模式**: 状态自动切换,状态之间有转换关系

**Q: 策略模式的优缺点?**  
A:

- ✅ 优点: 算法独立变化,易于扩展,消除条件分支
- ❌ 缺点: 增加类数量,客户端需要了解所有策略

---

### 📌 RAII + 智能指针管理资源

#### 知识点

RAII (Resource Acquisition Is Initialization) 是 C++ 核心惯用法:
资源的获取即初始化,资源的释放通过析构函数自动完成。

智能指针是 RAII 的最佳实践。

#### 经典例子

```cpp
// ❌ 错误: 手动管理容易忘记释放
void badExample() {
    FILE* fp = fopen("test.txt", "r");
    processFile(fp);  // 如果这里抛异常,fp 泄露!
    fclose(fp);
}

// ✅ 正确: RAII 自动释放
void goodExample() {
    auto fp = std::unique_ptr<FILE, decltype(&fclose)>(
        fopen("test.txt", "r"), fclose);
    processFile(fp.get());  // 异常安全,fp 自动关闭
}
```

#### 项目中的例子

本次改进中的资源管理:

```cpp
struct TaskService::Impl {
    // ✅ 使用 unique_ptr 管理 SAM2 生命周期
    std::unique_ptr<vision::Sam2Segmentor> sam2Segmentor;
};

Impl::Impl() {
    try {
        sam2Segmentor = std::make_unique<vision::Sam2Segmentor>(...);

        if (!sam2Segmentor->init()) {
            // 初始化失败,显式释放
            sam2Segmentor.reset();  // 调用析构函数
        }
    } catch (...) {
        // 异常发生时,unique_ptr 自动析构
    }
}

// Impl 析构时,sam2Segmentor 自动销毁,无需手动 delete
```

**关键点**:

1. **自动清理**: `Impl` 析构时,`sam2Segmentor` 自动释放
2. **异常安全**: 初始化失败不会泄露内存
3. **所有权清晰**: `unique_ptr` 表示独占所有权

#### 面试要点

**Q: unique_ptr 和 shared_ptr 如何选择?**  
A:

- **unique_ptr**: 独占所有权,无开销,优先使用
- **shared_ptr**: 共享所有权,有引用计数开销,仅在需要共享时使用

本项目中 SAM2 只被 TaskService 使用,选择 `unique_ptr`。

**Q: 如何实现自定义删除器?**  
A:

```cpp
auto deleter = [](MyResource* p) {
    cleanup(p);
    delete p;
};
std::unique_ptr<MyResource, decltype(deleter)> ptr(new MyResource, deleter);
```

---

## 🚀 后续优化方向

### 1. 性能优化

- [ ] **缓存 Encoder 特征**: 视频流场景下,连续帧图像相似,可缓存 encoder 输出
- [ ] **GPU 预处理**: BGR→RGB 转换可用 BMCV 加速
- [ ] **批量推理**: 多个检测框可批量送入 Decoder

### 2. 功能扩展

- [ ] **支持纯分割模式**: 不依赖检测器,直接对全图分割
- [ ] **支持多类别分割**: 当前只支持单类,可扩展为多类语义分割
- [ ] **分割结果优化**: 多边形简化、孔洞填充等后处理

### 3. 配置灵活化

- [ ] **运行时切换算法**: 支持在不重启程序的情况下切换检测/分割
- [ ] **算法参数调优**: 通过配置文件调整 IOU 阈值、输入尺寸等

---

## 📚 相关文档

- [SAM2 分割器集成说明](../module/SAM2分割器集成说明.md)
- [SAM2 完整实现](./SAM2完整实现-2025-12-11.md)
- [SAM2 配置添加](./SAM2配置添加-2025-12-11.md)
- [任务管理架构设计](../architecture/架构设计方案-多算法任务管理.md)

---

## 🎓 总结

本次改进实现了 SAM2 语义分割功能与现有检测系统的无缝集成:

1. **架构层面**: 引入策略模式,实现算法类型路由
2. **代码层面**: 使用 RAII 和智能指针保证资源安全
3. **配置层面**: 通过 JSON 配置灵活控制功能启用
4. **接口层面**: 完全兼容云平台接口规范

这次改进不仅增加了新功能,更重要的是展示了**如何在现有系统中优雅地集成新模块**,
这是工程化开发的核心能力!

---

**创建日期**: 2025-12-11  
**修改日期**: 2025-12-11  
**版本**: v1.0  
**状态**: ✅ 完成 (已完成编码,等待实际设备测试)

---

## 📋 最终实现总结

### 修改的文件清单

1. **include/esdk_sophon/task/TaskService.h**

   - 新增 `processDetection()` 声明
   - 新增 `processSegmentation()` 声明
   - 新增 `buildSegmentationEvent()` 声明
   - 新增 `#include "esdk_sophon/vision/segmentation/ISegmentor.h"`

2. **src/task/TaskService.cpp**
   - Impl 结构体新增: `sam2Segmentor`, `useSam2`, `segmentorMutex`
   - 构造函数新增 SAM2 初始化逻辑
   - `processFrame()` 重构为算法路由入口
   - 新增 `processDetection()` 实现 (原 processFrame 逻辑)
   - 新增 `processSegmentation()` 实现
   - 新增 `buildSegmentationEvent()` 实现

### 代码统计

- **新增代码**: 约 250 行
- **重构代码**: 约 50 行
- **总计**: 约 300 行

### 关键技术细节

1. **类型转换**: BoundingBox (w/h) ↔ DetectionBox (width/height)
2. **SAM2 API**: `init(encoderPath, decoderPath)`, `segmentWithPrompts(frame, boxes)`
3. **轮廓提取**: cv::findContours + cv::approxPolyDP 简化多边形
4. **Config API**: `getString`, `getInt`, `getBool` (不是模板方法)

---

## 🧪 测试建议

### 单元测试 (已有)

```bash
cd /data/Edge-SDK/build
./bin/tests/test_sam2
```

### 集成测试 (需要添加)

1. 修改 `config/config.json` 启用 SAM2
2. 通过 MQTT 下发分割任务 (main_type=100001)
3. 观察日志输出
4. 检查 MQTT 事件中的 polygons 字段

---

**代码审查**: 已通过编译检查  
**单元测试**: test_sam2.cpp 已实现  
**集成测试**: 待实际设备验证  
**文档更新**: ✅
