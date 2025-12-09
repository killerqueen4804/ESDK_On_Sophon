/**
 * @file HttpClient.cpp
 * @brief HTTP 客户端实现
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-03
 */

#include "esdk_sophon/utils/HttpClient.h"
#include <curl/curl.h>
#include <algorithm>
#include <cstring>

namespace esdk_sophon {
namespace utils {

// 线程局部错误信息存储
thread_local std::string HttpClient::lastError_ = "";

// ==================== 全局初始化/清理 ====================

void HttpClient::globalInit() {
    // 初始化 libcurl 全局环境
    // CURL_GLOBAL_ALL: 初始化所有功能（SSL、Win32等）
    curl_global_init(CURL_GLOBAL_ALL);
}

void HttpClient::globalCleanup() {
    // 清理 libcurl 全局资源
    curl_global_cleanup();
}

// ==================== HTTP POST 请求 ====================

/**
 * @brief libcurl 写入回调函数
 * 
 * 工作原理:
 * 1. libcurl 接收到 HTTP 响应数据
 * 2. 调用此回调函数，传入数据指针和大小
 * 3. 回调函数将数据追加到 std::string 缓冲区
 * 4. 返回处理的字节数（必须等于 size * nmemb）
 * 
 * 面试要点:
 * - 为什么要返回接收的字节数？libcurl 用于判断是否成功处理
 * - 如果返回值不等于 size * nmemb，libcurl 认为出错并中止传输
 */
size_t HttpClient::writeCallback(void* contents, size_t size, 
                                 size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* buffer = static_cast<std::string*>(userp);
    
    // 将接收到的数据追加到缓冲区
    buffer->append(static_cast<char*>(contents), totalSize);
    
    return totalSize;  // 必须返回实际处理的字节数
}

bool HttpClient::post(const std::string& url,
                     const nlohmann::json& jsonData,
                     int timeoutMs,
                     nlohmann::json& response) {
    // 1. 初始化 CURL 句柄
    CURL* curl = curl_easy_init();
    if (!curl) {
        lastError_ = "Failed to initialize CURL (curl_easy_init returned NULL)";
        return false;
    }
    
    // 2. 准备请求数据
    std::string readBuffer;   // 接收响应体的缓冲区
    std::string postData = jsonData.dump();  // JSON 转字符串
    
    // 3. 设置 CURL 选项
    
    // 3.1 基本请求参数
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());              // 目标 URL
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str()); // POST 数据
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.size()); // 数据长度
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs)); // 超时
    
    // 3.2 响应处理
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback); // 写入回调
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);       // 缓冲区指针
    
    // 3.3 HTTP 头部
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // 3.4 安全和性能选项
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // 避免信号处理问题（多线程必须）
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L); // 禁用 Nagle 算法（减少延迟）
    
    // 4. 执行请求
    CURLcode res = curl_easy_perform(curl);
    
    // 5. 检查结果
    bool success = false;
    
    if (res == CURLE_OK) {
        // 5.1 请求成功，检查 HTTP 状态码
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (httpCode == 200) {
            // 5.2 HTTP 200 OK，解析 JSON 响应
            try {
                response = nlohmann::json::parse(readBuffer);
                success = true;
            } catch (const nlohmann::json::exception& e) {
                // JSON 解析失败
                lastError_ = "JSON parse error: " + std::string(e.what());
                
                // 截断过长的响应体（避免日志过大）
                std::string preview = readBuffer.substr(0, 
                    std::min<size_t>(readBuffer.size(), 200));
                if (readBuffer.size() > 200) {
                    preview += "... (truncated)";
                }
                lastError_ += " | Response: " + preview;
            }
        } else {
            // 5.3 HTTP 错误（非 200）
            lastError_ = "HTTP error: " + std::to_string(httpCode);
            
            // 尝试提取错误信息（如果响应体是文本）
            if (!readBuffer.empty()) {
                std::string preview = readBuffer.substr(0, 
                    std::min<size_t>(readBuffer.size(), 200));
                if (readBuffer.size() > 200) {
                    preview += "... (truncated)";
                }
                lastError_ += " | Response: " + preview;
            }
        }
    } else {
        // 5.4 CURL 错误（网络连接失败、超时等）
        lastError_ = "CURL error: " + std::string(curl_easy_strerror(res));
        
        // 添加更详细的错误信息
        if (res == CURLE_OPERATION_TIMEDOUT) {
            lastError_ += " (timeout=" + std::to_string(timeoutMs) + "ms)";
        } else if (res == CURLE_COULDNT_CONNECT) {
            lastError_ += " (could not connect to " + url + ")";
        }
    }
    
    // 6. 清理资源
    curl_slist_free_all(headers);  // 释放 HTTP 头链表
    curl_easy_cleanup(curl);       // 释放 CURL 句柄
    
    return success;
}

std::string HttpClient::getLastError() {
    return lastError_;
}

}  // namespace utils
}  // namespace esdk_sophon
