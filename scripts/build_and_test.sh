#!/bin/bash

###############################################################################
# 编译测试脚本 - 事件推送接口更新验证
# 
# 功能:
# 1. 在 Docker 容器内交叉编译
# 2. 上传到 SE7 设备
# 3. 运行程序并查看日志
#
# 使用方法:
#   bash scripts/build_and_test.sh
#
# 作者: GitHub Copilot
# 日期: 2025-12-12
###############################################################################

set -e  # 遇到错误立即退出

echo "========================================="
echo "📦 事件推送接口更新 - 编译测试脚本"
echo "========================================="

# ==================== 配置 ====================
DOCKER_CONTAINER="stream_lzy"
WORKSPACE_DIR="/workspace"
BUILD_DIR="$WORKSPACE_DIR/build"
SE7_HOST="117.132.4.173"
SE7_PORT="6003"
SE7_USER="admin"
SE7_TARGET_DIR="/data/Edge-SDK/build/bin"

# ==================== 步骤 1: Docker 交叉编译 ====================
echo ""
echo "🔨 [步骤 1/4] 在 Docker 容器内编译..."
echo "  容器名: $DOCKER_CONTAINER"
echo "  工作目录: $WORKSPACE_DIR"

docker exec -i $DOCKER_CONTAINER /bin/bash << 'EOF'
cd /workspace/build

# 清理旧的构建产物 (可选,首次编译可注释掉)
# rm -rf *

# CMake 配置
echo "📝 CMake 配置..."
cmake ..

# 编译 (使用 8 个并行任务)
echo "🔧 开始编译..."
make -j8

# 检查编译结果
if [ -f bin/ESDK_Sophon ]; then
    echo "✅ 编译成功: bin/ESDK_Sophon"
    ls -lh bin/ESDK_Sophon
else
    echo "❌ 编译失败: bin/ESDK_Sophon 不存在"
    exit 1
fi
EOF

if [ $? -ne 0 ]; then
    echo "❌ Docker 编译失败,请检查错误信息"
    exit 1
fi

echo "✅ Docker 编译完成"

# ==================== 步骤 2: 上传到 SE7 设备 ====================
echo ""
echo "📤 [步骤 2/4] 上传到 SE7 设备..."
echo "  目标: $SE7_USER@$SE7_HOST:$SE7_PORT"

# 先备份旧的可执行文件 (可选)
ssh -p $SE7_PORT $SE7_USER@$SE7_HOST << 'EOF'
if [ -f /data/Edge-SDK/build/bin/ESDK_Sophon ]; then
    echo "📦 备份旧版本..."
    cp /data/Edge-SDK/build/bin/ESDK_Sophon /data/Edge-SDK/build/bin/ESDK_Sophon.bak
fi
EOF

# 上传新的可执行文件
scp -P $SE7_PORT \
    ./build/bin/ESDK_Sophon \
    $SE7_USER@$SE7_HOST:$SE7_TARGET_DIR/

if [ $? -ne 0 ]; then
    echo "❌ 上传失败,请检查 SSH 连接"
    exit 1
fi

echo "✅ 上传完成"

# ==================== 步骤 3: 创建可视化目录 ====================
echo ""
echo "📁 [步骤 3/4] 创建可视化保存目录..."

ssh -p $SE7_PORT $SE7_USER@$SE7_HOST << 'EOF'
mkdir -p /data/Edge-SDK/results/seg_vis
chmod 755 /data/Edge-SDK/results/seg_vis
echo "✅ 目录已创建: /data/Edge-SDK/results/seg_vis"
EOF

# ==================== 步骤 4: 运行测试 ====================
echo ""
echo "🚀 [步骤 4/4] 准备运行测试..."
echo ""
echo "========================================="
echo "✅ 编译和部署完成!"
echo "========================================="
echo ""
echo "下一步操作:"
echo "  1. SSH 登录到 SE7 设备:"
echo "     ssh -p $SE7_PORT $SE7_USER@$SE7_HOST"
echo ""
echo "  2. 运行主程序:"
echo "     cd $SE7_TARGET_DIR"
echo "     ./ESDK_Sophon"
echo ""
echo "  3. 查看日志 (另一个终端):"
echo "     tail -f /data/Edge-SDK/logs/esdk_sophon.log | grep -E '(分割|可视化|objects|resultImage)'"
echo ""
echo "  4. 查看可视化图片:"
echo "     ls -lh /data/Edge-SDK/results/seg_vis/"
echo ""
echo "  5. 下载可视化图片到本地查看:"
echo "     scp -P $SE7_PORT $SE7_USER@$SE7_HOST:/data/Edge-SDK/results/seg_vis/*.jpg ."
echo ""
echo "========================================="
echo "📋 验证清单:"
echo "  [ ] 1. MQTT JSON 格式正确 (包含 result.image 和 result.objects)"
echo "  [ ] 2. resultImage 是有效的 Base64 数据"
echo "  [ ] 3. objects 数组包含 label, bbox, mask 字段"
echo "  [ ] 4. 可视化图片已保存到 /data/Edge-SDK/results/seg_vis/"
echo "  [ ] 5. SAM2 分割轮廓清晰可见 (蓝色轮廓 + 绿色框)"
echo "========================================="
