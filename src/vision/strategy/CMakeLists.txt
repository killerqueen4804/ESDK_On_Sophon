# ============================================================================
# src/vision/strategy/CMakeLists.txt - 变焦拍照策略模块
# ============================================================================
# 功能：实现智能变焦拍照策略，判断何时触发变焦拍照
# 依赖：tracker, OpenCV
# ============================================================================

# 源文件
set(STRATEGY_SOURCES
    SmartZoomCaptureStrategy.cpp
)

# 创建静态库
add_library(strategy STATIC ${STRATEGY_SOURCES})

# 头文件路径
target_include_directories(strategy
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

# 链接依赖
target_link_libraries(strategy
    PUBLIC
        tracker                 # TrackedObject 类型
        ThirdParty::OpenCV      # cv::Rect, cv::Size 等
)

# C++17 标准
target_compile_features(strategy PUBLIC cxx_std_17)

message(STATUS "配置 strategy 库: SmartZoomCaptureStrategy 变焦拍照策略")
