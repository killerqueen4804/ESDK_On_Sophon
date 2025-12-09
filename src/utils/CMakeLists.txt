# ============================================================================
# src/utils/CMakeLists.txt - 工具类模块
# ============================================================================
# 功能：提供通用工具函数，如文件操作、字符串处理、时间处理等
# 依赖：无（仅依赖标准库）
# 特点：纯函数，无状态，可被任何模块使用
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 定义源文件列表
# ----------------------------------------------------------------------------
set(UTILS_SOURCES
    FileUtils.cpp
    StringUtils.cpp
    TimeUtils.cpp
    ImageProcessor.cpp  # 图片处理工具（EXIF解析、图片变换、Base64编码）
    GeoUtils.cpp        # 地理信息工具（GPS计算、距离计算、坐标转换）
    HttpClient.cpp      # HTTP 客户端（GPS坐标计算需要调用 GeoDecodeAPI）
)

# 面试要点：为什么要条件编译？
# - 在依赖库缺失时，不阻塞整个项目的编译
# - 可以通过 CMake 选项控制功能的启用/禁用
# - 例如：option(ENABLE_HTTP_CLIENT "Enable HTTP client support" OFF)

# ----------------------------------------------------------------------------
# 2. 创建静态库
# ----------------------------------------------------------------------------
add_library(utils STATIC
    ${UTILS_SOURCES}
)

