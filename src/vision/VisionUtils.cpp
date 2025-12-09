/**
 * @file VisionUtils.cpp
 * @brief Vision 模块统一工具类实现
 * 
 * 本文件实现了深度学习推理所需的所有工具方法。
 * 
 * 实现优先级:
 * - P0 (核心方法): letterbox, nms, iou, inverseTransform
 * - P1 (重要方法): normalize, hwcToChw, clipBoxes, filterSmallBoxes, 坐标转换
 * - P2 (可选方法): giou, diou, ciou, mAP, softNms
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-22
 */

#include "esdk_sophon/vision/VisionUtils.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace esdk_sophon {
namespace vision {

// ==================== 📦 P0 核心方法实现 ====================

/**
 * @brief Letterbox 预处理（简化版）
 * 
 * 实现要点:
 * 1. 计算缩放比例 - 使用 min 保证不裁剪
 * 2. 按比例缩放 - 保持宽高比
 * 3. 创建画布并填充灰色 - 训练时的标准做法
 * 4. 居中放置缩放后的图像
 * 5. 记录变换参数供后处理使用
 * 
 * 时间复杂度: O(W*H) - 主要是 cv::resize 的开销
 * 空间复杂度: O(W*H) - 需要创建新的图像
 */
cv::Mat VisionUtils::letterbox(const cv::Mat& image, 
                               const cv::Size& targetSize,
                               LetterboxTransform& transform) {
    // 【步骤 1】边界检查
    if (image.empty()) {
        throw std::invalid_argument("VisionUtils::letterbox: Input image is empty");
    }
    
    // 【步骤 2】计算缩放比例
    // 面试要点: 为什么用 min？
    // 答: min 保证长边缩放到目标尺寸，短边不会超出，图像完整不被裁剪
    float scale = std::min(
        static_cast<float>(targetSize.width) / image.cols,
        static_cast<float>(targetSize.height) / image.rows
    );
    
    // 【步骤 3】计算缩放后的尺寸
    int newWidth = static_cast<int>(image.cols * scale);
    int newHeight = static_cast<int>(image.rows * scale);
    
    // 【步骤 4】计算偏移量（居中对齐）
    int offsetX = (targetSize.width - newWidth) / 2;
    int offsetY = (targetSize.height - newHeight) / 2;
    
    // 【步骤 5】记录变换参数（用于后处理坐标还原）
    transform.scale = scale;
    transform.offsetX = offsetX;
    transform.offsetY = offsetY;
    transform.inputSize = image.size();
    transform.outputSize = targetSize;
    
    // 【步骤 6】创建目标尺寸的画布，灰色填充 (114, 114, 114)
    // 面试要点: 为什么填充灰色？
    // 答: 这是 YOLO 系列训练时使用的标准填充值，保持训练和推理的一致性
    cv::Mat letterboxed(targetSize, image.type(), cv::Scalar(114, 114, 114));
    
    // 【步骤 7】缩放图像
    cv::Mat resized;
    if (newWidth > 0 && newHeight > 0) {
        cv::resize(image, resized, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);
    } else {
        throw std::runtime_error("VisionUtils::letterbox: Invalid resized dimensions");
    }
    
    // 【步骤 8】将缩放后的图像复制到画布中心
    cv::Rect roi(offsetX, offsetY, newWidth, newHeight);
    resized.copyTo(letterboxed(roi));
    
    return letterboxed;
}

/**
 * @brief Letterbox 预处理（完整版）
 */
cv::Mat VisionUtils::letterbox(const cv::Mat& image,
                               const LetterboxOptions& options,
                               LetterboxTransform& transform) {
    if (image.empty()) {
        throw std::invalid_argument("VisionUtils::letterbox: Input image is empty");
    }
    
    // 计算缩放比例
    float scale = std::min(
        static_cast<float>(options.targetSize.width) / image.cols,
        static_cast<float>(options.targetSize.height) / image.rows
    );
    
    // 计算缩放后的尺寸
    int newWidth = static_cast<int>(image.cols * scale);
    int newHeight = static_cast<int>(image.rows * scale);
    
    // 计算偏移量
    int offsetX = options.center ? (options.targetSize.width - newWidth) / 2 : 0;
    int offsetY = options.center ? (options.targetSize.height - newHeight) / 2 : 0;
    
    // 记录变换参数
    transform.scale = scale;
    transform.offsetX = offsetX;
    transform.offsetY = offsetY;
    transform.inputSize = image.size();
    transform.outputSize = options.targetSize;
    
    // 创建画布
    cv::Mat letterboxed(options.targetSize, image.type(), options.paddingColor);
    
    // 缩放图像
    cv::Mat resized;
    if (newWidth > 0 && newHeight > 0) {
        cv::resize(image, resized, cv::Size(newWidth, newHeight), 
                  0, 0, options.interpolation);
    } else {
        throw std::runtime_error("VisionUtils::letterbox: Invalid resized dimensions");
    }
    
    // 复制到画布
    cv::Rect roi(offsetX, offsetY, newWidth, newHeight);
    resized.copyTo(letterboxed(roi));
    
    return letterboxed;
}

/**
 * @brief NMS (Non-Maximum Suppression) 非极大值抑制
 * 
 * 实现要点:
 * 1. 先过滤低置信度的框
 * 2. 按置信度降序排序
 * 3. 贪心策略: 选择最高置信度的框，删除与其高度重叠的框
 * 4. 重复直到处理完所有框
 * 
 * 时间复杂度: O(N^2) - N 是框的数量
 * 空间复杂度: O(N) - 需要存储排序后的索引
 * 
 * 面试高频问题:
 * Q: 如何优化 NMS 的性能？
 * A: 1. Soft-NMS (不删除而是降低置信度)
 *    2. Fast-NMS (并行计算 IoU 矩阵)
 *    3. DIoU-NMS (考虑中心点距离)
 *    4. Matrix-NMS (基于矩阵运算)
 */
std::vector<BBox> VisionUtils::nms(const std::vector<BBox>& boxes,
                                   float iouThreshold,
                                   float confThreshold) {
    // 【步骤 1】过滤低置信度的框
    std::vector<BBox> filteredBoxes;
    for (const auto& box : boxes) {
        if (box.confidence >= confThreshold) {
            filteredBoxes.push_back(box);
        }
    }
    
    // 如果没有框通过过滤，直接返回空结果
    if (filteredBoxes.empty()) {
        return {};
    }
    
    // 【步骤 2】按置信度降序排序
    // 使用索引排序，避免频繁移动 BBox 对象
    std::vector<size_t> indices(filteredBoxes.size());
    std::iota(indices.begin(), indices.end(), 0);  // 生成 0, 1, 2, ...
    
    std::sort(indices.begin(), indices.end(), 
              [&filteredBoxes](size_t i, size_t j) {
                  return filteredBoxes[i].confidence > filteredBoxes[j].confidence;
              });
    
    // 【步骤 3】NMS 主循环
    std::vector<bool> suppressed(filteredBoxes.size(), false);  // 标记是否被抑制
    std::vector<BBox> result;
    
    for (size_t i = 0; i < indices.size(); ++i) {
        size_t idx = indices[i];
        
        // 如果已被抑制，跳过
        if (suppressed[idx]) {
            continue;
        }
        
        // 保留当前框
        result.push_back(filteredBoxes[idx]);
        
        // 计算当前框与剩余框的 IoU，抑制高度重叠的框
        cv::Rect2f box1 = filteredBoxes[idx].toXYXY();
        
        for (size_t j = i + 1; j < indices.size(); ++j) {
            size_t idx2 = indices[j];
            
            if (suppressed[idx2]) {
                continue;
            }
            
            cv::Rect2f box2 = filteredBoxes[idx2].toXYXY();
            
            // 计算 IoU
            float iouValue = iou(box1, box2);
            
            // 如果 IoU > 阈值，抑制该框
            if (iouValue > iouThreshold) {
                suppressed[idx2] = true;
            }
        }
    }
    
    return result;
}

/**
 * @brief NMS (完整选项版本)
 */
std::vector<BBox> VisionUtils::nms(const std::vector<BBox>& boxes,
                                   const NMSOptions& options) {
    // 如果需要按类别分别 NMS
    if (options.classSeparate) {
        // 按类别分组
        std::map<int, std::vector<BBox>> boxesByClass;
        for (const auto& box : boxes) {
            boxesByClass[box.classId].push_back(box);
        }
        
        // 对每个类别分别执行 NMS
        std::vector<BBox> result;
        for (auto& [classId, classBoxes] : boxesByClass) {
            auto nmsBoxes = nms(classBoxes, options.iouThreshold, options.confThreshold);
            result.insert(result.end(), nmsBoxes.begin(), nmsBoxes.end());
        }
        
        // 限制最大检测数量
        if (result.size() > static_cast<size_t>(options.maxDetections)) {
            // 按置信度降序排序
            std::sort(result.begin(), result.end(),
                     [](const BBox& a, const BBox& b) {
                         return a.confidence > b.confidence;
                     });
            result.resize(options.maxDetections);
        }
        
        return result;
    } else {
        // 不分类别，直接 NMS
        auto result = nms(boxes, options.iouThreshold, options.confThreshold);
        
        // 限制最大检测数量
        if (result.size() > static_cast<size_t>(options.maxDetections)) {
            result.resize(options.maxDetections);
        }
        
        return result;
    }
}

/**
 * @brief 计算 IoU (Intersection over Union)
 * 
 * 实现要点:
 * 1. 计算交集矩形
 * 2. 计算交集面积
 * 3. 计算并集面积 = area1 + area2 - intersection
 * 4. IoU = intersection / union
 * 
 * 时间复杂度: O(1)
 * 空间复杂度: O(1)
 * 
 * 边界情况处理:
 * - 两框不重叠 → IoU = 0
 * - 两框完全重叠 → IoU = 1
 * - 并集为 0 → IoU = 0 (避免除零)
 * 
 * 面试高频问题:
 * Q: IoU 的取值范围？
 * A: [0, 1]，0 表示无交集，1 表示完全重叠
 * 
 * Q: IoU 有什么缺点？
 * A: 1. 当两框不重叠时，IoU=0 无法反映距离信息
 *    2. 对小目标检测不友好（小偏差导致 IoU 大幅下降）
 *    3. 不考虑框的中心点距离和宽高比
 */
float VisionUtils::iou(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    // 【步骤 1】计算交集矩形
    float x1 = std::max(box1.x, box2.x);
    float y1 = std::max(box1.y, box2.y);
    float x2 = std::min(box1.x + box1.width, box2.x + box2.width);
    float y2 = std::min(box1.y + box1.height, box2.y + box2.height);
    
    // 【步骤 2】计算交集面积
    float intersectionWidth = std::max(0.0f, x2 - x1);
    float intersectionHeight = std::max(0.0f, y2 - y1);
    float intersectionArea = intersectionWidth * intersectionHeight;
    
    // 【步骤 3】计算各框面积
    float area1 = box1.width * box1.height;
    float area2 = box2.width * box2.height;
    
    // 【步骤 4】计算并集面积
    float unionArea = area1 + area2 - intersectionArea;
    
    // 【步骤 5】计算 IoU（避免除零）
    if (unionArea <= 0.0f) {
        return 0.0f;
    }
    
    return intersectionArea / unionArea;
}

/**
 * @brief 坐标逆变换（模型输出 → 原图）
 * 
 * 实现要点:
 * 1. 遍历所有检测框
 * 2. 对每个框应用逆变换公式
 * 3. 注意浮点数精度问题
 * 
 * 逆变换公式:
 * x_orig = (x_model - offsetX) / scale
 * y_orig = (y_model - offsetY) / scale
 * w_orig = w_model / scale
 * h_orig = h_model / scale
 * 
 * 时间复杂度: O(N) - N 是框的数量
 * 空间复杂度: O(N) - 需要创建新的框列表
 * 
 * 面试要点:
 * Q: 为什么需要坐标逆变换？
 * A: 因为模型推理是在 letterbox 后的图像上进行的，输出的坐标
 *    是在 letterbox 图像上的，需要还原到原图才能正确标注
 */
std::vector<BBox> VisionUtils::inverseTransform(
    const std::vector<BBox>& boxes,
    const LetterboxTransform& transform) {
    
    std::vector<BBox> result;
    result.reserve(boxes.size());
    
    for (const auto& box : boxes) {
        BBox transformedBox = box;  // 复制其他属性（confidence, classId, className）
        
        // 应用逆变换
        transformedBox.x = (box.x - transform.offsetX) / transform.scale;
        transformedBox.y = (box.y - transform.offsetY) / transform.scale;
        transformedBox.w = box.w / transform.scale;
        transformedBox.h = box.h / transform.scale;
        
        result.push_back(transformedBox);
    }
    
    return result;
}

// ==================== 📦 P1 重要方法实现 ====================

/**
 * @brief 归一化（均值-方差标准化）
 */
cv::Mat VisionUtils::normalize(const cv::Mat& image,
                               const cv::Scalar& mean,
                               const cv::Scalar& std) {
    if (image.empty()) {
        throw std::invalid_argument("VisionUtils::normalize: Input image is empty");
    }
    
    // 转换为 float32
    cv::Mat floatImage;
    image.convertTo(floatImage, CV_32F, 1.0 / 255.0);
    
    // 归一化: (image - mean) / std
    cv::Mat normalized;
    cv::subtract(floatImage, mean, normalized);
    cv::divide(normalized, std, normalized);
    
    return normalized;
}

/**
 * @brief HWC → CHW 转换
 */
cv::Mat VisionUtils::hwcToChw(const cv::Mat& image) {
    if (image.empty()) {
        throw std::invalid_argument("VisionUtils::hwcToChw: Input image is empty");
    }
    
    // 分离通道
    std::vector<cv::Mat> channels;
    cv::split(image, channels);
    
    // 重新组织为 CHW 格式
    // 原始: [H, W, C] → 目标: [C, H, W]
    int height = image.rows;
    int width = image.cols;
    int numChannels = image.channels();
    
    // 创建 CHW 格式的 Mat
    cv::Mat chw(1, numChannels * height * width, CV_32F);
    float* ptr = chw.ptr<float>();
    
    for (int c = 0; c < numChannels; ++c) {
        cv::Mat channel = channels[c];
        std::memcpy(ptr + c * height * width, 
                   channel.data, 
                   height * width * sizeof(float));
    }
    
    return chw;
}

/**
 * @brief BGR → RGB 转换
 */
cv::Mat VisionUtils::bgrToRgb(const cv::Mat& image) {
    if (image.empty()) {
        throw std::invalid_argument("VisionUtils::bgrToRgb: Input image is empty");
    }
    
    cv::Mat rgb;
    cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
    return rgb;
}

/**
 * @brief 边界框裁剪（确保在图像范围内）
 */
std::vector<BBox> VisionUtils::clipBoxes(const std::vector<BBox>& boxes,
                                         const cv::Size& imageSize) {
    std::vector<BBox> result;
    result.reserve(boxes.size());
    
    for (const auto& box : boxes) {
        BBox clippedBox = box;
        
        // 裁剪 x, y（确保 >= 0）
        clippedBox.x = std::max(0.0f, box.x);
        clippedBox.y = std::max(0.0f, box.y);
        
        // 裁剪宽高（确保不超出图像边界）
        clippedBox.w = std::min(box.w, static_cast<float>(imageSize.width) - clippedBox.x);
        clippedBox.h = std::min(box.h, static_cast<float>(imageSize.height) - clippedBox.y);
        
        // 确保宽高为正
        if (clippedBox.w > 0 && clippedBox.h > 0) {
            result.push_back(clippedBox);
        }
    }
    
    return result;
}

/**
 * @brief 过滤小框（宽高小于阈值的框）
 */
std::vector<BBox> VisionUtils::filterSmallBoxes(const std::vector<BBox>& boxes,
                                                int minSize) {
    std::vector<BBox> result;
    result.reserve(boxes.size());
    
    for (const auto& box : boxes) {
        if (box.w >= minSize && box.h >= minSize) {
            result.push_back(box);
        }
    }
    
    return result;
}

// ==================== 📦 坐标格式转换实现 ====================

/**
 * @brief xywh → xyxy 格式转换
 */
cv::Rect2f VisionUtils::xywh2xyxy(float x, float y, float w, float h) {
    return cv::Rect2f(x, y, x + w, y + h);
}

/**
 * @brief xyxy → xywh 格式转换
 */
cv::Rect2f VisionUtils::xyxy2xywh(float x1, float y1, float x2, float y2) {
    return cv::Rect2f(x1, y1, x2 - x1, y2 - y1);
}

/**
 * @brief xywh → cxcywh 格式转换（中心点+宽高）
 */
cv::Rect2f VisionUtils::xywh2cxcywh(float x, float y, float w, float h) {
    return cv::Rect2f(x + w / 2.0f, y + h / 2.0f, w, h);
}

/**
 * @brief cxcywh → xywh 格式转换
 */
cv::Rect2f VisionUtils::cxcywh2xywh(float cx, float cy, float w, float h) {
    return cv::Rect2f(cx - w / 2.0f, cy - h / 2.0f, w, h);
}

// ==================== 📦 统计工具实现 ====================

/**
 * @brief FPS 计算器实现
 */
VisionUtils::FPSCounter::FPSCounter() {
    lastTime_ = std::chrono::high_resolution_clock::now();
}

void VisionUtils::FPSCounter::tick() {
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> duration = now - lastTime_;
    float fps = 1.0f / duration.count();
    
    fpsList_.push_back(fps);
    if (fpsList_.size() > maxSamples_) {
        fpsList_.erase(fpsList_.begin());
    }
    
    lastTime_ = now;
}

float VisionUtils::FPSCounter::getFPS() const {
    if (fpsList_.empty()) {
        return 0.0f;
    }
    return fpsList_.back();
}

float VisionUtils::FPSCounter::getAvgFPS() const {
    if (fpsList_.empty()) {
        return 0.0f;
    }
    float sum = std::accumulate(fpsList_.begin(), fpsList_.end(), 0.0f);
    return sum / fpsList_.size();
}

void VisionUtils::FPSCounter::reset() {
    fpsList_.clear();
    lastTime_ = std::chrono::high_resolution_clock::now();
}

/**
 * @brief 计算置信度统计
 */
void VisionUtils::calculateConfidenceStats(
    const std::vector<BBox>& boxes,
    float& minConf, float& maxConf, float& avgConf) {
    
    if (boxes.empty()) {
        minConf = maxConf = avgConf = 0.0f;
        return;
    }
    
    minConf = boxes[0].confidence;
    maxConf = boxes[0].confidence;
    float sum = 0.0f;
    
    for (const auto& box : boxes) {
        minConf = std::min(minConf, box.confidence);
        maxConf = std::max(maxConf, box.confidence);
        sum += box.confidence;
    }
    
    avgConf = sum / boxes.size();
}

/**
 * @brief 统计每个类别的检测数量
 */
std::map<int, int> VisionUtils::countByClass(const std::vector<BBox>& boxes) {
    std::map<int, int> counts;
    for (const auto& box : boxes) {
        counts[box.classId]++;
    }
    return counts;
}

// ==================== 📦 调试工具实现 ====================

/**
 * @brief 打印检测框信息（用于调试）
 */
void VisionUtils::printBoxes(const std::vector<BBox>& boxes,
                             const std::string& title) {
    std::cout << "========== " << title << " ==========" << std::endl;
    std::cout << "Total boxes: " << boxes.size() << std::endl;
    
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& box = boxes[i];
        std::cout << std::fixed << std::setprecision(2)
                  << "[" << i << "] "
                  << "Class: " << box.classId 
                  << " (" << box.className << "), "
                  << "Conf: " << box.confidence << ", "
                  << "Box: (" << box.x << ", " << box.y << ", "
                  << box.w << ", " << box.h << ")"
                  << std::endl;
    }
    std::cout << "=================================" << std::endl;
}

