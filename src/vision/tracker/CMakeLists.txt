# ============================================================================
# src/vision/tracker/CMakeLists.txt - 目标追踪模块
# ============================================================================
# 功能：封装 ByteTrack 追踪算法，提供统一的追踪接口
# 依赖：ByteTrack (third_party), OpenCV, vision_common
# ============================================================================

# 源文件
set(TRACKER_SOURCES
    ByteTracker.cpp
)

# 创建静态库
add_library(tracker STATIC ${TRACKER_SOURCES})

# 头文件路径
target_include_directories(tracker
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/bytetrack/include
)

# 链接依赖
target_link_libraries(tracker
    PUBLIC
        vision_common           # DetectionBox 等类型定义
        ThirdParty::OpenCV      # cv::Rect 等
    PRIVATE
        ThirdParty::ByteTrack   # ByteTrack 核心算法（内部使用）
)

# C++17 标准
target_compile_features(tracker PUBLIC cxx_std_17)

message(STATUS "配置 tracker 库: ByteTracker 追踪器封装")
