/**
 * @file test_config.cpp
 * @brief Config类功能测试程序
 * @author ESDK_Sophon Team
 * @date 2025-10-25
 * 
 * @details
 * 测试内容：
 * 1. 配置文件加载
 * 2. 各种类型的配置项读取（string/int/bool/double）
 * 3. 嵌套路径访问（logger.level, mqtt.broker.host）
 * 4. 默认值机制
 * 5. 配置项存在性检查
 * 6. 配置热更新
 */

#include "esdk_sophon/core/Config.h"
#include "esdk_sophon/core/Logger.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace esdk_sophon::core;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 打印测试标题
 */
void printTestTitle(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

/**
 * @brief 打印测试结果
 */
void printTestResult(const std::string& testName, bool passed) {
    std::cout << "[" << (passed ? "✓ PASS" : "✗ FAIL") << "] " 
              << testName << "\n";
}

// ============================================================================
// 测试用例
// ============================================================================

/**
 * @brief 测试1：配置文件加载
 */
void test_ConfigLoading() {
    printTestTitle("测试1：配置文件加载");
    
    auto& config = Config::getInstance();
    
    // 测试加载存在的配置文件
    bool result1 = config.load("../config/config.json");
    printTestResult("加载config.json", result1);
    
    // 测试加载不存在的配置文件
    bool result2 = !config.load("config/nonexistent.json");
    printTestResult("加载不存在的文件应失败", result2);
    
    // 重新加载正确的文件
    config.load("../config/config.json");
    
    std::cout << "当前配置文件路径: " << config.getConfigPath() << "\n";
}

/**
 * @brief 测试2：字符串类型配置读取
 */
void test_StringConfig() {
    printTestTitle("测试2：字符串类型配置读取");
    
    auto& config = Config::getInstance();
    
    // 测试顶级键
    std::string logLevel = config.getString("logger.level", "DEBUG");
    std::cout << "logger.level = " << logLevel << "\n";
    printTestResult("读取logger.level", logLevel == "INFO");
    
    // 测试嵌套键
    std::string mqttHost = config.getString("mqtt.broker.host", "default");
    std::cout << "mqtt.broker.host = " << mqttHost << "\n";
    printTestResult("读取mqtt.broker.host", mqttHost == "localhost");
    
    // 测试多级嵌套
    std::string modelPath = config.getString("detector.yolov10.model_path", "");
    std::cout << "detector.yolov10.model_path = " << modelPath << "\n";
    printTestResult("读取detector.yolov10.model_path", 
                    modelPath == "models/yolov10n.bmodel");
    
    // 测试不存在的键（使用默认值）
    std::string nonexistent = config.getString("nonexistent.key", "default_value");
    std::cout << "nonexistent.key = " << nonexistent << "\n";
    printTestResult("不存在的键使用默认值", nonexistent == "default_value");
}

/**
 * @brief 测试3：整数类型配置读取
 */
void test_IntConfig() {
    printTestTitle("测试3：整数类型配置读取");
    
    auto& config = Config::getInstance();
    
    // 测试整数配置
    int mqttPort = config.getInt("mqtt.broker.port", 0);
    std::cout << "mqtt.broker.port = " << mqttPort << "\n";
    printTestResult("读取mqtt.broker.port", mqttPort == 1883);
    
    int qos = config.getInt("mqtt.qos", 0);
    std::cout << "mqtt.qos = " << qos << "\n";
    printTestResult("读取mqtt.qos", qos == 1);
    
    int maxDays = config.getInt("logger.rotation.max_days", 7);
    std::cout << "logger.rotation.max_days = " << maxDays << "\n";
    printTestResult("读取logger.rotation.max_days", maxDays == 15);
    
    // 测试不存在的键
    int nonexistent = config.getInt("nonexistent.number", 999);
    std::cout << "nonexistent.number = " << nonexistent << "\n";
    printTestResult("不存在的整数键使用默认值", nonexistent == 999);
}

/**
 * @brief 测试4：布尔类型配置读取
 */
void test_BoolConfig() {
    printTestTitle("测试4：布尔类型配置读取");
    
    auto& config = Config::getInstance();
    
    // 测试布尔配置
    bool consoleOutput = config.getBool("logger.console_output", false);
    std::cout << "logger.console_output = " << (consoleOutput ? "true" : "false") << "\n";
    printTestResult("读取logger.console_output", consoleOutput == true);
    
    bool rotationEnabled = config.getBool("logger.rotation.enabled", false);
    std::cout << "logger.rotation.enabled = " << (rotationEnabled ? "true" : "false") << "\n";
    printTestResult("读取logger.rotation.enabled", rotationEnabled == true);
    
    bool mqttReconnect = config.getBool("mqtt.reconnect.enabled", false);
    std::cout << "mqtt.reconnect.enabled = " << (mqttReconnect ? "true" : "false") << "\n";
    printTestResult("读取mqtt.reconnect.enabled", mqttReconnect == true);
    
    // 测试不存在的键
    bool nonexistent = config.getBool("nonexistent.flag", true);
    std::cout << "nonexistent.flag = " << (nonexistent ? "true" : "false") << "\n";
    printTestResult("不存在的布尔键使用默认值", nonexistent == true);
}

/**
 * @brief 测试5：浮点数类型配置读取
 */
void test_DoubleConfig() {
    printTestTitle("测试5：浮点数类型配置读取");
    
    auto& config = Config::getInstance();
    
    // 测试浮点数配置
    double confThreshold = config.getDouble("detector.yolov10.confidence_threshold", 0.0);
    std::cout << "detector.yolov10.confidence_threshold = " << confThreshold << "\n";
    printTestResult("读取detector.yolov10.confidence_threshold", 
                    confThreshold == 0.5);
    
    double nmsThreshold = config.getDouble("detector.yolov10.nms_threshold", 0.0);
    std::cout << "detector.yolov10.nms_threshold = " << nmsThreshold << "\n";
    printTestResult("读取detector.yolov10.nms_threshold", 
                    nmsThreshold == 0.45);
    
    double pitchSpeed = config.getDouble("device.gimbal.pitch_speed", 0.0);
    std::cout << "device.gimbal.pitch_speed = " << pitchSpeed << "\n";
    printTestResult("读取device.gimbal.pitch_speed", pitchSpeed == 10.0);
    
    // 测试不存在的键
    double nonexistent = config.getDouble("nonexistent.value", 3.14);
    std::cout << "nonexistent.value = " << nonexistent << "\n";
    printTestResult("不存在的浮点键使用默认值", nonexistent == 3.14);
}

/**
 * @brief 测试6：配置项存在性检查
 */
void test_HasConfig() {
    printTestTitle("测试6：配置项存在性检查");
    
    auto& config = Config::getInstance();
    
    // 测试存在的键
    bool has1 = config.has("logger.level");
    std::cout << "has(logger.level) = " << (has1 ? "true" : "false") << "\n";
    printTestResult("检查存在的键logger.level", has1);
    
    bool has2 = config.has("mqtt.broker.host");
    std::cout << "has(mqtt.broker.host) = " << (has2 ? "true" : "false") << "\n";
    printTestResult("检查存在的键mqtt.broker.host", has2);
    
    // 测试不存在的键
    bool has3 = config.has("nonexistent.key");
    std::cout << "has(nonexistent.key) = " << (has3 ? "true" : "false") << "\n";
    printTestResult("检查不存在的键", !has3);
}

/**
 * @brief 测试7：配置热更新
 */
void test_ConfigReload() {
    printTestTitle("测试7：配置热更新");
    
    auto& config = Config::getInstance();
    
    std::cout << "初始值: logger.level = " 
              << config.getString("logger.level", "UNKNOWN") << "\n";
    
    std::cout << "\n提示：如需测试热更新，请：\n";
    std::cout << "1. 修改config/config.json中的logger.level\n";
    std::cout << "2. 保存文件\n";
    std::cout << "3. 等待5秒后程序将自动重新加载\n\n";
    
    std::cout << "等待5秒...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // 重新加载配置
    bool reloadResult = config.reload();
    printTestResult("重新加载配置", reloadResult);
    
    std::cout << "重新加载后: logger.level = " 
              << config.getString("logger.level", "UNKNOWN") << "\n";
}

/**
 * @brief 测试8：与Logger集成
 */
void test_LoggerIntegration() {
    printTestTitle("测试8：与Logger集成（演示）");
    
    auto& config = Config::getInstance();
    
    // 读取Logger相关配置
    std::string logDir = config.getString("logger.log_directory", "logs");
    int maxDays = config.getInt("logger.rotation.max_days", 15);
    int maxFileSizeMB = config.getInt("logger.rotation.max_file_size_mb", 100);
    bool consoleOutput = config.getBool("logger.console_output", true);
    
    std::cout << "Logger配置读取演示：\n";
    std::cout << "  日志目录: " << logDir << "\n";
    std::cout << "  保存天数: " << maxDays << " 天\n";
    std::cout << "  文件大小限制: " << maxFileSizeMB << " MB\n";
    std::cout << "  控制台输出: " << (consoleOutput ? "启用" : "禁用") << "\n";
    
    std::cout << "\n说明：这些配置可以在Logger初始化时使用，替换硬编码常量\n";
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Config 类功能测试程序                            ║\n";
    std::cout << "║         ESDK_Sophon 配置管理系统                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    try {
        // 运行所有测试
        test_ConfigLoading();
        test_StringConfig();
        test_IntConfig();
        test_BoolConfig();
        test_DoubleConfig();
        test_HasConfig();
        test_ConfigReload();
        test_LoggerIntegration();
        
        printTestTitle("测试总结");
        std::cout << "所有测试完成！\n";
        std::cout << "\n✅ 建议：\n";
        std::cout << "1. 检查控制台输出，确认所有测试通过\n";
        std::cout << "2. 尝试修改config.json，测试热更新功能\n";
        std::cout << "3. 查看logs/目录下的日志文件（如果Logger已启用）\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n❌ 测试过程中发生异常: " << e.what() << "\n";
        return 1;
    }
}

