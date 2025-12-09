/**
 * @file DeviceManager.h
 * @brief Device管理器公共接口
 * 
 * 封装DJI Edge-SDK,提供两种数据源:
 * 1. 实时视频流 (Liveview → H264 → cv::Mat)
 * 2. 媒体文件 (MediaManager → JPEG → cv::Mat)
 * 
 * @author Your Name
 * @date 2025-10-27
 */

#ifndef ESDK_SOPHON_DEVICE_DEVICE_MANAGER_H_
#define ESDK_SOPHON_DEVICE_DEVICE_MANAGER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <ctime>

#include "esdk_sophon/Device/IDeviceObserver.h"

// 前向声明,避免包含OpenCV头文件
namespace cv {
    class Mat;
}

namespace esdk_sophon {
namespace device {

/**
 * @brief 媒体文件信息
 */
struct MediaFileInfo {
    std::string fileId;       ///< 文件唯一标识(用于下载)
    std::string fileName;     ///< 文件名(如"DJI_20251027_143022.JPG")
    std::string filePath;     ///< Edge-SDK返回的文件路径
    size_t fileSize;          ///< 文件大小(字节)
    int32_t width;            ///< 图片宽度(像素)
    int32_t height;           ///< 图片高度(像素)
    std::time_t createTime;   ///< 创建时间(Unix时间戳)
    std::string cameraType;   ///< 相机类型("Wide"/"Zoom"/"IR")
};

/**
 * @brief Device管理器(单例)
 * 
 * 线程安全,负责:
 * - SDK初始化和设备连接
 * - 提供实时视频流(H264 → cv::Mat)
 * - 提供媒体文件读取(JPEG → cv::Mat)
 * - 通知观察者状态变化
 * 
 * @note Pimpl模式,隐藏Edge-SDK实现细节
 * 
 * 使用示例:
 * @code
 * // 1. 初始化
 * DeviceManager& deviceMgr = DeviceManager::getInstance();
 * if (!deviceMgr.initialize()) {
 *     LOG_FATAL("初始化失败");
 *     return -1;
 * }
 * 
 * // 2. 连接设备
 * if (!deviceMgr.connect()) {
 *     LOG_ERROR("连接失败");
 *     return -1;
 * }
 * 
 * // 3. 启动视频流(数据源1)
 * deviceMgr.startLiveview([](const cv::Mat& frame) {
 *     // 每帧到达时调用
 *     auto results = VisionModule::detect(frame);
 *     MqttClient::getInstance().publish("detection/result", results.toJSON());
 * });
 * 
 * // 4. 下载媒体文件(数据源2)
 * auto files = deviceMgr.listMediaFiles();
 * for (auto& file : files) {
 *     deviceMgr.downloadMediaFile(file.fileId, [](const cv::Mat& image) {
 *         // 图片下载并解码后调用
 *         auto results = VisionModule::segment(image);
 *         MqttClient::getInstance().publish("segmentation/result", results.toJSON());
 *     });
 * }
 * @endcode
 */
class DeviceManager {
public:
    /**
     * @brief 获取单例实例
     * @return DeviceManager引用
     * 
     * @note 线程安全(C++11保证)
     */
    static DeviceManager& getInstance();
    
    /**
     * @brief 析构函数
     */
    ~DeviceManager();
    
    // 禁止拷贝和赋值
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;
    
    /* ========== SDK生命周期 ========== */
    
    /**
     * @brief 初始化SDK
     * 
     * @return true 初始化成功
     * @return false 初始化失败(见日志)
     * 
     * @note 从config.json读取Edge-SDK配置
     * @note 调用ESDKInit::Init()
     * @note 只能调用一次,重复调用返回true
     */
    bool initialize();
    
    /**
     * @brief 连接设备
     * 
     * @return true 连接成功
     * @return false 连接失败
     * 
     * @note 连接成功后触发IDeviceObserver::onDeviceConnected()
     * @note 重复调用返回true(如果已连接)
     */
    bool connect();
    
    /**
     * @brief 断开设备连接
     * 
     * @note 会停止所有视频流
     * @note 触发IDeviceObserver::onDeviceDisconnected()
     * @note 重复调用无影响
     */
    void disconnect();
    
