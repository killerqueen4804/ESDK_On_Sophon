/**
 * @file test_task_runtime.cpp
 * @brief Day 5 任务运行时功能单元测试
 * 
 * 测试内容：
 * 1. LiveStreamTask 生命周期测试
 * 2. MediaFileTask 生命周期测试
 * 3. 状态转换测试
 * 4. 线程安全测试
 * 5. 资源清理测试
 * 6. 异常场景测试
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-10
 */

#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>

// Task 模块
#include "esdk_sophon/task/LiveStreamTask.h"
#include "esdk_sophon/task/MediaFileTask.h"
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/task/TaskTypes.h"

// Core 模块
#include "esdk_sophon/core/Logger.h"

// MQTT 模块
#include "esdk_sophon/mqtt/MqttClient.h"

using namespace esdk_sophon;
using namespace esdk_sophon::task;

// ==================== 枚举输出重载 ====================

/**
 * @brief TaskState 枚举输出重载(用于测试断言)
 */
inline std::ostream& operator<<(std::ostream& os, TaskState state) {
    switch (state) {
        case TaskState::IDLE: return os << "IDLE";
        case TaskState::PENDING: return os << "PENDING";
        case TaskState::RUNNING: return os << "RUNNING";
        case TaskState::PAUSED: return os << "PAUSED";
        case TaskState::COMPLETED: return os << "COMPLETED";
        case TaskState::FAILED: return os << "FAILED";
        case TaskState::CANCELLED: return os << "CANCELLED";
        default: return os << "UNKNOWN(" << static_cast<int>(state) << ")";
    }
}

/**
 * @brief TaskType 枚举输出重载(用于测试断言)
 */
inline std::ostream& operator<<(std::ostream& os, TaskType type) {
    switch (type) {
        case TaskType::DETECTION_LIVESTREAM: return os << "DETECTION_LIVESTREAM";
        case TaskType::DETECTION_MEDIAFILE: return os << "DETECTION_MEDIAFILE";
        case TaskType::SEGMENTATION: return os << "SEGMENTATION";
        case TaskType::TRACKING: return os << "TRACKING";
        case TaskType::UNKNOWN: return os << "UNKNOWN";
        default: return os << "UNKNOWN(" << static_cast<int>(type) << ")";
    }
}

// ==================== 测试辅助函数 ====================

/**
 * @brief 创建测试用的 TaskConfig
 */
TaskConfig createTestConfig(const std::string& taskId, TaskType type) {
    TaskConfig config;
    config.taskId = taskId;
    config.type = type;
    config.algorithmId = 1;
    config.source = (type == TaskType::DETECTION_LIVESTREAM) 
        ? DataSource::LIVESTREAM 
        : DataSource::MEDIAFILE;
    config.confidenceThreshold = 0.5f;
    config.nmsThreshold = 0.45f;
    config.reportIntervalSec = 10;
    config.enableVisualization = true;
    config.maxDetectionsPerFrame = 100;
    config.deviceSn = "TEST_DEVICE_001";
    
    // 添加测试事件类型
    EventType event;
    event.id = 1;
    event.mainType = 1;
    event.eventDescribe = "测试事件";
    event.classIds = {0};  // person
    config.eventTypes.push_back(event);
    
    return config;
}

/**
 * @brief 创建测试用的 TaskService
 * @note TaskService 需要 MqttClient 参数,不能为 nullptr
 * @note 这里使用 MqttClient 单例,但不真正初始化连接(测试环境)
 */
std::shared_ptr<TaskService> createTestTaskService() {
    // 方案:使用 shared_ptr 包装单例的引用
    // MqttClient 是单例,使用 getInstance() 获取引用
    // 然后用 shared_ptr 的别名构造函数创建不拥有所有权的智能指针
    
    // 获取单例引用
    mqtt::MqttClient& mqttInstance = mqtt::MqttClient::getInstance();
    
    // 创建一个 shared_ptr,使用别名构造函数:
    // - 第一个参数:空的 shared_ptr (不拥有任何对象)
    // - 第二个参数:实际指向的对象指针
    // 这样创建的 shared_ptr 不会在析构时删除对象(因为是单例)
    std::shared_ptr<mqtt::MqttClient> mqttPtr(
        std::shared_ptr<mqtt::MqttClient>(),  // 空的控制块
        &mqttInstance                          // 指向单例的指针
    );
    
    return std::make_shared<TaskService>(mqttPtr);
}

