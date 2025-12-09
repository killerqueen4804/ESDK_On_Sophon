/**
 * @file IDeviceObserver.h
 * @brief Device模块观察者接口
 * 
 * 监听设备连接状态变化、错误事件等。
 * 视频帧/图片数据通过Callback传递,不在Observer中。
 * 
 * @author Your Name
 * @date 2025-10-27
 */

#ifndef ESDK_SOPHON_DEVICE_IDEVICE_OBSERVER_H_
#define ESDK_SOPHON_DEVICE_IDEVICE_OBSERVER_H_

#include <string>

namespace esdk_sophon {
namespace device {

/**
 * @brief 设备信息结构体
 */
struct DeviceInfo {
    std::string firmwareVersion;  ///< 固件版本 (如"v1.2.3")
    std::string productName;      ///< 产品名称 (如"DJI Dock")
    std::string serialNumber;     ///< 序列号 (如"1581F5BKD2358A0K0001")
    std::string vendorName;       ///< 厂商名称 (如"DJI")
};

/**
 * @brief 设备观察者接口
 * 
 * 实现此接口以接收设备状态通知。
 * 生命周期由外部管理,DeviceManager使用裸指针。
 * 
 * 使用示例:
 * @code
 * class MyObserver : public IDeviceObserver {
 *     void onDeviceConnected(const DeviceInfo& info) override {
 *         LOG_INFO("设备已连接: " + info.productName);
 *     }
 *     
 *     void onDeviceDisconnected(const std::string& reason) override {
 *         LOG_ERROR("设备断开: " + reason);
 *     }
 *     
 *     void onDeviceError(int errorCode, const std::string& message) override {
 *         LOG_ERROR("设备错误[" + std::to_string(errorCode) + "]: " + message);
 *     }
 * };
 * 
 * auto observer = std::make_shared<MyObserver>();
 * DeviceManager::getInstance().addObserver(observer.get());
 * @endcode
 */
class IDeviceObserver {
public:
    virtual ~IDeviceObserver() = default;
    
    /**
     * @brief 设备连接成功回调
     * 
     * @param info 设备信息(固件版本、产品名称等)
     * 
     * @note 此时可以调用startLiveview()或listMediaFiles()
     */
    virtual void onDeviceConnected(const DeviceInfo& info) = 0;
    
    /**
     * @brief 设备断开连接回调
     * 
     * @param reason 断开原因
     *               - "网络超时": 网络连接丢失
     *               - "手动断开": 用户调用disconnect()
     *               - "设备离线": DJI Dock关机或断网
     *               - "SDK错误": Edge-SDK内部错误
     * 
     * @note 断开后需要重新调用connect()
     */
    virtual void onDeviceDisconnected(const std::string& reason) = 0;
    
    /**
     * @brief 视频流状态变化回调(可选实现)
     * 
     * @param available 视频流是否可用
     * @param quality 当前流质量(如"720p", "1080p")
     * 
     * @note 默认实现为空,子类可选择性实现
     */
    virtual void onLiveviewStatusChanged(bool available, 
                                        const std::string& quality) {}
    
