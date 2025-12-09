#!/bin/bash
# ============================================
# 检测器诊断脚本
# 用于排查 LiveStreamTask 检测不工作的问题
# ============================================

echo "=========================================="
echo "🔍 检测器诊断脚本"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ============================================
# 1. 检查检测器初始化日志
# ============================================
echo "【步骤 1】检查检测器初始化状态"
echo "-------------------------------------------"

if grep -q "✅ PP-YOLOE检测器初始化完成" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null; then
    echo -e "${GREEN}✅ 检测器初始化成功${NC}"
else
    echo -e "${RED}❌ 检测器初始化失败或未找到日志${NC}"
fi

echo ""

# ============================================
# 2. 检查 processFrame 调用
# ============================================
echo "【步骤 2】检查 processFrame 是否被调用"
echo "-------------------------------------------"

# 搜索 processFrame 相关日志
PROCESS_FRAME_COUNT=$(grep -c "processFrame" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null || echo "0")
echo "processFrame 调用次数: $PROCESS_FRAME_COUNT"

if [ "$PROCESS_FRAME_COUNT" -eq 0 ]; then
    echo -e "${RED}⚠️ processFrame 从未被调用！${NC}"
    echo ""
    echo "可能原因："
    echo "  1. 异步检测线程没有收到帧"
    echo "  2. newFrameAvailable 标志没有被设置"
    echo "  3. 条件变量等待超时"
fi

echo ""

# ============================================
# 3. 检查异步检测线程状态
# ============================================
echo "【步骤 3】检查异步检测线程"
echo "-------------------------------------------"

if grep -q "🕵️ 异步检测线程已启动" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null; then
    echo -e "${GREEN}✅ 异步检测线程已启动${NC}"
else
    echo -e "${RED}❌ 异步检测线程未启动${NC}"
fi

# 检查是否有帧提交到检测线程
FRAME_SUBMIT_COUNT=$(grep -c "提交.*检测" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null || echo "0")
echo "帧提交到检测线程次数: $FRAME_SUBMIT_COUNT"

if [ "$FRAME_SUBMIT_COUNT" -eq 0 ]; then
    echo -e "${YELLOW}⚠️ 没有帧被提交到检测线程${NC}"
fi

echo ""

# ============================================
# 4. 检查检测结果
# ============================================
echo "【步骤 4】检查检测结果"
echo "-------------------------------------------"

DETECTION_RESULT_COUNT=$(grep -c "检测结果" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null || echo "0")
echo "检测结果日志数量: $DETECTION_RESULT_COUNT"

DETECTION_BOX_COUNT=$(grep -c "检测到.*个目标" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null || echo "0")
echo "检测到目标的次数: $DETECTION_BOX_COUNT"

if [ "$DETECTION_BOX_COUNT" -eq 0 ]; then
    echo -e "${YELLOW}⚠️ 从未检测到任何目标${NC}"
    echo ""
    echo "可能原因："
    echo "  1. 画面中确实没有 COCO 类别的物体"
    echo "  2. 置信度阈值设置过高（当前: 0.5）"
    echo "  3. 检测器没有真正执行推理"
fi

echo ""

# ============================================
# 5. 检查 TaskService::processFrame
# ============================================
echo "【步骤 5】检查 TaskService::processFrame 日志"
echo "-------------------------------------------"

SERVICE_PROCESS_COUNT=$(grep -c "TaskService.*processFrame" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null || echo "0")
echo "TaskService::processFrame 日志数量: $SERVICE_PROCESS_COUNT"

if [ "$SERVICE_PROCESS_COUNT" -eq 0 ]; then
    echo -e "${RED}❌ TaskService::processFrame 没有日志输出${NC}"
    echo ""
    echo "关键问题：这说明检测函数根本没有被调用！"
fi

echo ""

# ============================================
# 6. 统计关键信息
# ============================================
echo "【步骤 6】统计关键信息"
echo "-------------------------------------------"

TOTAL_FRAMES=$(grep "接收帧数:" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null | tail -1 | awk '{print $NF}')
DECODED_FRAMES=$(grep "解码成功:" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null | tail -1 | awk '{print $NF}')
DETECTED_FRAMES=$(grep "检测成功:" /data/Edge-SDK/build/bin/logs/*.log 2>/dev/null | tail -1 | awk '{print $NF}')

echo "最新统计数据："
echo "  接收帧数: ${TOTAL_FRAMES:-未知}"
echo "  解码成功: ${DECODED_FRAMES:-未知}"
echo "  检测成功: ${DETECTED_FRAMES:-0}"

echo ""

# ============================================
# 7. 诊断结论
# ============================================
echo "=========================================="
echo "📋 诊断结论"
echo "=========================================="

if [ "$DETECTED_FRAMES" == "0" ] || [ -z "$DETECTED_FRAMES" ]; then
    echo -e "${RED}问题确认：检测功能未正常工作${NC}"
    echo ""
    echo "根据日志分析，最可能的原因是："
    echo ""
    echo "【核心问题】异步检测线程没有收到帧"
    echo ""
    echo "检查代码逻辑："
    echo "  1. execute() 中的 try_lock 可能一直失败"
    echo "  2. newFrameAvailable 标志可能一直为 true（上一帧未被取走）"
    echo "  3. detectionLoop() 可能在 wait 中阻塞"
    echo ""
    echo "建议的修复方案："
    echo "  → 添加详细日志，确认帧是否被提交到检测队列"
    echo "  → 检查 try_lock 的成功率"
    echo "  → 验证 newFrameAvailable 的状态变化"
else
    echo -e "${GREEN}✅ 检测功能正常工作${NC}"
fi

echo ""
echo "=========================================="
echo "下一步操作建议："
echo "=========================================="
echo ""
echo "1. 查看完整日志："
echo "   $ tail -f /data/Edge-SDK/build/bin/logs/*.log | grep -E \"检测|processFrame|异步\""
echo ""
echo "2. 添加诊断日志（需要修改代码）："
echo "   → 在 execute() 的 try_lock 前后添加日志"
echo "   → 在 detectionLoop() 的 wait 前后添加日志"
echo ""
echo "3. 如果需要帮助，提供以下信息："
echo "   → 完整的日志文件"
echo "   → LiveStreamTask.cpp 的 execute() 和 detectionLoop() 代码"
echo ""
