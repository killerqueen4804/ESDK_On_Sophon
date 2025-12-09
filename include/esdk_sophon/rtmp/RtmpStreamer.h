/**
 * @file RtmpStreamer.h
 * @brief RTMP 推流器 - 采用重新编码方案（x264enc）
 * 
 * @details
 * RtmpStreamer 负责：
 * 1. 接收解码后的 cv::Mat 图像（从 H264Decoder）
 * 2. 使用 GStreamer x264enc 重新编码为 H.264（自动生成 SPS/PPS）
 * 3. 通过 RTMP 协议推流到服务器（如 nginx-rtmp、SRS）
 * 4. 支持在画面上叠加检测框、标签等信息
 * 
 * @note
 * - 线程安全：内部使用互斥锁保护状态
 * - GStreamer 管道：appsrc ! videoconvert ! x264enc ! h264parse ! flvmux ! rtmpsink
 * - x264enc 自动生成 SPS/PPS，无需手动处理
 * - 比直推 H.264 方案更简单可靠（参考旧项目验证）
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-18 (技术选型：采用重新编码方案 A）
 * 
 * @example
 * @code
 * // 创建推流器
 * auto streamer = std::make_shared<RtmpStreamer>();
 * 
 * // 初始化（连接到 RTMP 服务器）
 * if (streamer->initialize("rtmp://127.0.0.1:1935/live/test", 1920, 1080, 30)) {
 *     // 解码 + 检测 + 推流
 *     cv::Mat frame = decoder->decode(h264Data);
 *     detector->process(frame);  // 可选：绘制检测框
 *     streamer->pushFrame(frame);  // 推送图像（自动重新编码）
 * }
 * 
 * // 停止推流
 * streamer->shutdown();
 * @endcode
 */

#ifndef ESDK_SOPHON_RTMP_STREAMER_H_
#define ESDK_SOPHON_RTMP_STREAMER_H_

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

// GStreamer C 头文件
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

// OpenCV 头文件
#include <opencv2/opencv.hpp>

#include "esdk_sophon/core/Logger.h"

namespace esdk_sophon {
namespace rtmp {

/**
 * @brief RTMP 推流器类（基于 GStreamer 重新编码方案）
 * 
 * 将 cv::Mat 图像重新编码为 H.264 并推送到 RTMP 服务器。
 * 
 * 工作流程：
 * 1. initialize() - 创建 GStreamer 管道，连接 RTMP 服务器
 * 2. pushFrame() - 持续推送 cv::Mat 图像到 appsrc
 * 3. shutdown() - 关闭管道，释放资源
 * 
 * GStreamer 管道（方案 A - 重新编码）：
 * appsrc name=source is-live=true format=time 
 *   ! videoconvert               （RGB → YUV）
 *   ! x264enc                     （编码为 H.264，自动生成 SPS/PPS）
 *   ! h264parse                   （解析 H.264 流）
 *   ! flvmux streamable=true      （封装为 FLV）
 *   ! rtmpsink location=rtmpUrl   （推流到 RTMP）
 * 
 * 核心优势：
 * - x264enc 自动生成 SPS/PPS，无需手动提取
 * - 支持在画面上叠加检测框、标签等
 * - 简单可靠，旧项目已验证
 * 
 * 线程安全：所有公共方法都是线程安全的
 */
class RtmpStreamer {
public:
    /**
     * @brief 构造函数
     */
    RtmpStreamer();

    /**
     * @brief 析构函数
     * 
     * 自动调用 shutdown() 释放资源
     */
    ~RtmpStreamer();

    // 禁用拷贝和赋值（GStreamer 管道不可拷贝）
    RtmpStreamer(const RtmpStreamer&) = delete;
    RtmpStreamer& operator=(const RtmpStreamer&) = delete;

    /**
     * @brief 初始化推流器并连接到 RTMP 服务器
     * 
     * @param rtmpUrl RTMP 服务器地址，例如 "rtmp://127.0.0.1:1935/live/test"
     * @param width 视频宽度（像素）
     * @param height 视频高度（像素）
     * @param fps 视频帧率（默认 30 FPS）
     * @return true 初始化成功，可以开始推流
     * @return false 初始化失败（URL 无效、连接失败等）
     * 
     * @note
     * - 必须在 pushFrame() 之前调用
     * - 重复调用会先关闭旧管道再创建新管道
     * - 会自动启动 GStreamer 管道到 PLAYING 状态
     * 
     * @example
     * @code
     * if (streamer->initialize("rtmp://192.168.1.100:1935/live/drone", 1920, 1080, 30)) {
     *     std::cout << "RTMP 推流器初始化成功" << std::endl;
     * } else {
     *     std::cerr << "初始化失败，请检查 RTMP 服务器地址" << std::endl;
     * }
     * @endcode
     */
    bool initialize(const std::string& rtmpUrl, int width, int height, int fps = 30);

