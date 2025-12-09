/**
 * @file Logger.cpp
 * @brief 日志系统实现（支持日志轮转，配置驱动）
 * @author ESDK_Sophon Team
 * @date 2025-10-25
 * 
 * @details
 * 功能特性：
 * - 按日期自动切换日志文件
 * - 自动清理超过N天的旧日志
 * - 文件大小限制（可选）
 * - 线程安全
 * - 从Config读取配置（解耦）
 * 
 * @note 实现风格说明（2025-10-26 统一重构）
 * 本文件采用"类内声明 + 类外实现"的标准C++风格：
 * - Impl类定义清晰简洁，只包含方法声明和成员变量（~100行）
 * - 所有方法实现在类外，便于阅读、修改和code review
 * - 符合工业界主流实践（Google C++ Style Guide、LLVM Coding Standards）
 * - 与项目其他模块（如MqttClient）保持一致的代码风格
 * 
 * 重构原因：
 * - 统一项目代码风格
 * - 提升代码可维护性
 * - 便于后续扩展和优化
 */

#include "esdk_sophon/core/Logger.h"
// 📌 移除对 Config 的依赖，避免循环依赖问题
// Logger 直接读取 logger.json，不依赖 Config 类

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <mutex>
#include <filesystem>  // C++17文件系统库
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>  // 📌 直接使用 nlohmann/json 解析 logger.json
// #include <locale>      // 📌 移除 locale 依赖，直接写入 UTF-8 字节流

namespace esdk_sophon {
namespace core {

// 使用namespace简化代码
namespace fs = std::filesystem;

// ============================================================================
// 日志轮转配置（从Config读取，提供默认值作为降级方案）
// ============================================================================
namespace {
    // 默认配置常量（当 logger.json 未找到或配置项缺失时使用）
    constexpr const char* DEFAULT_LOG_DIRECTORY = "logs";
    constexpr const char* DEFAULT_LOG_FILE_PREFIX = "esdk_sophon_";
    constexpr int DEFAULT_MAX_LOG_DAYS = 15;
    constexpr size_t DEFAULT_MAX_LOG_FILE_SIZE_MB = 100;
    
    // 📌 Logger 专用配置文件路径（相对于可执行文件）
    // 注意：与 config.json 在同一目录下
    constexpr const char* LOGGER_CONFIG_FILE = "../config/logger.json";
}

// ============================================================================
// Pimpl实现类定义
// ============================================================================

/**
 * @brief Logger的实现类（Pimpl惯用法）
 * 
 * @details
 * 隐藏实现细节，包含：
 * - 日志文件流
 * - 互斥锁（线程安全）
 * - 日志级别
 * - 格式化方法
 * 
 * 【教学要点】为什么使用Pimpl？
 * 1. 编译防火墙：修改实现不需要重新编译依赖Logger.h的文件
 * 2. 隐藏细节：头文件不需要包含<fstream>、<mutex>等，减少依赖
 * 3. 二进制兼容：修改Impl不影响ABI
 * 
 * @note 实现风格说明
 * 采用"类内声明 + 类外实现"的标准C++风格：
 * - 类定义清晰简洁，只包含方法声明和成员变量
 * - 所有方法实现在类外，便于阅读和维护
 * - 符合工业界主流实践（Google/LLVM/Qt等项目）
 */
class Logger::Impl {
public:
    // ===== 构造/析构 =====
    Impl();
    ~Impl();
    
    // 禁止拷贝和赋值
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    
    // ===== 公共接口 =====
    
    /**
     * @brief 核心日志记录方法
     * @param level 日志级别
     * @param message 日志消息
     */
    void log(LogLevel level, const std::string& message);
    
    /**
     * @brief 设置日志级别
     * @param level 新的日志级别
     */
    void setLevel(LogLevel level);
    
    /**
     * @brief 设置日志文件路径
     * @param filePath 日志文件路径
     * @return true 设置成功，false 失败
     */
    bool setLogFile(const std::string& filePath);
    
    /**
     * @brief 启用/禁用控制台输出
     * @param enable true启用，false禁用
     */
    void setConsoleOutput(bool enable);

private:
    // ===== 格式化方法 =====
    
    /**
     * @brief 格式化日志行
     * @param level 日志级别
     * @param message 消息内容
     * @return 格式化后的日志字符串
     */
    std::string formatLogLine(LogLevel level, const std::string& message);
    
    /**
     * @brief 获取当前时间字符串
     * @return 格式化的时间字符串 (YYYY-MM-DD HH:MM:SS)
     */
    std::string getCurrentTime();
    
    /**
     * @brief 日志级别转字符串
     * @param level 日志级别枚举
     * @return 级别字符串
     */
    std::string levelToString(LogLevel level);
    
    // ===== 日志轮转方法 =====
    
    /**
     * @brief 创建日志目录
     */
    void createLogDirectory();
    
    /**
     * @brief 获取当前日期字符串
     * @return 格式化的日期字符串 (YYYY-MM-DD)
     */
    std::string getCurrentDate();
    
    /**
     * @brief 生成日志文件路径
     * @param date 日期字符串
     * @return 完整的日志文件路径
     */
    std::string generateLogFilePath(const std::string& date);
    
    /**
     * @brief 轮转日志文件
     */
    void rotateLogFile();
    
    /**
     * @brief 检查并轮转日志
     */
    void checkAndRotateLog();
    
    /**
     * @brief 带序号的日志轮转
     */
    void rotateLogFileWithSequence();
    
