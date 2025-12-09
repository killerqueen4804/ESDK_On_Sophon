# ============================================================================
# src/storage/CMakeLists.txt - 数据存储模块
# ============================================================================
# 功能：数据库操作、文件下载管理、持久化存储
# 依赖：core、Qt5Sql、libssh2、libexif
# ============================================================================

# ----------------------------------------------------------------------------
# 1. 定义源文件列表
# ----------------------------------------------------------------------------
set(STORAGE_SOURCES
    Database.cpp
    DownloadManager.cpp
    FileManager.cpp
)

# ----------------------------------------------------------------------------
# 2. 创建静态库
# ----------------------------------------------------------------------------
add_library(storage STATIC
    ${STORAGE_SOURCES}
)

# ----------------------------------------------------------------------------
# 3. 设置头文件包含路径
# ----------------------------------------------------------------------------
target_include_directories(storage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ----------------------------------------------------------------------------
# 4. 链接依赖库
# ----------------------------------------------------------------------------
target_link_libraries(storage
    PUBLIC
        core
    PRIVATE
        ThirdParty::Qt5Sql        # 数据库操作
        ThirdParty::libssh2       # SSH文件传输（注意：小写）
        ThirdParty::libexif       # EXIF信息提取（注意：小写）
        # ThirdParty::exiv2       # 扩展元数据处理（暂时注释，缺少依赖库libexpat/zlib/brotli）
)

# ----------------------------------------------------------------------------
# 5. 设置编译选项
# ----------------------------------------------------------------------------
target_compile_options(storage
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ----------------------------------------------------------------------------
# 6. 设置库输出路径
# ----------------------------------------------------------------------------
set_target_properties(storage PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ============================================================================
# 📚 知识点：Repository模式
# ============================================================================
#
# Database类的设计采用Repository模式：
#
# 什么是Repository模式？
# - 在领域层和数据层之间提供抽象
# - 隐藏数据访问细节
# - 提供面向对象的数据操作接口
#
# 代码示例：
#   ```cpp
#   // Database.h（公共接口）
#   class Database {
#   public:
#       static Database& getInstance();
#       
#       // Repository接口（面向业务对象）
#       bool saveDetectionResult(const DetectionResult& result);
#       std::vector<DetectionResult> getResults(const TimeRange& range);
#       bool deleteResult(int id);
#       
#   private:
#       // 数据访问细节（用户看不到）
#       QSqlDatabase db_;
#       bool executeQuery(const std::string& sql);
#   };
#   ```
#
# 不使用Repository的代码（不推荐）：
#   ```cpp
#   // 业务代码中直接写SQL
#   void saveResult(const DetectionResult& result) {
#       QSqlQuery query;
#       query.prepare("INSERT INTO results VALUES (?, ?, ?)");
#       query.addBindValue(result.id);
#       query.addBindValue(result.bbox);
#       query.exec();
#   }
#   ```
#   问题：
#   - SQL语句散落在各处
#   - 难以维护和测试
#   - 切换数据库困难
#
# 使用Repository的代码（推荐）：
#   ```cpp
#   // 业务代码中调用Repository
#   void saveResult(const DetectionResult& result) {
#       Database::getInstance().saveDetectionResult(result);
#   }
#   ```
#   优势：
#   - 业务逻辑不关心SQL
#   - 统一的数据访问入口
#   - 易于mock和测试
#   - 可以切换数据库实现
#
# 面试问题：
# Q: Repository模式和DAO（Data Access Object）有什么区别？
# A: - Repository：面向领域对象，操作整个聚合根
#    - DAO：面向数据表，一个DAO对应一个表
#    - Repository更抽象，DAO更底层
#    - 例：Repository有saveUser()，DAO有insertUser()、insertAddress()
#
# Q: 为什么要用Repository而不是直接用ORM？
# A: - Repository是设计模式，ORM是技术实现
#    - Repository可以基于ORM实现
#    - Repository提供额外的抽象层，便于切换ORM
#    - Repository可以实现缓存、事务等高级功能
# ============================================================================

# ============================================================================
# 📚 知识点：单例模式的正确使用
# ============================================================================
#
# Database为什么用单例？
#
# 合理性：
# 1. 数据库连接是全局唯一的资源
# 2. 避免多个连接导致的竞争
# 3. 方便全局访问
#
# 实现要点：
#   ```cpp
#   class Database {
#   public:
#       // 获取单例
#       static Database& getInstance() {
#           static Database instance;  // C++11保证线程安全
#           return instance;
#       }
#       
#       // 禁用拷贝和赋值
#       Database(const Database&) = delete;
#       Database& operator=(const Database&) = delete;
#       
#   private:
#       // 私有构造函数
#       Database() {
#           // 初始化数据库连接
#       }
#       
#       ~Database() {
#           // 关闭数据库连接
#       }
#   };
#   ```
#
# 线程安全性：
# - C++11之后，静态局部变量的初始化是线程安全的
# - 多线程调用getInstance()不会创建多个实例
# - 但数据库操作本身需要加锁保护
#
# 使用场景：
#   ```cpp
#   // 任何地方都可以访问
#   Database::getInstance().saveDetectionResult(result);
#   ```
#
# 单例的缺点：
# 1. 难以测试（无法mock）
# 2. 隐藏依赖关系
# 3. 全局状态
#
# 改进方案（依赖注入）：
#   ```cpp
#   class DroneController {
#   public:
#       // 构造函数注入
#       explicit DroneController(Database* db) : db_(db) {}
#   private:
#       Database* db_;  // 依赖注入，便于测试
#   };
#   ```
#
# 面试问题：
# Q: 单例模式有哪些缺点？如何改进？
# A: 缺点：
#    1. 全局状态，违反依赖倒置原则
#    2. 难以测试，无法mock
#    3. 隐藏依赖关系
#    改进：
#    1. 使用依赖注入代替单例
#    2. 提供抽象接口（IDatabase），方便mock
#    3. 使用工厂模式创建实例
#
# Q: C++11的单例为什么是线程安全的？
# A: C++11标准保证静态局部变量的初始化是线程安全的。
#    编译器会在第一次调用时加锁，确保只初始化一次。
#    这被称为"Meyers Singleton"（Scott Meyers提出）。
# ============================================================================
