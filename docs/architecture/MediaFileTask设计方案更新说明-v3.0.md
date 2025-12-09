# 📘 MediaFileTask 设计方案更新说明 (v3.0)

**日期**: 2025-11-20  
**更新原因**: 架构重构 - 使用聚合工具类  
**相关文档**: [MediaFileTask 架构重构说明.md](MediaFileTask架构重构说明.md)

---

## 🎯 核心变化总结

### 移除的私有方法 ❌

以下方法已从 `MediaFileTask` 类中移除，改为使用工具类：

```cpp
// ❌ 已移除 - 使用 ImageProcessor 替代
bool parseExif(const std::string& filePath, FileInfo& fileInfo);

// ❌ 已移除 - 使用 GeoUtils 替代
bool calculateGpsBatch(std::vector<FileInfo>& fileInfos);
bool requestGpsCalculation(const nlohmann::json& exifData, nlohmann::json& gpsResults);

// ❌ 已移除 - 使用 EventCache 替代
void cacheEvent(const nlohmann::json& event);
void retryCachedEvents();
bool publishEvent(const nlohmann::json& event);
```

### 新的工具类依赖 ✅

```cpp
#include "esdk_sophon/utils/ImageProcessor.h"   // 图片处理
#include "esdk_sophon/utils/GeoUtils.h"         // 地理信息
#include "esdk_sophon/core/EventCache.h"        // 事件缓存
```

---

## 💻 processFile() 方法重构

### 改进前（v1.0）

```cpp
bool MediaFileTask::processFile(const FileInfo& fileInfo) {
    // 1. 读取图片
    cv::Mat image;
    if (!readImage(fileInfo.filePath, image)) {
        return false;
    }

    // 2. 解析 EXIF（私有方法）❌
    FileInfo enrichedInfo = fileInfo;
    if (!parseExif(fileInfo.filePath, enrichedInfo)) {
        return false;
    }

    // 3. 调用检测
    auto result = service_->processFrame(image, config_);
    if (!result.has_value()) {
        return false;
    }

    // 4. 生成事件
    auto event = buildEvent(enrichedInfo, result.value());

    // 5. 计算 GPS（私有方法）❌
    std::vector<FileInfo> batch = {enrichedInfo};
    if (!calculateGpsBatch(batch)) {
        logger_.warning("GPS 计算失败");
    }

    // 6. 推送事件（私有方法）❌
    if (!publishEvent(event)) {
        cacheEvent(event);  // 失败时缓存
    }

    return true;
}
```

### 改进后（v3.0）✅

```cpp
bool MediaFileTask::processFile(const FileInfo& fileInfo) {
    // 1. 读取图片
    cv::Mat image;
    if (!readImage(fileInfo.filePath, image)) {
        return false;
    }

    // 2. 解析 EXIF（使用 ImageProcessor）⭐
    auto& processor = ImageProcessor::getInstance();
    auto metadata = processor.parseExif(fileInfo.filePath);
    if (!metadata || !metadata->hasGps) {
        logger_.error("EXIF 解析失败或无 GPS 数据: " + fileInfo.filePath);
        return false;
    }

    // 3. 调用检测
    auto result = service_->processFrame(image, config_);
    if (!result.has_value()) {
        return false;
    }

    // 4. 生成事件
    nlohmann::json event = buildEvent(fileInfo, result.value());

    // 5. 添加图片元数据
    event["timestamp"] = metadata->timestamp;
    event["cameraModel"] = metadata->cameraModel;

    // 6. 生成缩略图（使用 ImageProcessor）⭐
    auto resized = processor.resize(image, 800, 600);
    event["thumbnail"] = processor.encodeBase64(resized);

    // 7. 计算 GPS（使用 GeoUtils）⭐
    auto& geo = GeoUtils::getInstance();
    GpsCoordinate gpsCoord{
        metadata->latitude,
        metadata->longitude,
        metadata->altitude,
        metadata->heading
    };
    auto pixelCoord = geo.convertGpsToPixel(gpsCoord);
    if (pixelCoord.success) {
        event["gpsX"] = pixelCoord.x;
        event["gpsY"] = pixelCoord.y;
    } else {
        logger_.warning("GPS 计算失败: " + pixelCoord.errorMsg);
    }

    // 8. 推送事件（使用 EventCache）⭐
    auto& cache = EventCache::getInstance();
    cache.publishEvent("drone/detection_event", event);  // 自动缓存+重试

    // 9. 更新统计
    stats_.processedFrames++;

    // 10. 更新最后收到文件时间
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        lastFileTime_ = std::chrono::steady_clock::now();
    }

    return true;
}
```

