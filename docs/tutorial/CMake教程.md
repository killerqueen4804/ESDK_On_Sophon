# CMake 完全教程 - 从入门到精通

> 📚 本文档详细讲解 CMake 的使用，包括基础语法、高级特性和最佳实践

---

## 📑 目录

1. [CMake 基础概念](#1-cmake基础概念)
2. [最简单的 CMakeLists.txt](#2-最简单的cmakelists)
3. [项目配置](#3-项目配置)
4. [添加源文件和可执行文件](#4-添加源文件和可执行文件)
5. [头文件和库文件路径](#5-头文件和库文件路径)
6. [链接库](#6-链接库)
7. [编译选项](#7-编译选项)
8. [子目录和模块化](#8-子目录和模块化)
9. [第三方库管理](#9-第三方库管理)
10. [实战：改进现有 CMakeLists.txt](#10-实战改进现有cmakelists)

---

## 1. CMake 基础概念

### 什么是 CMake？

**CMake** = Cross-platform Make（跨平台构建工具）

```
源代码(.cpp, .h)
       ↓
  CMakeLists.txt (配置文件)
       ↓
    CMake (解析)
       ↓
  Makefile / VS工程文件 (根据平台生成)
       ↓
  Make / MSBuild (编译)
       ↓
  可执行文件 / 库文件
```

### 为什么需要 CMake？

**传统方式的问题**:

```bash
# 手动编译，太麻烦！
g++ main.cpp utils.cpp logger.cpp -o myapp -I./include -L./lib -lmylib
```

**使用 CMake**:

```cmake
# CMakeLists.txt - 简单配置
add_executable(myapp main.cpp utils.cpp logger.cpp)
```

### 核心概念

| 概念                 | 说明         | 例子                       |
| -------------------- | ------------ | -------------------------- |
| **Target（目标）**   | 要生成的东西 | 可执行文件、静态库、动态库 |
| **Source（源文件）** | .cpp/.c 文件 | main.cpp                   |
| **Header（头文件）** | .h/.hpp 文件 | utils.h                    |
| **Library（库）**    | 已编译的代码 | libopencv.so               |
| **Variable（变量）** | 存储值的容器 | ${PROJECT_NAME}            |

---

## 2. 最简单的 CMakeLists

### Hello World 示例

```cmake
# 1. 指定CMake最低版本
cmake_minimum_required(VERSION 3.15)

# 2. 定义项目名称和语言
project(HelloWorld CXX)

# 3. 创建可执行文件
add_executable(hello main.cpp)
```

**对应的 main.cpp**:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, CMake!" << std::endl;
    return 0;
}
```

**编译和运行**:

```bash
# Windows PowerShell
mkdir build
cd build
cmake ..
cmake --build .
.\bin\hello.exe

# Linux/Mac
mkdir build && cd build
cmake ..
make
./hello
```

### 知识点讲解

#### cmake_minimum_required

```cmake
cmake_minimum_required(VERSION 3.15)
```

- **作用**: 指定需要的 CMake 最低版本
- **为什么需要**: 不同版本 CMake 支持的特性不同
- **建议**: 使用 3.15+以上版本（支持现代 CMake 特性）

#### project

```cmake
project(MyProject
    VERSION 1.0.0          # 可选：版本号
    DESCRIPTION "我的项目" # 可选：描述
    LANGUAGES CXX          # 可选：使用的语言
)
```

- **作用**: 定义项目信息
- **生成的变量**:
  - `PROJECT_NAME`: 项目名称
  - `PROJECT_VERSION`: 项目版本
  - `PROJECT_SOURCE_DIR`: 源码根目录

#### add_executable

```cmake
add_executable(可执行文件名 源文件列表)
```

- **作用**: 定义一个可执行文件目标
- **示例**:

```cmake
add_executable(myapp main.cpp utils.cpp)
```

---

## 3. 项目配置

### C++标准设置

```cmake
# 设置C++标准（重要！）
set(CMAKE_CXX_STANDARD 17)           # 使用C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # 强制要求，不支持就报错
set(CMAKE_CXX_EXTENSIONS OFF)        # 禁用GNU扩展，保证可移植性
```

**面试考点**:

- Q: CMAKE_CXX_STANDARD_REQUIRED 的作用？
- A: 如果编译器不支持指定的 C++标准，CMake 会报错停止，而不是降级使用旧标准

### 构建类型

```cmake
# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
```

**四种构建类型**:

1. **Debug**: 调试版本，包含调试符号，无优化
2. **Release**: 发布版本，最高优化，无调试信息
3. **RelWithDebInfo**: 发布版本+调试信息
4. **MinSizeRel**: 最小体积优化

**使用方式**:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

### 输出目录设置

```cmake
# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 设置库文件输出目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 动态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库
```

**重要目录变量**:

- `CMAKE_SOURCE_DIR`: 顶层 CMakeLists.txt 所在目录
- `CMAKE_BINARY_DIR`: 构建目录（通常是 build/）
- `CMAKE_CURRENT_SOURCE_DIR`: 当前 CMakeLists.txt 所在目录
- `PROJECT_SOURCE_DIR`: project()命令所在目录

---

## 4. 添加源文件和可执行文件

### 方式一：列举所有文件（推荐）

```cmake
# 明确列出所有源文件
add_executable(myapp
    src/main.cpp
    src/utils.cpp
    src/logger.cpp
    src/config.cpp
)
```

**优点**:

- ✅ 清晰明确
- ✅ CMake 能正确追踪依赖
- ✅ 修改源文件后会自动重新配置

### 方式二：使用变量

```cmake
# 先定义变量
set(SOURCES
    src/main.cpp
    src/utils.cpp
    src/logger.cpp
)

# 再使用变量
add_executable(myapp ${SOURCES})
```

### 方式三：使用 file(GLOB)（不推荐）

```cmake
# 自动查找所有.cpp文件
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(myapp ${SOURCES})
```

**缺点**:

- ❌ 添加新文件后需要重新运行 cmake
- ❌ 可能包含不需要的文件
- ❌ 不利于 CMake 追踪依赖

**现有代码问题**:

```cmake
# 你的代码使用了GLOB，不是最佳实践
file(GLOB_RECURSE MODULE_SRC
    ${PROJECT_SOURCE_DIR}/src/Go2Control/*.cpp
    ${PROJECT_SOURCE_DIR}/src/LogicControl/*.cpp
    ${PROJECT_SOURCE_DIR}/src/Mqtt/*.cpp
    ${PROJECT_SOURCE_DIR}/src/main.cpp
)
```

**改进建议**: 明确列出文件，或使用子目录

---

## 5. 头文件和库文件路径

### include_directories（全局方式）

```cmake
# 添加头文件搜索路径（全局生效）
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/3rdparty/include
)
```

**特点**:

- 作用范围：之后定义的所有目标
- 优点：简单
- 缺点：可能造成命名冲突

### target_include_directories（推荐）

```cmake
# 只对特定目标生效
target_include_directories(myapp
    PRIVATE   # 只有myapp能看到
        ${PROJECT_SOURCE_DIR}/src
    PUBLIC    # myapp和链接它的目标都能看到
        ${PROJECT_SOURCE_DIR}/include
)
```

**三种可见性**:

1. **PRIVATE**: 只有当前目标能看到
2. **PUBLIC**: 当前目标和链接它的目标都能看到
3. **INTERFACE**: 当前目标看不到，但链接它的目标能看到

**示例**:

```cmake
# 创建一个库
add_library(mylib utils.cpp)
target_include_directories(mylib
    PUBLIC include/  # 库的公共接口
    PRIVATE src/     # 库的内部实现
)

# 创建可执行文件并链接库
add_executable(myapp main.cpp)
target_link_libraries(myapp mylib)
# myapp自动获得 include/ 路径（PUBLIC传递）
```

### link_directories（不推荐）

```cmake
# 添加库文件搜索路径
link_directories(
    ${PROJECT_SOURCE_DIR}/lib
    /usr/local/lib
)
```

**问题**: 只指定目录，不指定具体库，可能链接错误的库

**更好的方式**:

```cmake
# 直接指定库的完整路径
target_link_libraries(myapp
    ${PROJECT_SOURCE_DIR}/lib/libmylib.a
)
```

---

## 6. 链接库

### 链接系统库

```cmake
# 查找并链接系统库
find_package(Threads REQUIRED)
target_link_libraries(myapp Threads::Threads)

find_package(OpenCV REQUIRED)
target_link_libraries(myapp ${OpenCV_LIBS})
```

### 链接自己编译的库

```cmake
# 创建静态库
add_library(mylib STATIC utils.cpp logger.cpp)

# 创建动态库
add_library(mylib SHARED utils.cpp logger.cpp)

# 链接库
add_executable(myapp main.cpp)
target_link_libraries(myapp mylib)
```

### 链接第三方库

```cmake
# 方式1：直接指定库文件
target_link_libraries(myapp
    ${PROJECT_SOURCE_DIR}/third_party/lib/libfoo.so
)

# 方式2：使用find_package
find_package(Foo REQUIRED)
target_link_libraries(myapp Foo::Foo)

# 方式3：使用IMPORTED库
add_library(foo SHARED IMPORTED)
set_target_properties(foo PROPERTIES
    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/third_party/lib/libfoo.so"
)
target_link_libraries(myapp foo)
```

**你的代码**:

```cmake
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Widgets)
target_link_libraries(${PROJECT_NAME} unitree_sdk2 pthread ddsc ddscxx)
```

✅ 这部分写得不错！

---

## 7. 编译选项

### add_compile_options（全局）

```cmake
# 添加编译选项（所有目标生效）
add_compile_options(
    -Wall          # 所有警告
    -Wextra        # 额外警告
    -Werror        # 警告视为错误
    -pthread       # 多线程支持
)
```

### target_compile_options（推荐）

```cmake
# 只对特定目标生效
target_compile_options(myapp PRIVATE
    -Wall
    -Wextra
)
```

### 根据构建类型设置选项

```cmake
# Debug模式
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)  # 调试符号，无优化
    add_definitions(-DDEBUG)     # 定义DEBUG宏
endif()

# Release模式
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3)     # 最高优化
    add_definitions(-DNDEBUG)    # 定义NDEBUG宏
endif()
```

**更现代的写法**:

```cmake
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
```

---

## 8. 子目录和模块化

### 为什么要模块化？

**单个大文件的问题**:

```
project/
├── CMakeLists.txt  (500行，难以维护！)
└── src/
```

**模块化结构**:

```
project/
├── CMakeLists.txt       (主配置，简洁)
├── src/
│   ├── CMakeLists.txt   (源码配置)
│   ├── core/
│   │   └── CMakeLists.txt
│   └── mqtt/
│       └── CMakeLists.txt
└── tests/
    └── CMakeLists.txt
```

### add_subdirectory

**根 CMakeLists.txt**:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyProject CXX)

# 添加子目录
add_subdirectory(src)
add_subdirectory(tests)
```

**src/CMakeLists.txt**:

```cmake
# 可以访问父目录定义的变量
add_executable(myapp
    main.cpp
    utils.cpp
)

# 继续添加子目录
add_subdirectory(core)
add_subdirectory(mqtt)
```

**src/core/CMakeLists.txt**:

```cmake
# 创建core库
add_library(core STATIC
    Logger.cpp
    Config.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
```

### 变量作用域

```cmake
# 父目录
set(MY_VAR "parent")  # 子目录可见

add_subdirectory(subdir)

# 子目录 (subdir/CMakeLists.txt)
set(MY_VAR "child")   # 不影响父目录

# 如果想影响父目录
set(MY_VAR "child" PARENT_SCOPE)
```

---

## 9. 第三方库管理

### find_package（推荐）

```cmake
# 查找Qt5
find_package(Qt5 COMPONENTS Core Widgets REQUIRED)

# 使用方式1：通过导入的目标
target_link_libraries(myapp Qt5::Core Qt5::Widgets)

# 使用方式2：通过变量
target_link_libraries(myapp ${Qt5_LIBRARIES})
target_include_directories(myapp PRIVATE ${Qt5_INCLUDE_DIRS})
```

**工作原理**:

1. CMake 在系统中查找 `Qt5Config.cmake` 或 `FindQt5.cmake`
2. 加载配置，设置变量和导入目标
3. 提供 `Qt5::Core` 等 IMPORTED 目标供使用

### FetchContent（下载源码）

```cmake
include(FetchContent)

# 下载GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)

FetchContent_MakeAvailable(googletest)

# 使用
target_link_libraries(mytest gtest_main)
```

### 手动管理第三方库

**目录结构**:

```
third_party/
├── CMakeLists.txt
├── opencv/
│   ├── include/
│   └── lib/
└── mqtt/
    ├── include/
    └── lib/
```

**third_party/CMakeLists.txt**:

```cmake
# 创建IMPORTED库
add_library(opencv SHARED IMPORTED GLOBAL)
set_target_properties(opencv PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/opencv/lib/libopencv_core.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/opencv/include"
)

add_library(mqtt STATIC IMPORTED GLOBAL)
set_target_properties(mqtt PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/mqtt/lib/libmqtt.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/mqtt/include"
)
```

**使用**:

```cmake
# 主CMakeLists.txt
add_subdirectory(third_party)

add_executable(myapp main.cpp)
target_link_libraries(myapp opencv mqtt)
# 自动获得包含路径！
```

---

## 10. 实战：改进现有 CMakeLists

### 现有代码分析

你的代码：

```cmake
cmake_minimum_required(VERSION 3.5)  # ⚠️ 版本较旧
project(unitree_go2_patrol VERSION 2.0.0)  # ⚠️ 项目名不匹配

set(CMAKE_AUTOUIC ON)  # Qt特性
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ... (中间部分)

file(GLOB_RECURSE MODULE_SRC ...)  # ⚠️ 不推荐GLOB
```

### 改进建议

#### 1. 更新 CMake 版本

```cmake
# 旧代码
cmake_minimum_required(VERSION 3.5)

# 改进
cmake_minimum_required(VERSION 3.15)  # 支持现代特性
```

#### 2. 修正项目名称

```cmake
# 旧代码
project(unitree_go2_patrol VERSION 2.0.0)

# 改进
project(ESDK_Sophon VERSION 1.0.0
    DESCRIPTION "DJI ESDK智能边缘计算系统"
    LANGUAGES CXX
)
```

#### 3. 避免使用 GLOB

```cmake
# 旧代码（不推荐）
file(GLOB_RECURSE MODULE_SRC
    ${PROJECT_SOURCE_DIR}/src/Go2Control/*.cpp
    ...
)

# 改进方式1：明确列出文件
set(SOURCES
    src/main.cpp
    src/LogicControl/LogicControl.cpp
    src/Mqtt/MqttClient.cpp
)

# 改进方式2：使用子目录
add_subdirectory(src/LogicControl)
add_subdirectory(src/Mqtt)
```

#### 4. 使用 target\_\*命令

```cmake
# 旧代码（全局）
include_directories(${HEADERS_DIR})
link_directories(...)

# 改进（针对目标）
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/3rdparty/include
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/3rdparty/lib
)
```

#### 5. 模块化 CMakeLists

**建议结构**:

```
ESDK_On_Sophon/
├── CMakeLists.txt                 # 主配置（简洁）
├── cmake/
│   └── ThirdParty.cmake          # 第三方库配置
├── src/
│   ├── CMakeLists.txt            # 源码总配置
│   ├── core/
│   │   └── CMakeLists.txt        # core模块
│   ├── mqtt/
│   │   └── CMakeLists.txt        # mqtt模块
│   └── vision/
│       └── CMakeLists.txt        # vision模块
└── tests/
    └── CMakeLists.txt
```

---

## 📊 CMake 最佳实践总结

### ✅ 推荐做法

1. **使用现代 CMake（3.15+）**

```cmake
cmake_minimum_required(VERSION 3.15)
```

2. **使用 target\_\*命令代替全局命令**

```cmake
# ✅ 好
target_include_directories(myapp PRIVATE include/)
target_link_libraries(myapp mylib)

# ❌ 避免
include_directories(include/)
link_libraries(mylib)
```

3. **明确列出源文件**

```cmake
# ✅ 好
add_executable(myapp main.cpp utils.cpp)

# ❌ 避免
file(GLOB SOURCES "*.cpp")
add_executable(myapp ${SOURCES})
```

4. **使用 find_package 管理依赖**

```cmake
# ✅ 好
find_package(Qt5 REQUIRED)
target_link_libraries(myapp Qt5::Core)
```

5. **模块化组织**

```cmake
# ✅ 好：分模块
add_subdirectory(src)
add_subdirectory(tests)
```

### ❌ 避免做法

1. 使用过旧的 CMake 版本
2. 大量使用全局命令
3. 使用 file(GLOB)自动查找文件
4. 所有配置写在一个文件里
5. 硬编码路径

---

## 🎯 面试常见问题

### Q1: CMakeLists.txt 和 Makefile 的区别？

**A**:

- CMakeLists.txt 是配置文件，描述如何构建项目
- Makefile 是具体的构建脚本，由 CMake 生成
- CMakeLists.txt 跨平台，Makefile 平台相关

### Q2: add_library 的 STATIC、SHARED、MODULE 区别？

**A**:

- STATIC：静态库(.a/.lib)，编译时链接
- SHARED：动态库(.so/.dll)，运行时加载
- MODULE：插件库，不能被链接，只能动态加载

### Q3: PUBLIC、PRIVATE、INTERFACE 的区别？

**A**:

- PRIVATE：仅当前目标使用
- PUBLIC：当前目标和链接它的目标都使用
- INTERFACE：仅链接它的目标使用，当前目标不使用

### Q4: CMAKE_SOURCE_DIR 和 PROJECT_SOURCE_DIR 的区别？

**A**:

- CMAKE_SOURCE_DIR：顶层 CMakeLists.txt 所在目录
- PROJECT_SOURCE_DIR：最近的 project()命令所在目录
- 单项目中相同，多项目中可能不同

---

## 📚 学习资源

### 在线资源

- [CMake 官方文档](https://cmake.org/documentation/)
- [Modern CMake](https://cliutils.gitlab.io/modern-cmake/)
- [CMake Examples](https://github.com/ttroy50/cmake-examples)

### 推荐书籍

- 《Professional CMake》
- 《Modern CMake for C++》

---

**最后更新**: 2025-10-23  
**难度级别**: 入门 → 进阶  
**预计学习时间**: 3-5 小时
