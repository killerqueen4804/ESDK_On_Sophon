#!/bin/bash
#
# 📌 RTMP 推流测试脚本 (GStreamer 版本)
# 适用于只安装了 GStreamer 的嵌入式设备(如 SE7)
#
# 使用方法:
#   bash test_rtmp_gst.sh <RTMP_URL>
#
# 示例:
#   bash test_rtmp_gst.sh "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"

set -e

# ============================================
# 📌 自动查找 GStreamer 工具路径
# ============================================
GST_LAUNCH=""
GST_DISCOVERER=""

if [ -f "/data/gst/bin/gst-launch-1.0" ]; then
    GST_LAUNCH="/data/gst/bin/gst-launch-1.0"
    GST_DISCOVERER="/data/gst/bin/gst-discoverer-1.0"
elif command -v gst-launch-1.0 &> /dev/null; then
    GST_LAUNCH="gst-launch-1.0"
    GST_DISCOVERER="gst-discoverer-1.0"
else
    for path in /usr/bin /usr/local/bin /opt/sophon/libsophon-current/bin; do
        if [ -f "$path/gst-launch-1.0" ]; then
            GST_LAUNCH="$path/gst-launch-1.0"
            GST_DISCOVERER="$path/gst-discoverer-1.0"
            break
        fi
    done
fi

if [ -z "$GST_LAUNCH" ]; then
    echo "❌ 错误: 未找到 GStreamer 工具"
    echo "请先运行: bash find_gstreamer.sh"
    exit 1
fi

RTMP_URL="$1"

if [ -z "$RTMP_URL" ]; then
    echo "错误: 未提供 RTMP URL"
    echo "使用方法: $0 <RTMP_URL>"
    exit 1
fi

echo "=========================================="
echo "  RTMP 推流测试 (GStreamer 版本)"
echo "=========================================="
echo "URL: $RTMP_URL"
echo ""
echo "📌 使用的 GStreamer 工具:"
echo "   gst-launch: $GST_LAUNCH"
echo "   gst-discoverer: $GST_DISCOVERER"
echo ""

# 检查 GStreamer 是否安装
if [ ! -f "$GST_LAUNCH" ]; then
    echo "❌ 错误: GStreamer 工具不存在"
    echo "   路径: $GST_LAUNCH"
    exit 1
fi

echo "✅ GStreamer 版本:"
$GST_LAUNCH --version | head -1
echo ""

# 方法 1: 使用 gst-discoverer-1.0 分析流
echo "=== 方法 1: gst-discoverer 分析流 ==="
echo "命令: $GST_DISCOVERER \"$RTMP_URL\""
echo ""

if [ -f "$GST_DISCOVERER" ]; then
    timeout 10s $GST_DISCOVERER "$RTMP_URL" 2>&1 || {
        echo "❌ gst-discoverer 分析失败（可能流未启动或 URL 错误）"
        echo ""
    }
else
    echo "⚠️  gst-discoverer-1.0 未找到，跳过"
    echo ""
fi

# 方法 2: 使用 GStreamer 管道测试连接
echo "=== 方法 2: GStreamer 管道测试 ==="
echo "尝试连接 RTMP 流并输出统计信息..."
echo ""
echo "命令: $GST_LAUNCH rtmpsrc location=\"$RTMP_URL\" ! ..."
echo ""

timeout 10s $GST_LAUNCH -v \
    rtmpsrc location="$RTMP_URL" ! \
    flvdemux ! \
    h264parse ! \
    fakesink dump=false silent=false 2>&1 | head -50 || {
    echo ""
    echo "❌ GStreamer 管道测试失败"
    echo ""
}

# 方法 3: 使用 playbin 播放（如果有显示设备）
echo "=== 方法 3: playbin 播放测试 ==="
echo ""

if [ -n "$DISPLAY" ]; then
    echo "检测到显示设备，尝试播放..."
    echo "命令: $GST_LAUNCH playbin uri=\"$RTMP_URL\""
    echo ""
    echo "按 Ctrl+C 停止播放"
    sleep 2
    
    $GST_LAUNCH playbin uri="$RTMP_URL" 2>&1 || {
        echo "❌ playbin 播放失败"
    }
else
    echo "⚠️  未检测到显示设备 (DISPLAY 未设置)，跳过播放测试"
    echo ""
fi

echo "=========================================="
echo "  测试完成"
echo "=========================================="
echo ""
echo "如果所有方法都失败，可能原因："
echo "  1. ❌ RTMP 服务器未运行或 URL 错误/过期"
echo "  2. ❌ 推流端未启动或已停止推流"
echo "  3. ❌ 网络连接问题（防火墙/端口被阻止）"
echo "  4. ❌ H.264 编码参数不兼容（缺少 SPS/PPS）"
echo "  5. ❌ GStreamer 插件缺失 (rtmpsrc/flvdemux)"
echo ""
echo "✅ 调试建议："
echo "  1. 检查推流端日志（查看 GStreamer 错误信息）"
echo "  2. 使用 gst-inspect-1.0 检查插件:"
echo "     gst-inspect-1.0 rtmpsrc"
echo "     gst-inspect-1.0 flvdemux"
echo "     gst-inspect-1.0 h264parse"
echo "  3. 检查网络连通性:"
echo "     ping 27.223.85.130"
echo "     telnet 27.223.85.130 3519"
echo "  4. 查看 RTMP 服务器日志（如有权限）"
echo "  5. 确认推流端正在运行:"
echo "     ps aux | grep ESDK_SOPHON"
echo "  6. 检查推流端日志中的 GStreamer 消息:"
echo "     grep -i 'gstreamer\|rtmp\|error' logs/*.log"
echo ""