# ----------------------------------------------------------------------------
# 3. 设置头文件包含路径
# ----------------------------------------------------------------------------
target_include_directories(utils
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ----------------------------------------------------------------------------
# 4. 查找依赖库
# ----------------------------------------------------------------------------
# TODO: 配置 libcurl（用于 HttpClient）
# 当前暂时注释，因为交叉编译环境中需要手动配置 CURL 路径
# 在实际部署时需要确保 SE7 设备上有 libcurl.so

# find_package(CURL REQUIRED)
# if(CURL_FOUND)
#     message(STATUS "Found CURL: ${CURL_LIBRARIES}")
#     message(STATUS "  Include: ${CURL_INCLUDE_DIRS}")
#     message(STATUS "  Version: ${CURL_VERSION_STRING}")
# else()
#     message(FATAL_ERROR "libcurl not found! Please install libcurl development package.")
# endif()

message(STATUS "✅ CURL 已启用，HttpClient 功能将在运行时动态链接 libcurl.so")

# ----------------------------------------------------------------------------
# 5. 链接依赖库
# ----------------------------------------------------------------------------
target_link_libraries(utils
    PUBLIC
        # 新增依赖
        core                      # Logger 依赖
        ThirdParty::exiv2         # EXIF 解析（ImageProcessor）⭐ 使用 exiv2 替代 libexif
        ThirdParty::OpenCV        # 图片处理（ImageProcessor） - 注意大写C！
        ThirdParty::Json          # JSON 解析（GeoUtils） - 注意大写J！
        ThirdParty::CURL          # HttpClient 依赖 libcurl
)

# ----------------------------------------------------------------------------
# 6. 设置编译选项
# ----------------------------------------------------------------------------
target_compile_options(utils
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ----------------------------------------------------------------------------
# 7. 设置库输出路径
# ----------------------------------------------------------------------------
set_target_properties(utils PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ============================================================================
# 📚 知识点：工具类的设计原则
# ============================================================================
#
# 工具类（Utils）的特点：
#
# 1. 纯函数（Pure Function）：
#    - 无副作用
#    - 相同输入总是产生相同输出
#    - 不依赖外部状态
#
#    ```cpp
#    // FileUtils.h
#    namespace esdk_sophon {
#    namespace utils {
#    
#    class FileUtils {
#    public:
#        // 静态函数，无状态
#        static bool exists(const std::string& path);
#        static std::string getExtension(const std::string& path);
#        static std::vector<std::string> listFiles(const std::string& dir);
#        
#        // 禁止实例化
#        FileUtils() = delete;
#    };
#    
#    }  // namespace utils
#    }  // namespace esdk_sophon
#    ```
#
# 2. 无依赖：
#    - 只使用标准库（std::filesystem, std::string等）
#    - 不依赖第三方库
#    - 可以被任何模块使用，不引入额外依赖
#
# 3. 命名空间组织：
#    ```cpp
#    // 使用命名空间而非类，更符合C++风格
#    namespace file_utils {
#        bool exists(const std::string& path);
#    }
#    
#    // 或者使用类的静态方法
#    class FileUtils {
#    public:
#        static bool exists(const std::string& path);
#    };
#    ```
#
# 4. 易于测试：
#    - 纯函数易于单元测试
#    - 不需要mock
#    - 输入输出明确
#
# 面试问题：
# Q: 工具类应该用静态方法还是命名空间函数？
# A: C++中推荐使用命名空间函数：
#    1. 更符合C++风格（不滥用类）
#    2. 支持ADL（Argument-Dependent Lookup）
#    3. 更简洁
#    
#    // 推荐（命名空间）
#    namespace file_utils {
#        bool exists(const std::string& path);
#    }
#    file_utils::exists("file.txt");
#    
#    // 不推荐但可接受（静态类）
#    class FileUtils {
#    public:
#        static bool exists(const std::string& path);
#    };
#    FileUtils::exists("file.txt");
# ============================================================================

# ============================================================================
# 📚 知识点：为什么utils不依赖core？
# ============================================================================
#
# 依赖层次的设计考虑：
#
# 方案1：utils独立（推荐）
#   third_party
#        ↓
#      utils  ←── 无依赖，最底层
#        ↓
#      core   ←── 依赖utils
#        ↓
#    mqtt/vision/device
#
# 优势：
# - utils可以被core使用
# - 依赖关系清晰，单向流动
# - utils可以独立复用
#
# 方案2：utils依赖core（不推荐）
#   third_party
#        ↓
#      core
#        ↓
#      utils  ←── 依赖core（用Logger）
#        ↓
#    mqtt/vision/device
#
# 问题：
# - 循环依赖风险（core可能想用utils）
# - utils无法独立复用
# - 依赖链过长
#
# 如果utils需要日志怎么办？
#
# 方案A：回调函数（推荐）
#   ```cpp
#   namespace file_utils {
#       using LogCallback = std::function<void(const std::string&)>;
#       
#       bool copyFile(const std::string& src, 
#                    const std::string& dst,
#                    LogCallback log = nullptr) {
#           if (log) log("Copying " + src);
#           // ...
#       }
#   }
#   
#   // 使用时注入Logger
#   file_utils::copyFile("a.txt", "b.txt", 
#       [](const std::string& msg) {
#           Logger::getInstance().info(msg);
#       });
#   ```
#
# 方案B：异常处理
#   ```cpp
#   // utils抛出异常，不处理日志
#   bool copyFile(const std::string& src, const std::string& dst) {
#       if (!exists(src)) {
#           throw std::runtime_error("File not found: " + src);
#       }
#   }
#   
#   // 上层捕获异常并记录日志
#   try {
#       file_utils::copyFile("a.txt", "b.txt");
#   } catch (const std::exception& e) {
#       Logger::getInstance().error(e.what());
#   }
#   ```
#
# 面试问题：
# Q: 如何避免循环依赖？
# A: 1. 依赖倒置原则（DIP）：依赖抽象而非具体类
#    2. 分层架构：明确依赖方向（只能上层依赖下层）
#    3. 接口隔离：使用回调、观察者模式解耦
#    4. 提取公共部分：将双方依赖的部分提到更底层
# ============================================================================

# ============================================================================
# 📚 知识点：C++17 <filesystem> 的使用
# ============================================================================
#
# FileUtils的现代C++实现：
#
# ```cpp
# #include <filesystem>
# 
# namespace fs = std::filesystem;
# 
# class FileUtils {
# public:
#     static bool exists(const std::string& path) {
#         return fs::exists(path);
#     }
#     
#     static std::string getExtension(const std::string& path) {
#         return fs::path(path).extension().string();
#     }
#     
#     static std::vector<std::string> listFiles(const std::string& dir) {
#         std::vector<std::string> files;
#         for (const auto& entry : fs::directory_iterator(dir)) {
#             if (entry.is_regular_file()) {
#                 files.push_back(entry.path().string());
#             }
#         }
#         return files;
#     }
#     
#     static bool createDirectory(const std::string& path) {
#         return fs::create_directories(path);
#     }
# };
# ```
#
# C++17 <filesystem> 的优势：
# 1. 跨平台：自动处理Windows/Linux路径差异
# 2. 类型安全：fs::path比字符串更安全
# 3. 功能丰富：文件大小、时间、权限等
# 4. 标准库：不需要第三方库
#
# 面试问题：
# Q: C++如何实现跨平台的文件操作？
# A: 1. C++17之前：使用POSIX API或Windows API，需要条件编译
#    2. C++17之后：使用<filesystem>，标准库自动处理平台差异
#    3. 第三方库：Boost.Filesystem（C++17的前身）
# ============================================================================
