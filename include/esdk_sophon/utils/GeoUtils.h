/**
 * @file GeoUtils.h
 * @brief 地理信息处理工具类
 * 
 * 集中管理地理信息相关的所有操作，包括：
 * - GPS 坐标转换（WGS84、GCJ02、BD09）
 * - GPS 坐标计算（经纬度 → 像素坐标）
 * - 距离计算（Haversine、Vincenty）
 * - 区域判断（点是否在多边形内）
 * - 轨迹分析（速度、方向、轨迹平滑）
 * - 坐标系转换
 * 
 * 设计理念：
 * - 高内聚：所有地理信息功能聚合在一个类中
 * - 易扩展：新增地理计算功能只需在此类添加方法
 * - 单例模式：全局唯一实例，避免重复初始化
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-20
 */

#ifndef ESDK_SOPHON_UTILS_GEO_UTILS_H_
#define ESDK_SOPHON_UTILS_GEO_UTILS_H_

#include <string>
#include <vector>
#include <optional>
#include <cmath>
#include <mutex>
#include <nlohmann/json.hpp>

namespace esdk_sophon {
namespace utils {

// ==================== 地理信息结构 ====================

/**
 * @brief GPS 坐标（经纬度）
 */
struct GpsCoordinate {
    double latitude{0.0};      ///< 纬度（度，-90 到 90）
    double longitude{0.0};     ///< 经度（度，-180 到 180）
    double altitude{0.0};      ///< 海拔（米）
    double heading{0.0};       ///< 航向角（度，0-360，0=正北）
    
    /**
     * @brief 转换为 JSON
     */
    nlohmann::json toJson() const {
        return {
            {"latitude", latitude},
            {"longitude", longitude},
            {"altitude", altitude},
            {"heading", heading}
        };
    }
    
    /**
     * @brief 从 JSON 解析
     */
    static GpsCoordinate fromJson(const nlohmann::json& j) {
        GpsCoordinate coord;
        coord.latitude = j.value("latitude", 0.0);
        coord.longitude = j.value("longitude", 0.0);
        coord.altitude = j.value("altitude", 0.0);
        coord.heading = j.value("heading", 0.0);
        return coord;
    }
};

/**
 * @brief 像素坐标（屏幕/图片坐标系）
 */
struct PixelCoordinate {
    double x{0.0};             ///< X 坐标（像素）
    double y{0.0};             ///< Y 坐标（像素）
    bool success{false};       ///< 计算是否成功
    std::string errorMsg;      ///< 错误信息
    
    /**
     * @brief 转换为 JSON
     */
    nlohmann::json toJson() const {
        return {
            {"x", x},
            {"y", y},
            {"success", success}
        };
    }
    
    /**
     * @brief 从 JSON 解析
     */
    static PixelCoordinate fromJson(const nlohmann::json& j) {
        PixelCoordinate coord;
        coord.x = j.value("x", 0.0);
        coord.y = j.value("y", 0.0);
        coord.success = j.value("success", false);
        return coord;
    }
};

/**
 * @brief 坐标系类型
 */
enum class CoordinateSystem {
    WGS84,      ///< WGS84 坐标系（GPS 原始坐标）
    GCJ02,      ///< GCJ02 坐标系（国测局坐标，高德、腾讯）
    BD09        ///< BD09 坐标系（百度坐标）
};

/**
 * @brief 多边形区域
 */
struct Polygon {
    std::vector<GpsCoordinate> vertices;  ///< 顶点列表
    
    /**
     * @brief 添加顶点
     */
    void addVertex(double lat, double lon) {
        vertices.push_back({lat, lon, 0.0, 0.0});
    }
    
