/**
 * @file MqttHandler.h
 * @brief MQTT消息处理器 - 连接通信层和业务层的中间件
 * 
 * @details
 * MqttHandler是MQTT通信层(MqttClient)和业务逻辑层(TaskManager)之间的桥梁。
 * 
 * 职责：
 * 1. 接收并解析云端MQTT指令
 * 2. 将指令路由到对应的处理函数
 * 3. 调用TaskManager执行业务逻辑
 * 4. 将执行结果应答给云端
 * 5. 上报检测结果到云端
 * 
 * 数据流:
 *   云端 → MQTT → MqttClient → MqttHandler → TaskManager → Vision
 *   Vision → TaskMan      nlohmann::json buildDetectionTaskInfo(const task::TaskConfig& config);

    // ==================== 成员变量 ==================== buildDetectionTaskInfo(const task::TaskConfig& config);

    // ==================== 成员变量 ====================er → MqttClient → MQTT → 云端
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-29
 */

#ifndef ESDK_SOPHON_MQTT_MQTT_HANDLER_H_
#define ESDK_SOPHON_MQTT_MQTT_HANDLER_H_

#include <string>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

#include "esdk_sophon/mqtt/IMqttMessageObserver.h"

namespace esdk_sophon {

// 前向声明
namespace core {
    class Logger;
    class Config;
}

namespace task {
    class TaskManager;
}

// 注意: types::DetectionResult 已废弃，已完全移除

namespace mqtt {

// 前向声明
class MqttClient;

/**
 * @brief MQTT消息处理器
 * 
 * @details
 * 作为MqttClient的观察者，接收MQTT消息并进行业务处理：
 * 
 * 1. **指令解析**: 将JSON字符串解析为结构化数据
 * 2. **指令路由**: 根据method字段调用对应处理函数
 * 3. **业务调用**: 调用TaskManager执行任务管理
 * 4. **应答发送**: 向云端发送执行结果
 * 5. **结果上报**: 上报检测结果到云端
 * 
 * Topic订阅:
 * - `thing/product/{device_sn}/services` - 接收云端指令
 * 
 * Topic发布:
 * - `thing/product/{device_sn}/services_reply` - 应答云端指令
 * - `drone/{device_sn}/info/event` - 上报检测结果
 * 
 * 设计模式:
 * - **单例模式**: 全局唯一实例
 * - **观察者模式**: 实现IMqttMessageObserver接口
 * - **策略模式**: 使用map存储指令处理函数
 * 
 * 使用示例:
 * @code
 * // 1. 获取单例实例
 * auto& handler = MqttHandler::getInstance();
 * 
 * // 2. 初始化
 * if (!handler.initialize()) {
 *     // 初始化失败处理
 * }
 * 
 * // 3. 启动消息处理
 * if (!handler.start()) {
 *     // 启动失败处理
 * }
 * 
 * // 4. 上报检测结果(由Vision模块调用)
 * types::DetectionResult result;
 * // ... 填充result
 * handler.publishDetectionResult(result);
 * 
 * // 5. 程序退出前停止
 * handler.stop();
 * @endcode
 * 
 * @note 线程安全: onMessageReceived可能在MQTT内部线程调用
 * @note 错误处理: 所有JSON解析都用try-catch包裹
 * @note 单例模式: 使用Meyers单例，线程安全
 */
class MqttHandler : public IMqttMessageObserver {
public:
    /**
     * @brief 获取单例实例
     * 
     * @return MqttHandler的唯一实例引用
     * 
     * @note 使用Meyers单例模式，C++11保证线程安全
     */
    static MqttHandler& getInstance();

    /**
     * @brief 初始化MqttHandler
     * 
     * @return true 初始化成功
     * @return false 初始化失败
     * 
     * @details
     * 初始化流程:
     * 1. 从Config读取device_sn配置
     * 2. 构建订阅/发布的topic名称
     * 3. 初始化指令处理函数映射表
     * 
     * @note 必须在start()前调用
     * @note 可以重复调用，会重新加载配置
     */
    bool initialize();

    /**
     * @brief 启动消息处理
     * 
     * @return true 启动成功
     * @return false 启动失败
     * 
     * @details
     * 启动流程:
     * 1. 注册为MqttClient的观察者
     * 2. 订阅云端指令topic: `thing/product/{device_sn}/services`
     * 
     * @note 启动前必须先调用initialize()
     * @note 启动前MqttClient必须已经连接
     */
    bool start();

