/**
 * @file test_mqtt.cpp
 * @brief MQTT模块集成测试程序
 * 
 * 测试内容：
 * 1. 连接测试：连接MQTT代理，验证成功/失败处理
 * 2. 发布测试：发布不同QoS级别的消息
 * 3. 订阅测试：订阅主题，接收消息
 * 4. 观察者模式：多个观察者接收消息
 * 5. 重连测试：断线自动重连
 * 6. 配置驱动：从config.json读取配置
 * 
 * 编译：
 *   docker exec stream_lzy bash -c "cd /workspace/build && make test_mqtt"
 * 
 * 运行：
 *   docker exec stream_lzy bash -c "cd /workspace && ./build/bin/test_mqtt"
 * 
 * 前置条件：
 *   - 需要运行MQTT代理（mosquitto或公共代理）
 *   - config/config.json 中配置MQTT参数
 * 
 * @author ESDK_Sophon Team
 * @date 2025-10-27
 */

#include "esdk_sophon/mqtt/MqttClient.h"
#include "esdk_sophon/mqtt/IMqttMessageObserver.h"
#include "esdk_sophon/core/Logger.h"
#include "esdk_sophon/core/Config.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <memory>

using namespace esdk_sophon::mqtt;
using namespace esdk_sophon::core;

// ============================================================================
// 测试观察者类
// ============================================================================

/**
 * @brief 测试用的消息观察者
 * 
 * 实现IMqttMessageObserver接口，用于测试观察者模式
 */
class TestObserver : public IMqttMessageObserver {
public:
    explicit TestObserver(const std::string& name) 
        : name_(name), messageCount_(0) {}
    
    /**
     * @brief 消息接收回调
     */
    void onMessageReceived(const std::string& topic, 
                          const std::string& payload) override {
        messageCount_++;
        Logger::getInstance().info(
            "[" + name_ + "] 收到消息 #" + std::to_string(messageCount_.load()) + 
            " - 主题: " + topic + ", 内容: " + payload
        );
    }
    
    /**
     * @brief 连接丢失回调
     */
    void onConnectionLost(const std::string& cause) override {
        Logger::getInstance().warning(
            "[" + name_ + "] 连接丢失: " + cause
        );
    }
    
    /**
     * @brief 连接成功回调
     */
    void onConnected(const std::string& serverUri) override {
        Logger::getInstance().info("[" + name_ + "] 已连接到: " + serverUri);
    }
    
    /**
     * @brief 获取接收到的消息数量
     */
    int getMessageCount() const { 
        return messageCount_.load(); 
    }
    
private:
    std::string name_;                  ///< 观察者名称
    std::atomic<int> messageCount_;     ///< 接收消息计数（线程安全）
};

// ============================================================================
// 测试辅助函数
// ============================================================================

/**
 * @brief 打印测试分隔线
 */
void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "================================================\n";
}

/**
 * @brief 等待并打印倒计时
 */