    /**
     * @brief 顶点数量
     */
    size_t size() const { return vertices.size(); }
};

// ==================== 地理工具类 ====================

/**
 * @brief 地理信息处理工具类（单例）
 * 
 * 用法示例:
 * @code
 * auto& geo = GeoUtils::getInstance();
 * 
 * // 1. GPS 坐标转像素坐标（HTTP 批量请求）
 * std::vector<GpsCoordinate> gpsCoords = { ... };
 * auto pixelCoords = geo.convertGpsToPixelBatch(gpsCoords);
 * 
 * // 2. 计算两点距离
 * double distance = geo.calculateDistance(
 *     {23.123, 113.456}, 
 *     {23.234, 113.567}
 * );
 * 
 * // 3. 判断点是否在区域内
 * Polygon area;
 * area.addVertex(23.1, 113.1);
 * area.addVertex(23.2, 113.2);
 * area.addVertex(23.2, 113.1);
 * bool inside = geo.isPointInPolygon({23.15, 113.15}, area);
 * 
 * // 4. 坐标系转换
 * auto gcj02 = geo.convertCoordinate(
 *     {23.123, 113.456}, 
 *     CoordinateSystem::WGS84, 
 *     CoordinateSystem::GCJ02
 * );
 * @endcode
 * 
 * 线程安全性: ✅ 线程安全
 * - 所有方法都是 const 或线程安全
 * - 可在多线程环境中安全使用
 */
class GeoUtils {
public:
    /**
     * @brief 获取单例实例
     */
    static GeoUtils& getInstance() {
        static GeoUtils instance;
        return instance;
    }
    
    // 禁止拷贝和赋值
    GeoUtils(const GeoUtils&) = delete;
    GeoUtils& operator=(const GeoUtils&) = delete;
    
    // ==================== GPS 坐标转像素坐标 ====================
    
    /**
     * @brief 配置 GPS 计算服务 URL
     * 
     * @param url HTTP 服务地址（如 "http://gps-service:8080/api/gps/calculate"）
     * 
     * @note 必须在使用 GPS 转换功能前调用
     */
    void setGpsServiceUrl(const std::string& url);
    
    /**
     * @brief GPS 坐标转像素坐标（单个）
     * 
     * @param gps GPS 坐标
     * @return PixelCoordinate 像素坐标
     * 
     * @note 内部调用 HTTP 服务
     */
    PixelCoordinate convertGpsToPixel(const GpsCoordinate& gps) const;
    
    /**
     * @brief GPS 坐标转像素坐标（批量）⭐ 推荐
     * 
     * @param gpsCoords GPS 坐标列表
     * @return std::vector<PixelCoordinate> 像素坐标列表
     * 
     * @note 批量请求性能更好，减少 HTTP 往返次数
     * 
     * 示例:
     * @code
     * std::vector<GpsCoordinate> gpsCoords = {
     *     {23.123, 113.456, 100, 45},
     *     {23.234, 113.567, 120, 90}
     * };
     * auto pixelCoords = geo.convertGpsToPixelBatch(gpsCoords);
     * for (size_t i = 0; i < pixelCoords.size(); ++i) {
     *     if (pixelCoords[i].success) {
     *         std::cout << "(" << pixelCoords[i].x << ", " 
     *                   << pixelCoords[i].y << ")" << std::endl;
     *     }
     * }
     * @endcode
     */
    std::vector<PixelCoordinate> convertGpsToPixelBatch(
        const std::vector<GpsCoordinate>& gpsCoords) const;
    
    /**
     * @brief 设置 HTTP 超时时间
     * 
     * @param timeoutMs 超时时间（毫秒，默认 5000）
     */
    void setHttpTimeout(int timeoutMs);
    
    /**
     * @brief 设置 HTTP 重试次数
     * 
     * @param retryCount 重试次数（默认 3）
     */
    void setHttpRetryCount(int retryCount);
    
    /**
     * @brief 启用结果缓存
     * 
     * @param enable 是否启用（默认 false）
     * 
     * @note 缓存可避免重复计算，但会占用内存
     */
    void enableCache(bool enable);
    
    /**
     * @brief 清空缓存
     */
    void clearCache();
    
    // ==================== 距离计算 ====================
    
    /**
     * @brief 计算两点间距离（Haversine 公式）
     * 
     * @param p1 点 1
     * @param p2 点 2
     * @return double 距离（米）
     * 
     * @note 假设地球为球体，误差 < 0.5%
     * 
     * 示例:
     * @code
     * double distance = geo.calculateDistance(
     *     {23.123, 113.456}, 
     *     {23.234, 113.567}
     * );
     * std::cout << "距离: " << distance << " 米" << std::endl;
     * @endcode
     */
    double calculateDistance(const GpsCoordinate& p1, 
                            const GpsCoordinate& p2) const;
    