    /**
     * @brief 检查设备是否已连接
     * 
     * @return true 已连接DJI Dock
     * @return false 未连接
     * 
     * @warning 这是设备连接,不是MQTT连接!
     *          MqttClient::isConnected() ≠ DeviceManager::isConnected()
     */
    bool isConnected() const;
    
    /* ========== 数据源1: 实时视频流 ========== */
    
    /**
     * @brief 实时帧回调类型
     * @param frame 已解码的BGR图像(cv::Mat)
     * 
     * @note 回调在内部解码线程执行,注意线程安全
     */
    using LiveviewCallback = std::function<void(const cv::Mat& frame)>;
    
    /**
     * @brief 启动实时视频流
     * 
     * @param callback 帧到达时的回调函数
     * @return true 启动成功
     * @return false 启动失败(设备未连接/重复启动)
     * 
     * 工作流程:
     * 1. 从config.json读取相机类型(FPV/Payload)和分辨率(720p)
     * 2. 订阅H264流 (Liveview::StartH264Stream)
     * 3. 内部解码H264 → cv::Mat
     * 4. 调用callback传递给上层
     * 
     * @note callback在内部解码线程执行,注意线程安全
     * @note 重复调用会返回false
     * @note H264解码需要FFmpeg库,如果未集成则返回空Mat
     */
    bool startLiveview(LiveviewCallback callback);
    
    /**
     * @brief 停止实时视频流
     * 
     * @note 调用Liveview::StopH264Stream()
     * @note 重复调用无影响
     */
    void stopLiveview();
    
    /**
     * @brief 检查视频流是否正在运行
     * 
     * @return true 正在接收视频帧
     * @return false 已停止
     */
    bool isLiveviewRunning() const;
    
    /* ========== 数据源2: 媒体文件 ========== */
    
    /**
     * @brief 列出所有媒体文件
     * 
     * @return vector<MediaFileInfo> 文件列表
     * 
     * @note 调用MediaFilesReader::FileList()
     * @note 只返回JPEG文件(过滤掉MP4)
     * @note 如果设备未连接,返回空列表
     */
    std::vector<MediaFileInfo> listMediaFiles();
    
    /**
     * @brief 媒体文件回调类型
     * @param image 已解码的BGR图像(cv::Mat)
     * 
     * @note 回调在当前线程执行(同步操作)
     */
    using MediaFileCallback = std::function<void(const cv::Mat& image)>;
    
    /**
     * @brief 下载并解码媒体文件
     * 
     * @param fileId 文件ID(来自listMediaFiles())
     * @param callback 解码完成后的回调
     * @return true 下载成功
     * @return false 下载失败
     * 
     * 工作流程:
     * 1. MediaFilesReader::Open(fileId)
     * 2. Read()读取全部内容到内存
     * 3. cv::imdecode()解码JPEG
     * 4. 调用callback传递cv::Mat
     * 5. Close()关闭文件
     * 
     * @note callback在当前线程执行(同步操作)
     * @note 阻塞调用,下载大文件会耗时
     * @note 如果文件不存在或解码失败,返回false
     */
    bool downloadMediaFile(const std::string& fileId, 
                          MediaFileCallback callback);
    
    /* ========== 观察者模式 ========== */
    
    /**
     * @brief 添加观察者
     * @param observer 观察者指针(生命周期由外部管理)
     * 
     * @note 使用裸指针,不接管所有权
     * @note 线程安全(内部mutex保护)
     * @note 重复添加同一观察者无影响
     * 
     * @warning 观察者必须在DeviceManager析构前移除或析构!
     */
    void addObserver(IDeviceObserver* observer);
    
    /**
     * @brief 移除观察者
     * @param observer 观察者指针
     * 
     * @note 必须在observer析构前调用
     * @note 如果observer不在列表中,无影响
     */
    void removeObserver(IDeviceObserver* observer);
    
private:
    /**
     * @brief 私有构造函数(单例模式)
     */
    DeviceManager();
    
