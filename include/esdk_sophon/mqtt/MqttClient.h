/**
 * @file MqttClient.h
 * @brief MQTT客户端类
 * 
 * 基于paho.mqtt.c库实现的MQTT客户端，提供连接、发布、订阅功能。
 * 支持自动重连、QoS控制、观察者模式等特性。
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-26
 */

#ifndef ESDK_SOPHON_MQTT_MQTT_CLIENT_H_
#define ESDK_SOPHON_MQTT_MQTT_CLIENT_H_

#include <string>
#include <memory>
#include <vector>

namespace esdk_sophon {
namespace mqtt {

// 前向声明
class IMqttMessageObserver;

/**
 * @brief MQTT客户端类
 * 
 * 单例模式，提供MQTT通信功能：
 * - 连接管理：连接、断开、自动重连
 * - 消息发布：支持QoS 0/1/2
 * - 消息订阅：支持多主题订阅
 * - 观察者模式：通知多个观察者
 * - 配置驱动：从Config读取配置
 * - 线程安全：内部使用互斥锁保护
 * 
 * 使用示例:
 * @code
 * // 1. 获取单例实例
 * auto& client = MqttClient::getInstance();
 * 
 * // 2. 初始化（从config.json读取配置）
 * if (!client.initialize()) {
 *     // 初始化失败处理
 * }
 * 
 * // 3. 注册观察者
 * MyMessageHandler handler;
 * client.addObserver(&handler);
 * 
 * // 4. 连接到代理
 * if (client.connect()) {
 *     // 5. 订阅主题
 *     client.subscribe("esdk/command");
 *     
 *     // 6. 发布消息
 *     client.publish("esdk/status", "{\"status\":\"online\"}");
 * }
 * 
 * // 7. 程序退出前断开连接
 * client.disconnect();
 * @endcode
 * 
 * @note 线程安全：所有公共方法都是线程安全的
 * @note 自动重连：连接丢失后会自动尝试重连（可配置）
 * @note 单例模式：使用getInstance()获取唯一实例
 */
class MqttClient {
public:
    /**
     * @brief 获取单例实例
     * 
     * 使用Meyers单例模式，线程安全（C++11保证）。
     * 
     * @return MqttClient的唯一实例引用
     * 
     * @note C++11标准保证局部静态变量初始化的线程安全性
     */
    static MqttClient& getInstance();

    /**
     * @brief 初始化MQTT客户端
     * 
     * 从Config读取配置，包括：
     * - mqtt.broker.host: 代理地址
     * - mqtt.broker.port: 代理端口
     * - mqtt.broker.username: 用户名（可选）
     * - mqtt.broker.password: 密码（可选）
     * - mqtt.broker.client_id: 客户端ID
     * - mqtt.qos: 默认QoS等级
     * - mqtt.keep_alive: 心跳间隔
     * - mqtt.reconnect.*: 重连配置
     * 
     * @return true 初始化成功
     * @return false 初始化失败（配置错误或库初始化失败）
     * 
     * @note 必须在使用其他方法前调用
     * @note 可以多次调用，会使用最新配置重新初始化
     */
    bool initialize();

    /**
     * @brief 连接到MQTT代理
     * 
     * 使用initialize()读取的配置连接到代理。
     * 如果启用了自动重连，连接失败会自动重试。
     * 
     * @return true 连接成功
     * @return false 连接失败
     * 
     * @note 阻塞操作，超时时间由配置决定
     * @note 连接成功后会触发观察者的onConnected()回调
     * @note 如果已经连接，会先断开再重新连接
     */
    bool connect();

    /**
     * @brief 断开与MQTT代理的连接
     * 
     * 优雅地断开连接，等待未完成的消息发送完毕。
     * 
     * @note 断开后不会触发自动重连
     * @note 断开后需要重新调用connect()才能使用
     */
    void disconnect();

    /**
     * @brief 检查是否已连接
     * 
     * @return true 当前已连接到代理
     * @return false 当前未连接
     * 
     * @note 线程安全
     */
    bool isConnected() const;

    /**
     * @brief 发布消息到指定主题
     * 
     * @param topic 主题名称（不能包含通配符#或+）
     * @param message 消息内容（通常是JSON字符串）
     * @param qos QoS等级（0/1/2），默认使用配置中的值
     * @param retained 是否保留消息，默认false
     * 
     * @return true 消息发送成功（QoS 0）或已加入发送队列（QoS 1/2）
     * @return false 发送失败（未连接或参数错误）
     * 
     * @note QoS 0: 最多一次，不保证送达
     * @note QoS 1: 至少一次，可能重复
     * @note QoS 2: 恰好一次，最高可靠性但性能最低
     * @note retained消息会被代理保存，新订阅者会立即收到
     * 
     * 示例:
     * @code
     * client.publish("esdk/status", "{\"battery\":80}", 1, false);
     * @endcode
     */
    bool publish(const std::string& topic, 
                const std::string& message,
                int qos = -1,  // -1表示使用配置中的默认值
                bool retained = false);

    /**
     * @brief 订阅指定主题
     * 
     * @param topic 主题名称（支持通配符#和+）
     * @param qos 订阅QoS等级（0/1/2），默认使用配置中的值
     * 
     * @return true 订阅成功
     * @return false 订阅失败（未连接或主题无效）
     * 
     * @note 主题通配符：
     *       - `+`: 匹配单层，如 `esdk/+/status` 匹配 `esdk/device1/status`
     *       - `#`: 匹配多层，如 `esdk/#` 匹配 `esdk/device1/status/battery`
     * @note 订阅成功后，消息会通过观察者的onMessageReceived()回调通知
     * 
     * 示例:
     * @code
     * client.subscribe("esdk/command");        // 订阅单个主题
     * client.subscribe("esdk/device/+/status"); // 使用单层通配符
     * client.subscribe("esdk/#");              // 使用多层通配符
     * @endcode
     */
    bool subscribe(const std::string& topic, int qos = -1);

