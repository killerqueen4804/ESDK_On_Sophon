/**
 * @file RtmpStreamer.cpp
 * @brief RTMP 推流器实现（GStreamer 版本）
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-11 (技术选型：GStreamer 替代 FFmpeg)
 */

#include "esdk_sophon/rtmp/RtmpStreamer.h"
#include <cstring>
#include <sstream>

namespace esdk_sophon {
namespace rtmp {

// ============================================================================
// 构造函数和析构函数
// ============================================================================

RtmpStreamer::RtmpStreamer()
    : rtmpUrl_("")
    , width_(0)
    , height_(0)
    , fps_(0)
    , pipeline_(nullptr)
    , appsrc_(nullptr)
    , bus_(nullptr)
    , busWatchId_(0)
    , initialized_(false)
    , frameCount_(0)
    , logger_(core::Logger::getInstance())
{
    // 初始化 GStreamer（仅初始化一次）
    static bool gstInitialized = false;
    if (!gstInitialized) {
        // 📌 启用 GStreamer 详细日志（临时调试 RTMP 推流问题）
        // 级别：0=无, 1=错误, 2=警告, 3=信息, 4=调试, 5=详细
        setenv("GST_DEBUG", "rtmpsink:5,x264enc:4,flvmux:4", 0);  // 详细日志
        setenv("GST_DEBUG_FILE", "/tmp/gst_rtmp_debug.log", 0);  // 输出到文件
        
        // 设置 GStreamer 插件搜索路径（ARM64 设备）
        // 注：已安装 gstreamer1.0-plugins-bad 官方包
        setenv("GST_PLUGIN_SYSTEM_PATH", "/usr/lib/aarch64-linux-gnu/gstreamer-1.0", 0);
        
        gst_init(nullptr, nullptr);
        gstInitialized = true;
        logger_.info("GStreamer 初始化成功");
        
        // 调试：列出可用插件数量
        GList* plugins = gst_registry_get_plugin_list(gst_registry_get());
        guint pluginCount = g_list_length(plugins);
        gst_plugin_list_free(plugins);
        logger_.info("GStreamer 插件数量: " + std::to_string(pluginCount));
        
        // 验证关键插件是否可用（防御性编程）
        const char* requiredPlugins[] = {"h264parse", "flvmux", "rtmpsink"};
        bool allPluginsAvailable = true;
        for (const char* pluginName : requiredPlugins) {
            GstElementFactory* factory = gst_element_factory_find(pluginName);
            if (factory) {
                logger_.info(std::string("✓ 插件可用: ") + pluginName);
                gst_object_unref(factory);
            } else {
                logger_.error(std::string("✗ 插件缺失: ") + pluginName);
                allPluginsAvailable = false;
            }
        }
        
        if (!allPluginsAvailable) {
            logger_.error("缺少必需的 GStreamer 插件！");
            logger_.error("请执行: sudo apt-get install -y gstreamer1.0-plugins-bad:arm64");
        }
    }

    logger_.info("RtmpStreamer 创建");
}

RtmpStreamer::~RtmpStreamer() {
    shutdown();
    logger_.info("RtmpStreamer 销毁");
}

// ============================================================================
// 公共接口实现
// ============================================================================

bool RtmpStreamer::initialize(const std::string& rtmpUrl, int width, int height, int fps) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 如果已初始化，先关闭旧管道
    if (initialized_) {
        logger_.warning("RtmpStreamer 已初始化，重新初始化...");
        cleanup();
    }

    // 参数验证
    if (rtmpUrl.empty()) {
        logger_.error("RTMP URL 为空");
        return false;
    }
    if (width <= 0 || height <= 0) {
        logger_.error("视频尺寸无效: " + std::to_string(width) + "x" + std::to_string(height));
        return false;
    }
    if (fps <= 0 || fps > 120) {
        logger_.error("帧率无效: " + std::to_string(fps));
        return false;
    }

    // 保存配置
    rtmpUrl_ = rtmpUrl;
    width_ = width;
    height_ = height;
    fps_ = fps;

    logger_.info("初始化 RTMP 推流器:");
    logger_.info("  URL: " + rtmpUrl_);
    logger_.info("  分辨率: " + std::to_string(width_) + "x" + std::to_string(height_));
    logger_.info("  帧率: " + std::to_string(fps_) + " FPS");

    // 创建 GStreamer 管道
    if (!createPipeline()) {
        logger_.error("创建 GStreamer 管道失败");
        cleanup();
        return false;
    }

