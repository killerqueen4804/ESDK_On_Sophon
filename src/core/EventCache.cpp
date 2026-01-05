/**
 * @file EventCache.cpp
 * @brief MQTT 事件缓存实现
 *
 * 提供 MQTT 事件缓存与重试功能：
 * - 内存缓存：队列形式存储待重试事件
 * - 文件持久化：JSON 格式持久化到磁盘
 * - 自动重试：MQTT 重连后自动重试
 * - 过期清理：定期清理过期事件
 *
 * @author ESDK Sophon Team
 * @date 2025-11-20
 */

#include "esdk_sophon/core/EventCache.h"
#include "esdk_sophon/core/Logger.h"

// JSON
#include <nlohmann/json.hpp>

// 标准库
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <thread>

namespace esdk_sophon {
namespace core {

// ==================== Singleton 实现 ====================

EventCache& EventCache::getInstance() {
    static EventCache instance;
    return instance;
}

EventCache::EventCache()
    // ⚠️ 注意：成员变量的初始化顺序由头文件中的声明顺序决定，而不是由这里的书写顺序决定。
    // 为消除 -Wreorder 警告，这里按 EventCache.h 中的声明顺序显式排列初始化列表。
    : cacheFilePath_("/tmp/mqtt_event_cache.json")
    , maxRetryCount_(5)
    , maxEventAge_(3600)
    , logger_(Logger::getInstance()) {
    
    logger_.info("📦 EventCache 初始化完成");
    
    // 启动时加载缓存
    loadFromFile();
}

EventCache::~EventCache() {
    logger_.info("📦 EventCache 销毁");
    
    // 销毁时保存缓存
    saveToFile();
}

// ==================== 配置接口 ====================

void EventCache::setMaxRetryCount(int maxRetry) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    maxRetryCount_ = maxRetry;
    logger_.info("⚙️ 设置最大重试次数: " + std::to_string(maxRetry));
}

void EventCache::setMaxEventAge(int maxAgeSeconds) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    maxEventAge_ = maxAgeSeconds;
    logger_.info("⚙️ 设置事件过期时间: " + std::to_string(maxAgeSeconds) + " 秒");
}

void EventCache::setCacheFilePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheFilePath_ = path;
    logger_.info("⚙️ 设置缓存文件路径: " + path);
}

void EventCache::setPublishCallback(EventPublishCallback callback) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    publishCallback_ = std::move(callback);
    logger_.info("⚙️ 设置 MQTT 发布回调");
}

// ==================== 核心功能 ====================

bool EventCache::publishEvent(const std::string& topic, const nlohmann::json& event) {
    EventPublishCallback callback;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        callback = publishCallback_;
    }

    if (callback) {
        if (callback(topic, event.dump())) {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            totalPublished_++;
            logger_.debug("✅ MQTT 事件发布成功: " + topic);
            return true;
        } else {
            logger_.warning("⚠️ MQTT 事件发布失败，准备缓存: " + topic);
        }
    } else {
        logger_.warning("⚠️ MQTT 发布回调未设置，准备缓存: " + topic);
    }

    cacheEvent(topic, event);
    return false;
}

void EventCache::onMqttReconnected() {
    logger_.info("🔄 MQTT 重连成功，准备异步重试缓存事件（后台线程）");

    // 为避免在调用线程中阻塞（可能导致任务线程停滞），
    // 将重试操作放到后台线程执行。
    try {
        std::thread([this]() {
            // 后台线程执行，捕获异常并在日志中记录
            try {
                retryAll();
            } catch (const std::exception& e) {
                logger_.error(std::string("后台重试事件线程异常: ") + e.what());
            } catch (...) {
                logger_.error("后台重试事件线程发生未知异常");
            }
        }).detach();
    } catch (const std::exception& e) {
        logger_.error(std::string("启动后台重试线程失败: ") + e.what());
        // 退回到同步重试以保证事件不丢失
        retryAll();
    }
}

int EventCache::retryAll() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return retryAllInternal();
}

size_t EventCache::getCachedEventCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cachedEvents_.size();
}

void EventCache::clearAll() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    logger_.info("🗑️ 清空事件缓存: " + std::to_string(cachedEvents_.size()) + " 条");
    cachedEvents_.clear();
    saveToFile();
}

int EventCache::cleanupExpiredEvents() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    if (cachedEvents_.empty()) {
        return 0;
    }

    size_t beforeSize = cachedEvents_.size();
    auto it = cachedEvents_.begin();
    
    while (it != cachedEvents_.end()) {
        if (it->isExpired(maxEventAge_)) {
            logger_.debug("⏰ 清理过期事件: " + it->topic);
            it = cachedEvents_.erase(it);
            totalDropped_++;
        } else {
            ++it;
        }
    }
    
    int removedCount = static_cast<int>(beforeSize - cachedEvents_.size());
    if (removedCount > 0) {
        logger_.info("🗑️ 已清理过期事件: " + std::to_string(removedCount) + " 条");
        saveToFile();
    }
    return removedCount;
}

