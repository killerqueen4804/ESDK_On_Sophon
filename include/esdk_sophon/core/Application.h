/**
 * @file Application.h
 * @brief 应用程序主类
 * @author ESDK_Sophon Team
 * @date 2025-10-25
 * 
 * @details
 * 应用程序主类，负责初始化和运行整个系统。
 */

#ifndef ESDK_SOPHON_CORE_APPLICATION_H_
#define ESDK_SOPHON_CORE_APPLICATION_H_

#include <memory>

namespace esdk_sophon {
namespace core {

/**
 * @brief 应用程序主类
 */
class Application {
public:
    Application();
    ~Application();

    /**
     * @brief 初始化应用程序
     * @return true 成功，false 失败
     */
    bool initialize();

    /**
     * @brief 运行应用程序主循环
     * @return 退出码
     */
    int run();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace core
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_CORE_APPLICATION_H_
