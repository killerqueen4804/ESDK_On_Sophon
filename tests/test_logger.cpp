/**
 * @file test_logger.cpp
 * @brief Logger类独立测试程序
 * @author ESDK_Sophon Team
 * @date 2025-10-25
 * 
 * @details
 * 本程序用于测试Logger类的功能，不依赖其他模块
 */

#include "esdk_sophon/core/Logger.h"
#include <iostream>
#include <thread>
#include <vector>

using namespace esdk_sophon::core;

/**
 * @brief 测试Logger的基本功能
 */
void testBasicLogging() {
    std::cout << "\n========== 测试1: 基本日志功能 ==========\n" << std::endl;
    
    // 获取Logger单例
    auto& logger = Logger::getInstance();
    
    // 测试不同级别的日志
    logger.debug("这是一条DEBUG日志 - 详细调试信息");
    logger.info("这是一条INFO日志 - 程序启动成功");
    logger.warning("这是一条WARNING日志 - 配置项缺失，使用默认值");
    logger.error("这是一条ERROR日志 - 文件读取失败");
    logger.fatal("这是一条FATAL日志 - 致命错误");
    
    std::cout << "\n提示：请检查日志文件 esdk_sophon.log 查看输出内容\n" << std::endl;
}

/**
 * @brief 测试日志级别过滤
 */
void testLogLevelFiltering() {
    std::cout << "\n========== 测试2: 日志级别过滤 ==========\n" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // 设置日志级别为WARNING
    logger.setLevel(LogLevel::WARNING);
    std::cout << "设置日志级别为WARNING，只会记录WARNING、ERROR、FATAL\n" << std::endl;
    
    logger.debug("这条DEBUG不会被记录");
    logger.info("这条INFO不会被记录");
    logger.warning("这条WARNING会被记录");
    logger.error("这条ERROR会被记录");
    
    // 恢复为INFO级别
    logger.setLevel(LogLevel::INFO);
    std::cout << "\n恢复日志级别为INFO\n" << std::endl;
}

/**
 * @brief 测试控制台输出开关
 */
void testConsoleOutput() {
    std::cout << "\n========== 测试3: 控制台输出开关 ==========\n" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // 禁用控制台输出
    logger.setConsoleOutput(false);
    std::cout << "禁用控制台输出（这条日志只写文件，控制台看不到）：" << std::endl;
    logger.info("这条日志只写入文件，不在控制台显示");
    
    // 启用控制台输出
    logger.setConsoleOutput(true);
    std::cout << "\n启用控制台输出：" << std::endl;
    logger.info("这条日志会同时写入文件和控制台");
}

/**
 * @brief 多线程日志测试
 */
void threadLogFunction(int threadId, int logCount) {
    auto& logger = Logger::getInstance();
    
    for (int i = 0; i < logCount; ++i) {
        std::string message = "线程 " + std::to_string(threadId) + 
                             " 日志 " + std::to_string(i);
        logger.info(message);
    }
}

void testMultithreadedLogging() {
    std::cout << "\n========== 测试4: 多线程日志 ==========\n" << std::endl;
    
    const int threadCount = 5;    // 5个线程
    const int logsPerThread = 10; // 每个线程写10条日志
    
    std::vector<std::thread> threads;
    
    std::cout << "启动 " << threadCount << " 个线程，每个线程写 " 
              << logsPerThread << " 条日志..." << std::endl;
    
    // 创建多个线程
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(threadLogFunction, i, logsPerThread);
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "所有线程完成，请检查日志文件确认日志完整性\n" << std::endl;
}

/**
 * @brief 测试日志文件切换
 */
void testLogFileSwitch() {
    std::cout << "\n========== 测试5: 日志文件切换 ==========\n" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // 切换到新的日志文件
    std::cout << "切换日志文件到 test_logger.log" << std::endl;
    if (logger.setLogFile("test_logger.log")) {
        logger.info("这条日志写入到 test_logger.log");
        logger.warning("测试新日志文件写入");
        std::cout << "请检查 test_logger.log 文件\n" << std::endl;
    } else {
        logger.error("无法创建日志文件 test_logger.log");
    }
    
    // 切换回默认日志文件
    std::cout << "切换回默认日志文件 esdk_sophon.log" << std::endl;
    logger.setLogFile("esdk_sophon.log");
    logger.info("已切换回默认日志文件");
}

/**
 * @brief 模拟实际应用场景
 */
void testRealWorldScenario() {
    std::cout << "\n========== 测试6: 实际应用场景模拟 ==========\n" << std::endl;
    
    auto& logger = Logger::getInstance();
    
    // 模拟程序启动
    logger.info("========================================");
    logger.info("ESDK Sophon 视觉处理系统启动");
    logger.info("版本: 1.0.0");
    logger.info("========================================");
    
    // 模拟加载配置
    logger.info("开始加载配置文件...");
    logger.debug("配置文件路径: ../config/config.json");
    logger.info("配置加载成功");
    
    // 模拟MQTT连接
    logger.info("连接MQTT代理...");
    logger.debug("MQTT Broker: localhost:1883");
    logger.info("MQTT连接成功");
    
    // 模拟检测器初始化
    logger.info("初始化YOLOv10检测器...");
    logger.debug("模型文件: models/yolov10n.onnx");
    logger.info("检测器初始化成功");
    
    // 模拟处理流程
    logger.info("开始处理视频流...");
    logger.debug("检测到目标: bbox=[100, 200, 300, 400], confidence=0.95");
    logger.warning("推理耗时较长: 150ms");
    
    // 模拟错误处理
    logger.error("网络连接超时，尝试重连...");
    logger.info("重连成功");
    
    // 模拟程序关闭
    logger.info("========================================");
    logger.info("ESDK Sophon 正常退出");
    logger.info("========================================");
    
    std::cout << "\n实际场景模拟完成，请查看日志文件\n" << std::endl;
}

/**
 * @brief 主函数
 */
int main() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║          ESDK Sophon Logger 功能测试程序                 ║
║                                                           ║
║  本程序将测试Logger类的各项功能：                        ║
║  1. 基本日志功能（DEBUG/INFO/WARNING/ERROR/FATAL）      ║
║  2. 日志级别过滤                                         ║
║  3. 控制台输出开关                                       ║
║  4. 多线程安全性                                         ║
║  5. 日志文件切换                                         ║
║  6. 实际应用场景                                         ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
)" << std::endl;

    try {
        // 运行所有测试
        testBasicLogging();
        testLogLevelFiltering();
        testConsoleOutput();
        testMultithreadedLogging();
        testLogFileSwitch();
        testRealWorldScenario();
        
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "所有测试完成！" << std::endl;
        std::cout << "请检查以下日志文件：" << std::endl;
        std::cout << "  - esdk_sophon.log（主日志文件）" << std::endl;
        std::cout << "  - test_logger.log（测试日志文件）" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
