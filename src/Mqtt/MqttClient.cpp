/**
 * @file MqttClient.cpp
 * @brief MQTT客户端实现
 * 
 * 基于paho.mqtt.c库实现MQTT通信功能。
 * 使用Pimpl模式隐藏paho库的复杂API。
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-26
 */

#include "esdk_sophon/mqtt/MqttClient.h"
#include "esdk_sophon/mqtt/IMqttMessageObserver.h"
#include "esdk_sophon/core/Logger.h"
#include "esdk_sophon/core/Config.h"

#include <MQTTClient.h>  // paho.mqtt.c 库
#include <nlohmann/json.hpp>  // JSON库，用于DJI Cloud API消息构建

#include <atomic>   // 原子操作，防止并发重连
#include <mutex>
#include <condition_variable>  // 条件变量，用于异步重连
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstring>
#include <random>      // 用于UUID生成
#include <sstream>     // 用于字符串流
#include <iomanip>     // 用于格式化输出

namespace esdk_sophon {
namespace mqtt {

// ==================== 默认配置常量 ====================

namespace {
    // 默认值（降级方案）
    constexpr const char* DEFAULT_BROKER_HOST = "localhost";
    constexpr int DEFAULT_BROKER_PORT = 1883;
    constexpr const char* DEFAULT_CLIENT_ID = "esdk_sophon_mqtt_client";
    constexpr int DEFAULT_QOS = 1;
    constexpr int DEFAULT_KEEP_ALIVE = 60;
    constexpr int DEFAULT_MAX_RETRIES = 5;
    constexpr int DEFAULT_RETRY_INTERVAL_MS = 3000;
    constexpr int DEFAULT_TIMEOUT_MS = 10000;
}

// ==================== Pimpl实现类 ====================

/**
 * @brief MqttClient的内部实现类
 * 
 * 隐藏paho.mqtt.c的复杂细节，提供C++风格的封装。
 * 
 * 职责：
 * 1. 管理paho客户端生命周期
 * 2. 处理paho的回调函数
 * 3. 管理观察者列表
 * 4. 实现自动重连逻辑
 * 5. 线程安全保护
 */
class MqttClient::Impl {
public:
    Impl();
    ~Impl();

    // 禁止拷贝和赋值
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // 公共接口
    bool initialize();
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    bool publish(const std::string& topic, const std::string& message,
                int qos, bool retained);
    bool subscribe(const std::string& topic, int qos);
    bool unsubscribe(const std::string& topic);
    
    void addObserver(IMqttMessageObserver* observer);
    void removeObserver(IMqttMessageObserver* observer);
    
    // ===== DJI Cloud API 扩展方法 =====
    bool pauseWayline();
    bool resumeWayline();
    bool frameZoom(const std::string& payloadIndex,
                   float x1, float y1, float x2, float y2,
                   const std::string& cameraType,
                   const std::string& lockMode);
    bool setFocalLength(const std::string& payloadIndex, int focalLength);
    bool takePhoto(const std::string& payloadIndex);
    bool resetGimbal(const std::string& payloadIndex, int resetMode);

private:
    // ===== paho回调函数（静态方法，C风格） =====
    
    /**
     * @brief 连接丢失回调
     * 
     * paho库在检测到连接丢失时调用此函数。
     * 
     * @param context 用户数据指针（this指针）
     * @param cause 连接丢失原因
     */
    static void onConnectionLostCallback(void* context, char* cause);
    
    /**
     * @brief 消息到达回调
     * 
     * paho库收到订阅消息时调用此函数。
     * 
     * @param context 用户数据指针（this指针）
     * @param topicName 消息主题
     * @param topicLen 主题长度
     * @param message paho消息结构
     * @return 1表示成功处理，0表示失败
     */
    static int onMessageArrivedCallback(void* context, char* topicName,
                                       int topicLen, MQTTClient_message* message);
    
    /**
     * @brief 消息发送完成回调
     * 
     * paho库确认消息发送成功时调用此函数（仅QoS>0时）。
     * 
     * @param context 用户数据指针（this指针）
     * @param token 消息令牌
     */
    static void onDeliveryCompleteCallback(void* context, MQTTClient_deliveryToken token);

    // ===== 内部方法（C++风格） =====
    
    /**
     * @brief 通知所有观察者：连接丢失
     * 
     * @param cause 丢失原因
     */
    void notifyConnectionLost(const std::string& cause);
    
    /**
     * @brief 通知所有观察者：消息到达
     * 
     * @param topic 消息主题
     * @param message 消息内容
     */
    void notifyMessageReceived(const std::string& topic, const std::string& message);
    
    /**
     * @brief 通知所有观察者：连接成功
     * 
     * @param serverUri 服务器URI
     */
    void notifyConnected(const std::string& serverUri);
    
    /**
     * @brief 通知所有观察者：消息发送成功
     * 
     * @param token 消息令牌
     */
    void notifyDeliveryComplete(int token);
    
    /**
     * @brief 尝试自动重连
     * 
     * 在连接丢失后，根据配置进行重连尝试。
     */
    void attemptReconnect();
    
    /**
     * @brief 重新订阅所有主题
     * 
     * 在重连成功后，自动重新订阅之前订阅的所有主题。
     */
    void resubscribeAll();
    
    /**
     * @brief 从Config加载配置
     * 
     * 读取mqtt.*相关配置，失败时使用默认值。
     */
    void loadConfig();

    // ===== 成员变量 =====
    
    MQTTClient pahoClient_;  ///< paho客户端句柄
    
    // 配置参数（从Config读取）
    std::string brokerHost_;        ///< 代理地址
    int brokerPort_;                ///< 代理端口
    std::string username_;          ///< 用户名
    std::string password_;          ///< 密码
    std::string clientId_;          ///< 客户端ID
    int defaultQos_;                ///< 默认QoS等级
    int keepAliveInterval_;         ///< 心跳间隔（秒）
    int timeoutMs_;                 ///< 连接超时（毫秒）
    
    // 重连配置
    bool reconnectEnabled_;         ///< 是否启用自动重连
    int maxRetries_;                ///< 最大重试次数
    int retryIntervalMs_;           ///< 重试间隔（毫秒）
    int currentRetries_;            ///< 当前重试次数
    
    // 状态标志
    bool initialized_;              ///< 是否已初始化
    mutable bool connected_;        ///< 是否已连接（mutable允许在const方法中修改）
    std::atomic<bool> reconnecting_;  ///< 是否正在重连（防止并发重连）
    
