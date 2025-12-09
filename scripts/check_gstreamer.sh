#!/bin/bash
#
# 📌 GStreamer 环境诊断脚本
# 检查 GStreamer 安装、插件和 RTMP 支持
#
# 使用方法:
#   bash check_gstreamer.sh

# ============================================
# 📌 自动查找 GStreamer 工具路径
# ============================================
GST_INSPECT=""
GST_LAUNCH=""
GST_DISCOVERER=""

# 尝试从项目的 third_party 目录查找
if [ -f "/data/gst/bin/gst-inspect-1.0" ]; then
    GST_INSPECT="/data/gst/bin/gst-inspect-1.0"
    GST_LAUNCH="/data/gst/bin/gst-launch-1.0"
    GST_DISCOVERER="/data/gst/bin/gst-discoverer-1.0"
# 尝试从系统路径查找
elif command -v gst-inspect-1.0 &> /dev/null; then
    GST_INSPECT="gst-inspect-1.0"
    GST_LAUNCH="gst-launch-1.0"
    GST_DISCOVERER="gst-discoverer-1.0"
# 尝试从常见路径查找
else
    for path in /usr/bin /usr/local/bin /opt/sophon/libsophon-current/bin; do
        if [ -f "$path/gst-inspect-1.0" ]; then
            GST_INSPECT="$path/gst-inspect-1.0"
            GST_LAUNCH="$path/gst-launch-1.0"
            GST_DISCOVERER="$path/gst-discoverer-1.0"
            break
        fi
    done
fi

echo "=========================================="
echo "  GStreamer 环境诊断"
echo "=========================================="
echo ""

# 显示使用的工具路径
if [ -n "$GST_INSPECT" ]; then
    echo "📌 使用的 GStreamer 工具:"
    echo "   gst-inspect: $GST_INSPECT"
    echo "   gst-launch: $GST_LAUNCH"
    echo ""
else
    echo "❌ 错误: 未找到 GStreamer 工具"
    echo ""
    echo "请尝试以下方法:"
    echo "  1. 运行 bash find_gstreamer.sh 查找工具位置"
    echo "  2. 手动设置 PATH: export PATH=/data/gst/bin:\$PATH"
    echo "  3. 检查 GStreamer 是否已正确安装"
    exit 1
fi
echo ""

# 1. GStreamer 版本
echo "1️⃣ GStreamer 版本:"
if [ -n "$GST_LAUNCH" ]; then
    $GST_LAUNCH --version
    echo "   ✅ gst-launch-1.0 已安装"
else
    echo "   ❌ gst-launch-1.0 未找到"
fi
echo ""

# 2. GStreamer 工具
echo "2️⃣ GStreamer 工具:"
for tool_name in "gst-inspect-1.0" "gst-discoverer-1.0" "gst-launch-1.0"; do
    tool_path=""
    case $tool_name in
        "gst-inspect-1.0") tool_path="$GST_INSPECT" ;;
        "gst-discoverer-1.0") tool_path="$GST_DISCOVERER" ;;
        "gst-launch-1.0") tool_path="$GST_LAUNCH" ;;
    esac
    
    if [ -n "$tool_path" ] && [ -f "$tool_path" ]; then
        echo "   ✅ $tool_name ($tool_path)"
    else
        echo "   ❌ $tool_name (未找到)"
    fi
done
echo ""

# 3. 关键插件检查
echo "3️⃣ RTMP 推流关键插件:"
REQUIRED_PLUGINS="rtmpsink rtmpsrc flvmux flvdemux h264parse appsrc fakesink"

for plugin in $REQUIRED_PLUGINS; do
    if [ -n "$GST_INSPECT" ] && $GST_INSPECT $plugin &> /dev/null; then
        echo "   ✅ $plugin"
    else
        echo "   ❌ $plugin (未安装或未找到)"
    fi
done
echo ""

# 4. 插件总数
echo "4️⃣ 已安装插件总数:"
if [ -n "$GST_INSPECT" ]; then
    PLUGIN_COUNT=$($GST_INSPECT | grep -c "^[a-z]")
    echo "   共 $PLUGIN_COUNT 个插件"
else
    echo "   无法统计（gst-inspect 不可用）"
fi
echo ""

# 5. GStreamer 调试级别
echo "5️⃣ 当前 GStreamer 调试级别:"
if [ -z "$GST_DEBUG" ]; then
    echo "   GST_DEBUG 未设置 (默认级别)"
else
    echo "   GST_DEBUG=$GST_DEBUG"
fi
echo ""

# 6. 插件路径
echo "6️⃣ GStreamer 插件路径:"
if [ -z "$GST_PLUGIN_PATH" ]; then
    echo "   GST_PLUGIN_PATH 未设置 (使用系统默认)"
else
    echo "   GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
fi

# 尝试找到系统插件路径
echo ""
echo "   系统插件目录:"
for path in /usr/lib/gstreamer-1.0 /usr/lib/aarch64-linux-gnu/gstreamer-1.0 /usr/local/lib/gstreamer-1.0; do
    if [ -d "$path" ]; then
        COUNT=$(ls -1 "$path" 2>/dev/null | wc -l)
        echo "   ✅ $path ($COUNT 个插件)"
    fi
done
echo ""

# 7. 详细插件信息（可选）
echo "7️⃣ RTMP 推流插件详细信息:"
echo ""
if [ -n "$GST_INSPECT" ]; then
    echo "--- rtmpsink ---"
    $GST_INSPECT rtmpsink 2>&1 | head -20 || echo "   ❌ rtmpsink 不可用"
    echo ""

    echo "--- h264parse ---"
    $GST_INSPECT h264parse 2>&1 | head -20 || echo "   ❌ h264parse 不可用"
    echo ""
else
    echo "   ⚠️  gst-inspect 不可用，跳过详细信息"
    echo ""
fi

echo "=========================================="
echo "  诊断完成"
echo "=========================================="
echo ""
echo "💡 如果发现缺失插件，安装方法:"
echo "   sudo apt-get update"
echo "   sudo apt-get install gstreamer1.0-plugins-base"
echo "   sudo apt-get install gstreamer1.0-plugins-good"
echo "   sudo apt-get install gstreamer1.0-plugins-bad"
echo "   sudo apt-get install gstreamer1.0-plugins-ugly"
echo "   sudo apt-get install gstreamer1.0-rtsp"
echo ""
