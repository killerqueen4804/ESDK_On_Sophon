/**
 * @file TaskManager.h
 * @brief 任务管理器 - 单例模式
 * 
 * TaskManager 是任务系统的中央控制器,负责:
 * 1. 任务的创建、查询、删除
 * 2. 任务生命周期管理
 * 3. 全局资源协调 (Vision, MQTT, TaskService)
 * 
 * 设计模式:
 * - Singleton (单例): 全局唯一的管理器实例
 * - Factory (工厂): 根据配置创建不同类型的任务
 * - Registry (注册表): 维护所有活跃任务的映射
 * 
 * 线程安全:
 * - 所有公共方法都是线程安全的 (内部使用互斥锁)
 * - 可以在 MQTT 回调线程中安全调用
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-02
 */

#ifndef ESDK_SOPHON_TASK_MANAGER_H_
#define ESDK_SOPHON_TASK_MANAGER_H_

#include "esdk_sophon/task/ITask.h"
#include "esdk_sophon/task/TaskService.h"
// #include "esdk_sophon/vision/Vision.h"  // TODO: Vision类已废弃
#include "esdk_sophon/Mqtt/MqttClient.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace esdk_sophon {
namespace task {

/**
 * @brief 任务管理器 - 单例模式
 * 
 * TaskManager 采用 Meyers' Singleton (C++11 线程安全单例) 实现。
 * 
 * 典型使用流程:
 * @code
 * // 1. 初始化管理器 (在 Application 启动时调用一次)
 * auto& manager = TaskManager::getInstance();
 * manager.initialize(visionPtr, mqttPtr);
 * 
 * // 2. MQTT 回调中创建任务 (接收平台命令)
 * void onDeviceAlgorithmEnable(const Json::Value& payload) {
 *     TaskConfig config = parseConfig(payload);
 *     auto& manager = TaskManager::getInstance();
 *     TaskPtr task = manager.createTask(config);
 *     if (task) {
 *         task->start();
 *     }
 * }
 * 
 * // 3. 查询任务状态
 * TaskPtr task = manager.getTask("task-001");
 * if (task) {
 *     TaskState state = task->getState();
 *     LOG_INFO("Task state: {}", taskStateToString(state));
 * }
 * 
 * // 4. 停止并删除任务
 * manager.removeTask("task-001");
 * 
 * // 5. 应用退出时清理 (析构函数自动调用)
 * manager.shutdown();
 * @endcode
 * 
 * 面试要点:
 * - 单例模式的线程安全实现 (C++11 局部静态变量)
 * - 工厂模式的应用 (createTask)
 * - RAII 资源管理 (initialize/shutdown)
 */
class TaskManager {
public:
    /**
     * @brief 获取单例实例 - Meyers' Singleton
     * 
     * C++11 保证局部静态变量的初始化是线程安全的:
     * - 第一次调用时构造实例
     * - 后续调用直接返回同一实例
     * - 程序退出时自动析构
     * 
     * @return TaskManager& 单例引用
     * 
     * 面试要点:
     * Q: 为什么 Meyers' Singleton 是线程安全的?
     * A: C++11 标准规定,如果多个线程同时进入函数并遇到局部静态变量的初始化,
     *    只有一个线程会执行初始化,其他线程会阻塞等待。
     *    这个行为由编译器和运行时库保证 (通常用一个隐藏的互斥锁实现)。
     * 
     * 经典对比:
     * @code
     * // ❌ 饿汉式 (Eager Initialization) - 浪费资源
     * class Singleton {
     *     static Singleton instance;  // 程序启动时就创建
     * };
     * 
     * // ❌ 懒汉式 (Lazy, 非线程安全)
     * class Singleton {
     *     static Singleton* instance;
     *     static Singleton* getInstance() {
     *         if (!instance) {  // ❌ 多线程竞态条件!
     *             instance = new Singleton();
     *         }
     *         return instance;
     *     }
     * };
     * 
     * // ❌ Double-Checked Locking (C++11 前有问题)
     * static Singleton* getInstance() {
     *     if (!instance) {
     *         std::lock_guard<std::mutex> lock(mutex);
     *         if (!instance) {
     *             instance = new Singleton();  // ❌ 指令重排可能导致问题
     *         }
     *     }
     *     return instance;
     * }
     * 
     * // ✅ Meyers' Singleton (C++11, 推荐)
     * static TaskManager& getInstance() {
     *     static TaskManager instance;  // ✅ 线程安全 + 延迟初始化
     *     return instance;
     * }
     * @endcode
     */
    static TaskManager& getInstance();
    
