# ============================================================================
# src/mqtt/CMakeLists.txt - MQTT通信模块配置
# ============================================================================
# 功能：处理MQTT消息的收发、主题订阅、观察者模式消息分发
# 依赖：core（Logger、Config）、paho.mqtt.c（MQTT协议）
# 技术：Pimpl模式、单例模式、观察者模式、线程安全
# ============================================================================
# 更新日期：2025-10-26
# 变更：从QtMqtt迁移到paho.mqtt.c，采用标准C++实现
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 定义源文件列表
# ----------------------------------------------------------------------------
set(MQTT_SOURCES
    MqttClient.cpp
    MqttHandler.cpp  # 新增：MQTT消息处理器
)

set(MQTT_HEADERS
    ${CMAKE_SOURCE_DIR}/include/esdk_sophon/mqtt/MqttClient.h
    ${CMAKE_SOURCE_DIR}/include/esdk_sophon/mqtt/IMqttMessageObserver.h
    ${CMAKE_SOURCE_DIR}/include/esdk_sophon/mqtt/MqttHandler.h  # 新增
)

# ----------------------------------------------------------------------------
# 2. 创建静态库
# ----------------------------------------------------------------------------
add_library(mqtt STATIC
    ${MQTT_SOURCES}
    ${MQTT_HEADERS}  # 添加头文件到项目，便于IDE管理
)

# 设置库的别名（推荐的现代CMake实践）
add_library(esdk_sophon::mqtt ALIAS mqtt)

