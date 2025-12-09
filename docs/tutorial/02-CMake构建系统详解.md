# 第二章：CMake 构建系统详解

> 📅 创建日期：2025-11-26
> 🎯 学习目标：深入理解 CMake 构建系统，掌握现代 CMake 最佳实践

---

## 2.1 CMake 是什么？

### 概念介绍

**CMake** (Cross-platform Make) 是一个跨平台的构建系统生成器。它不直接编译代码，而是生成特定平台的构建文件（如 Makefile、Visual Studio 项目等）。

```
┌─────────────────────────────────────────────────────────────┐
│                    CMake 工作流程                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   CMakeLists.txt                                            │
│        │                                                    │
│        ▼                                                    │
│   ┌─────────┐      ┌─────────────────────────────────┐     │
│   │  cmake  │ ───▶ │ Makefile / build.ninja / .sln   │     │
│   └─────────┘      └─────────────────────────────────┘     │
│                              │                              │
│                              ▼                              │
│                    ┌─────────────────────┐                  │
│                    │  make / ninja / MSBuild  │             │
│                    └─────────────────────┘                  │
│                              │                              │
│                              ▼                              │
│                    ┌─────────────────────┐                  │
│                    │   可执行文件 / 库   │                   │
│                    └─────────────────────┘                  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 为什么使用 CMake？

| 特性         | 说明                                      |
| ------------ | ----------------------------------------- |
| **跨平台**   | 一份配置，多平台编译（Windows/Linux/Mac） |
| **IDE 支持** | 自动生成 VS/CLion/Xcode 项目              |
| **依赖管理** | 自动处理库依赖和头文件路径                |
| **现代特性** | 支持 target-based 配置（CMake 3.x）       |
| **广泛使用** | C++ 项目的事实标准                        |

---

## 2.2 项目 CMake 结构

### 文件层次

```
ESDK_On_Sophon/
├── CMakeLists.txt              # 🔴 根配置（入口）
├── third_party/
│   └── CMakeLists.txt          # 🟠 第三方库配置
├── src/
│   ├── CMakeLists.txt          # 🟡 源码总配置
│   ├── core/
│   │   └── CMakeLists.txt      # 🟢 core 模块
│   ├── mqtt/
│   │   └── CMakeLists.txt      # 🟢 mqtt 模块
│   ├── vision/
│   │   └── CMakeLists.txt      # 🟢 vision 模块
│   └── ...                     # 其他模块
└── tests/
    └── CMakeLists.txt          # 🔵 测试配置
```

---

## 2.3 根 CMakeLists.txt 详解

让我们逐段分析根 CMakeLists.txt：

### 2.3.1 CMake 版本要求

```cmake
cmake_minimum_required(VERSION 3.10)
```

**📚 知识点**：

- `cmake_minimum_required` 必须是第一条命令
- 指定最低 CMake 版本要求
- 版本 3.10 支持 `target_link_libraries` 的 IMPORTED 目标

**⚠️ 面试要点**：

> Q: 为什么要指定 CMake 最低版本？
> A: 1. 确保所使用的 CMake 特性可用 2. 避免在旧版本 CMake 上出现兼容性问题 3. CMake 3.x 和 2.x 语法差异很大

---

### 2.3.2 交叉编译配置

```cmake
# 目标系统：Linux ARM64
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 交叉编译器路径
set(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)
```

**📚 知识点：什么是交叉编译？**

```
                    ┌───────────────────────────────────────┐
                    │            本地编译                    │
                    │  编译环境 ═══════════ 运行环境         │
                    │  (x86-64)             (x86-64)        │
                    │     │                    │            │
                    │     └──────┬─────────────┘            │
                    │            ▼                          │
                    │       同一台机器                       │
                    └───────────────────────────────────────┘

                    ┌───────────────────────────────────────┐
                    │            交叉编译                    │
                    │  编译环境 ──────────▶ 运行环境         │
                    │  (x86-64)             (ARM64)         │
                    │     │                    │            │
                    │  Docker容器         SE7 微服务器       │
                    │  (stream_lzy)       (Sophon)          │
                    └───────────────────────────────────────┘