    /**
     * @brief 计算两点间距离（Vincenty 公式，高精度）
     * 
     * @param p1 点 1
     * @param p2 点 2
     * @return double 距离（米）
     * 
     * @note 考虑地球椭球体，误差 < 0.01%，计算较慢
     */
    double calculateDistanceVincenty(const GpsCoordinate& p1,
                                    const GpsCoordinate& p2) const;
    
    /**
     * @brief 计算方位角（从 p1 到 p2）
     * 
     * @param p1 起点
     * @param p2 终点
     * @return double 方位角（度，0-360，0=正北）
     * 
     * 示例:
     * @code
     * double bearing = geo.calculateBearing(
     *     {23.123, 113.456}, 
     *     {23.234, 113.567}
     * );
     * @endcode
     */
    double calculateBearing(const GpsCoordinate& p1,
                           const GpsCoordinate& p2) const;
    
    /**
     * @brief 根据起点、方位角、距离计算终点
     * 
     * @param start 起点
     * @param bearing 方位角（度）
     * @param distanceMeters 距离（米）
     * @return GpsCoordinate 终点坐标
     */
    GpsCoordinate calculateDestination(const GpsCoordinate& start,
                                      double bearing,
                                      double distanceMeters) const;
    
    // ==================== 区域判断 ====================
    
    /**
     * @brief 判断点是否在多边形内（射线法）
     * 
     * @param point 待判断点
     * @param polygon 多边形区域
     * @return true 点在多边形内
     * 
     * 示例:
     * @code
     * Polygon area;
     * area.addVertex(23.1, 113.1);
     * area.addVertex(23.2, 113.2);
     * area.addVertex(23.2, 113.1);
     * 
     * bool inside = geo.isPointInPolygon({23.15, 113.15}, area);
     * @endcode
     */
    bool isPointInPolygon(const GpsCoordinate& point,
                         const Polygon& polygon) const;
    
    /**
     * @brief 判断点是否在圆形区域内
     * 
     * @param point 待判断点
     * @param center 圆心
     * @param radiusMeters 半径（米）
     * @return true 点在圆形区域内
     */
    bool isPointInCircle(const GpsCoordinate& point,
                        const GpsCoordinate& center,
                        double radiusMeters) const;
    
    /**
     * @brief 判断点是否在矩形区域内
     * 
     * @param point 待判断点
     * @param topLeft 左上角
     * @param bottomRight 右下角
     * @return true 点在矩形区域内
     */
    bool isPointInRectangle(const GpsCoordinate& point,
                           const GpsCoordinate& topLeft,
                           const GpsCoordinate& bottomRight) const;
    
    // ==================== 坐标系转换 ====================
    
    /**
     * @brief 坐标系转换
     * 
     * @param coord 原始坐标
     * @param from 源坐标系
     * @param to 目标坐标系
     * @return GpsCoordinate 转换后的坐标
     * 
     * 示例:
     * @code
     * // WGS84 (GPS) → GCJ02 (高德)
     * auto gcj02 = geo.convertCoordinate(
     *     {23.123, 113.456}, 
     *     CoordinateSystem::WGS84, 
     *     CoordinateSystem::GCJ02
     * );
     * @endcode
     */
    GpsCoordinate convertCoordinate(const GpsCoordinate& coord,
                                   CoordinateSystem from,
                                   CoordinateSystem to) const;
    
    /**
     * @brief WGS84 → GCJ02（火星坐标）
     */
    GpsCoordinate wgs84ToGcj02(const GpsCoordinate& wgs84) const;
    
    /**
     * @brief GCJ02 → WGS84
     */
    GpsCoordinate gcj02ToWgs84(const GpsCoordinate& gcj02) const;
    
    /**
     * @brief GCJ02 → BD09（百度坐标）
     */
    GpsCoordinate gcj02ToBd09(const GpsCoordinate& gcj02) const;
    
