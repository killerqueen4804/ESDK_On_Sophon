# ==============================================================================
# DJI ESDK On Sophon - 主配置文件
# ==============================================================================
# 项目：DJI边缘计算智能系统
# 平台：Sophon BM1684X (ARM64)
# C++标准：C++17
# ==============================================================================

# 要求CMake 3.10+以上版本（与旧项目保持一致）
cmake_minimum_required(VERSION 3.10)

# ==============================================================================
# 交叉编译配置（必须在project()之前）
# ==============================================================================
# 目标系统：Linux ARM64
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 交叉编译器路径（Docker容器stream_lzy中的标准路径）
set(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)
set(CMAKE_ASM_COMPILER /usr/bin/aarch64-linux-gnu-g++)

# 【教学说明】为什么在CMakeLists.txt中直接配置交叉编译？
# 方案对比：
# 1. toolchain文件方式: cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake
#    - 优点：分离配置，可复用，CMake推荐方式
#    - 缺点：每次都要指定，容易忘记
# 
# 2. 直接写在CMakeLists.txt（本项目采用）
#    - 优点：简单直接，不需要额外参数，团队使用一致
#    - 缺点：耦合度高，切换平台需要修改文件
# 
# 本项目选择方案2的原因：
# - Docker容器专门用于交叉编译，不需要切换平台
# - 团队所有人使用相同环境，简化操作
# - 与旧项目保持一致，降低学习成本

message(STATUS "========================================")
message(STATUS "🔧 交叉编译配置")
message(STATUS "目标系统: ${CMAKE_SYSTEM_NAME}")
message(STATUS "目标架构: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "C编译器: ${CMAKE_C_COMPILER}")
message(STATUS "C++编译器: ${CMAKE_CXX_COMPILER}")
message(STATUS "========================================")

# ⚠️ 临时方案: 抑制第三方库的运行时依赖警告
# OpenCV/GStreamer 的依赖库在 SE7 系统中存在,这些警告可以安全忽略
# 使用 --unresolved-symbols=ignore-in-shared-libs 忽略共享库中的未定义符号
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--unresolved-symbols=ignore-in-shared-libs")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--unresolved-symbols=ignore-in-shared-libs")

# 项目信息
project(ESDK_Sophon 
    VERSION 1.0.0
    DESCRIPTION "DJI ESDK智能边缘计算系统"
    LANGUAGES CXX
)

# 打印项目信息
message(STATUS "========================================")
message(STATUS "⚠️ 链接器配置: 忽略共享库中的未定义符号 (运行时解析)")
message(STATUS "项目：${PROJECT_NAME}")
message(STATUS "版本：${PROJECT_VERSION}")
message(STATUS "描述：${PROJECT_DESCRIPTION}")
message(STATUS "========================================")

# Qt相关设置（禁用 AUTOMOC/AUTOUIC/AUTORCC - 我们不使用 Qt Widgets）
# 📌 修复: 禁用 Qt 自动代码生成,避免 CMake 警告
# 我们只使用 Qt5Network 和 Qt5Mqtt,不需要 MOC/UIC/RCC
set(CMAKE_AUTOUIC OFF)  # 禁用 .ui 文件自动处理
set(CMAKE_AUTOMOC OFF)  # 禁用 Qt MOC (Meta-Object Compiler)
set(CMAKE_AUTORCC OFF)  # 禁用 .qrc 资源文件自动处理

# ==============================================================================
# C++标准和编译选项
# ==============================================================================
# 使用C++17（支持auto、lambda、智能指针等现代特性）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # 强制要求C++17，不支持则报错
set(CMAKE_CXX_EXTENSIONS OFF)        # 禁用GNU扩展，保证代码可移植性

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O3 -pthread")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
set(COMMON_CXX_FLAGS "-std=c++17 -pthread")

# 定义宏
add_definitions(-D_GNU_SOURCE)
add_definitions(-DOPEN_CV_INSTALLED)
add_definitions(-DOPEN_CV_VERSION_4)

# 【知识点】编译选项说明
# - -std=c++17: 使用C++17标准
# - -O3: 最高级别优化（Release模式）
# - -pthread: 启用POSIX线程支持
# - -D_GNU_SOURCE: 启用GNU扩展功能
# - OPEN_CV_INSTALLED: 标识OpenCV已安装
# - OPEN_CV_VERSION_4: 标识使用OpenCV 4.x

# ARM64特定的include和link路径
include_directories(/usr/local/arm64-jpeg/include)
link_directories(/usr/local/arm64-jpeg/lib)

# ==============================================================================
# 输出目录设置
# ==============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # 可执行文件
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 动态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库