    /**
     * @brief Pimpl实现类(前向声明)
     */
    class Impl;
    std::unique_ptr<Impl> pImpl_;  ///< Pimpl指针(隐藏Edge-SDK细节)
};

}  // namespace device
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_DEVICE_DEVICE_MANAGER_H_

/**
 * @page device_manager_page DeviceManager使用指南
 * 
 * @section manager_intro 简介
 * 
 * DeviceManager是Device模块的核心类,负责封装DJI Edge-SDK,
 * 为上层模块提供统一的数据接口。
 * 
 * @section manager_architecture 架构设计
 * 
 * @subsection arch_layers 分层结构
 * 
 * @code
 * ┌─────────────────────────────────────────┐
 * │      上层模块 (Vision, Main)            │
 * │  - 目标检测                             │
 * │  - 图像分割                             │
 * └─────────────────────────────────────────┘
 *               ↓ Callback(cv::Mat)
 * ┌─────────────────────────────────────────┐
 * │    DeviceManager (数据源抽象层)         │
 * │  - 格式转换: H264/JPEG → cv::Mat       │
 * │  - 状态管理: 连接/断开/错误            │
 * └─────────────────────────────────────────┘
 *               ↓ Edge-SDK API
 * ┌─────────────────────────────────────────┐
 * │       DJI Edge-SDK (官方库)             │
 * │  - ESDKInit (初始化)                   │
 * │  - Liveview (视频流)                   │
 * │  - MediaManager (文件管理)             │
 * └─────────────────────────────────────────┘
 *               ↓ ESDK协议
 * ┌─────────────────────────────────────────┐
 * │      DJI Dock + 无人机                  │
 * └─────────────────────────────────────────┘
 * @endcode
 * 
 * @subsection arch_pimpl Pimpl模式
 * 
 * DeviceManager使用Pimpl模式隐藏Edge-SDK实现细节:
 * 
 * @code
 * // DeviceManager.h (公共接口)
 * class DeviceManager {
 * public:
 *     bool startLiveview(LiveviewCallback callback);
 * private:
 *     class Impl;  // 前向声明,不暴露内部实现
 *     std::unique_ptr<Impl> pImpl_;
 * };
 * 
 * // DeviceManager.cpp (实现)
 * class DeviceManager::Impl {
 * private:
 *     edge_sdk::Liveview* liveview_;  // 只在.cpp中可见
 *     edge_sdk::MediaManager* mediaManager_;
 * };
 * @endcode
 * 
 * **优点**:
 * - 使用DeviceManager的模块不需要链接Edge-SDK
 * - 修改实现不影响使用方
 * - 保持接口稳定
 * 
 * @section manager_two_sources 两种数据源
 * 
 * @subsection source1_liveview 数据源1: 实时视频流
 * 
 * **使用场景**: 云端下发指令 `source: 0`
 * 
 * @code
 * // 云端指令
 * {
 *   "method": "device_algorithm_enable",
 *   "source": 0,  // 视频流分析
 *   "algorithm_type": "object_detection"
 * }
 * 
 * // 应用代码
 * DeviceManager::getInstance().startLiveview([](const cv::Mat& frame) {
 *     // 每30帧/秒调用一次
 *     auto results = VisionModule::detect(frame);
 *     
 *     // 上报结果
 *     MqttClient::getInstance().publish("drone/{sn}/info/event", results.toJSON());
 * });
 * @endcode
 * 
 * **工作流程**:
 * 1. Edge-SDK订阅H264流
 * 2. H264数据到达回调
 * 3. FFmpeg解码 → cv::Mat
 * 4. 调用用户callback
 * 
 * **注意事项**:
 * - Callback在解码线程执行,注意线程安全
 * - 如果FFmpeg未集成,返回空Mat
 * - 可能丢帧(如果处理速度跟不上)
 * 
 * @subsection source2_media 数据源2: 媒体文件
 * 
 * **使用场景**: 云端下发指令 `source: 1`
 * 
 * @code
 * // 云端指令
 * {
 *   "method": "device_algorithm_enable",
 *   "source": 1,  // 图片分析
 *   "algorithm_type": "segmentation"
 * }
 * 
 * // 应用代码
 * auto files = DeviceManager::getInstance().listMediaFiles();
 * for (auto& file : files) {
 *     DeviceManager::getInstance().downloadMediaFile(
 *         file.fileId,
 *         [file](const cv::Mat& image) {
 *             // 图片下载并解码后调用
 *             auto results = VisionModule::segment(image);
 *             
 *             // 上报结果(包含文件名)
 *             json result = {
 *                 {"file_name", file.fileName},
 *                 {"segmentation", results.toJSON()}
 *             };
 *             MqttClient::getInstance().publish("...", result.dump());
 *         }
 *     );
 * }
 * @endcode
 * 
 * **工作流程**:
 * 1. MediaManager::FileList()获取文件列表
 * 2. MediaFilesReader::Open()打开文件
 * 3. Read()读取到内存
 * 4. cv::imdecode()解码JPEG
 * 5. 调用用户callback
 * 6. Close()关闭文件
 * 
 * **注意事项**:
 * - 同步阻塞操作(大文件耗时长)
 * - 只支持JPEG格式(MP4被过滤)
 * - 需要足够内存(图片加载到RAM)
 * 
 * @section manager_lifecycle 生命周期管理
 * 
 * @subsection lifecycle_init 初始化流程
 * 
 * @code
 * // main.cpp
 * 
 * int main() {
 *     // 1. 初始化基础设施
 *     Logger::getInstance().initialize();
 *     Config::getInstance().load("config.json");
 *     
 *     // 2. 初始化DeviceManager
 *     DeviceManager& deviceMgr = DeviceManager::getInstance();
 *     if (!deviceMgr.initialize()) {
 *         LOG_FATAL("DeviceManager初始化失败");
 *         return -1;
 *     }
 *     
 *     // 3. 添加观察者
 *     auto observer = std::make_shared<AppDeviceObserver>();
 *     deviceMgr.addObserver(observer.get());
 *     
 *     // 4. 连接设备
 *     if (!deviceMgr.connect()) {
 *         LOG_FATAL("连接设备失败");
 *         return -1;
 *     }
 *     
 *     // 5. 启动MQTT(接收云端指令)
 *     MqttClient::getInstance().initialize();
 *     MqttClient::getInstance().connect();
 *     
 *     // 6. 主循环
 *     while (running) {
 *         std::this_thread::sleep_for(std::chrono::seconds(1));
 *     }
 *     
 *     // 7. 清理
 *     deviceMgr.disconnect();
 *     deviceMgr.removeObserver(observer.get());
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * @section manager_thread_safety 线程安全
 * 
 * **线程安全的操作**:
 * - getInstance() (C++11保证)
 * - addObserver() / removeObserver() (mutex保护)
 * - isConnected() / isLiveviewRunning() (atomic变量)
 * 
 * **非线程安全的操作**:
 * - initialize() (必须在单线程环境调用)
 * - connect() / disconnect() (避免并发调用)
 * - startLiveview() / stopLiveview() (避免并发调用)
 * 
 * **Callback线程**:
 * - LiveviewCallback: 在Edge-SDK内部解码线程执行
 * - MediaFileCallback: 在调用线程执行(同步)
 * 
 * @section manager_error_handling 错误处理
 * 
 * @subsection error_return 返回值检查
 * 
 * @code
 * if (!deviceMgr.initialize()) {
 *     // 查看Logger输出的详细错误
 *     LOG_FATAL("初始化失败");
 *     return -1;
 * }
 * 
 * if (!deviceMgr.connect()) {
 *     // 可能原因:
 *     // - DJI Dock未开机
 *     // - 网络不通
 *     // - SDK配置错误
 *     LOG_ERROR("连接失败,5秒后重试");
 *     std::this_thread::sleep_for(std::chrono::seconds(5));
 *     deviceMgr.connect();  // 重试
 * }
 * @endcode
 * 
 * @subsection error_observer 观察者通知
 * 
 * @code
 * class MyObserver : public IDeviceObserver {
 *     void onDeviceError(int errorCode, const std::string& message) override {
 *         if (errorCode == -1) {
 *             // 网络错误,尝试重连
 *             DeviceManager::getInstance().disconnect();
 *             std::this_thread::sleep_for(std::chrono::seconds(5));
 *             DeviceManager::getInstance().connect();
 *         } else if (errorCode < -100) {
 *             // 严重错误,退出程序
 *             LOG_FATAL("设备严重错误: " + message);
 *             std::exit(1);
 *         }
 *     }
 * };
 * @endcode
 */