    // 禁止拷贝和赋值 (单例不应该被复制)
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;
    
    /**
     * @brief 析构函数 - RAII 清理
     * 
     * 自动停止所有任务并释放资源。
     */
    ~TaskManager();
    
    // ==================== 初始化和清理 ====================
    
    /**
     * @brief 初始化管理器 - 必须在使用前调用
     * 
     * 初始化全局资源:
     * - 从单例获取 MQTT 客户端
     * - 创建 TaskService 实例
     * 
     * @return true 初始化成功
     * @return false 初始化失败 (已经初始化过或MQTT未连接)
     * 
     * @note 此方法应该在 Application::init() 中调用一次
     * @note 必须在 MqttClient 连接成功后调用
     * @note 重复调用会返回 false
     * 
     * RAII 原则:
     * - Resource Acquisition Is Initialization
     * - 资源获取即初始化,资源释放在析构函数
     */
    bool initialize();
    
    /**
     * @brief 关闭管理器 - 释放所有资源
     * 
     * 停止并删除所有任务,释放全局资源。
     * 
     * @note 析构函数会自动调用此方法
     * @note 可以手动调用以提前清理资源
     */
    void shutdown();
    
    /**
     * @brief 检查管理器是否已初始化
     */
    bool isInitialized() const;
    
    // ==================== 任务管理 ====================
    
    /**
     * @brief 创建任务 - 工厂方法 ⭐
     * 
     * 根据配置创建对应类型的任务:
     * - DETECTION_LIVESTREAM → LiveStreamTask
     * - DETECTION_MEDIAFILE → MediaFileTask
     * 
     * 创建流程:
     * @code
     * createTask(config)
     *   ┣━━ 1. 验证配置 (config.isValid())
     *   ┣━━ 2. 检查重复 (taskId 已存在?)
     *   ┣━━ 3. 工厂创建
     *   ┃     ├─ case LIVESTREAM: new LiveStreamTask()
     *   ┃     └─ case MEDIAFILE: new MediaFileTask()
     *   ┣━━ 4. 注入依赖 (taskService)
     *   ┣━━ 5. 设置回调
     *   ┣━━ 6. 注册任务
     *   └━━ 7. 返回智能指针
     * @endcode
     * 
     * @param config 任务配置
     * @return TaskPtr 任务智能指针 (失败返回 nullptr)
     * 
     * @note 此方法只创建任务,不启动。需要调用 task->start()
     * @note 线程安全,可在 MQTT 回调中调用
     * 
     * 面试要点:
     * - 工厂模式 (Factory Pattern) 的应用
     * - 依赖注入 (Dependency Injection)
     * - 智能指针的所有权转移
     */
    TaskPtr createTask(const TaskConfig& config);
    
    /**
     * @brief 启动任务 - 便捷方法 🎯
     * 
     * 组合操作: createTask() + task->start()
     * 
     * @param config 任务配置
     * @return true 启动成功
     * @return false 创建或启动失败
     * 
     * @note 如果任务已存在,会尝试重新启动
     * @note 线程安全
     */
    bool startTask(const TaskConfig& config);
    
    /**
     * @brief 停止任务 - 便捷方法 🎯
     * 
     * 组合操作: getTask() + task->stop()
     * 
     * @param taskId 任务 ID
     * @return true 停止成功
     * @return false 任务不存在或已停止
     * 
     * @note 不会从管理器移除任务,只是停止运行
     * @note 线程安全
     */
    bool stopTask(const std::string& taskId);
    
    /**
     * @brief 获取任务
     * 
     * @param taskId 任务 ID
     * @return TaskPtr 任务智能指针 (不存在返回 nullptr)
     * 
     * @note 返回 shared_ptr,调用者可以持有任务引用
     * @note 线程安全
     */
    TaskPtr getTask(const std::string& taskId);
    
    /**
     * @brief 移除任务
     * 
     * 停止任务并从管理器中移除。
     * 
     * @param taskId 任务 ID
     * @return true 移除成功
     * @return false 任务不存在
     * 
     * @note 会调用 task->stop() 等待任务完全停止
     * @note 如果其他地方还持有 TaskPtr,任务对象不会立即销毁
     * @note 线程安全
     */
    bool removeTask(const std::string& taskId);
    
