/**
 * @file VisionUtils.h
 * @brief Vision 模块统一工具类
 * 
 * 提供深度学习推理所需的所有工具方法:
 * 
 * 📦 预处理工具:
 * - Letterbox 变换（保持宽高比缩放+填充）
 * - Normalize 归一化
 * - 通道变换（HWC ↔ CHW）
 * - 颜色空间转换（BGR ↔ RGB）
 * 
 * 📦 后处理工具:
 * - NMS (Non-Maximum Suppression)
 * - Soft-NMS
 * - 坐标格式转换（xyxy ↔ xywh ↔ cxcywh）
 * - 坐标逆变换（模型输出 → 原图）
 * - 边界框裁剪（确保在图像范围内）
 * 
 * 📦 几何工具:
 * - IoU 计算（Intersection over Union）
 * - GIoU, DIoU, CIoU 计算
 * - 多边形面积计算
 * - 点在框内判断
 * 
 * 📦 评估工具:
 * - mAP 计算（Mean Average Precision）
 * - Precision/Recall 曲线
 * - Confusion Matrix
 * - FPS 统计
 * 
 * @note 所有方法都是静态的，无需实例化
 * @note 线程安全，可在多线程环境使用
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-21
 */

#ifndef ESDK_SOPHON_VISION_VISION_UTILS_H_
#define ESDK_SOPHON_VISION_VISION_UTILS_H_

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include "esdk_sophon/task/TaskTypes.h"  // 引入统一的 BoundingBox 定义

namespace esdk_sophon {
namespace vision {

// ==================== 数据结构定义 ====================

/**
 * @brief Letterbox 变换参数
 * 
 * 记录缩放和偏移信息，用于后处理坐标还原。
 * 
 * @note 面试要点: Letterbox 是 YOLO 系列的标准预处理方法
 */
struct LetterboxTransform {
    float scale{1.0f};         ///< 缩放比例
    int offsetX{0};            ///< X 方向偏移（像素）
    int offsetY{0};            ///< Y 方向偏移（像素）
    cv::Size inputSize;        ///< 原始输入尺寸
    cv::Size outputSize;       ///< 输出尺寸
    
    /**
     * @brief 坐标正向变换（原图 → 模型输入）
     */
    cv::Point2f transformPoint(const cv::Point2f& point) const {
        return cv::Point2f(point.x * scale + offsetX, 
                           point.y * scale + offsetY);
    }
    
    /**
     * @brief 坐标逆变换（模型输出 → 原图）
     * 
     * @note 面试要点: 后处理必须将模型输出的坐标还原到原图
     */
    cv::Point2f inverseTransformPoint(const cv::Point2f& point) const {
        return cv::Point2f((point.x - offsetX) / scale, 
                           (point.y - offsetY) / scale);
    }
    
