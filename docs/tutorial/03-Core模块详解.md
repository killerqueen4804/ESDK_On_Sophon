# Core 模块详解 - 基础设施

> **学习目标**：掌握日志系统和配置管理的设计与实现
>
> **核心知识点**：
>
> - 单例模式（Meyers Singleton）
> - Pimpl 惯用法（编译防火墙）
> - RAII 原则（资源管理）
> - 线程安全（mutex + lock_guard）
> - 日志轮转与文件管理

---

## 📚 目录

1. [模块概述](#模块概述)
2. [Logger 日志系统](#logger-日志系统)
   - [设计思路](#设计思路)
   - [头文件详解](#头文件详解)
   - [实现文件详解](#实现文件详解)
   - [知识点深入](#知识点深入)
3. [Config 配置管理](#config-配置管理)
   - [设计思路](#config设计思路)
   - [JSON 解析技术](#json解析技术)
   - [嵌套路径访问](#嵌套路径访问)
4. [面试高频考点](#面试高频考点)
5. [实战练习](#实战练习)

---

## 模块概述

Core 模块是整个项目的基础设施层，提供日志和配置两个核心服务：

```
src/core/
├── Logger.cpp      # 日志系统实现
├── Config.cpp      # 配置管理实现
└── CMakeLists.txt  # Core模块构建配置

include/esdk_sophon/core/
├── Logger.h        # 日志系统接口
└── Config.h        # 配置管理接口
```

### 为什么需要这两个模块？

| 模块       | 作用             | 使用场景                       |
| ---------- | ---------------- | ------------------------------ |
| **Logger** | 记录程序运行信息 | 调试、问题排查、运行监控       |
| **Config** | 管理配置参数     | 运行时配置、环境切换、参数调整 |

### 模块依赖关系

```
┌─────────────────────────────────────────────────────────────────┐
│                         应用层                                   │
│   Application, TaskManager, MqttHandler, MediaFileTask ...      │
│                           │                                      │
│                           │ 依赖                                 │
│                           ▼                                      │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐         │
│   │   Logger    │    │   Config    │    │ EventCache  │         │
│   │ (读取自己的 │    │ (读取主配置)│    │ (事件缓存)  │         │
│   │ logger.json)│    │ config.json │    │             │         │
│   └──────┬──────┘    └──────┬──────┘    └─────────────┘         │
│          │                  │                                    │
│          ▼                  ▼                                    │
│   ┌─────────────┐    ┌─────────────┐                            │
│   │ logger.json │    │ config.json │                            │
│   │ (日志配置)  │    │ (主配置)    │                            │
│   └─────────────┘    └─────────────┘                            │
└─────────────────────────────────────────────────────────────────┘
```

**⚠️ 重要设计决策：Logger 和 Config 相互独立**

在早期版本中，Logger 曾依赖 Config 读取配置，但这会导致**循环依赖**问题：

```
问题场景：
Config::load() 想记录日志 → 需要 Logger
Logger::getInstance() 想读取配置 → 需要 Config
→ 循环依赖！谁先初始化？
```

**解决方案**：让 Logger 有自己独立的配置文件 `config/logger.json`

```cpp
// Logger.cpp 中直接读取自己的配置
constexpr const char* LOGGER_CONFIG_FILE = "../config/logger.json";

void Logger::Impl::loadConfigSettings() {
    // 直接使用 nlohmann/json 读取，不依赖 Config 类
    std::ifstream file(LOGGER_CONFIG_FILE);
    if (file.is_open()) {
        nlohmann::json config = nlohmann::json::parse(file);
        // 读取日志级别、输出目录等配置...
    }
}
```

**这样设计的好处**：

1. **无循环依赖**：Logger 和 Config 完全独立，可以按任意顺序初始化
2. **职责分离**：Logger 只关心日志配置，Config 只关心业务配置
3. **启动可靠**：即使 Config 加载失败，Logger 仍能正常工作
4. **便于调试**：日志系统越早可用，越容易排查启动问题

---

## Logger 日志系统

### 设计思路

一个好的日志系统应该具备：

1. **全局唯一**：只有一个日志实例，保证日志的一致性
2. **线程安全**：多线程环境下不会出现日志混乱
3. **可配置**：日志级别、输出位置可动态调整
4. **高性能**：不能成为程序的性能瓶颈
5. **易使用**：API 简单直观

### 头文件详解

让我们逐段分析 `Logger.h`：

#### 1. 头文件保护和命名空间

```cpp
#ifndef ESDK_SOPHON_CORE_LOGGER_H_
#define ESDK_SOPHON_CORE_LOGGER_H_

namespace esdk_sophon {
namespace core {
```

📌 **知识点：头文件保护**

- 防止头文件被多次包含导致重复定义
- 命名规范：`项目名_模块名_文件名_H_`
- C++17 可以用 `#pragma once`（非标准但广泛支持）

#### 2. 日志级别枚举

```cpp
/**
 * @brief 日志级别枚举
 *
 * 级别从低到高：DEBUG < INFO < WARNING < ERROR < FATAL
 * 设置某级别后，只输出大于等于该级别的日志
 */
enum class LogLevel {
    DEBUG = 0,    ///< 调试信息（最详细）
    INFO = 1,     ///< 一般信息
    WARNING = 2,  ///< 警告信息
    ERROR = 3,    ///< 错误信息
    FATAL = 4     ///< 致命错误
};
```

📌 **知识点：enum class（强类型枚举）**

| 特性     | C 风格 enum | C++11 enum class        |
| -------- | ----------- | ----------------------- |
| 作用域   | 无（全局）  | 有（`LogLevel::DEBUG`） |
| 隐式转换 | 会转为 int  | 不会隐式转换            |
| 类型安全 | 弱          | 强                      |
| 命名冲突 | 可能        | 不会                    |

**项目示例**：

```cpp
// ❌ C风格 enum 的问题
enum Color { RED, GREEN };
enum TrafficLight { RED, YELLOW, GREEN };  // 错误：RED重复定义！

// ✅ enum class 解决命名冲突
enum class Color { RED, GREEN };
enum class TrafficLight { RED, YELLOW, GREEN };  // OK！
Color c = Color::RED;
TrafficLight t = TrafficLight::RED;
```

#### 3. Logger 类定义（单例模式）

```cpp
class Logger {
public:
    /**
     * @brief 获取Logger单例
     * @return Logger的全局唯一实例
     */
    static Logger& getInstance();

    // 禁止拷贝和赋值
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
```

📌 **知识点：Meyers Singleton（C++11 线程安全单例）**

**什么是单例模式？**

确保一个类只有一个实例，并提供全局访问点。

**C++11 之前的问题：**

```cpp
// ❌ 非线程安全的双检锁（C++11前）
class Singleton {
    static Singleton* instance;
    static std::mutex mtx;
public:
    static Singleton* getInstance() {
        if (instance == nullptr) {           // 第一次检查
            std::lock_guard<std::mutex> lock(mtx);
            if (instance == nullptr) {       // 第二次检查
                instance = new Singleton();  // 问题：可能被重排序！
            }
        }
        return instance;
    }
};
```

**C++11 的优雅解决方案：**

```cpp
// ✅ Meyers Singleton（C++11线程安全）
Logger& Logger::getInstance() {
    static Logger instance;  // Magic Static
    return instance;
}
```

**为什么返回引用而不是指针？**

1. **防止误用**：用户不能 `delete` 返回的引用
2. **更自然的语法**：`Logger::getInstance().info()` vs `Logger::getInstance()->info()`
3. **不会返回空**：引用永远有效

#### 4. Pimpl 惯用法

```cpp
private:
    Logger();
    ~Logger();

    class Impl;                   // 前向声明
    std::unique_ptr<Impl> pImpl_; // 指向实现的智能指针
};
```

📌 **知识点：Pimpl（Pointer to Implementation）**

**什么是 Pimpl？**

将类的实现细节隐藏在源文件中，头文件只暴露接口。

**为什么使用 Pimpl？**

| 优点           | 说明                                        |
| -------------- | ------------------------------------------- |
| **编译防火墙** | 修改实现不需要重新编译依赖头文件的文件      |
| **隐藏细节**   | 头文件不需要 include `<fstream>`, `<mutex>` |
| **二进制兼容** | 修改 Impl 不影响 ABI（应用二进制接口）      |

**项目示例：**

```cpp
// === Logger.h ===
class Logger {
    class Impl;                    // 只有前向声明，不需要完整定义
    std::unique_ptr<Impl> pImpl_;  // 指向实现
};

// === Logger.cpp ===
class Logger::Impl {               // 完整定义在 .cpp
    std::ofstream logFile_;        // 实现细节
    std::mutex mutex_;             // 实现细节
    // ...
};
```

**编译效率对比：**

```
修改 Impl 的实现
    │
    ▼
┌────────────────────────────────────────┐
│ 不使用 Pimpl                            │
│ Logger.h 改变 → 所有包含它的文件重新编译  │
│ 编译时间：10分钟                         │
└────────────────────────────────────────┘

    │
    ▼
┌────────────────────────────────────────┐
│ 使用 Pimpl                              │
│ Logger.cpp 改变 → 只重新编译 Logger.cpp  │
│ 编译时间：10秒                           │
└────────────────────────────────────────┘
```

#### 5. 日志宏定义

```cpp
// 日志宏（简化调用，自动添加文件和行号）
#define LOG_DEBUG(msg) esdk_sophon::core::Logger::getInstance().debug(msg)
#define LOG_INFO(msg)  esdk_sophon::core::Logger::getInstance().info(msg)
#define LOG_WARN(msg)  esdk_sophon::core::Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) esdk_sophon::core::Logger::getInstance().error(msg)
#define LOG_FATAL(msg) esdk_sophon::core::Logger::getInstance().fatal(msg)
```

📌 **知识点：日志宏 vs 函数调用**

| 方式 | 优点                                | 缺点             |
| ---- | ----------------------------------- | ---------------- |
| 宏   | 可以自动添加 `__FILE__`, `__LINE__` | 不类型安全       |
| 函数 | 类型安全，可调试                    | 无法获取调用位置 |

**高级用法（带文件行号）：**

```cpp
#define LOG_INFO(msg) \
    esdk_sophon::core::Logger::getInstance().info( \
        std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " + msg)
```

---

### 实现文件详解

#### 1. Impl 类的完整定义

```cpp
class Logger::Impl {
public:
    Impl();
    ~Impl();

    void log(LogLevel level, const std::string& message);
    void setLevel(LogLevel level);
    bool setLogFile(const std::string& filePath);
    void setConsoleOutput(bool enable);

private:
    // 格式化方法
    std::string formatLogLine(LogLevel level, const std::string& message);
    std::string getCurrentTime();
    std::string levelToString(LogLevel level);

    // 日志轮转方法
    void rotateLogFile();
    void checkAndRotateLog();
    void cleanupOldLogs();

    // 成员变量
    std::ofstream logFile_;       // 日志文件流
    std::mutex mutex_;            // 互斥锁
    LogLevel currentLevel_;       // 当前日志级别
    bool enableConsole_;          // 是否启用控制台输出
    std::string logFilePath_;     // 当前日志文件路径
    std::string currentDate_;     // 当前日期

    // 配置参数
    std::string logDirectory_;    // 日志目录
    int maxLogDays_;              // 日志保留天数
    size_t maxLogFileSizeBytes_;  // 单文件最大大小
};
```

#### 2. 核心日志方法

```cpp
void Logger::Impl::log(LogLevel level, const std::string& message) {
    // 1. 日志级别过滤
    if (level < currentLevel_) {
        return;
    }

    // 2. 检查是否需要轮转日志文件
    checkAndRotateLog();

    // 3. 格式化日志行
    std::string logLine = formatLogLine(level, message);

    // 4. 加锁保证线程安全（RAII）
    std::lock_guard<std::mutex> lock(mutex_);

    // 5. 写入文件
    if (logFile_.is_open()) {
        logFile_ << logLine << std::endl;
        logFile_.flush();  // 立即刷新
    }

    // 6. 写入控制台
    if (enableConsole_) {
        if (level >= LogLevel::ERROR) {
            std::cerr << logLine << std::endl;  // 错误→stderr
        } else {
            std::cout << logLine << std::endl;  // 普通→stdout
        }
    }
}
```

📌 **知识点：std::lock_guard（RAII 风格的锁）**

```cpp
// ❌ 手动加锁解锁（容易出错）
mutex_.lock();
// ... 操作临界区 ...
mutex_.unlock();  // 如果上面抛出异常，这行不会执行！死锁！

// ✅ RAII 风格（异常安全）
{
    std::lock_guard<std::mutex> lock(mutex_);  // 构造时加锁
    // ... 操作临界区 ...
}  // 析构时自动解锁，即使发生异常也会解锁
```

**lock_guard vs unique_lock：**

| 特性     | std::lock_guard | std::unique_lock |
| -------- | --------------- | ---------------- |
| 复杂度   | 简单            | 复杂             |
| 手动解锁 | 不支持          | 支持             |
| 延迟加锁 | 不支持          | 支持             |
| 条件变量 | 不支持          | 支持             |
| 性能     | 更好            | 略差             |

#### 3. 日志轮转实现

```cpp
void Logger::Impl::rotateLogFile() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 关闭当前日志文件
    if (logFile_.is_open()) {
        logFile_.flush();
        logFile_.close();
    }

    // 获取今天的日期
    currentDate_ = getCurrentDate();

    // 生成新的日志文件路径：logs/esdk_sophon_2025-10-26.log
    logFilePath_ = generateLogFilePath(currentDate_);

    // 打开新文件（追加模式 + 二进制模式）
    logFile_.open(logFilePath_, std::ios::app | std::ios::out | std::ios::binary);

    if (logFile_.is_open()) {
        // 写入 UTF-8 BOM（新文件）
        logFile_.seekp(0, std::ios::end);
        if (logFile_.tellp() == 0) {
            const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            logFile_.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        }
    }
}
```

📌 **知识点：std::filesystem（C++17 文件系统库）**

```cpp
#include <filesystem>
namespace fs = std::filesystem;

// 创建目录（递归创建父目录）
fs::create_directories("logs/2025/10");

// 检查文件是否存在
if (fs::exists("config.json")) { ... }

// 获取文件大小
auto size = fs::file_size("data.bin");

// 遍历目录
for (const auto& entry : fs::directory_iterator("logs")) {
    std::cout << entry.path() << std::endl;
}

// 删除文件
fs::remove("old.log");

// 路径拼接（跨平台！）
fs::path logPath = fs::path("logs") / "esdk_sophon_2025-10-26.log";
```

#### 4. 过期日志清理

```cpp
void Logger::Impl::cleanupOldLogs() {
    try {
        fs::path logDir(logDirectory_);
        if (!fs::exists(logDir)) return;

        // 计算截止日期
        auto now = std::chrono::system_clock::now();
        auto cutoffTime = now - std::chrono::hours(24 * maxLogDays_);

        // 遍历日志目录
        for (const auto& entry : fs::directory_iterator(logDir)) {
            if (!entry.is_regular_file()) continue;

            // 只处理我们的日志文件
            std::string filename = entry.path().filename().string();
            if (filename.find("esdk_sophon_") != 0) continue;

            // 检查文件年龄
            auto fileTime = fs::last_write_time(entry.path());
            // ... 时间比较逻辑 ...

            if (/* 文件过期 */) {
                fs::remove(entry.path());
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "清理日志时出错: " << e.what() << std::endl;
    }
}
```

📌 **知识点：时间处理（std::chrono）**

```cpp
#include <chrono>

// 获取当前时间
auto now = std::chrono::system_clock::now();

// 时间计算
auto yesterday = now - std::chrono::hours(24);
auto twoWeeksAgo = now - std::chrono::hours(24 * 14);

// 转换为 time_t（C风格时间）
auto now_c = std::chrono::system_clock::to_time_t(now);

// 格式化输出
std::stringstream ss;
ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
// 输出：2025-10-26 16:30:45
```

---

### 知识点深入

#### 📌 单例模式的多种实现

**1. 饿汉式（Eager Initialization）**

```cpp
// 程序启动时就创建实例
class Singleton {
private:
    static Singleton instance;  // 静态成员变量
    Singleton() {}
public:
    static Singleton& getInstance() {
        return instance;
    }
};
Singleton Singleton::instance;  // 程序启动时初始化
```

**2. 懒汉式（Lazy Initialization）- C++11 前**

```cpp
// 第一次使用时才创建（需要加锁）
class Singleton {
private:
    static Singleton* instance;
    static std::mutex mtx;
public:
    static Singleton* getInstance() {
        if (instance == nullptr) {
            std::lock_guard<std::mutex> lock(mtx);
            if (instance == nullptr) {
                instance = new Singleton();
            }
        }
        return instance;
    }
};
```

**3. Meyers Singleton（C++11 推荐）**

```cpp
// C++11保证静态局部变量的线程安全初始化
class Singleton {
private:
    Singleton() {}
public:
    static Singleton& getInstance() {
        static Singleton instance;  // Magic Static
        return instance;
    }
};
```

**项目中的选择：**

我们使用 Meyers Singleton，原因：

1. **线程安全**：C++11 标准保证
2. **懒加载**：第一次使用时才创建
3. **代码简洁**：一行代码搞定
4. **零成本**：编译器自动处理同步

#### 📌 Pimpl 的实现要点

**为什么析构函数必须在 .cpp 中定义？**

```cpp
// === Logger.h ===
class Logger {
    class Impl;                    // 只有前向声明
    std::unique_ptr<Impl> pImpl_;
public:
    ~Logger();                     // 声明
};

// === Logger.cpp ===
Logger::~Logger() = default;       // 必须在这里定义！

// 原因：
// unique_ptr 需要知道 Impl 的完整定义才能调用其析构函数
// 在 .h 中，Impl 是不完整类型，编译器不知道如何删除
// 在 .cpp 中，Impl 已经完整定义，所以可以正确析构
```

**如果在头文件中定义会怎样？**

```cpp
// ❌ 错误：在头文件中定义析构函数
class Logger {
    class Impl;
    std::unique_ptr<Impl> pImpl_;
public:
    ~Logger() = default;  // 编译错误！
    // error: invalid application of 'sizeof' to incomplete type 'Logger::Impl'
};
```

---

## Config 配置管理

### Config 设计思路

配置管理的核心需求：

1. **集中管理**：所有配置统一管理，不散落在代码各处
2. **类型安全**：提供类型明确的接口，避免运行时类型错误
3. **默认值支持**：配置缺失时有合理的降级方案
4. **热更新**：支持运行时重新加载配置

### JSON 解析技术

我们使用 **nlohmann/json** 库，这是现代 C++最流行的 JSON 库：

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 解析JSON字符串
json j = json::parse(R"({"name": "test", "value": 42})");

// 解析JSON文件
std::ifstream file("config.json");
json config = json::parse(file);

// 访问值
std::string name = config["name"];
int value = config["value"];

// 安全访问（带默认值）
int port = config.value("port", 1883);  // 不存在则返回1883
```

📌 **知识点：nlohmann/json 的优势**

| 特性            | 说明                     |
| --------------- | ------------------------ |
| **Header-only** | 只需包含一个头文件       |
| **STL 风格**    | 使用方式类似 std::map    |
| **类型推导**    | 自动推导 C++类型         |
| **异常安全**    | 提供异常和非异常两种 API |
| **高性能**      | 零拷贝、内存高效         |

### 嵌套路径访问

我们实现了使用点号(`.`)分隔的嵌套路径访问：

```cpp
// 配置文件 config.json
{
    "logger": {
        "level": "INFO",
        "rotation": {
            "max_days": 15,
            "max_file_size_mb": 100
        }
    },
    "mqtt": {
        "broker": {
            "host": "localhost",
            "port": 1883
        }
    }
}

// 使用方式
auto& config = Config::getInstance();
config.load("config/config.json");

std::string level = config.getString("logger.level", "INFO");
int maxDays = config.getInt("logger.rotation.max_days", 15);
std::string host = config.getString("mqtt.broker.host", "localhost");
int port = config.getInt("mqtt.broker.port", 1883);
```

**实现原理：**

```cpp
std::string Config::Impl::getString(const std::string& key,
                                     const std::string& defaultValue) const {
    json current = configData_;
    std::string currentKey;

    // 遍历路径中的每个字符
    for (char c : key) {
        if (c == '.') {
            // 遇到点号，进入下一层
            if (!current.contains(currentKey)) {
                return defaultValue;
            }
            current = current[currentKey];
            currentKey.clear();
        } else {
            currentKey += c;
        }
    }

    // 处理最后一个键
    if (!currentKey.empty()) {
        if (!current.contains(currentKey)) {
            return defaultValue;
        }
        return current[currentKey].get<std::string>();
    }

    return defaultValue;
}
```

---

## 面试高频考点

### 📌 单例模式

**Q1: 什么是单例模式？有什么优缺点？**

**A1:**

- **定义**：确保一个类只有一个实例，并提供全局访问点
- **优点**：
  - 全局唯一，保证数据一致性
  - 节省资源，不重复创建
  - 方便全局访问
- **缺点**：
  - 难以测试（全局状态）
  - 隐藏依赖关系
  - 可能成为性能瓶颈
- **适用场景**：日志、配置、数据库连接池

**Q2: 如何实现线程安全的单例？**

**A2:**

```cpp
// C++11 推荐：Meyers Singleton
Logger& Logger::getInstance() {
    static Logger instance;  // 编译器保证线程安全
    return instance;
}

// C++11 前：双检锁（DCLP）
Singleton* getInstance() {
    if (instance == nullptr) {
        std::lock_guard<std::mutex> lock(mtx);
        if (instance == nullptr) {
            instance = new Singleton();
        }
    }
    return instance;
}
```

**Q3: 单例的生命周期？析构时机？**

**A3:**

- 静态局部变量单例：程序结束时析构（main 返回后）
- 析构顺序：与构造顺序相反
- 注意：不要在析构函数中访问其他可能已经析构的单例！

### 📌 Pimpl 惯用法

**Q4: 什么是 Pimpl？有什么好处？**

**A4:**

- **定义**：Pointer to Implementation，将实现细节隐藏在源文件中
- **好处**：
  1. **编译防火墙**：修改实现不触发大量重新编译
  2. **隐藏细节**：头文件不暴露私有成员
  3. **二进制兼容**：修改实现不影响 ABI

**Q5: 使用 unique_ptr 实现 Pimpl 要注意什么？**

**A5:**

- 必须在 .cpp 中定义析构函数（即使 `= default`）
- 原因：unique_ptr 需要知道如何删除 Impl 对象
- 在 .h 中 Impl 是不完整类型，编译器不知道其析构函数

### 📌 RAII 原则

**Q6: 什么是 RAII？项目中有哪些应用？**

**A6:**

- **定义**：Resource Acquisition Is Initialization（资源获取即初始化）
- **核心思想**：构造函数获取资源，析构函数释放资源
- **项目应用**：

  ```cpp
  // 1. 智能指针
  std::unique_ptr<Impl> pImpl_;  // 自动释放内存

  // 2. 文件流
  std::ofstream logFile_;  // 析构时自动关闭

  // 3. 互斥锁
  std::lock_guard<std::mutex> lock(mutex_);  // 自动解锁
  ```

### 📌 线程安全

**Q7: 如何保证日志系统的线程安全？**

**A7:**

```cpp
// 使用 mutex + lock_guard
void Logger::Impl::log(LogLevel level, const std::string& message) {
    // ... 非临界区代码 ...

    std::lock_guard<std::mutex> lock(mutex_);  // 加锁
    // 临界区：文件写入
    if (logFile_.is_open()) {
        logFile_ << logLine << std::endl;
    }
    // 自动解锁
}
```

**Q8: lock_guard 和 unique_lock 的区别？**

**A8:**
| 特性 | lock_guard | unique_lock |
|------|------------|-------------|
| 手动解锁 | ❌ | ✅ |
| 延迟加锁 | ❌ | ✅ |
| 条件变量 | ❌ | ✅ |
| 移动语义 | ❌ | ✅ |
| 性能 | 更好 | 略差 |

---

## 实战练习

### 练习 1：添加日志级别动态修改接口

**需求**：通过 MQTT 命令动态修改日志级别

**提示**：

1. 在 MqttClient 中订阅 `system/log/level` 主题
2. 收到消息时调用 `Logger::getInstance().setLevel()`
3. 解析消息内容（如 `{"level": "DEBUG"}`）

### 练习 2：实现日志压缩功能

**需求**：将超过 7 天的日志自动压缩

**提示**：

1. 使用 zlib 库进行 gzip 压缩
2. 在 `cleanupOldLogs()` 中添加压缩逻辑
3. 保留原文件名 + `.gz` 后缀

### 练习 3：添加配置变更通知

**需求**：当配置重新加载时，通知所有监听者

**提示**：

1. 使用观察者模式
2. 添加 `registerListener()` 和 `notifyListeners()` 方法
3. 在 `reload()` 成功后调用 `notifyListeners()`

---

## 总结

本节我们深入学习了 Core 模块的两个核心组件：

| 组件       | 设计模式         | 关键技术                          |
| ---------- | ---------------- | --------------------------------- |
| **Logger** | 单例模式 + Pimpl | mutex、RAII、日志轮转、filesystem |
| **Config** | 单例模式 + Pimpl | JSON 解析、嵌套路径、异常处理     |

**核心收获**：

1. **单例模式**：C++11 Meyers Singleton 是最佳实践
2. **Pimpl**：编译防火墙，必须在 .cpp 中定义析构函数
3. **RAII**：资源管理的黄金法则，lock_guard 是典型应用
4. **线程安全**：mutex 保护临界区，lock_guard 自动管理锁

**下一节预告**：[04-MQTT 模块详解](./04-MQTT模块详解.md) - 学习 Qt 信号槽机制和异步通信

---

_创建日期：2025-10-26_
_适用版本：ESDK_On_Sophon v1.0_
