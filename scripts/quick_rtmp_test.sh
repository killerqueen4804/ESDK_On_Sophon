#!/bin/bash
#
# 📌 RTMP 推流快速测试脚本
# 快速验证 RTMP 流是否可用（10秒测试）
#
# 使用方法:
#   bash quick_rtmp_test.sh <RTMP_URL>

# ============================================
# 📌 自动查找 GStreamer 工具路径
# ============================================
GST_LAUNCH=""

if [ -f "/data/gst/bin/gst-launch-1.0" ]; then
    GST_LAUNCH="/data/gst/bin/gst-launch-1.0"
elif command -v gst-launch-1.0 &> /dev/null; then
    GST_LAUNCH="gst-launch-1.0"
else
    for path in /usr/bin /usr/local/bin /opt/sophon/libsophon-current/bin; do
        if [ -f "$path/gst-launch-1.0" ]; then
            GST_LAUNCH="$path/gst-launch-1.0"
            break
        fi
    done
fi

if [ -z "$GST_LAUNCH" ]; then
    echo "❌ 错误: 未找到 gst-launch-1.0"
    echo "请先运行: bash find_gstreamer.sh"
    exit 1
fi

RTMP_URL="$1"

if [ -z "$RTMP_URL" ]; then
    echo "使用方法: $0 <RTMP_URL>"
    exit 1
fi

echo "🔍 快速测试 RTMP 流..."
echo "URL: $RTMP_URL"
echo ""

# 测试 1: 检查网络连通性
echo "1️⃣ 检查网络连通性..."
HOST=$(echo "$RTMP_URL" | sed -n 's|rtmp://\([^:/]*\).*|\1|p')
PORT=$(echo "$RTMP_URL" | sed -n 's|rtmp://[^:]*:\([0-9]*\).*|\1|p')
PORT=${PORT:-1935}  # RTMP 默认端口 1935

echo "   主机: $HOST"
echo "   端口: $PORT"

if timeout 3s bash -c "echo > /dev/tcp/$HOST/$PORT" 2>/dev/null; then
    echo "   ✅ 网络连通"
else
    echo "   ❌ 网络不通（服务器未运行或端口被阻止）"
    exit 1
fi
echo ""

# 测试 2: 使用 GStreamer 尝试连接
echo "2️⃣ 使用 GStreamer 测试连接（10秒）..."
echo "   工具路径: $GST_LAUNCH"
echo ""

timeout 10s $GST_LAUNCH \
    rtmpsrc location="$RTMP_URL" ! \
    flvdemux ! \
    h264parse ! \
    fakesink silent=true 2>&1 | grep -i "error\|warning\|state" | head -20

RESULT=$?

echo ""
if [ $RESULT -eq 0 ] || [ $RESULT -eq 124 ]; then
    echo "✅ RTMP 流连接成功!"
    echo "   (timeout 正常,说明流在持续接收数据)"
else
    echo "❌ RTMP 流连接失败 (退出码: $RESULT)"
fi

echo ""
echo "💡 提示:"
echo "   - 如果网络通但连接失败,可能是 URL 错误或流未启动"
echo "   - 如果网络不通,检查防火墙和服务器状态"
echo "   - 查看详细日志: tail -100 logs/esdk_sophon_*.log"