    /**
     * @brief 清理过期日志
     */
    void cleanupOldLogs();
    
    // ===== 配置方法 =====
    
    /**
     * @brief 从Config加载配置
     */
    void loadConfigSettings();
    
    /**
     * @brief 字符串转日志级别
     * @param levelStr 日志级别字符串
     * @return 日志级别枚举
     */
    LogLevel stringToLogLevel(const std::string& levelStr) const;

private:
    // ===== 成员变量 =====
    
    // 文件和线程控制
    std::ofstream logFile_;       ///< 日志文件流
    std::mutex mutex_;             ///< 互斥锁（保护多线程访问）
    LogLevel currentLevel_;        ///< 当前日志级别
    bool enableConsole_;           ///< 是否启用控制台输出
    std::string logFilePath_;      ///< 当前日志文件路径
    std::string currentDate_;      ///< 当前日期（用于检测日期变化）
    
    // 从Config读取的配置参数
    std::string logDirectory_;     ///< 日志目录（从Config读取）
    int maxLogDays_;               ///< 日志保留天数（从Config读取）
    int maxLogFileSizeMB_;         ///< 单文件最大大小MB（从Config读取）
    size_t maxLogFileSizeBytes_;   ///< 单文件最大大小字节（计算值）
};

// ============================================================================
// Impl 构造/析构实现
// ============================================================================

/**
 * @brief 构造函数实现
 * 
 * @details
 * 初始化流程：
 * 1. 从Config加载配置
 * 2. 创建日志目录
 * 3. 清理过期日志
 * 4. 打开今天的日志文件
 * 
 * 【知识点】成员初始化列表
 * - 按照成员变量声明顺序初始化
 * - 效率高于在构造函数体内赋值
 * - 引用和const成员必须在初始化列表中初始化
 */
Logger::Impl::Impl()
    : currentLevel_(LogLevel::INFO)   // 默认日志级别：INFO
    , enableConsole_(true)             // 默认启用控制台输出
    , currentDate_("")                 // 当前日期（用于检测日期变化）
{
    // 1. 从Config加载配置
    loadConfigSettings();
    
    // 2. 创建日志目录（如果不存在）
    createLogDirectory();
    
    // 3. 清理过期日志
    cleanupOldLogs();
    
    // 4. 打开今天的日志文件
    rotateLogFile();
}

/**
 * @brief 析构函数实现
 * 
 * @details
 * RAII原则：析构函数自动关闭文件
 * - 确保缓冲区内容写入
 * - 正确关闭文件句柄
 * - 异常安全
 */
Logger::Impl::~Impl() {
    if (logFile_.is_open()) {
        logFile_.flush();  // 确保缓冲区内容写入
        logFile_.close();
    }
}

// ============================================================================
// Impl 公共接口实现
// ============================================================================

/**
 * @brief 核心日志记录方法实现
 * 
 * @param level 日志级别
 * @param message 日志消息
 * 
 * @details
 * 实现步骤：
 * 1. 检查日志级别过滤
 * 2. 检查并轮转日志文件
 * 3. 格式化日志行
 * 4. 加锁保证线程安全
 * 5. 写入文件和控制台
 * 
 * 【知识点】std::lock_guard
 * - RAII风格的互斥锁管理
 * - 构造时自动加锁，析构时自动解锁
 * - 异常安全：即使发生异常也会正确解锁
 */
void Logger::Impl::log(LogLevel level, const std::string& message) {
    // 1. 日志级别过滤：只记录大于等于当前级别的日志
    if (level < currentLevel_) {
        return;
    }
    
    // 2. 检查是否需要轮转日志文件（日期变化或文件过大）
    checkAndRotateLog();
    
    // 3. 格式化日志行
    std::string logLine = formatLogLine(level, message);
    
    // 4. 加锁保证线程安全（RAII）
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 5. 写入文件
    if (logFile_.is_open()) {
        logFile_ << logLine << std::endl;
        logFile_.flush();  // 立即刷新，确保日志不丢失
    }
    
    // 6. 写入控制台
    if (enableConsole_) {
        // 根据日志级别选择输出流
        if (level >= LogLevel::ERROR) {
            std::cerr << logLine << std::endl;  // 错误输出到stderr
        } else {
            std::cout << logLine << std::endl;  // 普通输出到stdout
        }
    }
}

/**
 * @brief 设置日志级别实现
 * 
 * @param level 新的日志级别
 * 
 * @details
 * 线程安全：虽然只是简单赋值，但为了保证一致性，还是加锁
 */
void Logger::Impl::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentLevel_ = level;
}

/**
 * @brief 设置日志文件路径实现
 * 
 * @param filePath 日志文件路径
 * @return true 设置成功，false 失败
 * 
 * @details
 * 手动指定日志文件，会覆盖自动轮转的文件
 * 注意：手动设置后，自动日期轮转仍然生效
 */
bool Logger::Impl::setLogFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 关闭旧文件
    if (logFile_.is_open()) {
        logFile_.flush();
        logFile_.close();
    }
    
    // 打开新文件（使用 binary 模式以便写入 BOM）
    logFilePath_ = filePath;
    logFile_.open(filePath, std::ios::app | std::ios::out | std::ios::binary);
    
    if (!logFile_.is_open()) {
        std::cerr << "Failed to open log file: " << filePath << std::endl;
        return false;
    }
    
