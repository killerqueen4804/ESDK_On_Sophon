/**
 * @file ITask.h
 * @brief 任务抽象接口 (Interface)
 * 
 * 定义了所有任务类型必须实现的统一接口。
 * 使用 "接口隔离原则 (ISP)" 和 "依赖倒置原则 (DIP)"。
 * 
 * 设计模式:
 * - Template Method (模板方法): execute() 定义执行流程骨架
 * - Strategy (策略模式): 不同任务类型实现不同策略
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-02
 */

#ifndef ESDK_SOPHON_TASK_ITASK_H_
#define ESDK_SOPHON_TASK_ITASK_H_

#include "esdk_sophon/task/TaskTypes.h"
#include <memory>
#include <functional>

namespace esdk_sophon {
namespace task {

/**
 * @brief 任务回调函数类型定义
 * 
 * 使用 std::function 实现回调机制,解耦任务和调用者。
 */
using TaskCallback = std::function<void(const std::string& taskId, TaskState state)>;
using ErrorCallback = std::function<void(const std::string& taskId, const std::string& error)>;

/**
 * @brief 任务抽象接口 (Abstract Base Class)
 * 
 * 所有具体任务类 (LiveStreamTask, MediaFileTask) 必须继承此接口。
 * 
 * 生命周期:
 * @code
 *   创建 → start() → [execute 循环] → pause()/resume() → stop() → 销毁
 * @endcode
 * 
 * 线程安全: 
 * - 状态查询方法 (getState, getConfig) 是线程安全的
 * - 控制方法 (start, stop, pause) 由 TaskManager 加锁保护
 * 
 * @note 这是一个**纯虚接口**,不能直接实例化
 * 
 * 面试要点:
 * - 虚函数表 (vtable) 的实现原理
 * - 虚析构函数的必要性
 * - 纯虚函数 (= 0) 和普通虚函数的区别
 */
class ITask {
public:
    /**
     * @brief 虚析构函数 (必须!)
     * 
     * ⚠️ 为什么基类析构函数必须是 virtual?
     * 
     * @code
     * // 如果基类析构不是 virtual
     * class Base {
     *     ~Base() { cout << "~Base" << endl; }  // ❌ 非虚析构
     * };
     * 
     * class Derived : public Base {
     *     int* data;
     *     ~Derived() { delete[] data; cout << "~Derived" << endl; }
     * };
     * 
     * Base* ptr = new Derived();
     * delete ptr;  // ❌ 只调用 ~Base(), 内存泄漏!
     * 
     * // ✅ 正确的写法
     * class Base {
     *     virtual ~Base() { }  // ✅ 虚析构
     * };
     * delete ptr;  // ✅ 调用 ~Derived() 再调用 ~Base()
     * @endcode
     * 
     * 面试标准答案:
     * - 基类指针 delete 派生类对象时,如果析构不是 virtual,
     *   只会调用基类析构,派生类资源不会释放,导致内存泄漏
     * - 一旦类中有 virtual 函数,析构函数就应该是 virtual
     */
    virtual ~ITask() = default;
    
    // ==================== 生命周期控制 ====================
    
    /**
     * @brief 启动任务
     * 
     * 状态转换: PENDING → RUNNING
     * 
     * 实现要点:
     * - 检查前置条件 (配置是否有效, 资源是否就绪)
     * - 初始化资源 (创建线程, 打开文件/流)
     * - 启动执行线程
     * 
     * @return true 启动成功
     * @return false 启动失败 (会触发 errorCallback)
     * 
     * @throws 不应该抛出异常,所有错误通过返回值和回调报告
     */
    virtual bool start() = 0;
    
    /**
     * @brief 停止任务
     * 
     * 状态转换: RUNNING/PAUSED → COMPLETED/CANCELLED
     * 
     * 实现要点:
     * - 设置停止标志
     * - 等待执行线程结束 (join)
     * - 释放资源 (关闭文件, 断开连接)
     * - 发布统计信息
     * 
     * @note 此方法应该是**阻塞的**,返回时任务已完全停止
     */
    virtual void stop() = 0;
    
    /**
     * @brief 暂停任务
     * 
     * 状态转换: RUNNING → PAUSED
     * 
     * @note 只有 RUNNING 状态可以暂停
     * @return true 暂停成功
     */
    virtual bool pause() = 0;
    
    /**
     * @brief 恢复任务
     * 
     * 状态转换: PAUSED → RUNNING
     * 
     * @note 只有 PAUSED 状态可以恢复
     * @return true 恢复成功
     */
    virtual bool resume() = 0;
    
    // ==================== 状态查询 ====================
    
    /**
     * @brief 检查任务是否正在运行 (便利方法)
     * 
     * @return true 任务正在运行
     * @return false 任务未运行
     * 
     * @note 这是一个便利方法，等价于 getState() == TaskState::RUNNING
     * @note 提供此方法的原因：
     *       1. 高频使用：TaskManager、测试代码经常需要检查运行状态
     *       2. 语义清晰：明确表达"是否在运行"的意图
     *       3. 符合惯例：类似 std::vector::empty()
     * 
     * 使用示例:
     * @code
     * if (task->isRunning()) {
     *     task->stop();
     * }
     * @endcode
     */
    virtual bool isRunning() const = 0;
    