    // 启动管道
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        logger_.error("启动 GStreamer 管道失败");
        cleanup();
        return false;
    } else if (ret == GST_STATE_CHANGE_ASYNC) {
        logger_.info("管道正在异步启动，等待状态切换...");
        // 等待最多 5 秒让管道进入 PLAYING 状态
        GstState state = GST_STATE_NULL;
        GstState pending = GST_STATE_NULL;
        ret = gst_element_get_state(pipeline_, &state, &pending, 5 * GST_SECOND);
        
        if (ret == GST_STATE_CHANGE_FAILURE) {
            logger_.error("等待管道启动失败");
            cleanup();
            return false;
        } else if (ret == GST_STATE_CHANGE_ASYNC) {
            logger_.warning("等待管道启动超时 (仍处于 ASYNC 状态)，但将尝试继续...");
        }
        
        logger_.info("管道状态确认: " + std::string(gst_element_state_get_name(state)));
    }
    
    // � 修复: 移除重复的总线监听
    // 总线监听已在 createPipeline() 中通过 onBusMessage 函数添加
    // 这里的 Lambda 版本是重复的,会导致 GStreamer 警告:
    // "ERROR GST_BUS Tried to add new watch while one was already there"
    
    // ❌ 删除此重复代码:
    // GstBus* bus = gst_element_get_bus(pipeline_);
    // gst_bus_add_watch(bus, [...] {}, this);  // 重复!
    // gst_object_unref(bus);
    
    initialized_ = true;
    frameCount_ = 0;

    logger_.info("RTMP 推流器初始化成功，管道已启动");
    return true;
}

bool RtmpStreamer::pushFrame(const cv::Mat& frame) {
    // 双重检查：防止在初始化过程中被调用
    if (!initialized_.load()) {
        static std::atomic<int> earlyCallCount{0};
        if (earlyCallCount.fetch_add(1) < 3) {
            logger_.debug("推流器尚未初始化，跳过帧推送（早期调用 #" + 
                         std::to_string(earlyCallCount.load()) + "）");
        }
        return false;
    }
    
    if (!appsrc_) {
        logger_.error("appsrc 为空（可能管道创建失败）");
        return false;
    }
    
    // 检查管道状态
    if (!pipeline_) {
        logger_.error("pipeline 为空");
        return false;
    }
    
    GstState state;
    GstStateChangeReturn stateRet = gst_element_get_state(pipeline_, &state, nullptr, 0);
    if (stateRet == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PLAYING) {
        static std::atomic<int> stateErrorCount{0};
        if (stateErrorCount.fetch_add(1) < 3) {
            logger_.error("管道状态异常: state=" + std::string(gst_element_state_get_name(state)));
        }
        return false;
    }

    if (frame.empty()) {
        logger_.warning("cv::Mat 为空，跳过");
        return false;
    }

    // 验证图像尺寸
    if (frame.cols != width_ || frame.rows != height_) {
        static std::atomic<int> sizeWarningCount{0};
        if (sizeWarningCount.fetch_add(1) < 3) {
            logger_.warning("图像尺寸不匹配！期望: " + std::to_string(width_) + "x" + std::to_string(height_) + 
                           "，实际: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows));
        }
        // 继续处理，resize 会自动调整
    }

    // 处理图像：调整大小 + BGR → I420 (YUV)
    cv::Mat processed;
    
    // 步骤 1: 调整到目标尺寸（如果需要）
    if (frame.cols != width_ || frame.rows != height_) {
        cv::resize(frame, processed, cv::Size(width_, height_), 0, 0, cv::INTER_LINEAR);
    } else {
        processed = frame;
    }

    // 步骤 2: 转换颜色格式 (BGR -> I420)
    // 使用 OpenCV 进行颜色转换，比 GStreamer videoconvert 更可靠
    cv::Mat yuv;
    if (processed.channels() == 3) {
        // BGR → I420 (YUV420P)
        cv::cvtColor(processed, yuv, cv::COLOR_BGR2YUV_I420);
    } else if (processed.channels() == 1) {
        // 灰度图 → I420 (需要特殊处理，这里简化为先转BGR再转I420)
        cv::Mat bgr;
        cv::cvtColor(processed, bgr, cv::COLOR_GRAY2BGR);
        cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV_I420);
    } else if (processed.channels() == 4) {
        // BGRA → I420
        cv::Mat bgr;
        cv::cvtColor(processed, bgr, cv::COLOR_BGRA2BGR);
        cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV_I420);
    } else {
        logger_.error("不支持的图像通道数: " + std::to_string(processed.channels()));
        return false;
    }

    // 创建 GstBuffer（分配内存）
    // I420 大小 = width * height * 1.5
    size_t bufferSize = width_ * height_ * 3 / 2;
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, bufferSize, nullptr);
    if (!buffer) {
        logger_.error("分配 GstBuffer 失败");
        return false;
    }

    // 映射缓冲区并拷贝数据
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        logger_.error("映射 GstBuffer 失败");
        gst_buffer_unref(buffer);
        return false;
    }

    // 拷贝 I420 数据到 GstBuffer
    // OpenCV 的 I420 输出是连续的内存块 (Y plane + U plane + V plane)
    // 直接拷贝即可
    if (yuv.total() * yuv.elemSize() != bufferSize) {
        logger_.warning("YUV 数据大小不匹配: 期望 " + std::to_string(bufferSize) + 
                       ", 实际 " + std::to_string(yuv.total() * yuv.elemSize()));
        // 继续尝试拷贝，防止崩溃
        size_t copySize = std::min(bufferSize, yuv.total() * yuv.elemSize());
        memcpy(map.data, yuv.data, copySize);
    } else {
        memcpy(map.data, yuv.data, bufferSize);
    }
    
    gst_buffer_unmap(buffer, &map);

    // 📌 移除手动时间戳设置，改用 appsrc 的 do-timestamp=true
    // 这能更好地适应处理抖动，避免因处理延迟导致的 PTS 累积偏差
    // GstClockTime pts = static_cast<GstClockTime>(frameCount_.load() * GST_SECOND / fps_);
    // GST_BUFFER_PTS(buffer) = pts;
    // GST_BUFFER_DTS(buffer) = pts;
    // GST_BUFFER_DURATION(buffer) = GST_SECOND / fps_;

    // 推送到 appsrc
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
    
    if (ret != GST_FLOW_OK) {
        const char* retName = "UNKNOWN";
        switch (ret) {
            case GST_FLOW_FLUSHING: retName = "FLUSHING"; break;
            case GST_FLOW_EOS: retName = "EOS"; break;
            case GST_FLOW_NOT_LINKED: retName = "NOT_LINKED"; break;
            case GST_FLOW_ERROR: retName = "ERROR"; break;
            default: retName = "OTHER"; break;
        }
        
        logger_.error("推送 RGB 帧失败: GstFlowReturn = " + std::string(retName) + 
                     " (" + std::to_string(static_cast<int>(ret)) + ")");
        logger_.error("  帧号: " + std::to_string(frameCount_.load() + 1));
        logger_.error("  数据大小: " + std::to_string(bufferSize) + " 字节");
        
        // 关键修复: push_buffer 失败时需要手动释放
        gst_buffer_unref(buffer);
        return false;
    }

    // 更新帧计数
    frameCount_++;

    // 每 30 帧打印一次统计
    if (frameCount_ % 30 == 0) {
        logger_.debug("RTMP 推流中: 已推送 " + std::to_string(frameCount_.load()) + " 帧");
    }

    return true;
}

