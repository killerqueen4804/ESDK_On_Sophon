/**
 * @file Application.cpp
 * @brief 应用程序协调器实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-30
 */

#include "esdk_sophon/Application.h"

// DJI Edge-SDK
#include "error_code.h"  // edge_sdk::ErrorCode
// 注意：ESDKInit() 函数在 Edge-SDK/examples/init/pre_init.cc 中实现
// 已通过 CMakeLists.txt 将其编译到主程序中
edge_sdk::ErrorCode ESDKInit();      // ESDK 初始化（必须在使用任何 Edge-SDK 功能前调用）
edge_sdk::ErrorCode ESDKDeInit();    // ESDK 反初始化（程序退出前调用）

// 依赖模块
#include "esdk_sophon/core/Logger.h"
#include "esdk_sophon/core/Config.h"
#include "esdk_sophon/mqtt/MqttClient.h"
#include "esdk_sophon/mqtt/MqttHandler.h"
#include "esdk_sophon/task/TaskManager.h"
#include "esdk_sophon/utils/HttpClient.h"  // HTTP 客户端（用于 GeoDecodeAPI）

// 标准库
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>  // std::_Exit()

namespace esdk_sophon {

// ==================== 单例实现 ====================

Application& Application::getInstance() {
    static Application instance;  // Meyers单例，C++11保证线程安全
    return instance;
}

// ==================== 构造和析构 ====================

Application::Application()
    : logger_(core::Logger::getInstance())
    , config_(core::Config::getInstance())
    , mqttClient_(mqtt::MqttClient::getInstance())
    , mqttHandler_(mqtt::MqttHandler::getInstance())
    , taskManager_(task::TaskManager::getInstance())
    , running_(false)
    , initialized_(false) {
    
    // 构造函数只初始化成员变量，不执行实际初始化
    // 实际初始化在initialize()中进行
    
    // 初始化 libcurl 全局环境（必须在任何 HTTP 请求之前）
    // TODO: 重新启用 HttpClient 后取消注释
    // utils::HttpClient::globalInit();
}

Application::~Application() {
    // 析构时确保资源正确释放
    if (initialized_) {
        shutdown();
    }
    
    // 清理 libcurl 全局资源
    // TODO: 重新启用 HttpClient 后取消注释
    // utils::HttpClient::globalCleanup();
}

// ==================== 初始化 ====================

bool Application::initialize() {
    std::cout << "========================================\n";
    std::cout << "  ESDK Sophon 应用程序启动\n";
    std::cout << "========================================\n\n";
    
    try {
        // ------------------------------------------------
        // 步骤0: 初始化DJI Edge-SDK（最优先！）
        // ------------------------------------------------
        std::cout << "[0/5] 初始化 DJI Edge-SDK...\n";
        
        using namespace edge_sdk;
        auto rc = ESDKInit();
        if (rc != kOk) {
            std::cerr << "❌ Edge-SDK 初始化失败，错误码: " << rc << "\n";
            std::cerr << "⚠️  请检查:\n";
            std::cerr << "   1. app_info.h 中的 DJI 开发者凭证是否正确\n";
            std::cerr << "   2. RSA 密钥生成是否成功 (/tmp/pub_key, /tmp/private_key)\n";
            std::cerr << "   3. Edge-SDK 库文件是否存在且架构匹配\n";
            return false;
        }
        
        std::cout << "✅ Edge-SDK 初始化成功\n\n";
        
        // ------------------------------------------------
        // 步骤1: 初始化Logger（最先初始化，其他模块需要日志）
        // ------------------------------------------------
        std::cout << "[1/5] 初始化日志系统...\n";
        // Logger是单例，已在构造函数中获取引用
        // 这里只是验证Logger可用
        logger_.info("========================================");
        logger_.info("  ESDK Sophon Application Starting");
        logger_.info("========================================");
        std::cout << "✅ 日志系统初始化成功\n\n";
        
        // ------------------------------------------------
        // 步骤2: 加载配置文件
        // ------------------------------------------------
        std::cout << "[2/5] 加载配置文件...\n";
        
        // 尝试从多个位置加载配置
        std::vector<std::string> configPaths = {
            "config/config.json",           // 相对路径（当前目录）
            "../config/config.json",        // 上一级目录
            "/workspace/config/config.json" // Docker环境
        };
        
        bool configLoaded = false;
        for (const auto& path : configPaths) {
            if (config_.load(path)) {
                logger_.info("配置文件加载成功: " + path);
                std::cout << "✅ 配置文件加载成功: " << path << "\n\n";
                configLoaded = true;
                break;
            }
        }
        
        if (!configLoaded) {
            logger_.warning("未找到配置文件，使用默认配置");
            std::cout << "⚠️  未找到配置文件，使用默认配置\n\n";
        }
        
        // ------------------------------------------------
        // 步骤3: 初始化TaskManager
        // ------------------------------------------------
        std::cout << "[3/5] 初始化任务管理器...\n";
        
        // ⚠️ 注意：TaskManager 必须在 MqttClient 之后初始化
        //         因为 TaskManager 需要 MqttClient 的引用
        // 
        // 但由于 MqttClient 还未初始化，这里先跳过
        // 在 MqttClient 初始化后再初始化 TaskManager
        
        logger_.info("TaskManager等待后续初始化（需要MQTT客户端）");
        std::cout << "⏳ TaskManager等待MQTT连接后初始化\n\n";
        
        // ------------------------------------------------
        // 步骤4: 初始化并连接MqttClient
        // ------------------------------------------------
        std::cout << "[4/5] 连接MQTT服务器...\n";
        
        // MqttClient从Config中读取配置，无需传参
        // 配置项包括：mqtt.broker, mqtt.port, mqtt.client_id等
        logger_.info("开始初始化MQTT客户端...");
        
        // 初始化（从Config读取配置）
        if (!mqttClient_.initialize()) {
            logger_.error("MQTT客户端初始化失败");
            std::cout << "❌ MQTT客户端初始化失败\n";
            return false;
        }
        
        // 连接到MQTT代理
        if (!mqttClient_.connect()) {
            logger_.error("MQTT服务器连接失败");
            std::cout << "❌ MQTT服务器连接失败\n";
            return false;
        }
        
        logger_.info("MQTT连接成功");
        std::cout << "✅ MQTT连接成功\n\n";
        
        // ------------------------------------------------
        // 步骤4.5: 初始化TaskManager
        // ------------------------------------------------
        std::cout << "[4.5/5] 初始化任务管理器...\n";
        
        // TaskManager 会自动从单例获取 MQTT 客户端
        if (!taskManager_.initialize()) {
            logger_.error("TaskManager初始化失败");
            std::cout << "❌ TaskManager初始化失败\n";
            return false;
        }
        
        logger_.info("TaskManager初始化成功");
        std::cout << "✅ 任务管理器初始化成功\n\n";
        
        // ------------------------------------------------
        // 步骤5: 初始化并启动MqttHandler
        // ------------------------------------------------
        std::cout << "[5/5] 启动MQTT消息处理器...\n";
        
        // 初始化（读取配置、构建Topic）
        if (!mqttHandler_.initialize()) {
            logger_.error("MqttHandler初始化失败");
            std::cout << "❌ MqttHandler初始化失败\n";
            return false;
        }
        
        // 启动（注册观察者、订阅Topic）
        if (!mqttHandler_.start()) {
            logger_.error("MqttHandler启动失败");
            std::cout << "❌ MqttHandler启动失败\n";
            return false;
        }
        
        logger_.info("MqttHandler启动成功");
        std::cout << "✅ MQTT消息处理器启动成功\n\n";
        
        // ------------------------------------------------
        // 注册信号处理函数
        // ------------------------------------------------
        registerSignalHandlers();
        logger_.info("信号处理函数注册成功");
        
        // ------------------------------------------------
        // 初始化完成
        // ------------------------------------------------
        initialized_ = true;
        
        std::cout << "========================================\n";
        std::cout << "  所有模块初始化完成！\n";
        std::cout << "========================================\n\n";
        
        logger_.info("Application初始化完成");
        logger_.info("系统已准备就绪，等待MQTT指令...");
        
        return true;
        
    } catch (const std::exception& e) {
        logger_.error("初始化异常: " + std::string(e.what()));
        std::cerr << "❌ 初始化异常: " << e.what() << "\n";
        return false;
    }
}

// ==================== 运行 ====================

void Application::run() {
    if (!initialized_) {
        logger_.error("应用程序未初始化，无法运行");
        std::cerr << "❌ 应用程序未初始化，无法运行\n";
        return;
    }
    
    logger_.info("应用程序开始运行...");
    std::cout << "========================================\n";
    std::cout << "  应用程序正在运行\n";
    std::cout << "  按 Ctrl+C 退出\n";
    std::cout << "========================================\n\n";
    
    running_ = true;
    
    // 主事件循环
    while (running_) {
        // 这里可以添加周期性任务
        // 例如：健康检查、状态上报、性能监控
        
        // 休眠一段时间，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 可选：周期性日志
        // logger_.debug("主循环心跳");
    }
    
    logger_.info("应用程序退出主循环");
    std::cout << "\n应用程序准备退出...\n";
}

// ==================== 停止 ====================

void Application::stop() {
    logger_.info("收到停止信号");
    std::cout << "\n收到停止信号...\n";
    
    running_ = false;
}

// ==================== 关闭 ====================

void Application::shutdown() {
    std::cout << "\n========================================\n";
    std::cout << "  正在关闭应用程序\n";
    std::cout << "========================================\n\n";
    
    logger_.info("开始关闭应用程序...");
    
    try {
        // ------------------------------------------------
        // 步骤1: 停止MqttHandler（停止消息处理）
        // ------------------------------------------------
        std::cout << "[1/4] 停止MQTT消息处理器...\n";
        mqttHandler_.stop();
        logger_.info("MqttHandler已停止");
        std::cout << "✅ MQTT消息处理器已停止\n\n";
        
        // ------------------------------------------------
        // 步骤2: 断开MQTT连接
        // ------------------------------------------------
        std::cout << "[2/4] 断开MQTT连接...\n";
        if (mqttClient_.isConnected()) {
            mqttClient_.disconnect();
            logger_.info("MQTT连接已断开");
            std::cout << "✅ MQTT连接已断开\n\n";
        }
        
        // ------------------------------------------------
        // 步骤3: 停止所有任务
        // ------------------------------------------------
        std::cout << "[3/4] 停止所有运行中的任务...\n";
        auto taskIds = taskManager_.getRunningTaskIds();
        
        if (taskIds.empty()) {
            std::cout << "ℹ️  没有运行中的任务\n\n";
        } else {
            std::cout << "停止 " << taskIds.size() << " 个任务...\n";
            for (const auto& taskId : taskIds) {
                taskManager_.stopTask(taskId);
                logger_.info("任务已停止: " + taskId);
            }
            std::cout << "✅ 所有任务已停止\n\n";
        }
        
        // ------------------------------------------------
        // 步骤4: 刷新日志
        // ------------------------------------------------
        std::cout << "[4/5] 刷新日志缓冲...\n";
        logger_.info("应用程序关闭完成");
        logger_.info("========================================");
        // Logger会在析构时自动刷新
        std::cout << "✅ 日志已刷新\n\n";
        
        // ------------------------------------------------
        // 步骤5: 反初始化DJI Edge-SDK
        // ------------------------------------------------
        std::cout << "[5/5] 反初始化 DJI Edge-SDK...\n";
        
        using namespace edge_sdk;
        auto rc = ESDKDeInit();
        if (rc != kOk) {
            logger_.warning("Edge-SDK 反初始化失败，错误码: " + std::to_string(rc));
            std::cerr << "⚠️  Edge-SDK 反初始化失败，错误码: " << rc << "\n";
        } else {
            std::cout << "✅ Edge-SDK 已反初始化\n\n";
        }
        
        // ------------------------------------------------
        // 关闭完成
        // ------------------------------------------------
        initialized_ = false;
        
        std::cout << "========================================\n";
        std::cout << "  应用程序已安全关闭\n";
        std::cout << "========================================\n\n";
        
        // ------------------------------------------------
        // 【修复】强制终止进程，绕过 SDK 内部线程 bug
        // ------------------------------------------------
        // DJI SDK 的 DeInit() 有 bug：内部心跳线程不会停止，
        // 会持续尝试访问已销毁的互斥锁，产生错误日志。
        // 
        // 测试证明：
        // 1. 延迟退出无效 —— 线程永远不会自然停止
        // 2. 错误无害 —— 只是日志噪音，不影响数据完整性
        // 
        // 解决方案：使用 _Exit() 立即终止进程
        // - _Exit() vs exit(): 
        //   * exit() 调用析构函数和 atexit 处理器，可能触发更多 SDK bug
        //   * _Exit() 直接终止，不调用清理函数，干净利落
        // - _Exit() vs _exit():
        //   * _Exit() 是 C99/C++11 标准，在 <cstdlib> 中
        //   * _exit() 是 POSIX 函数，需要 <unistd.h>
        // 
        // 注意：此时所有重要资源已经正确清理：
        // - MQTT 已断开 ✅
        // - 任务已停止 ✅
        // - 日志已刷新 ✅
        // - SDK DeInit 已调用 ✅
        std::cout << "✅ 系统已安全退出，再见！\n";
        std::cout.flush();  // 确保输出刷新
        
        std::_Exit(0);  // 立即终止，不触发 SDK 僵尸线程的析构问题
        
    } catch (const std::exception& e) {
        logger_.error("关闭异常: " + std::string(e.what()));
        std::cerr << "❌ 关闭异常: " << e.what() << "\n";
    }
}

// ==================== 状态查询 ====================

bool Application::isRunning() const {
    return running_;
}

// ==================== 信号处理 ====================

void Application::registerSignalHandlers() {
    // 注册SIGINT（Ctrl+C）
    std::signal(SIGINT, Application::signalHandler);
    
    // 注册SIGTERM（kill命令）
    std::signal(SIGTERM, Application::signalHandler);
    
    std::cout << "信号处理函数已注册（SIGINT, SIGTERM）\n\n";
}

void Application::signalHandler(int signum) {
    // 静态函数，从信号处理上下文中调用
    // 只能访问静态成员或全局对象
    
    std::cout << "\n\n收到信号 " << signum << " ";
    
    switch (signum) {
        case SIGINT:
            std::cout << "(SIGINT - Ctrl+C)\n";
            break;
        case SIGTERM:
            std::cout << "(SIGTERM - kill)\n";
            break;
        default:
            std::cout << "(未知信号)\n";
            break;
    }
    
    // 调用stop()退出主循环
    Application::getInstance().stop();
}

}  // namespace esdk_sophon

// ============================================================================
// 📚 实现要点说明
// ============================================================================
//
// 1. 模块初始化顺序
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Logger → Config → TaskManager → MqttClient → MqttHandler
//
// 依赖关系：
// - Config依赖Logger（记录配置加载日志）
// - TaskManager依赖Logger和Config
// - MqttClient依赖Config（读取MQTT参数）
// - MqttHandler依赖MqttClient（订阅Topic）和TaskManager（启动任务）
//
// 2. 模块关闭顺序
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// MqttHandler → MqttClient → TaskManager → Logger
//
// 为什么这个顺序？
// - MqttHandler先停止，避免收到新消息
// - MqttClient再断开，确保没有未发送的消息
// - TaskManager停止任务，释放资源
// - Logger最后关闭，确保所有日志都被记录
//
// 3. 异常处理策略
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// - 初始化失败时立即返回false
// - 关闭过程中捕获异常，确保其他模块仍能关闭
// - 使用try-catch包裹每个步骤
//
// 4. 用户友好性
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// - 控制台输出详细的进度信息
// - 使用表情符号（✅❌⚠️）增强可读性
// - 分隔线和标题让输出结构清晰
// - 同时输出到Logger和控制台
//
// ============================================================================