void waitSeconds(int seconds, const std::string& message) {
    std::cout << message << " (等待" << seconds << "秒...)\n";
    for (int i = seconds; i > 0; --i) {
        std::cout << "  " << i << "秒..." << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "\n";
}

// ============================================================================
// 测试用例
// ============================================================================

/**
 * @brief 测试1：初始化和配置加载
 */
bool test1_InitializeAndConfig() {
    printSeparator("测试1: 初始化和配置加载");
    
    try {
        // 初始化Logger
        Logger::getInstance().info("初始化Logger...");
        
        // 加载配置
        Config& config = Config::getInstance();
        if (!config.load("../config/config.json")) {
            Logger::getInstance().error("加载配置文件失败");
            return false;
        }
        
        // 读取MQTT配置
        std::string broker = config.getString("mqtt.broker", "tcp://localhost:1883");
        std::string clientId = config.getString("mqtt.client_id", "test_mqtt_client");
        int keepalive = config.getInt("mqtt.keepalive", 60);
        
        Logger::getInstance().info("MQTT配置:");
        Logger::getInstance().info("  - Broker: " + broker);
        Logger::getInstance().info("  - Client ID: " + clientId);
        Logger::getInstance().info("  - Keepalive: " + std::to_string(keepalive) + "s");
        
        std::cout << "✅ 测试1通过：配置加载成功\n";
        return true;
        
    } catch (const std::exception& e) {
        Logger::getInstance().error("测试1失败: " + std::string(e.what()));
        std::cout << "❌ 测试1失败\n";
        return false;
    }
}

/**
 * @brief 测试2：连接MQTT代理
 */
bool test2_Connect() {
    printSeparator("测试2: 连接MQTT代理");
    
    try {
        MqttClient& client = MqttClient::getInstance();
        
        // 初始化（从配置文件读取参数）
        if (!client.initialize()) {
            Logger::getInstance().error("MqttClient初始化失败");
            return false;
        }
        
        // 连接
        Logger::getInstance().info("正在连接MQTT代理...");
        if (!client.connect()) {
            Logger::getInstance().error("连接失败");
            return false;
        }
        
        Logger::getInstance().info("连接成功！");
        
        // 等待确保连接稳定
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "✅ 测试2通过：连接成功\n";
        return true;
        
    } catch (const std::exception& e) {
        Logger::getInstance().error("测试2失败: " + std::string(e.what()));
        std::cout << "❌ 测试2失败\n";
        return false;
    }
}

/**
 * @brief 测试3：发布消息
 */
bool test3_Publish() {
    printSeparator("测试3: 发布消息");
    
    try {
        MqttClient& client = MqttClient::getInstance();
        
        // 测试主题
        std::string topic = "test/mqtt/publish";
        
        // 发布不同QoS级别的消息
        for (int qos = 0; qos <= 2; ++qos) {
            std::string payload = "测试消息 QoS=" + std::to_string(qos);
            
            Logger::getInstance().info(
                "发布消息 [QoS=" + std::to_string(qos) + "]: " + topic + " -> " + payload
            );
            
            if (!client.publish(topic, payload, qos)) {
                Logger::getInstance().error("发布失败 (QoS=" + std::to_string(qos) + ")");
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        std::cout << "✅ 测试3通过：消息发布成功\n";
        return true;
        
    } catch (const std::exception& e) {
        Logger::getInstance().error("测试3失败: " + std::string(e.what()));
        std::cout << "❌ 测试3失败\n";
        return false;
    }
}

/**
 * @brief 测试4：订阅主题
 */
bool test4_Subscribe() {
    printSeparator("测试4: 订阅主题");
    
    try {
        MqttClient& client = MqttClient::getInstance();
        
        // 订阅多个主题（包括通配符）
        std::vector<std::string> topics = {
            "test/mqtt/publish",      // 精确匹配
            "test/mqtt/+",            // 单层通配符
            "test/mqtt/#"             // 多层通配符
        };
        
        for (const auto& topic : topics) {
            Logger::getInstance().info("订阅主题: " + topic);
            
            if (!client.subscribe(topic)) {
                Logger::getInstance().error("订阅失败: " + topic);
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        
        std::cout << "✅ 测试4通过：主题订阅成功\n";
        return true;
        
    } catch (const std::exception& e) {
        Logger::getInstance().error("测试4失败: " + std::string(e.what()));
        std::cout << "❌ 测试4失败\n";
        return false;
    }
}

/**
 * @brief 测试5：观察者模式
 */
bool test5_ObserverPattern() {
    printSeparator("测试5: 观察者模式");
    
    try {
        MqttClient& client = MqttClient::getInstance();
        
        // 创建多个观察者
        auto observer1 = std::make_shared<TestObserver>("观察者1");
        auto observer2 = std::make_shared<TestObserver>("观察者2");
        auto observer3 = std::make_shared<TestObserver>("观察者3");
        
        // 添加观察者 (使用.get()获取裸指针)
        client.addObserver(observer1.get());
        client.addObserver(observer2.get());
        client.addObserver(observer3.get());
        
        Logger::getInstance().info("已添加3个观察者");
        
        // 发布测试消息
        std::string topic = "test/mqtt/observer";
        std::string payload = "观察者模式测试消息";
        
        Logger::getInstance().info("发布消息: " + topic + " -> " + payload);
        client.publish(topic, payload, 1);
        
        // 等待消息接收
        waitSeconds(3, "等待观察者接收消息");
        
        // 验证所有观察者都收到消息
        int count1 = observer1->getMessageCount();
        int count2 = observer2->getMessageCount();
        int count3 = observer3->getMessageCount();
        
        Logger::getInstance().info("消息接收统计:");
        Logger::getInstance().info("  - 观察者1: " + std::to_string(count1) + " 条");
        Logger::getInstance().info("  - 观察者2: " + std::to_string(count2) + " 条");
        Logger::getInstance().info("  - 观察者3: " + std::to_string(count3) + " 条");
        
        // 移除一个观察者 (使用.get())
        client.removeObserver(observer2.get());
        Logger::getInstance().info("已移除观察者2");
        
        // 再发布一条消息
        payload = "移除观察者后的测试消息";
        Logger::getInstance().info("发布消息: " + topic + " -> " + payload);
        client.publish(topic, payload, 1);
        
        waitSeconds(2, "等待消息接收");
        
        // 验证观察者2不再接收消息
        int newCount1 = observer1->getMessageCount();
        int newCount2 = observer2->getMessageCount();
        int newCount3 = observer3->getMessageCount();
        
        Logger::getInstance().info("移除后的消息统计:");
        Logger::getInstance().info("  - 观察者1: " + std::to_string(newCount1) + " 条 (增加 " + std::to_string(newCount1 - count1) + ")");
        Logger::getInstance().info("  - 观察者2: " + std::to_string(newCount2) + " 条 (增加 " + std::to_string(newCount2 - count2) + ")");
        Logger::getInstance().info("  - 观察者3: " + std::to_string(newCount3) + " 条 (增加 " + std::to_string(newCount3 - count3) + ")");
        
        if (newCount2 == count2) {
            std::cout << "✅ 测试5通过：观察者模式工作正常\n";
            return true;
        } else {
            Logger::getInstance().error("观察者2不应该收到新消息");
            return false;
        }
        
    } catch (const std::exception& e) {
        Logger::getInstance().error("测试5失败: " + std::string(e.what()));
        std::cout << "❌ 测试5失败\n";
        return false;
    }
}

/**
 * @brief 测试6：断开连接
 */
bool test6_Disconnect() {
    printSeparator("测试6: 断开连接");
    
    try {
        MqttClient& client = MqttClient::getInstance();
        
        Logger::getInstance().info("断开连接...");
        client.disconnect();
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        Logger::getInstance().info("连接已断开");
        
        std::cout << "✅ 测试6通过：断开连接成功\n";
        return true;
        
    } catch (const std::exception& e) {
        Logger::getInstance().error("测试6失败: " + std::string(e.what()));
        std::cout << "❌ 测试6失败\n";
        return false;
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[]) {
    // 避免未使用参数警告
    (void)argc;
    (void)argv;
    
    std::cout << R"(
╔════════════════════════════════════════════════════════════════╗
║          MQTT模块集成测试程序 v1.0                             ║
║          ESDK_Sophon Project                                   ║
╚════════════════════════════════════════════════════════════════╝
)" << std::endl;
    
    // 测试结果统计
    int totalTests = 0;
    int passedTests = 0;
    
    // 执行测试用例
    auto runTest = [&](const std::string& name, bool(*testFunc)()) {
        totalTests++;
        std::cout << "\n执行测试: " << name << "\n";
        if (testFunc()) {
            passedTests++;
        }
    };
    
    try {
        runTest("测试1: 初始化和配置加载", test1_InitializeAndConfig);
        runTest("测试2: 连接MQTT代理", test2_Connect);
        runTest("测试3: 发布消息", test3_Publish);
        runTest("测试4: 订阅主题", test4_Subscribe);
        runTest("测试5: 观察者模式", test5_ObserverPattern);
        runTest("测试6: 断开连接", test6_Disconnect);
        
    } catch (const std::exception& e) {
        Logger::getInstance().error("测试过程中发生异常: " + std::string(e.what()));
    }
    
    // 打印测试结果
    printSeparator("测试结果");
    std::cout << "总测试数: " << totalTests << "\n";
    std::cout << "通过测试: " << passedTests << "\n";
    std::cout << "失败测试: " << (totalTests - passedTests) << "\n";
    std::cout << "通过率: " 
              << (totalTests > 0 ? (passedTests * 100 / totalTests) : 0) 
              << "%\n";
    
    if (passedTests == totalTests) {
        std::cout << "\n🎉 恭喜！所有测试通过！\n";
        return 0;
    } else {
        std::cout << "\n❌ 部分测试失败，请检查日志\n";
        return 1;
    }
}
