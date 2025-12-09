# src 目录结构设计方案

> 📂 专业的、模块化的 C++项目目录结构设计

---

## 📋 设计原则

### 1. 模块化原则

- 按功能领域划分模块
- 每个模块职责单一、边界清晰
- 模块间低耦合、高内聚

### 2. 层次化原则

```
应用层 (main.cpp)
    ↓
业务逻辑层 (mqtt, vision, device)
    ↓
核心基础层 (core: logger, config)
    ↓
第三方库层 (third_party)
```

### 3. 头文件分离原则

```
include/           # 公共接口（对外暴露）
src/               # 实现细节（内部使用）
```

---

## 🏗️ 目录结构设计

### 完整目录树

```
ESDK_On_Sophon/
├── include/                          # 公共头文件目录（对外接口）
│   └── esdk_sophon/                 # 项目命名空间目录
│       ├── core/                     # 核心模块接口
│       │   ├── Logger.h             # 日志系统
│       │   ├── Config.h             # 配置管理
│       │   └── Application.h        # 应用程序主类
│       ├── mqtt/                     # MQTT模块接口
│       │   ├── MqttClient.h         # MQTT客户端
│       │   └── MqttProcessor.h      # 消息处理器
│       ├── vision/                   # 视觉处理模块接口
│       │   ├── IDetector.h          # 检测器接口（抽象类）
│       │   ├── ISegmentor.h         # 分割器接口（抽象类）
│       │   └── VisionProcessor.h    # 视觉处理器
│       ├── device/                   # 设备控制模块接口
│       │   └── DroneController.h    # 无人机控制器
│       ├── storage/                  # 数据存储模块接口
│       │   ├── Database.h           # 数据库接口
│       │   └── DownloadManager.h    # 下载管理器
│       └── utils/                    # 工具类接口
│           ├── FileUtils.h
│           ├── StringUtils.h
│           └── TimeUtils.h
│
├── src/                              # 源代码实现目录
│   ├── CMakeLists.txt               # 源码总配置
│   │
│   ├── core/                         # 核心模块实现
│   │   ├── CMakeLists.txt
│   │   ├── Logger.cpp               # 日志系统实现
│   │   ├── Config.cpp               # 配置管理实现
│   │   └── Application.cpp          # 应用程序主类实现
│   │
│   ├── mqtt/                         # MQTT通信模块
│   │   ├── CMakeLists.txt
│   │   ├── MqttClient.cpp           # MQTT客户端实现
│   │   ├── MqttProcessor.cpp        # 消息处理器实现
│   │   └── MessageQueue.cpp         # 消息队列（内部）
│   │   └── MessageQueue.h           # 内部头文件
│   │
│   ├── vision/                       # 视觉处理模块
│   │   ├── CMakeLists.txt
│   │   ├── VisionProcessor.cpp      # 视觉处理器实现
│   │   │
│   │   ├── detector/                # 目标检测子模块
│   │   │   ├── CMakeLists.txt
│   │   │   ├── IDetector.cpp        # 接口实现（如果需要）
│   │   │   ├── Yolov10Detector.cpp  # YOLOv10实现
│   │   │   ├── Yolov10Detector.h    # 内部头文件
│   │   │   ├── Yolov8Detector.cpp   # YOLOv8实现（扩展）
│   │   │   └── DetectorFactory.cpp  # 工厂类
│   │   │   └── DetectorFactory.h
│   │   │
│   │   ├── segmentation/            # 图像分割子模块
│   │   │   ├── CMakeLists.txt
│   │   │   ├── ISegmentor.cpp
│   │   │   ├── PaddleSegmentor.cpp  # PaddleSeg实现
│   │   │   └── PaddleSegmentor.h
│   │   │
│   │   └── stream/                  # 视频流处理子模块
│   │       ├── CMakeLists.txt
│   │       ├── StreamDecoder.cpp    # 流解码器
│   │       ├── StreamDecoder.h
│   │       ├── StreamProcessor.cpp  # 流处理器
│   │       └── RtmpStreamer.cpp     # RTMP推流
│   │       └── RtmpStreamer.h
│   │
│   ├── device/                       # 设备控制模块
│   │   ├── CMakeLists.txt
│   │   ├── DroneController.cpp      # 无人机控制实现
│   │   └── CommandHandler.cpp       # 命令处理器（内部）
│   │   └── CommandHandler.h
│   │
│   ├── storage/                      # 数据存储模块
│   │   ├── CMakeLists.txt
│   │   ├── Database.cpp             # 数据库实现
│   │   ├── DownloadManager.cpp      # 下载管理器
│   │   └── FileManager.cpp          # 文件管理器（内部）
│   │   └── FileManager.h
│   │
│   ├── utils/                        # 工具类实现
│   │   ├── CMakeLists.txt
│   │   ├── FileUtils.cpp
│   │   ├── StringUtils.cpp
│   │   └── TimeUtils.cpp
│   │
│   └── main.cpp                      # 程序入口
│
├── third_party/                      # 第三方库（已完成）
│   └── CMakeLists.txt
│
├── tests/                            # 单元测试（后续添加）
│   ├── CMakeLists.txt
│   ├── core/
│   ├── mqtt/
│   └── vision/
│
├── config/                           # 配置文件
│   └── config.json
│
├── docs/                             # 文档（已有）
│   ├── 重构计划.md
│   ├── 改进记录.md
│   └── ...
│
├── CMakeLists.txt                    # 根CMake配置
└── README.md
```

