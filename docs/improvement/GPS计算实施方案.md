# GPS 计算实施方案

> **文档目的**: 详细说明如何实现检测框中心点的实际 GPS 坐标计算
> **参考项目**: esdk_on_sophon_old/src/yolov10/image_processor_yolov10.cpp
> **创建时间**: 2025-11-03

---

## 📋 问题描述

### 当前实现（错误）

```cpp
// TaskService.cpp 第 257 行
event.latitude = 31.230391;   // ❌ 使用无人机自身经纬度
event.longitude = 121.473701;
```

### 正确做法

**event.latitude / longitude**: 应该是所有检测框中心点的实际 GPS 坐标的平均值

**points[i].lat / lon**: 每个检测框中心点的实际 GPS 坐标

---

## 🔍 旧项目实现分析

### 核心流程（image_processor_yolov10.cpp 第 477-488 行）

```cpp
// 1. 从图片 EXIF 读取无人机的经纬度
readExifData(file_name, frame, event.createTime, event.latitude, event.longitude);

// 2. 准备 API 请求数据
QJsonObject requestData;
requestData["path"] = "/data/Edge-SDK/build/bin/" + file_name;  // 图片路径
requestData["points"] = points;  // 检测框数组 [{x, y, w, h}, ...]

// 3. 调用 GeoDecodeAPI，获取每个检测框的 GPS 坐标
float lon = 0, lat = 0;
QJsonArray pointsArray = getPos(lon, lat, requestData, points_info);

// 4. 设置事件经纬度为平均值
event.longitude = lon;
event.latitude = lat;

// 5. 发布事件（pointsArray 包含每个框的 GPS 坐标）
QJsonObject evjson = event.toJson();
evjson["points"] = pointsArray;
emit pushMsg(evjson, "drone/XXX/info/event");
```

### getPos() 函数详解（第 500-546 行）

```cpp
QJsonArray YoloV10::getPos(float &lon, float &lat,
                           QJsonObject requestData,
                           QJsonArray points_info) {
    // 1. 调用 GeoDecodeAPI（HTTP POST）
    QJsonObject response = callGeoDecodeAPI(requestData);

    // 2. 解析响应
    QJsonArray dataArray = response["data"].toArray();
    QJsonArray pointsArray;
    int effectivePoint = 0;

    // 3. 遍历每个检测框
    for (int i = 0; i < dataArray.size(); ++i) {
        QJsonObject new_pointObj = dataArray[i].toObject();  // API 返回的 GPS
        QJsonObject old_pointObj = points_info[i].toObject(); // 原始检测框

        QJsonObject newPoint;

        // 3.1 提取经度并累加
        if (!new_pointObj["longitude"].isNull()) {
            newPoint["lon"] = new_pointObj["longitude"].toDouble();
            lon += new_pointObj["longitude"].toDouble();
            effectivePoint++;
        } else {
            newPoint["lon"] = QJsonValue::Null;
        }

        // 3.2 提取纬度并累加
        if (!new_pointObj["latitude"].isNull()) {
            newPoint["lat"] = new_pointObj["latitude"].toDouble();
            lat += new_pointObj["latitude"].toDouble();
        } else {
            newPoint["lat"] = QJsonValue::Null;
        }

        // 3.3 保留原始图像坐标
        newPoint["x"] = old_pointObj["x"];
        newPoint["y"] = old_pointObj["y"];
        newPoint["w"] = old_pointObj["w"];
        newPoint["h"] = old_pointObj["h"];

        pointsArray.append(newPoint);
    }

    // 4. 计算平均值
    lon /= effectivePoint;
    lat /= effectivePoint;

    return pointsArray;
}
```

### callGeoDecodeAPI() 函数（第 550-589 行）

```cpp
QJsonObject YoloV10::callGeoDecodeAPI(const QJsonObject& requestData,
                                      int timeoutMs) {
    QEventLoop loop;
    QNetworkAccessManager manager;
    QJsonObject result;

    // 发送 HTTP POST 请求
    QNetworkRequest request;
    request.setUrl(QUrl("http://127.0.0.1:8122/geoDecode/arithmetic/getLonLatByPoints"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = manager.post(request,
                                       QJsonDocument(requestData).toJson());

    // 处理响应
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            result = QJsonDocument::fromJson(response).object();
        } else {
            result["error"] = reply->errorString();
            result["http_code"] = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
        }
        reply->deleteLater();
        loop.quit();
    });

    loop.exec();  // 阻塞等待响应
    return result;
}
```

