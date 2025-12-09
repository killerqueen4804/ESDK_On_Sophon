/**
 * @file test_event_cache.cpp
 * @brief EventCache 模块测试程序
 * 
 * @details
 * 验证事件缓存服务的核心功能：
 * 1. 单例模式验证
 * 2. 事件发布和缓存
 * 3. MQTT 重连自动重试
 * 4. 过期事件清理
 * 5. 文件持久化和恢复
 * 6. 线程安全验证
 * 7. 最大重试次数控制
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-20
 */

#include "esdk_sophon/core/EventCache.h"
#include "esdk_sophon/core/Logger.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <filesystem>
#include <mutex>  // ⭐ 新增：为 MockMqttPublisher 提供线程安全

using namespace esdk_sophon::core;

// ============================================================================
// 测试辅助工具
// ============================================================================

/**
 * @brief 打印测试标题
 */
void printTestHeader(const std::string& testName) {
    std::cout << "\n========================================\n";
    std::cout << "测试: " << testName << "\n";
    std::cout << "========================================\n";
}

/**
 * @brief 打印测试结果
 */
void printTestResult(bool passed, const std::string& message = "") {
    if (passed) {
        std::cout << "✅ 测试通过";
        if (!message.empty()) {
            std::cout << " - " << message;
        }
        std::cout << "\n";
    } else {
        std::cout << "❌ 测试失败";
        if (!message.empty()) {
            std::cout << " - " << message;
        }
        std::cout << "\n";
    }
}

/**
 * @brief 模拟的 MQTT 发布回调（可控制成功/失败）
 */
class MockMqttPublisher {
public:
    bool shouldSucceed = true;        // 控制发布是否成功
    int publishCount = 0;             // 记录发布次数
    std::vector<std::string> topics;  // 记录发布的主题
    bool enableOutput = true;         // 控制是否输出日志（多线程测试时关闭）
    
    bool publish(const std::string& topic, const std::string& payload) {
        // ⭐ 线程安全：使用互斥锁保护共享数据和输出
        std::lock_guard<std::mutex> lock(mutex_);
        
        publishCount++;
        topics.push_back(topic);
        
        if (enableOutput) {
            std::cout << "  [Mock MQTT] 发布事件: topic=" << topic 
                      << ", payload_size=" << payload.size() 
                      << ", result=" << (shouldSucceed ? "成功" : "失败") << "\n";
        }
        
        return shouldSucceed;
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        publishCount = 0;
        topics.clear();
        shouldSucceed = true;
        enableOutput = true;  // 默认开启输出
    }

private:
    std::mutex mutex_;  // 保护共享数据的互斥锁
};

// 全局 Mock 对象
MockMqttPublisher g_mockMqtt;

// ============================================================================
// 测试用例
// ============================================================================

/**
 * @brief 测试1: 单例模式验证
 */
void test1_Singleton() {
    printTestHeader("测试1: 单例模式验证");
    
    auto& cache1 = EventCache::getInstance();
    auto& cache2 = EventCache::getInstance();
    
    // 验证是同一个实例
    bool passed = (&cache1 == &cache2);
    
    std::cout << "cache1 地址: " << &cache1 << "\n";
    std::cout << "cache2 地址: " << &cache2 << "\n";
    
    assert(passed);
    printTestResult(passed, "两个引用指向同一实例");
}

/**
 * @brief 测试2: 事件发布成功（MQTT 正常）
 */
void test2_PublishSuccess() {
    printTestHeader("测试2: 事件发布成功（MQTT 正常）");
    
    auto& cache = EventCache::getInstance();
    g_mockMqtt.reset();
    g_mockMqtt.shouldSucceed = true;  // 模拟 MQTT 连接正常
    
    // 设置回调
    cache.setPublishCallback([](const std::string& topic, const std::string& payload) {
        return g_mockMqtt.publish(topic, payload);
    });
    
    // 发布事件
    nlohmann::json event = {
        {"taskID", 1001},
        {"eventType", 200007},
        {"timestamp", 1700000000},
        {"detections", nlohmann::json::array()}
    };
    
    bool result = cache.publishEvent("drone/event/detection", event);
    
    std::cout << "发布结果: " << (result ? "成功" : "失败") << "\n";
    std::cout << "Mock MQTT 调用次数: " << g_mockMqtt.publishCount << "\n";
    std::cout << "缓存事件数: " << cache.getCachedEventCount() << "\n";
    
    // 验证
    assert(result == true);                           // 发布应该成功
    assert(g_mockMqtt.publishCount == 1);             // 应该调用了 1 次
    assert(cache.getCachedEventCount() == 0);         // 不应该缓存
    
    printTestResult(true, "事件直接发布，未缓存");
}

