/**
 * @file Logger.h
 * @brief 日志系统 - 单例模式实现
 * @author ESDK_Sophon Team
 * @date 2025-10-23
 * 
 * @details
 * 提供线程安全的日志记录功能，支持多级别日志输出。
 * 采用单例模式确保全局唯一的日志实例。
 * 
 * 使用示例:
 * @code
 * #include "esdk_sophon/core/Logger.h"
 * 
 * auto& logger = Logger::getInstance();
 * logger.info("程序启动");
 * logger.error("发生错误: ", errorMsg);
 * @endcode
 */

#ifndef ESDK_SOPHON_CORE_LOGGER_H_
#define ESDK_SOPHON_CORE_LOGGER_H_

#include <string>
#include <memory>
#include <mutex>

namespace esdk_sophon {
namespace core {

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    DEBUG,      ///< 调试信息
    INFO,       ///< 一般信息
    WARNING,    ///< 警告信息
    ERROR,      ///< 错误信息
    FATAL       ///< 致命错误
};

/**
 * @brief 日志系统类（单例模式）
 * 
 * @details
 * 线程安全的日志记录器，支持多级别日志输出到文件和控制台。
 * 
 * 设计模式：
 * - 单例模式：确保全局唯一的日志实例
 * - Pimpl惯用法：隐藏实现细节，减少编译依赖
 * 
 * 线程安全：
 * - C++11静态局部变量保证单例创建的线程安全
 * - 内部使用互斥锁保护日志写入操作
 * 
 * @note 面试考点
 * 1. 单例模式的线程安全实现（Meyers Singleton）
 * 2. RAII原则（构造函数打开文件，析构函数关闭文件）
 * 3. Pimpl惯用法（减少编译依赖）
 */
class Logger {
public:
    /**
     * @brief 获取Logger单例
     * 
     * @return Logger& 日志器引用
     * 
     * @details
     * 使用Meyers Singleton实现，C++11保证线程安全。
     * 
     * 原理：
     * - 静态局部变量在第一次调用时初始化
     * - C++11标准保证初始化是线程安全的（编译器会加锁）
     * - 程序退出时自动调用析构函数
     * 
     * @note 面试问题：
     * Q: 为什么C++11的单例是线程安全的？
     * A: C++11标准规定，静态局部变量的初始化是线程安全的。
     *    编译器会在第一次调用时加锁，确保只初始化一次。
     */
    static Logger& getInstance();

    /**
     * @brief 删除拷贝构造函数
     * 
     * @details
     * 单例模式不允许拷贝，保证全局唯一性。
     * 使用= delete明确禁止拷贝（C++11特性）。
     */
    Logger(const Logger&) = delete;

    /**
     * @brief 删除赋值运算符
     * 
     * @details
     * 单例模式不允许赋值，保证全局唯一性。
     */
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief 析构函数
     * 
     * @details
     * 自动关闭日志文件，遵循RAII原则。
     */
    ~Logger();

    // ========================================================================
    // 日志记录接口
    // ========================================================================

    /**
     * @brief 记录调试信息
     * 
     * @param message 日志消息
     * 
     * @details
     * 只在Debug模式下输出，Release模式下会被优化掉。
     */
    void debug(const std::string& message);

    /**
     * @brief 记录一般信息
     * 
     * @param message 日志消息
     */
    void info(const std::string& message);

    /**
     * @brief 记录警告信息
     * 
     * @param message 日志消息
     */
    void warning(const std::string& message);

    /**
     * @brief 记录错误信息
     * 
     * @param message 日志消息
     */
    void error(const std::string& message);

    /**
     * @brief 记录致命错误
     * 
     * @param message 日志消息
     * 
     * @details
     * 记录后可能触发程序终止。
     */
    void fatal(const std::string& message);

    // ========================================================================
    // 配置接口
    // ========================================================================

    /**
     * @brief 设置日志级别
     * 
     * @param level 日志级别
     * 
     * @details
     * 只有大于等于设置级别的日志才会被记录。
     * 例如：设置为INFO，则DEBUG不会被记录。
     */
    void setLevel(LogLevel level);

    /**
     * @brief 设置日志文件路径
     * 
     * @param filePath 文件路径
     * @return true 设置成功
     * @return false 设置失败（文件无法打开）
     */
    bool setLogFile(const std::string& filePath);

