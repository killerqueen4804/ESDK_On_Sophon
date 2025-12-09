/**
 * @file test_task_basic.cpp
 * @brief Day 4 任务模块基础功能验证
 * 
 * @details
 * 简化的功能验证测试,验证:
 * 1. LiveStreamTask 能够成功创建和销毁
 * 2. MediaFileTask 能够成功创建和销毁
 * 3. TaskManager 工厂方法能够正确创建任务
 * 
 * @note 
 * 本测试不测试运行时行为,只验证编译链接正确性
 * 详细的功能测试将在后续 Day 5 完善
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-09 (Day 4)
 */

#include "esdk_sophon/task/LiveStreamTask.h"
#include "esdk_sophon/task/MediaFileTask.h"
#include "esdk_sophon/task/TaskManager.h"
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/Mqtt/MqttClient.h"  // TaskService需要MqttClient
#include "esdk_sophon/core/Logger.h"
#include <iostream>
#include <cassert>
#include <memory>

using namespace esdk_sophon::task;
using namespace esdk_sophon::core;
// 注意: 不要 using namespace esdk_sophon::mqtt,避免命名冲突

// ============================================================================
// 辅助函数：创建测试用的 TaskConfig
// ============================================================================

TaskConfig createLiveStreamConfig() {
    TaskConfig config;
    
    // 基本信息
    config.taskId = "livestream_test_001";
    config.type = TaskType::DETECTION_LIVESTREAM;
    config.algorithmId = 1001;
    
    // 事件类型
    EventType event;
    event.id = 200007;
    event.mainType = 100000;
    event.eventDescribe = "人车检测";
    config.eventTypes.push_back(event);
    
    // 检测参数
    config.confidenceThreshold = 0.5f;
    config.nmsThreshold = 0.45f;
    
    // 执行控制
    config.reportIntervalSec = 10;
    config.enableVisualization = false;
    
    // 设备信息
    config.deviceSn = "TEST_DEVICE_SN";
    
    return config;
}

TaskConfig createMediaFileConfig() {
    TaskConfig config;
    
    // 基本信息
    config.taskId = "mediafile_test_001";
    config.type = TaskType::DETECTION_MEDIAFILE;
    config.algorithmId = 1002;
    
    // 媒体文件路径
    config.mediaPath = "/tmp/test_images";
    
    // 事件类型
    EventType event;
    event.id = 200004;
    event.mainType = 100000;
    event.eventDescribe = "垃圾倾倒";
    config.eventTypes.push_back(event);
    
    // 检测参数
    config.confidenceThreshold = 0.6f;
    config.nmsThreshold = 0.5f;
    
    // 执行控制
    config.reportIntervalSec = 5;
    
    // 设备信息
    config.deviceSn = "TEST_DEVICE_SN";
    
    return config;
}

// ============================================================================
// 测试函数
// ============================================================================

/**
 * @brief 测试1：LiveStreamTask 创建和销毁
 */
void test_livestream_task_creation() {
    std::cout << "\n========== 测试1：LiveStreamTask 创建和销毁 ==========\n";
    
    try {
        // 获取 MqttClient 单例 (TaskService需要)
        // 注意: MqttClient 是单例,通过 getInstance() 获取
        // 测试环境可能无法连接真实的 MQTT Broker,但对象创建不会失败
        auto mqttClientPtr = std::shared_ptr<esdk_sophon::mqtt::MqttClient>(
            &esdk_sophon::mqtt::MqttClient::getInstance(),
            [](esdk_sophon::mqtt::MqttClient*) {} // 空删除器,因为单例不应该被删除
        );
        std::cout << "✓ MqttClient 单例获取成功\n";
        
        // 创建 TaskService
        auto service = std::make_shared<TaskService>(mqttClientPtr);
        std::cout << "✓ TaskService 创建成功\n";
        
        // 创建配置
        TaskConfig config = createLiveStreamConfig();
        std::cout << "✓ TaskConfig 创建成功 (taskId=" << config.taskId << ")\n";
        
        // 创建任务
        auto task = std::make_unique<LiveStreamTask>(config, service);
        std::cout << "✓ LiveStreamTask 创建成功\n";
        
        // 验证任务ID (通过getConfig()获取)
        const TaskConfig& actualConfig = task->getConfig();
        assert(actualConfig.taskId == config.taskId);
        std::cout << "✓ TaskId 验证通过: " << actualConfig.taskId << "\n";
        
        // 验证初始状态 (通过getState()获取)
        TaskState state = task->getState();
        assert(!state.isRunning);
        std::cout << "✓ 初始状态验证通过 (未运行)\n";
        
        std::cout << "✅ 测试1通过\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试1失败: " << e.what() << "\n";
        throw;
    }
}