```

**为什么需要交叉编译？**

1. SE7 设备算力有限，直接编译很慢
2. 开发机（x86）和目标机（ARM）架构不同
3. 服务器 Docker 容器编译更快、环境一致

**两种配置方式对比**：

| 方式                        | 优点                 | 缺点                       |
| --------------------------- | -------------------- | -------------------------- |
| **CMakeLists.txt 直接配置** | 简单，一劳永逸       | 耦合度高，切换平台需改文件 |
| **toolchain 文件**          | 分离配置，CMake 推荐 | 每次都要指定参数           |

本项目选择直接配置，因为 Docker 容器专门用于交叉编译。

---

### 2.3.3 项目声明

```cmake
project(ESDK_Sophon
    VERSION 1.0.0
    DESCRIPTION "DJI ESDK智能边缘计算系统"
    LANGUAGES CXX
)
```

**📚 知识点**：

- `project()` 定义项目名称和元信息
- `VERSION`：版本号，可用 `${PROJECT_VERSION}` 引用
- `DESCRIPTION`：项目描述
- `LANGUAGES`：使用的语言（CXX = C++）
- 执行后会设置变量：`PROJECT_NAME`, `PROJECT_SOURCE_DIR` 等

**⚠️ 重要**：交叉编译配置必须在 `project()` 之前！

---

### 2.3.4 C++ 标准设置

```cmake
# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # 强制要求，不支持则报错
set(CMAKE_CXX_EXTENSIONS OFF)        # 禁用 GNU 扩展
```

**📚 知识点：C++17 新特性**

本项目使用的 C++17 特性：

| 特性                | 示例                               | 使用位置          |
| ------------------- | ---------------------------------- | ----------------- |
| **结构化绑定**      | `auto [key, value] = map.begin();` | `VisionUtils.cpp` |
| **if constexpr**    | 编译期条件分支                     | 模板代码          |
| **std::optional**   | 可空值                             | 配置读取          |
| **std::filesystem** | 文件系统操作                       | `FileUtils.cpp`   |
| **折叠表达式**      | 可变参数模板                       | 日志系统          |

**⚠️ 面试要点**：

> Q: 为什么设置 `CMAKE_CXX_EXTENSIONS OFF`？
> A: 禁用编译器扩展（如 `__attribute__`），保证代码在不同编译器上的可移植性。

---

### 2.3.5 编译选项

```cmake
# 通用选项
add_compile_options(
    -Wall          # 开启所有警告
    -Wextra        # 开启额外警告
    -pthread       # 支持多线程
)

# Debug 模式
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Release 模式
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
```

**📚 知识点：编译选项详解**

| 选项       | 说明                          |
| ---------- | ----------------------------- |
| `-Wall`    | 开启常见警告（建议始终开启）  |
| `-Wextra`  | 开启更多警告                  |
| `-pthread` | 启用 POSIX 线程支持           |
| `-g`       | 生成调试信息                  |
| `-O0`      | 不优化（便于调试）            |
| `-O3`      | 最高级别优化                  |
| `-DDEBUG`  | 定义 DEBUG 宏                 |
| `-DNDEBUG` | 定义 NDEBUG 宏（禁用 assert） |

**构建类型对比**：

```
                Debug 模式                    Release 模式
              ┌────────────┐               ┌────────────┐
  编译选项    │ -g -O0     │               │ -O3        │
              ├────────────┤               ├────────────┤
  可执行文件  │ 较大       │               │ 较小       │
              ├────────────┤               ├────────────┤
  运行速度    │ 较慢       │               │ 较快       │
              ├────────────┤               ├────────────┤
  可调试性    │ 可用 gdb   │               │ 难以调试   │
              └────────────┘               └────────────┘
```

---

### 2.3.6 输出目录设置

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # 可执行文件
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 动态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库
```

**📚 知识点**：

- `CMAKE_BINARY_DIR`：构建目录（如 `build/`）
- `CMAKE_SOURCE_DIR`：源码根目录
- 这样设置后，所有输出文件集中到 `build/bin/` 和 `build/lib/`

---

### 2.3.7 添加子目录

```cmake
add_subdirectory(third_party)  # 第三方库
add_subdirectory(src)          # 源代码
add_subdirectory(tests)        # 测试
```

**📚 知识点**：

- `add_subdirectory()` 会进入指定目录，执行其中的 `CMakeLists.txt`
- 子目录可以访问父目录定义的变量
- 执行顺序很重要：先配置依赖项，再配置主程序

---

## 2.4 第三方库配置（third_party/CMakeLists.txt）

### 2.4.1 IMPORTED 目标

```cmake
# 创建 IMPORTED 目标
add_library(ThirdParty::OpenCV INTERFACE IMPORTED GLOBAL)

# 设置属性
set_target_properties(ThirdParty::OpenCV PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_ROOT}/opencv4.2/include/opencv4"
)
```

**📚 知识点：IMPORTED 目标**

IMPORTED 目标是 CMake 管理预编译库的现代方式：

```
传统方式（不推荐）                    现代方式（推荐）
──────────────────                    ──────────────────
include_directories(...)              target_link_libraries(
link_directories(...)                     my_target
link_libraries(...)                       ThirdParty::OpenCV
                                      )

问题：全局污染                         优势：
- 所有目标都受影响                    - 自动获得头文件路径
- 难以追踪依赖                        - 自动传递依赖
- 容易出错                            - 作用域清晰
```

### 2.4.2 库类型

