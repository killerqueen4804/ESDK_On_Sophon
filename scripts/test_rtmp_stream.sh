#!/bin/bash
#
# RTMP 推流测试脚本
# 使用 FFmpeg/VLC 验证 RTMP 流是否可播放
#
# 使用方法:
#   bash test_rtmp_stream.sh <RTMP_URL>
#
# 示例:
#   bash test_rtmp_stream.sh "rtmp://27.223.85.130:3519/live/u25IGQMHg?sign=9hcIMwaNR"

set -e

RTMP_URL="$1"

if [ -z "$RTMP_URL" ]; then
    echo "错误: 未提供 RTMP URL"
    echo "使用方法: $0 <RTMP_URL>"
    exit 1
fi

echo "=== RTMP 推流测试 ==="
echo "URL: $RTMP_URL"
echo ""

# 方法 1: 使用 ffprobe 分析流（不播放）
echo "=== 方法 1: ffprobe 分析流 ==="
echo "命令: ffprobe -v error -show_format -show_streams \"$RTMP_URL\""
echo ""

if command -v ffprobe &> /dev/null; then
    timeout 10s ffprobe -v error -show_format -show_streams "$RTMP_URL" 2>&1 || {
        echo "❌ ffprobe 分析失败（可能流未启动或 URL 错误）"
    }
else
    echo "⚠️  未安装 ffprobe，跳过"
fi

echo ""
echo "=== 方法 2: ffplay 播放流 ==="
echo "命令: ffplay -v warning \"$RTMP_URL\""
echo ""

if command -v ffplay &> /dev/null; then
    echo "即将开始播放（Ctrl+C 停止）..."
    sleep 2
    ffplay -v warning "$RTMP_URL" || {
        echo "❌ ffplay 播放失败"
    }
else
    echo "⚠️  未安装 ffplay，跳过"
fi

echo ""
echo "=== 方法 3: VLC 播放 ==="
echo "命令: vlc \"$RTMP_URL\""
echo ""

if command -v vlc &> /dev/null; then
    echo "启动 VLC 播放器..."
    vlc "$RTMP_URL" &
    echo "VLC 已在后台启动（PID: $!）"
else
    echo "⚠️  未安装 VLC，跳过"
fi

echo ""
echo "=== 测试完成 ==="
echo ""
echo "如果所有方法都失败，可能原因："
echo "  1. RTMP 服务器未运行或 URL 错误"
echo "  2. 推流端未启动或已停止"
echo "  3. 网络连接问题（防火墙/端口）"
echo "  4. H.264 编码参数不兼容"
echo ""
echo "调试建议："
echo "  - 检查 RTMP 服务器日志"
echo "  - 检查推流端日志（GStreamer 错误）"
echo "  - 使用 Wireshark 抓包分析 RTMP 握手"