    /**
     * @brief 停止所有任务
     * 
     * 遍历所有任务并调用 stop()。
     * 
     * @note 阻塞方法,等待所有任务停止
     * @note 通常在 shutdown() 或应用退出时调用
     */
    void stopAllTasks();
    
    /**
     * @brief 获取所有任务 ID 列表
     * 
     * @return std::vector<std::string> 任务 ID 列表
     * 
     * @note 返回当前活跃任务的快照
     */
    std::vector<std::string> getAllTaskIds() const;
    
    /**
     * @brief 获取运行中的任务 ID 列表 🎯
     * 
     * @return std::vector<std::string> 运行中任务的 ID 列表
     * 
     * @note 只返回状态为 RUNNING 的任务
     */
    std::vector<std::string> getRunningTaskIds() const;
    
    /**
     * @brief 获取任务数量
     */
    size_t getTaskCount() const;
    
    // ==================== 查询和统计 ====================
    
    /**
     * @brief 获取指定状态的任务数量
     * 
     * @param state 任务状态
     * @return size_t 该状态的任务数量
     * 
     * 使用示例:
     * @code
     * size_t running = manager.getTaskCountByState(TaskState::RUNNING);
     * size_t failed = manager.getTaskCountByState(TaskState::FAILED);
     * LOG_INFO("Running: {}, Failed: {}", running, failed);
     * @endcode
     */
    size_t getTaskCountByState(TaskState state) const;
    
    /**
     * @brief 获取所有任务的统计信息
     * 
     * @return std::unordered_map<std::string, TaskStatistics> 
     *         键: taskId, 值: 统计信息
     */
    std::unordered_map<std::string, TaskStatistics> getAllStatistics() const;
    
    /**
     * @brief 打印所有任务的状态 (用于调试)
     */
    void printTaskStatus() const;

private:
    /**
     * @brief 私有构造函数 (单例模式)
     * 
     * 防止外部创建实例,只能通过 getInstance() 获取。
     */
    TaskManager();
    
    /**
     * @brief 任务状态变更回调 (内部使用)
     * 
     * 当任务状态改变时被调用,用于:
     * - 记录日志
     * - 清理失败/完成的任务
     * - 触发告警
     * 
     * @param taskId 任务 ID
     * @param state 新状态
     */
    void onTaskStateChanged(const std::string& taskId, TaskState state);
    
    /**
     * @brief 任务错误回调 (内部使用)
     * 
     * @param taskId 任务 ID
     * @param errorMsg 错误信息
     */
    void onTaskError(const std::string& taskId, const std::string& errorMsg);

private:
    // ==================== 成员变量 ====================
    
    bool initialized_;  ///< 是否已初始化
    
    /// 全局资源 (所有任务共享)
    // MQTT 客户端通过单例获取，不需要保存
    std::shared_ptr<TaskService> taskService_;     ///< 任务服务 (业务编排)
    
    /// 任务注册表 (Registry Pattern)
    std::unordered_map<std::string, TaskPtr> tasks_;  ///< 键: taskId, 值: Task 智能指针
    
    /// 线程安全保护
    mutable std::mutex mutex_;  ///< 互斥锁 (保护 tasks_ 的并发访问)
    
    /**
     * @brief mutable 关键字解释 ⭐ 面试考点
     * 
     * Q: 为什么 mutex_ 要用 mutable?
     * A: const 成员函数 (如 getTaskCount() const) 承诺不修改对象状态,
     *    但是加锁操作会修改 mutex_ 的内部状态。
     *    mutable 允许在 const 函数中修改该成员。
     * 
     * 示例:
     * @code
     * size_t getTaskCount() const {  // const 函数
     *     std::lock_guard<std::mutex> lock(mutex_);  
     *     // ✅ 如果 mutex_ 是 mutable,可以加锁
     *     // ❌ 如果不是 mutable,编译错误!
     *     return tasks_.size();
     * }
     * @endcode
     * 
     * 经典使用场景:
     * - 互斥锁 (mutex)
     * - 缓存 (cache)
     * - 引用计数 (reference count)
     * - 日志记录 (logger)
     * 这些都是 "逻辑上不改变对象状态" 的操作
     */
};

}  // namespace task
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_TASK_MANAGER_H_
