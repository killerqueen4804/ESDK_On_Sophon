/**
 * @file Config.cpp
 * @brief 配置管理系统实现
 * @author ESDK_Sophon Team
 * @date 2025-10-25
 */

#include "esdk_sophon/core/Config.h"
#include "esdk_sophon/core/Logger.h"

#include <fstream>
#include <sstream>
#include <mutex>
#include <nlohmann/json.hpp>

// 使用nlohmann::json的别名
using json = nlohmann::json;

namespace esdk_sophon {
namespace core {

// ============================================================================
// Pimpl实现类定义
// ============================================================================

/**
 * @brief Config的实现类（Pimpl惯用法）
 * 
 * @details
 * 隐藏nlohmann/json的依赖，避免在头文件中包含
 * 好处：
 * 1. 减少编译依赖
 * 2. 隐藏第三方库细节
 * 3. 加快编译速度
 */
class Config::Impl {
public:
    Impl() : configLoaded_(false) {}
    
    ~Impl() = default;
    
    /**
     * @brief 加载配置文件
     * 
     * @param configPath 配置文件路径
     * @return true 成功，false 失败
     * 
     * @details
     * 【知识点】JSON解析异常处理
     * - std::ifstream: 文件输入流
     * - json::parse(): 解析JSON文本
     * - try-catch: 捕获解析异常
     */
    bool load(const std::string& configPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // 1. 打开配置文件
            std::ifstream file(configPath);
            if (!file.is_open()) {
                Logger::getInstance().error("无法打开配置文件: " + configPath);
                return false;
            }
            
            // 2. 解析JSON
            configData_ = json::parse(file);
            
            // 3. 记录配置路径
            configPath_ = configPath;
            configLoaded_ = true;
            
            Logger::getInstance().info("配置文件加载成功: " + configPath);
            return true;
            
        } catch (const json::parse_error& e) {
            Logger::getInstance().error("JSON解析错误: " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            Logger::getInstance().error("加载配置文件失败: " + std::string(e.what()));
            return false;
        }
    }
    
    /**
     * @brief 重新加载配置文件
     */
    bool reload() {
        if (configPath_.empty()) {
            Logger::getInstance().warning("未加载配置文件，无法重新加载");
            return false;
        }
        
        Logger::getInstance().info("重新加载配置文件: " + configPath_);
        return load(configPath_);
    }
    
    /**
     * @brief 获取字符串配置项
     * 
     * @details
     * 【知识点】嵌套JSON访问
     * nlohmann/json访问嵌套值的方式：
     * 1. 方式一：使用operator[] 连续访问
     *    json j; j["logger"]["level"] → "INFO"
     * 2. 方式二：使用JSON Pointer字符串
     *    json j; j["/logger/level"_json_pointer] → "INFO"
     * 
     * 【面试要点】
     * Q: 如何安全地访问嵌套JSON？
     * A: 1. 使用at()方法会抛出异常
     *    2. 使用value()方法提供默认值
     *    3. 捕获异常处理不存在的键
     *    4. 手动解析路径，逐层检查
     */
    std::string getString(const std::string& key, const std::string& defaultValue) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!configLoaded_) {
            return defaultValue;
        }
        
        try {
            // 手动解析嵌套路径：将"logger.level"分割为["logger", "level"]
            json current = configData_;
            std::string currentKey;
            
            for (char c : key) {
                if (c == '.') {
                    // 遇到点号，访问当前层级
                    if (!currentKey.empty()) {
                        if (!current.contains(currentKey)) {
                            return defaultValue;
                        }
                        current = current[currentKey];
                        currentKey.clear();
                    }
                } else {
                    currentKey += c;
                }
            }
            
            // 处理最后一个键
            if (!currentKey.empty()) {
                if (!current.contains(currentKey)) {
                    return defaultValue;
                }
                
                // 获取最终值，确保类型正确
                const auto& value = current[currentKey];
                if (value.is_string()) {
                    return value.get<std::string>();
                }
            }
            
            return defaultValue;
            
        } catch (const std::exception& e) {
            Logger::getInstance().debug("配置项不存在: " + key + ", 使用默认值: " + defaultValue);
            return defaultValue;
        }
    }
    
