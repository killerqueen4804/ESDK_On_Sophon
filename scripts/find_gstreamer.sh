#!/bin/bash
#
# 查找 GStreamer 工具的实际位置

echo "=== 查找 GStreamer 工具 ==="
echo ""

echo "1. 搜索 gst-inspect-1.0:"
find /usr -name "gst-inspect-1.0" 2>/dev/null
find /opt -name "gst-inspect-1.0" 2>/dev/null
find /data -name "gst-inspect-1.0" 2>/dev/null
echo ""

echo "2. 搜索 gst-launch-1.0:"
find /usr -name "gst-launch-1.0" 2>/dev/null
find /opt -name "gst-launch-1.0" 2>/dev/null
find /data -name "gst-launch-1.0" 2>/dev/null
echo ""

echo "3. 检查常见路径:"
for path in /usr/bin /usr/local/bin /opt/sophon/libsophon-current/bin /data/gst/bin; do
    if [ -d "$path" ]; then
        echo "检查 $path:"
        ls -la "$path" | grep gst- | head -5
    fi
done
echo ""

echo "4. 检查 GStreamer 库文件:"
ldconfig -p | grep gstreamer | head -10
echo ""

echo "5. 当前 PATH:"
echo $PATH
