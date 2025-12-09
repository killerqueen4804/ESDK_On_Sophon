/**
 * @file GeoUtils.cpp
 * @brief 地理信息工具类实现
 *
 * 提供地理信息相关的所有功能，包括：
 * - GPS 坐标计算（HTTP 批量请求）
 * - 坐标系转换（WGS84/GCJ02/BD09）
 * - 距离计算（Haversine 公式、Vincenty 公式）
 * - 区域判断（点在多边形内、点在圆内等）
 * - 轨迹分析（轨迹长度、轨迹平滑、轨迹简化）
 *
 * @author ESDK Sophon Team
 * @date 2025-11-20
 */

#include "esdk_sophon/utils/GeoUtils.h"
#include "esdk_sophon/core/Logger.h"

// 标准库
#include <cmath>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <thread>
#include <future>
#include <chrono>

// JSON
#include <nlohmann/json.hpp>

namespace esdk_sophon {
namespace utils {

// ==================== 常量定义 ====================

// 地球半径（米）
constexpr double EARTH_RADIUS_M = 6371000.0;  // WGS84 平均半径

// WGS84 椭球参数
constexpr double WGS84_A = 6378137.0;          // 长半轴（米）
constexpr double WGS84_B = 6356752.314245;     // 短半轴（米）
constexpr double WGS84_F = 1.0 / 298.257223563;  // 扁率

// 坐标系转换参数（WGS84 → GCJ02）
constexpr double GCJ02_A = 6378245.0;
constexpr double GCJ02_EE = 0.00669342162296594323;  // (a^2 - b^2) / a^2

// 数学常量
constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;

// ==================== 构造/析构 ====================

GeoUtils::GeoUtils()
    : gpsServiceUrl_("http://127.0.0.1:8122/geoDecode/arithmetic/getLonLatByPoints")
    , httpTimeoutMs_(5000)
    , httpRetryCount_(3)
    , cacheEnabled_(false) {
    // logger_ is not a member of GeoUtils in header, using static Logger::getInstance()
    core::Logger::getInstance().info("🌍 GeoUtils 初始化完成");
}

// ==================== GPS 坐标计算 ====================

void GeoUtils::setGpsServiceUrl(const std::string& url) {
    gpsServiceUrl_ = url;
    core::Logger::getInstance().info("📍 GPS 计算服务 URL: " + url);
}

void GeoUtils::setHttpTimeout(int timeoutMs) {
    httpTimeoutMs_ = timeoutMs;
}

void GeoUtils::setHttpRetryCount(int retryCount) {
    httpRetryCount_ = retryCount;
}

void GeoUtils::enableCache(bool enable) {
    cacheEnabled_ = enable;
}

void GeoUtils::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    core::Logger::getInstance().info("�️ GPS 像素缓存已清空");
}

/**
 * @brief 简单的 HTTP POST 实现（使用 system() 调用 curl）
 */
std::optional<nlohmann::json> GeoUtils::httpPost(const std::string& url, const nlohmann::json& requestBody) const {
    // 创建临时文件
    std::string tempInputFile = "/tmp/geo_request_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".json";
    std::string tempOutputFile = "/tmp/geo_response_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".json";

    // 写入请求数据
    {
        std::ofstream ofs(tempInputFile);
        if (!ofs.is_open()) {
            return std::nullopt;
        }
        ofs << requestBody.dump();
    }

    // 构建 curl 命令
    std::ostringstream cmd;
    cmd << "curl -s -X POST "
        << "\"" << url << "\" "
        << "-H \"Content-Type: application/json\" "
        << "-d @" << tempInputFile << " "
        << "-o " << tempOutputFile << " "
        << "--connect-timeout " << (httpTimeoutMs_ / 1000) << " "
        << "--max-time " << (httpTimeoutMs_ / 1000 * 2);

    // 执行 curl
    int result = system(cmd.str().c_str());
    
    // 清理输入文件
    std::remove(tempInputFile.c_str());

    if (result != 0) {
        std::remove(tempOutputFile.c_str());
        return std::nullopt;
    }

    // 读取响应
    std::string responseStr;
    {
        std::ifstream ifs(tempOutputFile);
        if (!ifs.is_open()) {
            return std::nullopt;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        responseStr = ss.str();
    }

    // 清理输出文件
    std::remove(tempOutputFile.c_str());

    try {
        return nlohmann::json::parse(responseStr);
    } catch (...) {
        return std::nullopt;
    }
}

std::string GeoUtils::generateCacheKey(const GpsCoordinate& gps) const {
    return std::to_string(gps.latitude) + "," + std::to_string(gps.longitude);
}

PixelCoordinate GeoUtils::convertGpsToPixel(const GpsCoordinate& gps) const {
    core::Logger::getInstance().debug("📍 GPS → 像素: (" + std::to_string(gps.latitude) + ", " +
                 std::to_string(gps.longitude) + ")");

    // 1. 检查缓存
    if (cacheEnabled_) {
        std::string key = generateCacheKey(gps);
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
    }

    // 2. 构建请求
    nlohmann::json request;
    request["latitude"] = gps.latitude;
    request["longitude"] = gps.longitude;
    request["altitude"] = gps.altitude;
    request["heading"] = gps.heading;

    // 3. HTTP 请求（带重试）
    std::optional<nlohmann::json> response;
    for (int retry = 0; retry < httpRetryCount_; ++retry) {
        response = httpPost(gpsServiceUrl_, request);
        if (response) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    PixelCoordinate pixel;
    if (!response || !response->contains("x") || !response->contains("y")) {
        pixel.success = false;
        pixel.errorMsg = "Request failed or invalid response";
        return pixel;
    }

    pixel.x = (*response)["x"].get<double>();
    pixel.y = (*response)["y"].get<double>();
    pixel.success = true;

    // 4. 缓存结果
    if (cacheEnabled_) {
        std::string key = generateCacheKey(gps);
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.size() >= MAX_CACHE_SIZE) {
            cache_.erase(cache_.begin());
        }
        cache_[key] = pixel;
    }

    return pixel;
}

std::vector<PixelCoordinate> GeoUtils::convertGpsToPixelBatch(
    const std::vector<GpsCoordinate>& gpsCoords) const {
    
    core::Logger::getInstance().info("📍 批量 GPS → 像素: " + std::to_string(gpsCoords.size()) + " 个坐标");

    // 1. 构建批量请求
    nlohmann::json request = nlohmann::json::array();
    for (const auto& gps : gpsCoords) {
        nlohmann::json item;
        item["latitude"] = gps.latitude;
        item["longitude"] = gps.longitude;
        item["altitude"] = gps.altitude;
        item["heading"] = gps.heading;
        request.push_back(item);
    }

    // 2. HTTP 请求
    std::optional<nlohmann::json> response;
    for (int retry = 0; retry < httpRetryCount_; ++retry) {
        response = httpPost(gpsServiceUrl_, request);
        if (response) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::vector<PixelCoordinate> results;
    results.reserve(gpsCoords.size());

    if (!response || !response->is_array() || response->size() != gpsCoords.size()) {
        for (size_t i = 0; i < gpsCoords.size(); ++i) {
            PixelCoordinate p;
            p.success = false;
            p.errorMsg = "Batch request failed";
            results.push_back(p);
        }
        return results;
    }

    for (size_t i = 0; i < gpsCoords.size(); ++i) {
        PixelCoordinate pixel;
        const auto& item = (*response)[i];
        if (item.contains("x") && item.contains("y")) {
            pixel.x = item["x"].get<double>();
            pixel.y = item["y"].get<double>();
            pixel.success = true;
        } else {
            pixel.success = false;
            pixel.errorMsg = "Invalid item response";
        }
        results.push_back(pixel);

        if (cacheEnabled_ && pixel.success) {
            std::string key = generateCacheKey(gpsCoords[i]);
            std::lock_guard<std::mutex> lock(cacheMutex_);
            if (cache_.size() >= MAX_CACHE_SIZE) {
                cache_.erase(cache_.begin());
            }
            cache_[key] = pixel;
        }
    }

    return results;
}

// ==================== 距离计算 ====================

double GeoUtils::calculateDistance(const GpsCoordinate& from, const GpsCoordinate& to) const {
    // Haversine 公式
    double lat1 = from.latitude * DEG_TO_RAD;
    double lon1 = from.longitude * DEG_TO_RAD;
    double lat2 = to.latitude * DEG_TO_RAD;
    double lon2 = to.longitude * DEG_TO_RAD;

    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;

    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dlon / 2.0) * std::sin(dlon / 2.0);

    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    return EARTH_RADIUS_M * c;
}

double GeoUtils::calculateDistanceVincenty(const GpsCoordinate& from, const GpsCoordinate& to) const {
    // Vincenty 公式（高精度，考虑地球椭球）
    double lat1 = from.latitude * DEG_TO_RAD;
    double lon1 = from.longitude * DEG_TO_RAD;
    double lat2 = to.latitude * DEG_TO_RAD;
    double lon2 = to.longitude * DEG_TO_RAD;

    double U1 = std::atan((1.0 - WGS84_F) * std::tan(lat1));
    double U2 = std::atan((1.0 - WGS84_F) * std::tan(lat2));
    double L = lon2 - lon1;

    double sinU1 = std::sin(U1);
    double cosU1 = std::cos(U1);
    double sinU2 = std::sin(U2);
    double cosU2 = std::cos(U2);

    double lambda = L;
    double lambdaP = 2.0 * PI;
    int iterLimit = 100;

    double cosSqAlpha = 0.0, sinSigma = 0.0, cos2SigmaM = 0.0, cosSigma = 0.0, sigma = 0.0;
    double sinAlpha = 0.0; // Initialize

    while (std::abs(lambda - lambdaP) > 1e-12 && --iterLimit > 0) {
        double sinLambda = std::sin(lambda);
        double cosLambda = std::cos(lambda);

        sinSigma = std::sqrt((cosU2 * sinLambda) * (cosU2 * sinLambda) +
                            (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda) *
                            (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda));

        if (sinSigma == 0) return 0;  // 重合点

        cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        sigma = std::atan2(sinSigma, cosSigma);

        sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        cosSqAlpha = 1.0 - sinAlpha * sinAlpha;

        cos2SigmaM = cosSigma - 2.0 * sinU1 * sinU2 / cosSqAlpha;
        if (std::isnan(cos2SigmaM)) cos2SigmaM = 0;  // 赤道线

        double C = WGS84_F / 16.0 * cosSqAlpha * (4.0 + WGS84_F * (4.0 - 3.0 * cosSqAlpha));

        lambdaP = lambda;
        lambda = L + (1.0 - C) * WGS84_F * sinAlpha *
                 (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma *
                  (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)));
    }

    if (iterLimit == 0) {
        core::Logger::getInstance().warning("⚠️ Vincenty 公式未收敛，降级使用 Haversine");
        return calculateDistance(from, to);
    }

    double uSq = cosSqAlpha * (WGS84_A * WGS84_A - WGS84_B * WGS84_B) / (WGS84_B * WGS84_B);
    double A = 1.0 + uSq / 16384.0 * (4096.0 + uSq * (-768.0 + uSq * (320.0 - 175.0 * uSq)));
    double B = uSq / 1024.0 * (256.0 + uSq * (-128.0 + uSq * (74.0 - 47.0 * uSq)));

    double deltaSigma = B * sinSigma * (cos2SigmaM + B / 4.0 * (cosSigma *
                       (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM) -
                        B / 6.0 * cos2SigmaM * (-3.0 + 4.0 * sinSigma * sinSigma) *
                       (-3.0 + 4.0 * cos2SigmaM * cos2SigmaM)));

    return WGS84_B * A * (sigma - deltaSigma);
}

double GeoUtils::calculateBearing(const GpsCoordinate& from, const GpsCoordinate& to) const {
    double lat1 = from.latitude * DEG_TO_RAD;
    double lon1 = from.longitude * DEG_TO_RAD;
    double lat2 = to.latitude * DEG_TO_RAD;
    double lon2 = to.longitude * DEG_TO_RAD;

    double dlon = lon2 - lon1;

    double y = std::sin(dlon) * std::cos(lat2);
    double x = std::cos(lat1) * std::sin(lat2) -
               std::sin(lat1) * std::cos(lat2) * std::cos(dlon);

    double bearing = std::atan2(y, x) * RAD_TO_DEG;

    // 转换为 [0, 360) 范围
    return std::fmod(bearing + 360.0, 360.0);
}

GpsCoordinate GeoUtils::calculateDestination(const GpsCoordinate& from, double bearing, double distanceMeters) const {
    double lat1 = from.latitude * DEG_TO_RAD;
    double lon1 = from.longitude * DEG_TO_RAD;
    double brng = bearing * DEG_TO_RAD;

    double angularDistance = distanceMeters / EARTH_RADIUS_M;

    double lat2 = std::asin(std::sin(lat1) * std::cos(angularDistance) +
                           std::cos(lat1) * std::sin(angularDistance) * std::cos(brng));

    double lon2 = lon1 + std::atan2(std::sin(brng) * std::sin(angularDistance) * std::cos(lat1),
                                    std::cos(angularDistance) - std::sin(lat1) * std::sin(lat2));

    GpsCoordinate dest;
    dest.latitude = lat2 * RAD_TO_DEG;
    dest.longitude = lon2 * RAD_TO_DEG;
    dest.altitude = from.altitude;
    dest.heading = bearing;

    return dest;
}

// ==================== 区域判断 ====================

bool GeoUtils::isPointInPolygon(const GpsCoordinate& point, const Polygon& polygon) const {
    if (polygon.vertices.size() < 3) {
        core::Logger::getInstance().error("❌ 多边形顶点数量不足（需要至少 3 个）");
        return false;
    }

    // 射线法（Ray Casting Algorithm）
    bool inside = false;
    size_t j = polygon.vertices.size() - 1;

    for (size_t i = 0; i < polygon.vertices.size(); j = i++) {
        double xi = polygon.vertices[i].longitude;
        double yi = polygon.vertices[i].latitude;
        double xj = polygon.vertices[j].longitude;
        double yj = polygon.vertices[j].latitude;

        bool intersect = ((yi > point.latitude) != (yj > point.latitude)) &&
                        (point.longitude < (xj - xi) * (point.latitude - yi) / (yj - yi) + xi);

        if (intersect) {
            inside = !inside;
        }
    }

    return inside;
}

bool GeoUtils::isPointInCircle(const GpsCoordinate& point, const GpsCoordinate& center, double radiusMeters) const {
    double distance = calculateDistance(point, center);
    return distance <= radiusMeters;
}

bool GeoUtils::isPointInRectangle(const GpsCoordinate& point, const GpsCoordinate& topLeft, const GpsCoordinate& bottomRight) const {
    // 假设矩形是轴对齐的，且 topLeft 是西北角，bottomRight 是东南角
    // 注意：纬度北正南负，经度东正西负
    // topLeft.lat > bottomRight.lat
    // topLeft.lon < bottomRight.lon (除非跨越 180 度经线)
    
    double minLat = std::min(topLeft.latitude, bottomRight.latitude);
    double maxLat = std::max(topLeft.latitude, bottomRight.latitude);
    double minLon = std::min(topLeft.longitude, bottomRight.longitude);
    double maxLon = std::max(topLeft.longitude, bottomRight.longitude);

    return point.latitude >= minLat && point.latitude <= maxLat &&
           point.longitude >= minLon && point.longitude <= maxLon;
}

// ==================== 坐标系转换 ====================

/**
 * @brief 判断坐标是否在中国境内
 */
static bool isInChina(double lat, double lon) {
    return lat >= 0.8293 && lat <= 55.8271 && lon >= 72.004 && lon <= 137.8347;
}

/**
 * @brief WGS84 → GCJ02 转换辅助函数
 */
static void transformLatLon(double lat, double lon, double& dlat, double& dlon) {
    double dLat = -100.0 + 2.0 * lon + 3.0 * lat + 0.2 * lat * lat +
                  0.1 * lon * lat + 0.2 * std::sqrt(std::abs(lon));
    dLat += (20.0 * std::sin(6.0 * lon * PI) + 20.0 * std::sin(2.0 * lon * PI)) * 2.0 / 3.0;
    dLat += (20.0 * std::sin(lat * PI) + 40.0 * std::sin(lat / 3.0 * PI)) * 2.0 / 3.0;
    dLat += (160.0 * std::sin(lat / 12.0 * PI) + 320.0 * std::sin(lat * PI / 30.0)) * 2.0 / 3.0;

    double dLon = 300.0 + lon + 2.0 * lat + 0.1 * lon * lon +
                  0.1 * lon * lat + 0.1 * std::sqrt(std::abs(lon));
    dLon += (20.0 * std::sin(6.0 * lon * PI) + 20.0 * std::sin(2.0 * lon * PI)) * 2.0 / 3.0;
    dLon += (20.0 * std::sin(lon * PI) + 40.0 * std::sin(lon / 3.0 * PI)) * 2.0 / 3.0;
    dLon += (150.0 * std::sin(lon / 12.0 * PI) + 300.0 * std::sin(lon / 30.0 * PI)) * 2.0 / 3.0;

    double radLat = lat * DEG_TO_RAD;
    double magic = std::sin(radLat);
    magic = 1.0 - GCJ02_EE * magic * magic;
    double sqrtMagic = std::sqrt(magic);

    dlat = (dLat * 180.0) / ((GCJ02_A / sqrtMagic) * (1.0 - GCJ02_EE) / magic * PI);
    dlon = (dLon * 180.0) / (GCJ02_A / sqrtMagic * std::cos(radLat) * PI);
}

GpsCoordinate GeoUtils::wgs84ToGcj02(const GpsCoordinate& wgs84) const {
    if (!isInChina(wgs84.latitude, wgs84.longitude)) {
        // 中国境外，不需要转换
        return wgs84;
    }

    double dlat, dlon;
    transformLatLon(wgs84.latitude, wgs84.longitude, dlat, dlon);

    GpsCoordinate gcj02;
    gcj02.latitude = wgs84.latitude + dlat;
    gcj02.longitude = wgs84.longitude + dlon;
    gcj02.altitude = wgs84.altitude;
    gcj02.heading = wgs84.heading;

    return gcj02;
}

GpsCoordinate GeoUtils::gcj02ToWgs84(const GpsCoordinate& gcj02) const {
    if (!isInChina(gcj02.latitude, gcj02.longitude)) {
        return gcj02;
    }

    // 迭代逼近
    GpsCoordinate wgs84 = gcj02;
    for (int i = 0; i < 10; ++i) {
        GpsCoordinate temp = wgs84ToGcj02(wgs84);
        wgs84.latitude = wgs84.latitude - (temp.latitude - gcj02.latitude);
        wgs84.longitude = wgs84.longitude - (temp.longitude - gcj02.longitude);
    }

    wgs84.altitude = gcj02.altitude;
    wgs84.heading = gcj02.heading;

    return wgs84;
}

GpsCoordinate GeoUtils::gcj02ToBd09(const GpsCoordinate& gcj02) const {
    double x = gcj02.longitude;
    double y = gcj02.latitude;
    double z = std::sqrt(x * x + y * y) + 0.00002 * std::sin(y * PI * 3000.0 / 180.0);
    double theta = std::atan2(y, x) + 0.000003 * std::cos(x * PI * 3000.0 / 180.0);

    GpsCoordinate bd09;
    bd09.longitude = z * std::cos(theta) + 0.0065;
    bd09.latitude = z * std::sin(theta) + 0.006;
    bd09.altitude = gcj02.altitude;
    bd09.heading = gcj02.heading;

    return bd09;
}

GpsCoordinate GeoUtils::bd09ToGcj02(const GpsCoordinate& bd09) const {
    double x = bd09.longitude - 0.0065;
    double y = bd09.latitude - 0.006;
    double z = std::sqrt(x * x + y * y) - 0.00002 * std::sin(y * PI * 3000.0 / 180.0);
    double theta = std::atan2(y, x) - 0.000003 * std::cos(x * PI * 3000.0 / 180.0);

    GpsCoordinate gcj02;
    gcj02.longitude = z * std::cos(theta);
    gcj02.latitude = z * std::sin(theta);
    gcj02.altitude = bd09.altitude;
    gcj02.heading = bd09.heading;

    return gcj02;
}

GpsCoordinate GeoUtils::convertCoordinate(
    const GpsCoordinate& coord,
    CoordinateSystem from,
    CoordinateSystem to) const {
    
    if (from == to) {
        return coord;
    }

    // 两步转换：from → WGS84 → to
    GpsCoordinate wgs84 = coord;

    // Step 1: from → WGS84
    if (from == CoordinateSystem::GCJ02) {
        wgs84 = gcj02ToWgs84(coord);
    } else if (from == CoordinateSystem::BD09) {
        GpsCoordinate gcj02 = bd09ToGcj02(coord);
        wgs84 = gcj02ToWgs84(gcj02);
    }

    // Step 2: WGS84 → to
    if (to == CoordinateSystem::WGS84) {
        return wgs84;
    } else if (to == CoordinateSystem::GCJ02) {
        return wgs84ToGcj02(wgs84);
    } else if (to == CoordinateSystem::BD09) {
        GpsCoordinate gcj02 = wgs84ToGcj02(wgs84);
        return gcj02ToBd09(gcj02);
    }

    return wgs84;
}

// ==================== 轨迹分析 ====================

double GeoUtils::calculateTrajectoryLength(const std::vector<GpsCoordinate>& trajectory) const {
    if (trajectory.size() < 2) {
        return 0.0;
    }

    double totalLength = 0.0;
    for (size_t i = 1; i < trajectory.size(); ++i) {
        totalLength += calculateDistance(trajectory[i - 1], trajectory[i]);
    }

    return totalLength;
}

double GeoUtils::calculateAverageSpeed(const std::vector<GpsCoordinate>& trajectory, double timeIntervalSeconds) const {
    if (trajectory.size() < 2 || timeIntervalSeconds <= 0) {
        return 0.0;
    }
    double length = calculateTrajectoryLength(trajectory);
    double totalTime = (trajectory.size() - 1) * timeIntervalSeconds;
    return length / totalTime;
}

std::vector<GpsCoordinate> GeoUtils::smoothTrajectory(
    const std::vector<GpsCoordinate>& trajectory,
    int windowSize) const {
    
    if (trajectory.size() < static_cast<size_t>(windowSize) || windowSize < 2) {
        return trajectory;
    }

    std::vector<GpsCoordinate> smoothed;
    smoothed.reserve(trajectory.size());

    int halfWindow = windowSize / 2;

    for (size_t i = 0; i < trajectory.size(); ++i) {
        int start = std::max(0, static_cast<int>(i) - halfWindow);
        int end = std::min(static_cast<int>(trajectory.size()), static_cast<int>(i) + halfWindow + 1);

        double sumLat = 0.0;
        double sumLon = 0.0;
        double sumAlt = 0.0;

        for (int j = start; j < end; ++j) {
            sumLat += trajectory[j].latitude;
            sumLon += trajectory[j].longitude;
            sumAlt += trajectory[j].altitude;
        }

        int count = end - start;
        GpsCoordinate smoothPoint;
        smoothPoint.latitude = sumLat / count;
        smoothPoint.longitude = sumLon / count;
        smoothPoint.altitude = sumAlt / count;
        smoothPoint.heading = trajectory[i].heading;

        smoothed.push_back(smoothPoint);
    }

    return smoothed;
}

std::vector<GpsCoordinate> GeoUtils::simplifyTrajectory(
    const std::vector<GpsCoordinate>& trajectory,
    double toleranceMeters) const {
    
    if (trajectory.size() < 3 || toleranceMeters <= 0.0) {
        return trajectory;
    }

    // Douglas-Peucker 算法
    std::function<void(int, int, std::vector<bool>&)> douglasPeucker =
        [&](int start, int end, std::vector<bool>& keep) {
            if (end - start < 2) {
                return;
            }

            // 找到距离直线最远的点
            double maxDistance = 0.0;
            int maxIndex = start;

            for (int i = start + 1; i < end; ++i) {
                // 计算点到直线的距离
                double d = perpendicularDistance(trajectory[i], trajectory[start], trajectory[end]);
                if (d > maxDistance) {
                    maxDistance = d;
                    maxIndex = i;
                }
            }

            if (maxDistance > toleranceMeters) {
                keep[maxIndex] = true;
                douglasPeucker(start, maxIndex, keep);
                douglasPeucker(maxIndex, end, keep);
            }
        };

    std::vector<bool> keep(trajectory.size(), false);
    keep[0] = true;
    keep[trajectory.size() - 1] = true;

    douglasPeucker(0, trajectory.size() - 1, keep);

    std::vector<GpsCoordinate> simplified;
    for (size_t i = 0; i < trajectory.size(); ++i) {
        if (keep[i]) {
            simplified.push_back(trajectory[i]);
        }
    }

    core::Logger::getInstance().info("🛤️ 轨迹简化: " + std::to_string(trajectory.size()) + " → " +
                std::to_string(simplified.size()) + " 点");

    return simplified;
}

double GeoUtils::perpendicularDistance(
    const GpsCoordinate& point,
    const GpsCoordinate& lineStart,
    const GpsCoordinate& lineEnd) const {
    
    // 计算点到直线的垂直距离（简化为平面几何）
    double x = point.longitude;
    double y = point.latitude;
    double x1 = lineStart.longitude;
    double y1 = lineStart.latitude;
    double x2 = lineEnd.longitude;
    double y2 = lineEnd.latitude;

    double A = x - x1;
    double B = y - y1;
    double C = x2 - x1;
    double D = y2 - y1;

    double dot = A * C + B * D;
    double lenSq = C * C + D * D;

    if (lenSq == 0.0) {
        // lineStart 和 lineEnd 重合
        return calculateDistance(point, lineStart);
    }

    double param = dot / lenSq;

    double xx, yy;

    if (param < 0.0) {
        xx = x1;
        yy = y1;
    } else if (param > 1.0) {
        xx = x2;
        yy = y2;
    } else {
        xx = x1 + param * C;
        yy = y1 + param * D;
    }

    GpsCoordinate nearest;
    nearest.latitude = yy;
    nearest.longitude = xx;

    return calculateDistance(point, nearest);
}

double GeoUtils::toRadians(double degrees) const {
    return degrees * DEG_TO_RAD;
}

double GeoUtils::toDegrees(double radians) const {
    return radians * RAD_TO_DEG;
}

bool GeoUtils::isValidCoordinate(const GpsCoordinate& coord) const {
    return coord.latitude >= -90.0 && coord.latitude <= 90.0 &&
           coord.longitude >= -180.0 && coord.longitude <= 180.0;
}

std::string GeoUtils::formatCoordinate(const GpsCoordinate& coord, int precision) const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << coord.latitude << ", " << coord.longitude;
    return ss.str();
}

}  // namespace utils
}  // namespace esdk_sophon
