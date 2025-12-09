# ByteTrack 追踪库
# 基于 sophon-demo ByteTrack 实现，使用 OpenCV 版本

cmake_minimum_required(VERSION 3.10)
project(bytetrack)

# 注意：OpenCV 已在父级 third_party/CMakeLists.txt 中配置为 ThirdParty::OpenCV
# 这里不需要 find_package(OpenCV)，直接链接即可

# 源文件
set(BYTETRACK_SOURCES
    src/kalmanfilter.cpp
    src/strack.cpp
    src/lapjv.cpp
    src/BYTETracker.cpp
)

# 创建静态库
add_library(bytetrack STATIC ${BYTETRACK_SOURCES})

# 头文件路径
target_include_directories(bytetrack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接 OpenCV（使用项目预定义的 IMPORTED 目标）
target_link_libraries(bytetrack PUBLIC
    ThirdParty::OpenCV
)

# C++17 标准
target_compile_features(bytetrack PUBLIC cxx_std_17)

# 编译选项
target_compile_options(bytetrack PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)