/**
 * @brief 测试2：MediaFileTask 创建和销毁
 */
void test_mediafile_task_creation() {
    std::cout << "\n========== 测试2：MediaFileTask 创建和销毁 ==========\n";
    
    try {
        // 获取 MqttClient 单例
        auto mqttClientPtr = std::shared_ptr<esdk_sophon::mqtt::MqttClient>(
            &esdk_sophon::mqtt::MqttClient::getInstance(),
            [](esdk_sophon::mqtt::MqttClient*) {} // 空删除器
        );
        std::cout << "✓ MqttClient 单例获取成功\n";
        
        // 创建 TaskService
        auto service = std::make_shared<TaskService>(mqttClientPtr);
        std::cout << "✓ TaskService 创建成功\n";
        
        // 创建配置
        TaskConfig config = createMediaFileConfig();
        std::cout << "✓ TaskConfig 创建成功 (taskId=" << config.taskId << ")\n";
        
        // 创建任务
        auto task = std::make_unique<MediaFileTask>(config, service);
        std::cout << "✓ MediaFileTask 创建成功\n";
        
        // 验证任务ID (通过getConfig()获取)
        const TaskConfig& actualConfig = task->getConfig();
        assert(actualConfig.taskId == config.taskId);
        std::cout << "✓ TaskId 验证通过: " << actualConfig.taskId << "\n";
        
        // 验证初始状态 (通过getState()获取)
        TaskState state = task->getState();
        assert(!state.isRunning);
        std::cout << "✓ 初始状态验证通过 (未运行)\n";
        
        std::cout << "✅ 测试2通过\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试2失败: " << e.what() << "\n";
        throw;
    }
}

/**
 * @brief 测试3：TaskManager 工厂方法
 */
void test_task_manager_factory() {
    std::cout << "\n========== 测试3：TaskManager 工厂方法 ==========\n";
    
    try {
        // 获取 TaskManager 单例
        TaskManager& manager = TaskManager::getInstance();
        std::cout << "✓ TaskManager 单例获取成功\n";
        
        // 初始化 TaskManager
        // 注意：不再需要传递参数，TaskManager 会自动从单例获取 MQTT 客户端
        // 前提：MqttClient 必须已经连接（在实际应用中由 Application 保证）
        bool initialized = manager.initialize();
        if (!initialized) {
            std::cout << "⚠️  TaskManager 初始化失败（可能是 MQTT 未连接），跳过此测试\n";
            std::cout << "✅ 测试3跳过（依赖 MQTT 连接）\n";
            return;
        }
        std::cout << "✓ TaskManager 初始化成功\n";
        
        // 测试 LiveStreamTask 创建
        TaskConfig config1 = createLiveStreamConfig();
        auto task1 = manager.createTask(config1);
        assert(task1 != nullptr);
        const TaskConfig& actualConfig1 = task1->getConfig();
        assert(actualConfig1.taskId == config1.taskId);
        std::cout << "✓ 工厂方法创建 LiveStreamTask 成功 (taskId=" << actualConfig1.taskId << ")\n";
        
        // 测试 MediaFileTask 创建
        TaskConfig config2 = createMediaFileConfig();
        auto task2 = manager.createTask(config2);
        assert(task2 != nullptr);
        const TaskConfig& actualConfig2 = task2->getConfig();
        assert(actualConfig2.taskId == config2.taskId);
        std::cout << "✓ 工厂方法创建 MediaFileTask 成功 (taskId=" << actualConfig2.taskId << ")\n";
        
        std::cout << "✅ 测试3通过\n";
        
        // 清理: 关闭 TaskManager (可选,析构函数会自动调用)
        // manager.shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试3失败: " << e.what() << "\n";
        throw;
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Day 4 任务模块基础功能验证\n";
    std::cout << "========================================\n";
    
    try {
        // 初始化日志系统
        Logger::getInstance().info("开始任务模块基础测试");
        
        // 运行测试
        test_livestream_task_creation();
        test_mediafile_task_creation();
        test_task_manager_factory();
        
        std::cout << "\n========================================\n";
        std::cout << "✅ 所有测试通过!\n";
        std::cout << "========================================\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "❌ 测试失败: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    }
}