/**
 * @brief 测试3: 事件缓存（MQTT 断线）
 */
void test3_CacheOnFailure() {
    printTestHeader("测试3: 事件缓存（MQTT 断线）");
    
    auto& cache = EventCache::getInstance();
    cache.clearAll();  // 清空之前的缓存
    g_mockMqtt.reset();
    g_mockMqtt.shouldSucceed = false;  // 模拟 MQTT 断线
    
    // 发布 3 个事件
    for (int i = 0; i < 3; i++) {
        nlohmann::json event = {
            {"taskID", 2000 + i},
            {"eventType", 200007},
            {"message", "测试事件 " + std::to_string(i)}
        };
        
        bool result = cache.publishEvent("drone/event", event);
        
        std::cout << "事件 " << i << " 发布结果: " << (result ? "成功" : "失败（已缓存）") << "\n";
        assert(result == false);  // 应该失败
    }
    
    std::cout << "缓存事件数: " << cache.getCachedEventCount() << "\n";
    
    // 验证
    assert(cache.getCachedEventCount() == 3);  // 应该缓存了 3 个事件
    
    printTestResult(true, "3个事件已缓存");
}

/**
 * @brief 测试4: MQTT 重连自动重试
 */
void test4_RetryOnReconnect() {
    printTestHeader("测试4: MQTT 重连自动重试");
    
    auto& cache = EventCache::getInstance();
    g_mockMqtt.reset();
    
    // 之前已缓存 3 个事件（test3）
    std::cout << "重连前缓存事件数: " << cache.getCachedEventCount() << "\n";
    
    // 模拟 MQTT 重连成功
    g_mockMqtt.shouldSucceed = true;
    cache.onMqttReconnected();
    
    std::cout << "重连后缓存事件数: " << cache.getCachedEventCount() << "\n";
    std::cout << "Mock MQTT 调用次数: " << g_mockMqtt.publishCount << "\n";
    
    // 验证
    assert(cache.getCachedEventCount() == 0);      // 缓存应该清空
    assert(g_mockMqtt.publishCount == 3);          // 应该发布了 3 次
    
    printTestResult(true, "重连后自动发布了3个缓存事件");
}

/**
 * @brief 测试5: 最大重试次数控制
 */
void test5_MaxRetryCount() {
    printTestHeader("测试5: 最大重试次数控制");
    
    auto& cache = EventCache::getInstance();
    cache.clearAll();
    cache.setMaxRetryCount(2);  // 最多重试 2 次
    
    g_mockMqtt.reset();
    g_mockMqtt.shouldSucceed = false;  // 始终失败
    
    // 缓存 1 个事件
    nlohmann::json event = {{"taskID", 5001}, {"test", "max_retry"}};
    cache.publishEvent("drone/event", event);
    
    std::cout << "初始缓存事件数: " << cache.getCachedEventCount() << "\n";
    
    // 第 1 次重试（失败）
    g_mockMqtt.reset();
    int retry1 = cache.retryAll();
    std::cout << "第1次重试: 成功=" << retry1 << ", 剩余缓存=" << cache.getCachedEventCount() << "\n";
    assert(cache.getCachedEventCount() == 1);  // 应该还在缓存中
    
    // 第 2 次重试（失败）
    g_mockMqtt.reset();
    int retry2 = cache.retryAll();
    std::cout << "第2次重试: 成功=" << retry2 << ", 剩余缓存=" << cache.getCachedEventCount() << "\n";
    assert(cache.getCachedEventCount() == 1);  // 应该还在缓存中
    
    // 第 3 次重试（失败，超过最大次数，应该被丢弃）
    g_mockMqtt.reset();
    int retry3 = cache.retryAll();
    std::cout << "第3次重试: 成功=" << retry3 << ", 剩余缓存=" << cache.getCachedEventCount() << "\n";
    assert(cache.getCachedEventCount() == 0);  // 应该被丢弃
    
    printTestResult(true, "超过最大重试次数，事件已丢弃");
    
    // 恢复默认值
    cache.setMaxRetryCount(5);
}