    // 📌 关键修复：写入 UTF-8 BOM（仅对新文件）
    // 使用 tellp() 检查文件大小，比 fs::exists 更可靠
    logFile_.seekp(0, std::ios::end);
    if (logFile_.tellp() == 0) {
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        logFile_.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        logFile_.flush();
        // 仅在控制台输出提示，避免污染日志文件头
        if (enableConsole_) {
            std::cout << "[Logger] Created new log file with UTF-8 BOM: " << logFilePath_ << std::endl;
        }
    }
    
    return true;
}

/**
 * @brief 启用/禁用控制台输出实现
 * 
 * @param enable true启用，false禁用
 */
void Logger::Impl::setConsoleOutput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    enableConsole_ = enable;
}

// ============================================================================
// Impl 格式化方法实现
// ============================================================================

/**
 * @brief 格式化日志行实现
 * 
 * @param level 日志级别
 * @param message 消息内容
 * @return 格式化后的日志字符串
 * 
 * @details
 * 日志格式：[时间] [级别] 消息
 * 例如：[2025-10-26 16:30:45] [INFO] 程序启动成功
 * 
 * 【知识点】std::stringstream
 * - 字符串拼接的高效方式
 * - 比多次字符串+操作性能更好
 * - 支持各种类型的格式化输出
 */
std::string Logger::Impl::formatLogLine(LogLevel level, const std::string& message) {
    std::stringstream ss;
    
    // 1. 添加时间戳
    ss << "[" << getCurrentTime() << "] ";
    
    // 2. 添加日志级别
    ss << "[" << levelToString(level) << "] ";
    
    // 3. 添加消息内容
    ss << message;
    
    return ss.str();
}

/**
 * @brief 获取当前时间字符串实现
 * 
 * @return 格式化的时间字符串 (YYYY-MM-DD HH:MM:SS)
 * 
 * @details
 * 【知识点】std::chrono时间处理
 * 1. system_clock::now() - 获取当前系统时间
 * 2. time_t - C风格时间类型，用于格式化
 * 3. localtime() - 转换为本地时间（注意：非线程安全）
 * 4. put_time() - C++11格式化输出
 * 
 * 【面试要点】
 * Q: localtime()为什么不是线程安全的？
 * A: localtime()使用静态内存存储结果，多线程调用会相互覆盖。
 *    线程安全版本：Windows用localtime_s，Linux用localtime_r，
 *    C++20用std::chrono::zoned_time
 */
std::string Logger::Impl::getCurrentTime() {
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    
    // 转换为time_t（C风格时间）
    auto now_c = std::chrono::system_clock::to_time_t(now);
    
    // 格式化输出
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    
    return ss.str();
}

/**
 * @brief 日志级别转字符串实现
 * 
 * @param level 日志级别枚举
 * @return 级别字符串
 * 
 * @details
 * 【知识点】enum class的好处
 * - 强类型：不会隐式转换为int
 * - 作用域：LogLevel::INFO 而不是 INFO
 * - 避免命名冲突
 * 
 * 【面试要点】
 * Q: enum class 和 enum 的区别？
 * A: 1. enum class是强类型，不会隐式转换
 *    2. enum class有作用域，必须用LogLevel::INFO
 *    3. enum class更安全，避免命名冲突
 */
std::string Logger::Impl::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO ";
        case LogLevel::WARNING: return "WARN ";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "UNKN ";
    }
}

// ============================================================================
// Impl 日志轮转方法实现
// ============================================================================

/**
 * @brief 创建日志目录实现
 * 
 * @details
 * 【知识点】std::filesystem::create_directories
 * - C++17标准库，用于创建目录
 * - 自动创建父目录（类似mkdir -p）
 * - 如果目录已存在，不报错
 * 
 * 【面试要点】
 * Q: C++如何创建目录？
 * A: C++17用std::filesystem::create_directories，
 *    之前需要用平台相关的API（Windows的_mkdir，Linux的mkdir）
 */
void Logger::Impl::createLogDirectory() {
    try {
        fs::path logDir(logDirectory_);
        if (!fs::exists(logDir)) {
            fs::create_directories(logDir);
            std::cout << "创建日志目录: " << logDirectory_ << std::endl;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "无法创建日志目录: " << e.what() << std::endl;
    }
}

/**
 * @brief 获取当前日期字符串实现
 * 
 * @return 格式化的日期字符串 (YYYY-MM-DD)
 * 
 * @details
 * 用于生成日志文件名和检测日期变化
 */
std::string Logger::Impl::getCurrentDate() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d");
    
    return ss.str();
}

/**
 * @brief 生成日志文件路径实现
 * 
 * @param date 日期字符串
 * @return 完整的日志文件路径
 * 
 * @details
 * 例如：logs/esdk_sophon_2025-10-26.log
 * 
 * 【知识点】std::filesystem::path
 * - 跨平台路径处理
 * - 自动处理路径分隔符（Windows的\和Linux的/）
 * - 支持路径拼接：path1 / path2
 */
std::string Logger::Impl::generateLogFilePath(const std::string& date) {
    fs::path logPath = fs::path(logDirectory_) / (DEFAULT_LOG_FILE_PREFIX + date + ".log");
    return logPath.string();
}

/**
 * @brief 轮转日志文件实现
 * 
 * @details
 * 切换到新的日志文件：
 * 1. 关闭当前文件
 * 2. 生成新文件路径
 * 3. 打开新文件
 * 4. 更新当前日期
 * 
 * 【项目示例】
 * 每天0点或程序启动时调用，切换到新的日期文件
 */
