/**
 * @file FlightDataProvider.h
 * @brief 飞行数据提供者 - 提供飞行器实时状态数据的访问接口
 * 
 * @details
 * FlightDataProvider 是一个线程安全的单例类，负责存储和提供飞行器的实时状态数据。
 * 数据来源于 DJI Cloud API 的 OSD (On-Screen Display) 推送。
 * 
 * OSD 数据通过 MQTT Topic 推送:
 * - 飞行器 OSD: `thing/product/{aircraft_sn}/osd`
 * - 推送频率: 0.5Hz
 * 
 * 主要功能:
 * 1. 存储飞行器 GPS 位置 (latitude, longitude, height)
 * 2. 存储飞行器姿态 (attitude_head, attitude_pitch, attitude_roll)
 * 3. 存储飞行器速度 (horizontal_speed, vertical_speed)
 * 4. 提供线程安全的读写接口
 * 
 * 设计模式:
 * - **单例模式**: 全局唯一数据提供者
 * - **观察者模式**: 可选的数据更新回调
 * 
 * 使用示例:
 * @code
 * // 在 MqttHandler 中更新数据
 * FlightDataProvider::getInstance().updateGpsPosition(lat, lon, height);
 * 
 * // 在 LiveStreamTask 中获取数据
 * auto gps = FlightDataProvider::getInstance().getGpsPosition();
 * logger_.info("GPS: " + std::to_string(gps.latitude) + ", " + std::to_string(gps.longitude));
 * @endcode
 * 
 * 📌 面试考点:
 * - 单例模式的线程安全实现 (Meyers 单例)
 * - std::mutex 与 std::shared_mutex 的选择
 * - std::atomic 在简单数据类型中的应用
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-28
 */

#ifndef ESDK_SOPHON_DEVICE_FLIGHT_DATA_PROVIDER_H_
#define ESDK_SOPHON_DEVICE_FLIGHT_DATA_PROVIDER_H_

#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <cmath>

namespace esdk_sophon {
namespace device {

/**
 * @brief GPS 位置信息结构体
 * 
 * 存储飞行器的 WGS-84 坐标系位置信息。
 * 
 * @note 字段来源于 DJI Cloud API OSD 数据
 */
struct GpsPosition {
    double latitude{0.0};    ///< 纬度 (-90 ~ 90 度)
    double longitude{0.0};   ///< 经度 (-180 ~ 180 度)
    double height{0.0};      ///< 椭球高度 (米)
    double altitude{0.0};    ///< 海拔高度 (米)，相对起飞点
    
    /**
     * @brief 计算与另一个点的水平距离（Haversine 公式）
     * 
     * @param other 另一个 GPS 坐标点
     * @return double 距离 (米)
     * 
     * 📌 面试考点: Haversine 公式计算球面距离
     */
    double distanceTo(const GpsPosition& other) const {
        constexpr double R = 6371000.0;  // 地球半径 (米)
        
        // 转换为弧度
        double lat1 = latitude * M_PI / 180.0;
        double lat2 = other.latitude * M_PI / 180.0;
        double deltaLat = (other.latitude - latitude) * M_PI / 180.0;
        double deltaLon = (other.longitude - longitude) * M_PI / 180.0;
        
        // Haversine 公式
        double a = std::sin(deltaLat / 2) * std::sin(deltaLat / 2) +
                   std::cos(lat1) * std::cos(lat2) *
                   std::sin(deltaLon / 2) * std::sin(deltaLon / 2);
        double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
        
        return R * c;
    }
    