    /**
     * @brief 获取整数配置项
     */
    int getInt(const std::string& key, int defaultValue) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!configLoaded_) {
            return defaultValue;
        }
        
        try {
            // 解析嵌套路径
            json current = configData_;
            std::string currentKey;
            
            for (char c : key) {
                if (c == '.') {
                    if (!currentKey.empty()) {
                        if (!current.contains(currentKey)) {
                            return defaultValue;
                        }
                        current = current[currentKey];
                        currentKey.clear();
                    }
                } else {
                    currentKey += c;
                }
            }
            
            if (!currentKey.empty()) {
                if (!current.contains(currentKey)) {
                    return defaultValue;
                }
                
                const auto& value = current[currentKey];
                if (value.is_number_integer()) {
                    return value.get<int>();
                }
            }
            
            return defaultValue;
            
        } catch (const std::exception& e) {
            Logger::getInstance().debug("配置项不存在: " + key + ", 使用默认值: " + std::to_string(defaultValue));
            return defaultValue;
        }
    }
    
    /**
     * @brief 获取布尔配置项
     */
    bool getBool(const std::string& key, bool defaultValue) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!configLoaded_) {
            return defaultValue;
        }
        
        try {
            // 解析嵌套路径
            json current = configData_;
            std::string currentKey;
            
            for (char c : key) {
                if (c == '.') {
                    if (!currentKey.empty()) {
                        if (!current.contains(currentKey)) {
                            return defaultValue;
                        }
                        current = current[currentKey];
                        currentKey.clear();
                    }
                } else {
                    currentKey += c;
                }
            }
            
            if (!currentKey.empty()) {
                if (!current.contains(currentKey)) {
                    return defaultValue;
                }
                
                const auto& value = current[currentKey];
                if (value.is_boolean()) {
                    return value.get<bool>();
                }
            }
            
            return defaultValue;
            
        } catch (const std::exception& e) {
            Logger::getInstance().debug("配置项不存在: " + key);
            return defaultValue;
        }
    }
    
    /**
     * @brief 获取浮点数配置项
     */
    double getDouble(const std::string& key, double defaultValue) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!configLoaded_) {
            return defaultValue;
        }
        
        try {
            // 解析嵌套路径
            json current = configData_;
            std::string currentKey;
            
            for (char c : key) {
                if (c == '.') {
                    if (!currentKey.empty()) {
                        if (!current.contains(currentKey)) {
                            return defaultValue;
                        }
                        current = current[currentKey];
                        currentKey.clear();
                    }
                } else {
                    currentKey += c;
                }
            }
            
            if (!currentKey.empty()) {
                if (!current.contains(currentKey)) {
                    return defaultValue;
                }
                
                const auto& value = current[currentKey];
                if (value.is_number()) {
                    return value.get<double>();
                }
            }
            
            return defaultValue;
            
        } catch (const std::exception& e) {
            Logger::getInstance().debug("配置项不存在: " + key);
            return defaultValue;
        }
    }
    
    /**
     * @brief 检查配置项是否存在
     */
    bool has(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!configLoaded_) {
            return false;
        }
        
        try {
            // 解析嵌套路径
            json current = configData_;
            std::string currentKey;
            
            for (char c : key) {
                if (c == '.') {
                    if (!currentKey.empty()) {
                        if (!current.contains(currentKey)) {
                            return false;
                        }
                        current = current[currentKey];
                        currentKey.clear();
                    }
                } else {
                    currentKey += c;
                }
            }
            
            // 检查最后一个键
            if (!currentKey.empty()) {
                return current.contains(currentKey);
            }
            
            return false;
            
        } catch (const std::exception&) {
            return false;
        }
    }
    
    /**
     * @brief 获取配置文件路径
     */
    std::string getConfigPath() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return configPath_;
    }

private:
    json configData_;            ///< JSON配置数据
    std::string configPath_;     ///< 配置文件路径
    bool configLoaded_;          ///< 配置是否已加载
    mutable std::mutex mutex_;   ///< 互斥锁（保护多线程访问）
};

// ============================================================================
// Config公共接口实现（单例模式）
// ============================================================================

/**
 * @brief 获取Config单例
 * 
 * @details
 * Meyers Singleton（C++11线程安全）
 * 同Logger::getInstance()的实现
 */
Config& Config::getInstance() {
    static Config instance;
    return instance;
}

/**
 * @brief 构造函数（私有）
 */
Config::Config() 
    : pImpl_(std::make_unique<Impl>())
{
}

/**
 * @brief 析构函数
 * 
 * @details
 * 必须在.cpp中定义（Pimpl要求）
 */
Config::~Config() = default;

// ============================================================================
// 配置加载接口
// ============================================================================

bool Config::load(const std::string& configPath) {
    return pImpl_->load(configPath);
}

