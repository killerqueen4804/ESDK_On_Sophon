/**
 * @file MqttHandler.cpp
 * @brief MQTT消息处理器实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-29
 */

#include "esdk_sophon/mqtt/MqttHandler.h"

// 依赖模块
#include "esdk_sophon/mqtt/MqttClient.h"
#include "esdk_sophon/task/TaskManager.h"
#include "esdk_sophon/task/TaskTypes.h"      // task::TaskConfig (统一使用)
#include "esdk_sophon/core/Logger.h"
#include "esdk_sophon/core/Config.h"
#include "esdk_sophon/device/FlightDataProvider.h"  // GPS 数据提供者

// 注意: types::DetectionResult 已废弃，已完全移除

// 标准库
#include <sstream>
#include <ctime>
#include <iomanip>
#include <thread>  // for std::this_thread::sleep_for

namespace esdk_sophon {
namespace mqtt {

// ==================== 单例实现 ====================

MqttHandler& MqttHandler::getInstance() {
    static MqttHandler instance;  // Meyers单例，C++11保证线程安全
    return instance;
}

// ==================== 构造和析构 ====================

MqttHandler::MqttHandler()
    : mqttClient_(MqttClient::getInstance())
    , taskManager_(task::TaskManager::getInstance())
    , logger_(core::Logger::getInstance())  // Logger不接受参数
    , config_(core::Config::getInstance())
    , deviceSn_("")
    , aircraftSn_("")
    , isStarted_(false)
    , osdSubscribed_(false) {
    
    logger_.debug("MqttHandler构造函数");
}

MqttHandler::~MqttHandler() {
    logger_.debug("MqttHandler析构函数");
    
    // 清理: 停止消息处理
    if (isStarted_) {
        stop();
    }
}

// ==================== 初始化和启动 ====================

bool MqttHandler::initialize() {
    logger_.info("初始化MqttHandler...");
    
    // 1. 从配置读取设备序列号
    try {
        deviceSn_ = config_.getString("device.analysis_sn", "analysis_device_WRSE7001");
        logger_.info("设备序列号: " + deviceSn_);
    } catch (const std::exception& e) {
        logger_.error("读取device_sn配置失败: " + std::string(e.what()));
        // 使用默认值
        deviceSn_ = "analysis_device_WRSE7001";
        logger_.warning("使用默认设备序列号: " + deviceSn_);
    }
    
    // 1.1 从配置读取飞行器序列号（用于 OSD 订阅）
    try {
        aircraftSn_ = config_.getString("device.aircraft.sn", "");
        if (!aircraftSn_.empty()) {
            logger_.info("飞行器序列号: " + aircraftSn_);
        } else {
            logger_.warning("未配置飞行器序列号 (device.aircraft.sn)，无法订阅 OSD 数据");
        }
    } catch (const std::exception& e) {
        logger_.warning("读取飞行器序列号失败: " + std::string(e.what()));
    }
    
    // 1.2 从配置读取机场网关序列号（用于 events 订阅）
    try {
        gatewaySn_ = config_.getString("device.gateway_sn", "");
        if (!gatewaySn_.empty()) {
            logger_.info("机场网关序列号: " + gatewaySn_);
        } else {
            logger_.warning("未配置机场网关序列号 (device.gateway_sn)，无法订阅航线任务进度事件");
        }
    } catch (const std::exception& e) {
        logger_.warning("读取机场网关序列号失败: " + std::string(e.what()));
    }
    
    // 2. 构建Topic名称
    servicesTopicSub_ = "thing/product/" + deviceSn_ + "/services";
    servicesTopicPub_ = "thing/product/" + deviceSn_ + "/services_reply";
    eventsTopicPub_ = "thing/product/" + deviceSn_ + "/events";
    detectionTopicPub_ = "drone/" + deviceSn_ + "/info/event";
    
    // 2.1 构建飞行器 OSD Topic
    if (!aircraftSn_.empty()) {
        aircraftOsdTopicSub_ = "thing/product/" + aircraftSn_ + "/osd";
        logger_.debug("飞行器OSD Topic: " + aircraftOsdTopicSub_);
    }
    
    // 2.2 构建机场 Events Topic（用于接收航线任务进度）
    if (!gatewaySn_.empty()) {
        gatewayEventsTopicSub_ = "thing/product/" + gatewaySn_ + "/events";
        logger_.debug("机场Events Topic: " + gatewayEventsTopicSub_);
    }
    
    logger_.debug("订阅Topic: " + servicesTopicSub_);
    logger_.debug("应答Topic: " + servicesTopicPub_);
    logger_.debug("事件Topic: " + eventsTopicPub_);
    logger_.debug("检测Topic: " + detectionTopicPub_);
    
    // 3. 初始化指令处理函数映射表
    initCommandHandlers();
    
    logger_.info("MqttHandler初始化完成");
    return true;
}

bool MqttHandler::start() {
    logger_.info("启动MqttHandler...");
    
    // 检查MQTT客户端是否已连接
    if (!mqttClient_.isConnected()) {
        logger_.error("MQTT客户端未连接，无法启动MqttHandler");
        return false;
    }
    
    // 1. 注册为观察者
    mqttClient_.addObserver(this);
    logger_.debug("已注册为MQTT观察者");
    
    // 2. 订阅云端指令topic
    if (!mqttClient_.subscribe(servicesTopicSub_, 1)) {
        logger_.error("订阅topic失败: " + servicesTopicSub_);
        mqttClient_.removeObserver(this);
        return false;
    }
    
    logger_.info("订阅成功: " + servicesTopicSub_);
    
    // 3. 订阅飞行器 OSD topic（如果配置了飞行器序列号）
    if (!aircraftOsdTopicSub_.empty()) {
        if (mqttClient_.subscribe(aircraftOsdTopicSub_, 0)) {  // QoS 0 即可
            osdSubscribed_ = true;
            logger_.info("订阅飞行器OSD成功: " + aircraftOsdTopicSub_);
        } else {
            logger_.warning("订阅飞行器OSD失败: " + aircraftOsdTopicSub_ + " (非关键性错误，继续运行)");
            osdSubscribed_ = false;
        }
    }
    
    // 4. 订阅机场 Events topic（如果配置了机场网关序列号）
    //    用于接收 flighttask_progress 事件，监控航线任务状态
    if (!gatewayEventsTopicSub_.empty()) {
        if (mqttClient_.subscribe(gatewayEventsTopicSub_, 0)) {  // QoS 0 即可
            gatewayEventsSubscribed_ = true;
            logger_.info("📋 订阅机场Events成功: " + gatewayEventsTopicSub_);
            logger_.info("   💡 将监听航线任务进度 (flighttask_progress)，只有到达航点后才启用智能变焦拍照");
        } else {
            logger_.warning("订阅机场Events失败: " + gatewayEventsTopicSub_ + " (非关键性错误，继续运行)");
            gatewayEventsSubscribed_ = false;
        }
    }
    
    isStarted_ = true;
    logger_.info("MqttHandler启动成功");
    return true;
}

void MqttHandler::stop() {
    logger_.info("停止MqttHandler...");
    
    if (!isStarted_) {
        logger_.warning("MqttHandler未启动，无需停止");
        return;
    }
    
    // 1. 取消订阅
    if (mqttClient_.isConnected()) {
        mqttClient_.unsubscribe(servicesTopicSub_);
        logger_.debug("取消订阅: " + servicesTopicSub_);
        
        // 取消订阅飞行器 OSD
        if (osdSubscribed_ && !aircraftOsdTopicSub_.empty()) {
            mqttClient_.unsubscribe(aircraftOsdTopicSub_);
            logger_.debug("取消订阅飞行器OSD: " + aircraftOsdTopicSub_);
            osdSubscribed_ = false;
        }
        
        // 取消订阅机场 Events
        if (gatewayEventsSubscribed_ && !gatewayEventsTopicSub_.empty()) {
            mqttClient_.unsubscribe(gatewayEventsTopicSub_);
            logger_.debug("取消订阅机场Events: " + gatewayEventsTopicSub_);
            gatewayEventsSubscribed_ = false;
        }
    }
    
    // 2. 移除观察者
    mqttClient_.removeObserver(this);
    logger_.debug("已移除MQTT观察者");
    
    isStarted_ = false;
    logger_.info("MqttHandler已停止");
}

// ==================== 指令路由初始化 ====================

void MqttHandler::initCommandHandlers() {
    logger_.debug("初始化指令处理函数映射表...");
    
    // 使用lambda表达式注册处理函数
    
    // 1. 算法同步(启动任务)
    commandHandlers_["device_algorithm_sync"] = 
        [this](const nlohmann::json& data) {
            handleAlgorithmSync(data);
        };
    
    // 2. 启用算法
    commandHandlers_["device_algorithm_enable"] = 
        [this](const nlohmann::json& data) {
            handleAlgorithmEnable(data);
        };
    
    // 3. 关闭算法(停止任务)
    commandHandlers_["device_algorithm_disable"] = 
        [this](const nlohmann::json& data) {
            handleAlgorithmDisable(data);
        };
    
    // 4. 任务结束
    commandHandlers_["device_task_end"] = 
        [this](const nlohmann::json& data) {
            handleTaskEnd(data);
        };
    
    // 5. 设备重启
    commandHandlers_["device_reboot"] = 
        [this](const nlohmann::json& data) {
            handleReboot(data);
        };
    
    logger_.info("注册了 " + std::to_string(commandHandlers_.size()) + 
                " 个指令处理函数");
}

// ==================== IMqttMessageObserver接口实现 ====================

void MqttHandler::onMessageReceived(const std::string& topic,
                                    const std::string& message) {
    logger_.debug("收到消息 topic=" + topic + " 长度=" + 
                 std::to_string(message.length()));
    
    try {
        // 1. 解析JSON
        nlohmann::json json = nlohmann::json::parse(message);
        
        // 2. 根据topic路由
        if (topic == servicesTopicSub_) {
            // 云端服务调用指令
            handleServiceCommand(json);
        } else if (!aircraftOsdTopicSub_.empty() && topic == aircraftOsdTopicSub_) {
            // 飞行器 OSD 数据
            handleAircraftOsd(json);
        } else if (!gatewayEventsTopicSub_.empty() && topic == gatewayEventsTopicSub_) {
            // 机场 Events（包括航线任务进度）
            handleGatewayEvents(json);
        } else {
            logger_.warning("未知topic: " + topic);
        }
        
    } catch (const nlohmann::json::exception& e) {
        logger_.error("JSON解析失败: " + std::string(e.what()));
        logger_.error("原始消息: " + message);
    } catch (const std::exception& e) {
        logger_.error("处理消息异常: " + std::string(e.what()));
    }
}

void MqttHandler::onConnectionLost(const std::string& cause) {
    logger_.warning("🔴 MqttHandler::onConnectionLost() 被调用");
    logger_.warning("MQTT连接丢失: " + cause);
    logger_.info("MqttClient将自动重连，无需手动处理");
    logger_.debug("🔴 MqttHandler::onConnectionLost() 执行完毕");
}

void MqttHandler::onConnected(const std::string& serverUri) {
    logger_.info("🟢 MqttHandler::onConnected() 被调用");
    logger_.info("MQTT连接成功: " + serverUri);
    logger_.debug("🟢 MqttHandler::onConnected() 执行完毕");
}

void MqttHandler::onDeliveryComplete(int token) {
    logger_.debug("🟡 MqttHandler::onDeliveryComplete() 被调用，token=" + std::to_string(token));
    logger_.debug("🟡 MqttHandler::onDeliveryComplete() 执行完毕");
}

// ==================== 服务调用指令处理 ====================

void MqttHandler::handleServiceCommand(const nlohmann::json& json) {
    try {
        // 打印完整的JSON消息用于调试
        logger_.debug("收到服务调用原始JSON: " + json.dump());
        
        // 1. 提取method字段
        if (!json.contains("method")) {
            logger_.error("服务调用缺少method字段");
            logger_.error("JSON内容: " + json.dump());
            return;
        }
        
        std::string method = json.at("method").get<std::string>();
        logger_.info("处理服务调用: method=" + method);
        
        // 2. 提取data字段（兼容两种格式）
        nlohmann::json data;
        
        if (json.contains("data")) {
            // 标准格式: {"method": "xxx", "data": {...}}
            data = json.at("data");
        } else if (json.contains("params")) {
            // 兼容格式: {"method": "xxx", "params": {...}}
            data = json.at("params");
            logger_.debug("使用params字段作为data");
        } else {
            // 兼容格式: 除method外的所有字段都作为data
            data = json;
            data.erase("method");  // 移除method字段
            
            if (data.empty()) {
                logger_.error("服务调用缺少data/params字段: method=" + method);
                logger_.error("完整JSON: " + json.dump());
                sendServiceReply(method, 1, 0);  // 1=参数错误
                return;
            }
            
            logger_.debug("使用JSON根字段作为data");
        }
        
        logger_.debug("提取的data内容: " + data.dump());
        
        // 3. 查找对应的处理函数
        auto it = commandHandlers_.find(method);
        if (it != commandHandlers_.end()) {
            // 调用处理函数
            it->second(data);
        } else {
            logger_.warning("未知的服务调用方法: " + method);
            sendServiceReply(method, 255, 0);  // 255=未知错误
        }
        
    } catch (const nlohmann::json::exception& e) {
        logger_.error("解析服务调用失败: " + std::string(e.what()));
        logger_.error("原始JSON: " + json.dump());
    }
}

// ==================== 具体指令处理函数 ====================

void MqttHandler::handleAlgorithmSync(const nlohmann::json& data) {
    /**
     * 📖 算法同步 (device_algorithm_sync)
     * 
     * 功能: 下载并安装算法包到设备
     * 
     * 请求格式: {
     *   "algorithm_list": [
     *     {
     *       "id": 1234,          // 算法仓ID
     *       "name": "算法名称",
     *       "version": "v1.0",
     *       "url": "下载地址",
     *       "type": [...]        // 算法类型列表
     *     }
     *   ]
     * }
     * 
     * 注意: ❌ 没有 taskID 字段！这个指令只是同步算法包，不启动任务
     * 
     * 应答格式: {
     *   "result": 0,  // 0=成功, 1=错误, 255=未知错误
     *   "method": "device_algorithm_sync"
     * }
     */
    logger_.info("处理算法同步指令...");
    logger_.debug("算法同步数据: " + data.dump());
    
    try {
        // 1. 验证algorithm_list字段
        if (!data.contains("algorithm_list")) {
            logger_.error("算法同步指令缺少algorithm_list字段");
            sendServiceReply("device_algorithm_sync", 1, 0);
            return;
        }
        
        const auto& algorithmList = data.at("algorithm_list");
        if (!algorithmList.is_array() || algorithmList.empty()) {
            logger_.error("algorithm_list字段格式错误或为空");
            sendServiceReply("device_algorithm_sync", 1, 0);
            return;
        }
        
        // 2. 遍历算法包列表
        logger_.info("收到 " + std::to_string(algorithmList.size()) + " 个算法包");
        
        for (const auto& algorithm : algorithmList) {
            // 提取算法包信息
            int algorithmId = algorithm.value("id", 0);
            std::string name = algorithm.value("name", "");
            std::string version = algorithm.value("version", "");
            std::string url = algorithm.value("url", "");
            
            logger_.info("算法包: id=" + std::to_string(algorithmId) + 
                        " name=" + name + " version=" + version);
            logger_.debug("下载地址: " + url);
            
            // TODO: 实现算法包下载和安装逻辑
            // 1. 从url下载算法包
            // 2. 验证算法包完整性
            // 3. 解压并安装到指定目录
            // 4. 更新本地算法包列表
        }
        
        // 3. 发送成功应答（目前只是占位实现）
        logger_.info("算法同步完成（占位实现）");
        sendServiceReply("device_algorithm_sync", 0, 0);
        
    } catch (const nlohmann::json::exception& e) {
        logger_.error("解析算法同步数据失败: " + std::string(e.what()));
        logger_.error("数据内容: " + data.dump());
        sendServiceReply("device_algorithm_sync", 1, 0);
    } catch (const std::exception& e) {
        logger_.error("处理算法同步异常: " + std::string(e.what()));
        sendServiceReply("device_algorithm_sync", 255, 0);
    }
}

void MqttHandler::handleAlgorithmEnable(const nlohmann::json& data) {
    /**
     * 📖 算法启用 (device_algorithm_enable)
     * 
     * 功能: 启动任务，开始算法分析
     * 
     * 请求格式: {
     *   "id": 1234,              // 算法仓ID
     *   "name": "算法名称",
     *   "version": "v1.0",
     *   "type": [...],           // 算法类型列表
     *   "taskID": 1234,          // ✅ 任务ID（这里才有！）
     *   "source": 0,             // 0=视频流, 1=图片
     *   "display": 0,            // 是否显示结果
     *   "airtransfer": 0         // 是否空中回传
     * }
     * 
     * 应答格式: {
     *   "taskID": 1234,
     *   "result": 0,
     *   "method": "device_algorithm_enable"
     * }
     */
    logger_.info("处理启用算法指令...");
    logger_.debug("启用算法数据: " + data.dump());
    
    try {
        // 1. 验证taskID字段
        if (!data.contains("taskID")) {
            logger_.error("启用算法指令缺少taskID字段");
            sendServiceReply("device_algorithm_enable", 1, 0);
            return;
        }
        
        // 2. 解析为 TaskConfig (统一使用 task::TaskConfig)
        task::TaskConfig config = task::TaskConfig::fromJson(data);
        
        logger_.debug("解析TaskConfig: taskId=" + config.taskId +
                     " algorithmId=" + std::to_string(config.algorithmId));
        
        // 3. 验证配置有效性
        logger_.info("📋 [诊断] 验证任务配置...");
        logger_.info("  - taskId: " + config.taskId);
        logger_.info("  - type: " + std::to_string(static_cast<int>(config.type)));
        logger_.info("  - source: " + std::to_string(static_cast<int>(config.source)));
        logger_.info("  - confidenceThreshold: " + std::to_string(config.confidenceThreshold));
        logger_.info("  - mediaPath: " + config.mediaPath);
        logger_.info("  - enableRTMP: " + std::to_string(config.enableRTMP));
        logger_.info("  - rtmpUrl: " + config.rtmpUrl);
        logger_.info("  - deviceSn: " + config.deviceSn);
        logger_.info("  - eventTypes.size: " + std::to_string(config.eventTypes.size()));
        
        if (!config.isValid()) {
            logger_.error("❌ 任务配置验证失败: taskId=" + config.taskId);
            sendServiceReply("device_algorithm_enable", 1, std::stoi(config.taskId));
            return;
        }
        
        logger_.info("✅ 任务配置验证通过");
        
        // 4. 调用 TaskManager 启动任务
        bool success = taskManager_.startTask(config);
        
        // 5. 发送应答
        int taskIdInt = std::stoi(config.taskId);
        if (success) {
            logger_.info("任务启动成功: taskId=" + config.taskId);
            sendServiceReply("device_algorithm_enable", 0, taskIdInt);
        } else {
            logger_.error("任务启动失败: taskId=" + config.taskId);
            sendServiceReply("device_algorithm_enable", 3, taskIdInt);  // 3=资源不足
        }
        
    } catch (const nlohmann::json::exception& e) {
        logger_.error("解析启用算法数据失败: " + std::string(e.what()));
        logger_.error("数据内容: " + data.dump());
        sendServiceReply("device_algorithm_enable", 1, 0);
    } catch (const std::exception& e) {
        logger_.error("处理启用算法异常: " + std::string(e.what()));
        sendServiceReply("device_algorithm_enable", 255, 0);
    }
}

void MqttHandler::handleAlgorithmDisable(const nlohmann::json& data) {
    logger_.info("处理关闭算法指令...");
    
    try {
        // 1. 提取taskID
        if (!data.contains("taskID")) {
            logger_.error("关闭算法指令缺少taskID字段");
            sendServiceReply("device_algorithm_disable", 1, 0);
            return;
        }
        
        int taskId = data.at("taskID").get<int>();
        std::string taskIdStr = std::to_string(taskId);
        
        logger_.debug("关闭任务: taskID=" + taskIdStr);
        
        // 2. 调用TaskManager停止任务
        bool success = taskManager_.stopTask(taskIdStr);
        
        // 3. 发送应答
        if (success) {
            logger_.info("任务停止成功: taskID=" + taskIdStr);
            sendServiceReply("device_algorithm_disable", 0, taskId);
        } else {
            logger_.error("任务停止失败(任务不存在): taskID=" + taskIdStr);
            sendServiceReply("device_algorithm_disable", 2, taskId);  // 2=任务不存在
        }
        
    } catch (const nlohmann::json::exception& e) {
        logger_.error("解析关闭算法数据失败: " + std::string(e.what()));
        sendServiceReply("device_algorithm_disable", 1, 0);
    } catch (const std::exception& e) {
        logger_.error("处理关闭算法异常: " + std::string(e.what()));
        sendServiceReply("device_algorithm_disable", 255, 0);
    }
}

void MqttHandler::handleTaskEnd(const nlohmann::json& data) {
    /**
     * 📖 任务结束通知 (device_task_end)
     * 
     * 功能: 通知设备航线任务已结束（飞机已返航）
     * 
     * ⚠️ 重要: 不同任务类型的处理逻辑不同！
     * 
     * LiveStreamTask (视频流任务):
     * - 收到此消息后 **立即停止任务**
     * - 推送 device_task_analysis_result (任务分析完成)
     * - 等待平台发送 device_algorithm_disable
     * 
     * MediaFileTask (媒体文件任务):
     * - 收到此消息后 **仅标记航线结束标志**，不停止任务
     * - 继续等待文件传输完成（可能需要几分钟）
     * - 当 60 秒无新文件 + taskEnded_ 标志 → 自动推送完成消息
     * 
     * 请求格式: {
     *   "taskID": 1234
     * }
     * 
     * 应答格式: {
     *   "taskID": 1234,
     *   "result": 0,
     *   "method": "device_task_end"
     * }
     * 
     * 📌 设计模式: 使用多态（虚函数）避免类型判断
     */
    logger_.info("处理任务结束指令...");
    
    try {
        // 1. 提取 taskID
        if (!data.contains("taskID")) {
            logger_.error("任务结束指令缺少taskID字段");
            sendServiceReply("device_task_end", 1, 0);
            return;
        }
        
        int taskId = data.at("taskID").get<int>();
        std::string taskIdStr = std::to_string(taskId);
        
        logger_.info("📥 收到 device_task_end: taskID=" + taskIdStr);
        
        // 2. 立即发送应答 ⭐（先回复，再处理）
        sendServiceReply("device_task_end", 0, taskId);
        logger_.info("✅ 已回复 device_task_end 成功");
        
        // 3. 获取任务
        auto task = taskManager_.getTask(taskIdStr);
        if (!task) {
            logger_.warning("⚠️ 任务不存在: " + taskIdStr);
            return;
        }
        
        // 4. 多态调用 onTaskEnd() ⭐⭐⭐
        // 不需要判断任务类型（dynamic_cast），由各任务类自己实现逻辑
        // - LiveStreamTask::onTaskEnd() → 立即停止任务
        // - MediaFileTask::onTaskEnd() → 仅标记结束标志
        task->onTaskEnd();
        
        logger_.info("✅ 任务 onTaskEnd() 调用完成: " + taskIdStr);
        
    } catch (const nlohmann::json::exception& e) {
        logger_.error("解析任务结束数据失败: " + std::string(e.what()));
        sendServiceReply("device_task_end", 1, 0);
    } catch (const std::exception& e) {
        logger_.error("处理任务结束异常: " + std::string(e.what()));
        sendServiceReply("device_task_end", 255, 0);
    }
}

void MqttHandler::handleReboot(const nlohmann::json& data) {
    logger_.warning("收到设备重启指令!");
    
    (void)data;  // 可能为空，避免未使用警告
    
    try {
        // 1. 停止所有任务
        logger_.info("停止所有运行中的任务...");
        auto taskIds = taskManager_.getRunningTaskIds();
        for (const auto& taskId : taskIds) {
            taskManager_.stopTask(taskId);
        }
        
        // 2. 发送应答
        sendServiceReply("device_reboot", 0, 0);
        
        // 3. 等待应答发送完成
        logger_.info("等待应答发送完成...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 4. 执行重启命令
        logger_.warning("执行系统重启...");
        
        // ⚠️ 实际项目中取消注释
        // system("reboot");
        
        logger_.warning("重启命令已注释，仅日志记录");
        
    } catch (const std::exception& e) {
        logger_.error("处理重启指令异常: " + std::string(e.what()));
        sendServiceReply("device_reboot", 255, 0);
    }
}

// ==================== 应答和上报函数 ====================

void MqttHandler::sendServiceReply(const std::string& method,
                                   int result,
                                   int taskId) {
    try {
        // 1. 构建应答JSON
        nlohmann::json reply;
        reply["result"] = result;
        reply["method"] = method;
        reply["taskID"] = taskId;
        
        // 2. 如果失败，添加错误描述
        if (result != 0) {
            std::string errorMsg;
            switch (result) {
                case 1:
                    errorMsg = "参数错误";
                    break;
                case 2:
                    errorMsg = "任务不存在";
                    break;
                case 3:
                    errorMsg = "资源不足";
                    break;
                default:
                    errorMsg = "未知错误";
                    break;
            }
            reply["message"] = errorMsg;
        }
        
        // 3. 序列化为字符串
        std::string payload = reply.dump();
        
        // 4. 所有消息都使用 QoS 0（根据用户需求，不等待确认）
        int qos = 0;
        
        // 5. 发布到MQTT
        bool success = mqttClient_.publish(servicesTopicPub_, payload, qos);
        
        if (success) {
            logger_.debug("发送应答成功: method=" + method + 
                         " result=" + std::to_string(result) +
                         " taskID=" + std::to_string(taskId) +
                         " qos=" + std::to_string(qos));
        } else {
            logger_.error("发送应答失败: method=" + method);
        }
        
    } catch (const std::exception& e) {
        logger_.error("构建应答消息异常: " + std::string(e.what()));
    }
}

void MqttHandler::publishEvent(const nlohmann::json& eventData) {
    try {
        std::string payload = eventData.dump();
        
        bool success = mqttClient_.publish(eventsTopicPub_, payload, 0);  // QoS 0（不等待确认）
        
        if (success) {
            logger_.debug("发布事件成功");
        } else {
            logger_.error("发布事件失败");
        }
        
    } catch (const std::exception& e) {
        logger_.error("发布事件异常: " + std::string(e.what()));
    }
}

// ==================== OSD 数据处理 ====================

void MqttHandler::handleAircraftOsd(const nlohmann::json& json) {
    /**
     * DJI 飞行器 OSD 数据格式 (topic: thing/product/{aircraft_sn}/osd)
     * 
     * 根据 DJI Cloud API 文档，OSD 数据结构如下：
     * {
     *   "tid": "xxx",
     *   "bid": "xxx", 
     *   "timestamp": 1234567890123,
     *   "data": {
     *     "latitude": 22.1234,          // 纬度 (-90 ~ 90)
     *     "longitude": 113.5678,        // 经度 (-180 ~ 180)
     *     "height": 100.5,              // 椭球高度 (m)
     *     "altitude": 50.2,             // 相对起飞点海拔 (m)
     *     "attitude_head": 90.0,        // 机头朝向/偏航角 (度)
     *     "attitude_pitch": 5.0,        // 俯仰角 (度)
     *     "attitude_roll": 2.0,         // 横滚角 (度)
     *     "horizontal_speed": 10.5,     // 水平速度 (m/s)
     *     "vertical_speed": 1.2,        // 垂直速度 (m/s)
     *     ...其他字段略...
     *   }
     * }
     * 
     * 推送频率: 0.5Hz (每2秒一次)
     */
    
    try {
        // 检查 data 字段是否存在
        if (!json.contains("data")) {
            logger_.warning("OSD消息缺少data字段");
            return;
        }
        
        const auto& data = json["data"];
        
        // 提取 GPS 位置
        double latitude = data.value("latitude", 0.0);
        double longitude = data.value("longitude", 0.0);
        double height = data.value("height", 0.0);
        double altitude = data.value("altitude", 0.0);
        
        // 提取姿态
        double attitudeHead = data.value("attitude_head", 0.0);
        double attitudePitch = data.value("attitude_pitch", 0.0);
        double attitudeRoll = data.value("attitude_roll", 0.0);
        
        // 提取速度
        double horizontalSpeed = data.value("horizontal_speed", 0.0);
        double verticalSpeed = data.value("vertical_speed", 0.0);
        
        // 提取飞行模式 (mode_code)
        // mode_code 是枚举值，表示无人机当前状态
        // 9 = 自动返航 (RTH), 10 = 自动降落, 11 = 强制降落
        int modeCode = data.value("mode_code", 0);
        
        // 更新 FlightDataProvider
        auto& flightData = device::FlightDataProvider::getInstance();
        
        flightData.updateGpsPosition(latitude, longitude, height, altitude);
        flightData.updateAttitude(attitudeHead, attitudePitch, attitudeRoll);
        flightData.updateVelocity(horizontalSpeed, verticalSpeed);
        flightData.updateFlightMode(modeCode);
        
        // 每次收到有效 GPS 数据时记录（但不频繁输出日志）
        static int osdCount = 0;
        static int lastModeCode = -1;  // 用于检测飞行模式变化
        osdCount++;
        
        // 当飞行模式改变时，立即记录
        if (modeCode != lastModeCode) {
            logger_.info("✈️ 飞行模式变更: " + flightData.getFlightModeString() + 
                        " (mode_code=" + std::to_string(modeCode) + ")");
            
            // 如果切换到返航/降落状态，输出警告
            if (flightData.isReturningOrLanding()) {
                logger_.warning("⚠️ 无人机进入返航/降落状态，智能变焦拍照将暂停");
            }
            lastModeCode = modeCode;
        }
        
        if (osdCount == 1 || osdCount % 30 == 0) {  // 每分钟记录一次 (0.5Hz * 30 = 1分钟)
            logger_.info("📍 收到OSD数据: GPS(" + 
                        std::to_string(latitude) + ", " + 
                        std::to_string(longitude) + "), 高度=" +
                        std::to_string(height) + "m, 航向=" +
                        std::to_string(attitudeHead) + "°");
        }
        
    } catch (const std::exception& e) {
        logger_.error("解析OSD数据失败: " + std::string(e.what()));
    }
}

// ==================== 机场事件处理 ====================

/**
 * @brief 处理机场 Events 消息
 * 
 * @param json Events JSON 数据
 * 
 * @details
 * 主要处理 flighttask_progress 事件，用于获取航线任务状态。
 * 当航线任务状态为 5（进入航线，到达第一个航点）或 6（航线执行中）时，
 * 才允许启用智能变焦拍照，避免在起飞、爬升过程中触发拍照干扰流程。
 * 
 * DJI Cloud API wayline_mission_state 枚举：
 * - 0: 断连
 * - 1: 不支持该航点
 * - 2: 航线准备状态
 * - 3: 航线文件上传中
 * - 4: 飞行器准备中
 * - 5: ⭐ 进入航线，到达第一个航点
 * - 6: ⭐ 航线执行中
 * - 7: 航线中断
 * - 8: 航线恢复中
 * - 9: 航线停止
 */
void MqttHandler::handleGatewayEvents(const nlohmann::json& json) {
    try {
        // 检查是否有 method 字段
        if (!json.contains("method")) {
            return;  // 不是事件格式，忽略
        }
        
        std::string method = json.at("method").get<std::string>();
        
        // 只处理 flighttask_progress 事件
        if (method != "flighttask_progress") {
            return;
        }
        
        // 解析航线任务进度
        // 格式: data.output.ext.wayline_mission_state
        if (!json.contains("data") || !json.at("data").contains("output")) {
            return;
        }
        
        auto output = json.at("data").at("output");
        if (!output.contains("ext")) {
            return;
        }
        
        auto ext = output.at("ext");
        if (!ext.contains("wayline_mission_state")) {
            return;
        }
        
        int waylineMissionState = ext.at("wayline_mission_state").get<int>();
        
        // 更新 FlightDataProvider
        auto& flightData = device::FlightDataProvider::getInstance();
        
        // 检测状态变化
        auto oldState = flightData.getWaylineMissionState();
        flightData.updateWaylineMissionState(waylineMissionState);
        auto newState = flightData.getWaylineMissionState();
        
        // 状态变化时记录日志
        if (static_cast<int>(oldState) != static_cast<int>(newState)) {
            logger_.info("🛫 航线任务状态变更: " + 
                        device::waylineMissionStateToString(oldState) + " → " +
                        device::waylineMissionStateToString(newState) + 
                        " (state=" + std::to_string(waylineMissionState) + ")");
            
            // 关键状态变化时的提示
            if (newState == device::WaylineMissionState::kEnteringFirstWaypoint) {
                logger_.info("✅ 到达第一个航点，智能变焦拍照已启用");
            } else if (newState == device::WaylineMissionState::kExecuting) {
                logger_.info("✅ 航线执行中，智能变焦拍照持续启用");
            } else if (newState == device::WaylineMissionState::kInterrupted) {
                logger_.warning("⚠️ 航线中断，智能变焦拍照已暂停");
            } else if (newState == device::WaylineMissionState::kStopped) {
                logger_.info("📌 航线停止，智能变焦拍照已禁用");
            }
        }
        
        // 提取其他有用信息用于日志
        if (ext.contains("current_waypoint_index")) {
            int waypointIndex = ext.at("current_waypoint_index").get<int>();
            logger_.debug("   当前航点: " + std::to_string(waypointIndex));
        }
        
    } catch (const std::exception& e) {
        logger_.error("解析机场Events失败: " + std::string(e.what()));
    }
}

// ==================== 废弃方法 ====================
// publishDetectionResult() 已删除
// 原因：types::DetectionResult 已废弃，现在使用 EventCache 模式
// 如果需要上报检测结果，请使用：
//   core::EventCache::getInstance().publishEvent(topic, eventJson)

}  // namespace mqtt
}  // namespace esdk_sophon