// ============================================================================
// 📚 测试知识点
// ============================================================================
// 
// 【1. 单元测试的重要性】
// ✅ 为什么要写测试程序？
//    - 验证功能正确性
//    - 防止回归错误（修改后功能失效）
//    - 作为使用示例和文档
//    - 提升代码信心
// 
// 【2. 测试用例设计】
// ✅ 好的测试应该包含：
//    - 正常情况：标准输入，预期输出
//    - 边界情况：空值、最大值、最小值
//    - 异常情况：错误输入、不存在的配置项
//    - 集成测试：多个组件协同工作
// 
// 【3. 测试驱动开发（TDD）】
// ✅ TDD流程：
//    1. 先写测试（定义期望行为）
//    2. 运行测试（应该失败）
//    3. 实现功能
//    4. 运行测试（应该通过）
//    5. 重构代码
// 
// 【4. 面试要点】
// Q: 如何测试配置管理模块？
// A: 1. 测试加载：有效/无效文件
//    2. 测试读取：各种数据类型
//    3. 测试默认值：不存在的配置项
//    4. 测试热更新：reload功能
//    5. 测试线程安全：多线程并发访问
// 
// Q: 为什么需要默认值？
// A: 1. 容错机制：配置缺失时仍能运行
//    2. 向后兼容：新功能的配置可选
//    3. 降级方案：提供合理的备选值
// 
// ============================================================================
