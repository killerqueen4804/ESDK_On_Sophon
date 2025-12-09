# Detector 模块单元测试说明

> **创建日期**: 2025-10-31  
> **测试文件**: `tests/test_detector.cpp`  
> **目标**: 验证 Vision 模块检测器的正确性

---

## 📋 测试概览

### 测试覆盖范围

| 测试项                 | 测试方法 | 状态 | 难度       |
| ---------------------- | -------- | ---- | ---------- |
| 工厂模式类型转换       | test1    | ✅   | ⭐⭐       |
| 支持的检测器类型       | test2    | ✅   | ⭐⭐       |
| 路径推断类型           | test3    | ✅   | ⭐⭐⭐     |
| 基本功能测试           | test4    | ✅   | ⭐⭐       |
| DetectionBox 辅助方法  | test5    | ✅   | ⭐⭐       |
| DetectionResult 统计   | test6    | ✅   | ⭐⭐       |
| **Letterbox 变换算法** | test7    | ✅   | ⭐⭐⭐⭐⭐ |
| **坐标逆变换算法**     | test8    | ✅   | ⭐⭐⭐⭐⭐ |
| 实际检测流程           | test9    | ⏸️   | ⭐⭐⭐⭐   |

---

## 🎓 核心测试详解

### 测试 7: Letterbox 变换算法 ⭐⭐⭐⭐⭐

#### 为什么重要?

这是**目标检测预处理的核心算法**，也是面试高频考点！

#### 测试场景

```cpp
// 场景1: 横向图像 1920x1080 → 640x640
输入尺寸: 1920x1080
目标尺寸: 640x640

计算过程:
1. scaleW = 640 / 1920 = 0.333
2. scaleH = 640 / 1080 = 0.593
3. scale = min(0.333, 0.593) = 0.333  ✅ 选较小的
4. newWidth = 1920 * 0.333 = 640
5. newHeight = 1080 * 0.333 = 360
6. offsetX = (640 - 640) / 2 = 0
7. offsetY = (640 - 360) / 2 = 140

结果:
- 缩放后: 640x360
- 上下各填充140像素灰边
- 左右无填充
```

#### 图示

```
原图 1920x1080        缩放 640x360         最终 640x640
┌──────────┐         ┌─────────┐         ┌─────────┐
│          │         │         │         │  灰色   │ ← 140px
│          │ scale   │         │ copy    ├─────────┤
│  原图    │ ────→  │ 缩放图  │ ────→  │ 缩放图  │ ← 360px
│          │ 0.333   │         │         ├─────────┤
│          │         │         │         │  灰色   │ ← 140px
└──────────┘         └─────────┘         └─────────┘
```

#### 验证点

```cpp
// ✅ 应该通过的断言
assert(scale < scaleW);        // 选择高度的缩放比例
assert(newWidth == 640);
assert(newHeight == 360);
assert(offsetX == 0);
assert(offsetY == 140);
```

#### 面试问题

**Q1: 为什么选择 `min(scaleW, scaleH)` 而不是 `max`?**

A:

- `min`: 保证图像完整，不裁剪任何内容 ✅
- `max`: 会裁剪图像，丢失边缘目标 ❌

**Q2: 如果是纵向图像 1080x1920 会怎样?**

A:

```
scaleW = 640/1080 = 0.593
scaleH = 640/1920 = 0.333
scale = min(0.593, 0.333) = 0.333  ← 选宽度比例
→ 缩放为 360x640，左右填充
```

**Q3: 为什么填充颜色是(114, 114, 114)?**

A:

- 中性灰色，不引入额外颜色信息
- 接近 ImageNet 均值
- YOLO 系列标准做法

---

### 测试 8: 坐标逆变换算法 ⭐⭐⭐⭐⭐

#### 为什么重要?

模型输出的坐标是基于 640x640 的，必须转换回原图坐标！

#### 逆变换公式

```cpp
// 前向变换 (原图 → 模型)
x_model = x_orig * scale + offsetX
y_model = y_orig * scale + offsetY

// 逆变换 (模型 → 原图) ⭐ 核心公式
x_orig = (x_model - offsetX) / scale
y_orig = (y_model - offsetY) / scale
```

#### 测试场景

```cpp
// 场景: 1920x1080 → 640x640
scale = 0.333
offsetX = 0
offsetY = 140

// 测试点1: 模型中心 (320, 320)
x_orig = (320 - 0) / 0.333 = 960  ← 原图中心x
y_orig = (320 - 140) / 0.333 = 540 ← 原图中心y

// 测试点2: 模型左上角 (0, 140)
x_orig = (0 - 0) / 0.333 = 0      ← 原图左上角
y_orig = (140 - 140) / 0.333 = 0  ← 原图左上角
```

#### 图示

```
模型输入 640x640          原图 1920x1080
┌─────────┐              ┌──────────┐
│  灰色   │              │          │
├─────────┤              │          │
│  ┌─┐    │              │   ┌──┐   │
│  └─┘    │ 逆变换        │   └──┘   │
│ (100,   │ ────────→    │  (300,   │
│  200)   │  /scale      │   180)   │
├─────────┤              │          │
│  灰色   │              │          │
└─────────┘              └──────────┘
```

#### 面试问题

**Q1: 为什么要减去 offset?**