/**
 * @brief 生成检测结果摘要
 */
std::string VisionUtils::generateSummary(const std::vector<BBox>& boxes) {
    if (boxes.empty()) {
        return "No detections";
    }
    
    // 统计信息
    auto classCounts = countByClass(boxes);
    float minConf, maxConf, avgConf;
    calculateConfidenceStats(boxes, minConf, maxConf, avgConf);
    
    // 构建摘要字符串
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Total: " << boxes.size() << " boxes, ";
    oss << "Classes: {";
    
    bool first = true;
    for (const auto& [classId, count] : classCounts) {
        if (!first) oss << ", ";
        oss << classId << ": " << count;
        first = false;
    }
    
    oss << "}, ";
    oss << "Conf: [" << minConf << ", " << maxConf << "], ";
    oss << "Avg: " << avgConf;
    
    return oss.str();
}

// ==================== 私有辅助方法实现 ====================

/**
 * @brief 计算 Letterbox 变换参数
 */
void VisionUtils::calculateLetterboxParams(
    const cv::Size& imageSize,
    const cv::Size& targetSize,
    float& scale, int& offsetX, int& offsetY) {
    
    // 计算缩放比例
    scale = std::min(
        static_cast<float>(targetSize.width) / imageSize.width,
        static_cast<float>(targetSize.height) / imageSize.height
    );
    
    // 计算缩放后的尺寸
    int newWidth = static_cast<int>(imageSize.width * scale);
    int newHeight = static_cast<int>(imageSize.height * scale);
    
    // 计算偏移量（居中）
    offsetX = (targetSize.width - newWidth) / 2;
    offsetY = (targetSize.height - newHeight) / 2;
}