void RtmpStreamer::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;  // 未初始化，无需清理
    }

    logger_.info("关闭 RTMP 推流器...");

    // 发送 EOS（End Of Stream）事件
    if (appsrc_) {
        gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
        logger_.info("已发送 EOS 事件");
    }

    // 停止管道
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        logger_.info("管道已停止");
    }

    // 移除总线监听
    if (busWatchId_ > 0) {
        g_source_remove(busWatchId_);
        busWatchId_ = 0;
    }

    // 释放资源
    cleanup();

    initialized_ = false;
    logger_.info("RTMP 推流器已关闭，总推送帧数: " + std::to_string(frameCount_.load()));
}

bool RtmpStreamer::isConnected() const {
    if (!initialized_ || !pipeline_) {
        return false;
    }

    // 检查管道状态
    GstState state;
    GstStateChangeReturn ret = gst_element_get_state(
        pipeline_, 
        &state, 
        nullptr,  // 不关心 pending state
        0         // 不等待
    );

    return (ret != GST_STATE_CHANGE_FAILURE && state == GST_STATE_PLAYING);
}

// ============================================================================
// 私有方法实现
// ============================================================================

bool RtmpStreamer::createPipeline() {
    // 构建 GStreamer 管道描述字符串
    // 📌 方案 A：重新编码方案（参考旧项目）
    // appsrc: 应用程序数据源，推送 RGB cv::Mat
    // videoconvert: 自动转换颜色格式（RGB → YUV）
    // x264enc: H.264 编码器，自动生成 SPS/PPS（解决核心问题！）
    // h264parse: 解析 H.264 流，提取 codec_data
    // flvmux: 封装为 FLV 格式
    // rtmpsink: 推流到 RTMP 服务器
    std::string pipelineDesc = 
        "appsrc name=source is-live=true format=time do-timestamp=true "
        "! videoconvert "
        "! queue max-size-buffers=5 leaky=2 "  // 📌 减小队列长度以降低延迟
        "! x264enc bitrate=2000 tune=zerolatency speed-preset=ultrafast key-int-max=" + 
        std::to_string(fps_ * 2) + " "
        "! h264parse "
        "! flvmux streamable=true "
        "! rtmpsink location=\"" + rtmpUrl_ + "\" async=false sync=false";

    logger_.info("创建 GStreamer 管道: " + pipelineDesc);

    // 解析并创建管道
    GError* error = nullptr;
    pipeline_ = gst_parse_launch(pipelineDesc.c_str(), &error);
    if (error) {
        std::string errorMsg = error->message;
        logger_.error("创建管道失败: " + errorMsg);
        g_error_free(error);
        return false;
    }

    if (!pipeline_) {
        logger_.error("管道创建失败（未知原因）");
        return false;
    }

    // 获取 appsrc 元素
    appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "source");
    if (!appsrc_) {
        logger_.error("获取 appsrc 元素失败");
        return false;
    }

    // 配置 appsrc 的 caps（告诉 GStreamer 输入数据格式）
    // 📌 方案 B：直接推送 I420 (YUV) 格式
    // 规避 GStreamer videoconvert 在 BGR/RGB 转换上的潜在问题
    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "I420",    // I420 (YUV420P)
        "width", G_TYPE_INT, width_,
        "height", G_TYPE_INT, height_,
        "framerate", GST_TYPE_FRACTION, fps_, 1,
        nullptr
    );

    gst_app_src_set_caps(GST_APP_SRC(appsrc_), caps);
    gst_caps_unref(caps);

    logger_.info("appsrc caps 设置成功: video/x-raw, format=I420");

    // 配置 appsrc 属性
    g_object_set(
        G_OBJECT(appsrc_),
        "stream-type", GST_APP_STREAM_TYPE_STREAM,  // 流式数据（非 seekable）
        "is-live", TRUE,                             // 实时流
        "format", GST_FORMAT_TIME,                   // 使用时间戳格式
        "do-timestamp", TRUE,                        // 📌 自动打时间戳（基于到达时间）
        nullptr
    );

    // 设置总线消息监听（监听错误、警告、EOS）
    bus_ = gst_element_get_bus(pipeline_);
    busWatchId_ = gst_bus_add_watch(bus_, onBusMessage, this);
    gst_object_unref(bus_);  // add_watch 会增加引用计数，这里可以立即 unref

    logger_.info("GStreamer 管道创建成功（重新编码方案）");
    return true;
}