void Logger::Impl::rotateLogFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 关闭当前日志文件
    if (logFile_.is_open()) {
        logFile_.flush();
        logFile_.close();
    }
    
    // 获取今天的日期
    currentDate_ = getCurrentDate();
    
    // 生成新的日志文件路径
    logFilePath_ = generateLogFilePath(currentDate_);
    
    // 打开新文件（使用 binary 模式以便写入 BOM）
    logFile_.open(logFilePath_, std::ios::app | std::ios::out | std::ios::binary);
    
    if (!logFile_.is_open()) {
        std::cerr << "无法打开日志文件: " << logFilePath_ << std::endl;
    } else {
        // 📌 关键修复：写入 UTF-8 BOM（仅对新文件）
        logFile_.seekp(0, std::ios::end);
        if (logFile_.tellp() == 0) {
            const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            logFile_.write(reinterpret_cast<const char*>(bom), sizeof(bom));
            logFile_.flush();
        }
        
        // 写入日志轮转标记
        logFile_ << "\n========================================\n";
        logFile_ << "日志轮转: " << getCurrentTime() << "\n";
        logFile_ << "日志文件: " << logFilePath_ << "\n";
        logFile_ << "========================================\n";
        logFile_.flush();
    }
}

/**
 * @brief 检查并轮转日志实现
 * 
 * @details
 * 检查条件：
 * 1. 日期是否变化（跨天）
 * 2. 文件大小是否超限（可选）
 * 
 * 满足任一条件则轮转
 * 
 * 【性能优化】
 * - 只在写日志时检查，不用定时器
 * - 加锁操作，保证线程安全
 */
void Logger::Impl::checkAndRotateLog() {
    std::string today = getCurrentDate();
    
    // 检查日期是否变化
    if (today != currentDate_) {
        rotateLogFile();
        return;
    }
    
    // 检查文件大小（可选）
    if (maxLogFileSizeBytes_ > 0) {
        try {
            if (fs::exists(logFilePath_)) {
                auto fileSize = fs::file_size(logFilePath_);
                if (fileSize >= maxLogFileSizeBytes_) {
                    // 文件过大，添加序号后缀
                    rotateLogFileWithSequence();
                }
            }
        } catch (const fs::filesystem_error& e) {
            // 忽略文件大小检查失败
        }
    }
}

/**
 * @brief 带序号的日志轮转实现
 * 
 * @details
 * 当单个日期的日志文件过大时：
 * esdk_sophon_2025-10-26.log (100MB)
 * → esdk_sophon_2025-10-26_001.log
 * → esdk_sophon_2025-10-26_002.log
 */
void Logger::Impl::rotateLogFileWithSequence() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (logFile_.is_open()) {
        logFile_.flush();
        logFile_.close();
    }
    
    // 查找下一个可用序号
    int sequence = 1;
    std::string newPath;
    do {
        std::stringstream ss;
        ss << logDirectory_ << "/" << DEFAULT_LOG_FILE_PREFIX << currentDate_ 
           << "_" << std::setw(3) << std::setfill('0') << sequence << ".log";
        newPath = ss.str();
        sequence++;
    } while (fs::exists(newPath));
    
    // 打开新文件（使用 binary 模式以便写入 BOM）
    logFilePath_ = newPath;
    logFile_.open(logFilePath_, std::ios::app | std::ios::out | std::ios::binary);
    
    if (logFile_.is_open()) {
        // 📌 关键修复：写入 UTF-8 BOM（仅对新文件）
        logFile_.seekp(0, std::ios::end);
        if (logFile_.tellp() == 0) {
            const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            logFile_.write(reinterpret_cast<const char*>(bom), sizeof(bom));
            logFile_.flush();
        }
        
        logFile_ << "\n========================================\n";
        logFile_ << "日志文件过大，切换至: " << logFilePath_ << "\n";
        logFile_ << "========================================\n";
        logFile_.flush();
    }
}

/**
 * @brief 清理过期日志实现
 * 
 * @details
 * 删除超过MAX_LOG_DAYS天的日志文件
 * 
 * 【知识点】文件时间处理
 * - fs::last_write_time: 获取文件最后修改时间
 * - fs::file_time_type: 文件时间类型
 * - chrono::duration_cast: 时间单位转换
 * 
 * 【面试要点】
 * Q: 如何管理日志文件，避免磁盘占满？
 * A: 1. 按日期轮转（每天一个文件）
 *    2. 文件大小限制（单文件最大100MB）
 *    3. 定期清理旧日志（保留N天）
 *    4. 日志压缩（可选，节省空间）
 */