    /**
     * @brief 边界框逆变换
     */
    cv::Rect inverseTransformRect(const cv::Rect& rect) const {
        return cv::Rect(
            static_cast<int>((rect.x - offsetX) / scale),
            static_cast<int>((rect.y - offsetY) / scale),
            static_cast<int>(rect.width / scale),
            static_cast<int>(rect.height / scale)
        );
    }
};

/**
 * @brief Letterbox 选项
 */
struct LetterboxOptions {
    cv::Size targetSize{640, 640};           ///< 目标尺寸
    cv::Scalar paddingColor{114, 114, 114};  ///< 填充颜色（默认灰色）
    cv::InterpolationFlags interpolation{cv::INTER_LINEAR};  ///< 插值方法
    bool center{true};                       ///< 是否居中对齐
};

/**
 * @brief 边界框类型别名
 * 
 * @note 统一使用 task::BoundingBox，废弃 vision::BBox
 * @note 这是架构重构的一部分：消除重复定义，实现 Single Source of Truth
 * 
 * 历史原因:
 * - 之前: vision::BBox, task::BoundingBox, types::Detection 三个结构体表示同一事物
 * - 现在: 统一使用 task::BoundingBox
 * - 好处: 消除数据转换代码，降低维护成本，避免不一致性
 * 
 * @see task::BoundingBox
 */
using BBox = task::BoundingBox;

/**
 * @brief NMS 选项
 */
struct NMSOptions {
    float iouThreshold{0.45f};   ///< IoU 阈值（默认 0.45）
    float confThreshold{0.25f};  ///< 置信度阈值（默认 0.25）
    bool classSeparate{true};    ///< 是否按类别分别 NMS
    int maxDetections{300};      ///< 最大检测框数量
};

// ==================== VisionUtils 工具类 ====================

/**
 * @brief Vision 模块统一工具类（静态工具类）
 * 
 * 用法示例:
 * @code
 * // 1. 预处理
 * LetterboxTransform transform;
 * cv::Mat preprocessed = VisionUtils::letterbox(image, cv::Size(640, 640), transform);
 * 
 * // 2. 推理
 * auto rawBoxes = model->predict(preprocessed);
 * 
 * // 3. 后处理
 * std::vector<BBox> boxes = parseModelOutput(rawBoxes);  // 解析模型输出
 * boxes = VisionUtils::nms(boxes, 0.45f, 0.25f);         // NMS 过滤
 * boxes = VisionUtils::inverseTransform(boxes, transform); // 坐标还原
 * boxes = VisionUtils::clipBoxes(boxes, cv::Size(1920, 1080)); // 边界裁剪
 * @endcode
 * 
 * @note 面试要点: 工具类使用静态方法，无状态，线程安全
 */
class VisionUtils {
public:
    // ==================== 📦 预处理工具 ====================
    
    /**
     * @brief Letterbox 预处理（简化版）
     * 
     * @param image 原始图像 (BGR, uint8)
     * @param targetSize 目标尺寸
     * @param[out] transform 变换参数（用于后处理坐标还原）
     * @return cv::Mat 预处理后的图像 (BGR, uint8)
     * 
     * @details
     * Letterbox 流程:
     * 1. 计算缩放比例 scale = min(targetW/imageW, targetH/imageH)
     * 2. 按比例缩放图像（保持宽高比）
     * 3. 创建目标尺寸画布，灰色填充 (114, 114, 114)
     * 4. 将缩放后图像居中放置
     * 5. 记录变换参数 (scale, offsetX, offsetY)
     * 
     * @note 面试高频考点！Letterbox 是 YOLO 系列的标准预处理
     * 
     * Q: 为什么选择 min 而不是 max？
     * A: min 保证图像完整显示不被裁剪，max 会导致长边超出需要裁剪
     * 
     * Q: 为什么填充灰色 (114, 114, 114)？
     * A: 这是训练时使用的标准填充值，保持训练和推理的一致性
     */
    static cv::Mat letterbox(const cv::Mat& image, 
                            const cv::Size& targetSize,
                            LetterboxTransform& transform);
    
    /**
     * @brief Letterbox 预处理（完整版）
     * 
     * @param image 原始图像
     * @param options Letterbox 选项（目标尺寸、填充颜色等）
     * @param[out] transform 变换参数
     * @return cv::Mat 预处理后的图像
     */
    static cv::Mat letterbox(const cv::Mat& image,
                            const LetterboxOptions& options,
                            LetterboxTransform& transform);
    
    /**
     * @brief 归一化（均值-方差标准化）
     * 
     * @param image 输入图像 [0, 255] (uint8)
     * @param mean 均值 {R, G, B}
     * @param std 标准差 {R, G, B}
     * @return cv::Mat 归一化后的图像 (float32)
     * 
     * @details
     * 公式: output = (input / 255.0 - mean) / std
     * 
     * 常用参数:
     * - ImageNet: mean={0.485, 0.456, 0.406}, std={0.229, 0.224, 0.225}
     * - COCO: mean={0.0, 0.0, 0.0}, std={1.0, 1.0, 1.0}
     * 
     * @note 归一化可以加速模型收敛，提高训练稳定性
     */
    static cv::Mat normalize(const cv::Mat& image,
                            const cv::Scalar& mean,
                            const cv::Scalar& std);
    