    // 观察者列表
    std::vector<IMqttMessageObserver*> observers_;
    mutable std::mutex observersMutex_;  ///< 保护观察者列表的互斥锁
    
    // ⭐ 订阅主题列表（用于重连后自动重新订阅）
    struct SubscriptionInfo {
        std::string topic;
        int qos;
    };
    std::vector<SubscriptionInfo> subscriptions_;  ///< 已订阅的主题列表
    mutable std::mutex subscriptionsMutex_;        ///< 保护订阅列表的互斥锁
    
    // 连接锁
    mutable std::mutex connectionMutex_;  ///< 保护连接状态的互斥锁
    
    // ⭐ 异步重连支持
    std::thread reconnectThread_;              ///< 重连线程
    std::condition_variable reconnectSignal_;  ///< 重连信号
    std::mutex reconnectMutex_;                ///< 保护重连信号的互斥锁
    bool shouldReconnect_;                     ///< 是否需要重连
    bool threadRunning_;                       ///< 重连线程是否运行
    
    // 依赖对象（缓存引用，避免重复获取）
    core::Logger& logger_;  ///< Logger引用（构造时获取一次）
    core::Config& config_;  ///< Config引用（构造时获取一次）
    
    // ⭐ DJI Cloud API 相关配置
    std::string gatewaySn_;     ///< 机场网关序列号（用于构建MQTT Topic）
    std::string analysisSn_;    ///< 分析设备序列号
    
    // ⭐ 异步重连方法
    void reconnectThreadFunc();  ///< 重连线程函数
    void startReconnectThread(); ///< 启动重连线程
    void stopReconnectThread();  ///< 停止重连线程
    
    // ⭐ DJI Cloud API 辅助方法
    /**
     * @brief 生成 UUID v4
     * 
     * 用于 MQTT 消息的 bid 和 tid 字段。
     * 
     * @return UUID 字符串，格式：xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
     */
    std::string generateUUID();
    
    /**
     * @brief 获取当前时间戳（毫秒）
     * 
     * @return 自 Unix 纪元以来的毫秒数
     */
    int64_t getCurrentTimestampMs();
    
    /**
     * @brief 发送 DJI Cloud API 服务命令（到 services topic）
     * 
     * @param method 方法名（如 "flighttask_pause"）
     * @param data 数据对象
     * @return true 发送成功
     */
    bool sendServiceCommand(const std::string& method, const nlohmann::json& data);
    
    /**
     * @brief 发送 DJI Cloud API DRC 命令（到 drc/down topic）
     * 
     * @param method 方法名（如 "camera_photo_take"）
     * @param data 数据对象
     * @return true 发送成功
     */
    bool sendDrcCommand(const std::string& method, const nlohmann::json& data);
};

// ==================== Impl 构造/析构 ====================

MqttClient::Impl::Impl()
    : pahoClient_(nullptr),
      brokerHost_(DEFAULT_BROKER_HOST),
      brokerPort_(DEFAULT_BROKER_PORT),
      clientId_(DEFAULT_CLIENT_ID),
      defaultQos_(DEFAULT_QOS),
      keepAliveInterval_(DEFAULT_KEEP_ALIVE),
      timeoutMs_(DEFAULT_TIMEOUT_MS),
      reconnectEnabled_(true),
      maxRetries_(DEFAULT_MAX_RETRIES),
      retryIntervalMs_(DEFAULT_RETRY_INTERVAL_MS),
      currentRetries_(0),
      initialized_(false),
      connected_(false),
      reconnecting_(false),  // 初始化重连标志为false
      shouldReconnect_(false),  // ⭐ 初始化重连信号为false
      threadRunning_(false),    // ⭐ 线程未运行
      logger_(core::Logger::getInstance()),  // 缓存Logger引用
      config_(core::Config::getInstance()) {  // 缓存Config引用
    
    logger_.debug("MqttClient::Impl 构造函数");
    
    // ⭐ 启动重连线程
    startReconnectThread();
}

MqttClient::Impl::~Impl() {
    logger_.debug("MqttClient::Impl 析构函数");
    
    // ⭐ 先停止重连线程
    stopReconnectThread();
    
    // 断开连接并清理资源
    disconnect();
    
    if (pahoClient_ != nullptr) {
        MQTTClient_destroy(&pahoClient_);
        pahoClient_ = nullptr;
    }
}

// ==================== Impl 公共接口实现 ====================

bool MqttClient::Impl::initialize() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // 如果已经初始化，先清理
    if (initialized_) {
        logger_.info("MqttClient已初始化，重新初始化");
        if (pahoClient_ != nullptr) {
            MQTTClient_destroy(&pahoClient_);
            pahoClient_ = nullptr;
        }
    }
    
    // 从Config加载配置
    loadConfig();
    
    // 构建服务器URI
    std::string serverUri = "tcp://" + brokerHost_ + ":" + std::to_string(brokerPort_);
    
    logger_.info("初始化MQTT客户端: " + serverUri + ", ClientID: " + clientId_);
    
    // 创建paho客户端
    int rc = MQTTClient_create(&pahoClient_, serverUri.c_str(), clientId_.c_str(),
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("创建MQTT客户端失败, 错误码: " + std::to_string(rc));
        return false;
    }
    
    // 设置回调函数（传递this指针作为context）
    rc = MQTTClient_setCallbacks(pahoClient_, this,
                                 onConnectionLostCallback,
                                 onMessageArrivedCallback,
                                 onDeliveryCompleteCallback);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("设置MQTT回调失败, 错误码: " + std::to_string(rc));
        MQTTClient_destroy(&pahoClient_);
        pahoClient_ = nullptr;
        return false;
    }
    
    initialized_ = true;
    logger_.info("MQTT客户端初始化成功");
    
    return true;
}