/**
 * @brief 测试6: 过期事件清理
 */
void test6_ExpiredEventCleanup() {
    printTestHeader("测试6: 过期事件清理");
    
    auto& cache = EventCache::getInstance();
    cache.clearAll();
    cache.setMaxEventAge(2);  // 设置过期时间为 2 秒
    
    g_mockMqtt.reset();
    g_mockMqtt.shouldSucceed = false;
    
    // 缓存 3 个事件
    for (int i = 0; i < 3; i++) {
        nlohmann::json event = {{"taskID", 6000 + i}};
        cache.publishEvent("drone/event", event);
    }
    
    std::cout << "缓存事件数: " << cache.getCachedEventCount() << "\n";
    
    // 等待 3 秒（超过过期时间）
    std::cout << "等待 3 秒...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 清理过期事件
    int cleaned = cache.cleanupExpiredEvents();
    std::cout << "清理过期事件: " << cleaned << " 个\n";
    std::cout << "剩余缓存事件: " << cache.getCachedEventCount() << "\n";
    
    // 验证
    assert(cleaned == 3);                       // 应该清理了 3 个
    assert(cache.getCachedEventCount() == 0);   // 缓存应该清空
    
    printTestResult(true, "过期事件已清理");
    
    // 恢复默认值
    cache.setMaxEventAge(3600);
}

/**
 * @brief 测试7: 文件持久化和恢复
 */
void test7_FilePersistence() {
    printTestHeader("测试7: 文件持久化和恢复");
    
    std::string cacheFile = "test_event_cache.json";
    
    // 清理旧缓存文件
    if (std::filesystem::exists(cacheFile)) {
        std::filesystem::remove(cacheFile);
    }
    
    // ========== 阶段1: 缓存事件并保存 ==========
    {
        auto& cache = EventCache::getInstance();
        cache.clearAll();
        cache.setCacheFilePath(cacheFile);
        
        g_mockMqtt.reset();
        g_mockMqtt.shouldSucceed = false;
        
        // 缓存 2 个事件
        for (int i = 0; i < 2; i++) {
            nlohmann::json event = {
                {"taskID", 7000 + i},
                {"persist", "test"}
            };
            cache.publishEvent("drone/event", event);
        }
        
        std::cout << "阶段1: 缓存了 " << cache.getCachedEventCount() << " 个事件\n";
        
        // 保存到文件
        bool saved = cache.saveToFile();
        std::cout << "保存到文件: " << (saved ? "成功" : "失败") << "\n";
        assert(saved == true);
        
        // 验证文件存在
        assert(std::filesystem::exists(cacheFile));
        std::cout << "缓存文件已创建: " << cacheFile << "\n";
    }
    
    // ========== 阶段2: 模拟程序重启，从文件恢复 ==========
    {
        auto& cache = EventCache::getInstance();
        cache.clearAll();  // 清空内存缓存
        cache.setCacheFilePath(cacheFile);
        
        std::cout << "\n阶段2: 模拟程序重启\n";
        std::cout << "清空内存后缓存事件数: " << cache.getCachedEventCount() << "\n";
        
        // 从文件加载
        int loaded = cache.loadFromFile();
        std::cout << "从文件加载: " << loaded << " 个事件\n";
        std::cout << "加载后缓存事件数: " << cache.getCachedEventCount() << "\n";
        
        // 验证
        assert(loaded == 2);                        // 应该加载了 2 个
        assert(cache.getCachedEventCount() == 2);   // 缓存应该有 2 个
    }
    
    // 清理测试文件
    if (std::filesystem::exists(cacheFile)) {
        std::filesystem::remove(cacheFile);
    }
    
    printTestResult(true, "文件持久化和恢复正常");
}

/**
 * @brief 测试8: 线程安全验证（并发发布）
 */
