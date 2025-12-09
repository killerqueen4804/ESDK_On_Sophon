# ============================================================================
# src/vision/CMakeLists.txt - 视觉处理模块总配置
# ============================================================================
# 功能：管理目标检测、图像分割、视频流处理三个子模块
# 依赖：core、FastDeploy、OpenCV、FFmpeg、GStreamer
# 被依赖：device（视觉结果用于设备决策）
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 添加子模块
# ----------------------------------------------------------------------------
add_subdirectory(detector)
add_subdirectory(segmentation)
add_subdirectory(stream)
add_subdirectory(tracker)    # 新增：目标追踪模块
add_subdirectory(strategy)   # 新增：变焦拍照策略模块

# ----------------------------------------------------------------------------
# 2. 创建vision_common库（包含VisionConfigLoader、VisionUtils等通用类）
# ----------------------------------------------------------------------------
add_library(vision_common STATIC
    VisionConfigLoader.cpp
    VisionUtils.cpp            # Vision统一工具类（包含算法+可视化）
)

# 设置头文件路径
target_include_directories(vision_common
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

# 链接依赖
target_link_libraries(vision_common
    PUBLIC
        core                    # Config、Logger等核心模块
        ThirdParty::OpenCV      # IDetector.h需要OpenCV, VisionUtils需要OpenCV
        ThirdParty::Json        # TaskTypes.h需要nlohmann/json
)

# 添加编译信息
message(STATUS "配置vision_common库: VisionConfigLoader, VisionUtils (已合并Visualizer)")

# ----------------------------------------------------------------------------
# 3. 创建vision顶层接口库（可选）
# ----------------------------------------------------------------------------
# INTERFACE库：不包含源文件，只用于组织依赖关系
# 作用：统一管理vision的三个子模块
add_library(vision INTERFACE)

# ----------------------------------------------------------------------------
# 4. 链接子模块
# ----------------------------------------------------------------------------
# 其他模块只需链接vision，就自动获得所有子模块
target_link_libraries(vision
    INTERFACE
        vision_common   # 配置加载器等通用功能
        detector        # 目标检测
        segmentation    # 图像分割
        stream          # 视频流处理
        tracker         # 新增：目标追踪
        strategy        # 新增：变焦拍照策略
)

# ============================================================================
# 📚 知识点：INTERFACE库
# ============================================================================
#
# 什么是INTERFACE库？
# - 没有编译输出（不生成.a或.so文件）
# - 只用于组织依赖关系和传递属性
# - 类似C++中的纯虚接口类
#
# 使用场景：
# 1. 组合多个子模块：vision = detector + segmentation + stream
# 2. Header-only库：只有头文件，没有cpp文件
# 3. 传递编译选项：统一设置所有子模块的编译参数
#
# 对比三种库：
# 
# add_library(core STATIC ...)
#   - 有编译输出：libcore.a
#   - 包含源文件：Logger.cpp等
#   - 用途：实现具体功能
#
# add_library(vision INTERFACE)
#   - 无编译输出
#   - 不包含源文件
#   - 用途：组织依赖关系
#
# add_library(header_only INTERFACE)
#   - 无编译输出
#   - 只有头文件
#   - 用途：header-only库（如Eigen）
#
# 面试问题：
# Q: 为什么vision用INTERFACE而不是STATIC？
# A: 因为vision本身没有独立的源文件，只是为了方便管理三个子模块。
#    如果用STATIC，必须提供至少一个.cpp文件，这是不必要的。
#    使用INTERFACE可以让上层模块通过一个target就链接所有子模块。
#
# 示例：
# // 不使用INTERFACE（麻烦）
# target_link_libraries(device PRIVATE detector segmentation stream)
#
# // 使用INTERFACE（简洁）
# target_link_libraries(device PRIVATE vision)  // 自动包含三个子模块
# ============================================================================

# ============================================================================
# 📚 知识点：模块化设计的优势
# ============================================================================
#
# 为什么要拆分成detector、segmentation、stream？
#
# 1. 单一职责原则（SOLID中的S）：
#    - detector：只负责目标检测
#    - segmentation：只负责图像分割
#    - stream：只负责视频流处理
#
# 2. 独立编译：
#    - 修改detector不影响segmentation
#    - 增量编译更快
#
# 3. 独立测试：
#    - 可以单独测试每个模块
#    - 降低测试复杂度
#
# 4. 灵活组合：
#    - 某些项目可能只需要detector
#    - 可以独立复用某个模块
#
# 5. 团队协作：
#    - 不同人负责不同子模块
#    - 避免代码冲突
#
# CMake体现：
#   src/vision/
#   ├── CMakeLists.txt           # 顶层：组织子模块
#   ├── detector/
#   │   └── CMakeLists.txt       # 独立：detector的编译配置
#   ├── segmentation/
#   │   └── CMakeLists.txt       # 独立：segmentation的编译配置
#   └── stream/
#       └── CMakeLists.txt       # 独立：stream的编译配置
# ============================================================================