```cmake
# STATIC：静态库（.a）
add_library(ThirdParty::ESDK STATIC IMPORTED GLOBAL)

# SHARED：动态库（.so）
add_library(ThirdParty::OpenCV SHARED IMPORTED GLOBAL)

# INTERFACE：只有接口，没有库文件（如 header-only 库）
add_library(ThirdParty::Json INTERFACE IMPORTED GLOBAL)
```

**📚 知识点：静态库 vs 动态库**

| 特性           | 静态库 (.a)  | 动态库 (.so) |
| -------------- | ------------ | ------------ |
| 链接时机       | 编译时       | 运行时       |
| 可执行文件大小 | 较大         | 较小         |
| 依赖分发       | 无需额外文件 | 需要分发 .so |
| 内存使用       | 多进程不共享 | 多进程可共享 |
| 更新方式       | 需重新编译   | 只需替换 .so |

本项目的选择：

- `libedgesdk.a`（静态）：DJI SDK，稳定不变
- `libopencv_*.so`（动态）：OpenCV，系统可能已安装
- `nlohmann/json`（INTERFACE）：header-only，无需编译

---

## 2.5 模块 CMakeLists.txt 示例

### 2.5.1 core 模块

```cmake
# src/core/CMakeLists.txt

# 1. 定义源文件
set(CORE_SOURCES
    Logger.cpp
    Config.cpp
    EventCache.cpp
)

# 2. 创建静态库
add_library(core STATIC ${CORE_SOURCES})

# 3. 设置头文件路径
target_include_directories(core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 4. 链接依赖
target_link_libraries(core
    PUBLIC
        ThirdParty::Json     # JSON 解析
    PRIVATE
        ThirdParty::Qt5Core  # Qt 只在内部使用
)
```

**📚 知识点：PUBLIC vs PRIVATE vs INTERFACE**

```
                        ┌────────────────────────────────────────┐
                        │       target_link_libraries            │
                        ├────────────────────────────────────────┤
                        │                                        │
  PRIVATE ─────────────▶│ 只在当前目标使用，不传递给依赖者        │
                        │ 例：core 内部用 Qt，使用者不需要知道    │
                        │                                        │
  PUBLIC ──────────────▶│ 当前目标使用，且传递给依赖者            │
                        │ 例：core 用 JSON，使用 core 也需要 JSON │
                        │                                        │
  INTERFACE ───────────▶│ 当前目标不用，但传递给依赖者            │
                        │ 例：header-only 库                     │
                        │                                        │
                        └────────────────────────────────────────┘
```

**实际场景**：

```cmake
# core 模块
target_link_libraries(core
    PUBLIC  ThirdParty::Json  # Logger.h 暴露了 JSON 类型
    PRIVATE ThirdParty::Qt5Core  # Config.cpp 内部用 Qt，不暴露
)

# 使用 core 的模块
target_link_libraries(mqtt
    PRIVATE core  # 自动获得 JSON，但不会获得 Qt5Core
)
```

---

### 2.5.2 生成器表达式

```cmake
target_include_directories(core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
```

**📚 知识点：生成器表达式**

生成器表达式 `$<...>` 在生成构建系统时求值：

| 表达式                     | 说明               | 求值时机        |
| -------------------------- | ------------------ | --------------- |
| `$<BUILD_INTERFACE:...>`   | 构建时使用的路径   | cmake --build   |
| `$<INSTALL_INTERFACE:...>` | 安装后使用的路径   | cmake --install |
| `$<TARGET_FILE:target>`    | 目标文件的完整路径 | 运行时          |
| `$<$<CONFIG:Debug>:...>`   | 仅 Debug 模式生效  | 构建时          |

**为什么要这样写？**

```
构建时（开发阶段）              安装后（部署阶段）
──────────────────              ──────────────────
include/esdk_sophon/...         /usr/local/include/esdk_sophon/...

$<BUILD_INTERFACE:...>          $<INSTALL_INTERFACE:...>
```

---

## 2.6 完整编译流程

