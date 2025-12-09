/**
 * @file TaskManager.cpp
 * @brief 任务管理器实现 - 管理算法任务的完整生命周期
 * 
 * 实现要点:
 * 1. 单例模式 - 全局唯一的任务管理器
 * 2. 线程安全 - 使用 mutex 保护共享状态
 * 3. RAII 原则 - 智能指针管理资源生命周期
 * 4. 工厂模式 - 根据配置创建不同类型的任务
 * 
 * @author 项目重构者
 * @date 2025-11-09
 */

#include "esdk_sophon/task/TaskManager.h"
#include "esdk_sophon/task/LiveStreamTask.h"
#include "esdk_sophon/task/MediaFileTask.h"
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/core/Logger.h"
#include <algorithm>

namespace esdk_sophon {
namespace task {

// ==================== 单例模式实现 ====================

/**
 * @brief 获取单例实例
 * 
 * 使用局部静态变量实现线程安全的单例模式 (C++11 Meyers' Singleton)
 */
TaskManager& TaskManager::getInstance() {
    static TaskManager instance;
    return instance;
}

/**
 * @brief 构造函数
 */
TaskManager::TaskManager()
    : initialized_(false)
{
    auto& logger = core::Logger::getInstance();
    logger.info("TaskManager 构造完成");
}

/**
 * @brief 析构函数
 */
TaskManager::~TaskManager() {
    shutdown();
    auto& logger = core::Logger::getInstance();
    logger.info("TaskManager 已销毁");
}

// ==================== 初始化和清理 ====================

/**
 * @brief 初始化 TaskManager
 * 
 * @note 不需要传入参数，直接使用单例
 *       MqttClient 和其他依赖都通过单例获取
 */
bool TaskManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& logger = core::Logger::getInstance();
    
    if (initialized_) {
        logger.warning("TaskManager 已经初始化,重复调用被忽略");
        return false;
    }
    
    // 获取 MQTT 客户端单例
    auto& mqttClient = mqtt::MqttClient::getInstance();
    
    //检查 MQTT 是否已连接
    if (!mqttClient.isConnected()) {
        logger.error("TaskManager 初始化失败: MQTT 客户端未连接");
        return false;
    }
    
    // 创建 TaskService (业务编排层)
    // 注意：TaskService 需要 shared_ptr，但我们可以用别名构造
    // 这里我们使用一个技巧：创建一个不管理生命周期的 shared_ptr
    std::shared_ptr<mqtt::MqttClient> mqttPtr(&mqttClient, [](mqtt::MqttClient*){});
    taskService_ = std::make_shared<TaskService>(mqttPtr);
    
    initialized_ = true;
    logger.info("TaskManager 初始化成功");
    
    return true;
}

void TaskManager::shutdown() {
    stopAllTasks();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 清理资源
    taskService_.reset();
    // mqtt_ 不再需要，已移除
    
    initialized_ = false;
}

bool TaskManager::isInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

// ==================== 任务管理 ====================

TaskPtr TaskManager::createTask(const TaskConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& logger = core::Logger::getInstance();
    
    // 1. 检查是否已初始化
    if (!initialized_ || !taskService_) {
        logger.error("TaskManager 未初始化,无法创建任务");
        return nullptr;
    }
    
    // 2. 检查任务是否已存在
    if (tasks_.find(config.taskId) != tasks_.end()) {
        logger.warning("任务已存在: " + config.taskId);
        return tasks_[config.taskId];
    }
    
    // 3. 工厂方法 - 根据任务类型创建具体任务
    TaskPtr task = nullptr;
    
    switch (config.type) {
        case TaskType::DETECTION_LIVESTREAM: {
            logger.info("创建直播流任务: " + config.taskId);
            task = std::make_shared<LiveStreamTask>(config, taskService_);
            break;
        }
        
        case TaskType::DETECTION_MEDIAFILE: {
            logger.info("创建媒体文件任务: " + config.taskId);
            task = std::make_shared<MediaFileTask>(config, taskService_);
            break;
        }
        
        default:
            logger.error("不支持的任务类型: " + std::to_string(static_cast<int>(config.type)));
            return nullptr;
    }
    
    // 4. 检查创建是否成功
    if (!task) {
        logger.error("任务创建失败: " + config.taskId);
        return nullptr;
    }
    
    // 5. 注册任务到管理器
    tasks_[config.taskId] = task;
    
    logger.info("任务创建成功并注册: taskId=" + config.taskId + 
                ", type=" + std::to_string(static_cast<int>(config.type)));
    
    return task;
}

bool TaskManager::startTask(const TaskConfig& config) {
    auto& logger = core::Logger::getInstance();
    
    // 1. 创建任务 (如果已存在会返回已有任务)
    TaskPtr task = createTask(config);
    if (!task) {
        logger.error("启动任务失败: 任务创建失败, taskId=" + config.taskId);
        return false;
    }
    
    // 2. 启动任务
    bool success = task->start();
    if (success) {
        logger.info("任务启动成功: taskId=" + config.taskId);
    } else {
        logger.error("任务启动失败: start()返回false, taskId=" + config.taskId);
    }
    
    return success;
}

bool TaskManager::stopTask(const std::string& taskId) {
    auto& logger = core::Logger::getInstance();
    
    // 1. 获取任务
    TaskPtr task = getTask(taskId);
    if (!task) {
        logger.warning("停止任务失败: 任务不存在, taskId=" + taskId);
        return false;
    }
    
    // 2. 停止任务
    task->stop();
    logger.info("任务已停止: taskId=" + taskId);
    
    return true;
}

TaskPtr TaskManager::getTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        return nullptr;
    }
    
    return it->second;
}

bool TaskManager::removeTask(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& logger = core::Logger::getInstance();
    
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        logger.warning("任务不存在,无法移除: " + taskId);
        return false;
    }
    
    // 停止任务
    auto task = it->second;
    if (task) {
        auto state = task->getState();
        if (state == TaskState::RUNNING || state == TaskState::PAUSED) {
            logger.info("停止任务: " + taskId);
            task->stop();
        }
    }
    
    // 从管理器移除
    tasks_.erase(it);
    
    logger.info("任务已从管理器移除: " + taskId);
    return true;
}

void TaskManager::stopAllTasks() {
    // 先复制任务列表,避免在锁内停止任务(可能耗时)
    std::vector<TaskPtr> taskList;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : tasks_) {
            taskList.push_back(pair.second);
        }
    }
    
    auto& logger = core::Logger::getInstance();
    
    if (!taskList.empty()) {
        logger.info("开始停止所有任务, 共 " + std::to_string(taskList.size()) + " 个");
        
        // 停止所有任务
        for (auto& task : taskList) {
            if (task) {
                auto state = task->getState();
                if (state == TaskState::RUNNING || state == TaskState::PAUSED) {
                    task->stop();
                }
            }
        }
    }
    
    // 清空任务注册表
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.clear();
    }
    
    logger.info("所有任务已停止");
}

size_t TaskManager::getTaskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

std::vector<std::string> TaskManager::getAllTaskIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> taskIds;
    taskIds.reserve(tasks_.size());
    
    for (const auto& pair : tasks_) {
        taskIds.push_back(pair.first);
    }
    
    return taskIds;
}

std::vector<std::string> TaskManager::getRunningTaskIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> runningTaskIds;
    
    for (const auto& pair : tasks_) {
        const auto& task = pair.second;
        if (task && task->getState() == TaskState::RUNNING) {
            runningTaskIds.push_back(pair.first);
        }
    }
    
    return runningTaskIds;
}

}  // namespace task
}  // namespace esdk_sophon