    /**
     * @brief 检查坐标是否有效
     * 
     * @return true 坐标在有效范围内
     * @return false 坐标无效 (可能未初始化)
     */
    bool isValid() const {
        return latitude >= -90.0 && latitude <= 90.0 &&
               longitude >= -180.0 && longitude <= 180.0 &&
               latitude != 0.0 && longitude != 0.0;  // 排除默认值
    }
};

/**
 * @brief 飞行器姿态信息结构体
 * 
 * 存储飞行器的欧拉角姿态信息。
 */
struct Attitude {
    double head{0.0};    ///< 机头朝向/偏航角 (yaw), 单位: 度
    double pitch{0.0};   ///< 俯仰角, 单位: 度
    double roll{0.0};    ///< 横滚角, 单位: 度
};

/**
 * @brief 飞行器速度信息结构体
 */
struct Velocity {
    double horizontal{0.0};  ///< 水平速度 (m/s)
    double vertical{0.0};    ///< 垂直速度 (m/s)
};

/**
 * @brief 飞行模式枚举
 * 
 * 根据 DJI Cloud API 文档的 mode_code 字段定义。
 * 用于判断无人机当前的飞行状态。
 * 
 * @note 字段来源: thing/product/{aircraft_sn}/osd -> data.mode_code
 * @see https://developer.dji.com/doc/cloud-api-tutorial/cn/server-api-reference/
 */
enum class FlightMode {
    kStandby = 0,           ///< 待机 (Standby)
    kTakeoffPreparation = 1,///< 起飞准备 (Takeoff preparation)
    kTakeoffReady = 2,      ///< 起飞就绪 (Takeoff ready, not mentioned in doc)
    kManual = 3,            ///< 手动控制 (Manual flying, not mentioned in doc)
    kAutoTakeoff = 4,       ///< 自动起飞 (Automatic takeoff)
    kWaypointFlight = 5,    ///< 航线飞行 (Wayline flying)
    kPanoramaPhoto = 6,     ///< 全景拍摄 (Panoramic photographing)
    kIntelligentTracking = 7,///< 智能跟踪 (Intelligent tracking)
    kAdsb = 8,              ///< ADS-B 避让 (ADS-B avoidance)
    kAutoRth = 9,           ///< 自动返航 (Automatic RTH) ⚠️ 关键状态
    kAutoLanding = 10,      ///< 自动降落 (Automatic landing) ⚠️ 关键状态
    kForcedLanding = 11,    ///< 强制降落 (Forced landing) ⚠️ 关键状态
    kThreePropellerEmergency = 12, ///< 三桨紧急模式 (Three-propeller emergency landing)
    kUpgrading = 13,        ///< 升级中 (Upgrading)
    kDisconnected = 14,     ///< 失联 (Not connected)
    kApasManeuver = 15,     ///< APAS 规避 (APAS maneuver)
    kVirtualStickFlight = 16,///< 虚拟摇杆 (Virtual stick state)
    kLiveFlightControl = 17, ///< 实时飞行控制 (Live Flight Controls)
    kUnknown = 255          ///< 未知状态 (Unknown)
};

/**
 * @brief 获取飞行模式的可读名称
 * 
 * @param mode 飞行模式枚举
 * @return std::string 中文名称
 */
inline std::string flightModeToString(FlightMode mode) {
    switch (mode) {
        case FlightMode::kStandby: return "待机";
        case FlightMode::kTakeoffPreparation: return "起飞准备";
        case FlightMode::kTakeoffReady: return "起飞就绪";
        case FlightMode::kManual: return "手动控制";
        case FlightMode::kAutoTakeoff: return "自动起飞";
        case FlightMode::kWaypointFlight: return "航线飞行";
        case FlightMode::kPanoramaPhoto: return "全景拍摄";
        case FlightMode::kIntelligentTracking: return "智能跟踪";
        case FlightMode::kAdsb: return "ADS-B避让";
        case FlightMode::kAutoRth: return "自动返航";
        case FlightMode::kAutoLanding: return "自动降落";
        case FlightMode::kForcedLanding: return "强制降落";
        case FlightMode::kThreePropellerEmergency: return "三桨紧急模式";
        case FlightMode::kUpgrading: return "升级中";
        case FlightMode::kDisconnected: return "失联";
        case FlightMode::kApasManeuver: return "APAS规避";
        case FlightMode::kVirtualStickFlight: return "虚拟摇杆";
        case FlightMode::kLiveFlightControl: return "实时飞行控制";
        default: return "未知状态";
    }
}

/**
 * @brief 航线任务状态枚举
 * 
 * 根据 DJI Cloud API 文档的 wayline_mission_state 字段定义。
 * 用于判断航线任务的执行阶段，决定是否可以启用智能变焦拍照。
 * 
 * @note 字段来源: thing/product/{gateway_sn}/events -> flighttask_progress
 *       -> data.output.ext.wayline_mission_state
 * 
 * 📌 关键状态:
 * - kEnteringFirstWaypoint (5): 进入航线，到第一个航点 → 开始启用智能拍照
 * - kExecuting (6): 航线执行中 → 继续智能拍照
 * - kInterrupted (7): 航线中断 → 暂停智能拍照
 */
enum class WaylineMissionState {
    kDisconnected = 0,      ///< 断连
    kNotSupported = 1,      ///< 不支持该航点
    kReady = 2,             ///< 航线准备状态，可上传文件
    kUploading = 3,         ///< 航线文件上传中
    kPreparing = 4,         ///< 触发开始命令，飞行器准备中
    kEnteringFirstWaypoint = 5, ///< ⭐ 进入航线，到第一个航点
    kExecuting = 6,         ///< ⭐ 航线执行中
    kInterrupted = 7,       ///< 航线中断（用户暂停或飞控异常）
    kRecovering = 8,        ///< 航线恢复中
    kStopped = 9,           ///< 航线停止
    kUnknown = 255          ///< 未知状态
};

/**
 * @brief 获取航线任务状态的可读名称
 * 
 * @param state 航线任务状态枚举
 * @return std::string 中文名称
 */
inline std::string waylineMissionStateToString(WaylineMissionState state) {
    switch (state) {
        case WaylineMissionState::kDisconnected: return "断连";
        case WaylineMissionState::kNotSupported: return "不支持该航点";
        case WaylineMissionState::kReady: return "航线准备";
        case WaylineMissionState::kUploading: return "文件上传中";
        case WaylineMissionState::kPreparing: return "准备起飞";
        case WaylineMissionState::kEnteringFirstWaypoint: return "进入航线-到达第一个航点";
        case WaylineMissionState::kExecuting: return "航线执行中";
        case WaylineMissionState::kInterrupted: return "航线中断";
        case WaylineMissionState::kRecovering: return "航线恢复中";
        case WaylineMissionState::kStopped: return "航线停止";
        default: return "未知状态";
    }
}

/**
 * @brief 数据更新回调类型
 * 
 * @param timestamp 数据更新时间戳 (毫秒)
 */
using DataUpdateCallback = std::function<void(uint64_t timestamp)>;

/**
 * @brief 飞行数据提供者 - 提供飞行器实时状态数据的线程安全访问
 * 
 * @details
 * 使用读写锁 (std::shared_mutex) 实现高效的并发访问:
 * - 多个读者可以同时读取数据
 * - 只有一个写者可以更新数据
 * 
 * 这种设计适合「写少读多」的场景:
 * - OSD 数据更新频率: 0.5Hz (写)
 * - 检测循环读取频率: 10-30Hz (读)
 * 
 * 📌 面试考点:
 * Q: 为什么使用 std::shared_mutex 而不是 std::mutex？
 * A: std::shared_mutex 允许多个读者同时访问，提高读取性能。
 *    在我们的场景中，OSD 数据每 2 秒更新一次（写），
 *    而检测循环每秒读取 10-30 次（读），是典型的读多写少场景。
 *    使用 shared_mutex 可以避免读者之间互相阻塞。
 */
class FlightDataProvider {
public:
    /**
     * @brief 获取单例实例
     * 
     * @return FlightDataProvider& 单例引用
     * 
     * 📌 Meyers 单例: 
     * - C++11 保证静态局部变量的初始化是线程安全的
     * - 延迟初始化，首次调用时才创建实例
     */
    static FlightDataProvider& getInstance() {
        static FlightDataProvider instance;
        return instance;
    }
    