    /**
     * @brief 取消订阅指定主题
     * 
     * @param topic 主题名称
     * 
     * @return true 取消订阅成功
     * @return false 取消订阅失败（未连接）
     */
    bool unsubscribe(const std::string& topic);

    // ==================== DJI Cloud API 扩展接口 ====================
    
    /**
     * @brief 暂停航线任务
     * 
     * 发送 flighttask_pause 指令，使无人机悬停在当前位置。
     * 用于目标检测时暂停航线，进行变焦拍照。
     * 
     * @return true 指令发送成功
     * @return false 发送失败（未连接或发送错误）
     * 
     * @note Topic: thing/product/{gateway_sn}/services
     * @note 这是异步操作，发送成功不代表执行成功
     */
    bool pauseWayline();

    /**
     * @brief 恢复航线任务
     * 
     * 发送 flighttask_recovery 指令，恢复暂停的航线任务。
     * 
     * @return true 指令发送成功
     * @return false 发送失败
     * 
     * @note Topic: thing/product/{gateway_sn}/services
     */
    bool resumeWayline();

    /**
     * @brief 框选变焦
     * 
     * 发送 camera_frame_zoom 指令，对指定区域进行变焦放大。
     * 
     * @param payloadIndex 相机 payload_index，如 "81-0-0"
     * @param x1 框选区域左上角 x 坐标（归一化 0.0-1.0）
     * @param y1 框选区域左上角 y 坐标（归一化 0.0-1.0）
     * @param x2 框选区域右下角 x 坐标（归一化 0.0-1.0）
     * @param y2 框选区域右下角 y 坐标（归一化 0.0-1.0）
     * @param cameraType 相机类型，"wide"（广角）或 "zoom"（变焦）
     * @param lockMode 锁定模式，"single" 单次锁定
     * 
     * @return true 指令发送成功
     * @return false 发送失败
     * 
     * @note Topic: thing/product/{gateway_sn}/drc/down
     * 
     * 使用示例：
     * @code
     * // 对画面中心区域进行变焦
     * client.frameZoom("81-0-0", 0.3, 0.3, 0.7, 0.7);
     * @endcode
     */
    bool frameZoom(const std::string& payloadIndex,
                   float x1, float y1, float x2, float y2,
                   const std::string& cameraType = "wide",
                   const std::string& lockMode = "single");

    /**
     * @brief 设置变焦倍率
     * 
     * 发送 camera_focal_length_set 指令，设置相机变焦倍率。
     * 
     * @param payloadIndex 相机 payload_index
     * @param focalLength 焦距值（毫米），范围取决于相机型号
     * 
     * @return true 指令发送成功
     * @return false 发送失败
     * 
     * @note Topic: thing/product/{gateway_sn}/drc/down
     */
    bool setFocalLength(const std::string& payloadIndex, int focalLength);

    /**
     * @brief 拍照
     * 
     * 发送 camera_photo_take 指令，控制相机拍摄照片。
     * 
     * @param payloadIndex 相机 payload_index，如 "81-0-0"
     * 
     * @return true 指令发送成功
     * @return false 发送失败
     * 
     * @note Topic: thing/product/{gateway_sn}/drc/down
     * @note 照片存储在无人机 SD 卡中
     */
    bool takePhoto(const std::string& payloadIndex);

    /**
     * @brief 云台复位
     * 
     * 发送 gimbal_reset 指令，将云台恢复到默认位置。
     * 用于变焦拍照后恢复视野。
     * 
     * @param payloadIndex 相机 payload_index
     * @param resetMode 复位模式:
     *                  - 0: 回中（yaw 回到正前方，pitch 回到水平）
     *                  - 1: 机头朝下（pitch 回到 -90°）
     *                  - 2: 仅 yaw 回中
     *                  - 3: yaw 回到负前方，pitch 回到水平
     * 
     * @return true 指令发送成功
     * @return false 发送失败
     * 
     * @note Topic: thing/product/{gateway_sn}/drc/down
     */
    bool resetGimbal(const std::string& payloadIndex, int resetMode = 0);

    /**
     * @brief 添加消息观察者
     * 
     * @param observer 观察者指针（不能为nullptr）
     * 
     * @note 观察者的生命周期由调用者管理
     * @note 同一观察者不会重复添加
     * @note 线程安全
     */
    void addObserver(IMqttMessageObserver* observer);

    /**
     * @brief 移除消息观察者
     * 
     * @param observer 要移除的观察者指针
     * 
     * @note 如果观察者不存在，调用无效果
     * @note 线程安全
     */
    void removeObserver(IMqttMessageObserver* observer);

    // 禁止拷贝和赋值（单例模式）
    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    MqttClient();

    /**
     * @brief 析构函数
     * 
     * 自动断开连接并清理资源
     */
    ~MqttClient();

    /**
     * @brief Pimpl实现类
     * 
     * 隐藏paho.mqtt.c库的具体实现，避免头文件污染
     */
    class Impl;
    std::unique_ptr<Impl> pImpl_;  ///< Pimpl指针
};

}  // namespace mqtt
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_MQTT_MQTT_CLIENT_H_
