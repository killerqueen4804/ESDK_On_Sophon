/**
 * @file EventCache.h
 * @brief 事件缓存服务
 * 
 * 负责 MQTT 事件的本地缓存和重试机制。
 * 当 MQTT 断线时，将事件缓存到本地；重连后自动重试。
 * 
 * 核心功能:
 * - 内存缓存（快速）
 * - 文件持久化（可靠）
 * - 自动重试（智能）
 * - 过期清理（避免堆积）
 * 
 * 使用场景:
 * - MediaFileTask: 缓存检测事件
 * - LiveStreamTask: 缓存实时检测事件
 * - 所有需要可靠事件推送的任务
 * 
 * 设计模式:
 * - Singleton (单例模式): 全局唯一实例
 * - Observer (观察者模式): 监听 MQTT 重连事件
 * 
 * 线程安全:
 * - ✅ 所有方法都是线程安全的
 * - ✅ 使用互斥锁保护共享数据
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-20
 */

#ifndef ESDK_SOPHON_CORE_EVENT_CACHE_H_
#define ESDK_SOPHON_CORE_EVENT_CACHE_H_

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>
#include "esdk_sophon/core/Logger.h"

namespace esdk_sophon {
namespace core {

/**
 * @brief 缓存事件结构体
 */
struct CachedEvent {
    nlohmann::json data;                                    ///< 事件数据（JSON）
    std::string topic;                                      ///< MQTT 主题
    std::chrono::system_clock::time_point timestamp;        ///< 缓存时间戳
    int retryCount{0};                                      ///< 已重试次数
    
    /**
     * @brief 构造函数
     */
    CachedEvent(const nlohmann::json& eventData, 
                const std::string& mqttTopic)
        : data(eventData)
        , topic(mqttTopic)
        , timestamp(std::chrono::system_clock::now())
        , retryCount(0) {}
    
    /**
     * @brief 检查是否过期
     * 
     * @param maxAgeSeconds 最大缓存时长（秒）
     * @return true 已过期
     */
    bool isExpired(int maxAgeSeconds) const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - timestamp
        ).count();
        return age > maxAgeSeconds;
    }
    
    /**
     * @brief 转换为 JSON（用于持久化）
     */
    nlohmann::json toJson() const {
        return nlohmann::json{
            {"data", data},
            {"topic", topic},
            {"timestamp", std::chrono::system_clock::to_time_t(timestamp)},
            {"retryCount", retryCount}
        };
    }
    
    /**
     * @brief 从 JSON 解析（用于恢复）
     */
    static CachedEvent fromJson(const nlohmann::json& json) {
        CachedEvent event(
            json.at("data").get<nlohmann::json>(),
            json.at("topic").get<std::string>()
        );
        
        auto timestamp_t = json.at("timestamp").get<std::time_t>();
        event.timestamp = std::chrono::system_clock::from_time_t(timestamp_t);
        event.retryCount = json.value("retryCount", 0);
        
        return event;
    }
};

/**
 * @brief 事件发布回调函数类型
 * 
 * @param topic MQTT 主题
 * @param payload MQTT 消息体（JSON 字符串）
 * @return true 发布成功
 * @return false 发布失败
 */
using EventPublishCallback = std::function<bool(const std::string& topic, 
                                                 const std::string& payload)>;

/**
 * @brief 事件缓存服务
 * 
 * 单例模式，提供全局事件缓存功能。
 * 
 * 工作流程:
 * @code
 * 1. 任务尝试推送事件 → publishEvent()
 *    ↓
 * 2. 如果 MQTT 连接正常 → 直接发布，返回成功
 *    ↓
 * 3. 如果 MQTT 断线 → 缓存到内存 + 持久化到文件
 *    ↓
 * 4. MQTT 重连 → 触发 onMqttReconnected()
 *    ↓
 * 5. 自动重试缓存事件 → retryAll()
 *    ↓
 * 6. 成功的事件从缓存移除，失败的继续保留（增加重试计数）
 *    ↓
 * 7. 超过最大重试次数的事件被丢弃
 * @endcode
 * 
 * 示例用法:
 * @code
 * auto& cache = EventCache::getInstance();
 * 
 * // 1. 配置缓存文件路径
 * cache.setCacheFilePath("/data/event_cache.json");
 * 
 * // 2. 注册事件发布回调
 * cache.setPublishCallback([](const std::string& topic, const std::string& payload) {
 *     return mqttClient.publish(topic, payload);
 * });
 * 
 * // 3. 监听 MQTT 重连事件
 * mqttClient.setReconnectCallback([&cache]() {
 *     cache.onMqttReconnected();
 * });
 * 
 * // 4. 任务推送事件（自动处理缓存）
 * nlohmann::json event = { ... };
 * if (!cache.publishEvent("drone/event", event)) {
 *     // 事件已缓存，等待重试
 * }
 * @endcode
 */
class EventCache {
public:
    /**
     * @brief 获取单例实例
     * 
     * @return EventCache& 单例引用
     */
    static EventCache& getInstance();
    
    // 禁止拷贝和赋值
    EventCache(const EventCache&) = delete;
    EventCache& operator=(const EventCache&) = delete;
    