    // ==================== 数据更新接口 (MqttHandler 调用) ====================
    
    /**
     * @brief 更新 GPS 位置
     * 
     * @param latitude 纬度 (-90 ~ 90)
     * @param longitude 经度 (-180 ~ 180)
     * @param height 椭球高度 (米)
     * @param altitude 海拔高度 (米), 可选
     * 
     * @note 由 MqttHandler 在收到 OSD 消息时调用
     * @note 线程安全，内部使用写锁
     */
    void updateGpsPosition(double latitude, double longitude, 
                           double height, double altitude = 0.0) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        gpsPosition_.latitude = latitude;
        gpsPosition_.longitude = longitude;
        gpsPosition_.height = height;
        gpsPosition_.altitude = altitude;
        lastGpsUpdateTime_ = std::chrono::steady_clock::now();
    }
    
    /**
     * @brief 更新飞行器姿态
     * 
     * @param head 机头朝向/偏航角 (度)
     * @param pitch 俯仰角 (度)
     * @param roll 横滚角 (度)
     */
    void updateAttitude(double head, double pitch, double roll) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        attitude_.head = head;
        attitude_.pitch = pitch;
        attitude_.roll = roll;
        lastAttitudeUpdateTime_ = std::chrono::steady_clock::now();
    }
    
