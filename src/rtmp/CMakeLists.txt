# ==============================================================================
# RTMP 推流模块 CMakeLists.txt
# ==============================================================================
#
# 本模块提供 RTMP 推流功能，使用 GStreamer 将 H.264 视频流推送到 RTMP 服务器。
#
# 依赖：
# - GStreamer (gst, gst-app)
# - core 模块 (Logger)
#
# 作者: ESDK Sophon Team
# 日期: 2025-11-11 (技术选型：GStreamer 替代 FFmpeg)
# ==============================================================================

message(STATUS "配置 rtmp 模块...")

# 创建静态库
add_library(rtmp STATIC
    RtmpStreamer.cpp
)

# 设置别名（方便引用）
add_library(esdk_sophon::rtmp ALIAS rtmp)

# 包含目录（PUBLIC：使用者也能看到）
target_include_directories(rtmp
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 链接库
target_link_libraries(rtmp
    PUBLIC
        # GStreamer 库（推流核心）
        ThirdParty::GStreamer
        
        # OpenCV 库（cv::Mat 图像处理）
        ThirdParty::OpenCV
        
        # core 模块（Logger）
        core
    PRIVATE
        # 无私有依赖
)

# 编译选项
target_compile_features(rtmp PUBLIC cxx_std_17)

message(STATUS "  ✓ rtmp 模块已配置 (使用 GStreamer)")

# ==============================================================================
# CMake 链接库的 PUBLIC/PRIVATE/INTERFACE 说明
# ==============================================================================
# PUBLIC: 当前目标需要，使用者也需要（依赖传递）
# PRIVATE: 当前目标需要，使用者不需要（依赖不传递）
# INTERFACE: 当前目标不需要，使用者需要（纯头文件库）
#
# 本模块选择 PUBLIC 的原因：
# - RtmpStreamer.h 包含了 FFmpeg 头文件（<libavformat/avformat.h>）
# - 头文件中暴露了 AVFormatContext* 等 FFmpeg 类型
# - 使用 rtmp 模块的代码需要看到这些类型的定义
# ==============================================================================

# ==============================================================================
# 📚 知识点：CMake 链接库的 PUBLIC/PRIVATE/INTERFACE
# ==============================================================================
#
# ### 1. PUBLIC（公共依赖）
#
# **含义**: 
# - 当前目标需要这个库
# - 使用当前目标的其他目标也需要这个库
#
# **传递性**: 依赖会传递
#
# **示例**:
# ```cmake
# # rtmp 库需要 FFmpeg，使用 rtmp 的也需要 FFmpeg
# target_link_libraries(rtmp
#     PUBLIC ThirdParty::FFmpeg
# )
#
# # task 使用 rtmp，自动获得 FFmpeg
# target_link_libraries(task
#     PRIVATE rtmp  # ✅ 自动获得 FFmpeg 头文件和库
# )
# ```
#
# ### 2. PRIVATE（私有依赖）
#
# **含义**: 
# - 当前目标需要这个库
# - 使用当前目标的其他目标**不需要**这个库
#
# **传递性**: 依赖不传递
#
# **示例**:
# ```cmake
# # rtmp 内部使用某个工具库，但不暴露给外部
# target_link_libraries(rtmp
#     PRIVATE some_internal_util
# )
#
# # task 使用 rtmp，不会获得 some_internal_util
# target_link_libraries(task
#     PRIVATE rtmp  # ❌ 看不到 some_internal_util
# )
# ```
#
# ### 3. INTERFACE（接口依赖）
#
# **含义**: 
# - 当前目标**不需要**这个库
# - 使用当前目标的其他目标需要这个库
#
# **传递性**: 依赖传递，但自己不用
#
# **示例**:
# ```cmake
# # 纯头文件库（header-only）
# add_library(MyHeaderLib INTERFACE)
# target_link_libraries(MyHeaderLib
#     INTERFACE SomeDependency  # 用户需要，但库本身不编译
# )
# ```
#
# ### 4. 选择原则
#
# **何时用 PUBLIC？**
# - 头文件中暴露了依赖库的类型
# - 公共 API 返回依赖库的对象
#
# 例如:
# RtmpStreamer.h 暴露了 FFmpeg 类型（AVFormatContext*）
# 因此 FFmpeg 必须是 PUBLIC 依赖
#
# **何时用 PRIVATE？**
# - 依赖库只在 .cpp 文件中使用
# - 头文件中不暴露依赖库的类型
#
# 例如: 如果 RtmpStreamer.h 不包含 FFmpeg 头文件
# 只在 RtmpStreamer.cpp 中使用，则 FFmpeg 可以是 PRIVATE 依赖
# ==============================================================================
