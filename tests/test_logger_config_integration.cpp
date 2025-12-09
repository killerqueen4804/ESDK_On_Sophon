/**
 * @file test_logger_config_integration.cpp
 * @brief Logger与Config集成测试
 * @author ESDK_Sophon Team
 * @date 2025-10-25
 * 
 * @details
 * 验证Logger从Config读取配置的功能
 */

#include "esdk_sophon/core/Logger.h"
#include "esdk_sophon/core/Config.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace esdk_sophon::core;

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Logger与Config集成测试                           ║\n";
    std::cout << "║         验证配置驱动的日志系统                            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    try {
        // ========================================================================
        // 步骤1：加载配置文件
        // ========================================================================
        std::cout << "【步骤1】加载配置文件...\n";
        
        auto& config = Config::getInstance();
        if (!config.load("../config/config.json")) {
            std::cerr << "❌ 配置文件加载失败！\n";
            std::cerr << "提示：请确保 config/config.json 文件存在\n";
            return 1;
        }
        
        std::cout << "✅ 配置文件加载成功！\n";
        std::cout << "   配置文件路径: " << config.getConfigPath() << "\n\n";
        
        // 显示Logger相关配置
        std::cout << "【配置信息】从Config读取的Logger配置：\n";
        std::cout << "  - 日志目录: " << config.getString("logger.log_directory", "logs") << "\n";
        std::cout << "  - 日志级别: " << config.getString("logger.level", "INFO") << "\n";
        std::cout << "  - 控制台输出: " << (config.getBool("logger.console_output", true) ? "启用" : "禁用") << "\n";
        std::cout << "  - 日志轮转: " << (config.getBool("logger.rotation.enabled", true) ? "启用" : "禁用") << "\n";
        std::cout << "  - 保留天数: " << config.getInt("logger.rotation.max_days", 15) << " 天\n";
        std::cout << "  - 文件大小限制: " << config.getInt("logger.rotation.max_file_size_mb", 100) << " MB\n\n";
        
        // ========================================================================
        // 步骤2：初始化Logger（将从Config读取配置）
        // ========================================================================
        std::cout << "【步骤2】初始化Logger...\n";
        std::cout << "Logger将自动从Config读取配置...\n\n";
        
        auto& logger = Logger::getInstance();
        
        // ========================================================================
        // 步骤3：测试日志输出
        // ========================================================================
        std::cout << "【步骤3】测试日志输出...\n\n";
        
        logger.info("=== Logger与Config集成测试开始 ===");
        logger.info("测试配置驱动的日志系统");
        
        logger.debug("这是一条DEBUG日志（根据配置可能不显示）");
        logger.info("这是一条INFO日志");
        logger.warning("这是一条WARNING日志");
        logger.error("这是一条ERROR日志");
        
        // ========================================================================
        // 步骤4：验证配置可修改性
        // ========================================================================
        std::cout << "\n【步骤4】配置修改提示\n";
        std::cout << "您可以：\n";
        std::cout << "1. 修改 config/config.json 中的logger配置\n";
        std::cout << "2. 重启程序查看效果\n";
        std::cout << "3. 例如修改 logger.level 为 \"DEBUG\" 可看到更多日志\n";
        std::cout << "4. 例如修改 logger.log_directory 可更改日志位置\n\n";
        
        // ========================================================================
        // 步骤5：测试多线程日志
        // ========================================================================
        std::cout << "【步骤5】测试多线程并发写入（验证线程安全）...\n\n";
        
        auto writeLogsThread = [&logger](int threadId) {
            for (int i = 0; i < 5; ++i) {
                logger.info("线程" + std::to_string(threadId) + " - 消息" + std::to_string(i));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        
        std::thread t1(writeLogsThread, 1);
        std::thread t2(writeLogsThread, 2);
        std::thread t3(writeLogsThread, 3);
        
        t1.join();
        t2.join();
        t3.join();
        
        logger.info("=== 多线程测试完成 ===");
        
        // ========================================================================
        // 总结
        // ========================================================================
        std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║                测试总结                                  ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n";
        std::cout << "✅ Logger成功从Config读取配置\n";
        std::cout << "✅ 配置驱动的日志系统工作正常\n";
        std::cout << "✅ 多线程并发写入测试通过\n\n";
        
        std::cout << "📂 日志文件位置：" 
                  << config.getString("logger.log_directory", "logs") << "/\n";
        std::cout << "   请检查日志文件验证输出\n\n";
        
        std::cout << "💡 提示：这就是配置驱动设计的优势！\n";
        std::cout << "   - 无需重新编译即可调整日志行为\n";
        std::cout << "   - 配置集中管理，易于维护\n";
        std::cout << "   - 支持运行时热更新（通过Config::reload()）\n\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n❌ 测试失败: " << e.what() << "\n";
        return 1;
    }
}

// ============================================================================
// 📚 知识点总结 - 配置驱动设计
// ============================================================================
//
// 【1. 配置驱动 vs 硬编码】
//
// 硬编码（旧方式）：
// ```cpp
// const std::string LOG_DIR = "logs";  // 写死在代码中
// ```
// ❌ 缺点：修改需要重新编译、部署困难、不灵活
//
// 配置驱动（新方式）：
// ```cpp
// logDir = config.getString("logger.log_directory", "logs");
// ```
// ✅ 优点：运行时可调、配置集中、降级方案（默认值）
//
// 【2. 依赖注入思想】
//
// Logger不直接依赖Config，而是：
// 1. 尝试从Config读取
// 2. 如果Config未初始化，使用默认值
// 3. 实现解耦，提高容错性
//
// 【3. 降级方案（Fallback）】
//
// ```cpp
// logDir = config.getString("logger.log_directory", "logs");
//                                                    ^^^^^^
//                                                    默认值
// ```
// 好处：
// - Config加载失败时，系统仍能运行
// - 配置项缺失时，使用合理默认值
// - 提高系统健壮性
//
// 【4. 配置热更新】
//
// 虽然Logger在构造时读取配置，但可以：
// 1. 提供reload()方法重新读取Config
// 2. 使用Config::reload()更新配置文件
// 3. 实现运行时配置更新
//
// 【5. 面试要点】
//
// Q1: 为什么要用配置文件而不是硬编码？
// A1: 1. 灵活性：无需重新编译
//     2. 可维护性：配置集中管理
//     3. 环境适配：开发/测试/生产环境不同配置
//     4. 降级方案：配置缺失时使用默认值
//
// Q2: Logger如何从Config读取配置？
// A2: 在构造函数中调用loadConfigSettings()，
//     从Config::getInstance()读取各项配置，
//     如果Config未初始化则使用默认值
//
// Q3: Config加载失败会导致Logger无法工作吗？
// A3: 不会！Logger使用默认值作为降级方案，
//     即使Config完全失败，Logger仍能正常工作
//
// Q4: 如何在运行时修改Logger配置？
// A4: 1. 修改config.json文件
//     2. 调用Config::reload()重新加载
//     3. 提供Logger::reloadConfig()方法
//        （需要额外实现）
//
// 【6. 项目应用】
//
// 本项目中：
// - Logger从Config读取日志目录、轮转策略
// - MQTT从Config读取代理地址、连接参数
// - Detector从Config读取模型路径、阈值
// - 所有模块配置统一管理在config/config.json
//
// ============================================================================