    /**
     * @brief HWC → CHW 转换
     * 
     * @param image 输入图像 (H, W, C)
     * @return cv::Mat 输出图像 (C, H, W)
     * 
     * @note PyTorch/PaddlePaddle 需要 CHW 格式，OpenCV 默认 HWC 格式
     * 
     * @note 面试要点: 为什么深度学习框架用 CHW？
     * A: 因为卷积操作按通道并行，CHW 内存连续性更好，缓存友好
     */
    static cv::Mat hwcToChw(const cv::Mat& image);
    
    /**
     * @brief BGR → RGB 转换
     * 
     * @param image 输入图像 (BGR)
     * @return cv::Mat 输出图像 (RGB)
     * 
     * @note OpenCV 默认 BGR，大多数模型训练用 RGB
     */
    static cv::Mat bgrToRgb(const cv::Mat& image);
    
    // ==================== 📦 后处理工具 ====================
    
    /**
     * @brief NMS (Non-Maximum Suppression) 非极大值抑制
     * 
     * @param boxes 检测框列表
     * @param iouThreshold IoU 阈值（默认 0.45）
     * @param confThreshold 置信度阈值（默认 0.25）
     * @return std::vector<BBox> 过滤后的检测框
     * 
     * @details
     * NMS 流程:
     * 1. 过滤置信度 < confThreshold 的框
     * 2. 按置信度降序排序
     * 3. 选择置信度最高的框 A
     * 4. 计算 A 与其他框的 IoU
     * 5. 删除 IoU > iouThreshold 的框（认为是重复检测）
     * 6. 重复 3-5 直到处理完所有框
     * 
     * @note 面试高频考点！NMS 是目标检测的标准后处理
     * 
     * Q: NMS 的时间复杂度？
     * A: O(N^2)，N 是框的数量。优化方法：Soft-NMS, Fast-NMS
     * 
     * Q: 为什么需要 NMS？
     * A: 同一目标会产生多个重叠的检测框，NMS 保留最优的一个
     */
    static std::vector<BBox> nms(const std::vector<BBox>& boxes,
                                 float iouThreshold = 0.45f,
                                 float confThreshold = 0.25f);
    
    /**
     * @brief NMS (完整选项)
     * 
     * @param boxes 检测框列表
     * @param options NMS 选项（IoU 阈值、置信度阈值、是否按类别分别 NMS）
     * @return std::vector<BBox> 过滤后的检测框
     */
    static std::vector<BBox> nms(const std::vector<BBox>& boxes,
                                 const NMSOptions& options);
    
    /**
     * @brief Soft-NMS
     * 
     * @param boxes 检测框列表
     * @param sigma 高斯衰减参数（默认 0.5）
     * @param confThreshold 置信度阈值
     * @return std::vector<BBox> 处理后的检测框
     * 
     * @details
     * Soft-NMS 不是直接删除重叠框，而是降低其置信度:
     * conf_new = conf_old * exp(-(iou^2 / sigma))
     * 
     * 优点: 减少漏检，尤其适合重叠目标密集的场景（人群、货架商品）
     * 
     * @note 面试加分项：了解 Soft-NMS 的改进原理
     */
    static std::vector<BBox> softNms(const std::vector<BBox>& boxes,
                                     float sigma = 0.5f,
                                     float confThreshold = 0.001f);
    
    /**
     * @brief 坐标逆变换（模型输出 → 原图）
     * 
     * @param boxes 模型输出的检测框（在 letterbox 图像上的坐标）
     * @param transform Letterbox 变换参数
     * @return std::vector<BBox> 原图坐标的检测框
     * 
     * @details
     * 逆变换公式:
     * x_orig = (x_model - offsetX) / scale
     * y_orig = (y_model - offsetY) / scale
     * w_orig = w_model / scale
     * h_orig = h_model / scale
     * 
     * @note 面试要点: 后处理必须将坐标还原到原图，否则无法正确标注
     */
    static std::vector<BBox> inverseTransform(
        const std::vector<BBox>& boxes,
        const LetterboxTransform& transform);
    