bool MqttClient::Impl::connect() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    if (!initialized_) {
        logger_.error("MQTT客户端未初始化，无法连接");
        return false;
    }
    
    if (connected_) {
        logger_.warning("MQTT客户端已连接，先断开再重新连接");
        // 注意：这里不能调用disconnect()，因为已经持有锁
        MQTTClient_disconnect(pahoClient_, timeoutMs_);
        connected_ = false;
    }
    
    // 配置连接选项
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = keepAliveInterval_;
    conn_opts.cleansession = 1;  // 清除会话
    conn_opts.connectTimeout = timeoutMs_ / 1000;  // 转换为秒
    
    // 设置用户名密码（如果有）
    if (!username_.empty()) {
        conn_opts.username = username_.c_str();
        conn_opts.password = password_.c_str();
    }
    
    logger_.info("正在连接到MQTT代理...");
    
    // 连接到代理
    int rc = MQTTClient_connect(pahoClient_, &conn_opts);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("连接MQTT代理失败, 错误码: " + std::to_string(rc));
        
        // 如果启用了自动重连，尝试重连
        if (reconnectEnabled_ && currentRetries_ < maxRetries_) {
            logger_.info("将在 " + std::to_string(retryIntervalMs_) + "ms 后重试");
            // 注意：实际重连在后台线程中进行，这里只是记录日志
        }
        
        return false;
    }
    
    connected_ = true;
    
    std::string serverUri = "tcp://" + brokerHost_ + ":" + std::to_string(brokerPort_);
    logger_.info("成功连接到MQTT代理: " + serverUri);
    
    // ⭐ 重连时自动重新订阅所有主题
    // 判断是否为重连：如果之前有重试，则为重连
    bool isReconnect = (currentRetries_ > 0);
    currentRetries_ = 0;  // 重置重试计数
    
    if (isReconnect) {
        // 这是重连，需要重新订阅
        logger_.info("🔄 检测到重连，重新订阅所有主题...");
        resubscribeAll();
    }
    
    // 通知观察者连接成功
    notifyConnected(serverUri);
    
    return true;
}

void MqttClient::Impl::disconnect() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    if (!connected_ || pahoClient_ == nullptr) {
        logger_.debug("MQTT客户端未连接，无需断开");
        return;
    }
    
    logger_.info("正在断开MQTT连接...");
    
    // ⚠️ 关键修复：在调用paho的disconnect之前，先设置connected_=false
    // 这样如果paho触发onConnectionLostCallback，回调会立即返回，不会执行重连逻辑
    connected_ = false;
    
    // 断开连接（等待消息发送完毕）
    // 注意：这个调用可能会触发onConnectionLostCallback，但由于connected_已经是false，回调会直接返回
    int rc = MQTTClient_disconnect(pahoClient_, timeoutMs_);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.warning("断开MQTT连接失败, 错误码: " + std::to_string(rc));
    } else {
        logger_.info("MQTT连接已断开");
    }
}

bool MqttClient::Impl::isConnected() const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return connected_ && pahoClient_ != nullptr && 
           MQTTClient_isConnected(pahoClient_);
}

bool MqttClient::Impl::publish(const std::string& topic, const std::string& message,
                               int qos, bool retained) {
    if (!isConnected()) {
        logger_.error("MQTT未连接，无法发布消息到主题: " + topic);
        return false;
    }
    
    // 使用默认QoS（如果qos=-1）
    int actualQos = (qos == -1) ? defaultQos_ : qos;
    
    // 配置消息
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = const_cast<char*>(message.c_str());
    pubmsg.payloadlen = static_cast<int>(message.length());
    pubmsg.qos = actualQos;
    pubmsg.retained = retained ? 1 : 0;
    
    MQTTClient_deliveryToken token;
    
    logger_.debug("发布消息到主题 [" + topic + "], QoS=" + std::to_string(actualQos) +
                   ", 长度=" + std::to_string(message.length()));
    
    // 发布消息
    int rc = MQTTClient_publishMessage(pahoClient_, topic.c_str(), &pubmsg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("发布消息失败, 主题: " + topic + ", 错误码: " + std::to_string(rc));
        return false;
    }
    
    // QoS > 0 时等待确认
    if (actualQos > 0) {
        rc = MQTTClient_waitForCompletion(pahoClient_, token, timeoutMs_);
        if (rc != MQTTCLIENT_SUCCESS) {
            logger_.warning("等待消息确认超时, token: " + std::to_string(token));
        }
    }
    
    logger_.debug("消息发布成功, 主题: " + topic);
    
    return true;
}

bool MqttClient::Impl::subscribe(const std::string& topic, int qos) {
    if (!isConnected()) {
        logger_.error("MQTT未连接，无法订阅主题: " + topic);
        return false;
    }
    
    // 使用默认QoS（如果qos=-1）
    int actualQos = (qos == -1) ? defaultQos_ : qos;
    
    logger_.info("订阅主题: " + topic + ", QoS=" + std::to_string(actualQos));
    
    // 订阅主题
    int rc = MQTTClient_subscribe(pahoClient_, topic.c_str(), actualQos);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("订阅主题失败: " + topic + ", 错误码: " + std::to_string(rc));
        return false;
    }
    
    logger_.info("成功订阅主题: " + topic);
    
    // ⭐ 保存订阅信息，用于重连后自动重新订阅
    {
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        
        // 检查是否已存在
        auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                              [&topic](const SubscriptionInfo& info) {
                                  return info.topic == topic;
                              });
        
        if (it != subscriptions_.end()) {
            // 更新 QoS
            it->qos = actualQos;
            logger_.debug("更新订阅信息: " + topic);
        } else {
            // 新增订阅
            subscriptions_.push_back({topic, actualQos});
            logger_.debug("保存订阅信息: " + topic + ", 当前订阅数: " + 
                         std::to_string(subscriptions_.size()));
        }
    }
    
    return true;
}

bool MqttClient::Impl::unsubscribe(const std::string& topic) {
    if (!isConnected()) {
        logger_.error("MQTT未连接，无法取消订阅主题: " + topic);
        return false;
    }
    
    logger_.info("取消订阅主题: " + topic);
    
    // 取消订阅
    int rc = MQTTClient_unsubscribe(pahoClient_, topic.c_str());
    
    if (rc != MQTTCLIENT_SUCCESS) {
        logger_.error("取消订阅失败: " + topic + ", 错误码: " + std::to_string(rc));
        return false;
    }
    
    logger_.info("成功取消订阅: " + topic);
    
    // ⭐ 移除订阅信息
    {
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        
        auto it = std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                                [&topic](const SubscriptionInfo& info) {
                                    return info.topic == topic;
                                });
        
        if (it != subscriptions_.end()) {
            subscriptions_.erase(it, subscriptions_.end());
            logger_.debug("移除订阅信息: " + topic + ", 剩余订阅数: " + 
                         std::to_string(subscriptions_.size()));
        }
    }
    
    return true;
}