    /**
     * @brief 推送 OpenCV 图像帧（重新编码方案）
     * 
     * @param frame OpenCV Mat 对象（BGR 格式）
     * @return true 推送成功
     * @return false 推送失败（未初始化、管道错误等）
     * 
     * @note
     * - 采用重新编码方案（x264enc），自动生成 SPS/PPS
     * - 内部自动转换 BGR → RGB，并通过 GStreamer 编码为 H.264
     * - 支持在图像上叠加检测框、标签等信息
     * 
     * @warning
     * - 需要重新编码，有 CPU 开销（但 BM1684X 算力足够）
     * - frame 必须非空且尺寸与 initialize() 的参数一致
     * 
     * @example
     * @code
     * // 解码 + 检测 + 推流
     * cv::Mat frame = decoder->decode(h264Data);
     * detector->process(frame);  // 在 frame 上绘制检测框
     * streamer->pushFrame(frame);  // 推送带检测框的图像
     * @endcode
     */
    bool pushFrame(const cv::Mat& frame);

    /**
     * @brief 关闭推流并释放资源
     * 
     * @note
     * - 可以安全地多次调用
     * - 会发送 EOS（End Of Stream）事件
     * - 释放所有 GStreamer 元素
     * - 析构函数会自动调用
     */
    void shutdown();

    /**
     * @brief 检查推流器是否已初始化且管道正常
     * 
     * @return true 已初始化且管道处于 PLAYING 状态
     * @return false 未初始化或管道已停止
     */
    bool isConnected() const;

    /**
     * @brief 获取已推送的帧数
     * 
     * @return int64_t 自 initialize() 以来推送的总帧数
     */
    int64_t getFrameCount() const { return frameCount_; }

    /**
     * @brief 获取 RTMP URL
     * 
     * @return std::string 当前的 RTMP 服务器地址
     */
    std::string getRtmpUrl() const { return rtmpUrl_; }

private:
    /**
     * @brief 创建 GStreamer 管道
     * 
     * 使用 gst_parse_launch 创建完整管道
     * 
     * @return true 成功
     * @return false 失败
     */
    bool createPipeline();

    /**
     * @brief GStreamer 消息回调（总线监听）
     * 
     * 处理错误、警告、EOS 等消息
     * 
     * @param bus GStreamer 总线
     * @param message 消息
     * @param userData 用户数据（RtmpStreamer 指针）
     * @return gboolean 是否继续监听
     */
    static gboolean onBusMessage(GstBus* bus, GstMessage* message, gpointer userData);

    /**
     * @brief 释放所有 GStreamer 资源
     */
    void cleanup();

private:
    // ========== 配置参数 ==========
    std::string rtmpUrl_;           ///< RTMP 服务器地址
    int width_;                     ///< 视频宽度
    int height_;                    ///< 视频高度
    int fps_;                       ///< 视频帧率

    // ========== GStreamer 上下文 ==========
    GstElement* pipeline_;          ///< GStreamer 管道（包含所有元素）
    GstElement* appsrc_;            ///< appsrc 元素（用于推送 H.264 数据）
    GstBus* bus_;                   ///< GStreamer 消息总线
    guint busWatchId_;              ///< 总线监听 ID

    // ========== 状态管理 ==========
    std::atomic<bool> initialized_; ///< 是否已初始化
    std::atomic<int64_t> frameCount_; ///< 已推送的帧数

    // ========== 线程安全 ==========
    mutable std::mutex mutex_;      ///< 保护内部状态的互斥锁