gboolean RtmpStreamer::onBusMessage(GstBus* bus, GstMessage* message, gpointer userData) {
    (void)bus;  // 消除未使用参数警告
    RtmpStreamer* self = static_cast<RtmpStreamer*>(userData);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);

            std::string errorMsg = error ? error->message : "未知错误";
            std::string debugMsg = debug ? debug : "无调试信息";

            self->logger_.error("GStreamer 错误: " + errorMsg);
            self->logger_.error("调试信息: " + debugMsg);

            g_error_free(error);
            g_free(debug);
            break;
        }

        case GST_MESSAGE_WARNING: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(message, &error, &debug);

            std::string warnMsg = error ? error->message : "未知警告";
            self->logger_.warning("GStreamer 警告: " + warnMsg);

            g_error_free(error);
            g_free(debug);
            break;
        }

        case GST_MESSAGE_EOS: {
            self->logger_.info("GStreamer 收到 EOS（流结束）");
            break;
        }

        case GST_MESSAGE_STATE_CHANGED: {
            // 只记录管道级别的状态变化
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(self->pipeline_)) {
                GstState oldState, newState, pendingState;
                gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);

                const char* oldStateName = gst_element_state_get_name(oldState);
                const char* newStateName = gst_element_state_get_name(newState);

                self->logger_.info(
                    "管道状态变化: " + std::string(oldStateName) + " -> " + std::string(newStateName)
                );
            }
            break;
        }

        default:
            // 忽略其他消息
            break;
    }

    return TRUE;  // 继续监听
}

void RtmpStreamer::cleanup() {
    // 释放 appsrc（已经被 pipeline 管理，不需要单独 unref）
    appsrc_ = nullptr;

    // 释放管道
    if (pipeline_) {
        gst_object_unref(GST_OBJECT(pipeline_));
        pipeline_ = nullptr;
    }

    // bus_ 已经在 createPipeline() 中 unref，这里不需要重复释放
    bus_ = nullptr;

    logger_.info("GStreamer 资源已释放");
}

}  // namespace rtmp
}  // namespace esdk_sophon