---

## 📋 更新后的类成员变量

### 移除的成员变量 ❌

```cpp
// ❌ 已移除 - EventCache 替代
std::vector<nlohmann::json> cachedEvents_;
std::mutex cacheMutex_;
std::string cacheFilePath_;
```

### 保留的成员变量 ✅

```cpp
class MediaFileTask : public ITask {
private:
    TaskConfig config_;
    std::shared_ptr<TaskService> service_;

    // 状态管理
    std::atomic<TaskState> state_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<bool> taskEnded_;  // ⭐ 航线任务是否结束

    // 线程管理
    std::unique_ptr<std::thread> workerThread_;
    std::unique_ptr<std::thread> monitorThread_;

    // 回调函数
    TaskCallback stateCallback_;
    ErrorCallback errorCallback_;

    // 统计信息
    TaskStatistics stats_;
    mutable std::mutex statsMutex_;

    // 文件队列
    std::queue<FileInfo> fileQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    size_t maxQueueSize_{100};

    // 超时控制
    std::chrono::steady_clock::time_point lastFileTime_;
    std::mutex timerMutex_;
    static constexpr int TIMEOUT_SECONDS = 60;

    // DJI MediaManager
    std::shared_ptr<edge_sdk::MediaManager> mediaManager_;
    int observerId_{-1};

    // 文件存储
    std::vector<std::string> downloadedFiles_;
    std::mutex filesMutex_;

    // 日志
    core::Logger& logger_;
};
```

---

## 🔧 工具类使用示例

### 1. ImageProcessor 使用

```cpp
auto& processor = ImageProcessor::getInstance();

// EXIF 解析
auto metadata = processor.parseExif("photo.jpg");
if (metadata && metadata->hasGps) {
    double lat = metadata->latitude;
    double lon = metadata->longitude;
}

// 图片缩放
cv::Mat resized = processor.resize(image, 800, 600);

// Base64 编码
std::string base64 = processor.encodeBase64(resized);

// 压缩到指定大小
std::vector<uchar> buffer;
processor.compressToSize(image, 200, buffer);  // 200KB
```

### 2. GeoUtils 使用

```cpp
auto& geo = GeoUtils::getInstance();

// 配置 GPS 服务
geo.setGpsServiceUrl("http://gps-service:8080/api/gps/calculate");

// GPS 转像素坐标
GpsCoordinate gps{23.123, 113.456, 100.0, 45.0};
auto pixel = geo.convertGpsToPixel(gps);

// 批量转换（推荐）
std::vector<GpsCoordinate> coords = {...};
auto pixels = geo.convertGpsToPixelBatch(coords);

// 计算距离
double distance = geo.calculateDistance(p1, p2);

// 判断点是否在区域内
bool inside = geo.isPointInPolygon(point, polygon);
```

### 3. EventCache 使用

```cpp
auto& cache = EventCache::getInstance();

// 配置
cache.setCacheFilePath("/data/event_cache.json");
cache.setPublishCallback([](const std::string& topic, const std::string& payload) {
    return mqttClient.publish(topic, payload);
});

// 发布事件（自动缓存）
nlohmann::json event = {...};
cache.publishEvent("drone/event", event);

// MQTT 重连时自动重试
mqttClient.setReconnectCallback([&cache]() {
    cache.onMqttReconnected();
});
```