---

## 🛠️ 新项目实施方案

### 方案 A: GeoDecodeAPI 集成（推荐，与旧项目一致）

#### 步骤 1: 创建 HttpClient 工具类

**文件**: `include/esdk_sophon/utils/HttpClient.h`

```cpp
#ifndef ESDK_SOPHON_UTILS_HTTP_CLIENT_H_
#define ESDK_SOPHON_UTILS_HTTP_CLIENT_H_

#include <string>
#include <nlohmann/json.hpp>

namespace esdk_sophon {
namespace utils {

/**
 * @brief HTTP 客户端工具类
 *
 * 使用 libcurl 实现简单的 HTTP POST 请求。
 * 线程安全（每次请求创建新的 CURL 句柄）。
 */
class HttpClient {
public:
    /**
     * @brief 发送 HTTP POST 请求
     *
     * @param url 目标 URL
     * @param jsonData 请求 JSON 数据
     * @param timeoutMs 超时时间（毫秒）
     * @param response 输出参数：响应 JSON
     * @return true 请求成功
     * @return false 请求失败（网络错误或 HTTP 错误）
     *
     * @note 使用示例:
     * @code
     * nlohmann::json req = {{"path", "/data/image.jpg"}};
     * nlohmann::json resp;
     * if (HttpClient::post("http://127.0.0.1:8122/api", req, 5000, resp)) {
     *     std::cout << "成功: " << resp.dump() << std::endl;
     * }
     * @endcode
     */
    static bool post(const std::string& url,
                    const nlohmann::json& jsonData,
                    int timeoutMs,
                    nlohmann::json& response);

    /**
     * @brief 获取最后一次错误信息
     */
    static std::string getLastError();

private:
    static std::string lastError_;
};

}  // namespace utils
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_UTILS_HTTP_CLIENT_H_
```

**文件**: `src/utils/HttpClient.cpp`

```cpp
#include "esdk_sophon/utils/HttpClient.h"
#include <curl/curl.h>
#include <sstream>

namespace esdk_sophon {
namespace utils {

std::string HttpClient::lastError_ = "";

// libcurl 回调函数：写入响应数据
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool HttpClient::post(const std::string& url,
                     const nlohmann::json& jsonData,
                     int timeoutMs,
                     nlohmann::json& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        lastError_ = "Failed to initialize CURL";
        return false;
    }

    std::string readBuffer;
    std::string postData = jsonData.dump();

    // 设置请求参数
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    // 设置 HTTP 头
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 执行请求
    CURLcode res = curl_easy_perform(curl);

    // 检查结果
    bool success = false;
    if (res == CURLE_OK) {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        if (httpCode == 200) {
            try {
                response = nlohmann::json::parse(readBuffer);
                success = true;
            } catch (const nlohmann::json::exception& e) {
                lastError_ = "JSON parse error: " + std::string(e.what());
            }
        } else {
            lastError_ = "HTTP error: " + std::to_string(httpCode);
        }
    } else {
        lastError_ = "CURL error: " + std::string(curl_easy_strerror(res));
    }

    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return success;
}

std::string HttpClient::getLastError() {
    return lastError_;
}

}  // namespace utils
}  // namespace esdk_sophon
```

#### 步骤 2: 修改 TaskService::buildEvent()

**文件**: `src/task/TaskService.cpp`（第 250-280 行）