---

## 📦 模块详细说明

### 1. core/ - 核心基础模块

**职责**: 提供项目的基础设施和公共服务

| 类名        | 文件              | 职责           | 设计模式 |
| ----------- | ----------------- | -------------- | -------- |
| Logger      | Logger.h/cpp      | 日志系统，单例 | 单例模式 |
| Config      | Config.h/cpp      | 配置管理，单例 | 单例模式 |
| Application | Application.h/cpp | 应用程序主类   | 外观模式 |

**依赖**:

- 第三方库: 无（或仅标准库）
- 其他模块: 无

**被依赖**: 几乎所有模块都依赖 core

---

### 2. mqtt/ - MQTT 通信模块

**职责**: 处理 MQTT 通信、消息收发、主题订阅

| 类名          | 文件                | 职责             | 设计模式      |
| ------------- | ------------------- | ---------------- | ------------- |
| MqttClient    | MqttClient.h/cpp    | MQTT 客户端封装  | 适配器模式    |
| MqttProcessor | MqttProcessor.h/cpp | 消息处理和分发   | 观察者模式    |
| MessageQueue  | MessageQueue.h/cpp  | 消息队列（内部） | 生产者-消费者 |

**依赖**:

- 第三方库: ThirdParty::Qt5Mqtt, ThirdParty::Json
- 其他模块: core (Logger, Config)

**被依赖**: main, device

**接口示例**:

```cpp
class MqttClient {
public:
    static MqttClient& getInstance();

    void connect(const std::string& host, int port);
    void subscribe(const std::string& topic);
    void publish(const std::string& topic, const std::string& message);

    // 观察者模式：注册消息回调
    void registerCallback(const std::string& topic,
                         std::function<void(const std::string&)> callback);
};
```

---

### 3. vision/ - 视觉处理模块

**职责**: 视觉算法、目标检测、图像分割、视频流处理

#### 3.1 detector/ - 目标检测子模块

| 类名            | 文件                  | 职责                 | 设计模式 |
| --------------- | --------------------- | -------------------- | -------- |
| IDetector       | IDetector.h           | 检测器接口（抽象类） | 接口模式 |
| Yolov10Detector | Yolov10Detector.h/cpp | YOLOv10 实现         | 策略模式 |
| DetectorFactory | DetectorFactory.h/cpp | 创建检测器           | 工厂模式 |

**依赖**:

- 第三方库: ThirdParty::FastDeploy, ThirdParty::OpenCV
- 其他模块: core

**接口示例**:

```cpp
// 抽象基类
class IDetector {
public:
    virtual ~IDetector() = default;
    virtual DetectionResult detect(const cv::Mat& image) = 0;
    virtual bool initialize(const std::string& modelPath) = 0;
};

// 具体实现
class Yolov10Detector : public IDetector {
public:
    DetectionResult detect(const cv::Mat& image) override;
    bool initialize(const std::string& modelPath) override;
private:
    std::unique_ptr<fastdeploy::vision::detection::YOLOv10> model_;
};

// 工厂类
class DetectorFactory {
public:
    enum class Type { YOLOV10, YOLOV8, PPYOLO };
    static std::unique_ptr<IDetector> create(Type type);
};
```

#### 3.2 segmentation/ - 图像分割子模块

类似 detector 的结构

#### 3.3 stream/ - 视频流处理子模块

| 类名            | 文件                  | 职责       |
| --------------- | --------------------- | ---------- |
| StreamDecoder   | StreamDecoder.h/cpp   | 视频流解码 |
| StreamProcessor | StreamProcessor.h/cpp | 流处理管道 |
| RtmpStreamer    | RtmpStreamer.h/cpp    | RTMP 推流  |

**依赖**:

- 第三方库: ThirdParty::FFmpeg, ThirdParty::GStreamer
- 其他模块: core, vision/detector

---

### 4. device/ - 设备控制模块

**职责**: 无人机设备控制、命令发送

| 类名            | 文件                  | 职责             | 设计模式 |
| --------------- | --------------------- | ---------------- | -------- |
| DroneController | DroneController.h/cpp | 无人机控制       | 外观模式 |
| CommandHandler  | CommandHandler.h/cpp  | 命令处理（内部） | 命令模式 |

**依赖**:

- 第三方库: ThirdParty::ESDK
- 其他模块: core, mqtt

---

### 5. storage/ - 数据存储模块

**职责**: 数据持久化、文件管理、下载管理

| 类名            | 文件                  | 职责             | 设计模式        |
| --------------- | --------------------- | ---------------- | --------------- |
| Database        | Database.h/cpp        | 数据库操作       | Repository 模式 |
| DownloadManager | DownloadManager.h/cpp | 文件下载管理     | 单例模式        |
| FileManager     | FileManager.h/cpp     | 文件管理（内部） | -               |