    /**
     * @brief 获取任务当前状态
     * 
     * @return TaskState 当前状态枚举值
     * 
     * @note 线程安全 (内部使用原子操作或互斥锁)
     * @note 与 isRunning() 的区别：
     *       - getState() 返回详细状态（IDLE/RUNNING/PAUSED 等）
     *       - isRunning() 只判断是否在运行（简化常见用例）
     */
    virtual TaskState getState() const = 0;
    
    /**
     * @brief 获取任务配置
     * 
     * @return const TaskConfig& 配置的只读引用
     * @note 返回引用避免拷贝大对象
     */
    virtual const TaskConfig& getConfig() const = 0;
    
    /**
     * @brief 获取任务统计信息
     * 
     * @return TaskStatistics 当前统计数据的副本
     * @note 返回副本是因为统计数据在不断更新,引用可能不安全
     */
    virtual TaskStatistics getStatistics() const = 0;
    
    // ==================== 回调设置 ====================
    
    /**
     * @brief 设置状态变更回调
     * 
     * 当任务状态改变时会调用此回调,通知 TaskManager 或其他监听者。
     * 
     * 使用示例:
     * @code
     * task->setStateCallback([](const std::string& id, TaskState state) {
     *     LOG_INFO("Task {} state changed to {}", id, taskStateToString(state));
     * });
     * @endcode
     * 
     * @param callback 回调函数
     * @note 回调会在任务线程中执行,应该快速返回,不要阻塞
     */
    virtual void setStateCallback(TaskCallback callback) = 0;
    
    /**
     * @brief 设置错误回调
     * 
     * 当任务执行出错时调用。
     * 
     * @param callback 错误回调函数
     */
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    // ==================== 任务特定事件处理 ====================
    
    /**
     * @brief 处理"航线任务结束"通知（device_task_end）⭐
     * 
     * 📖 功能说明:
     * 当平台发送 device_task_end 消息时，不同任务类型的处理逻辑不同：
     * - LiveStreamTask: 立即停止任务，推送 device_task_analysis_result
     * - MediaFileTask: 仅标记航线结束标志，继续等待文件传输完成
     * 
     * 状态转换（取决于具体实现）:
     * - LiveStreamTask: RUNNING → COMPLETED
     * - MediaFileTask: RUNNING → RUNNING (仅设置内部标志)
     * 
     * 调用时机: MqttHandler 收到 device_task_end 消息时
     * 
     * 实现要求:
     * - 线程安全：可能在 MQTT 线程中调用
     * - 快速返回：不阻塞 MQTT 线程（长时间操作应异步处理）
     * - 幂等性：重复调用应该是安全的
     * 
     * @note 默认实现为空（不支持此消息的任务类型）
     * @note 派生类应该根据自己的业务逻辑覆盖此方法
     * 
     * 使用示例:
     * @code
     * // 在 MqttHandler::handleTaskEnd() 中
     * auto task = taskMgr.getTask(taskId);
     * if (task) {
     *     task->onTaskEnd();  // 多态调用，自动路由到正确的实现
     * }
     * @endcode
     * 
     * 面试要点:
     * - 虚函数的默认实现（非纯虚函数）
     * - 接口隔离原则（ISP）：不是所有任务都需要此功能
     * - 多态的应用：避免 dynamic_cast 类型判断
     */
    virtual void onTaskEnd() {
        // 默认实现：忽略此消息
        // 不支持 task_end 的任务类型不需要覆盖此方法
    }
    
protected:
    /**
     * @brief 执行任务的核心逻辑 (纯虚函数)
     * 
     * 这是 **模板方法模式** 的应用:
     * - 基类定义执行框架 (start/stop/pause 的通用逻辑)
     * - 派生类实现具体算法 (execute 的细节)
     * 
     * 实现要点:
     * - 循环检查停止标志
     * - 获取数据 (读取帧/文件)
     * - 调用 TaskService 处理数据
     * - 控制帧率 (避免 CPU 占用过高)
     * 
     * @note 此方法在独立线程中运行
     */
    virtual void execute() = 0;
    
    /**
     * @brief 通知状态变更 (供派生类调用)
     * 
     * 派生类在改变状态时应该调用此方法触发回调。
     * 
     * @param newState 新的状态
     */
    virtual void notifyStateChanged(TaskState newState) = 0;
    
    /**
     * @brief 通知错误 (供派生类调用)
     * 
     * @param errorMsg 错误信息
     */
    virtual void notifyError(const std::string& errorMsg) = 0;
};

/**
 * @brief 任务智能指针类型
 * 
 * 使用智能指针管理任务对象的生命周期:
 * - std::shared_ptr: 允许多个地方持有任务引用 (TaskManager, 用户代码)
 * - 自动内存管理: 最后一个持有者释放时自动 delete
 * 
 * 面试要点:
 * - shared_ptr 的引用计数实现
 * - weak_ptr 的作用 (打破循环引用)
 * - make_shared 的优势 (一次内存分配)
 */
using TaskPtr = std::shared_ptr<ITask>;

}  // namespace task
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_TASK_ITASK_H_