    /**
     * @brief 启用/禁用控制台输出
     * 
     * @param enable true启用，false禁用
     */
    void setConsoleOutput(bool enable);

private:
    /**
     * @brief 私有构造函数
     * 
     * @details
     * 单例模式要求构造函数私有，防止外部创建实例。
     * 在构造函数中初始化日志系统。
     */
    Logger();

    /**
     * @brief 通用日志记录方法
     * 
     * @param level 日志级别
     * @param message 日志消息
     * 
     * @details
     * 内部方法，被public方法调用。
     * 负责格式化日志、加锁、写入文件等。
     */
    void log(LogLevel level, const std::string& message);

    /**
     * @brief Pimpl惯用法：实现类前向声明
     * 
     * @details
     * 隐藏实现细节，减少头文件依赖。
     * 具体实现在Logger.cpp中定义。
     * 
     * 优势：
     * 1. 头文件不需要包含<fstream>、<sstream>等
     * 2. 修改实现不需要重新编译依赖Logger.h的所有文件
     * 3. 加快编译速度
     * 
     * @note 面试问题：
     * Q: 什么是Pimpl惯用法？
     * A: Pointer to Implementation，指向实现的指针。
     *    将实现细节放在单独的类中，头文件只包含指向该类的指针。
     *    目的是减少编译依赖，加快编译速度。
     */
    class Impl;
    std::unique_ptr<Impl> pImpl_;  ///< 指向实现的智能指针
};

}  // namespace core
}  // namespace esdk_sophon

// ============================================================================
// 便捷日志宏定义
// ============================================================================

/**
 * @brief 日志宏 - 简化日志调用
 * @details 提供类似printf的格式化输出
 * 
 * 使用示例:
 * @code
 * INFO("服务器启动成功，监听端口: %d", port);
 * ERROR("文件打开失败: %s", filename.c_str());
 * @endcode
 * 
 * @note 这些宏会自动添加文件名、行号、函数名等上下文信息
 */

#define DEBUG(fmt, ...) \
    esdk_sophon::core::Logger::getInstance().debug( \
        std::string(fmt) + " [" + __FILE__ + ":" + std::to_string(__LINE__) + "]")

#define INFO(fmt, ...) \
    do { \
        char buf[1024]; \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        esdk_sophon::core::Logger::getInstance().info(buf); \
    } while(0)

#define WARN(fmt, ...) \
    do { \
        char buf[1024]; \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        esdk_sophon::core::Logger::getInstance().warning(buf); \
    } while(0)

#define ERROR(fmt, ...) \
    do { \
        char buf[1024]; \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        esdk_sophon::core::Logger::getInstance().error(buf); \
    } while(0)

#define FATAL(fmt, ...) \
    do { \
        char buf[1024]; \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        esdk_sophon::core::Logger::getInstance().fatal(buf); \
    } while(0)

#endif  // ESDK_SOPHON_CORE_LOGGER_H_

// ============================================================================
// 📚 本文件涉及的知识点总结（八股文）
// ============================================================================
//
// 1. 单例模式（Singleton Pattern）
//    - 定义：确保一个类只有一个实例，并提供全局访问点
//    - 实现：Meyers Singleton（C++11静态局部变量）
//    - 线程安全：C++11标准保证
//    - 优点：节省资源、全局访问
//    - 缺点：难以测试、全局状态
//
// 2. RAII原则（Resource Acquisition Is Initialization）
//    - 定义：资源获取即初始化
//    - 应用：构造函数打开文件，析构函数关闭文件
//    - 优势：自动管理资源，防止泄漏
//    - 例子：智能指针、文件流、锁
//
// 3. Pimpl惯用法（Pointer to Implementation）
//    - 定义：使用指针指向实现类，隐藏实现细节
//    - 优势：减少编译依赖、加快编译速度、ABI稳定
//    - 实现：前向声明Impl类，使用unique_ptr管理
//
// 4. C++11特性
//    - static局部变量的线程安全初始化
//    - = delete 删除函数（禁止拷贝）
//    - std::unique_ptr 智能指针
//    - enum class 强类型枚举
//
// 5. 命名空间
//    - 嵌套命名空间：esdk_sophon::core
//    - 避免命名冲突
//    - 组织代码结构
//
// 6. 头文件保护
//    - #ifndef #define #endif 宏保护
//    - 命名规范：项目名_模块名_文件名_H_
//    - 防止重复包含
//
// 7. Doxygen注释
//    - @brief 简要说明
//    - @param 参数说明
//    - @return 返回值说明
//    - @details 详细说明
//    - @note 注意事项
//    - @code @endcode 代码示例
//
// ============================================================================