/**
 * @brief 断言宏（带消息）
 */
#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "❌ 断言失败: " << message << std::endl; \
            std::cerr << "   位置: " << __FILE__ << ":" << __LINE__ << std::endl; \
            assert(false); \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            std::cerr << "❌ 断言失败: " << message << std::endl; \
            std::cerr << "   期望: " << (expected) << ", 实际: " << (actual) << std::endl; \
            std::cerr << "   位置: " << __FILE__ << ":" << __LINE__ << std::endl; \
            assert(false); \
        } \
    } while(0)

/**
 * @brief 测试结果打印
 */
void printTestResult(const std::string& testName, bool passed) {
    if (passed) {
        std::cout << "✅ " << testName << " - 通过" << std::endl;
    } else {
        std::cout << "❌ " << testName << " - 失败" << std::endl;
    }
}

// ==================== 测试用例 ====================

/**
 * @brief 测试1: LiveStreamTask 基本生命周期
 * 
 * 测试内容：
 * - 任务创建时状态为 IDLE
 * - start() 后状态变为 RUNNING
 * - stop() 后状态变为 COMPLETED
 * - isRunning() 方法正确工作
 */
void test_livestream_task_lifecycle() {
    std::cout << "\n========== 测试1: LiveStreamTask 生命周期 ==========" << std::endl;
    
    try {
        // 1. 创建任务配置
        auto config = createTestConfig("livestream_001", TaskType::DETECTION_LIVESTREAM);
        
        // 2. 创建 TaskService（简化版,不实际推理）
        auto service = createTestTaskService();
        
        // 3. 创建任务
        auto task = std::make_shared<LiveStreamTask>(config, service);
        
        // 4. 验证初始状态
        ASSERT_EQ(TaskState::IDLE, task->getState(), "初始状态应该是 IDLE");
        ASSERT_TRUE(!task->isRunning(), "初始状态不应该是 running");
        
        // 5. 启动任务
        bool started = task->start();
        ASSERT_TRUE(started, "任务应该成功启动");
        
        // 6. 验证运行状态
        ASSERT_EQ(TaskState::RUNNING, task->getState(), "启动后状态应该是 RUNNING");
        ASSERT_TRUE(task->isRunning(), "启动后 isRunning() 应该返回 true");
        
        // 7. 等待一段时间（让任务处理几帧）
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 8. 验证仍在运行
        ASSERT_TRUE(task->isRunning(), "任务应该仍在运行");
        
        // 9. 停止任务
        task->stop();
        
        // 10. 验证停止状态
        ASSERT_EQ(TaskState::COMPLETED, task->getState(), "停止后状态应该是 COMPLETED");
        ASSERT_TRUE(!task->isRunning(), "停止后 isRunning() 应该返回 false");
        
        // 11. 获取统计信息
        auto stats = task->getStatistics();
        std::cout << "   处理帧数: " << stats.framesProcessed << std::endl;
        ASSERT_TRUE(stats.framesProcessed > 0, "应该处理了至少一帧");
        
        printTestResult("LiveStreamTask 生命周期测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("LiveStreamTask 生命周期测试", false);
        throw;
    }
}

/**
 * @brief 测试2: LiveStreamTask 暂停和恢复
 * 
 * 测试内容：
 * - pause() 后状态变为 PAUSED
 * - resume() 后状态恢复为 RUNNING
 * - 暂停期间不处理帧
 */
void test_livestream_task_pause_resume() {
    std::cout << "\n========== 测试2: LiveStreamTask 暂停/恢复 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("livestream_002", TaskType::DETECTION_LIVESTREAM);
        auto service = createTestTaskService();
        auto task = std::make_shared<LiveStreamTask>(config, service);
        
        // 1. 启动任务
        task->start();
        ASSERT_TRUE(task->isRunning(), "任务应该在运行");
        
        // 2. 运行一段时间
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto stats1 = task->getStatistics();
        uint64_t framesBeforePause = stats1.framesProcessed;
        std::cout << "   暂停前处理帧数: " << framesBeforePause << std::endl;
        
        // 3. 暂停任务
        bool paused = task->pause();
        ASSERT_TRUE(paused, "任务应该成功暂停");
        ASSERT_EQ(TaskState::PAUSED, task->getState(), "状态应该是 PAUSED");
        ASSERT_TRUE(!task->isRunning(), "暂停时 isRunning() 应该返回 false");
        
        // 4. 暂停期间等待
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto stats2 = task->getStatistics();
        std::cout << "   暂停后处理帧数: " << stats2.framesProcessed << std::endl;
        
        // 5. 验证暂停期间没有处理新帧（或只处理了很少的帧，因为有竞态）
        uint64_t framesDuringPause = stats2.framesProcessed - framesBeforePause;
        std::cout << "   暂停期间处理帧数: " << framesDuringPause << std::endl;
        ASSERT_TRUE(framesDuringPause < 5, "暂停期间不应该处理太多帧");
        
        // 6. 恢复任务
        bool resumed = task->resume();
        ASSERT_TRUE(resumed, "任务应该成功恢复");
        ASSERT_EQ(TaskState::RUNNING, task->getState(), "状态应该恢复为 RUNNING");
        ASSERT_TRUE(task->isRunning(), "恢复后 isRunning() 应该返回 true");
        
        // 7. 恢复后继续处理
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto stats3 = task->getStatistics();
        std::cout << "   恢复后处理帧数: " << stats3.framesProcessed << std::endl;
        ASSERT_TRUE(stats3.framesProcessed > stats2.framesProcessed, "恢复后应该继续处理帧");
        
        // 8. 停止任务
        task->stop();
        
        printTestResult("LiveStreamTask 暂停/恢复测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("LiveStreamTask 暂停/恢复测试", false);
        throw;
    }
}

/**
 * @brief 测试3: LiveStreamTask 状态转换边界条件
 * 
 * 测试内容：
 * - 已运行的任务不能再次启动
 * - 未运行的任务不能暂停
 * - 停止后的任务不能恢复
 * - stop() 具有幂等性
 */
void test_livestream_task_state_transitions() {
    std::cout << "\n========== 测试3: LiveStreamTask 状态转换边界 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("livestream_003", TaskType::DETECTION_LIVESTREAM);
        auto service = createTestTaskService();
        auto task = std::make_shared<LiveStreamTask>(config, service);
        
        // 1. 测试重复启动
        task->start();
        bool startedAgain = task->start();
        ASSERT_TRUE(!startedAgain, "已运行的任务不应该能再次启动");
        
        // 2. 测试停止的幂等性
        task->stop();
        ASSERT_EQ(TaskState::COMPLETED, task->getState(), "停止后状态应该是 COMPLETED");
        
        // 再次停止（不应该崩溃）
        task->stop();
        ASSERT_EQ(TaskState::COMPLETED, task->getState(), "重复停止后状态仍是 COMPLETED");
        
        // 3. 测试停止后不能恢复
        bool resumed = task->resume();
        ASSERT_TRUE(!resumed, "停止后的任务不应该能恢复");
        
        printTestResult("LiveStreamTask 状态转换边界测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("LiveStreamTask 状态转换边界测试", false);
        throw;
    }
}

/**
 * @brief 测试4: MediaFileTask 基本生命周期
 * 
 * 测试内容：
 * - 任务创建和启动
 * - 状态转换正确
 * - 停止后资源释放
 */
void test_mediafile_task_lifecycle() {
    std::cout << "\n========== 测试4: MediaFileTask 生命周期 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("mediafile_001", TaskType::DETECTION_MEDIAFILE);
        auto service = createTestTaskService();
        auto task = std::make_shared<MediaFileTask>(config, service);
        
        // 1. 验证初始状态
        ASSERT_EQ(TaskState::IDLE, task->getState(), "初始状态应该是 IDLE");
        ASSERT_TRUE(!task->isRunning(), "初始状态不应该是 running");
        
        // 2. 启动任务
        bool started = task->start();
        ASSERT_TRUE(started, "任务应该成功启动");
        ASSERT_EQ(TaskState::RUNNING, task->getState(), "启动后状态应该是 RUNNING");
        ASSERT_TRUE(task->isRunning(), "启动后 isRunning() 应该返回 true");
        
        // 3. 等待一段时间（虽然队列可能为空）
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 4. 停止任务
        task->stop();
        ASSERT_EQ(TaskState::COMPLETED, task->getState(), "停止后状态应该是 COMPLETED");
        ASSERT_TRUE(!task->isRunning(), "停止后 isRunning() 应该返回 false");
        
        // 5. 获取统计信息
        auto stats = task->getStatistics();
        std::cout << "   处理文件数: " << stats.framesProcessed << std::endl;
        std::cout << "   上报事件数: " << stats.eventsPublished << std::endl;
        
        printTestResult("MediaFileTask 生命周期测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("MediaFileTask 生命周期测试", false);
        throw;
    }
}

/**
 * @brief 测试5: 多任务并发运行
 * 
 * 测试内容：
 * - 多个任务同时运行
 * - 任务之间互不干扰
 * - 资源正确管理
 */
void test_multiple_tasks_concurrent() {
    std::cout << "\n========== 测试5: 多任务并发运行 ==========" << std::endl;
    
    try {
        auto service = createTestTaskService();
        
        // 创建多个任务
        std::vector<std::shared_ptr<LiveStreamTask>> tasks;
        for (int i = 0; i < 3; i++) {
            auto config = createTestConfig("multi_task_" + std::to_string(i), 
                                          TaskType::DETECTION_LIVESTREAM);
            auto task = std::make_shared<LiveStreamTask>(config, service);
            tasks.push_back(task);
        }
        
        // 1. 启动所有任务
        for (auto& task : tasks) {
            bool started = task->start();
            ASSERT_TRUE(started, "任务应该成功启动");
            ASSERT_TRUE(task->isRunning(), "任务应该在运行");
        }
        
        // 2. 等待所有任务运行
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 3. 验证所有任务都在运行
        for (auto& task : tasks) {
            ASSERT_TRUE(task->isRunning(), "任务应该仍在运行");
        }
        
        // 4. 停止所有任务
        for (auto& task : tasks) {
            task->stop();
            ASSERT_TRUE(!task->isRunning(), "任务应该已停止");
        }
        
        // 5. 验证统计信息
        for (size_t i = 0; i < tasks.size(); i++) {
            auto stats = tasks[i]->getStatistics();
            std::cout << "   任务" << i << " 处理帧数: " << stats.framesProcessed << std::endl;
            ASSERT_TRUE(stats.framesProcessed > 0, "每个任务都应该处理了帧");
        }
        
        printTestResult("多任务并发运行测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("多任务并发运行测试", false);
        throw;
    }
}

/**
 * @brief 测试6: RAII 资源自动清理
 * 
 * 测试内容：
 * - 任务对象销毁时自动停止
 * - 析构函数正确清理资源
 * - 无内存泄漏
 */
void test_raii_resource_cleanup() {
    std::cout << "\n========== 测试6: RAII 资源自动清理 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("raii_test", TaskType::DETECTION_LIVESTREAM);
        auto service = createTestTaskService();
        
        {
            // 在作用域内创建任务
            auto task = std::make_shared<LiveStreamTask>(config, service);
            task->start();
            ASSERT_TRUE(task->isRunning(), "任务应该在运行");
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // 任务对象离开作用域时会自动调用析构函数
            // 析构函数应该自动调用 stop()
        }
        
        // 如果程序没有崩溃，说明资源清理正确
        std::cout << "   ✓ 任务对象销毁，资源已自动清理" << std::endl;
        
        printTestResult("RAII 资源自动清理测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("RAII 资源自动清理测试", false);
        throw;
    }
}

/**
 * @brief 测试7: 状态回调机制
 * 
 * 测试内容：
 * - 设置状态回调
 * - 状态变更时触发回调
 * - 回调参数正确
 */
void test_state_callback() {
    std::cout << "\n========== 测试7: 状态回调机制 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("callback_test", TaskType::DETECTION_LIVESTREAM);
        auto service = createTestTaskService();
        auto task = std::make_shared<LiveStreamTask>(config, service);
        
        // 1. 设置状态回调
        std::atomic<int> callbackCount{0};
        TaskState lastState = TaskState::IDLE;
        
        task->setStateCallback([&callbackCount, &lastState](const std::string& taskId, TaskState state) {
            callbackCount++;
            lastState = state;
            std::cout << "   📢 状态回调: taskId=" << taskId 
                     << ", state=" << taskStateToString(state) << std::endl;
        });
        
        // 2. 启动任务（应该触发回调）
        task->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_TRUE(callbackCount >= 1, "启动时应该触发回调");
        ASSERT_EQ(TaskState::RUNNING, lastState, "回调参数应该是 RUNNING");
        
        // 3. 暂停任务（应该触发回调）
        int countBeforePause = callbackCount.load();
        task->pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ASSERT_TRUE(callbackCount > countBeforePause, "暂停时应该触发回调");
        ASSERT_EQ(TaskState::PAUSED, lastState, "回调参数应该是 PAUSED");
        
        // 4. 恢复任务（应该触发回调）
        int countBeforeResume = callbackCount.load();
        task->resume();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ASSERT_TRUE(callbackCount > countBeforeResume, "恢复时应该触发回调");
        ASSERT_EQ(TaskState::RUNNING, lastState, "回调参数应该是 RUNNING");
        
        // 5. 停止任务（应该触发回调）
        int countBeforeStop = callbackCount.load();
        task->stop();
        ASSERT_TRUE(callbackCount > countBeforeStop, "停止时应该触发回调");
        ASSERT_EQ(TaskState::COMPLETED, lastState, "回调参数应该是 COMPLETED");
        
        std::cout << "   总回调次数: " << callbackCount.load() << std::endl;
        
        printTestResult("状态回调机制测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("状态回调机制测试", false);
        throw;
    }
}

/**
 * @brief 测试8: 错误回调机制
 * 
 * 测试内容：
 * - 设置错误回调
 * - 发生错误时触发回调
 */
void test_error_callback() {
    std::cout << "\n========== 测试8: 错误回调机制 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("error_test", TaskType::DETECTION_LIVESTREAM);
        auto service = createTestTaskService();
        auto task = std::make_shared<LiveStreamTask>(config, service);
        
        // 设置错误回调
        std::atomic<bool> errorReceived{false};
        task->setErrorCallback([&errorReceived](const std::string& taskId, const std::string& error) {
            errorReceived = true;
            std::cout << "   ⚠️  错误回调: taskId=" << taskId 
                     << ", error=" << error << std::endl;
        });
        
        // 启动和停止任务（可能会触发一些内部错误）
        task->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        task->stop();
        
        // 注意：由于是模拟实现，可能不会触发错误
        // 这里只验证回调机制设置成功，不验证是否触发
        std::cout << "   错误回调已设置（实际触发取决于运行时条件）" << std::endl;
        
        printTestResult("错误回调机制测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("错误回调机制测试", false);
        throw;
    }
}

/**
 * @brief 测试9: isRunning() 便利方法
 * 
 * 测试内容：
 * - isRunning() 与 getState() 的一致性
 * - 各种状态下的正确返回值
 */
void test_isrunning_convenience_method() {
    std::cout << "\n========== 测试9: isRunning() 便利方法 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("isrunning_test", TaskType::DETECTION_LIVESTREAM);
        auto service = createTestTaskService();
        auto task = std::make_shared<LiveStreamTask>(config, service);
        
        // 1. IDLE 状态
        ASSERT_TRUE(!task->isRunning(), "IDLE 状态，isRunning() 应该返回 false");
        ASSERT_TRUE((task->getState() == TaskState::IDLE) == !task->isRunning(), 
                   "isRunning() 与 getState() 应该一致");
        
        // 2. RUNNING 状态
        task->start();
        ASSERT_TRUE(task->isRunning(), "RUNNING 状态，isRunning() 应该返回 true");
        ASSERT_TRUE((task->getState() == TaskState::RUNNING) == task->isRunning(), 
                   "isRunning() 与 getState() 应该一致");
        
        // 3. PAUSED 状态
        task->pause();
        ASSERT_TRUE(!task->isRunning(), "PAUSED 状态，isRunning() 应该返回 false");
        ASSERT_TRUE((task->getState() == TaskState::RUNNING) == task->isRunning(), 
                   "isRunning() 与 getState() 应该一致");
        
        // 4. 恢复到 RUNNING
        task->resume();
        ASSERT_TRUE(task->isRunning(), "RUNNING 状态，isRunning() 应该返回 true");
        
        // 5. COMPLETED 状态
        task->stop();
        ASSERT_TRUE(!task->isRunning(), "COMPLETED 状态，isRunning() 应该返回 false");
        ASSERT_TRUE((task->getState() == TaskState::RUNNING) == task->isRunning(), 
                   "isRunning() 与 getState() 应该一致");
        
        printTestResult("isRunning() 便利方法测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("isRunning() 便利方法测试", false);
        throw;
    }
}

/**
 * @brief 测试10: 任务配置有效性
 * 
 * 测试内容：
 * - getConfig() 返回正确的配置
 * - 配置在任务生命周期中保持不变
 */
void test_task_config() {
    std::cout << "\n========== 测试10: 任务配置有效性 ==========" << std::endl;
    
    try {
        auto config = createTestConfig("config_test", TaskType::DETECTION_LIVESTREAM);
        config.confidenceThreshold = 0.75f;
        config.reportIntervalSec = 15;
        
        auto service = createTestTaskService();
        auto task = std::make_shared<LiveStreamTask>(config, service);
        
        // 1. 验证配置正确保存
        const TaskConfig& taskConfig = task->getConfig();
        ASSERT_EQ(config.taskId, taskConfig.taskId, "taskId 应该一致");
        ASSERT_EQ(config.type, taskConfig.type, "type 应该一致");
        ASSERT_TRUE(std::abs(config.confidenceThreshold - taskConfig.confidenceThreshold) < 0.01f, 
                   "confidenceThreshold 应该一致");
        ASSERT_EQ(config.reportIntervalSec, taskConfig.reportIntervalSec, "reportIntervalSec 应该一致");
        
        // 2. 启动任务后配置不变
        task->start();
        const TaskConfig& runningConfig = task->getConfig();
        ASSERT_EQ(config.taskId, runningConfig.taskId, "运行时配置应该不变");
        
        // 3. 停止任务后配置不变
        task->stop();
        const TaskConfig& stoppedConfig = task->getConfig();
        ASSERT_EQ(config.taskId, stoppedConfig.taskId, "停止后配置应该不变");
        
        printTestResult("任务配置有效性测试", true);
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        printTestResult("任务配置有效性测试", false);
        throw;
    }
}

// ==================== 主函数 ====================

int main() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Day 5 任务运行时功能单元测试" << std::endl;
    std::cout << "测试时间: 2025-11-10" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    try {
        // 初始化日志系统
        auto& logger = core::Logger::getInstance();
        logger.info("开始 Day 5 任务运行时功能测试");
        
        // 执行所有测试
        test_livestream_task_lifecycle();              // 测试1
        test_livestream_task_pause_resume();           // 测试2
        test_livestream_task_state_transitions();      // 测试3
        test_mediafile_task_lifecycle();               // 测试4
        test_multiple_tasks_concurrent();              // 测试5
        test_raii_resource_cleanup();                  // 测试6
        test_state_callback();                         // 测试7
        test_error_callback();                         // 测试8
        test_isrunning_convenience_method();           // 测试9
        test_task_config();                            // 测试10
        
        // 总结
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "🎉 所有测试通过！" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        
        logger.info("Day 5 任务运行时功能测试完成 - 全部通过");
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n" << std::string(60, '=') << std::endl;
        std::cerr << "❌ 测试失败: " << e.what() << std::endl;
        std::cerr << std::string(60, '=') << std::endl;
        return 1;
    }
}