/**
 * @brief 计算矩形面积
 */
float VisionUtils::boxArea(const cv::Rect2f& box) {
    return box.width * box.height;
}

/**
 * @brief 计算两个矩形的交集
 */
cv::Rect2f VisionUtils::boxIntersection(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    float x1 = std::max(box1.x, box2.x);
    float y1 = std::max(box1.y, box2.y);
    float x2 = std::min(box1.x + box1.width, box2.x + box2.width);
    float y2 = std::min(box1.y + box1.height, box2.y + box2.height);
    
    float width = std::max(0.0f, x2 - x1);
    float height = std::max(0.0f, y2 - y1);
    
    return cv::Rect2f(x1, y1, width, height);
}

/**
 * @brief 计算两个矩形的并集
 */
cv::Rect2f VisionUtils::boxUnion(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    float x1 = std::min(box1.x, box2.x);
    float y1 = std::min(box1.y, box2.y);
    float x2 = std::max(box1.x + box1.width, box2.x + box2.width);
    float y2 = std::max(box1.y + box1.height, box2.y + box2.height);
    
    return cv::Rect2f(x1, y1, x2 - x1, y2 - y1);
}

// ==================== 📦 P2 可选方法实现（暂时留空，后续补充） ====================

/**
 * @brief Soft-NMS
 * @note P2 优先级，暂未实现
 */