void MqttClient::Impl::addObserver(IMqttMessageObserver* observer) {
    if (observer == nullptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(observersMutex_);
    
    // 检查是否已存在
    auto it = std::find(observers_.begin(), observers_.end(), observer);
    if (it != observers_.end()) {
        return;  // 已存在，不重复添加
    }
    
    observers_.push_back(observer);
    
    logger_.debug("添加MQTT观察者，当前观察者数量: " + std::to_string(observers_.size()));
}

void MqttClient::Impl::removeObserver(IMqttMessageObserver* observer) {
    if (observer == nullptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(observersMutex_);
    
    auto it = std::find(observers_.begin(), observers_.end(), observer);
    if (it != observers_.end()) {
        observers_.erase(it);
        logger_.debug("移除MQTT观察者，当前观察者数量: " + std::to_string(observers_.size()));
    }
}

// ==================== Impl paho回调实现 ====================

void MqttClient::Impl::onConnectionLostCallback(void* context, char* cause) {
    // context就是this指针
    auto* self = static_cast<Impl*>(context);
    
    // ⚠️ 关键修复：防止在对象析构时执行回调
    // 检查 connected_ 标志，如果已经断开（disconnect被调用），直接返回
    if (!self->connected_) {
        // 不打印日志，因为对象可能正在析构，访问logger_可能不安全
        return;
    }
    
    std::string causeStr = (cause != nullptr) ? std::string(cause) : "Unknown";
    
    self->logger_.warning("💔 onConnectionLostCallback 被paho调用");
    self->logger_.warning("MQTT连接丢失: " + causeStr);
    
    // ⭐ 异步重连：设置 connected_ = false
    {
        std::lock_guard<std::mutex> lock(self->connectionMutex_);
        self->connected_ = false;
    }
    
    // ⭐ 通知观察者（快速执行）
    self->notifyConnectionLost(causeStr);
    
    // ⭐ 触发异步重连（不在回调中执行重连）
    if (self->reconnectEnabled_) {
        std::lock_guard<std::mutex> lock(self->reconnectMutex_);
        self->shouldReconnect_ = true;  // 设置重连信号
        self->reconnectSignal_.notify_one();  // 唤醒重连线程
        self->logger_.info("💔 ✅ 已触发异步重连，回调即将返回");
    }
    
    self->logger_.debug("💔 onConnectionLostCallback 快速返回");
}

int MqttClient::Impl::onMessageArrivedCallback(void* context, char* topicName,
                                               int topicLen, MQTTClient_message* message) {
    // context就是this指针
    auto* self = static_cast<Impl*>(context);
    
    // 提取主题（可能没有null结尾）
    std::string topic;
    if (topicLen > 0) {
        topic = std::string(topicName, topicLen);
    } else {
        topic = std::string(topicName);
    }
    
    // 提取消息内容
    std::string msg;
    if (message != nullptr && message->payload != nullptr) {
        msg = std::string(static_cast<char*>(message->payload), message->payloadlen);
    }
    
    self->logger_.debug("收到MQTT消息, 主题: " + topic + ", 长度: " + std::to_string(msg.length()));
    
    // 通知观察者
    self->notifyMessageReceived(topic, msg);
    
    // 释放paho消息内存
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    
    return 1;  // 返回1表示成功处理
}

void MqttClient::Impl::onDeliveryCompleteCallback(void* context, 
                                                  MQTTClient_deliveryToken token) {
    // context就是this指针
    auto* self = static_cast<Impl*>(context);
    
    self->logger_.debug("MQTT消息发送完成, token: " + std::to_string(token));
    
    // 通知观察者
    self->notifyDeliveryComplete(static_cast<int>(token));
}

// ==================== Impl 内部方法实现 ====================

void MqttClient::Impl::notifyConnectionLost(const std::string& cause) {
    std::lock_guard<std::mutex> lock(observersMutex_);
    
    logger_.debug("🔔 notifyConnectionLost 开始，观察者数量: " + std::to_string(observers_.size()));
    
    // 拷贝观察者列表，避免在回调中修改列表导致迭代器失效
    std::vector<IMqttMessageObserver*> observersCopy = observers_;
    
    logger_.debug("✅ 观察者列表已拷贝，开始通知...");
    
    int index = 0;
    for (auto* observer : observersCopy) {
        logger_.debug("📍 通知观察者 #" + std::to_string(index) + " 地址=" + 
                     std::to_string(reinterpret_cast<uintptr_t>(observer)));
        
        if (observer == nullptr) {
            logger_.warning("⚠️ 观察者 #" + std::to_string(index) + " 是空指针，跳过");
            index++;
            continue;
        }
        
        try {
            logger_.debug("➡️ 调用观察者 #" + std::to_string(index) + "->onConnectionLost()");
            observer->onConnectionLost(cause);
            logger_.debug("✅ 观察者 #" + std::to_string(index) + " 处理完成");
        } catch (const std::exception& e) {
            logger_.error("❌ 观察者 #" + std::to_string(index) + " 处理连接丢失异常: " + std::string(e.what()));
        } catch (...) {
            logger_.error("💥 观察者 #" + std::to_string(index) + " 处理连接丢失时发生未知异常");
        }
        
        index++;
    }
    
    logger_.debug("🏁 notifyConnectionLost 完成");
}

void MqttClient::Impl::notifyMessageReceived(const std::string& topic, 
                                             const std::string& message) {
    std::lock_guard<std::mutex> lock(observersMutex_);
    
    logger_.debug("🔔 notifyMessageReceived 开始，观察者数量: " + std::to_string(observers_.size()));
    
    // 拷贝观察者列表，避免在回调中修改列表导致迭代器失效
    std::vector<IMqttMessageObserver*> observersCopy = observers_;
    
    logger_.debug("✅ 观察者列表已拷贝，开始通知...");
    
    int index = 0;
    for (auto* observer : observersCopy) {
        logger_.debug("📍 通知观察者 #" + std::to_string(index) + " 地址=" + 
                     std::to_string(reinterpret_cast<uintptr_t>(observer)));
        
        if (observer == nullptr) {
            logger_.warning("⚠️ 观察者 #" + std::to_string(index) + " 是空指针，跳过");
            index++;
            continue;
        }
        
        try {
            logger_.debug("➡️ 调用观察者 #" + std::to_string(index) + "->onMessageReceived()");
            observer->onMessageReceived(topic, message);
            logger_.debug("✅ 观察者 #" + std::to_string(index) + " 处理完成");
        } catch (const std::exception& e) {
            logger_.error("❌ 观察者 #" + std::to_string(index) + " 处理消息异常: " + std::string(e.what()));
        } catch (...) {
            logger_.error("💥 观察者 #" + std::to_string(index) + " 处理消息时发生未知异常");
        }
        
        index++;
    }
    
    logger_.debug("🏁 notifyMessageReceived 完成");
}

void MqttClient::Impl::notifyConnected(const std::string& serverUri) {
    std::lock_guard<std::mutex> lock(observersMutex_);
    
    logger_.debug("🔔 notifyConnected 开始，观察者数量: " + std::to_string(observers_.size()));
    logger_.info("🎯 准备通知观察者: MQTT连接成功");
    
    // 拷贝观察者列表，避免在回调中修改列表导致迭代器失效
    std::vector<IMqttMessageObserver*> observersCopy = observers_;
    
    logger_.debug("✅ 观察者列表已拷贝，开始通知...");
    
    int index = 0;
    for (auto* observer : observersCopy) {
        logger_.info("📍 通知观察者 #" + std::to_string(index) + " 地址=" + 
                     std::to_string(reinterpret_cast<uintptr_t>(observer)));
        
        if (observer == nullptr) {
            logger_.warning("⚠️ 观察者 #" + std::to_string(index) + " 是空指针，跳过");
            index++;
            continue;
        }
        
        try {
            logger_.info("➡️ 即将调用观察者 #" + std::to_string(index) + "->onConnected()");
            observer->onConnected(serverUri);
            logger_.info("✅ 观察者 #" + std::to_string(index) + " 处理完成");
        } catch (const std::exception& e) {
            logger_.error("❌ 观察者 #" + std::to_string(index) + " 处理连接成功异常: " + std::string(e.what()));
        } catch (...) {
            logger_.error("💥 观察者 #" + std::to_string(index) + " 处理连接成功时发生未知异常");
        }
        
        index++;
    }
    
    logger_.info("🏁 notifyConnected 完成，已通知 " + std::to_string(index) + " 个观察者");
}

void MqttClient::Impl::notifyDeliveryComplete(int token) {
    std::lock_guard<std::mutex> lock(observersMutex_);
    
    logger_.debug("🔔 notifyDeliveryComplete 开始，观察者数量: " + std::to_string(observers_.size()));
    
    // 拷贝观察者列表，避免在回调中修改列表导致迭代器失效
    std::vector<IMqttMessageObserver*> observersCopy = observers_;
    
    logger_.debug("✅ 观察者列表已拷贝，开始通知...");
    
    int index = 0;
    for (auto* observer : observersCopy) {
        logger_.debug("📍 通知观察者 #" + std::to_string(index) + " 地址=" + 
                     std::to_string(reinterpret_cast<uintptr_t>(observer)));
        
        if (observer == nullptr) {
            logger_.warning("⚠️ 观察者 #" + std::to_string(index) + " 是空指针，跳过");
            index++;
            continue;
        }
        
        try {
            logger_.debug("➡️ 调用观察者 #" + std::to_string(index) + "->onDeliveryComplete()");
            observer->onDeliveryComplete(token);
            logger_.debug("✅ 观察者 #" + std::to_string(index) + " 处理完成");
        } catch (const std::exception& e) {
            logger_.error("❌ 观察者 #" + std::to_string(index) + " 处理消息发送成功异常: " + std::string(e.what()));
        } catch (...) {
            logger_.error("💥 观察者 #" + std::to_string(index) + " 处理消息发送成功时发生未知异常");
        }
        
        index++;
    }
    
    logger_.debug("🏁 notifyDeliveryComplete 完成");
}

void MqttClient::Impl::resubscribeAll() {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    
    if (subscriptions_.empty()) {
        logger_.debug("没有需要重新订阅的主题");
        return;
    }
    
    logger_.info("🔄 重新订阅所有主题，数量: " + std::to_string(subscriptions_.size()));
    
    int successCount = 0;
    int failCount = 0;
    
    for (const auto& sub : subscriptions_) {
        logger_.debug("重新订阅主题: " + sub.topic + ", QoS=" + std::to_string(sub.qos));
        
        int rc = MQTTClient_subscribe(pahoClient_, sub.topic.c_str(), sub.qos);
        
        if (rc == MQTTCLIENT_SUCCESS) {
            successCount++;
            logger_.info("✅ 重新订阅成功: " + sub.topic);
        } else {
            failCount++;
            logger_.error("❌ 重新订阅失败: " + sub.topic + ", 错误码: " + std::to_string(rc));
        }
    }
    
    logger_.info("🏁 重新订阅完成: 成功=" + std::to_string(successCount) + 
                 ", 失败=" + std::to_string(failCount));
}

void MqttClient::Impl::attemptReconnect() {
    logger_.debug("🔄 attemptReconnect() 开始");
    
    if (currentRetries_ >= maxRetries_) {
        logger_.error("MQTT重连次数已达上限 (" + std::to_string(maxRetries_) + 
                       "), 停止重连");
        return;
    }
    
    currentRetries_++;
    logger_.debug("🔄 currentRetries_ 已增加到 " + std::to_string(currentRetries_));
    
    logger_.info("尝试第 " + std::to_string(currentRetries_) + "/" + 
                  std::to_string(maxRetries_) + " 次重连...");
    
    // 等待一段时间后重连
    logger_.debug("🔄 等待 " + std::to_string(retryIntervalMs_) + "ms...");
    std::this_thread::sleep_for(std::chrono::milliseconds(retryIntervalMs_));
    
    // 尝试重连
    logger_.debug("🔄 调用 connect()...");
    bool connectResult = connect();
    logger_.debug("🔄 connect() 返回: " + std::string(connectResult ? "true" : "false"));
    
    if (connectResult) {
        logger_.info("MQTT重连成功");
        logger_.debug("🔄 准备重置 currentRetries_...");
        currentRetries_ = 0;
        logger_.debug("🔄 currentRetries_ 已重置为 0");
    } else {
        logger_.warning("MQTT重连失败");
        // 递归尝试下一次重连
        if (reconnectEnabled_ && currentRetries_ < maxRetries_) {
            logger_.debug("🔄 准备递归调用 attemptReconnect()...");
            attemptReconnect();
        }
    }
    
    logger_.debug("🔄 attemptReconnect() 结束");
}

// ⭐ 异步重连线程函数
void MqttClient::Impl::reconnectThreadFunc() {
    logger_.info("🧵 重连线程启动");
    
    while (threadRunning_) {
        std::unique_lock<std::mutex> lock(reconnectMutex_);
        
        // 等待重连信号（或超时退出）
        reconnectSignal_.wait_for(lock, std::chrono::seconds(1), [this] {
            return shouldReconnect_ || !threadRunning_;
        });
        
        // 检查是否应该退出
        if (!threadRunning_) {
            logger_.info("🧵 重连线程收到退出信号");
            break;
        }
        
        // 检查是否需要重连
        if (shouldReconnect_) {
            shouldReconnect_ = false;  // 重置信号
            lock.unlock();  // 释放锁，允许其他线程设置信号
            
            logger_.info("🧵 重连线程开始执行重连...");
            
            // 执行重连（在后台线程中，不会阻塞paho回调）
            attemptReconnect();
            
            logger_.info("🧵 重连线程完成一次重连尝试");
        }
    }
    
    logger_.info("🧵 重连线程退出");
}

// ⭐ 启动重连线程
void MqttClient::Impl::startReconnectThread() {
    if (threadRunning_) {
        logger_.warning("重连线程已在运行");
        return;
    }
    
    threadRunning_ = true;
    reconnectThread_ = std::thread(&Impl::reconnectThreadFunc, this);
    
    logger_.info("✅ 重连线程已启动");
}

// ⭐ 停止重连线程
void MqttClient::Impl::stopReconnectThread() {
    if (!threadRunning_) {
        return;
    }
    
    logger_.info("正在停止重连线程...");
    
    {
        std::lock_guard<std::mutex> lock(reconnectMutex_);
        threadRunning_ = false;
        reconnectSignal_.notify_one();  // 唤醒线程让它退出
    }
    
    if (reconnectThread_.joinable()) {
        reconnectThread_.join();  // 等待线程退出
    }
    
    logger_.info("✅ 重连线程已停止");
}

void MqttClient::Impl::loadConfig() {
    try {
        // 读取代理配置
        // 配置文件格式: "mqtt.broker": "tcp://host:port"
        std::string broker = config_.getString("mqtt.broker", "");
        if (!broker.empty()) {
            // 解析broker字符串，格式: "tcp://host:port" 或 "host:port"
            std::string hostPort = broker;
            
            // 移除协议前缀（tcp:// 或 ssl://）
            size_t protocolPos = hostPort.find("://");
            if (protocolPos != std::string::npos) {
                hostPort = hostPort.substr(protocolPos + 3);
            }
            
            // 分割host和port
            size_t colonPos = hostPort.find(':');
            if (colonPos != std::string::npos) {
                brokerHost_ = hostPort.substr(0, colonPos);
                try {
                    brokerPort_ = std::stoi(hostPort.substr(colonPos + 1));
                } catch (...) {
                    logger_.warning("解析MQTT端口失败，使用默认值: " + 
                                   std::to_string(DEFAULT_BROKER_PORT));
                    brokerPort_ = DEFAULT_BROKER_PORT;
                }
            } else {
                brokerHost_ = hostPort;
                brokerPort_ = DEFAULT_BROKER_PORT;
            }
        } else {
            brokerHost_ = DEFAULT_BROKER_HOST;
            brokerPort_ = DEFAULT_BROKER_PORT;
        }
        
        // 读取用户名和密码
        username_ = config_.getString("mqtt.username", "");
        password_ = config_.getString("mqtt.password", "");
        
        // 读取client_id
        clientId_ = config_.getString("mqtt.client_id", DEFAULT_CLIENT_ID);
        
        // 读取通信配置
        defaultQos_ = config_.getInt("mqtt.qos", DEFAULT_QOS);
        keepAliveInterval_ = config_.getInt("mqtt.keep_alive", DEFAULT_KEEP_ALIVE);
        
        // 读取重连配置
        reconnectEnabled_ = config_.getBool("mqtt.reconnect.enabled", true);
        maxRetries_ = config_.getInt("mqtt.reconnect.max_retries", DEFAULT_MAX_RETRIES);
        retryIntervalMs_ = config_.getInt("mqtt.reconnect.retry_interval_ms", 
                                        DEFAULT_RETRY_INTERVAL_MS);
        
        // ⭐ 读取DJI Cloud API相关配置（设备序列号）
        gatewaySn_ = config_.getString("device.gateway_sn", "");
        analysisSn_ = config_.getString("device.analysis_sn", "analysis_device_WRSE7001");
        
        if (gatewaySn_.empty()) {
            logger_.warning("未配置 device.gateway_sn，DJI Cloud API 功能可能无法正常工作");
        } else {
            logger_.info("DJI Gateway SN: " + gatewaySn_);
        }
        
        logger_.info("MQTT配置加载成功: broker=" + brokerHost_ + ":" + 
                      std::to_string(brokerPort_) + ", clientId=" + clientId_);
        
    } catch (const std::exception& e) {
        // 降级方案：使用硬编码默认值
        logger_.warning("MQTT配置加载失败，使用默认配置: " + std::string(e.what()));
        
        brokerHost_ = DEFAULT_BROKER_HOST;
        brokerPort_ = DEFAULT_BROKER_PORT;
        clientId_ = DEFAULT_CLIENT_ID;
        defaultQos_ = DEFAULT_QOS;
        keepAliveInterval_ = DEFAULT_KEEP_ALIVE;
        reconnectEnabled_ = true;
        maxRetries_ = DEFAULT_MAX_RETRIES;
        retryIntervalMs_ = DEFAULT_RETRY_INTERVAL_MS;
    }
}

// ==================== MqttClient 公共接口实现 ====================

MqttClient& MqttClient::getInstance() {
    static MqttClient instance;
    return instance;
}

MqttClient::MqttClient() : pImpl_(std::make_unique<Impl>()) {
}

MqttClient::~MqttClient() = default;

bool MqttClient::initialize() {
    return pImpl_->initialize();
}

bool MqttClient::connect() {
    return pImpl_->connect();
}

void MqttClient::disconnect() {
    pImpl_->disconnect();
}

bool MqttClient::isConnected() const {
    return pImpl_->isConnected();
}

bool MqttClient::publish(const std::string& topic, const std::string& message,
                        int qos, bool retained) {
    return pImpl_->publish(topic, message, qos, retained);
}

bool MqttClient::subscribe(const std::string& topic, int qos) {
    return pImpl_->subscribe(topic, qos);
}

bool MqttClient::unsubscribe(const std::string& topic) {
    return pImpl_->unsubscribe(topic);
}

void MqttClient::addObserver(IMqttMessageObserver* observer) {
    pImpl_->addObserver(observer);
}

void MqttClient::removeObserver(IMqttMessageObserver* observer) {
    pImpl_->removeObserver(observer);
}

// ==================== DJI Cloud API 公共接口实现 ====================

bool MqttClient::pauseWayline() {
    return pImpl_->pauseWayline();
}

bool MqttClient::resumeWayline() {
    return pImpl_->resumeWayline();
}

bool MqttClient::frameZoom(const std::string& payloadIndex,
                           float x1, float y1, float x2, float y2,
                           const std::string& cameraType,
                           const std::string& lockMode) {
    return pImpl_->frameZoom(payloadIndex, x1, y1, x2, y2, cameraType, lockMode);
}

bool MqttClient::setFocalLength(const std::string& payloadIndex, int focalLength) {
    return pImpl_->setFocalLength(payloadIndex, focalLength);
}

bool MqttClient::takePhoto(const std::string& payloadIndex) {
    return pImpl_->takePhoto(payloadIndex);
}

bool MqttClient::resetGimbal(const std::string& payloadIndex, int resetMode) {
    return pImpl_->resetGimbal(payloadIndex, resetMode);
}

// ==================== Impl DJI Cloud API 实现 ====================

/**
 * @brief 生成 UUID v4
 * 
 * 用于 MQTT 消息的 bid（业务ID）和 tid（事务ID）字段。
 * 
 * 实现原理：
 * - UUID v4 是基于随机数生成的 128 位标识符
 * - 格式：xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 * - 其中 4 表示版本号，y 的高两位固定为 10（variant）
 * 
 * 面试要点：
 * - thread_local：线程本地存储，避免多线程竞争
 * - std::random_device：硬件随机数生成器
 * - std::mt19937_64：64位梅森旋转算法
 */
std::string MqttClient::Impl::generateUUID() {
    // 使用 thread_local 确保线程安全，每个线程有自己的随机数生成器
    thread_local std::random_device rd;
    thread_local std::mt19937_64 gen(rd());
    thread_local std::uniform_int_distribution<uint64_t> dis;
    
    // 生成 128 bits 随机数（两个 64 位数）
    uint64_t high = dis(gen);
    uint64_t low = dis(gen);
    
    // 设置版本号 (v4) 和 variant 位
    // version = 4: 第 13-16 位设为 0100
    high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // variant = 10b: 第 65-66 位设为 10
    low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    
    // 格式化为标准 UUID 字符串
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    oss << std::setw(8) << (high >> 32);               // 8 hex
    oss << '-';
    oss << std::setw(4) << ((high >> 16) & 0xFFFF);    // 4 hex
    oss << '-';
    oss << std::setw(4) << (high & 0xFFFF);            // 4 hex (含版本号)
    oss << '-';
    oss << std::setw(4) << (low >> 48);                // 4 hex (含变体位)
    oss << '-';
    oss << std::setw(12) << (low & 0xFFFFFFFFFFFFULL); // 12 hex
    
    return oss.str();
}

/**
 * @brief 获取当前时间戳（毫秒）
 * 
 * 用于 MQTT 消息的 timestamp 字段。
 * 
 * @return 自 Unix 纪元（1970-01-01 00:00:00 UTC）以来的毫秒数
 */
int64_t MqttClient::Impl::getCurrentTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

/**
 * @brief 发送 DJI Cloud API 服务命令（到 services topic）
 * 
 * Topic: thing/product/{gateway_sn}/services
 * 
 * 消息格式：
 * {
 *     "bid": "uuid",
 *     "tid": "uuid", 
 *     "timestamp": 1234567890123,
 *     "method": "flighttask_pause",
 *     "data": {}
 * }
 */
bool MqttClient::Impl::sendServiceCommand(const std::string& method, 
                                          const nlohmann::json& data) {
    if (gatewaySn_.empty()) {
        logger_.error("发送服务命令失败: 未配置 gateway_sn");
        return false;
    }
    
    // 构建 Topic
    std::string topic = "thing/product/" + gatewaySn_ + "/services";
    
    // 构建消息体
    nlohmann::json message;
    message["bid"] = generateUUID();
    message["tid"] = generateUUID();
    message["timestamp"] = getCurrentTimestampMs();
    message["method"] = method;
    message["data"] = data;
    
    std::string messageStr = message.dump();
    
    logger_.debug("📤 发送服务命令: " + method + " -> " + topic);
    logger_.debug("消息内容: " + messageStr);
    
    return publish(topic, messageStr, 1, false);
}

/**
 * @brief 发送 DJI Cloud API DRC 命令（到 drc/down topic）
 * 
 * DRC（Device Remote Control）用于实时控制命令，如相机控制。
 * 
 * Topic: thing/product/{gateway_sn}/drc/down
 * 
 * 消息格式：
 * {
 *     "bid": "uuid",
 *     "tid": "uuid",
 *     "timestamp": 1234567890123,
 *     "method": "camera_photo_take",
 *     "data": { "payload_index": "81-0-0" }
 * }
 */
bool MqttClient::Impl::sendDrcCommand(const std::string& method,
                                       const nlohmann::json& data) {
    if (gatewaySn_.empty()) {
        logger_.error("发送DRC命令失败: 未配置 gateway_sn");
        return false;
    }
    
    // 构建 Topic
    std::string topic = "thing/product/" + gatewaySn_ + "/drc/down";
    
    // 构建消息体
    nlohmann::json message;
    message["bid"] = generateUUID();
    message["tid"] = generateUUID();
    message["timestamp"] = getCurrentTimestampMs();
    message["method"] = method;
    message["data"] = data;
    
    std::string messageStr = message.dump();
    
    logger_.debug("📤 发送DRC命令: " + method + " -> " + topic);
    logger_.debug("消息内容: " + messageStr);
    
    return publish(topic, messageStr, 1, false);
}

/**
 * @brief 暂停航线任务
 * 
 * 调用 DJI Cloud API 的 flighttask_pause 方法，
 * 使无人机暂停当前航线任务并悬停在原地。
 * 
 * 使用场景：
 * - 检测到目标后，需要悬停进行变焦拍照
 * - 需要临时中断航线执行其他操作
 */
bool MqttClient::Impl::pauseWayline() {
    logger_.info("🛑 暂停航线任务...");
    
    nlohmann::json data = nlohmann::json::object();  // 空对象
    return sendServiceCommand("flighttask_pause", data);
}

/**
 * @brief 恢复航线任务
 * 
 * 调用 DJI Cloud API 的 flighttask_recovery 方法，
 * 恢复被暂停的航线任务。
 * 
 * 使用场景：
 * - 变焦拍照完成后，恢复航线继续执行
 */
bool MqttClient::Impl::resumeWayline() {
    logger_.info("▶️ 恢复航线任务...");
    
    nlohmann::json data = nlohmann::json::object();
    return sendServiceCommand("flighttask_recovery", data);
}

/**
 * @brief 框选变焦
 * 
 * 调用 DJI Cloud API 的 camera_frame_zoom 方法，
 * 对指定矩形区域进行变焦放大。
 * 
 * 📌 重要：根据 DJI Cloud API 文档，参数格式为：
 * - x, y: 目标框左上角坐标（归一化 0.0-1.0）
 * - width, height: 目标框宽度和高度（归一化 0.0-1.0）
 * - locked: 是否锁定机头（true=云台和机身一起转）
 * 
 * @param payloadIndex 相机 payload 索引，如 "81-0-0"
 * @param x1, y1 左上角坐标（归一化 0.0-1.0）
 * @param x2, y2 右下角坐标（归一化 0.0-1.0）
 * @param cameraType 相机类型: "wide"=广角, "zoom"=变焦, "ir"=红外
 * @param lockMode 锁定模式（已废弃，改用 locked 参数）
 * 
 * 示例：对画面中央 40% 区域进行变焦
 * frameZoom("81-0-0", 0.3, 0.3, 0.7, 0.7)
 */
bool MqttClient::Impl::frameZoom(const std::string& payloadIndex,
                                  float x1, float y1, float x2, float y2,
                                  const std::string& cameraType,
                                  const std::string& lockMode) {
    // 计算宽度和高度
    float width = x2 - x1;
    float height = y2 - y1;
    
    logger_.info("🔍 框选变焦: payload=" + payloadIndex + 
                 ", 区域: x=" + std::to_string(x1) + ", y=" + std::to_string(y1) +
                 ", w=" + std::to_string(width) + ", h=" + std::to_string(height));
    
    // 参数验证
    if (x1 < 0 || x1 > 1 || y1 < 0 || y1 > 1 ||
        x2 < 0 || x2 > 1 || y2 < 0 || y2 > 1) {
        logger_.error("frameZoom 参数错误: 坐标必须在 0.0-1.0 范围内");
        return false;
    }
    
    if (x1 >= x2 || y1 >= y2) {
        logger_.error("frameZoom 参数错误: 左上角坐标必须小于右下角坐标");
        return false;
    }
    
    // 构建符合 DJI Cloud API 规范的参数
    // 参考: https://developer.dji.com/doc/cloud-api-tutorial/cn/api-reference/dock-to-cloud/mqtt/dock/dock2/drc.html
    nlohmann::json data;
    data["payload_index"] = payloadIndex;
    data["camera_type"] = cameraType;
    data["locked"] = true;    // 锁定机头，云台和机身一起转
    data["x"] = x1;           // 左上角 x
    data["y"] = y1;           // 左上角 y
    data["width"] = width;    // 宽度
    data["height"] = height;  // 高度
    
    // 📌 修复：camera_frame_zoom 应该发送到 services topic，不是 drc topic
    return sendServiceCommand("camera_frame_zoom", data);
}

/**
 * @brief 设置变焦倍率
 * 
 * 调用 DJI Cloud API 的 camera_focal_length_set 方法。
 * 
 * 📌 修复：
 * 1. 参数应该是 zoom_factor（变焦倍数），不是 zoom_focal_length（焦距毫米）
 * 2. 应该发送到 services topic，不是 drc topic
 * 
 * @param payloadIndex 相机 payload 索引
 * @param zoomFactor 变焦倍数，可见光范围 2-200，红外范围 2-20
 */
bool MqttClient::Impl::setFocalLength(const std::string& payloadIndex, 
                                       int zoomFactor) {
    logger_.info("🔭 设置变焦倍率: payload=" + payloadIndex + 
                 ", zoom_factor=" + std::to_string(zoomFactor) + "x");
    
    nlohmann::json data;
    data["payload_index"] = payloadIndex;
    data["camera_type"] = "zoom";  // 变焦设置针对变焦相机
    data["zoom_factor"] = zoomFactor;  // 📌 修复：正确的参数名是 zoom_factor
    
    // 📌 修复：camera_focal_length_set 应该发送到 services topic
    return sendServiceCommand("camera_focal_length_set", data);
}

/**
 * @brief 拍照
 * 
 * 调用 DJI Cloud API 的 camera_photo_take 方法。
 * 照片将存储在无人机 SD 卡中。
 * 
 * 📌 修复：camera_photo_take 应该发送到 services topic
 * 
 * @param payloadIndex 相机 payload 索引，如 "81-0-0"
 */
bool MqttClient::Impl::takePhoto(const std::string& payloadIndex) {
    logger_.info("📷 拍照: payload=" + payloadIndex);
    
    nlohmann::json data;
    data["payload_index"] = payloadIndex;
    
    return sendServiceCommand("camera_photo_take", data);
}

/**
 * @brief 云台复位
 * 
 * 调用 DJI Cloud API 的 gimbal_reset 方法。
 * 用于变焦拍照后恢复云台到默认位置。
 * 
 * 📌 修复：gimbal_reset 应该发送到 services topic
 * 
 * @param payloadIndex 相机 payload 索引
 * @param resetMode 复位模式:
 *   - 0: 回中（yaw 回正前方，pitch 回水平）
 *   - 1: 机头朝下（pitch 回 -90°）
 *   - 2: 仅 yaw 回中
 *   - 3: yaw 回负前方，pitch 回水平
 */
bool MqttClient::Impl::resetGimbal(const std::string& payloadIndex, int resetMode) {
    logger_.info("🔄 云台复位: payload=" + payloadIndex + 
                 ", mode=" + std::to_string(resetMode));
    
    nlohmann::json data;
    data["payload_index"] = payloadIndex;
    data["reset_mode"] = resetMode;
    
    return sendServiceCommand("gimbal_reset", data);
}

}  // namespace mqtt
}  // namespace esdk_sophon