    /**
     * @brief 设置缓存文件路径
     * 
     * @param filePath 缓存文件绝对路径（如 "/data/event_cache.json"）
     * 
     * @note 必须在调用其他方法前设置
     * @note 线程安全
     */
    void setCacheFilePath(const std::string& filePath);
    
    /**
     * @brief 设置事件发布回调
     * 
     * @param callback 回调函数（用于发布事件到 MQTT）
     * 
     * @note 必须设置，否则无法发布事件
     * @note 线程安全
     */
    void setPublishCallback(EventPublishCallback callback);
    
    /**
     * @brief 设置最大重试次数
     * 
     * @param maxRetry 最大重试次数，默认 5 次
     * 
     * @note 超过此次数的事件会被丢弃
     * @note 线程安全
     */
    void setMaxRetryCount(int maxRetry);
    
    /**
     * @brief 设置事件最大缓存时长
     * 
     * @param maxAgeSeconds 最大缓存时长（秒），默认 3600 秒（1小时）
     * 
     * @note 超过此时长的事件会被清理
     * @note 线程安全
     */
    void setMaxEventAge(int maxAgeSeconds);
    
    /**
     * @brief 发布事件（自动缓存）
     * 
     * 流程:
     * 1. 尝试通过回调发布到 MQTT
     * 2. 如果成功 → 返回 true
     * 3. 如果失败 → 缓存到本地 + 持久化，返回 false
     * 
     * @param topic MQTT 主题
     * @param event 事件数据（JSON）
     * @return true 发布成功
     * @return false 发布失败（已缓存）
     * 
     * 示例:
     * @code
     * nlohmann::json event = {
     *     {"taskID", 1234},
     *     {"eventType", 101},
     *     {"detections", {...}}
     * };
     * 
     * if (!cache.publishEvent("drone/event", event)) {
     *     logger.warning("事件已缓存，等待重试");
     * }
     * @endcode
     * 
     * @note 线程安全
     * @note 失败的事件会自动缓存，无需手动处理
     */
    bool publishEvent(const std::string& topic, const nlohmann::json& event);
    
    /**
     * @brief MQTT 重连回调
     * 
     * 当 MQTT 重连成功时调用，自动重试所有缓存事件。
     * 
     * @note 应该在 MQTT 重连回调中调用此方法
     * @note 线程安全
     */
    void onMqttReconnected();
    
    /**
     * @brief 手动重试所有缓存事件
     * 
     * @return int 成功发布的事件数
     * 
     * @note 线程安全
     */
    int retryAll();
    
    /**
     * @brief 获取缓存事件数量
     * 
     * @return size_t 当前缓存的事件数
     * 
     * @note 线程安全
     */
    size_t getCachedEventCount() const;
    
    /**
     * @brief 清空所有缓存事件
     * 
     * @note 线程安全
     * @note 谨慎使用，会丢失所有未发布的事件
     */
    void clearAll();
    
    /**
     * @brief 清理过期事件
     * 
     * 删除所有超过 maxEventAge 的事件。
     * 
     * @return int 删除的事件数
     * 
     * @note 线程安全
     * @note 建议定期调用（如每小时一次）
     */
    int cleanupExpiredEvents();
    
    /**
     * @brief 从文件加载缓存事件
     * 
     * 程序启动时调用，恢复上次未发布的事件。
     * 
     * @return int 加载的事件数
     * 
     * @note 线程安全
     * @note 自动在构造函数中调用
     */
    int loadFromFile();
    
    /**
     * @brief 保存缓存事件到文件
     * 
     * 持久化所有缓存事件，防止程序崩溃丢失。
     * 
     * @return true 保存成功
     * @return false 保存失败
     * 
     * @note 线程安全
     * @note 每次缓存事件时自动调用
     */
    bool saveToFile();

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    EventCache();
    
    /**
     * @brief 析构函数
     * 
     * 自动保存缓存到文件。
     */
    ~EventCache();
    
    // ==================== 内部辅助方法 ====================
    
    /**
     * @brief 缓存单个事件
     * 
     * @param topic MQTT 主题
     * @param event 事件数据
     */
    void cacheEvent(const std::string& topic, const nlohmann::json& event);
    
    /**
     * @brief 尝试发布单个事件
     * 
     * @param cachedEvent 缓存事件
     * @return true 发布成功
     * @return false 发布失败
     */
    bool tryPublish(CachedEvent& cachedEvent);
    
    /**
     * @brief 内部重试逻辑（无锁）
     * 
     * @return int 成功发布的事件数
     */
    int retryAllInternal();

private:
    std::string cacheFilePath_;                  ///< 缓存文件路径
    EventPublishCallback publishCallback_;       ///< 事件发布回调
    int maxRetryCount_{5};                       ///< 最大重试次数
    int maxEventAge_{3600};                      ///< 最大缓存时长（秒）
    
    // 缓存队列
    std::vector<CachedEvent> cachedEvents_;      ///< 缓存事件列表
    mutable std::mutex cacheMutex_;              ///< 缓存互斥锁
    
    // 统计信息
    size_t totalCached_{0};                      ///< 累计缓存事件数
    size_t totalPublished_{0};                   ///< 累计发布事件数
    size_t totalDropped_{0};                     ///< 累计丢弃事件数
    
    // 日志
    core::Logger& logger_;
};

}  // namespace core
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_CORE_EVENT_CACHE_H_
