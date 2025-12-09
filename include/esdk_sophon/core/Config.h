/**
 * @file Config.h
 * @brief 配置管理系统（单例模式）
 * @author ESDK_Sophon Team
 * @date 2025-10-25
 * 
 * @details
 * 功能特性：
 * - JSON配置文件解析
 * - 类型安全的配置读取
 * - 支持嵌套路径访问（如"logger.level"）
 * - 默认值支持
 * - 线程安全
 */

#ifndef ESDK_SOPHON_CORE_CONFIG_H_
#define ESDK_SOPHON_CORE_CONFIG_H_

#include <string>
#include <memory>

namespace esdk_sophon {
namespace core {

/**
 * @brief 配置管理类（单例模式）
 * 
 * @details
 * 使用方法：
 * @code
 * auto& config = Config::getInstance();
 * config.load("config/config.json");
 * 
 * std::string level = config.getString("logger.level", "INFO");
 * int port = config.getInt("mqtt.port", 1883);
 * bool enable = config.getBool("logger.console_output", true);
 * @endcode
 * 
 * 【知识点】单例模式 + JSON解析
 * - 全局唯一的配置管理实例
 * - 线程安全的访问
 * - 使用nlohmann/json库解析JSON
 * 
 * 【面试要点】
 * Q: 为什么配置管理要用单例？
 * A: 1. 配置应该全局唯一，避免多份配置不一致
 *    2. 方便全局访问，不需要传递Config对象
 *    3. 节省内存，不重复加载配置文件
 */
class Config {
public:
    /**
     * @brief 获取Config单例
     * 
     * @return Config的全局唯一实例
     * 
     * @details
     * 线程安全的单例实现（Meyers Singleton）
     * C++11保证静态局部变量初始化的线程安全性
     */
    static Config& getInstance();
    
    // 禁止拷贝和赋值
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    /**
     * @brief 加载配置文件
     * 
     * @param configPath 配置文件路径（相对或绝对路径）
     * @return true 加载成功，false 加载失败
     * 
     * @details
     * 支持JSON格式的配置文件
     * 加载失败时使用默认配置
     * 
     * @code
     * Config::getInstance().load("config/config.json");
     * @endcode
     */
    bool load(const std::string& configPath);
    
    /**
     * @brief 重新加载配置文件
     * 
     * @return true 加载成功，false 加载失败
     * 
     * @details
     * 用于配置热更新，不需要重启程序
     * 
     * @code
     * // 修改配置文件后
     * Config::getInstance().reload();
     * @endcode
     */
    bool reload();
    
    /**
     * @brief 获取字符串配置项
     * 
     * @param key 配置键（支持嵌套路径，如"logger.level"）
     * @param defaultValue 默认值（键不存在时返回）
     * @return 配置值或默认值
     * 
     * @details
     * 【知识点】嵌套路径访问
     * 使用点号(.)分隔多层键值：
     * - "logger.level" → config["logger"]["level"]
     * - "mqtt.broker.host" → config["mqtt"]["broker"]["host"]
     * 
     * @code
     * std::string level = config.getString("logger.level", "INFO");
     * std::string host = config.getString("mqtt.broker.host", "localhost");
     * @endcode
     */
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    
    /**
     * @brief 获取整数配置项
     * 
     * @param key 配置键
     * @param defaultValue 默认值
     * @return 配置值或默认值
     * 
     * @code
     * int port = config.getInt("mqtt.port", 1883);
     * int maxDays = config.getInt("logger.rotation.max_days", 15);
     * @endcode
     */
    int getInt(const std::string& key, int defaultValue = 0) const;
    
    /**
     * @brief 获取布尔配置项
     * 
     * @param key 配置键
     * @param defaultValue 默认值
     * @return 配置值或默认值
     * 
     * @code
     * bool enable = config.getBool("logger.console_output", true);
     * bool enableRotation = config.getBool("logger.rotation.enable", true);
     * @endcode
     */
    bool getBool(const std::string& key, bool defaultValue = false) const;
    
    /**
     * @brief 获取浮点数配置项
     * 
     * @param key 配置键
     * @param defaultValue 默认值
     * @return 配置值或默认值
     * 
     * @code
     * double threshold = config.getDouble("detector.confidence_threshold", 0.5);
     * @endcode
     */
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    
    /**
     * @brief 检查配置项是否存在
     * 
     * @param key 配置键
     * @return true 存在，false 不存在
     * 
     * @code
     * if (config.has("logger.log_directory")) {
     *     // 使用配置的目录
     * } else {
     *     // 使用默认目录
     * }
     * @endcode
     */
    bool has(const std::string& key) const;
    
    /**
     * @brief 获取配置文件路径
     * 
     * @return 当前加载的配置文件路径
     */
    std::string getConfigPath() const;

private:
    /**
     * @brief 私有构造函数（单例模式）
     * 
     * @details
     * 只能通过getInstance()访问
     * 保证全局唯一实例
     */
    Config();
    
    /**
     * @brief 析构函数
     */
    ~Config();
    
    /**
     * @brief Pimpl实现类（前向声明）
     * 
     * @details
     * 隐藏实现细节，减少头文件依赖
     * 避免在头文件中包含<nlohmann/json.hpp>
     */
    class Impl;
    std::unique_ptr<Impl> pImpl_;  ///< 指向实现的智能指针
};

}  // namespace core
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_CORE_CONFIG_H_

// ============================================================================
// 📚 使用示例
// ============================================================================
// 
// 1. 加载配置文件
//    auto& config = Config::getInstance();
//    config.load("config/config.json");
// 
// 2. 读取配置
//    std::string logLevel = config.getString("logger.level", "INFO");
//    int mqttPort = config.getInt("mqtt.port", 1883);
//    bool enableConsole = config.getBool("logger.console_output", true);
// 
// 3. 嵌套路径访问
//    int maxDays = config.getInt("logger.rotation.max_days", 15);
//    std::string brokerHost = config.getString("mqtt.broker.host", "localhost");
// 
// 4. 配置热更新
//    config.reload();  // 重新加载配置文件
// 
// 5. 检查配置是否存在
//    if (config.has("detector.model_path")) {
//        std::string modelPath = config.getString("detector.model_path");
//    }
// 
// ============================================================================