std::vector<BBox> VisionUtils::softNms(const std::vector<BBox>& boxes,
                                       float sigma,
                                       float confThreshold) {
    // TODO: 后续实现 Soft-NMS
    // 当前返回普通 NMS 结果
    return nms(boxes, 0.45f, confThreshold);
}

/**
 * @brief 计算 GIoU (Generalized IoU)
 * @note P2 优先级，暂未实现
 */
float VisionUtils::giou(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    // TODO: 后续实现 GIoU
    return iou(box1, box2);
}

/**
 * @brief 计算 DIoU (Distance-IoU)
 * @note P2 优先级，暂未实现
 */
float VisionUtils::diou(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    // TODO: 后续实现 DIoU
    return iou(box1, box2);
}

/**
 * @brief 计算 CIoU (Complete-IoU)
 * @note P2 优先级，暂未实现
 */
float VisionUtils::ciou(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    // TODO: 后续实现 CIoU
    return iou(box1, box2);
}

/**
 * @brief 判断点是否在框内
 */
bool VisionUtils::pointInBox(const cv::Point2f& point, const cv::Rect2f& box) {
    return point.x >= box.x && point.x <= box.x + box.width &&
           point.y >= box.y && point.y <= box.y + box.height;
}

/**
 * @brief 计算多边形面积
 * @note P2 优先级，暂未实现
 */