// ==================== 内部辅助方法 ====================

void EventCache::cacheEvent(const std::string& topic, const nlohmann::json& event) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    CachedEvent cachedEvent(event, topic);
    cachedEvents_.push_back(cachedEvent);
    totalCached_++;
    
    logger_.info("📦 事件已缓存: " + topic + " (队列大小: " + 
                std::to_string(cachedEvents_.size()) + ")");
                
    saveToFile();
}

bool EventCache::tryPublish(CachedEvent& cachedEvent) {
    if (publishCallback_) {
        return publishCallback_(cachedEvent.topic, cachedEvent.data.dump());
    }
    return false;
}

int EventCache::retryAllInternal() {
    if (cachedEvents_.empty()) {
        return 0;
    }

    logger_.info("🔄 开始重试缓存事件: " + std::to_string(cachedEvents_.size()) + " 条");

    auto it = cachedEvents_.begin();
    int successCount = 0;
    int failedCount = 0;

    while (it != cachedEvents_.end()) {
        if (it->isExpired(maxEventAge_)) {
            logger_.warning("⏰ 事件已过期，删除: " + it->topic);
            it = cachedEvents_.erase(it);
            totalDropped_++;
            continue;
        }

        if (it->retryCount >= maxRetryCount_) {
            logger_.error("❌ 事件重试次数超限，删除: " + it->topic +
                         " (重试次数: " + std::to_string(it->retryCount) + ")");
            it = cachedEvents_.erase(it);
            totalDropped_++;
            continue;
        }

        bool success = false;
        if (publishCallback_) {
            success = publishCallback_(it->topic, it->data.dump());
        }

        if (success) {
            logger_.info("✅ 事件重试成功: " + it->topic +
                        " (重试次数: " + std::to_string(it->retryCount) + ")");
            it = cachedEvents_.erase(it);
            totalPublished_++;
            successCount++;
        } else {
            logger_.warning("⚠️ 事件重试失败: " + it->topic +
                           " (重试次数: " + std::to_string(it->retryCount) + ")");
            it->retryCount++;
            ++it;
            failedCount++;
        }
    }

    logger_.info("🔄 重试完成: 成功 " + std::to_string(successCount) +
                " 条，失败 " + std::to_string(failedCount) +
                " 条，剩余 " + std::to_string(cachedEvents_.size()) + " 条");

    if (successCount > 0 || failedCount > 0) {
        saveToFile();
    }
    
    return successCount;
}

// ==================== 文件持久化 ====================

bool EventCache::saveToFile() {
    logger_.debug("💾 保存事件缓存到文件: " + cacheFilePath_);

    try {
        nlohmann::json jsonArray = nlohmann::json::array();

        for (const auto& event : cachedEvents_) {
            jsonArray.push_back(event.toJson());
        }

        std::ofstream ofs(cacheFilePath_);
        if (!ofs.is_open()) {
            logger_.error("❌ 无法打开缓存文件: " + cacheFilePath_);
            return false;
        }

        ofs << jsonArray.dump(2);
        ofs.close();

        return true;

    } catch (const std::exception& e) {
        logger_.error("❌ 保存事件缓存失败: " + std::string(e.what()));
        return false;
    }
}

int EventCache::loadFromFile() {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    logger_.info("📂 加载事件缓存从文件: " + cacheFilePath_);

    std::ifstream ifs(cacheFilePath_);
    if (!ifs.is_open()) {
        logger_.info("ℹ️ 缓存文件不存在，跳过加载");
        return 0;
    }

    try {
        nlohmann::json jsonArray;
        ifs >> jsonArray;
        ifs.close();

        if (!jsonArray.is_array()) {
            logger_.error("❌ 缓存文件格式错误（非数组）");
            return 0;
        }

        cachedEvents_.clear();

        for (const auto& item : jsonArray) {
            try {
                cachedEvents_.push_back(CachedEvent::fromJson(item));
            } catch (const std::exception& e) {
                logger_.error("❌ 解析缓存事件失败: " + std::string(e.what()));
            }
        }

        logger_.info("✅ 事件缓存已加载: " + std::to_string(cachedEvents_.size()) + " 条");

        auto it = cachedEvents_.begin();
        while (it != cachedEvents_.end()) {
            if (it->isExpired(maxEventAge_)) {
                it = cachedEvents_.erase(it);
            } else {
                ++it;
            }
        }

        return static_cast<int>(cachedEvents_.size());

    } catch (const std::exception& e) {
        logger_.error("❌ 加载事件缓存失败: " + std::string(e.what()));
        return 0;
    }
}

}  // namespace core
}  // namespace esdk_sophon
