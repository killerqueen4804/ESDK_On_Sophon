/**
 * @file DetectorFactory.h
 * @brief 检测器工厂 - 负责创建不同类型的检测器
 * 
 * 设计模式: 工厂模式 (Factory Pattern)
 * 职责: 封装检测器创建逻辑,隐藏具体类的实例化细节
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-31
 */

#ifndef ESDK_SOPHON_VISION_DETECTOR_FACTORY_H_
#define ESDK_SOPHON_VISION_DETECTOR_FACTORY_H_

#include "esdk_sophon/vision/IDetector.h"
#include "esdk_sophon/core/Logger.h"  // Logger定义
#include <memory>
#include <string>

namespace esdk_sophon {
namespace vision {

/**
 * @brief 检测器类型枚举
 */
enum class DetectorType {
    PPYOLOE,      ///< PP-YOLOE 检测器 (PaddleDetection)
    YOLOV8,       ///< YOLOv8 检测器 (未来支持)
    YOLOV5,       ///< YOLOv5 检测器 (未来支持)
    UNKNOWN       ///< 未知类型
};

/**
 * @brief 检测器工厂类
 * 
 * 设计说明:
 * - 使用静态工厂方法创建检测器
 * - 隐藏具体检测器的创建细节
 * - 统一管理模型路径、参数等配置
 * - 支持从字符串解析检测器类型
 * 
 * 面试要点:
 * Q: 为什么要用工厂模式而不是直接new对象?
 * A: 1. 封装创建逻辑 - 隐藏复杂的初始化过程
 *    2. 解耦 - 使用者不需要知道具体类名
 *    3. 统一管理 - 集中处理配置、日志、错误
 *    4. 易于扩展 - 添加新类型只修改工厂
 *    5. 符合开闭原则 - 对扩展开放,对修改关闭
 * 
 * Q: 工厂模式 vs 抽象工厂模式?
 * A: - 工厂模式: 创建单一产品族(IDetector)
 *    - 抽象工厂: 创建多个相关产品族(Detector+Segmentor+Tracker)
 *    当前场景使用简单工厂模式即可
 * 
 * 使用示例:
 * @code
 * // 方式1: 直接指定类型
 * DetectorConfig config;
 * config.modelPath = "/path/to/ppyoloe.bmodel";
 * config.classes = {"car", "person", "bicycle"};
 * 
 * auto detector = DetectorFactory::create(
 *     DetectorType::PPYOLOE, config);
 * 
 * // 方式2: 从字符串解析类型
 * auto detector2 = DetectorFactory::createFromString(
 *     "ppyoloe", config);
 * 
 * // 方式3: 自动推断类型(根据模型文件名)
 * auto detector3 = DetectorFactory::createFromModelPath(
 *     "/path/to/ppyoloe_coco.bmodel", config);
 * @endcode
 */
class DetectorFactory {
public:
    /**
     * @brief 创建检测器
     * 
     * @param type 检测器类型
     * @param config 检测器配置
     * @return std::unique_ptr<IDetector> 检测器智能指针
     * 
     * @details
     * 创建流程:
     * 1. 根据type实例化具体检测器类
     * 2. 调用initialize()初始化
     * 3. 返回基类指针(多态)
     * 
     * @note 面试要点:
     * Q: 为什么返回unique_ptr而不是shared_ptr?
     * A: 1. 检测器通常独占所有权,不需要共享
     *    2. unique_ptr更轻量,无原子操作开销
     *    3. 明确表达"唯一所有权"的语义
     *    4. 如需共享,可以通过std::move转换
     * 
     * @throws std::runtime_error 创建失败时抛出异常
     */
    static std::unique_ptr<IDetector> create(
        DetectorType type, 
        const DetectorConfig& config);
    
    /**
     * @brief 从字符串创建检测器
     * 
     * @param typeStr 类型字符串 (不区分大小写)
     *                支持: "yolov10", "yolov8", "yolov5"
     * @param config 检测器配置
     * @return std::unique_ptr<IDetector> 检测器智能指针
     * 
     * @throws std::runtime_error 类型字符串无效或创建失败
     */
    static std::unique_ptr<IDetector> createFromString(
        const std::string& typeStr,
        const DetectorConfig& config);
    
    /**
     * @brief 从模型路径自动推断类型并创建
     * 
     * @param modelPath 模型文件路径
     *                  例如: "/path/to/yolov10_coco.bmodel"
     * @param config 检测器配置 (modelPath会被自动设置)
     * @return std::unique_ptr<IDetector> 检测器智能指针
     * 
     * @details
     * 推断规则:
     * - 文件名包含"yolov10" → YOLOV10
     * - 文件名包含"yolov8"  → YOLOV8
     * - 文件名包含"yolov5"  → YOLOV5
     * - 无法推断 → 抛出异常
     * 
     * @throws std::runtime_error 无法推断类型或创建失败
     */
    static std::unique_ptr<IDetector> createFromModelPath(
        const std::string& modelPath,
        const DetectorConfig& config);
    
    /**
     * @brief 检查是否支持指定类型
     * 
     * @param type 检测器类型
     * @return true 支持
     * @return false 不支持
     */
    static bool isSupported(DetectorType type);
    
    /**
     * @brief 获取所有支持的类型列表
     * 
     * @return std::vector<DetectorType> 类型列表
     */
    static std::vector<DetectorType> getSupportedTypes();
    
    /**
     * @brief 获取类型的字符串表示
     * 
     * @param type 检测器类型
     * @return std::string 类型字符串 (如"YOLOv10")
     */
    static std::string typeToString(DetectorType type);
    
    /**
     * @brief 从字符串解析类型
     * 
     * @param typeStr 类型字符串 (不区分大小写)
     * @return DetectorType 检测器类型
     * 
     * @note 无法解析时返回DetectorType::UNKNOWN
     */
    static DetectorType stringToType(const std::string& typeStr);

private:
    /**
     * @brief 私有构造函数 - 禁止实例化
     * 
     * @note 面试要点:
     * Q: 为什么要禁止实例化工厂类?
     * A: 工厂类只包含静态方法,无需实例化。
     *    禁止实例化可以:
     *    1. 节省内存(不会创建无用对象)
     *    2. 明确设计意图(这是工具类,不是数据类)
     *    3. 避免误用(防止有人new一个工厂对象)
     */
    DetectorFactory() = delete;
    ~DetectorFactory() = delete;
    DetectorFactory(const DetectorFactory&) = delete;
    DetectorFactory& operator=(const DetectorFactory&) = delete;
    
    /**
     * @brief 获取Logger单例引用
     * 
     * @note 优化点:
     * 所有静态方法共享同一个Logger实例,避免重复获取
     * 使用内联函数避免多次调用getInstance()
     */
    static inline core::Logger& getLogger() {
        return core::Logger::getInstance();
    }
};

}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_DETECTOR_FACTORY_H_