---

## 📊 Day 10-14 开发计划更新

### Day 10: 基础架构 ✅

**更新内容**:

- [x] 添加工具类头文件引用
- [x] 移除 parseExif、calculateGpsBatch、cacheEvent 等私有方法
- [x] 更新 processFile() 使用工具类
- [x] 初始化工具类配置（GPS URL、EventCache 回调）

### Day 11: 检测和事件生成

**更新内容**:

- [ ] 使用 `ImageProcessor::parseExif()` 解析 EXIF
- [ ] 使用 `ImageProcessor::resize()` 生成缩略图
- [ ] 使用 `ImageProcessor::encodeBase64()` 编码图片
- [ ] 实现 `buildEvent()` (按算法类型分组)

### Day 12: GPS 计算和 MQTT 推送

**更新内容**:

- [ ] 使用 `GeoUtils::convertGpsToPixelBatch()` 批量计算 GPS
- [ ] 使用 `EventCache::publishEvent()` 推送事件
- [ ] 实现 `shouldComplete()` 超时逻辑
- [ ] 实现 `completeTask()` 自动完成

### Day 13: 集成测试

**更新内容**:

- [ ] 测试 ImageProcessor 集成
- [ ] 测试 GeoUtils 集成
- [ ] 测试 EventCache 集成
- [ ] 端到端测试

### Day 14: 文档完善

**更新内容**:

- [ ] 更新改进记录（记录工具类使用）
- [ ] 更新八股文档（类粒度设计实例）
- [ ] Code Review

---

## ⚠️ 注意事项

### 1. 工具类初始化

在 `start()` 方法中初始化工具类：

```cpp
bool MediaFileTask::start() {
    logger_.info("启动媒体文件分析任务: " + config_.taskId);

    // 1. 配置 GeoUtils
    auto& geo = GeoUtils::getInstance();
    geo.setGpsServiceUrl(config_.gpsServiceUrl);  // 从配置读取
    geo.setHttpTimeout(5000);
    geo.setHttpRetryCount(3);
    geo.enableCache(true);  // 启用 LRU 缓存

    // 2. 配置 EventCache
    auto& cache = EventCache::getInstance();
    cache.setCacheFilePath("/data/mediafile_event_cache.json");
    cache.setPublishCallback([this](const std::string& topic, const std::string& payload) {
        // 调用 MqttClient 发布
        return mqttClient_->publish(topic, payload);
    });

    // 3. 注册 MediaManager
    if (!registerMediaObserver()) {
        return false;
    }

    // 4. 启动线程
    running_ = true;
    workerThread_ = std::make_unique<std::thread>(&MediaFileTask::execute, this);
    monitorThread_ = std::make_unique<std::thread>(&MediaFileTask::monitorLoop, this);

    return true;
}
```

### 2. 错误处理

```cpp
// ImageProcessor::parseExif() 返回 std::optional
auto metadata = processor.parseExif(filePath);
if (!metadata) {
    logger_.error("EXIF 解析失败");
    return false;
}

// GeoUtils::convertGpsToPixel() 返回 PixelCoordinate (含 success 标志)
auto pixelCoord = geo.convertGpsToPixel(gpsCoord);
if (!pixelCoord.success) {
    logger_.warning("GPS 计算失败: " + pixelCoord.errorMsg);
    // 继续处理，GPS 失败不影响检测
}

// EventCache::publishEvent() 自动处理失败（内部缓存）
cache.publishEvent("drone/event", event);  // 无需检查返回值
```

### 3. 线程安全

所有工具类都是线程安全的，可在多线程环境中安全使用：

- `ImageProcessor`: Singleton + 无状态方法
- `GeoUtils`: Singleton + mutex 保护的配置和缓存
- `EventCache`: Singleton + mutex 保护的缓存队列

---

**创建者**: AI Assistant  
**最后更新**: 2025-11-20  
**版本**: v3.0  
**状态**: ✅ 设计完成
