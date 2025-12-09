/**
 * @file HttpClient.h
 * @brief HTTP 客户端工具类
 * 
 * 提供简单的 HTTP POST 请求功能，用于与本地服务（如 GeoDecodeAPI）通信。
 * 使用 libcurl 实现，线程安全。
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-03
 */

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
 * 
 * 线程安全说明:
 * - 每次请求创建独立的 CURL 句柄（线程安全）
 * - 错误信息使用 thread_local 存储（线程隔离）
 * - 全局初始化需要在主线程调用一次（curl_global_init）
 * 
 * 使用示例:
 * @code
 * // 1. 程序启动时初始化（在 main 函数开头）
 * HttpClient::globalInit();
 * 
 * // 2. 发送请求
 * nlohmann::json request = {{"path", "/data/image.jpg"}};
 * nlohmann::json response;
 * if (HttpClient::post("http://127.0.0.1:8122/api", request, 5000, response)) {
 *     std::cout << "成功: " << response.dump() << std::endl;
 * } else {
 *     std::cerr << "失败: " << HttpClient::getLastError() << std::endl;
 * }
 * 
 * // 3. 程序退出时清理（在 main 函数末尾）
 * HttpClient::globalCleanup();
 * @endcode
 * 
 * 面试要点:
 * - libcurl: 跨平台的 HTTP/HTTPS 库
 * - thread_local: C++11 线程局部存储
 * - RAII: curl_easy_cleanup 自动清理资源
 * - 同步请求 vs 异步请求: 当前为同步（阻塞）
 */
class HttpClient {
public:
    /**
     * @brief 全局初始化（程序启动时调用一次）
     * 
     * 初始化 libcurl 全局环境。必须在任何 HTTP 请求之前调用，
     * 且必须在主线程调用（非线程安全）。
     * 
     * @note 对应 curl_global_init(CURL_GLOBAL_ALL)
     */
    static void globalInit();
    
    /**
     * @brief 全局清理（程序退出时调用一次）
     * 
     * 清理 libcurl 全局资源。应该在程序退出前调用，
     * 且必须在主线程调用（非线程安全）。
     * 
     * @note 对应 curl_global_cleanup()
     */
    static void globalCleanup();
    
    /**
     * @brief 发送 HTTP POST 请求
     * 
     * 以 JSON 格式发送 POST 请求，并解析 JSON 响应。
     * 
     * @param url 目标 URL（例如: "http://127.0.0.1:8122/api"）
     * @param jsonData 请求体（JSON 格式）
     * @param timeoutMs 超时时间（毫秒）
     * @param response 输出参数：响应体（JSON 格式）
     * @return true 请求成功且 HTTP 状态码为 200
     * @return false 请求失败（网络错误、HTTP 错误、JSON 解析错误）
     * 
     * @note 线程安全，可在多线程环境中并发调用
     * @note 同步阻塞调用，会等待响应或超时
     * @note 失败时可通过 getLastError() 获取错误信息
     * 
     * 错误类型:
     * - CURL 错误: 网络连接失败、DNS 解析失败等
     * - HTTP 错误: 状态码非 200（例如 404, 500）
     * - JSON 错误: 响应体不是有效的 JSON
     * 
     * 性能考虑:
     * - 每次调用创建新的 CURL 句柄（约 0.1ms 开销）
     * - 如果需要复用连接，可以使用 CURL easy handle pool
     * - 典型调用时间: 本地服务 1-10ms，远程服务 100-1000ms
     */
    static bool post(const std::string& url,
                    const nlohmann::json& jsonData,
                    int timeoutMs,
                    nlohmann::json& response);
    
    /**
     * @brief 获取最后一次错误信息
     * 
     * 返回当前线程最后一次 HTTP 请求的错误信息。
     * 
     * @return std::string 错误描述（例如: "CURL error: Timeout was reached"）
     * 
     * @note 使用 thread_local 存储，不同线程互不影响
     * @note 仅在 post() 返回 false 时有意义
     */
    static std::string getLastError();

private:
    /**
     * @brief libcurl 写入回调函数
     * 
     * 当接收到 HTTP 响应数据时，libcurl 会调用此函数。
     * 将接收到的数据追加到 std::string 缓冲区。
     * 
     * @param contents 数据指针
     * @param size 单个元素大小（字节）
     * @param nmemb 元素个数
     * @param userp 用户数据（std::string* 类型）
     * @return size_t 实际处理的字节数（size * nmemb）
     * 
     * @note 面试考点: libcurl 回调函数机制
     */
    static size_t writeCallback(void* contents, size_t size, 
                               size_t nmemb, void* userp);
    
    /**
     * @brief 线程局部错误信息存储
     * 
     * 使用 thread_local 确保每个线程有独立的错误信息，
     * 避免多线程竞争。
     * 
     * @note 面试考点: 
     * - thread_local vs static: thread_local 每线程一份，static 全局共享
     * - 适用场景: 无锁的线程安全错误处理
     */
    static thread_local std::string lastError_;
};

}  // namespace utils
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_UTILS_HTTP_CLIENT_H_