float VisionUtils::polygonArea(const std::vector<cv::Point2f>& polygon) {
    // TODO: 后续使用 Shoelace 公式实现
    return 0.0f;
}

/**
 * @brief 计算 mAP (Mean Average Precision)
 * @note P2 优先级，暂未实现
 */
float VisionUtils::calculateMAP(
    const std::vector<std::vector<BBox>>& predictions,
    const std::vector<std::vector<BBox>>& groundTruths,
    float iouThreshold) {
    // TODO: 后续实现 mAP 计算
    return 0.0f;
}

/**
 * @brief 计算 Precision 和 Recall
 * @note P2 优先级，暂未实现
 */
void VisionUtils::calculatePrecisionRecall(
    const std::vector<BBox>& predictions,
    const std::vector<BBox>& groundTruths,
    float iouThreshold,
    float& precision,
    float& recall) {
    // TODO: 后续实现 Precision/Recall 计算
    precision = 0.0f;
    recall = 0.0f;
}

/**
 * @brief 计算混淆矩阵
 * @note P2 优先级，暂未实现
 */
cv::Mat VisionUtils::calculateConfusionMatrix(
    const std::vector<BBox>& predictions,
    const std::vector<BBox>& groundTruths,
    int numClasses) {
    // TODO: 后续实现混淆矩阵计算
    return cv::Mat::zeros(numClasses, numClasses, CV_32S);
}