void Logger::Impl::cleanupOldLogs() {
    try {
        fs::path logDir(logDirectory_);
        if (!fs::exists(logDir)) {
            return;
        }
        
        // 计算截止日期（保留maxLogDays_天）
        auto now = std::chrono::system_clock::now();
        auto cutoffTime = now - std::chrono::hours(24 * maxLogDays_);
        
        int deletedCount = 0;
        
        // 遍历日志目录
        for (const auto& entry : fs::directory_iterator(logDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            
            // 只处理我们的日志文件
            std::string filename = entry.path().filename().string();
            if (filename.find(DEFAULT_LOG_FILE_PREFIX) != 0) {
                continue;
            }
            
            // 检查文件年龄
            auto fileTime = fs::last_write_time(entry.path());
            
            // 将file_time转换为system_clock时间（C++20更简单）
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            
            if (sctp < cutoffTime) {
                fs::remove(entry.path());
                deletedCount++;
                std::cout << "清理过期日志: " << filename << std::endl;
            }
        }
        
        if (deletedCount > 0) {
            std::cout << "共清理 " << deletedCount << " 个过期日志文件" << std::endl;
        }
        
    } catch (const fs::filesystem_error& e) {
        std::cerr << "清理日志时出错: " << e.what() << std::endl;
    }
}

// ============================================================================
// Impl 配置方法实现
// ============================================================================

/**
 * @brief 从Config加载配置实现
 * 
 * @details
 * 【知识点】解决循环依赖问题
 * 
 * 问题背景：
 * - Logger 是最先初始化的单例
 * - Config 内部可能使用 Logger 记录日志
 * - 如果 Logger 依赖 Config，就会形成循环依赖
 * 
 * 解决方案：
 * - Logger 直接读取 logger.json 文件
 * - 不依赖 Config 类，完全独立初始化
 * - 使用 nlohmann/json 直接解析 JSON
 * 
 * 【面试要点】
 * Q: 如何解决单例之间的循环依赖？
 * A: 1. 让基础服务（如Logger）独立初始化，不依赖其他单例
 *    2. 使用配置文件分离（logger.json vs config.json）
 *    3. 延迟初始化：先创建对象，后加载依赖的配置
 */
void Logger::Impl::loadConfigSettings() {
    // 使用 nlohmann::json 的别名
    using json = nlohmann::json;
    
    try {
        // 1. 尝试打开 logger.json 配置文件
        std::ifstream configFile(LOGGER_CONFIG_FILE);
        
        if (!configFile.is_open()) {
            // 配置文件不存在，使用默认值
            std::cout << "[Logger配置] 未找到配置文件 " << LOGGER_CONFIG_FILE 
                      << "，使用默认配置" << std::endl;
            logDirectory_ = DEFAULT_LOG_DIRECTORY;
            maxLogDays_ = DEFAULT_MAX_LOG_DAYS;
            maxLogFileSizeMB_ = DEFAULT_MAX_LOG_FILE_SIZE_MB;
            maxLogFileSizeBytes_ = static_cast<size_t>(maxLogFileSizeMB_) * 1024 * 1024;
            return;
        }
        
        // 2. 解析 JSON 配置文件
        json configData = json::parse(configFile);
        
        // 3. 读取 logger 配置节点
        if (!configData.contains("logger")) {
            std::cout << "[Logger配置] 配置文件中缺少 'logger' 节点，使用默认配置" << std::endl;
            logDirectory_ = DEFAULT_LOG_DIRECTORY;
            maxLogDays_ = DEFAULT_MAX_LOG_DAYS;
            maxLogFileSizeMB_ = DEFAULT_MAX_LOG_FILE_SIZE_MB;
            maxLogFileSizeBytes_ = static_cast<size_t>(maxLogFileSizeMB_) * 1024 * 1024;
            return;
        }
        
        const auto& loggerConfig = configData["logger"];
        
        // 4. 读取各配置项（带默认值）
        
        // 日志级别
        std::string levelStr = "INFO";
        if (loggerConfig.contains("level") && loggerConfig["level"].is_string()) {
            levelStr = loggerConfig["level"].get<std::string>();
        }
        currentLevel_ = stringToLogLevel(levelStr);
        
        // 控制台输出
        if (loggerConfig.contains("console_output") && loggerConfig["console_output"].is_boolean()) {
            enableConsole_ = loggerConfig["console_output"].get<bool>();
        } else {
            enableConsole_ = true;  // 默认启用
        }
        
        // 日志目录
        if (loggerConfig.contains("log_directory") && loggerConfig["log_directory"].is_string()) {
            logDirectory_ = loggerConfig["log_directory"].get<std::string>();
        } else {
            logDirectory_ = DEFAULT_LOG_DIRECTORY;
        }
        
        // 5. 读取 rotation 配置节点
        if (loggerConfig.contains("rotation") && loggerConfig["rotation"].is_object()) {
            const auto& rotation = loggerConfig["rotation"];
            
            // 日志保留天数
            if (rotation.contains("max_days") && rotation["max_days"].is_number_integer()) {
                maxLogDays_ = rotation["max_days"].get<int>();
            } else {
                maxLogDays_ = DEFAULT_MAX_LOG_DAYS;
            }
            
            // 单文件最大大小
            if (rotation.contains("max_file_size_mb") && rotation["max_file_size_mb"].is_number_integer()) {
                maxLogFileSizeMB_ = rotation["max_file_size_mb"].get<int>();
            } else {
                maxLogFileSizeMB_ = DEFAULT_MAX_LOG_FILE_SIZE_MB;
            }
        } else {
            maxLogDays_ = DEFAULT_MAX_LOG_DAYS;
            maxLogFileSizeMB_ = DEFAULT_MAX_LOG_FILE_SIZE_MB;
        }
        
        // 6. 计算最大文件大小（字节）
        maxLogFileSizeBytes_ = static_cast<size_t>(maxLogFileSizeMB_) * 1024 * 1024;
        
        // 7. 输出配置加载结果
        std::cout << "[Logger配置] 从 " << LOGGER_CONFIG_FILE << " 加载成功:" << std::endl;
        std::cout << "  - 日志级别: " << levelStr << std::endl;
        std::cout << "  - 日志目录: " << logDirectory_ << std::endl;
        std::cout << "  - 控制台输出: " << (enableConsole_ ? "是" : "否") << std::endl;
        std::cout << "  - 保留天数: " << maxLogDays_ << std::endl;
        std::cout << "  - 文件大小限制: " << maxLogFileSizeMB_ << "MB" << std::endl;
        
    } catch (const json::parse_error& e) {
        // JSON 解析错误
        std::cerr << "[Logger配置] JSON解析错误: " << e.what() << std::endl;
        std::cerr << "[Logger配置] 使用默认配置" << std::endl;
        logDirectory_ = DEFAULT_LOG_DIRECTORY;
        maxLogDays_ = DEFAULT_MAX_LOG_DAYS;
        maxLogFileSizeMB_ = DEFAULT_MAX_LOG_FILE_SIZE_MB;
        maxLogFileSizeBytes_ = static_cast<size_t>(maxLogFileSizeMB_) * 1024 * 1024;
        
    } catch (const std::exception& e) {
        // 其他异常
        std::cerr << "[Logger配置] 加载配置失败: " << e.what() << std::endl;
        std::cerr << "[Logger配置] 使用默认配置" << std::endl;
        logDirectory_ = DEFAULT_LOG_DIRECTORY;
        maxLogDays_ = DEFAULT_MAX_LOG_DAYS;
        maxLogFileSizeMB_ = DEFAULT_MAX_LOG_FILE_SIZE_MB;
        maxLogFileSizeBytes_ = static_cast<size_t>(maxLogFileSizeMB_) * 1024 * 1024;
    }
}

/**
 * @brief 字符串转日志级别实现
 * 
 * @param levelStr 日志级别字符串
 * @return 日志级别枚举
 * 
 * @details
 * 支持的字符串：DEBUG, INFO, WARNING, ERROR, FATAL
 * 默认返回INFO
 */
LogLevel Logger::Impl::stringToLogLevel(const std::string& levelStr) const {
    if (levelStr == "DEBUG") return LogLevel::DEBUG;
    if (levelStr == "INFO") return LogLevel::INFO;
    if (levelStr == "WARNING") return LogLevel::WARNING;
    if (levelStr == "ERROR") return LogLevel::ERROR;
    if (levelStr == "FATAL") return LogLevel::FATAL;
    return LogLevel::INFO;  // 默认INFO
}

// ============================================================================
// Logger公共接口实现（单例模式）
// ============================================================================

/**
 * @brief 构造函数（私有）
 * 
 * @details
 * 【知识点】Pimpl的构造
 * - 使用std::make_unique创建实现对象
 * - 异常安全：如果构造失败，自动清理
 * - 私有构造函数：只能通过getInstance()访问，确保单例
 */
Logger::Logger() 
    : pImpl_(std::make_unique<Impl>())  // C++14推荐用法
{
    // Pimpl构造：实现在Impl类中
}

/**
 * @brief 析构函数
 * 
 * @details
 * 【知识点】为什么需要在.cpp中定义析构函数？
 * - Pimpl惯用法要求：如果只在.h中声明析构函数，编译器看不到Impl的完整定义
 * - unique_ptr需要知道如何删除Impl对象
 * - 在.cpp中定义，此时Impl已经完整定义，unique_ptr可以正确调用Impl的析构函数
 * 
 * 【面试要点】
 * Q: unique_ptr和Pimpl一起使用时要注意什么？
 * A: 必须在.cpp中定义析构函数（即使是=default），否则编译错误（incomplete type）
 */
Logger::~Logger() = default;  // 使用默认实现，但必须在.cpp中定义

/**
 * @brief 获取Logger单例
 * 
 * @return Logger的全局唯一实例
 * 
 * @details
 * 【知识点】C++11线程安全的单例模式（Meyers Singleton）
 * 
 * 1. 静态局部变量：
 *    - C++11保证局部static变量的初始化是线程安全的
 *    - 只会构造一次，即使多个线程同时调用
 * 
 * 2. Magic Static：
 *    - 编译器会自动添加锁保证线程安全
 *    - 性能优于手动加锁的单例实现
 * 
 * 3. 懒汉式vs饿汉式：
 *    - 懒汉式：第一次使用时才创建（本实现）
 *    - 饿汉式：程序启动时就创建
 *    - 懒汉式节省资源，适合大对象
 * 
 * 【面试要点】
 * Q: 如何实现线程安全的单例模式？
 * A: C++11之前需要双检锁（DCLP），C++11后使用静态局部变量即可，
 *    编译器保证线程安全。
 * 
 * Q: 为什么返回引用而不是指针？
 * A: 1. 防止用户delete单例对象
 *    2. 使用更自然：Logger::getInstance().info() vs Logger::getInstance()->info()
 *    3. 不会返回nullptr，更安全
 * 
 * 【项目示例】
 * @code
 * // 多线程环境下安全使用
 * void thread1() {
 *     Logger::getInstance().info("Thread 1 log");
 * }
 * void thread2() {
 *     Logger::getInstance().info("Thread 2 log");
 * }
 * @endcode
 */
Logger& Logger::getInstance() {
    static Logger instance;  // Magic Static：线程安全的懒汉单例
    return instance;
}

// ============================================================================
// 日志记录接口
// ============================================================================

/**
 * @brief DEBUG级别日志
 * 
 * @details
 * 用途：详细的调试信息，开发阶段使用
 * 生产环境建议关闭DEBUG级别以提升性能
 * 
 * 【项目示例】
 * @code
 * Logger::getInstance().debug("检测到目标: bbox=[100,200,300,400]");
 * @endcode
 */
void Logger::debug(const std::string& message) {
    pImpl_->log(LogLevel::DEBUG, message);
}

/**
 * @brief INFO级别日志
 * 
 * @details
 * 用途：常规信息，如程序启动、配置加载、正常操作流程
 * 生产环境默认级别
 * 
 * 【项目示例】
 * @code
 * Logger::getInstance().info("MQTT客户端连接成功");
 * Logger::getInstance().info("加载配置文件: config.json");
 * @endcode
 */
void Logger::info(const std::string& message) {
    pImpl_->log(LogLevel::INFO, message);
}

/**
 * @brief WARNING级别日志
 * 
 * @details
 * 用途：警告信息，如配置缺失但使用默认值、性能下降、即将废弃的功能
 * 不影响程序运行，但需要注意
 * 
 * 【项目示例】
 * @code
 * Logger::getInstance().warning("未找到配置项mqtt.port，使用默认值1883");
 * Logger::getInstance().warning("检测器推理耗时过长: 150ms");
 * @endcode
 */
void Logger::warning(const std::string& message) {
    pImpl_->log(LogLevel::WARNING, message);
}

/**
 * @brief ERROR级别日志
 * 
 * @details
 * 用途：错误信息，如文件读取失败、网络连接失败
 * 影响部分功能，但程序可以继续运行
 * 
 * 【项目示例】
 * @code
 * Logger::getInstance().error("无法打开配置文件: config.json");
 * Logger::getInstance().error("MQTT连接断开，尝试重连...");
 * @endcode
 */
void Logger::error(const std::string& message) {
    pImpl_->log(LogLevel::ERROR, message);
}

/**
 * @brief FATAL级别日志
 * 
 * @details
 * 用途：致命错误，程序无法继续运行
 * 记录后通常会终止程序
 * 
 * 【项目示例】
 * @code
 * Logger::getInstance().fatal("模型文件不存在，程序终止");
 * @endcode
 */
void Logger::fatal(const std::string& message) {
    pImpl_->log(LogLevel::FATAL, message);
}

// ============================================================================
// 配置接口
// ============================================================================

/**
 * @brief 设置日志级别
 * 
 * @param level 日志级别
 * 
 * @details
 * 只有大于等于此级别的日志才会被记录
 * 例如设置为WARNING，则只记录WARNING、ERROR、FATAL
 * 
 * 【项目示例】
 * @code
 * // 开发阶段：启用DEBUG
 * Logger::getInstance().setLevel(LogLevel::DEBUG);
 * 
 * // 生产环境：只记录INFO及以上
 * Logger::getInstance().setLevel(LogLevel::INFO);
 * @endcode
 */
void Logger::setLevel(LogLevel level) {
    pImpl_->setLevel(level);
}

/**
 * @brief 设置日志文件
 * 
 * @param filePath 日志文件路径（相对或绝对路径）
 * @return true 设置成功，false 失败（文件无法打开）
 * 
 * @details
 * 可以动态切换日志文件，如按日期滚动日志：
 * 
 * 【项目示例】
 * @code
 * // 按日期分割日志
 * Logger::getInstance().setLogFile("logs/2025-10-25.log");
 * 
 * // 按模块分割日志
 * Logger::getInstance().setLogFile("logs/mqtt_client.log");
 * @endcode
 */
bool Logger::setLogFile(const std::string& filePath) {
    return pImpl_->setLogFile(filePath);
}

/**
 * @brief 启用/禁用控制台输出
 * 
 * @param enable true启用，false禁用
 * 
 * @details
 * 开发阶段建议启用，方便查看日志
 * 生产环境可以禁用控制台，只写文件，避免性能损失
 * 
 * 【项目示例】
 * @code
 * // 开发环境：控制台+文件
 * Logger::getInstance().setConsoleOutput(true);
 * 
 * // 生产环境：仅文件（提升性能）
 * Logger::getInstance().setConsoleOutput(false);
 * @endcode
 */
void Logger::setConsoleOutput(bool enable) {
    pImpl_->setConsoleOutput(enable);
}

}  // namespace core
}  // namespace esdk_sophon