### 2.6.1 流程图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         CMake 构建流程                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. 配置阶段 (cmake ..)                                                 │
│     ┌─────────────────────────────────────────────────────────────┐    │
│     │ CMakeLists.txt ──▶ 解析 ──▶ CMakeCache.txt + Makefile       │    │
│     └─────────────────────────────────────────────────────────────┘    │
│                                                                         │
│  2. 构建阶段 (make)                                                     │
│     ┌─────────────────────────────────────────────────────────────┐    │
│     │                                                             │    │
│     │  第三方库 (IMPORTED)                                        │    │
│     │       ↓                                                     │    │
│     │  core.a ← Logger.cpp, Config.cpp                            │    │
│     │       ↓                                                     │    │
│     │  mqtt.a ← MqttClient.cpp (链接 core.a)                      │    │
│     │       ↓                                                     │    │
│     │  vision.a ← PPYOLOEDetector.cpp (链接 core.a)               │    │
│     │       ↓                                                     │    │
│     │  ... 其他模块 ...                                           │    │
│     │       ↓                                                     │    │
│     │  ESDK_Sophon ← main.cpp + Application.cpp                   │    │
│     │               + 所有模块.a                                  │    │
│     │               + 第三方库                                    │    │
│     │                                                             │    │
│     └─────────────────────────────────────────────────────────────┘    │
│                                                                         │
│  3. 输出                                                                │
│     ┌─────────────────────────────────────────────────────────────┐    │
│     │ build/                                                      │    │
│     │ ├── bin/                                                    │    │
│     │ │   └── ESDK_Sophon          # 可执行文件                   │    │
│     │ └── lib/                                                    │    │
│     │     ├── libcore.a            # 核心模块                     │    │
│     │     ├── libmqtt.a            # MQTT模块                     │    │
│     │     ├── libvision.a          # 视觉模块                     │    │
│     │     └── ...                                                 │    │
│     └─────────────────────────────────────────────────────────────┘    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.6.2 实际命令

```bash
# 1. 进入 Docker 容器
docker exec -it stream_lzy bash

# 2. 进入项目目录
cd /workspace/ESDK_On_Sophon

# 3. 创建并进入 build 目录
mkdir -p build && cd build

# 4. CMake 配置（只需执行一次）
cmake ..

# 5. 编译（使用所有 CPU 核心）
make -j$(nproc)

# 6. 验证输出
ls -la bin/ESDK_Sophon
file bin/ESDK_Sophon  # 应显示: ELF 64-bit LSB executable, ARM aarch64
```

---

## 2.7 CMake 调试技巧

### 2.7.1 查看变量值

```cmake
message(STATUS "变量值: ${MY_VAR}")
message(WARNING "这是警告")
message(FATAL_ERROR "致命错误，停止配置")
```

### 2.7.2 常用内置变量

| 变量                       | 说明                           |
| -------------------------- | ------------------------------ |
| `CMAKE_SOURCE_DIR`         | 顶层 CMakeLists.txt 所在目录   |
| `CMAKE_BINARY_DIR`         | 构建目录（如 build/）          |
| `CMAKE_CURRENT_SOURCE_DIR` | 当前处理的 CMakeLists.txt 目录 |
| `CMAKE_CURRENT_BINARY_DIR` | 当前目录对应的构建目录         |
| `PROJECT_NAME`             | 项目名称                       |
| `PROJECT_VERSION`          | 项目版本                       |

### 2.7.3 查看目标属性

```cmake
# 打印目标的所有属性
get_target_property(INCLUDES my_target INCLUDE_DIRECTORIES)
message(STATUS "Include directories: ${INCLUDES}")
```

---

## 📚 面试要点总结

### Q1: 解释 CMake 的 target_link_libraries 中 PUBLIC/PRIVATE/INTERFACE 的区别？

**标准答案**：

> - **PRIVATE**：依赖只在当前目标内部使用，不会传递给链接此目标的其他目标
> - **PUBLIC**：依赖在当前目标使用，且会传递给链接此目标的其他目标
> - **INTERFACE**：依赖不在当前目标使用，但会传递给链接此目标的其他目标
>
> 示例：如果 core 模块 PUBLIC 链接了 JSON 库，那么链接 core 的 mqtt 模块会自动获得 JSON 的头文件路径和链接。

### Q2: 什么是交叉编译？什么场景下需要？

**标准答案**：

> 交叉编译是在一种架构的机器上编译另一种架构的可执行文件。
>
> 使用场景：
>
> 1. 目标设备算力有限（嵌入式设备、ARM 服务器）
> 2. 目标设备没有编译环境
> 3. 需要批量编译不同架构的版本
>
> 本项目在 x86-64 Docker 容器中使用 aarch64-linux-gnu-g++ 编译，生成 ARM64 可执行文件，部署到 Sophon SE7 微服务器。

### Q3: CMake 的 IMPORTED 目标有什么优势？

**标准答案**：

> IMPORTED 目标是现代 CMake 管理预编译库的方式：
>
> 1. **封装性好**：库文件、头文件、依赖都封装在一个目标中
> 2. **依赖传递**：通过 `INTERFACE_*` 属性自动传递
> 3. **作用域清晰**：不会全局污染，只影响链接它的目标
> 4. **类型安全**：CMake 会检查目标是否存在
>
> 比传统的 `include_directories` + `link_directories` 更安全、更清晰。

---

下一章我们将深入分析 **Core 模块**，学习单例模式和 Pimpl 惯用法的实现。

[← 上一章：01-项目架构概览.md](01-项目架构概览.md) | [下一章：03-Core 模块详解.md →](03-Core模块详解.md)
