#!/bin/bash
#
# 编译 RTMP 播放测试程序

echo "=== 编译 RTMP 播放测试程序 ==="
echo ""

# 检查是否在 SE7 上
if [ ! -f "/proc/cpuinfo" ] || ! grep -q "aarch64" /proc/cpuinfo; then
    echo "⚠️  警告: 似乎不在 SE7 设备上"
    echo "   请在 SE7 上编译并运行此程序"
    echo ""
fi

# 检查 pkg-config
if ! command -v pkg-config &> /dev/null; then
    echo "❌ pkg-config 未安装"
    echo "   sudo apt-get install pkg-config"
    exit 1
fi

# 检查 GStreamer 开发文件
if ! pkg-config --exists gstreamer-1.0; then
    echo "❌ GStreamer 开发文件未安装"
    echo "   sudo apt-get install libgstreamer1.0-dev"
    exit 1
fi

# 编译
echo "📌 编译中..."
g++ -std=c++17 \
    test_rtmp_playback.cpp \
    -o test_rtmp_playback \
    $(pkg-config --cflags --libs gstreamer-1.0) \
    -lpthread

if [ $? -eq 0 ]; then
    echo "✅ 编译成功!"
    echo ""
    echo "运行测试:"
    echo "  ./test_rtmp_playback \"rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR\""
    echo ""
else
    echo "❌ 编译失败"
    exit 1
fi
