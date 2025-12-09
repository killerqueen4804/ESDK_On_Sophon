#!/bin/bash
# 
# 网络参数优化脚本（用于 DJI Liveview）
# 需要 root 权限运行: sudo ./setup_network.sh
#
# 功能：增加 UDP 接收缓冲区，防止高码率视频流丢包

set -e

echo "=== DJI Liveview 网络参数优化 ==="

# 检查 root 权限
if [ "$EUID" -ne 0 ]; then 
    echo "错误: 请使用 root 权限运行"
    echo "执行: sudo $0"
    exit 1
fi

# 当前值
echo "当前配置:"
echo "  rmem_default: $(cat /proc/sys/net/core/rmem_default)"
echo "  rmem_max:     $(cat /proc/sys/net/core/rmem_max)"

# 推荐值（根据 DJI 文档调整）
RMEM_DEFAULT=26214400   # 25 MB
RMEM_MAX=26214400       # 25 MB

# 设置网络参数
echo ""
echo "设置网络参数..."
echo $RMEM_DEFAULT > /proc/sys/net/core/rmem_default
echo $RMEM_MAX > /proc/sys/net/core/rmem_max

echo "✅ 设置成功:"
echo "  rmem_default: $(cat /proc/sys/net/core/rmem_default)"
echo "  rmem_max:     $(cat /proc/sys/net/core/rmem_max)"

# 持久化配置（重启后生效）
echo ""
echo "持久化配置到 /etc/sysctl.conf..."
if ! grep -q "net.core.rmem_default" /etc/sysctl.conf; then
    echo "net.core.rmem_default = $RMEM_DEFAULT" >> /etc/sysctl.conf
    echo "net.core.rmem_max = $RMEM_MAX" >> /etc/sysctl.conf
    echo "✅ 配置已添加（重启后自动生效）"
else
    echo "⚠️  配置已存在，跳过"
fi

echo ""
echo "=== 配置完成 ==="
echo "现在可以用普通用户运行程序了（无需 sudo）"