# ----------------------------------------------------------------------------
# 3. 设置头文件包含路径
# ----------------------------------------------------------------------------
# 【知识点】target_include_directories 详解
#
# 作用：告诉编译器去哪里找头文件（类似于 #include 的搜索路径）
#
# PUBLIC vs PRIVATE：
# - PUBLIC：  这个路径会传递给链接 mqtt 的其他模块
# - PRIVATE： 只有 mqtt 自己编译时用，不传递给别人
#
# 生成器表达式（$<...>）：
# - BUILD_INTERFACE：  编译时使用（源码树中的绝对路径）
# - INSTALL_INTERFACE：安装后使用（安装目录的相对路径）
#
# 示例：
# PUBLIC  路径 → device 模块链接 mqtt 后，自动获得 include/ 路径
# PRIVATE 路径 → device 模块看不到，只有 mqtt 内部能用
#
target_include_directories(mqtt
    PUBLIC
        # 编译时：F:/ESDK_On_Sophon/include
        # 作用：其他模块可以 #include "esdk_sophon/mqtt/MqttClient.h"
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        
        # 安装后：/usr/local/include
        # 作用：库安装到系统后，其他项目能找到头文件
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # F:/ESDK_On_Sophon/src/mqtt
        # 作用：mqtt模块内部的私有头文件（如果有的话）
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ----------------------------------------------------------------------------
# 4. 链接依赖库
# ----------------------------------------------------------------------------
target_link_libraries(mqtt
    PUBLIC
        esdk_sophon::core          # PUBLIC：mqtt公共接口使用了Logger
        esdk_sophon::task          # PUBLIC：MqttHandler使用TaskManager
        # 注意：types库已删除，DetectionResult.h现在直接include即可
    PRIVATE
        ThirdParty::PahoMqttC      # PRIVATE：paho库只在内部使用
)

# ----------------------------------------------------------------------------
# 5. 设置编译特性和选项
# ----------------------------------------------------------------------------
# C++17标准（需要std::filesystem等特性）
target_compile_features(mqtt PUBLIC cxx_std_17)

# 编译警告
target_compile_options(mqtt
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ----------------------------------------------------------------------------
# 6. 设置库输出路径
# ----------------------------------------------------------------------------
# 【知识点】set_target_properties 详解
#
# 作用：设置编译输出文件的位置和名称
#
# ARCHIVE_OUTPUT_DIRECTORY：
# - 静态库（.a 文件）的输出目录
# - ${CMAKE_BINARY_DIR}/lib → build/lib/
# - 好处：所有库文件集中管理，不分散在各模块目录
#
# OUTPUT_NAME：
# - 重命名输出文件
# - 默认：libmqtt.a（根据 add_library(mqtt) 自动生成）
# - 自定义：libesdk_sophon_mqtt.a（加上项目命名空间）
# - 好处：避免与其他项目的同名库冲突
#
# 三种库类型对应的属性：
# - ARCHIVE_OUTPUT_DIRECTORY：静态库 (.a)
# - LIBRARY_OUTPUT_DIRECTORY：动态库 (.so)
# - RUNTIME_OUTPUT_DIRECTORY：可执行文件
#
# 最终效果：
# build/lib/libesdk_sophon_mqtt.a  ← 清晰、集中、不冲突
#
set_target_properties(mqtt PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"  # 输出到 build/lib/
    OUTPUT_NAME "esdk_sophon_mqtt"                      # 文件名：libesdk_sophon_mqtt.a
)

# ----------------------------------------------------------------------------
# 7. 调试信息输出
# ----------------------------------------------------------------------------
message(STATUS "[MQTT模块] 配置完成")
message(STATUS "  - 源文件: ${MQTT_SOURCES}")
message(STATUS "  - 公共头文件: ${MQTT_HEADERS}")
message(STATUS "  - 依赖库: esdk_sophon::core (PUBLIC), ThirdParty::PahoMqttC (PRIVATE)")
message(STATUS "  - C++标准: C++17")

# ============================================================================
#  知识点：PUBLIC vs PRIVATE 链接
# ============================================================================
# 
# 为什么 core 用 PUBLIC，paho.mqtt.c 用 PRIVATE？
#
# 1. core 用 PUBLIC：
#    - MqttClient.h 中使用了 Logger（Logger::getInstance()）
#    - 依赖 mqtt 的模块也会调用 Logger
#    - 依赖传递是合理的（用 mqtt 就需要 core）
#
# 2. paho.mqtt.c 用 PRIVATE：
#    - paho 的类型（MQTTClient）只在 MqttClient.cpp 内部使用
#    - 使用了 Pimpl 模式隐藏实现细节
#    - 外部模块看不到 paho 的类型，不需要链接
#
# 示例：
# `cpp
# // MqttClient.h（公共接口）
# class MqttClient {
# public:
#     // 使用了 Logger  core 需要 PUBLIC
#     bool initialize();  // 内部调用 Logger::getInstance()
#     
# private:
#     class Impl;  // Pimpl：隐藏 paho 细节
#     std::unique_ptr<Impl> pImpl_;  // paho  PRIVATE
# };
# `
#
# 面试要点：
# Q: 如何决定依赖库用 PUBLIC 还是 PRIVATE？
# A: 1. 如果依赖库的类型出现在公共接口（.h）中，用 PUBLIC
#    2. 如果只在实现文件（.cpp）中使用，用 PRIVATE
#    3. Pimpl 模式可以把 PUBLIC 依赖变成 PRIVATE
# ============================================================================

# ============================================================================
#  知识点：为什么使用 paho.mqtt.c 而不是 QtMqtt？
# ============================================================================
#
# 技术选型对比：
#
# | 维度 | paho.mqtt.c | QtMqtt |
# |------|-------------|--------|
# | 二进制大小 | ~100KB | ~6.5MB |
# | 依赖 | libc（系统自带） | Qt5Core, Qt5Network |
# | 许可证 | EPL/EDL（宽松） | GPL v3（传染性） |
# | 交叉编译 | 简单（标准C库） | 复杂（需要Qt全家桶） |
# | 架构一致性 | 标准C++，无Qt依赖 | 引入Qt生态 |
#
# 选择 paho.mqtt.c 的理由：
# 1. 嵌入式系统优先考虑二进制大小（100KB vs 6.5MB）
# 2. 减少依赖，避免引入整个Qt框架
# 3. 许可证更宽松，商业友好
# 4. 交叉编译更简单
# 5. 与项目其他模块风格统一（标准C++）
#
# 封装策略：
# - 使用 Pimpl 模式隐藏 paho.mqtt.c 的 C API
# - 提供现代 C++ 接口（RAII、智能指针、异常安全）
# - 观察者模式实现消息分发
# ============================================================================

# ============================================================================
#  知识点：ALIAS 库的作用
# ============================================================================
#
# add_library(esdk_sophon::mqtt ALIAS mqtt)
#
# 作用：
# 1. 命名空间：清晰表达库的归属
# 2. 一致性：与外部库命名风格统一（如 ThirdParty::PahoMqttC）
# 3. 可读性：target_link_libraries(app PRIVATE esdk_sophon::mqtt)
# 4. 防冲突：避免与其他项目的 mqtt 库冲突
#
# 使用示例：
# `cmake
# # 在 tests/CMakeLists.txt 中
# target_link_libraries(test_mqtt
#     PRIVATE
#         esdk_sophon::mqtt   # 使用别名
#         esdk_sophon::core
# )
# `
# ============================================================================

# ============================================================================
# 📚 知识点总结：CMake高级特性速查表
# ============================================================================
#
# 1. 生成器表达式（Generator Expression）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 格式：$<条件:值>
#
# 常用表达式：
# - $<BUILD_INTERFACE:路径>     编译时使用（源码树中的路径）
# - $<INSTALL_INTERFACE:路径>   安装后使用（安装目录的路径）
# - $<$<CONFIG:Debug>:标志>     Debug模式下的编译标志
# - $<$<CXX_COMPILER_ID:GNU>:>  GNU编译器特定设置
#
# 为什么需要？
# - 编译时和安装后的目录结构不同
# - 不同编译器需要不同的编译选项
# - Debug/Release 需要不同的配置
#
# 面试要点：
# Q: 什么是CMake生成器表达式？
# A: 是在生成构建文件时计算的条件表达式，可以根据不同条件
#    （构建类型、编译器、平台）生成不同的配置。
#
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# 2. target_include_directories 作用域
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# PUBLIC：   传递给依赖者 + 自己使用
# PRIVATE：  只有自己使用，不传递
# INTERFACE：只传递给依赖者，自己不用（纯头文件库）
#
# 示例：
# ```cmake
# # mqtt 模块
# target_include_directories(mqtt
#     PUBLIC  ${CMAKE_SOURCE_DIR}/include    # device链接mqtt后自动获得
#     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}    # 只有mqtt能用
# )
#
# # device 模块
# target_link_libraries(device PRIVATE mqtt)
# # ↑ device 自动获得 mqtt 的 PUBLIC 路径，可以 #include "esdk_sophon/mqtt/..."
# ```
#
# 记忆技巧：
# PUBLIC  = 公开的，大家都能用
# PRIVATE = 私有的，只有我能用
# INTERFACE = 接口，我不用但传给别人
#
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# 3. 目录变量对照表
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# ${CMAKE_SOURCE_DIR}         项目顶层源码目录（CMakeLists.txt所在）
#   示例：F:/ESDK_On_Sophon/
#
# ${CMAKE_BINARY_DIR}         项目顶层构建目录（build/）
#   示例：F:/ESDK_On_Sophon/build/
#
# ${CMAKE_CURRENT_SOURCE_DIR} 当前CMakeLists.txt所在目录
#   示例：F:/ESDK_On_Sophon/src/mqtt/
#
# ${CMAKE_CURRENT_BINARY_DIR} 当前模块的构建目录
#   示例：F:/ESDK_On_Sophon/build/src/mqtt/
#
# ${PROJECT_SOURCE_DIR}       project()命令所在目录（通常=SOURCE_DIR）
# ${PROJECT_BINARY_DIR}       project()对应的构建目录（通常=BINARY_DIR）
#
# 记忆技巧：
# SOURCE = 源码位置
# BINARY = 构建位置（build/）
# CURRENT = 当前文件
# 无CURRENT = 顶层
#
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# 4. 输出目录属性对照表
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# 库类型                    属性名称                      输出文件
# ──────────────────────────────────────────────────────────────
# 静态库（STATIC）          ARCHIVE_OUTPUT_DIRECTORY      libxxx.a
# 动态库（SHARED）          LIBRARY_OUTPUT_DIRECTORY      libxxx.so
# 可执行文件（EXECUTABLE）  RUNTIME_OUTPUT_DIRECTORY      xxx
#
# Windows特例：
# - .dll 文件属于 RUNTIME（不是LIBRARY）
# - .lib 导入库属于 ARCHIVE
#
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# 5. 现代CMake vs 传统CMake
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# 传统方式（不推荐）：
# include_directories(/path/to/include)       # 全局生效，影响所有目标
# link_directories(/path/to/lib)              # 全局生效，容易冲突
# link_libraries(xxx)                         # 全局链接，难以控制
#
# 现代方式（推荐）：
# target_include_directories(mqtt ...)        # 只影响mqtt目标
# target_link_libraries(mqtt ...)             # 明确依赖关系
# IMPORTED目标 + INTERFACE属性                # 自动传递配置
#
# 优势：
# 1. 作用域清晰（不会影响其他目标）
# 2. 自动依赖传递（链接A自动获得A的依赖）
# 3. 易于维护（修改不影响全局）
#
# 面试要点：
# Q: 现代CMake和传统CMake的区别？
# A: 现代CMake强调target-based，每个设置都针对具体目标，
#    通过target_*命令精确控制，避免全局污染。
#    传统CMake使用全局命令，容易产生意外的依赖关系。
#
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# ============================================================================