message(STATUS "可执行文件输出: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# ==============================================================================
# 编译选项配置
# ==============================================================================
# 设置默认构建类型为Release（如果未指定）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "未指定构建类型，默认使用 Release")
endif()

# 通用编译选项（所有构建类型）
add_compile_options(
    -Wall          # 开启所有警告
    -Wextra        # 开启额外警告
    -pthread       # 支持多线程
)

# Debug模式：包含调试信息，无优化
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")  # Debug模式定义DEBUG宏

# Release模式：最高优化，无调试信息
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++标准: C++${CMAKE_CXX_STANDARD}")

# ==============================================================================
# 第三方库配置
# ==============================================================================
message(STATUS "配置第三方库...")

# 添加第三方库目录（会读取 third_party/CMakeLists.txt）
add_subdirectory(third_party)

# ==============================================================================
# DJI Edge-SDK 配置（集成到 third_party）
# ==============================================================================
message(STATUS "配置 DJI Edge-SDK（从 third_party）...")

# Edge-SDK 路径（在 third_party 中）
set(EDGE_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/esdk)
# Edge-SDK 头文件路径
include_directories(
    ${EDGE_SDK_ROOT}/include          # Edge-SDK 公共头文件
    ${EDGE_SDK_ROOT}/examples         # 示例代码头文件（包含 init/app_info.h 等）
)

# Edge-SDK 库文件路径（根据架构选择）
# CMAKE_SYSTEM_PROCESSOR 是目标架构（aarch64）
link_directories(${EDGE_SDK_ROOT}/lib/${CMAKE_SYSTEM_PROCESSOR})

# ⚠️ 注意：不要在这里使用 link_libraries()！
# 原因：link_libraries() 会全局应用到所有目标（包括测试程序），
#      但测试程序可能不需要 Edge-SDK，这会导致不必要的链接错误。
# 正确做法：在 src/CMakeLists.txt 中只对主程序链接 Edge-SDK

# Edge-SDK 示例源文件（直接编译到我们的项目中）
set(EDGE_SDK_SAMPLE_SRC
    ${EDGE_SDK_ROOT}/examples/init/pre_init.cc           # ESDK 初始化实现
    ${EDGE_SDK_ROOT}/examples/init/key_store_default.cc  # RSA 密钥存储实现
)

message(STATUS "✅ Edge-SDK 已集成")
message(STATUS "   - 根目录: ${EDGE_SDK_ROOT}")
message(STATUS "   - 库路径: ${EDGE_SDK_ROOT}/lib/${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "   - 示例源文件: pre_init.cc, key_store_default.cc")
message(STATUS "========================================")

# ==============================================================================
# 现代CMake方式
# ==============================================================================

# 【教学说明】为什么直接编译 Edge-SDK 的示例代码？
# 
# 方案对比：
# 1. 链接 Edge-SDK 的示例静态库（如果有）
#    - 优点：干净，不重复编译
#    - 缺点：Edge-SDK 没有提供编译好的示例库
# 
# 2. 将示例代码复制到我们的项目中
#    - 优点：完全控制，可以修改
#    - 缺点：代码重复，维护困难
# 
# 3. 直接编译 Edge-SDK 的示例源文件（本项目采用）
#    - 优点：不重复代码，自动获取更新，简单直接
#    - 缺点：依赖 Edge-SDK 目录结构
# 
# 本项目选择方案3的原因：
# - Edge-SDK 的示例代码很稳定，基本不会改变
# - 我们只需要 pre_init.cc 和 key_store_default.cc
# - 与旧项目保持一致，经过验证

# 【现代CMake方式】
# 不再使用 include_directories 和 link_directories（全局生效）
# 而是使用 target_link_libraries 链接IMPORTED目标
# 好处：
# 1. 自动获得头文件路径（INTERFACE_INCLUDE_DIRECTORIES）
# 2. 自动处理依赖关系（链接Qt5Mqtt会自动链接Qt5Network和Qt5Core）
# 3. 作用域清晰，不会影响其他目标

# ==============================================================================
# 源代码模块配置
# ==============================================================================
message(STATUS "配置源代码模块...")

# 添加src目录（包含所有模块）
# src/CMakeLists.txt会：
# 1. 添加core、mqtt、vision、device、storage、utils等子模块
# 2. 创建主可执行文件
# 3. 链接所有模块库
add_subdirectory(src)

# 添加tests目录（测试程序）
# ✅ Day 4 简化验证测试已添加 (test_task_basic.cpp)
add_subdirectory(tests)

# 【知识点】PRIVATE vs PUBLIC vs INTERFACE
# PRIVATE: 只有当前目标需要,不传递给链接它的目标
# PUBLIC: 当前目标需要,且传递给链接它的目标
# INTERFACE: 当前目标不需要,但传递给链接它的目标
# 
# 对于可执行文件，通常使用PRIVATE（因为没有其他目标会链接可执行文件）

message(STATUS "========================================")
message(STATUS "配置完成！")
message(STATUS "========================================")

# ==============================================================================
# 构建说明
# ==============================================================================
# 
# 🔧 本地编译（Windows PowerShell）：
# 
# 1. 创建构建目录
#    mkdir build
#    cd build
#
# 2. 配置项目（Debug模式）
#    cmake .. -DCMAKE_BUILD_TYPE=Debug
#
# 3. 编译（使用8个线程）
#    cmake --build . --parallel 8
#
# 4. 运行
#    .\bin\ESDK_Sophon.exe
#
# Release模式：
#    cmake .. -DCMAKE_BUILD_TYPE=Release
#
# ---
#
# 🐳 Docker交叉编译（生产环境）：
# 
# 1. 进入Docker容器
#    docker exec -it stream_lzy /bin/bash
#
# 2. 创建构建目录
#    mkdir -p build && cd build
#
# 3. 配置项目（使用ARM64工具链）
#    cmake .. \
#      -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake \
#      -DCMAKE_BUILD_TYPE=Release
#
# 4. 编译
#    cmake --build . --parallel 8
#
# 5. 验证架构
#    file bin/ESDK_Sophon
#    # 应输出：ELF 64-bit LSB executable, ARM aarch64
#
# 6. 部署到SE7微服务器
#    scp bin/ESDK_Sophon user@se7:/opt/esdk/
#
# 详细说明请参考：docs/Docker交叉编译指南.md
#
# ==============================================================================