    /**
     * @brief 停止消息处理
     * 
     * @details
     * 停止流程:
     * 1. 取消订阅所有topic
     * 2. 从MqttClient移除观察者
     * 
     * @note 停止后可以再次调用start()重新启动
     */
    void stop();

    /**
     * @brief 发布事件到云端
     * 
     * @param eventJson 事件JSON对象
     * 
     * @details
     * 将事件上报到MQTT云端。支持任意JSON结构。
     * 
     * Topic: `drone/{device_sn}/info/event`
     * QoS: 0 (不等待确认，提高性能)
     * 
     * @note 由TaskService/MediaFileTask调用
     * @note 现在统一使用 EventCache 模式
     */
    void publishEvent(const nlohmann::json& eventJson);

    // ==================== 废弃方法说明 ====================
    // publishDetectionResult(const types::DetectionResult&) 已废弃并删除
    // 原因: types::DetectionResult 已完全移除，现在使用 EventCache 模式
    // 替代方案: 使用 core::EventCache::publishEvent(topic, eventJson)

    // ==================== IMqttMessageObserver接口实现 ====================

    /**
     * @brief MQTT消息到达回调
     * 
     * @param topic 消息主题
     * @param message 消息内容(JSON字符串)
     * 
     * @details
     * 处理流程:
     * 1. 解析JSON字符串
     * 2. 根据topic匹配:
     *    - `services` → 云端指令 → handleServiceCommand()
     * 3. 异常处理: 捕获JSON解析异常
     * 
     * @note 可能在MQTT内部线程调用，注意线程安全
     * @note 不要在此函数中执行耗时操作
     */
    void onMessageReceived(const std::string& topic,
                          const std::string& message) override;

    /**
     * @brief MQTT连接丢失回调
     * 
     * @param cause 连接丢失原因
     * 
     * @details
     * MqttClient会自动重连，此处仅记录日志。
     * 
     * @note 不要在此函数中调用connect()，避免死锁
     */
    void onConnectionLost(const std::string& cause) override;

    /**
     * @brief MQTT连接成功回调
     * 
     * @param serverUri 服务器URI
     * 
     * @details
     * 连接或重连成功时被调用，记录日志用于调试。
     */
    void onConnected(const std::string& serverUri) override;

    /**
     * @brief MQTT消息发送成功回调
     * 
     * @param token 消息令牌
     * 
     * @details
     * QoS > 0 时消息发送成功会触发此回调。
     */
    void onDeliveryComplete(int token) override;

    // 禁止拷贝和赋值(单例模式)
    MqttHandler(const MqttHandler&) = delete;
    MqttHandler& operator=(const MqttHandler&) = delete;

private:
    /**
     * @brief 私有构造函数(单例模式)
     */
    MqttHandler();

    /**
     * @brief 析构函数
     */
    ~MqttHandler();

    // ==================== 指令路由 ====================

    /**
     * @brief 指令处理函数类型
     */
    using CommandHandler = std::function<void(const nlohmann::json&)>;

    /**
     * @brief 初始化指令处理函数映射表
     * 
     * @details
     * 注册所有支持的指令:
     * - device_algorithm_sync: 算法同步(启动任务)
     * - device_algorithm_enable: 启用算法
     * - device_algorithm_disable: 关闭算法(停止任务)
     * - device_task_end: 任务结束
     * - device_reboot: 设备重启
     */
    void initCommandHandlers();

    /**
     * @brief 处理云端服务调用指令
     * 
     * @param json 完整的消息JSON对象
     * 
     * @details
     * 根据method字段路由到对应处理函数:
     * {
     *   "method": "device_algorithm_sync",
     *   "data": { ... }
     * }
     */
    void handleServiceCommand(const nlohmann::json& json);

    // ==================== 具体指令处理 ====================

    /**
     * @brief 处理算法同步指令(启动任务)
     * 
     * @param data 指令数据部分
     * 
     * @details
     * 1. 解析data为TaskConfig
     * 2. 验证配置有效性
     * 3. 调用TaskManager::startTask()
     * 4. 发送应答(成功/失败)
     */
    void handleAlgorithmSync(const nlohmann::json& data);

    /**
     * @brief 处理启用算法指令
     * 
     * @param data 指令数据部分
     * 
     * @note 目前与algorithmSync相同，预留扩展
     */
    void handleAlgorithmEnable(const nlohmann::json& data);