A: offset 是填充区域，不是原图内容，必须先减去

**Q2: 为什么要除以 scale?**

A: 将模型坐标系的距离转换回原图坐标系的距离

**Q3: 边界检查为什么重要?**

A:

- 模型可能预测超出边界的框
- 浮点误差可能导致坐标越界
- 防止后续处理时数组越界

---

## 🔧 编译和运行

### 编译命令

```bash
# 在Docker容器中
cd /workspace/ESDK_On_Sophon/build
cmake ..
make test_detector
```

### 运行测试

```bash
# 运行单元测试 (不需要模型文件)
./bin/test_detector

# 预期输出:
# ======================================
#   Detector模块单元测试
# ======================================
#
# ========== 测试1: DetectorFactory类型转换 ==========
# YOLOV10 → YOLOv10
# ✅ [PASS] 类型转换
#
# ========== 测试2: DetectorFactory支持的类型 ==========
# YOLOV10支持: 是
# ✅ [PASS] 支持的类型
# ...
```

---

## 📚 测试设计原则

### 1. 单元测试 vs 集成测试

| 类型         | 范围        | 依赖         | 示例        |
| ------------ | ----------- | ------------ | ----------- |
| **单元测试** | 单个函数/类 | 无外部依赖   | test1-test8 |
| **集成测试** | 多个模块    | 需要真实环境 | test9       |

### 2. AAA 模式 (Arrange-Act-Assert)

```cpp
void testExample() {
    // Arrange - 准备测试数据
    DetectorConfig config = createTestConfig();

    // Act - 执行被测试的操作
    auto detector = DetectorFactory::create(DetectorType::YOLOV10, config);

    // Assert - 验证结果
    assert(detector->getName() == "YOLOv10");
}
```

### 3. 边界条件测试

```cpp
// ✅ 应该测试的边界条件
- 空图像
- 极小图像 (1x1)
- 极大图像 (8K分辨率)
- 正方形图像
- 极端宽高比 (10000x1)
- 无效配置
- 模型文件不存在
```

### 4. 异常测试

```cpp
// ✅ 应该测试异常情况
try {
    auto detector = DetectorFactory::createFromString("invalid_type", config);
    assert(false);  // 不应该执行到这里
} catch (const std::exception& e) {
    // 预期异常
    assert(std::string(e.what()).find("Unknown") != std::string::npos);
}
```

---

## 🎯 面试中的测试问题

### 高频问题

**Q1: 为什么要写单元测试?**

A:

1. **验证正确性** - 确保代码按预期工作
2. **防止回归** - 修改代码后快速发现 bug
3. **文档作用** - 测试用例就是最好的使用文档
4. **重构信心** - 有测试保护，大胆重构
5. **设计改进** - 难测试的代码往往设计有问题

**Q2: 测试覆盖率多少合适?**

A:

- 核心算法: **100%** (如 Letterbox、坐标变换)
- 业务逻辑: **80%+** (正常流程+主要异常)
- 辅助函数: **60%+** (getter/setter 可适当降低)
- 重点是**关键路径覆盖**，不是盲目追求数字

**Q3: Mock 对象何时使用?**

A:

```cpp
// 场景: 测试TaskManager时，不想依赖真实的MqttClient

class MockMqttClient : public IMqttClient {
public:
    bool publish(const std::string& topic, const std::string& payload) override {
        // 记录调用，不实际发送
        publishCalls.push_back({topic, payload});
        return true;
    }

    std::vector<std::pair<std::string, std::string>> publishCalls;
};

// 使用Mock测试
MockMqttClient mockClient;
TaskManager manager(&mockClient);
manager.reportResult(...);
assert(mockClient.publishCalls.size() == 1);  // 验证调用了一次
```

**Q4: TDD (测试驱动开发) 的优势?**

A:

1. **先写测试，后写实现** - 明确需求
2. **小步迭代** - 红 → 绿 → 重构循环
3. **接口优先** - 从使用者角度设计 API
4. **高覆盖率** - 自然达到高覆盖

---

## 📊 测试总结

### 当前测试状态

```
单元测试: ✅ 8个测试通过
  - 工厂模式    ✅
  - 基本功能    ✅
  - 辅助方法    ✅
  - Letterbox   ✅
  - 坐标变换    ✅

集成测试: ⏸️ 需要真实环境
  - 实际检测    ⏸️ (需要模型和TPU)
```

### 下一步

1. ✅ **单元测试** - 已完成
2. ⏸️ **集成测试** - 等待模型文件
3. ⏭️ **性能测试** - 测试 FPS 和延迟
4. ⏭️ **压力测试** - 长时间运行稳定性

---

## 🎓 项目亮点话术

```
"在这个项目中，我为Detector模块编写了完整的单元测试。

测试覆盖了:
1. 工厂模式的类型转换和推断逻辑
2. Letterbox预处理算法的正确性验证
3. 坐标逆变换的数学计算验证
4. 边界条件和异常情况处理

通过这些测试，我确保了检测器在各种输入条件下都能正确工作，
并且在后续重构优化时能够快速发现问题。

特别是Letterbox和坐标变换的测试，这些都是核心算法，
我通过理论计算验证了实现的正确性。"
```

---

**创建日期**: 2025-10-31  
**作者**: ESDK Sophon Team  
**版本**: v1.0
