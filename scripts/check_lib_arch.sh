#!/bin/bash

# ==============================================================================
# 第三方库架构检查脚本
# ==============================================================================
# 功能：检查third_party目录下的库是否都是ARM64架构
# 用法：bash scripts/check_lib_arch.sh
# ==============================================================================

echo "========================================"
echo "检查第三方库架构"
echo "========================================"

THIRD_PARTY_DIR="third_party"
ERROR_COUNT=0
CHECKED_COUNT=0

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查单个库文件
check_library() {
    local lib_file=$1
    local lib_name=$(basename "$lib_file")
    
    CHECKED_COUNT=$((CHECKED_COUNT + 1))
    
    # 使用file命令检查架构
    local arch_info=$(file "$lib_file")
    
    if echo "$arch_info" | grep -q "ARM aarch64"; then
        echo -e "${GREEN}✓${NC} $lib_name - ARM64"
    elif echo "$arch_info" | grep -q "x86-64"; then
        echo -e "${RED}✗${NC} $lib_name - x86_64 (错误！应该是ARM64)"
        ERROR_COUNT=$((ERROR_COUNT + 1))
    elif echo "$arch_info" | grep -q "Intel 80386"; then
        echo -e "${RED}✗${NC} $lib_name - x86 (错误！应该是ARM64)"
        ERROR_COUNT=$((ERROR_COUNT + 1))
    else
        echo -e "${YELLOW}?${NC} $lib_name - 未知架构: $arch_info"
    fi
}

# 查找所有.so和.a文件
echo ""
echo "检查动态库 (.so)..."
echo "----------------------------------------"
find "$THIRD_PARTY_DIR" -name "*.so" -type f | while read lib; do
    check_library "$lib"
done

echo ""
echo "检查静态库 (.a)..."
echo "----------------------------------------"
find "$THIRD_PARTY_DIR" -name "*.a" -type f | while read lib; do
    check_library "$lib"
done

echo ""
echo "========================================"
echo "检查完成"
echo "========================================"
echo "总共检查: $CHECKED_COUNT 个库文件"
echo "错误数量: $ERROR_COUNT 个"

if [ $ERROR_COUNT -eq 0 ]; then
    echo -e "${GREEN}✓ 所有库都是ARM64架构！${NC}"
    exit 0
else
    echo -e "${RED}✗ 发现 $ERROR_COUNT 个非ARM64库！${NC}"
    echo ""
    echo "解决方案："
    echo "1. 重新下载ARM64版本的库"
    echo "2. 或者使用静态链接避免部署问题"
    exit 1
fi
