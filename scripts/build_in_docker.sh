#!/bin/bash
###############################################################################
# 文件名: build_in_docker.sh
# 用途: 在Docker容器中交叉编译项目的便捷脚本
# 作者: ESDK_Sophon Team
# 日期: 2025-10-25
#
# 使用方法:
#   1. 在宿主机Windows上执行（进入Docker）:
#      docker exec -it stream_lzy /bin/bash
#   
#   2. 在容器内执行此脚本:
#      cd /workspace
#      bash scripts/build_in_docker.sh
#
# 或者一行命令:
#   docker exec -it stream_lzy bash -c "cd /workspace && bash scripts/build_in_docker.sh"
###############################################################################

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的信息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 打印分隔线
print_separator() {
    echo "=========================================================================="
}

###############################################################################
# 1. 环境检查
###############################################################################
print_separator
print_info "开始环境检查..."
print_separator

# 检查是否在容器内
if [ ! -d "/workspace" ]; then
    print_error "错误: 不在Docker容器内，或者/workspace未挂载！"
    print_info "请先执行: docker exec -it stream_lzy /bin/bash"
    exit 1
fi

# 检查是否在正确的目录
if [ ! -f "CMakeLists.txt" ]; then
    print_error "错误: 当前目录不是项目根目录！"
    print_info "请先执行: cd /workspace"
    exit 1
fi

# 检查工具链文件是否存在
if [ ! -f "cmake/toolchain-arm64.cmake" ]; then
    print_error "错误: 工具链文件不存在！"
    print_info "文件路径: cmake/toolchain-arm64.cmake"
    exit 1
fi

print_success "环境检查通过！"

###############################################################################
# 2. 检查第三方库架构（可选）
###############################################################################
print_separator
print_info "检查第三方库架构（可选，按Ctrl+C跳过）..."
print_separator

if [ -f "scripts/check_lib_arch.sh" ]; then
    read -p "是否检查第三方库架构？(y/n) [默认: n]: " -t 10 check_lib || check_lib="n"
    echo
    if [ "$check_lib" = "y" ] || [ "$check_lib" = "Y" ]; then
        bash scripts/check_lib_arch.sh
    else
        print_info "跳过库架构检查"
    fi
else
    print_warning "check_lib_arch.sh 不存在，跳过"
fi

###############################################################################
# 3. 清理旧的构建目录（可选）
###############################################################################
print_separator
print_info "构建目录处理..."
print_separator

if [ -d "build" ]; then
    print_warning "build目录已存在"
    read -p "是否清理重新构建？(y/n) [默认: n]: " -t 10 clean_build || clean_build="n"
    echo
    if [ "$clean_build" = "y" ] || [ "$clean_build" = "Y" ]; then
        print_info "清理build目录..."
        rm -rf build
        print_success "build目录已清理"
    else
        print_info "保留现有build目录"
    fi
fi

###############################################################################
# 4. 创建构建目录
###############################################################################
print_separator
print_info "创建构建目录..."
print_separator

mkdir -p build
cd build

print_success "构建目录准备完成: $(pwd)"

###############################################################################
# 5. CMake配置
###############################################################################
print_separator
print_info "开始CMake配置（交叉编译）..."
print_separator

# 选择构建类型
read -p "选择构建类型 (Debug/Release) [默认: Release]: " -t 10 build_type || build_type="Release"
echo
build_type=${build_type:-Release}

print_info "构建类型: $build_type"
print_info "工具链文件: ../cmake/toolchain-arm64.cmake"

# 执行CMake配置
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake \
    -DCMAKE_BUILD_TYPE=$build_type

if [ $? -eq 0 ]; then
    print_success "CMake配置成功！"
else
    print_error "CMake配置失败！"
    exit 1
fi

###############################################################################
# 6. 编译
###############################################################################
print_separator
print_info "开始编译..."
print_separator

# 获取CPU核心数
NPROC=$(nproc)
print_info "使用 $NPROC 个线程并行编译"

# 执行编译
cmake --build . --parallel $NPROC

if [ $? -eq 0 ]; then
    print_success "编译成功！"
else
    print_error "编译失败！"
    exit 1
fi

###############################################################################
# 7. 验证编译结果
###############################################################################
print_separator
print_info "验证编译结果..."
print_separator

# 检查可执行文件是否存在
if [ -f "bin/ESDK_Sophon" ]; then
    print_success "可执行文件生成成功: bin/ESDK_Sophon"
    
    # 检查架构
    print_info "检查可执行文件架构..."
    file bin/ESDK_Sophon
    
    # 检查是否是ARM64
    if file bin/ESDK_Sophon | grep -q "ARM aarch64"; then
        print_success "✅ 架构正确: ARM64"
    elif file bin/ESDK_Sophon | grep -q "x86-64"; then
        print_error "❌ 架构错误: x86-64 (应该是ARM64)"
        print_warning "可能原因: 未使用交叉编译工具链"
    else
        print_warning "⚠️  无法识别架构"
    fi
else
    print_error "可执行文件未生成！"
    exit 1
fi

# 检查库文件
print_info "检查生成的库文件..."
if [ -d "lib" ]; then
    ls -lh lib/
else
    print_warning "lib目录不存在（可能所有模块都是静态库并已链接到可执行文件）"
fi

###############################################################################
# 8. 完成
###############################################################################
print_separator
print_success "🎉 编译完成！"
print_separator

echo ""
print_info "编译产物位置:"
echo "  - 可执行文件: build/bin/ESDK_Sophon"
echo "  - 库文件: build/lib/"
echo ""

print_info "下一步操作:"
echo "  1. 部署到SE7设备:"
echo "     scp bin/ESDK_Sophon user@se7:/opt/esdk/"
echo ""
echo "  2. 在SE7上运行:"
echo "     ssh user@se7 \"/opt/esdk/ESDK_Sophon\""
echo ""

print_separator

exit 0
