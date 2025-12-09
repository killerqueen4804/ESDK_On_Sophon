#!/bin/bash
# GStreamer 插件诊断和修复脚本
# 适用于 SE7 ARM64 设备

echo "========================================"
echo "GStreamer 插件诊断工具 v1.0"
echo "========================================"
echo ""

# ============================================================================
# 1. 检查已安装的 GStreamer 包
# ============================================================================
echo "【1/6】检查已安装的 GStreamer 包..."
echo "----------------------------------------"
PLUGINS_BASE=$(dpkg -l | grep "gstreamer1.0-plugins-base" | awk '{print $2}')
PLUGINS_GOOD=$(dpkg -l | grep "gstreamer1.0-plugins-good" | awk '{print $2}')
PLUGINS_BAD=$(dpkg -l | grep "gstreamer1.0-plugins-bad" | awk '{print $2}')

if [ -n "$PLUGINS_BASE" ]; then
    echo "✓ gstreamer1.0-plugins-base: $PLUGINS_BASE"
else
    echo "✗ gstreamer1.0-plugins-base: 未安装"
fi

if [ -n "$PLUGINS_GOOD" ]; then
    echo "✓ gstreamer1.0-plugins-good: $PLUGINS_GOOD"
else
    echo "✗ gstreamer1.0-plugins-good: 未安装"
fi

if [ -n "$PLUGINS_BAD" ]; then
    echo "✓ gstreamer1.0-plugins-bad: $PLUGINS_BAD"
else
    echo "✗ gstreamer1.0-plugins-bad: 未安装 ← 缺失关键包！"
    MISSING_BAD=1
fi

echo ""

# ============================================================================
# 2. 检查关键插件文件
# ============================================================================
echo "【2/6】检查关键插件文件..."
echo "----------------------------------------"

PLUGIN_DIR="/usr/lib/aarch64-linux-gnu/gstreamer-1.0"

echo "插件目录: $PLUGIN_DIR"
echo ""

# 检查 h264parse（在 libgstvideoparsersbad.so 中）
if [ -f "$PLUGIN_DIR/libgstvideoparsersbad.so" ]; then
    echo "✓ h264parse 插件: $(ls -lh $PLUGIN_DIR/libgstvideoparsersbad.so | awk '{print $5}')"
else
    echo "✗ h264parse 插件: 未找到 libgstvideoparsersbad.so"
    MISSING_H264PARSE=1
fi

# 检查 flvmux（在 libgstflv.so 中）
if [ -f "$PLUGIN_DIR/libgstflv.so" ]; then
    echo "✓ flvmux 插件: $(ls -lh $PLUGIN_DIR/libgstflv.so | awk '{print $5}')"
else
    echo "✗ flvmux 插件: 未找到 libgstflv.so"
    MISSING_FLVMUX=1
fi

# 检查 rtmpsink（在 libgstrtmp.so 中）
if [ -f "$PLUGIN_DIR/libgstrtmp.so" ]; then
    echo "✓ rtmpsink 插件: $(ls -lh $PLUGIN_DIR/libgstrtmp.so | awk '{print $5}')"
else
    echo "✗ rtmpsink 插件: 未找到 libgstrtmp.so"
    MISSING_RTMPSINK=1
fi

echo ""

# ============================================================================
# 3. 检查旧项目插件（备选方案）
# ============================================================================
echo "【3/6】检查旧项目插件（备选方案）..."
echo "----------------------------------------"

OLD_PROJECT_PLUGIN="/data/ESDK_On_Sophon_old/third_party/gst/lib"

if [ -d "$OLD_PROJECT_PLUGIN" ]; then
    SO_COUNT=$(find "$OLD_PROJECT_PLUGIN" -name "*.so" | wc -l)
    echo "✓ 旧项目插件目录存在: $OLD_PROJECT_PLUGIN"
    echo "  插件数量: $SO_COUNT 个"
    
    # 检查关键插件
    if find "$OLD_PROJECT_PLUGIN" -name "*h264parse*" -o -name "*videoparser*" | grep -q .; then
        echo "  ✓ 包含 h264parse 相关插件"
        HAS_OLD_H264PARSE=1
    fi
else
    echo "✗ 旧项目插件目录不存在"
fi

echo ""

# ============================================================================
# 4. 提供修复建议
# ============================================================================
echo "【4/6】诊断结果与修复建议..."
echo "========================================"

if [ -z "$MISSING_BAD" ] && [ -z "$MISSING_H264PARSE" ] && [ -z "$MISSING_RTMPSINK" ]; then
    echo "✅ 所有必需插件都已安装！"
    echo ""
    echo "请重新编译并运行程序："
    echo "  cd /data/Edge-SDK/build"
    echo "  make -j4"
    echo "  ./bin/your_program"
    exit 0
fi

echo "⚠️  检测到缺失的插件！"
echo ""
echo "缺失内容："
[ -n "$MISSING_BAD" ] && echo "  - gstreamer1.0-plugins-bad 包"
[ -n "$MISSING_H264PARSE" ] && echo "  - h264parse 插件"
[ -n "$MISSING_RTMPSINK" ] && echo "  - rtmpsink 插件"
echo ""

echo "=========================================="
echo "【方案 1】安装官方插件包（推荐）"
echo "=========================================="
echo ""
echo "执行以下命令："
echo ""
echo "  sudo apt-get update"
echo "  sudo apt-get install -y gstreamer1.0-plugins-bad:arm64"
echo ""

if [ -n "$HAS_OLD_H264PARSE" ]; then
    echo "=========================================="
    echo "【方案 2】使用旧项目插件（离线）"
    echo "=========================================="
    echo ""
    echo "执行以下命令："
    echo ""
    echo "  mkdir -p /data/Edge-SDK/third_party/gst/lib/gstreamer-1.0"
    echo "  cp $OLD_PROJECT_PLUGIN/*.so \\"
    echo "     /data/Edge-SDK/third_party/gst/lib/gstreamer-1.0/"
    echo ""
    echo "然后重新编译程序（代码已自动配置路径）"
    echo ""
fi

# ============================================================================
# 5. 可选：一键安装
# ============================================================================
echo ""
echo "【5/6】一键修复选项..."
echo "----------------------------------------"
read -p "是否立即安装 gstreamer1.0-plugins-bad？(y/n) " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "正在安装..."
    sudo apt-get update
    sudo apt-get install -y gstreamer1.0-plugins-bad:arm64
    
    echo ""
    echo "安装完成！请重新运行诊断脚本验证。"
else
    echo "跳过安装。"
fi

echo ""
echo "========================================"
echo "【6/6】诊断完成"
echo "========================================"
echo ""
echo "如有问题，请查看文档："
echo "  /data/Edge-SDK/docs/issues/GStreamer插件路径问题.md"
echo ""