    /**
     * @brief 边界框裁剪（确保在图像范围内）
     * 
     * @param boxes 检测框列表
     * @param imageSize 图像尺寸
     * @return std::vector<BBox> 裁剪后的检测框
     * 
     * @details
     * 裁剪边界框，确保:
     * - x >= 0, y >= 0
     * - x + w <= imageWidth, y + h <= imageHeight
     * 
     * @note 坐标还原后可能超出图像边界，需要裁剪
     */
    static std::vector<BBox> clipBoxes(const std::vector<BBox>& boxes,
                                       const cv::Size& imageSize);
    
    /**
     * @brief 过滤小框（宽高小于阈值的框）
     * 
     * @param boxes 检测框列表
     * @param minSize 最小尺寸（像素）
     * @return std::vector<BBox> 过滤后的检测框
     * 
     * @note 过小的框通常是误检，可以过滤掉
     */
    static std::vector<BBox> filterSmallBoxes(const std::vector<BBox>& boxes,
                                              int minSize = 10);
    
    // ==================== 📦 坐标格式转换 ====================
    
    /**
     * @brief xywh → xyxy 格式转换
     * 
     * @param x 左上角 X
     * @param y 左上角 Y
     * @param w 宽度
     * @param h 高度
     * @return cv::Rect2f xyxy 格式 (x1, y1, x2, y2)
     * 
     * @note xywh: (左上角X, 左上角Y, 宽, 高)
     * @note xyxy: (左上角X, 左上角Y, 右下角X, 右下角Y)
     */
    static cv::Rect2f xywh2xyxy(float x, float y, float w, float h);
    
    /**
     * @brief xyxy → xywh 格式转换
     */
    static cv::Rect2f xyxy2xywh(float x1, float y1, float x2, float y2);
    
    /**
     * @brief xywh → cxcywh 格式转换（中心点+宽高）
     * 
     * @note cxcywh: (中心点X, 中心点Y, 宽, 高)
     */
    static cv::Rect2f xywh2cxcywh(float x, float y, float w, float h);
    
    /**
     * @brief cxcywh → xywh 格式转换
     */
    static cv::Rect2f cxcywh2xywh(float cx, float cy, float w, float h);
    
    // ==================== 📦 几何计算工具 ====================
    
    /**
     * @brief 计算 IoU (Intersection over Union)
     * 
     * @param box1 边界框 1 (xyxy 格式)
     * @param box2 边界框 2 (xyxy 格式)
     * @return float IoU 值 [0, 1]
     * 
     * @details
     * IoU = Area(Intersection) / Area(Union)
     * 
     * @note 面试高频考点！
     * 
     * Q: IoU 的取值范围？
     * A: [0, 1]，0 表示无交集，1 表示完全重叠
     * 
     * Q: IoU 用于什么场景？
     * A: 1. NMS 判断框是否重复
     *    2. 评估检测结果与真值的匹配程度
     *    3. Anchor-based 检测器的正负样本分配
     */
    static float iou(const cv::Rect2f& box1, const cv::Rect2f& box2);
    
    /**
     * @brief 计算 GIoU (Generalized IoU)
     * 
     * @param box1 边界框 1
     * @param box2 边界框 2
     * @return float GIoU 值 [-1, 1]
     * 
     * @details
     * GIoU = IoU - (Area(C) - Area(Union)) / Area(C)
     * 其中 C 是包含 box1 和 box2 的最小外接矩形
     * 
     * 优点: 当两框不重叠时，IoU=0 无法提供位置信息，GIoU 可以
     * 
     * @note 面试加分项: GIoU 作为损失函数比 IoU Loss 更优
     */
    static float giou(const cv::Rect2f& box1, const cv::Rect2f& box2);
    
    /**
     * @brief 计算 DIoU (Distance-IoU)
     * 
     * @param box1 边界框 1
     * @param box2 边界框 2
     * @return float DIoU 值
     * 
     * @details
     * DIoU = IoU - (d^2 / c^2)
     * d: 两框中心点的欧氏距离
     * c: 包含两框的最小外接矩形的对角线长度
     * 
     * 优点: 考虑了中心点距离，收敛更快
     */
    static float diou(const cv::Rect2f& box1, const cv::Rect2f& box2);
    
