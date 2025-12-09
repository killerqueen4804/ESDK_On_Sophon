/**
 * @file IDetector.h
 * @brief 目标检测器接口 - 定义所有检测算法的统一接口
 * 
 * 设计模式: 策略模式 (Strategy Pattern)
 * 职责: 定义检测算法的抽象接口,实现算法替换
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-31
 */

#ifndef ESDK_SOPHON_VISION_IDETECTOR_H_
#define ESDK_SOPHON_VISION_IDETECTOR_H_

#include <vector>
#include <string>
#include <memory>
#include <opencv2/opencv.hpp>

namespace esdk_sophon {
namespace vision {

/**
 * @brief 检测结果 - 单个目标的检测信息
 */
struct DetectionBox {
    int classId;              ///< 类别ID (在classes列表中的索引)
    std::string className;    ///< 类别名称 (如"car", "person")
    float confidence;         ///< 置信度 [0.0, 1.0]
    
    // 边界框坐标 (像素坐标)
    int x;                    ///< 左上角x坐标
    int y;                    ///< 左上角y坐标  
    int width;                ///< 宽度
    int height;               ///< 高度
    
    /**
     * @brief 构造函数
     */
    DetectionBox()
        : classId(-1), confidence(0.0f), x(0), y(0), width(0), height(0) {}
    
    /**
     * @brief 获取中心点坐标
     */
    cv::Point2f getCenter() const {
        return cv::Point2f(x + width / 2.0f, y + height / 2.0f);
    }
    
    /**
     * @brief 转换为cv::Rect
     */
    cv::Rect toRect() const {
        return cv::Rect(x, y, width, height);
    }
};

/**
 * @brief 检测结果集合 - 一帧图像的所有检测结果
 */
struct DetectionResult {
    std::vector<DetectionBox> boxes;  ///< 检测到的目标列表
    
    // 推理性能信息
    double preprocessTime;            ///< 预处理耗时 (ms)
    double inferenceTime;             ///< 推理耗时 (ms)
    double postprocessTime;           ///< 后处理耗时 (ms)
    
    /**
     * @brief 构造函数
     */
    DetectionResult()
        : preprocessTime(0.0), inferenceTime(0.0), postprocessTime(0.0) {}
    
    /**
     * @brief 获取总耗时
     */
    double getTotalTime() const {
        return preprocessTime + inferenceTime + postprocessTime;
    }
    
    /**
     * @brief 获取检测数量
     */
    size_t getDetectionCount() const {
        return boxes.size();
    }
    
    /**
     * @brief 是否检测到目标
     */
    bool hasDetections() const {
        return !boxes.empty();
    }
};

/**
 * @brief 检测器配置
 */
struct DetectorConfig {
    std::string modelPath;            ///< 模型文件路径
    std::string configFile;           ///< 配置文件路径 (PaddleDet需要infer_cfg.yml)
    std::vector<std::string> classes; ///< 类别名称列表
    float confidenceThreshold;        ///< 置信度阈值 [0.0, 1.0]
    float nmsThreshold;               ///< NMS阈值 [0.0, 1.0]
    int inputWidth;                   ///< 模型输入宽度
    int inputHeight;                  ///< 模型输入高度
    
    /**
     * @brief 默认构造函数
     */
    DetectorConfig()
        : confidenceThreshold(0.5f)
        , nmsThreshold(0.45f)
        , inputWidth(640)
        , inputHeight(640) {}
};

/**
 * @brief 目标检测器接口 (纯虚基类)
 * 
 * 设计说明:
 * - 使用纯虚函数定义接口,子类必须实现
 * - 遵循依赖倒置原则(DIP),高层模块依赖抽象
 * - 支持多种检测算法(YOLO、SSD、RetinaNet等)
 * 
 * 面试要点:
 * Q: 为什么要定义纯虚接口而不是直接用具体类?
 * A: 1. 解耦 - TaskManager不依赖具体算法实现
 *    2. 可扩展 - 添加新算法无需修改已有代码
 *    3. 可测试 - 可以mock接口进行单元测试
 *    4. 多态 - 运行时动态绑定,实现算法切换
 * 
 * 使用示例:
 * @code
 * // 创建检测器(通过工厂)
 * std::unique_ptr<IDetector> detector = 
 *     DetectorFactory::create(DetectorType::YOLOV10, config);
 * 
 * // 推理
 * cv::Mat image = cv::imread("test.jpg");
 * DetectionResult result = detector->detect(image);
 * 
 * // 处理结果
 * for (const auto& box : result.boxes) {
 *     std::cout << box.className << ": " << box.confidence << std::endl;
 * }
 * @endcode
 */
class IDetector {
public:
    /**
     * @brief 虚析构函数
     * 
     * @note 面试要点:
     * Q: 为什么基类析构函数必须是虚函数?
     * A: 确保通过基类指针删除派生类对象时,正确调用派生类的析构函数,
     *    避免资源泄漏。这是C++多态的基础要求。
     */
    virtual ~IDetector() = default;
    
    /**
     * @brief 初始化检测器
     * 
     * @param config 检测器配置
     * @return true 初始化成功
     * @return false 初始化失败
     * 
     * @details
     * 初始化流程:
     * 1. 加载模型文件
     * 2. 初始化推理引擎
     * 3. 预热模型(可选)
     * 4. 验证输入输出形状
     */
    virtual bool initialize(const DetectorConfig& config) = 0;
    
    /**
     * @brief 检测目标
     * 
     * @param image 输入图像 (BGR格式)
     * @return DetectionResult 检测结果
     * 
     * @details
     * 检测流程:
     * 1. 预处理: 缩放、归一化、颜色空间转换
     * 2. 推理: 调用TPU/GPU进行前向传播
     * 3. 后处理: NMS、置信度过滤、坐标映射
     * 
     * @note 线程安全性:
     * - 每个IDetector实例不保证线程安全
     * - 多线程场景需要为每个线程创建独立实例
     */
    virtual DetectionResult detect(const cv::Mat& image) = 0;
    
    /**
     * @brief 获取支持的类别列表
     * 
     * @return const std::vector<std::string>& 类别名称列表
     */
    virtual const std::vector<std::string>& getClasses() const = 0;
    
    /**
     * @brief 获取模型输入尺寸
     * 
     * @return cv::Size 输入尺寸 (width, height)
     */
    virtual cv::Size getInputSize() const = 0;
    
    /**
     * @brief 获取检测器名称
     * 
     * @return std::string 检测器名称 (如"YOLOv10", "YOLOv8")
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief 是否已初始化
     * 
     * @return true 已初始化
     * @return false 未初始化
     */
    virtual bool isInitialized() const = 0;

protected:
    /**
     * @brief 保护的默认构造函数
     * 
     * @note 面试要点:
     * Q: 为什么构造函数是protected而不是public?
     * A: 防止直接实例化接口类,只能通过工厂或派生类创建对象。
     *    这是接口类设计的常见做法。
     */
    IDetector() = default;
    
    /**
     * @brief 禁止拷贝构造
     * 
     * @note 面试要点:
     * Q: 为什么要禁止拷贝?
     * A: 检测器内部持有模型资源(TPU内存),拷贝代价高且易出错。
     *    使用std::unique_ptr管理所有权,通过移动语义转移。
     */
    IDetector(const IDetector&) = delete;
    IDetector& operator=(const IDetector&) = delete;
    
    /**
     * @brief 允许移动构造(派生类可选实现)
     */
    IDetector(IDetector&&) = default;
    IDetector& operator=(IDetector&&) = default;
};

}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_IDETECTOR_H_
