# ============================================================================
# src/vision/detector/CMakeLists.txt - 目标检测子模块
# ============================================================================
# 功能：提供目标检测功能，支持YOLOv10、YOLOv8等多种算法
# 依赖：core、FastDeploy、OpenCV
# 设计模式：工厂模式 + 策略模式
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 定义源文件列表
# ----------------------------------------------------------------------------
set(DETECTOR_SOURCES
    IDetector.cpp
    PPYoloeDetector.cpp      # PP-YOLOE检测器（PaddleDetection）
    DetectorFactory.cpp
    # Yolov8Detector.cpp      # 未来扩展
)

# ----------------------------------------------------------------------------
# 2. 创建静态库
# ----------------------------------------------------------------------------
add_library(detector STATIC
    ${DETECTOR_SOURCES}
)

# ----------------------------------------------------------------------------
# 3. 设置头文件包含路径
# ----------------------------------------------------------------------------
target_include_directories(detector
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}    # 内部头文件
)

# ----------------------------------------------------------------------------
# 4. 链接依赖库
# ----------------------------------------------------------------------------
target_link_libraries(detector
    PUBLIC
        core                           # 日志系统
        vision_common                  # Vision 模块通用工具（包含 VisionUtils）
    PRIVATE
        ThirdParty::FastDeploy        # AI推理引擎
        ThirdParty::OpenCV            # 图像处理
)

# ----------------------------------------------------------------------------
# 5. 设置编译选项
# ----------------------------------------------------------------------------
target_compile_options(detector
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ----------------------------------------------------------------------------
# 6. 设置库输出路径
# ----------------------------------------------------------------------------
set_target_properties(detector PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ============================================================================
# 📚 知识点：工厂模式 + 策略模式
# ============================================================================
#
# 设计模式在本模块的应用：
#
# 1. 策略模式（Strategy Pattern）：
#    目的：定义算法族，让它们可以相互替换
#
#    接口类：IDetector（抽象基类）
#    具体策略：Yolov10Detector、Yolov8Detector
#
#    代码示例：
#    ```cpp
#    // 抽象基类
#    class IDetector {
#    public:
#        virtual ~IDetector() = default;
#        virtual DetectionResult detect(const cv::Mat& image) = 0;
#    };
#
#    // 具体策略1
#    class Yolov10Detector : public IDetector {
#    public:
#        DetectionResult detect(const cv::Mat& image) override {
#            // YOLOv10检测逻辑
#        }
#    };
#
#    // 具体策略2
#    class Yolov8Detector : public IDetector {
#    public:
#        DetectionResult detect(const cv::Mat& image) override {
#            // YOLOv8检测逻辑
#        }
#    };
#    ```
#
# 2. 工厂模式（Factory Pattern）：
#    目的：创建对象，隐藏创建逻辑
#
#    工厂类：DetectorFactory
#
#    代码示例：
#    ```cpp
#    class DetectorFactory {
#    public:
#        enum class Type { YOLOV10, YOLOV8 };
#        
#        static std::unique_ptr<IDetector> create(Type type) {
#            switch (type) {
#                case Type::YOLOV10:
#                    return std::make_unique<Yolov10Detector>();
#                case Type::YOLOV8:
#                    return std::make_unique<Yolov8Detector>();
#                default:
#                    return nullptr;
#            }
#        }
#    };
#    ```
#
# 3. 两者结合使用：
#    ```cpp
#    // 使用工厂创建检测器
#    auto detector = DetectorFactory::create(DetectorFactory::Type::YOLOV10);
#    
#    // 使用统一接口调用
#    auto result = detector->detect(image);
#    
#    // 切换算法无需修改代码
#    auto detector2 = DetectorFactory::create(DetectorFactory::Type::YOLOV8);
#    auto result2 = detector2->detect(image);
#    ```
#
# 优势：
# - 开闭原则（OCP）：添加新算法不修改现有代码
# - 依赖倒置（DIP）：依赖抽象（IDetector）而非具体类
# - 易于扩展：添加YOLOv11只需实现新类
# - 易于测试：可以mock IDetector接口
#
# 面试问题：
# Q: 为什么要用工厂模式而不是直接new？
# A: 1. 隐藏创建细节（模型路径、初始化参数）
#    2. 统一管理对象创建
#    3. 便于扩展（添加新类型只修改工厂）
#    4. 符合单一职责原则（创建逻辑与业务逻辑分离）
#
# Q: 策略模式和继承多态有什么区别？
# A: 策略模式是继承多态的一种应用场景。
#    策略模式强调"算法可替换"，继承多态是实现手段。
#    关键是运行时可以动态切换策略（算法）。
# ============================================================================

# ============================================================================
# 📚 知识点：CMake中的依赖传递
# ============================================================================
#
# 为什么FastDeploy用PRIVATE？
#
# 场景分析：
#   detector使用FastDeploy进行推理
#   
#   // Yolov10Detector.cpp（实现文件）
#   #include <fastdeploy/vision.h>
#   
#   // Yolov10Detector.h（头文件）
#   class Yolov10Detector : public IDetector {
#       // 没有FastDeploy的类型！
#   private:
#       std::unique_ptr<fastdeploy::Runtime> runtime_;  // Pimpl隐藏
#   };
#
# FastDeploy只在.cpp文件中使用，头文件中没有FastDeploy的类型
# → 使用detector的模块不需要知道FastDeploy
# → 用PRIVATE
#
# 对比：如果头文件中有FastDeploy类型
#   // 错误示例（不推荐）
#   class Yolov10Detector : public IDetector {
#   public:
#       fastdeploy::Result detect(...);  // 返回类型是FastDeploy的
#   };
#   
#   // 这种情况必须用PUBLIC
#   target_link_libraries(detector PUBLIC ThirdParty::FastDeploy)
#
# 最佳实践：
# - 接口中使用项目自定义类型（DetectionResult）
# - 内部实现中使用第三方类型（fastdeploy::Result）
# - 这样第三方库就可以用PRIVATE
# ============================================================================