// ==================== 📦 可视化工具实现 ====================

/**
 * @brief 绘制检测结果
 * 
 * @details
 * 算法流程:
 * 1. 遍历所有检测框
 * 2. 获取类别颜色（基于 classId）
 * 3. 绘制边界框（2像素宽度）
 * 4. 绘制标签背景和文本
 * 
 * 性能优化:
 * - 边界检查: rect &= imageRect 确保不越界
 * - 预分配: label 字符串使用 reserve
 * 
 * @note 从 Visualizer::drawDetections 迁移而来
 * @note 合并理由: 可视化是视觉处理的自然扩展，与调试工具一致
 */
void VisionUtils::drawDetections(cv::Mat& image, const std::vector<BBox>& boxes) {
    if (image.empty()) {
        return;
    }

    cv::Rect imageRect(0, 0, image.cols, image.rows);
    
    for (const auto& box : boxes) {
        // 1. 获取类别颜色
        cv::Scalar color = getClassColor(box.classId);
        
        // 2. 绘制边界框
        cv::Rect rect(static_cast<int>(box.x), 
                     static_cast<int>(box.y), 
                     static_cast<int>(box.w), 
                     static_cast<int>(box.h));
        
        // 边界检查：确保矩形不超出图像范围
        rect &= imageRect;
        if (rect.empty()) {
            continue;  // 跳过完全在图像外的框
        }

        cv::rectangle(image, rect, color, 2);
        
        // // 3. 绘制标签
        // std::string label = box.className + " " + 
        //                    std::to_string(static_cast<int>(box.confidence * 100)) + "%";
        
        // // 计算标签背景大小
        // int baseline = 0;
        // cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 
        //                                      0.5, 1, &baseline);
        
        // // 标签位置（框的左上角）
        // int labelY = std::max(rect.y, labelSize.height);
        
        // // 绘制标签背景（半透明效果）
        // cv::Rect labelRect(rect.x, labelY - labelSize.height,
        //                   labelSize.width, labelSize.height + baseline);
        // labelRect &= imageRect;
        
        // if (!labelRect.empty()) {
        //     cv::rectangle(image, labelRect, color, cv::FILLED);
            
        //     // 绘制标签文本（白色）
        //     cv::putText(image, label, 
        //                cv::Point(rect.x, labelY),
        //                cv::FONT_HERSHEY_SIMPLEX, 0.5, 
        //                cv::Scalar(255, 255, 255), 1);
        // }
    }
}

