# ============================================================================
# src/vision/segmentation/CMakeLists.txt - 图像分割子模块
# ============================================================================
# 功能：提供图像分割功能，支持PaddleSeg等算法
# 依赖：core、FastDeploy、OpenCV
# 设计模式：工厂模式 + 策略模式（类似detector）
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 定义源文件列表
# ----------------------------------------------------------------------------
set(SEGMENTATION_SOURCES
    ISegmentor.cpp
    PaddleSegmentor.cpp
    Sam2Segmentor.cpp
)

# ----------------------------------------------------------------------------
# 2. 创建静态库
# ----------------------------------------------------------------------------
add_library(segmentation STATIC
    ${SEGMENTATION_SOURCES}
)

# ----------------------------------------------------------------------------
# 3. 设置头文件包含路径
# ----------------------------------------------------------------------------
target_include_directories(segmentation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ----------------------------------------------------------------------------
# 4. 链接依赖库
# ----------------------------------------------------------------------------
target_link_libraries(segmentation
    PUBLIC
        core
    PRIVATE
        ThirdParty::FastDeploy
        ThirdParty::OpenCV
        ThirdParty::bmlib     # BMLib 底层库 (提供 bm_dev_request, bm_memcpy_s2d 等)
        ThirdParty::bmrt      # BMRuntime 推理引擎
        ThirdParty::bmcv      # BMCV 图像处理加速
)

# ----------------------------------------------------------------------------
# 5. 设置编译选项
# ----------------------------------------------------------------------------
target_compile_options(segmentation
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ----------------------------------------------------------------------------
# 6. 设置库输出路径
# ----------------------------------------------------------------------------
set_target_properties(segmentation PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ============================================================================
# 📚 知识点：代码复用与模块独立性的平衡
# ============================================================================
#
# detector和segmentation的相似性：
# - 都使用工厂+策略模式
# - 都依赖FastDeploy和OpenCV
# - 都有抽象接口和具体实现
#
# 为什么不合并成一个模块？
#
# 1. 职责不同：
#    - detector：识别"物体在哪里"（边界框）
#    - segmentation：识别"像素属于什么"（分割掩码）
#
# 2. 应用场景不同：
#    - detector：目标跟踪、行为分析
#    - segmentation：场景理解、精细分割
#
# 3. 算法差异：
#    - detector：YOLO、SSD、Faster R-CNN
#    - segmentation：U-Net、DeepLab、SegFormer
#
# 4. 独立演进：
#    - detector可能升级到YOLOv11
#    - segmentation可能切换到SAM（Segment Anything）
#    - 互不影响
#
# 如何复用公共代码？
#
# 方案1：抽象基类（推荐）
#   ```cpp
#   // IVisionModel.h（公共接口）
#   class IVisionModel {
#   public:
#       virtual bool initialize(const std::string& modelPath) = 0;
#       virtual ~IVisionModel() = default;
#   };
#
#   // IDetector继承IVisionModel
#   class IDetector : public IVisionModel {
#       virtual DetectionResult detect(...) = 0;
#   };
#
#   // ISegmentor继承IVisionModel
#   class ISegmentor : public IVisionModel {
#       virtual SegmentationResult segment(...) = 0;
#   };
#   ```
#
# 方案2：工具类
#   ```cpp
#   // VisionUtils.h（公共工具）
#   namespace vision_utils {
#       cv::Mat preprocess(const cv::Mat& image);
#       void postprocess(...);
#   }
#   ```
#
# 面试问题：
# Q: 如何判断应该合并模块还是拆分模块？
# A: 1. 看职责是否单一（单一职责原则）
#    2. 看变化原因是否相同（变化的原因是拆分依据）
#    3. 看依赖关系是否合理（避免循环依赖）
#    4. 看团队规模和协作方式（大团队倾向更细粒度）
# ============================================================================