    // ========== 日志 ==========
    core::Logger& logger_;          ///< 日志记录器
};

}  // namespace rtmp
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_RTMP_STREAMER_H_

// ============================================================================
// 📚 知识点：GStreamer RTMP 推流
// ============================================================================
//
// ### 1. GStreamer 核心概念
//
// **Element（元素）**：
// - GStreamer 的基本构建块
// - 每个元素执行特定功能（解析、编码、封装、输出）
// - 元素之间通过 Pad 连接传递数据
//
// **Pipeline（管道）**：
// - 多个元素连接成的数据处理链
// - 使用 gst_parse_launch() 快速创建：
//   "appsrc ! h264parse ! flvmux ! rtmpsink"
//
// **Bus（消息总线）**：
// - 元素通过 Bus 发送消息（错误、警告、EOS）
// - 应用程序通过 Bus 监听管道状态
//
// ### 2. 本项目使用的 GStreamer 元素
//
// **appsrc**：
// - Application Source，应用程序数据源
// - 用途：将 H.264 数据从 C++ 推入管道
// - 关键属性：
//   - is-live=true：实时流（不缓冲过多数据）
//   - format=time：使用时间戳格式
//   - caps：设置数据格式（video/x-h264）
//
// **h264parse**：
// - H.264 解析器
// - 核心功能（解决 SPS/PPS 问题）：
//   1. 解析 NAL 单元，识别 SPS/PPS/IDR/P帧
//   2. 提取 SPS/PPS，生成 codec_data
//   3. 设置 codec_data 到输出 caps
//   4. 转换起始码格式（Annex B → 长度前缀）
// - 这就是我们不需要手动处理 SPS/PPS 的原因！
//
// **flvmux**：
// - FLV 封装器
// - 将 H.264 流封装为 FLV 格式
// - 关键属性：
//   - streamable=true：流式封装（无需文件头回写）
// - 自动处理时间戳和元数据
//
// **rtmpsink**：
// - RTMP 输出元素
// - 连接到 RTMP 服务器并推流
// - 关键属性：
//   - location：RTMP URL
//   - async=true：异步写入（不阻塞管道）
//   - sync=false：不同步时钟（降低延迟）
//
// ### 3. GStreamer 推流流程
//
// ```
// 1. gst_init()                      - 初始化 GStreamer
// 2. gst_parse_launch()              - 创建管道
// 3. gst_element_set_state(PLAYING)  - 启动管道
// 4. gst_app_src_push_buffer()       - 循环推送 H.264 数据
// 5. gst_element_send_event(EOS)     - 发送结束事件
// 6. gst_element_set_state(NULL)     - 停止管道
// 7. gst_object_unref()              - 释放资源
// ```
//
// ### 4. h264parse 自动处理 SPS/PPS 的原理
//
// **问题回顾**：
// - DJI SDK 输出的 H.264 是 Annex B 格式（带起始码）
// - RTMP 需要 AVCC 格式（codec_data 包含 SPS/PPS）
// - FFmpeg 方案需要手动解析 NAL、提取 SPS/PPS、设置 extradata
//
// **h264parse 的解决方案**：
// ```
// DJI SDK H.264 (Annex B)
//   ↓
// appsrc (推入 GStreamer)
//   ↓
// h264parse (自动处理)：
//   1. 解析 NAL 起始码（0x000001 或 0x00000001）
//   2. 识别 NAL 类型（SPS=0x67, PPS=0x68, IDR=0x65）
//   3. 提取 SPS/PPS，缓存起来
//   4. 生成 codec_data（AVCC 格式）
//   5. 设置 caps: video/x-h264,codec_data=<...>
//   ↓
// flvmux (读取 codec_data)
//   ↓
// RTMP 流（正确格式）
// ```
//
// **对比 FFmpeg 方案**：
// ```
// FFmpeg 方案（手动）：
// - 编写 H264Parser 类（100+ 行代码）
// - 手动解析起始码
// - 手动提取 SPS/PPS
// - 手动设置 extradata
// - 处理边界情况（3字节/4字节起始码）
//
// GStreamer 方案（自动）：
// - h264parse 一行代码搞定
// - 久经考验的开源实现
// - 自动处理所有边界情况
// ```
//
// ### 5. 面试要点
//
// **Q: GStreamer 和 FFmpeg 的区别？**
// A:
// - **架构**：
//   - GStreamer：管道式（Pipeline），元素组合
//   - FFmpeg：库式（Library），API 调用
// - **灵活性**：
//   - GStreamer：更灵活，可动态重配置管道
//   - FFmpeg：更轻量，编程模型简单
// - **应用场景**：
//   - GStreamer：流媒体、实时处理、复杂管道
//   - FFmpeg：转码、格式转换、简单推流
// - **学习曲线**：
//   - GStreamer：陡峭（概念多：Element、Pad、Caps、Bus）
//   - FFmpeg：平缓（直接调用 API）
//
// **Q: h264parse 是如何提取 SPS/PPS 的？**
// A:
// 1. 读取 H.264 字节流，查找起始码（0x000001）
// 2. 解析 NAL 单元类型（第一个字节 & 0x1F）
// 3. 如果是 SPS（0x67）或 PPS（0x68），缓存数据
// 4. 生成 AVCC 格式的 codec_data：
//    - 1 字节：configurationVersion
//    - 1 字节：AVCProfileIndication
//    - 1 字节：profile_compatibility
//    - 1 字节：AVCLevelIndication
//    - SPS 数量 + SPS 数据
//    - PPS 数量 + PPS 数据
// 5. 设置到输出 Caps 的 codec_data 字段
//
// **Q: 为什么选择 GStreamer 而不是 FFmpeg？**
// A:
// - **技术角度**：
//   - GStreamer 的 h264parse 自动处理 SPS/PPS
//   - FFmpeg 需要手动实现 NAL 解析（工作量大）
//   - GStreamer 管道更适合实时流处理
// - **工程角度**：
//   - 快速上线，减少开发时间
//   - 减少 bug 风险（使用成熟组件）
//   - 参考旧项目已验证可行性
// - **职业角度**：
//   - 拥抱新技术（GStreamer 在嵌入式领域广泛应用）
//   - 解决问题优先（不固步自封）
//
// ### 6. 易错点
//
// ❌ **错误 1**：忘记初始化 GStreamer
// ```cpp
// // 错误：直接创建管道
// GstElement* pipeline = gst_parse_launch("...", nullptr);  // ❌ 崩溃
// ```
// ✅ **正确**：
// ```cpp
// gst_init(nullptr, nullptr);  // ✅ 必须先初始化
// GstElement* pipeline = gst_parse_launch("...", nullptr);
// ```
//
// ❌ **错误 2**：不监听 Bus 消息
// ```cpp
// // 错误：忽略错误消息
// gst_element_set_state(pipeline, GST_STATE_PLAYING);
// // 推流失败了也不知道！
// ```
// ✅ **正确**：
// ```cpp
// GstBus* bus = gst_element_get_bus(pipeline);
// gst_bus_add_watch(bus, onBusMessage, this);  // ✅ 监听错误
// gst_object_unref(bus);
// ```
//
// ❌ **错误 3**：appsrc 的 caps 设置错误
// ```cpp
// // 错误：不设置 caps
// gst_app_src_push_buffer(appsrc, buffer);  // ❌ 管道不知道数据格式
// ```
// ✅ **正确**：
// ```cpp
// GstCaps* caps = gst_caps_new_simple(
//     "video/x-h264",
//     "stream-format", G_TYPE_STRING, "byte-stream",  // ✅ Annex B 格式
//     "alignment", G_TYPE_STRING, "au",               // ✅ Access Unit
//     nullptr
// );
// gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);
// gst_caps_unref(caps);
// ```
//
// ❌ **错误 4**：不释放 GStreamer 资源
// ```cpp
// // 错误：析构时不清理
// ~RtmpStreamer() {
//     // 忘记释放管道！
// }  // ❌ 内存泄漏
// ```
// ✅ **正确**：
// ```cpp
// ~RtmpStreamer() {
//     shutdown();  // ✅ 释放资源
// }
//
// void shutdown() {
//     if (pipeline_) {
//         gst_element_set_state(pipeline_, GST_STATE_NULL);
//         gst_object_unref(GST_OBJECT(pipeline_));
//         pipeline_ = nullptr;
//     }
// }
// ```
//
// ### 7. GStreamer 调试技巧
//
// **环境变量**：
// ```bash
// # 设置日志级别（0=无日志, 9=最详细）
// export GST_DEBUG=3
//
// # 只显示特定元素的日志
// export GST_DEBUG=rtmpsink:5,h264parse:4
//
// # 输出日志到文件
// export GST_DEBUG_FILE=/tmp/gst_debug.log
//
// # 生成管道图（dot 格式）
// export GST_DEBUG_DUMP_DOT_DIR=/tmp
// ```
//
// **命令行测试**：
// ```bash
// # 测试 RTMP 推流
// gst-launch-1.0 videotestsrc ! x264enc ! h264parse ! flvmux !
//   rtmpsink location=rtmp://localhost/live/test
//
// # 查看元素信息
// gst-inspect-1.0 h264parse
// gst-inspect-1.0 rtmpsink
// ```
//
// ============================================================================
