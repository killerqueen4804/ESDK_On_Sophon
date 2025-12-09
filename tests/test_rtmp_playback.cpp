/**
 * @file test_rtmp_playback.cpp
 * @brief 使用 GStreamer 库直接测试 RTMP 流播放
 * 
 * 用途:
 * - 在没有 gst-launch-1.0 命令行工具的嵌入式设备上测试 RTMP 流
 * - 验证 RTMP 推流是否可正常接收和解码
 * 
 * 编译:
 *   g++ test_rtmp_playback.cpp -o test_rtmp_playback \
 *       $(pkg-config --cflags --libs gstreamer-1.0)
 * 
 * 运行:
 *   ./test_rtmp_playback "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"
 */

#include <gst/gst.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

/**
 * @brief GStreamer 消息处理回调
 */
static gboolean bus_callback(GstBus* bus, GstMessage* message, gpointer data) {
    GMainLoop* loop = static_cast<GMainLoop*>(data);
    
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            
            gst_message_parse_error(message, &err, &debug);
            std::cerr << "❌ GStreamer 错误: " << err->message << std::endl;
            if (debug) {
                std::cerr << "   调试信息: " << debug << std::endl;
            }
            
            g_error_free(err);
            g_free(debug);
            
            g_main_loop_quit(loop);
            break;
        }
        
        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            
            gst_message_parse_warning(message, &err, &debug);
            std::cout << "⚠️  GStreamer 警告: " << err->message << std::endl;
            if (debug) {
                std::cout << "   调试信息: " << debug << std::endl;
            }
            
            g_error_free(err);
            g_free(debug);
            break;
        }
        
        case GST_MESSAGE_EOS:
            std::cout << "📌 流结束 (EOS)" << std::endl;
            g_main_loop_quit(loop);
            break;
        
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(data)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
                
                std::cout << "🔄 状态变化: " 
                          << gst_element_state_get_name(old_state) << " -> "
                          << gst_element_state_get_name(new_state) << std::endl;
            }
            break;
        }
        
        case GST_MESSAGE_STREAM_START:
            std::cout << "✅ 流已开始!" << std::endl;
            break;
        
        case GST_MESSAGE_NEW_CLOCK:
            std::cout << "🕒 新时钟已设置" << std::endl;
            break;
        
        default:
            break;
    }
    
    return TRUE;
}

/**
 * @brief Pad 添加回调(动态连接)
 */
static void on_pad_added(GstElement* element, GstPad* pad, gpointer data) {
    GstElement* decoder = static_cast<GstElement*>(data);
    GstPad* sinkpad = gst_element_get_static_pad(decoder, "sink");
    
    if (!gst_pad_is_linked(sinkpad)) {
        if (gst_pad_link(pad, sinkpad) == GST_PAD_LINK_OK) {
            std::cout << "✅ Pad 连接成功" << std::endl;
        } else {
            std::cerr << "❌ Pad 连接失败" << std::endl;
        }
    }
    
    gst_object_unref(sinkpad);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "使用方法: " << argv[0] << " <RTMP_URL>" << std::endl;
        std::cerr << "示例: " << argv[0] << " \"rtmp://...\"" << std::endl;
        return 1;
    }
    
    std::string rtmpUrl = argv[1];
    
    std::cout << "========================================" << std::endl;
    std::cout << "  RTMP 流播放测试" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "URL: " << rtmpUrl << std::endl;
    std::cout << std::endl;
    
    // 初始化 GStreamer
    gst_init(&argc, &argv);
    
    // 创建 GStreamer 元素
    GstElement* pipeline = gst_pipeline_new("rtmp-test-pipeline");
    GstElement* rtmpsrc = gst_element_factory_make("rtmpsrc", "rtmp-source");
    GstElement* flvdemux = gst_element_factory_make("flvdemux", "flv-demuxer");
    GstElement* h264parse = gst_element_factory_make("h264parse", "h264-parser");
    GstElement* fakesink = gst_element_factory_make("fakesink", "fake-sink");
    
    if (!pipeline || !rtmpsrc || !flvdemux || !h264parse || !fakesink) {
        std::cerr << "❌ 无法创建 GStreamer 元素" << std::endl;
        std::cerr << "请检查 GStreamer 插件是否已安装:" << std::endl;
        std::cerr << "  sudo apt-get install gstreamer1.0-plugins-bad" << std::endl;
        return 1;
    }
    
    // 设置 RTMP URL
    g_object_set(G_OBJECT(rtmpsrc), "location", rtmpUrl.c_str(), nullptr);
    
    // 设置 fakesink 属性(显示统计信息)
    g_object_set(G_OBJECT(fakesink), 
                 "silent", FALSE,     // 显示缓冲区信息
                 "sync", FALSE,       // 不同步(快速处理)
                 nullptr);
    
    // 添加元素到管道
    gst_bin_add_many(GST_BIN(pipeline), rtmpsrc, flvdemux, h264parse, fakesink, nullptr);
    
    // 连接静态元素
    if (!gst_element_link(rtmpsrc, flvdemux)) {
        std::cerr << "❌ 无法连接 rtmpsrc -> flvdemux" << std::endl;
        return 1;
    }
    
    if (!gst_element_link(h264parse, fakesink)) {
        std::cerr << "❌ 无法连接 h264parse -> fakesink" << std::endl;
        return 1;
    }
    
    // 动态连接 flvdemux -> h264parse (因为 flvdemux 有动态 pad)
    g_signal_connect(flvdemux, "pad-added", G_CALLBACK(on_pad_added), h264parse);
    
    // 创建消息总线
    GstBus* bus = gst_element_get_bus(pipeline);
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    gst_bus_add_watch(bus, bus_callback, loop);
    gst_object_unref(bus);
    
    // 启动管道
    std::cout << "📌 启动 GStreamer 管道..." << std::endl;
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "❌ 无法启动管道" << std::endl;
        gst_object_unref(pipeline);
        return 1;
    }
    
    std::cout << "✅ 管道已启动，正在连接 RTMP 流..." << std::endl;
    std::cout << "   (测试 10 秒后自动停止)" << std::endl;
    std::cout << std::endl;
    
    // 运行主循环(10 秒后自动停止)
    std::thread timeout_thread([loop]() {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::cout << std::endl;
        std::cout << "⏰ 测试时间到，停止管道..." << std::endl;
        g_main_loop_quit(loop);
    });
    
    g_main_loop_run(loop);
    
    // 清理资源
    timeout_thread.join();
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  测试完成" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "💡 结果分析:" << std::endl;
    std::cout << "  ✅ 如果看到 'buffer' 输出 → RTMP 流连接成功!" << std::endl;
    std::cout << "  ❌ 如果看到错误信息 → RTMP 流连接失败" << std::endl;
    std::cout << std::endl;
    
    return 0;
}