void test8_ThreadSafety() {
    printTestHeader("测试8: 线程安全验证（并发发布）");
    
    auto& cache = EventCache::getInstance();
    cache.clearAll();
    
    g_mockMqtt.reset();
    g_mockMqtt.shouldSucceed = false;  // 缓存事件
    g_mockMqtt.enableOutput = false;   // ⭐ 关闭 Mock 输出，避免日志混乱
    
    const int threadCount = 5;
    const int eventsPerThread = 10;
    
    std::vector<std::thread> threads;
    
    std::cout << "启动 " << threadCount << " 个线程，每个线程发布 " 
              << eventsPerThread << " 个事件...\n";
    
    // 启动多个线程并发发布事件
    for (int t = 0; t < threadCount; t++) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < eventsPerThread; i++) {
                nlohmann::json event = {
                    {"thread", t},
                    {"index", i},
                    {"taskID", 8000 + t * 100 + i}
                };
                
                cache.publishEvent("drone/event/thread" + std::to_string(t), event);
                
                // 随机短暂休眠，增加并发冲突概率
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& th : threads) {
        th.join();
    }
    
    size_t totalEvents = cache.getCachedEventCount();
    size_t expectedEvents = threadCount * eventsPerThread;
    
    std::cout << "线程数: " << threadCount << "\n";
    std::cout << "每线程事件数: " << eventsPerThread << "\n";
    std::cout << "预期总事件数: " << expectedEvents << "\n";
    std::cout << "实际缓存事件数: " << totalEvents << "\n";
    std::cout << "Mock MQTT 总调用次数: " << g_mockMqtt.publishCount << "\n";
    
    // 验证
    assert(totalEvents == expectedEvents);  // 应该缓存了所有事件，无丢失
    
    printTestResult(true, "多线程并发发布无数据丢失");
    
    // 恢复 Mock 输出
    g_mockMqtt.enableOutput = true;
}

/**
 * @brief 测试9: 混合场景（缓存 + 成功 + 重试）
 */
void test9_MixedScenario() {
    printTestHeader("测试9: 混合场景（缓存 + 成功 + 重试）");
    
    auto& cache = EventCache::getInstance();
    cache.clearAll();
    
    g_mockMqtt.reset();
    
    // 场景1: MQTT 正常，发布 2 个事件（直接成功）
    g_mockMqtt.shouldSucceed = true;
    for (int i = 0; i < 2; i++) {
        nlohmann::json event = {{"phase", "normal"}, {"index", i}};
        cache.publishEvent("drone/event", event);
    }
    std::cout << "场景1（正常）: 缓存数=" << cache.getCachedEventCount() 
              << ", MQTT调用=" << g_mockMqtt.publishCount << "\n";
    assert(cache.getCachedEventCount() == 0);
    
    // 场景2: MQTT 断线，发布 3 个事件（缓存）
    g_mockMqtt.reset();
    g_mockMqtt.shouldSucceed = false;
    for (int i = 0; i < 3; i++) {
        nlohmann::json event = {{"phase", "offline"}, {"index", i}};
        cache.publishEvent("drone/event", event);
    }
    std::cout << "场景2（断线）: 缓存数=" << cache.getCachedEventCount() 
              << ", MQTT调用=" << g_mockMqtt.publishCount << "\n";
    assert(cache.getCachedEventCount() == 3);
    
    // 场景3: MQTT 恢复，重试成功
    g_mockMqtt.reset();
    g_mockMqtt.shouldSucceed = true;
    cache.onMqttReconnected();
    std::cout << "场景3（重连）: 缓存数=" << cache.getCachedEventCount() 
              << ", MQTT调用=" << g_mockMqtt.publishCount << "\n";
    assert(cache.getCachedEventCount() == 0);
    assert(g_mockMqtt.publishCount == 3);
    
    printTestResult(true, "混合场景模拟真实使用流程");
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "  EventCache 模块测试程序\n";
    std::cout << "========================================\n";
    
    try {
        // 初始化日志
        Logger::getInstance().info("开始 EventCache 测试");
        
        // 运行测试
        test1_Singleton();
        test2_PublishSuccess();
        test3_CacheOnFailure();
        test4_RetryOnReconnect();
        test5_MaxRetryCount();
        test6_ExpiredEventCleanup();
        test7_FilePersistence();
        test8_ThreadSafety();
        test9_MixedScenario();
        
        // 总结
        std::cout << "\n========================================\n";
        std::cout << "  ✅ 所有 EventCache 测试通过！\n";
        std::cout << "========================================\n";
        
        Logger::getInstance().info("EventCache 测试完成");
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "❌ 测试失败: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    }
}
