/**
 * @file IMqttMessageObserver.h
 * @brief MQTT消息观察者接口
 * 
 * 定义了观察者模式的接口，用于接收MQTT消息通知。
 * 使用观察者模式解耦消息生产者和消费者。
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-26
 */

#ifndef ESDK_SOPHON_MQTT_IMQTT_MESSAGE_OBSERVER_H_
#define ESDK_SOPHON_MQTT_IMQTT_MESSAGE_OBSERVER_H_

#include <string>

namespace esdk_sophon {
namespace mqtt {

/**
 * @brief MQTT消息观察者接口
 * 
 * 实现此接口的类可以接收MQTT消息通知。
 * 这是观察者模式的核心接口。
 * 
 * 使用示例:
 * @code
 * class MyHandler : public IMqttMessageObserver {
 * public:
 *     void onMessageReceived(const std::string& topic, 
 *                           const std::string& message) override {
 *         // 处理收到的消息
 *         std::cout << "收到主题 " << topic << " 的消息: " << message << std::endl;
 *     }
 *     
 *     void onConnectionLost(const std::string& cause) override {
 *         // 处理连接丢失
 *         std::cerr << "连接丢失: " << cause << std::endl;
 *     }
 * };
 * 
 * // 注册观察者
 * auto& mqttClient = MqttClient::getInstance();
 * MyHandler handler;
 * mqttClient.addObserver(&handler);
 * @endcode
 */
class IMqttMessageObserver {
public:
    /**
     * @brief 虚析构函数
     * 
     * 接口类必须有虚析构函数，确保派生类对象正确销毁。
     */
    virtual ~IMqttMessageObserver() = default;

    /**
     * @brief 消息到达回调
     * 
     * 当订阅的主题收到新消息时，此方法被调用。
     * 
     * @param topic 消息主题
     * @param message 消息内容（可能是JSON字符串或其他格式）
     * 
     * @note 此方法可能在MQTT客户端的内部线程中调用，
     *       需要注意线程安全问题。
     * @note 不要在此方法中执行耗时操作，避免阻塞消息接收。
     */
    virtual void onMessageReceived(const std::string& topic,
                                   const std::string& message) = 0;

    /**
     * @brief 连接丢失回调
     * 
     * 当与MQTT代理的连接丢失时，此方法被调用。
     * 
     * @param cause 连接丢失的原因描述
     * 
     * @note MqttClient会自动尝试重新连接，此回调仅用于通知。
     * @note 不要在此方法中调用connect()，避免死锁。
     */
    virtual void onConnectionLost(const std::string& cause) = 0;

    /**
     * @brief 连接成功回调（可选）
     * 
     * 当成功连接到MQTT代理时，此方法被调用。
     * 默认实现为空，子类可按需重写。
     * 
     * @param serverUri 连接的服务器URI
     * 
     * @note 重新连接成功也会触发此回调
     */
    virtual void onConnected(const std::string& serverUri) {
        // 默认实现为空，子类可选择重写
        (void)serverUri;  // 避免未使用参数警告
    }

    /**
     * @brief 消息发送成功回调（可选）
     * 
     * 当消息成功发送到代理时，此方法被调用。
     * 默认实现为空，子类可按需重写。
     * 
     * @param token 消息发送令牌（唯一标识）
     * 
     * @note 仅在QoS > 0时有意义
     */
    virtual void onDeliveryComplete(int token) {
        // 默认实现为空，子类可选择重写
        (void)token;
    }
};

}  // namespace mqtt
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_MQTT_IMQTT_MESSAGE_OBSERVER_H_
