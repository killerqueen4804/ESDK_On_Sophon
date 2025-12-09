/**
 * @file Application.h
 * @brief 应用程序协调器 - 顶层控制类
 * 
 * @details
 * Application负责:
 * 1. 初始化所有模块（Logger、Config、MqttClient、MqttHandler、TaskManager）
 * 2. 管理模块间的启动顺序和依赖关系
 * 3. 处理系统信号（SIGINT、SIGTERM）实现优雅退出
 * 4. 主事件循环（保持程序运行）
 * 5. 资源清理和模块关闭
 * 
 * 设计模式:
 * - 单例模式: 全局唯一的应用程序实例
 * - 外观模式: 统一的系统初始化和控制接口
 * - RAII原则: 确保资源正确释放
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-30
 */

#ifndef ESDK_SOPHON_APPLICATION_H_
#define ESDK_SOPHON_APPLICATION_H_

#include <atomic>
#include <csignal>

namespace esdk_sophon {

// 前向声明
namespace core {
    class Logger;
    class Config;
}

namespace mqtt {
    class MqttClient;
    class MqttHandler;
}

namespace task {
    class TaskManager;
}

/**
 * @brief 应用程序协调器
 * 
 * @details
 * Application是整个系统的入口点和协调中心，负责：
 * - 按正确顺序初始化所有模块
 * - 管理模块间的依赖关系
 * - 处理系统信号（Ctrl+C）
 * - 优雅退出和资源清理
 * 
 * 使用方法:
 * @code
 * int main() {
 *     auto& app = Application::getInstance();
 *     
 *     if (!app.initialize()) {
 *         return 1;
 *     }
 *     
 *     app.run();  // 阻塞直到收到退出信号
 *     
 *     app.shutdown();
 *     return 0;
 * }
 * @endcode
 */
class Application {
public:
    /**
     * @brief 获取Application单例
     * 
     * @return Application的全局唯一实例
     * 
     * @details
     * 使用Meyers' Singleton实现，C++11保证线程安全
     */
    static Application& getInstance();
    
    // 禁止拷贝和赋值
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    
    /**
     * @brief 初始化应用程序
     * 
     * @return true 初始化成功，false 初始化失败
     * 
     * @details
     * 按顺序初始化:
     * 1. Logger（日志系统）
     * 2. Config（配置系统）
     * 3. TaskManager（任务管理）
     * 4. MqttClient（MQTT客户端）
     * 5. MqttHandler（MQTT消息处理）
     * 
     * 初始化失败时，会自动清理已初始化的模块
     */
    bool initialize();
    
    /**
     * @brief 运行应用程序
     * 
     * @details
     * 进入主事件循环，阻塞直到：
     * - 收到SIGINT信号（Ctrl+C）
     * - 收到SIGTERM信号（kill命令）
     * - 调用stop()方法
     * 
     * 线程安全：可以从其他线程调用stop()来停止运行
     */
    void run();
    
    /**
     * @brief 停止应用程序
     * 
     * @details
     * 设置停止标志，使run()方法退出事件循环
     * 线程安全，可以从信号处理函数中调用
     */
    void stop();
    
    /**
     * @brief 关闭应用程序
     * 
     * @details
     * 按正确顺序关闭所有模块：
     * 1. MqttHandler（停止消息处理）
     * 2. MqttClient（断开MQTT连接）
     * 3. TaskManager（停止所有任务）
     * 4. Logger（刷新日志缓冲）
     * 
     * 关闭顺序是初始化顺序的逆序（RAII原则）
     */
    void shutdown();
    
    /**
     * @brief 检查应用程序是否正在运行
     * 
     * @return true 正在运行，false 已停止
     */
    bool isRunning() const;
    
private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    Application();
    
    /**
     * @brief 析构函数
     * 
     * @details
     * 确保资源正确释放，调用shutdown()
     */
    ~Application();
    
    /**
     * @brief 注册信号处理函数
     * 
     * @details
     * 注册SIGINT和SIGTERM的处理函数
     * 收到信号时调用stop()优雅退出
     */
    void registerSignalHandlers();
    
    /**
     * @brief 信号处理函数
     * 
     * @param signum 信号编号
     * 
     * @details
     * 静态函数，可以注册为C风格的信号处理函数
     * 内部调用Application::getInstance().stop()
     */
    static void signalHandler(int signum);
    
private:
    // 模块引用（单例模式，通过getInstance()获取）
    core::Logger& logger_;
    core::Config& config_;
    mqtt::MqttClient& mqttClient_;
    mqtt::MqttHandler& mqttHandler_;
    task::TaskManager& taskManager_;
    
    // 运行状态标志（原子操作，线程安全）
    std::atomic<bool> running_;
    