**依赖**:

- 第三方库: ThirdParty::Qt5Sql
- 其他模块: core

---

### 6. utils/ - 工具类模块

**职责**: 提供通用工具函数

| 类名        | 文件              | 职责           |
| ----------- | ----------------- | -------------- |
| FileUtils   | FileUtils.h/cpp   | 文件操作工具   |
| StringUtils | StringUtils.h/cpp | 字符串处理工具 |
| TimeUtils   | TimeUtils.h/cpp   | 时间处理工具   |

**依赖**: 无（或仅标准库）

**特点**:

- 通常是静态函数
- 无状态
- 可被任何模块使用

---

## 🔗 模块依赖关系图

```
                    main.cpp
                       ↓
        ┌──────────────┼──────────────┐
        ↓              ↓              ↓
    device/        mqtt/          vision/
        ↓              ↓              ↓
        └──────────────┼──────────────┘
                       ↓
                    core/
                       ↓
                 third_party/
```

**依赖层次**:

1. **第 0 层**: third_party (无依赖)
2. **第 1 层**: core, utils (仅依赖第三方库)
3. **第 2 层**: mqtt, vision, storage (依赖 core)
4. **第 3 层**: device (依赖 mqtt, core)
5. **第 4 层**: main (依赖所有)

---

## 📝 头文件组织规则

### 为什么要分离 include 和 src？

**include/**: 公共接口，对外暴露

```cpp
// include/esdk_sophon/core/Logger.h
#ifndef ESDK_SOPHON_CORE_LOGGER_H_
#define ESDK_SOPHON_CORE_LOGGER_H_

namespace esdk_sophon {
namespace core {

class Logger {
public:
    static Logger& getInstance();
    void info(const std::string& message);
    // 公共接口...

private:
    Logger();  // 隐藏实现细节
    class Impl;  // Pimpl惯用法
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace core
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_CORE_LOGGER_H_
```

**src/**: 内部实现细节

```cpp
// src/core/Logger.cpp
#include "esdk_sophon/core/Logger.h"
#include "LoggerImpl.h"  // 内部头文件，不暴露

// 内部实现类（用户看不到）
class Logger::Impl {
    std::ofstream logFile_;
    std::mutex mutex_;
    // 实现细节...
};
```

**优势**:

1. ✅ 隐藏实现细节
2. ✅ 减少编译依赖
3. ✅ 便于生成文档
4. ✅ 符合工业标准

---

## 🎯 命名规范

### 1. 命名空间

```cpp
namespace esdk_sophon {  // 项目顶层命名空间
namespace core {         // 模块命名空间
namespace mqtt {
namespace vision {
// ...
}
```

### 2. 类名

```cpp
class Logger           // 大驼峰 (PascalCase)
class MqttClient
class Yolov10Detector
```

### 3. 文件名

```cpp
Logger.h / Logger.cpp          // 与类名一致
MqttClient.h / MqttClient.cpp
Yolov10Detector.h / .cpp
```

### 4. 接口类

```cpp
class IDetector    // I开头表示接口（抽象类）
class ISegmentor
```

### 5. 头文件保护

```cpp
#ifndef ESDK_SOPHON_CORE_LOGGER_H_
#define ESDK_SOPHON_CORE_LOGGER_H_
// 格式：项目名_模块名_文件名_H_
// 全大写，下划线分隔
#endif  // ESDK_SOPHON_CORE_LOGGER_H_
```

---

## 📊 CMake 组织

### 层次结构

```cmake
# 根CMakeLists.txt
add_subdirectory(src)

# src/CMakeLists.txt
add_subdirectory(core)
add_subdirectory(mqtt)
add_subdirectory(vision)
add_subdirectory(device)
add_subdirectory(storage)
add_subdirectory(utils)

# src/core/CMakeLists.txt
add_library(core STATIC
    Logger.cpp
    Config.cpp
    Application.cpp
)
```

---

## 🎓 设计模式应用

| 模块                   | 设计模式        | 应用场景             |
| ---------------------- | --------------- | -------------------- |
| core/Logger            | 单例模式        | 全局唯一的日志实例   |
| core/Config            | 单例模式        | 全局配置管理         |
| vision/IDetector       | 策略模式        | 不同检测算法可互换   |
| vision/DetectorFactory | 工厂模式        | 创建不同类型的检测器 |
| mqtt/MqttProcessor     | 观察者模式      | 消息到达时通知订阅者 |
| device/CommandHandler  | 命令模式        | 封装设备控制命令     |
| storage/Database       | Repository 模式 | 数据访问抽象         |

---

## 下一步行动

现在我们有了完整的设计方案，下一步：

1. ✅ 创建目录结构
2. ✅ 创建各模块的 CMakeLists.txt
3. ✅ 实现 core/Logger 类（单例模式）
4. ✅ 实现 core/Config 类
5. ✅ 逐步重构其他模块

准备好了吗？告诉我："开始创建目录结构！"

---

**设计日期**: 2025-10-23  
**设计版本**: v1.0