// ============================================================================
// 📚 知识点总结 - Logger实现
// ============================================================================
// 
// 【1. Pimpl惯用法】
// ✅ 定义：Pointer to Implementation（指向实现的指针）
// ✅ 优点：
//    - 编译防火墙：修改Impl不需要重新编译依赖Logger.h的文件
//    - 隐藏实现：头文件不暴露<fstream>、<mutex>等细节
//    - 二进制兼容：修改Impl不影响ABI
// ✅ 实现要点：
//    - 在.h中前向声明class Impl
//    - 在.cpp中定义完整的Impl类
//    - 使用std::unique_ptr管理Impl对象
//    - 析构函数必须在.cpp中定义（即使=default）
// 
// 【2. 单例模式（Meyers Singleton）】
// ✅ 定义：确保类只有一个实例，并提供全局访问点
// ✅ C++11实现：
//    - static Logger instance; // 静态局部变量
//    - 编译器保证线程安全（Magic Static）
//    - 懒汉式：第一次调用时才创建
// ✅ 面试高频：
//    Q: 如何实现线程安全的单例？
//    A: C++11用static局部变量，C++11前用双检锁（DCLP）
//    Q: 单例的析构时机？
//    A: 程序结束时自动析构（static对象）
// 
// 【3. RAII原则】
// ✅ 定义：Resource Acquisition Is Initialization
// ✅ 体现：
//    - 构造函数：打开日志文件，初始化资源
//    - 析构函数：关闭文件，释放资源
//    - 异常安全：析构函数总会被调用
// ✅ 项目示例：
//    - std::ofstream logFile_: 构造打开，析构关闭
//    - std::unique_ptr<Impl>: 构造创建，析构删除
//    - std::lock_guard<std::mutex>: 构造加锁，析构解锁
// 
// 【4. 线程安全】
// ✅ 问题：多线程同时写日志可能导致日志混乱或崩溃
// ✅ 解决：使用std::mutex + std::lock_guard
//    - std::mutex mutex_: 互斥锁成员变量
//    - std::lock_guard<std::mutex> lock(mutex_): RAII风格加锁
//    - 保护临界区：文件写入操作
// ✅ 面试要点：
//    Q: std::lock_guard vs std::unique_lock?
//    A: lock_guard轻量不可解锁，unique_lock可手动unlock
// 
// 【5. C++11时间处理】
// ✅ std::chrono::system_clock::now(): 获取当前时间
// ✅ std::chrono::system_clock::to_time_t(): 转为C风格time_t
// ✅ std::put_time(): 格式化输出时间
// ✅ 注意：std::localtime()非线程安全，C++20可用std::chrono::format
// 
// 【6. 智能指针】
// ✅ std::unique_ptr: 独占所有权
//    - 不可拷贝，只能移动
//    - 零开销（和裸指针相同）
//    - std::make_unique: C++14推荐创建方式
// ✅ 项目示例：
//    std::unique_ptr<Impl> pImpl_;  // Pimpl成员
//    pImpl_->log(...);               // 使用->访问
// 
// 【7. enum class】
// ✅ 强类型枚举（C++11）
// ✅ 优点：
//    - 不会隐式转换为int
//    - 有作用域：LogLevel::INFO而非INFO
//    - 避免命名冲突
// ✅ 对比C风格enum：
//    enum Color { RED, GREEN };     // C风格
//    enum class Color { RED, GREEN };  // C++11
// 
// 【8. 项目应用场景】
// ✅ 程序启动：
//    Logger::getInstance().info("ESDK Sophon 启动成功");
// ✅ 配置加载：
//    Logger::getInstance().debug("加载配置: mqtt.broker=localhost");
// ✅ 错误处理：
//    Logger::getInstance().error("MQTT连接失败: " + error);
// ✅ 性能监控：
//    Logger::getInstance().warning("检测耗时: 150ms");
// 
// 【9. 面试必问】
// Q1: 什么是Pimpl，有什么优点？
// A1: 指向实现的指针，优点是编译防火墙、隐藏实现、二进制兼容
// 
// Q2: 如何实现线程安全的单例？
// A2: C++11用静态局部变量，编译器保证线程安全
// 
// Q3: RAII是什么？
// A3: 资源获取即初始化，构造函数获取资源，析构函数释放资源
// 
// Q4: 如何保证日志系统的线程安全？
// A4: 使用std::mutex保护临界区，用std::lock_guard自动管理锁
// 
// Q5: unique_ptr和Pimpl一起用要注意什么？
// A5: 必须在.cpp中定义析构函数，否则编译错误（incomplete type）
// 
// 【10. 延伸学习】
// - 双检锁（DCLP）单例模式（C++11前）
// - std::shared_ptr的线程安全性
// - C++20的std::chrono::format
// - 日志滚动策略（按大小、按日期）
// - 异步日志（性能优化）
// 
// 【11. 日志轮转新增知识点】
// ✅ std::filesystem（C++17）
//    - create_directories: 创建多级目录
//    - exists: 检查文件/目录是否存在
//    - file_size: 获取文件大小
//    - last_write_time: 获取最后修改时间
//    - directory_iterator: 遍历目录
//    - remove: 删除文件
// ✅ 日志轮转策略
//    - 按日期轮转：每天一个日志文件
//    - 按大小轮转：超过100MB自动切换
//    - 自动清理：保留15天，删除旧日志
// ✅ 面试要点
//    Q: 如何管理日志文件，避免磁盘占满？
//    A: 1. 按日期轮转（每天一个文件）
//       2. 文件大小限制（单文件最大100MB）
//       3. 定期清理旧日志（保留N天）
//       4. 日志压缩（可选，节省空间）
// ✅ 项目示例
//    logs/
//      ├── esdk_sophon_2025-10-10.log (15天前，被自动删除)
//      ├── esdk_sophon_2025-10-24.log
//      ├── esdk_sophon_2025-10-25.log (当前)
//      └── esdk_sophon_2025-10-25_001.log (文件过大，自动切换)
// 
// 【12. TODO: 配置文件支持】
// 当前使用硬编码配置，后续改进：
// 1. 从config/logger.json读取配置
// 2. 支持运行时修改配置
// 3. 配置热更新（监听配置文件变化）
// 
// 配置文件格式：
// {
//   "logger": {
//     "level": "INFO",
//     "console_output": true,
//     "log_directory": "logs",
//     "file_name_pattern": "esdk_sophon_{date}.log",
//     "rotation": {
//       "enable": true,
//       "max_days": 15,
//       "max_file_size_mb": 100
//     }
//   }
// }
// ============================================================================