    // 初始化状态标志
    bool initialized_;
};

}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_APPLICATION_H_

// ============================================================================
// 📚 知识点：Application设计模式
// ============================================================================
//
// 1. 单例模式（Singleton Pattern）
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Application作为应用程序的唯一入口点，必须是全局唯一的
//
// 为什么用单例？
// - 避免多个Application实例导致资源冲突
// - 方便从信号处理函数中访问（静态上下文）
// - 确保初始化和关闭顺序的一致性
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
// 2. 外观模式（Facade Pattern）
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Application为复杂的子系统提供统一的高层接口
//
// 子系统：
// - Logger: 日志记录
// - Config: 配置管理
// - MqttClient: MQTT通信
// - MqttHandler: 消息处理
// - TaskManager: 任务调度
//
// 外观接口：
// - initialize(): 一键初始化所有子系统
// - run(): 统一的运行入口
// - shutdown(): 一键关闭所有子系统
//
// 优点：
// - 简化使用：用户不需要了解内部模块依赖关系
// - 解耦：子系统变化不影响用户代码
// - 控制：Application控制模块的初始化顺序
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
// 3. RAII原则（Resource Acquisition Is Initialization）
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 资源的获取即初始化，资源的释放即析构
//
// Application中的体现：
// - 构造函数：不获取资源（单例模式延迟初始化）
// - initialize(): 获取资源（初始化模块）
// - shutdown(): 释放资源（关闭模块）
// - 析构函数：确保调用shutdown()
//
// 关闭顺序 = 初始化顺序的逆序：
// 初始化: Logger → Config → TaskManager → MqttClient → MqttHandler
// 关闭:   MqttHandler → MqttClient → TaskManager → Logger
//
// 为什么这样？
// - MqttHandler依赖MqttClient，必须先关闭
// - TaskManager可能还在处理任务，要先停止
// - Logger最后关闭，确保所有日志都被记录
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
// 4. 信号处理（Signal Handling）
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Unix/Linux系统的进程间通信机制
//
// 常见信号：
// - SIGINT (2): Ctrl+C，用户中断
// - SIGTERM (15): kill命令，请求终止
// - SIGKILL (9): 强制终止（无法捕获）
//
// 优雅退出流程：
// 1. 收到SIGINT/SIGTERM信号
// 2. 调用signalHandler()
// 3. 设置running_ = false
// 4. run()退出事件循环
// 5. main()调用shutdown()
// 6. 程序正常退出
//
// 为什么需要优雅退出？
// - 保存未完成的工作
// - 关闭网络连接
// - 刷新日志缓冲
// - 释放系统资源
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
// 5. 线程安全（Thread Safety）
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// std::atomic<bool> 保证running_的线程安全访问
//
// 为什么需要atomic？
// - running_可能被多个线程同时访问
// - 主线程：在run()中读取
// - 信号处理线程：在stop()中写入
// - atomic保证操作的原子性，避免数据竞争
//
// 替代方案对比：
// - bool + mutex: 可行，但std::atomic更高效
// - volatile: 不够，无法保证原子性
// - std::atomic: 最佳选择，硬件级别的原子操作
//
// ============================================================================

// ============================================================================
// 📚 面试要点
// ============================================================================
//
// Q1: 为什么Application要用单例模式？
// A: 1. 应用程序的入口点应该是全局唯一的
//    2. 需要从信号处理函数（静态上下文）访问
//    3. 确保模块初始化和关闭顺序的一致性
//    4. 避免多实例导致的资源冲突
//
// Q2: 模块的初始化顺序为什么重要？
// A: 1. 依赖关系：后初始化的模块可能依赖先初始化的模块
//    2. 配置依赖：Logger需要Config提供日志级别
//    3. 网络依赖：MqttHandler依赖MqttClient已连接
//    4. 逻辑依赖：TaskManager需要Logger记录日志
//
// Q3: 如何实现优雅退出？
// A: 1. 注册信号处理函数（SIGINT、SIGTERM）
//    2. 信号到达时设置停止标志
//    3. 主循环检测标志后退出
//    4. 按逆序关闭所有模块
//    5. 释放资源、刷新日志
//
// Q4: std::atomic<bool>和bool + mutex有什么区别？
// A: 1. atomic: 硬件级别的原子操作，无锁，性能高
//    2. mutex: 操作系统级别的锁，有上下文切换开销
//    3. 对于简单的bool标志，atomic更合适
//    4. 对于复杂的数据结构，需要使用mutex
//
// Q5: 外观模式和适配器模式有什么区别？
// A: 1. 外观模式：为复杂子系统提供统一高层接口
//    2. 适配器模式：让不兼容的接口能够一起工作
//    3. 外观模式：简化接口（多对一）
//    4. 适配器模式：转换接口（一对一）
//
// ============================================================================