    /**
     * @brief 更新飞行器速度
     * 
     * @param horizontal 水平速度 (m/s)
     * @param vertical 垂直速度 (m/s)
     */
    void updateVelocity(double horizontal, double vertical) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        velocity_.horizontal = horizontal;
        velocity_.vertical = vertical;
    }
    
    /**
     * @brief 更新飞行模式
     * 
     * @param modeCode DJI Cloud API 中的 mode_code 值
     * 
     * @note 由 MqttHandler 在收到 OSD 消息时调用
     * @note 线程安全，内部使用写锁
     */
    void updateFlightMode(int modeCode) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // 将 mode_code 转换为枚举
        if (modeCode >= 0 && modeCode <= 17) {
            flightMode_ = static_cast<FlightMode>(modeCode);
        } else {
            flightMode_ = FlightMode::kUnknown;
        }
        lastFlightModeUpdateTime_ = std::chrono::steady_clock::now();
    }
    
    // ==================== 数据读取接口 (LiveStreamTask 调用) ====================
    
    /**
     * @brief 获取当前 GPS 位置
     * 
     * @return GpsPosition GPS 位置副本
     * 
     * @note 线程安全，内部使用读锁
     * @note 返回副本而非引用，避免数据竞争
     */
    GpsPosition getGpsPosition() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return gpsPosition_;
    }
    
    /**
     * @brief 获取当前姿态
     * 
     * @return Attitude 姿态副本
     */
    Attitude getAttitude() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return attitude_;
    }
    
    /**
     * @brief 获取当前速度
     * 
     * @return Velocity 速度副本
     */
    Velocity getVelocity() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return velocity_;
    }
    
    /**
     * @brief 获取当前飞行模式
     * 
     * @return FlightMode 飞行模式枚举
     */
    FlightMode getFlightMode() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return flightMode_;
    }
    
    /**
     * @brief 检查无人机是否处于返航或降落状态
     * 
     * @return true 无人机正在返航或降落
     * @return false 无人机处于其他飞行状态
     * 
     * @details
     * 当无人机处于以下状态时返回 true：
     * - kAutoRth (9): 自动返航
     * - kAutoLanding (10): 自动降落
     * - kForcedLanding (11): 强制降落
     * 
     * 这些状态下应该暂停智能变焦拍照等自动任务，
     * 避免干扰无人机的返航/降落流程。
     */
    bool isReturningOrLanding() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return flightMode_ == FlightMode::kAutoRth ||
               flightMode_ == FlightMode::kAutoLanding ||
               flightMode_ == FlightMode::kForcedLanding;
    }
    
    /**
     * @brief 检查无人机是否处于正常作业状态
     * 
     * @return true 无人机处于可以执行算法任务的状态
     * @return false 无人机处于特殊状态，应暂停算法任务
     * 
     * @details
     * 只有在以下状态时返回 true：
     * - kWaypointFlight (5): 航线飞行 (主要作业状态)
     * - kIntelligentTracking (7): 智能跟踪
     * - kManual (3): 手动控制 (可能用于测试)
     * - kVirtualStickFlight (16): 虚拟摇杆 (远程控制)
     */
    bool isReadyForAlgorithmTask() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return flightMode_ == FlightMode::kWaypointFlight ||
               flightMode_ == FlightMode::kIntelligentTracking ||
               flightMode_ == FlightMode::kManual ||
               flightMode_ == FlightMode::kVirtualStickFlight;
    }
    
    // ==================== 航线任务状态接口 ====================
    
    /**
     * @brief 更新航线任务状态
     * 
     * @param stateCode 航线任务状态码 (来自 flighttask_progress 事件)
     * 
     * @note 由 MqttHandler 在收到 flighttask_progress 事件时调用
     * @note 线程安全，内部使用写锁
     */
    void updateWaylineMissionState(int stateCode) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (stateCode >= 0 && stateCode <= 9) {
            waylineMissionState_ = static_cast<WaylineMissionState>(stateCode);
        } else {
            waylineMissionState_ = WaylineMissionState::kUnknown;
        }
        lastWaylineMissionStateUpdateTime_ = std::chrono::steady_clock::now();
    }
    
    /**
     * @brief 获取当前航线任务状态
     * 
     * @return WaylineMissionState 航线任务状态枚举
     */
    WaylineMissionState getWaylineMissionState() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return waylineMissionState_;
    }
    
    /**
     * @brief 检查是否可以执行智能变焦拍照
     * 
     * @return true 航线任务已到达航点，可以启用智能变焦拍照
     * @return false 还未到达航点或航线已停止，应禁用智能变焦拍照
     * 
     * @details
     * 只有当航线任务状态为以下值时返回 true：
     * - kEnteringFirstWaypoint (5): 进入航线，到达第一个航点
     * - kExecuting (6): 航线执行中
     * - kRecovering (8): 航线恢复中
     * 
     * 📌 设计原因：
     * 在无人机起飞、爬升、进入航线的过程中（状态 0-4），
     * 触发智能变焦拍照会干扰正常流程。
     * 只有到达第一个航点后（状态 >= 5），才开始执行智能拍照任务。
     */
    bool isReadyForSmartZoomCapture() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return waylineMissionState_ == WaylineMissionState::kEnteringFirstWaypoint ||
               waylineMissionState_ == WaylineMissionState::kExecuting ||
               waylineMissionState_ == WaylineMissionState::kRecovering;
    }
    
    /**
     * @brief 获取航线任务状态的可读名称
     * 
     * @return std::string 中文名称
     */
    std::string getWaylineMissionStateString() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return waylineMissionStateToString(waylineMissionState_);
    }
    
    /**
     * @brief 获取飞行模式的可读名称
     * 
     * @return std::string 中文名称
     */
    std::string getFlightModeString() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return flightModeToString(flightMode_);
    }
    
    /**
     * @brief 检查 GPS 数据是否有效且新鲜
     * 
     * @param maxAgeSeconds 最大数据年龄 (秒)，默认 5 秒
     * @return true GPS 数据有效且在有效期内
     * @return false GPS 数据无效或过期
     * 
     * @details
     * OSD 数据推送频率为 0.5Hz（每 2 秒一次），
     * 考虑网络延迟和处理时间，5 秒内的数据视为有效。
     */
    bool isGpsDataValid(double maxAgeSeconds = 5.0) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        if (!gpsPosition_.isValid()) {
            return false;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - lastGpsUpdateTime_).count();
        return elapsed <= maxAgeSeconds;
    }
    
    /**
     * @brief 获取自上次 GPS 更新以来的时间
     * 
     * @return double 秒数
     */
    double getGpsDataAge() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - lastGpsUpdateTime_).count();
    }
    
    /**
     * @brief 设置数据更新回调
     * 
     * @param callback 回调函数，每次数据更新时调用
     * 
     * @note 暂未实现，预留接口
     */
    void setUpdateCallback(DataUpdateCallback callback) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        updateCallback_ = callback;
    }
    
    /**
     * @brief 重置所有数据为默认值
     * 
     * @note 主要用于测试
     */
    void reset() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        gpsPosition_ = GpsPosition{};
        attitude_ = Attitude{};
        velocity_ = Velocity{};
        flightMode_ = FlightMode::kStandby;
        waylineMissionState_ = WaylineMissionState::kUnknown;  // 重置航线任务状态
        lastGpsUpdateTime_ = std::chrono::steady_clock::time_point{};
        lastAttitudeUpdateTime_ = std::chrono::steady_clock::time_point{};
        lastFlightModeUpdateTime_ = std::chrono::steady_clock::time_point{};
        lastWaylineMissionStateUpdateTime_ = std::chrono::steady_clock::time_point{};
    }

    // 禁止拷贝和赋值
    FlightDataProvider(const FlightDataProvider&) = delete;
    FlightDataProvider& operator=(const FlightDataProvider&) = delete;

private:
    FlightDataProvider() = default;
    ~FlightDataProvider() = default;
    
    // 数据成员
    GpsPosition gpsPosition_;
    Attitude attitude_;
    Velocity velocity_;
    FlightMode flightMode_{FlightMode::kStandby};  ///< 飞行模式，默认待机
    WaylineMissionState waylineMissionState_{WaylineMissionState::kUnknown};  ///< 航线任务状态
    
    // 时间戳
    std::chrono::steady_clock::time_point lastGpsUpdateTime_;
    std::chrono::steady_clock::time_point lastAttitudeUpdateTime_;
    std::chrono::steady_clock::time_point lastFlightModeUpdateTime_;  ///< 飞行模式更新时间
    std::chrono::steady_clock::time_point lastWaylineMissionStateUpdateTime_;  ///< 航线状态更新时间
    
    // 同步机制
    mutable std::shared_mutex mutex_;  ///< 读写锁 (mutable 允许在 const 方法中使用)
    
    // 回调
    DataUpdateCallback updateCallback_;
};

}  // namespace device
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_DEVICE_FLIGHT_DATA_PROVIDER_H_
