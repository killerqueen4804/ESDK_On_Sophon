# video 模块 - 视频处理
# - H264Decoder: FFmpeg H.264 解码器

# 📌 源文件
set(VIDEO_SOURCES
    H264Decoder.cpp
)

# 📌 创建库
add_library(video STATIC ${VIDEO_SOURCES})

# 📌 包含目录
target_include_directories(video
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 📌 链接库
target_link_libraries(video
    PUBLIC
        core                # Logger
    PRIVATE
        ThirdParty::FFmpeg  # FFmpeg (avcodec, avformat, swscale)
        ThirdParty::OpenCV  # OpenCV
)

# 📌 编译选项
target_compile_features(video PRIVATE cxx_std_17)
target_compile_options(video PRIVATE
    -Wall
    -Wextra
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

message(STATUS "✅ video 模块配置完成")