    /**
     * @brief 计算 CIoU (Complete-IoU)
     * 
     * @param box1 边界框 1
     * @param box2 边界框 2
     * @return float CIoU 值
     * 
     * @details
     * CIoU = DIoU - α * v
     * v: 宽高比一致性度量
     * α: 权重参数
     * 
     * 优点: 同时考虑重叠面积、中心点距离、宽高比，是最完善的 IoU 变体
     * 
     * @note YOLOv5/YOLOv8 使用 CIoU Loss
     */
    static float ciou(const cv::Rect2f& box1, const cv::Rect2f& box2);
    
    /**
     * @brief 判断点是否在框内
     * 
     * @param point 点坐标
     * @param box 边界框
     * @return true 点在框内
     */
    static bool pointInBox(const cv::Point2f& point, const cv::Rect2f& box);
    
    /**
     * @brief 计算多边形面积
     * 
     * @param polygon 多边形顶点列表（逆时针或顺时针）
     * @return float 面积
     * 
     * @note 使用 Shoelace 公式计算
     */
    static float polygonArea(const std::vector<cv::Point2f>& polygon);
    
    // ==================== 📦 评估工具 ====================
    
    /**
     * @brief 计算 mAP (Mean Average Precision)
     * 
     * @param predictions 预测结果（每张图的检测框列表）
     * @param groundTruths 真值标注（每张图的真值框列表）
     * @param iouThreshold IoU 阈值（默认 0.5）
     * @return float mAP 值 [0, 1]
     * 
     * @note 目标检测模型评估的标准指标
     * 
     * @note 面试要点: mAP 的计算流程
     */
    static float calculateMAP(
        const std::vector<std::vector<BBox>>& predictions,
        const std::vector<std::vector<BBox>>& groundTruths,
        float iouThreshold = 0.5f);
    
    /**
     * @brief 计算 Precision 和 Recall
     * 
     * @param predictions 预测结果
     * @param groundTruths 真值标注
     * @param iouThreshold IoU 阈值
     * @param[out] precision 精确率 (TP / (TP + FP))
     * @param[out] recall 召回率 (TP / (TP + FN))
     * 
     * @note 面试要点: Precision vs Recall 的权衡
     */
    static void calculatePrecisionRecall(
        const std::vector<BBox>& predictions,
        const std::vector<BBox>& groundTruths,
        float iouThreshold,
        float& precision,
        float& recall);
    
    /**
     * @brief 计算混淆矩阵
     * 
     * @param predictions 预测结果
     * @param groundTruths 真值标注
     * @param numClasses 类别数量
     * @return cv::Mat 混淆矩阵 (numClasses × numClasses)
     */
    static cv::Mat calculateConfusionMatrix(
        const std::vector<BBox>& predictions,
        const std::vector<BBox>& groundTruths,
        int numClasses);
    
    // ==================== 📦 统计工具 ====================
    
    /**
     * @brief FPS 计算器
     * 
     * 用于统计模型推理帧率。
     * 
     * 用法:
     * @code
     * VisionUtils::FPSCounter fpsCounter;
     * while (true) {
     *     auto result = model->detect(frame);
     *     fpsCounter.tick();
     *     float fps = fpsCounter.getFPS();
     *     std::cout << "FPS: " << fps << std::endl;
     * }
     * @endcode
     */
    class FPSCounter {
    public:
        FPSCounter();
        void tick();                 ///< 记录一帧
        float getFPS() const;        ///< 获取当前 FPS
        float getAvgFPS() const;     ///< 获取平均 FPS
        void reset();                ///< 重置计数器
        
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> lastTime_;
        std::vector<float> fpsList_;
        size_t maxSamples_{100};
    };
    