    /**
     * @brief 设备错误回调
     * 
     * @param errorCode Edge-SDK错误码
     *                  - 0: 成功
     *                  - 负数: 错误码(参考Edge-SDK文档)
     * @param message 错误消息描述
     * 
     * @note 错误发生后设备可能仍然连接,也可能自动断开
     */
    virtual void onDeviceError(int errorCode, 
                              const std::string& message) = 0;
};

}  // namespace device
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_DEVICE_IDEVICE_OBSERVER_H_

/**
 * @page device_observer_page Device观察者模式
 * 
 * @section observer_intro 简介
 * 
 * IDeviceObserver是Device模块的观察者接口,用于监听设备状态变化。
 * 
 * @section observer_vs_callback 观察者 vs 回调
 * 
 * | 方式 | 用途 | 生命周期 | 数据类型 |
 * |------|------|---------|---------|
 * | **观察者** | 状态通知 | 长期存在 | 简单数据 (字符串/int) |
 * | **回调** | 数据传递 | 临时调用 | 复杂数据 (cv::Mat) |
 * 
 * @section observer_example 完整示例
 * 
 * @code
 * // main.cpp
 * 
 * class AppDeviceObserver : public IDeviceObserver {
 * public:
 *     void onDeviceConnected(const DeviceInfo& info) override {
 *         Logger::getInstance().info("设备已连接:");
 *         Logger::getInstance().info("  产品: " + info.productName);
 *         Logger::getInstance().info("  版本: " + info.firmwareVersion);
 *         Logger::getInstance().info("  SN: " + info.serialNumber);
 *         
 *         // 连接成功后启动视频流
 *         DeviceManager::getInstance().startLiveview([](const cv::Mat& frame) {
 *             // 处理视频帧
 *             VisionModule::detect(frame);
 *         });
 *     }
 *     
 *     void onDeviceDisconnected(const std::string& reason) override {
 *         Logger::getInstance().error("设备断开: " + reason);
 *         
 *         // 尝试重连
 *         std::this_thread::sleep_for(std::chrono::seconds(5));
 *         DeviceManager::getInstance().connect();
 *     }
 *     
 *     void onLiveviewStatusChanged(bool available, const std::string& quality) override {
 *         if (available) {
 *             Logger::getInstance().info("视频流可用: " + quality);
 *         } else {
 *             Logger::getInstance().warn("视频流不可用");
 *         }
 *     }
 *     
 *     void onDeviceError(int errorCode, const std::string& message) override {
 *         Logger::getInstance().error("设备错误[" + std::to_string(errorCode) + "]: " + message);
 *         
 *         // 严重错误需要重启
 *         if (errorCode < -100) {
 *             Logger::getInstance().fatal("严重错误,程序退出");
 *             std::exit(1);
 *         }
 *     }
 * };
 * 
 * int main() {
 *     // 初始化Logger和Config
 *     Logger::getInstance().initialize();
 *     Config::getInstance().load("config.json");
 *     
 *     // 创建观察者
 *     auto observer = std::make_shared<AppDeviceObserver>();
 *     
 *     // 初始化DeviceManager
 *     DeviceManager& deviceMgr = DeviceManager::getInstance();
 *     deviceMgr.addObserver(observer.get());
 *     
 *     if (!deviceMgr.initialize()) {
 *         LOG_FATAL("DeviceManager初始化失败");
 *         return -1;
 *     }
 *     
 *     if (!deviceMgr.connect()) {
 *         LOG_FATAL("连接设备失败");
 *         return -1;
 *     }
 *     
 *     // 主循环
 *     while (running) {
 *         std::this_thread::sleep_for(std::chrono::seconds(1));
 *     }
 *     
 *     // 清理
 *     deviceMgr.disconnect();
 *     deviceMgr.removeObserver(observer.get());
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * @section observer_thread_safety 线程安全
 * 
 * - DeviceManager内部使用mutex保护观察者列表
 * - 可以在任意线程调用addObserver()/removeObserver()
 * - 观察者回调在DeviceManager内部线程执行,注意线程安全
 * 
 * @section observer_lifetime 生命周期管理
 * 
 * @warning 观察者必须在DeviceManager析构前移除或析构!
 * 
 * @code
 * // ❌ 错误示例:观察者先析构
 * {
 *     auto observer = std::make_shared<MyObserver>();
 *     DeviceManager::getInstance().addObserver(observer.get());
 * }  // observer析构,但DeviceManager还持有指针 → 野指针!
 * 
 * DeviceManager::getInstance().notifyConnected(...);  // 崩溃!
 * 
 * 
 * // ✅ 正确示例1:使用RAII
 * class ObserverGuard {
 * public:
 *     ObserverGuard(IDeviceObserver* obs) : observer_(obs) {
 *         DeviceManager::getInstance().addObserver(observer_);
 *     }
 *     ~ObserverGuard() {
 *         DeviceManager::getInstance().removeObserver(observer_);
 *     }
 * private:
 *     IDeviceObserver* observer_;
 * };
 * 
 * 
 * // ✅ 正确示例2:手动管理
 * auto observer = std::make_shared<MyObserver>();
 * DeviceManager::getInstance().addObserver(observer.get());
 * 
 * // ... 程序运行 ...
 * 
 * // 退出前移除
 * DeviceManager::getInstance().removeObserver(observer.get());
 * @endcode
 */