bool Config::reload() {
    return pImpl_->reload();
}

// ============================================================================
// 配置读取接口
// ============================================================================

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    return pImpl_->getString(key, defaultValue);
}

int Config::getInt(const std::string& key, int defaultValue) const {
    return pImpl_->getInt(key, defaultValue);
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    return pImpl_->getBool(key, defaultValue);
}

double Config::getDouble(const std::string& key, double defaultValue) const {
    return pImpl_->getDouble(key, defaultValue);
}

bool Config::has(const std::string& key) const {
    return pImpl_->has(key);
}

std::string Config::getConfigPath() const {
    return pImpl_->getConfigPath();
}

}  // namespace core
}  // namespace esdk_sophon

// ============================================================================
// 📚 知识点总结 - Config实现
// ============================================================================
// 
// 【1. nlohmann/json库】
// ✅ 现代C++的JSON库（header-only）
// ✅ 使用方式：
//    #include <nlohmann/json.hpp>
//    using json = nlohmann::json;
//    
//    // 解析JSON
//    json j = json::parse(file);
//    
//    // 访问值
//    std::string name = j["name"];
//    int age = j["user"]["age"];
//    
//    // 使用JSON Pointer
//    std::string city = j.value(json::pointer("/address/city"), "Unknown");
// 
// 【2. JSON Pointer (RFC 6901)】
// ✅ 定义：用于定位JSON文档中特定值的字符串语法
// ✅ 格式：以"/"开头，用"/"分隔层级
// ✅ 例子：
//    {
//      "logger": {
//        "level": "INFO",
//        "rotation": {
//          "max_days": 15
//        }
//      }
//    }
//    
//    Pointer: "/logger/level" → "INFO"
//    Pointer: "/logger/rotation/max_days" → 15
// 
// 【3. 类型安全的配置访问】
// ✅ 问题：JSON是动态类型，C++是静态类型
// ✅ 解决：提供类型明确的接口
//    - getString(): 返回string
//    - getInt(): 返回int
//    - getBool(): 返回bool
//    - getDouble(): 返回double
// ✅ 好处：编译时类型检查，避免运行时类型错误
// 
// 【4. 默认值策略】
// ✅ 为什么需要默认值？
//    - 配置文件可能缺少某些项
//    - 新功能的配置项可能不存在
//    - 提供合理的降级方案
// ✅ 实现方式：
//    json::value(pointer, defaultValue)
// 
// 【5. 线程安全】
// ✅ 问题：多线程可能同时读取配置
// ✅ 解决：使用std::mutex保护
//    - mutable成员：允许const方法修改
//    - std::lock_guard：RAII风格加锁
// ✅ 性能：读多写少，可考虑读写锁（std::shared_mutex）
// 
// 【6. 异常处理】
// ✅ JSON解析可能抛出异常：
//    - json::parse_error: 格式错误
//    - json::type_error: 类型不匹配
//    - json::out_of_range: 键不存在
// ✅ 处理策略：
//    - try-catch捕获
//    - 记录日志
//    - 返回默认值
// 
// 【7. 面试要点】
// Q1: 为什么Config要用单例模式？
// A1: 1. 配置全局唯一，避免不一致
//     2. 方便访问，不需要传递对象
//     3. 节省内存，只加载一次
// 
// Q2: 如何实现配置的热更新？
// A2: reload()方法重新加载文件，无需重启程序
// 
// Q3: JSON Pointer是什么？
// A3: RFC 6901标准，用于定位JSON值的路径语法
// 
// Q4: 如何保证配置访问的线程安全？
// A4: 使用std::mutex保护，所有读写操作都加锁
// 
// Q5: Pimpl在Config中的作用？
// A5: 隐藏nlohmann/json依赖，减少头文件编译依赖
// 
// 【8. 项目应用】
// ✅ Logger使用：
//    auto& config = Config::getInstance();
//    config.load("config/config.json");
//    
//    std::string logDir = config.getString("logger.log_directory", "logs");
//    int maxDays = config.getInt("logger.rotation.max_days", 15);
// 
// ✅ MQTT使用：
//    std::string broker = config.getString("mqtt.broker.host", "localhost");
//    int port = config.getInt("mqtt.broker.port", 1883);
// 
// ✅ Detector使用：
//    std::string modelPath = config.getString("detector.yolov10.model_path");
//    double threshold = config.getDouble("detector.confidence_threshold", 0.5);
// 
// ============================================================================
