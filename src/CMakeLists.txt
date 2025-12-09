# ============================================================================
# src/CMakeLists.txt - 源代码总配置文件
# ============================================================================
# 功能：管理所有源代码模块的编译，组织模块间的依赖关系
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 添加各模块子目录
# ----------------------------------------------------------------------------
# 按依赖层次顺序添加：core → mqtt/vision/storage/utils → device → main

# 第1层：核心基础模块（无依赖）
add_subdirectory(core)
# 注意: types/ 文件夹已删除（只剩 DetectionResult.h 在 include/ 中）

# 第2层：业务模块（依赖core）
add_subdirectory(mqtt)
add_subdirectory(vision)
add_subdirectory(video)  # 视频处理模块（H.264 解码器，依赖core、FFmpeg）
add_subdirectory(storage)
add_subdirectory(utils)
add_subdirectory(rtmp)   # RTMP 推流模块（依赖core、FFmpeg）
add_subdirectory(task)  # 任务管理模块（依赖core）

# 第3层：设备控制模块（依赖mqtt、core）
add_subdirectory(device)

# ----------------------------------------------------------------------------
# 2. 创建主可执行文件
# ----------------------------------------------------------------------------
# 【重要】添加 Edge-SDK 示例源文件到主程序编译
# EDGE_SDK_SAMPLE_SRC 在顶层 CMakeLists.txt 中定义，包含：
# - Edge-SDK/examples/init/pre_init.cc（ESDKInit/ESDKDeInit 实现）
# - Edge-SDK/examples/init/key_store_default.cc（RSA 密钥管理）
add_executable(ESDK_Sophon
    main.cpp
    Application.cpp  # 顶层应用协调器
    ${EDGE_SDK_SAMPLE_SRC}  # Edge-SDK 初始化代码
)

# ----------------------------------------------------------------------------
# 3. 链接所有模块
# ----------------------------------------------------------------------------
# PRIVATE：这些库只在编译主程序时需要，不会传递给其他依赖者
target_link_libraries(ESDK_Sophon
    PRIVATE
        # 项目模块
        core          # 核心模块：日志、配置
        mqtt          # MQTT通信模块
        vision        # 视觉处理模块
        video         # 视频处理模块：H.264 解码器
        device        # 设备控制模块
        storage       # 数据存储模块
        utils         # 工具模块
        rtmp          # RTMP推流模块
        task          # 任务管理模块
        
        # Edge-SDK 及其依赖
        ThirdParty::ESDK         # DJI Edge-SDK（已在 third_party/CMakeLists.txt 配置）
        ThirdParty::libssh2      # SSH2 库（Edge-SDK 依赖）
        ThirdParty::libcrypto    # OpenSSL 加密库（Edge-SDK 依赖）
)

# ----------------------------------------------------------------------------
# 4. 设置可执行文件输出路径
# ----------------------------------------------------------------------------
# 输出到 build/bin/ 目录
set_target_properties(ESDK_Sophon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ----------------------------------------------------------------------------
# 5. 安装配置（可选）
# ----------------------------------------------------------------------------
# 安装可执行文件到系统目录
install(TARGETS ESDK_Sophon
    RUNTIME DESTINATION bin
)

# ============================================================================
# 📚 知识点：CMake模块化组织
# ============================================================================
# 1. add_subdirectory()：添加子目录，每个子目录都有自己的CMakeLists.txt
# 2. 依赖顺序：先添加被依赖的模块（core），再添加依赖者（mqtt、vision等）
# 3. target_link_libraries()：链接模块，自动处理头文件包含路径
# 4. PRIVATE：只在当前目标使用，不传递给依赖者
# 5. set_target_properties()：设置目标属性，如输出路径
# ============================================================================