    /**
     * @brief 计算置信度统计
     * 
     * @param boxes 检测框列表
     * @param[out] minConf 最小置信度
     * @param[out] maxConf 最大置信度
     * @param[out] avgConf 平均置信度
     * 
     * @note 用于分析模型输出分布
     */
    static void calculateConfidenceStats(
        const std::vector<BBox>& boxes,
        float& minConf, float& maxConf, float& avgConf);
    
    /**
     * @brief 统计每个类别的检测数量
     * 
     * @param boxes 检测框列表
     * @return std::map<int, int> 类别 ID → 数量
     */
    static std::map<int, int> countByClass(const std::vector<BBox>& boxes);
    
    // ==================== 📦 调试工具 ====================
    
    /**
     * @brief 打印检测框信息（用于调试）
     * 
     * @param boxes 检测框列表
     * @param title 标题（默认 "Detection Results"）
     */
    static void printBoxes(const std::vector<BBox>& boxes,
                          const std::string& title = "Detection Results");
    
    /**
     * @brief 生成检测结果摘要
     * 
     * @param boxes 检测框列表
     * @return std::string 摘要字符串
     * 
     * 示例: "Total: 5 boxes, Classes: {0: 2, 1: 3}, Avg Conf: 0.87"
     */
    static std::string generateSummary(const std::vector<BBox>& boxes);
    
    // ==================== 📦 可视化工具 ====================
    
    /**
     * @brief 绘制检测结果（在图像上绘制边界框和标签）
     * 
     * @param image 输入图像（会被直接修改）
     * @param boxes 检测框列表（使用 VisionUtils::BBox）
     * 
     * @details
     * 绘制内容:
     * 1. 彩色边界框（每个类别不同颜色）
     * 2. 类别标签 + 置信度（格式: "person 0.95"）
     * 3. 标签背景（便于阅读）
     * 
     * @note 使用黄金分割比生成颜色，保证相邻类别颜色差异大
     * @note 会自动进行边界检查，防止绘制超出图像范围
     * 
     * 用法示例:
     * @code
     * std::vector<BBox> boxes = detector->detect(image);
     * VisionUtils::drawDetections(image, boxes);
     * cv::imshow("Result", image);
     * @endcode
     * 
     * @note 从 Visualizer 迁移而来，合并到 VisionUtils 统一管理
     */
    static void drawDetections(cv::Mat& image, const std::vector<BBox>& boxes);
    
    /**
     * @brief 获取类别颜色（用于绘图）
     * 
     * @param classId 类别ID
     * @return cv::Scalar BGR颜色值
     * 
     * @details
     * 使用黄金分割比（Golden Ratio）生成颜色序列：
     * - h = (classId * 0.618033988749895) % 1.0
     * - 转换 HSV → RGB
     * - 固定 S=0.7, V=0.95，保证颜色鲜艳
     * 
     * 优点:
     * 1. 相同 classId 总是返回相同颜色（种子固定）
     * 2. 相邻 classId 的颜色差异最大（黄金分割特性）
     * 3. 生成的颜色均匀分布在色环上
     * 
     * @note 面试要点: 黄金分割比在颜色生成中的应用
     */
    static cv::Scalar getClassColor(int classId);
    
private:
    // ==================== 私有辅助方法 ====================
    
    /**
     * @brief 计算 Letterbox 变换参数
     * 
     * @param imageSize 原图尺寸
     * @param targetSize 目标尺寸
     * @param[out] scale 缩放比例
     * @param[out] offsetX X 偏移
     * @param[out] offsetY Y 偏移
     */
    static void calculateLetterboxParams(
        const cv::Size& imageSize,
        const cv::Size& targetSize,
        float& scale, int& offsetX, int& offsetY);
    
    /**
     * @brief 计算矩形面积
     */
    static float boxArea(const cv::Rect2f& box);
    
    /**
     * @brief 计算两个矩形的交集
     */
    static cv::Rect2f boxIntersection(const cv::Rect2f& box1, const cv::Rect2f& box2);
    
    /**
     * @brief 计算两个矩形的并集
     */
    static cv::Rect2f boxUnion(const cv::Rect2f& box1, const cv::Rect2f& box2);
};

}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_VISION_UTILS_H_