```cpp
bool TaskService::buildEvent(const cv::Mat& frame,
                            const std::vector<BoundingBox>& boxes,
                            const TaskConfig& config,
                            DetectionEvent& event) {
    // 1. 基本信息
    event.uuid = generateUUID();
    event.taskId = config.taskId;
    event.eventType = config.eventTypes[0].id;
    event.mainType = config.eventTypes[0].mainType;
    event.eventDescribe = config.eventTypes[0].eventDescribe;
    event.pictureCode = ".jpg";

    // 2. 图片编码
    std::vector<uint8_t> jpegData;
    if (encodeImageToJPEG(frame, 85, jpegData)) {
        event.pictureBase64 = encodeBase64(jpegData);
    }

    // 3. 时间戳
    event.createTime = getCurrentTimeString();

    // 4. GPS 坐标计算（使用 GeoDecodeAPI）
    if (!calculateGPSCoordinates(frame, boxes, event)) {
        impl_->logger.warning("GPS 计算失败，使用默认坐标");
        // 降级方案：使用无人机GPS（需要 Device 模块提供）
        event.latitude = 31.230391;
        event.longitude = 121.473701;

        // 填充检测框（不含 GPS）
        for (const auto& box : boxes) {
            event.points.push_back(box);
        }
    }

    return true;
}

/**
 * @brief 计算检测框的 GPS 坐标（使用 GeoDecodeAPI）
 *
 * 调用本地 GeoDecodeAPI 服务，将图像坐标转换为实际 GPS 坐标。
 *
 * @param frame 原始图像
 * @param boxes 检测框列表
 * @param event 输出参数：填充 GPS 坐标和 points 数组
 * @return true 成功
 * @return false 失败（API 错误或网络问题）
 *
 * API 说明:
 * - 地址: http://127.0.0.1:8122/geoDecode/arithmetic/getLonLatByPoints
 * - 方法: POST
 * - 请求格式:
 *   {
 *     "path": "/tmp/frame_uuid.jpg",  // 图片路径
 *     "points": [
 *       {"x": 100, "y": 200, "w": 50, "h": 80},
 *       ...
 *     ]
 *   }
 * - 响应格式:
 *   {
 *     "code": 200,
 *     "data": [
 *       {"longitude": 121.473701, "latitude": 31.230391},
 *       ...
 *     ]
 *   }
 */
bool TaskService::calculateGPSCoordinates(const cv::Mat& frame,
                                         const std::vector<BoundingBox>& boxes,
                                         DetectionEvent& event) {
    // 1. 保存图片到临时文件（API 需要读取文件）
    std::string tempPath = "/tmp/frame_" + event.uuid + ".jpg";
    if (!cv::imwrite(tempPath, frame)) {
        impl_->logger.error("无法保存临时图片: " + tempPath);
        return false;
    }

    // 2. 构建 API 请求
    nlohmann::json request;
    request["path"] = tempPath;
    request["points"] = nlohmann::json::array();

    for (const auto& box : boxes) {
        nlohmann::json point;
        point["x"] = static_cast<int>(box.x);
        point["y"] = static_cast<int>(box.y);
        point["w"] = static_cast<int>(box.w);
        point["h"] = static_cast<int>(box.h);
        request["points"].push_back(point);
    }

    // 3. 调用 GeoDecodeAPI
    nlohmann::json response;
    if (!utils::HttpClient::post(
            "http://127.0.0.1:8122/geoDecode/arithmetic/getLonLatByPoints",
            request,
            50000,  // 5秒超时
            response)) {
        impl_->logger.error("GeoDecodeAPI 调用失败: " +
                           utils::HttpClient::getLastError());
        std::remove(tempPath.c_str());  // 删除临时文件
        return false;
    }

    // 4. 解析响应
    if (!response.contains("data") || !response["data"].is_array()) {
        impl_->logger.error("GeoDecodeAPI 响应格式错误");
        std::remove(tempPath.c_str());
        return false;
    }

    auto dataArray = response["data"];
    if (dataArray.size() != boxes.size()) {
        impl_->logger.error("GeoDecodeAPI 返回数量不匹配");
        std::remove(tempPath.c_str());
        return false;
    }

    // 5. 填充 GPS 坐标
    double sumLat = 0.0, sumLon = 0.0;
    int effectiveCount = 0;

    for (size_t i = 0; i < boxes.size(); ++i) {
        BoundingBox eventBox = boxes[i];

        if (!dataArray[i]["longitude"].is_null() &&
            !dataArray[i]["latitude"].is_null()) {
            double lon = dataArray[i]["longitude"].get<double>();
            double lat = dataArray[i]["latitude"].get<double>();

            eventBox.lon = lon;
            eventBox.lat = lat;

            sumLon += lon;
            sumLat += lat;
            effectiveCount++;
        }

        event.points.push_back(eventBox);
    }

    // 6. 设置事件经纬度为平均值
    if (effectiveCount > 0) {
        event.longitude = sumLon / effectiveCount;
        event.latitude = sumLat / effectiveCount;
    } else {
        impl_->logger.warning("所有检测框的 GPS 坐标都无效");
        std::remove(tempPath.c_str());
        return false;
    }

    // 7. 清理临时文件
    std::remove(tempPath.c_str());

    impl_->logger.info("GPS 计算成功: 平均坐标 (" +
                      std::to_string(event.latitude) + ", " +
                      std::to_string(event.longitude) + "), " +
                      "有效点数: " + std::to_string(effectiveCount));

    return true;
}
```