/**
 * @brief 获取类别颜色
 * 
 * @details
 * 黄金分割比（Golden Ratio）颜色生成算法:
 * 
 * 1. 黄金分割比: φ = (√5 - 1) / 2 ≈ 0.618033988749895
 * 2. 色相计算: h = (classId * φ) % 1.0
 * 3. HSV 设置: H = h * 180, S = 0.7 * 255, V = 0.95 * 255
 * 4. 转换到 BGR 颜色空间
 * 
 * 为什么使用黄金分割比?
 * - 数学特性: φ 是最无理的无理数（连分数展开最慢收敛）
 * - 颜色分布: 保证相邻类别的颜色在色环上尽可能远
 * - 视觉效果: 生成的颜色序列视觉区分度最大
 * 
 * 示例:
 * - classId=0: H=0°   (红色)
 * - classId=1: H=222° (蓝色)
 * - classId=2: H=84°  (绿色)
 * - 相邻颜色差异约 138°，远大于平均分布的 120°
 * 
 * @note 面试高频: 黄金分割比在工程中的应用
 * @note 参考: Fibonacci Hashing、Golden Angle 
 * 
 * 时间复杂度: O(1)
 * 空间复杂度: O(1)
 */
cv::Scalar VisionUtils::getClassColor(int classId) {
    // 黄金分割比常数
    constexpr float GOLDEN_RATIO = 0.618033988749895f;
    
    // 计算色相（Hue）使用黄金分割
    float hue = std::fmod(classId * GOLDEN_RATIO, 1.0f) * 180.0f;
    
    // HSV 颜色空间
    cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(hue, 0.7 * 255, 0.95 * 255));
    
    // 转换为 BGR
    cv::Mat bgr;
    cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
    
    return cv::Scalar(bgr.at<cv::Vec3b>(0, 0)[0],
                     bgr.at<cv::Vec3b>(0, 0)[1],
                     bgr.at<cv::Vec3b>(0, 0)[2]);
}

}  // namespace vision
}  // namespace esdk_sophon