    /**
     * @brief BD09 → GCJ02
     */
    GpsCoordinate bd09ToGcj02(const GpsCoordinate& bd09) const;
    
    // ==================== 轨迹分析 ====================
    
    /**
     * @brief 计算轨迹总长度
     * 
     * @param trajectory 轨迹点列表
     * @return double 总长度（米）
     */
    double calculateTrajectoryLength(const std::vector<GpsCoordinate>& trajectory) const;
    
    /**
     * @brief 计算平均速度
     * 
     * @param trajectory 轨迹点列表
     * @param timeIntervalSeconds 采样时间间隔（秒）
     * @return double 平均速度（米/秒）
     */
    double calculateAverageSpeed(const std::vector<GpsCoordinate>& trajectory,
                                double timeIntervalSeconds) const;
    
    /**
     * @brief 轨迹平滑（移动平均）
     * 
     * @param trajectory 原始轨迹
     * @param windowSize 平滑窗口大小
     * @return std::vector<GpsCoordinate> 平滑后的轨迹
     * 
     * @note 用于消除 GPS 噪声
     */
    std::vector<GpsCoordinate> smoothTrajectory(
        const std::vector<GpsCoordinate>& trajectory,
        int windowSize = 5) const;
    
    /**
     * @brief 轨迹简化（Douglas-Peucker 算法）
     * 
     * @param trajectory 原始轨迹
     * @param toleranceMeters 容差（米）
     * @return std::vector<GpsCoordinate> 简化后的轨迹
     * 
     * @note 减少轨迹点数量，保持形状
     */
    std::vector<GpsCoordinate> simplifyTrajectory(
        const std::vector<GpsCoordinate>& trajectory,
        double toleranceMeters = 5.0) const;
    
    // ==================== 工具方法 ====================
    
    /**
     * @brief 验证经纬度是否合法
     * 
     * @param coord GPS 坐标
     * @return true 合法
     */
    bool isValidCoordinate(const GpsCoordinate& coord) const;
    
    /**
     * @brief 格式化 GPS 坐标为字符串
     * 
     * @param coord GPS 坐标
     * @param precision 小数精度（默认 6 位）
     * @return std::string 格式化字符串（如 "23.123456, 113.456789"）
     */
    std::string formatCoordinate(const GpsCoordinate& coord, int precision = 6) const;

private:
    GeoUtils();
    ~GeoUtils() = default;
    
    // ==================== 私有成员 ====================
    
    std::string gpsServiceUrl_;        ///< GPS 计算服务 URL
    int httpTimeoutMs_{5000};          ///< HTTP 超时（毫秒）
    int httpRetryCount_{3};            ///< HTTP 重试次数
    bool cacheEnabled_{false};         ///< 是否启用缓存
    
    // LRU 缓存（GPS → Pixel）
    mutable std::unordered_map<std::string, PixelCoordinate> cache_;
    mutable std::mutex cacheMutex_;
    static constexpr size_t MAX_CACHE_SIZE = 1000;
    
    // ==================== 私有辅助方法 ====================
    
    /**
     * @brief HTTP POST 请求
     */
    std::optional<nlohmann::json> httpPost(const std::string& url,
                                          const nlohmann::json& requestBody) const;
    
    /**
     * @brief 生成缓存键
     */
    std::string generateCacheKey(const GpsCoordinate& gps) const;
    
    /**
     * @brief 角度转弧度
     */
    double toRadians(double degrees) const;
    
    /**
     * @brief 弧度转角度
     */
    double toDegrees(double radians) const;

    /**
     * @brief 计算点到直线的垂直距离
     */
    double perpendicularDistance(const GpsCoordinate& point,
                               const GpsCoordinate& lineStart,
                               const GpsCoordinate& lineEnd) const;
    
    /**
     * @brief 地球半径（米）
     */
    static constexpr double EARTH_RADIUS = 6371000.0;
    
    /**
     * @brief WGS84 椭球体参数
     */
    static constexpr double WGS84_A = 6378137.0;      // 长半轴
    static constexpr double WGS84_F = 1.0 / 298.257223563;  // 扁率
};

}  // namespace utils
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_UTILS_GEO_UTILS_H_