    /**
     * @brief 处理关闭算法指令(停止任务)
     * 
     * @param data 指令数据部分 {taskID: 1234}
     * 
     * @details
     * 1. 提取taskID
     * 2. 调用TaskManager::stopTask()
     * 3. 发送应答
     */
    void handleAlgorithmDisable(const nlohmann::json& data);

    /**
     * @brief 处理任务结束指令
     * 
     * @param data 指令数据部分 {taskID: 1234}
     * 
     * @details
     * 与algorithmDisable类似，但可能包含额外的清理逻辑
     */
    void handleTaskEnd(const nlohmann::json& data);

    /**
     * @brief 处理设备重启指令
     * 
     * @param data 指令数据部分(可能为空)
     * 
     * @details
     * 1. 停止所有任务
     * 2. 发送应答
     * 3. 执行系统重启命令
     */
    void handleReboot(const nlohmann::json& data);

    // ==================== 应答和上报 ====================

    /**
     * @brief 发送服务调用应答
     * 
     * @param method 原始method名称
     * @param result 结果码: 0=成功, 1=参数错误, 2=任务不存在, 3=资源不足, 255=未知错误
     * @param taskId 任务ID
     * 
     * @details
     * 发布到topic: `thing/product/{device_sn}/services_reply`
     * QoS: 1 (确保应答送达)
     * 
     * 消息格式:
     * {
     *   "result": 0,
     *   "method": "device_algorithm_sync",
     *   "taskID": 1234,
     *   "message": "错误描述" (仅result!=0时)
     * }
     */
    void sendServiceReply(const std::string& method,
                         int result,
                         int taskId);

    // ==================== 成员变量 ====================

    MqttClient& mqttClient_;             ///< MQTT客户端引用
    task::TaskManager& taskManager_;     ///< 任务管理器引用
    core::Logger& logger_;               ///< 日志记录器引用
    core::Config& config_;               ///< 配置管理器引用

    std::string deviceSn_;               ///< 设备序列号 (如 "analysis_device_WRSE7001")
    std::string aircraftSn_;             ///< 飞行器序列号 (如 "1581F6GMD2390900P0GT")
    std::string gatewaySn_;              ///< 机场网关序列号 (如 "7CTDM3D00B9K63")

    // Topic名称(从deviceSn构建)
    std::string servicesTopicSub_;       ///< 订阅: thing/product/{sn}/services
    std::string servicesTopicPub_;       ///< 发布: thing/product/{sn}/services_reply
    std::string eventsTopicPub_;         ///< 发布: thing/product/{sn}/events
    std::string detectionTopicPub_;      ///< 发布: drone/{sn}/info/event
    
    // OSD Topic (从 aircraftSn 构建)
    std::string aircraftOsdTopicSub_;    ///< 订阅: thing/product/{aircraft_sn}/osd
    
    // 机场 Events Topic (从 gatewaySn 构建)
    std::string gatewayEventsTopicSub_;  ///< 订阅: thing/product/{gateway_sn}/events

    // 指令处理函数映射表
    std::map<std::string, CommandHandler> commandHandlers_;

    bool isStarted_;                     ///< 是否已启动
    bool osdSubscribed_;                 ///< OSD topic 是否已订阅
    bool gatewayEventsSubscribed_;       ///< 机场 Events topic 是否已订阅

    // ==================== OSD 数据处理 ====================

    /**
     * @brief 处理飞行器 OSD 消息
     * 
     * @param json OSD JSON 数据
     * 
     * @details
     * 解析 OSD 数据并更新 FlightDataProvider:
     * - latitude, longitude, height (GPS 位置)
     * - attitude_head, attitude_pitch, attitude_roll (姿态)
     * - horizontal_speed, vertical_speed (速度)
     * 
     * OSD 推送频率: 0.5Hz
     */
    void handleAircraftOsd(const nlohmann::json& json);
    
    /**
     * @brief 处理机场 Events 消息
     * 
     * @param json Events JSON 数据
     * 
     * @details
     * 主要处理 flighttask_progress 事件，获取航线任务状态。
     * 当 wayline_mission_state >= 5（到达第一个航点）时，
     * 才允许启用智能变焦拍照，避免在起飞过程中触发拍照。
     */
    void handleGatewayEvents(const nlohmann::json& json);
};

}  // namespace mqtt
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_MQTT_MQTT_HANDLER_H_