#### 步骤 3: 更新头文件

**文件**: `include/esdk_sophon/task/TaskService.h`

```cpp
private:
    /**
     * @brief 计算检测框的 GPS 坐标
     *
     * 调用 GeoDecodeAPI 将图像坐标转换为实际 GPS 坐标。
     *
     * @param frame 原始图像
     * @param boxes 检测框列表
     * @param event 输出参数：填充 GPS 坐标和 points 数组
     * @return true 成功
     * @return false 失败（API 错误）
     */
    bool calculateGPSCoordinates(const cv::Mat& frame,
                                const std::vector<BoundingBox>& boxes,
                                DetectionEvent& event);
```

---

### 方案 B: 像素偏移估算（临时方案）

**优点**: 不依赖外部服务，快速实现
**缺点**: 精度较低，只适合测试

**实现**: 参考前面提供的方案 B 代码

---

## 📋 实施计划

### Day 3 (2025-11-04)

- [ ] 创建 `HttpClient.h` 和 `HttpClient.cpp`
- [ ] 实现 `calculateGPSCoordinates()` 方法
- [ ] 更新 `buildEvent()` 调用逻辑
- [ ] 编写单元测试（Mock GeoDecodeAPI）

### Day 4 (2025-11-05)

- [ ] 测试 GeoDecodeAPI 集成
- [ ] 添加降级逻辑（API 失败时使用方案 B）
- [ ] 性能测试（评估 API 调用延迟）
- [ ] 更新文档和改进记录

---

## 🧪 测试方案

### 单元测试

```cpp
TEST_F(TaskServiceTest, CalculateGPSCoordinates_Success) {
    // 1. 准备测试数据
    cv::Mat frame(1080, 1920, CV_8UC3);
    std::vector<BoundingBox> boxes;
    BoundingBox box1;
    box1.x = 500; box1.y = 300; box1.w = 100; box1.h = 80;
    boxes.push_back(box1);

    DetectionEvent event;
    event.uuid = "test-uuid";

    // 2. Mock GeoDecodeAPI 响应
    // （需要在测试环境启动 Mock HTTP 服务器）

    // 3. 调用方法
    bool result = taskService->calculateGPSCoordinates(frame, boxes, event);

    // 4. 验证结果
    EXPECT_TRUE(result);
    EXPECT_GT(event.latitude, 0.0);
    EXPECT_GT(event.longitude, 0.0);
    EXPECT_EQ(event.points.size(), 1);
    EXPECT_GT(event.points[0].lat, 0.0);
}
```

### 集成测试

1. 启动 GeoDecodeAPI JAR 服务
2. 准备测试图片（包含 EXIF GPS 信息）
3. 运行完整的 `processFrame()` 流程
4. 验证上报的 MQTT 消息中包含正确的 GPS 坐标

---

## 📚 参考资料

- **旧项目代码**: `esdk_on_sophon_old/src/yolov10/image_processor_yolov10.cpp`
- **GeoDecodeAPI 文档**: （需要补充）
- **libcurl 文档**: https://curl.se/libcurl/c/
- **nlohmann::json 文档**: https://github.com/nlohmann/json

---

> **作者**: ESDK Sophon Team
> **最后更新**: 2025-11-03
