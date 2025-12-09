# ============================================================================
# src/vision/stream/CMakeLists.txt - 视频流处理子模块
# ============================================================================
# 功能：视频流解码、处理、RTMP推流
# 依赖：core、FFmpeg、GStreamer、OpenCV
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 定义源文件列表
# ----------------------------------------------------------------------------
set(STREAM_SOURCES
    StreamDecoder.cpp
    StreamProcessor.cpp
    RtmpStreamer.cpp
)

# ----------------------------------------------------------------------------
# 2. 创建静态库
# ----------------------------------------------------------------------------
add_library(stream STATIC
    ${STREAM_SOURCES}
)

# ----------------------------------------------------------------------------
# 3. 设置头文件包含路径
# ----------------------------------------------------------------------------
target_include_directories(stream
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ----------------------------------------------------------------------------
# 4. 链接依赖库
# ----------------------------------------------------------------------------
target_link_libraries(stream
    PUBLIC
        core
    PRIVATE
        ThirdParty::FFmpeg         # 视频解码
        ThirdParty::GStreamer      # 流媒体处理
        ThirdParty::OpenCV         # 图像转换
)

# ----------------------------------------------------------------------------
# 5. 设置编译选项
# ----------------------------------------------------------------------------
target_compile_options(stream
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ----------------------------------------------------------------------------
# 6. 设置库输出路径
# ----------------------------------------------------------------------------
set_target_properties(stream PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ============================================================================
# 📚 知识点：为什么stream模块单独存在？
# ============================================================================
#
# stream模块的特殊性：
#
# 1. 依赖不同：
#    - detector/segmentation：依赖FastDeploy（AI推理）
#    - stream：依赖FFmpeg/GStreamer（流媒体处理）
#    - 两者完全不同的技术栈
#
# 2. 职责不同：
#    - detector/segmentation：处理单帧图像
#    - stream：处理视频流（连续帧、时序信息）
#
# 3. 性能要求不同：
#    - detector：吞吐量（每秒处理多少帧）
#    - stream：实时性（延迟要低）
#
# 4. 可选性：
#    - 某些应用可能不需要推流功能
#    - 模块独立便于裁剪
#
# stream模块的典型使用流程：
#
#   RTSP/RTMP输入 → StreamDecoder → StreamProcessor → RtmpStreamer → 输出
#   （无人机视频）   （解码成帧）     （检测/分割）      （编码推流）   （到服务器）
#
# 代码示例：
#   ```cpp
#   // 创建流处理管道
#   StreamDecoder decoder;
#   detector.initialize("rtsp://drone/live");
#
#   StreamProcessor processor;
#   processor.setDetector(yolov10);  // 使用检测器
#
#   RtmpStreamer streamer;
#   streamer.initialize("rtmp://server/live");
#
#   // 处理流
#   while (true) {
#       cv::Mat frame = decoder.decode();
#       cv::Mat processed = processor.process(frame);
#       streamer.push(processed);
#   }
#   ```
#
# 面试问题：
# Q: 视频流处理和图像处理有什么区别？
# A: 1. 时序信息：视频有前后帧的关系，可以利用时序做跟踪
#    2. 实时性要求：视频需要保证帧率，延迟敏感
#    3. 内存管理：视频需要缓冲区管理，避免内存溢出
#    4. 编解码：视频需要处理编解码，图像通常是解码后的
#
# Q: FFmpeg和GStreamer如何选择？
# A: - FFmpeg：功能强大，API相对底层，性能好
#    - GStreamer：插件架构，易于扩展，但学习曲线陡
#    - 本项目两者都支持，根据具体场景选择
# ============================================================================
