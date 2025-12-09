/**
 * @file main.cpp
 * @brief ESDK Sophon主程序入口 - 使用Application协调器
 * @author ESDK_Sophon Team
 * @date 2025-10-30
 * @version 2.0.0 - 重构版本
 * 
 * @details
 * 本程序是ESDK Sophon视觉处理系统的主入口。
 * 使用Application类作为顶层协调器，统一管理所有模块的生命周期。
 * 
 * 系统架构：
 *   Application (顶层协调器)
 *   ├── Logger      (日志系统)
 *   ├── Config      (配置管理)
 *   ├── MqttClient  (MQTT客户端)
 *   ├── MqttHandler (MQTT消息处理)
 *   └── TaskManager (任务管理)
 * 
 * 程序流程：
 *   1. 初始化Application (initialize)
 *   2. 运行主事件循环 (run) - 阻塞直到收到SIGINT/SIGTERM信号
 *   3. 优雅关闭 (shutdown) - 按逆序释放资源
 */

#include "esdk_sophon/Application.h"
#include <iostream>

/**
 * @brief 打印欢迎界面
 */
void printWelcome() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║          ESDK Sophon 视觉处理系统 v2.0                   ║
║          DJI 边缘AI计算平台（算能SE7）                   ║
║                                                           ║
║  功能特性：                                              ║
║  • YOLOv10 实时目标检测                                 ║
║  • MQTT 云端通信                                        ║
║  • 多任务并行处理                                       ║
║  • 智能算法调度                                         ║
║  • 自动文件归档                                         ║
║                                                           ║
║  按 Ctrl+C 优雅退出                                     ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
)" << std::endl;
}

/**
 * @brief 主函数
 * 
 * @return int 返回码
 *   - 0: 正常退出
 *   - 1: 初始化失败
 *   - 2: 运行时异常
 * 
 * @details
 * 程序执行流程：
 * 1. 打印欢迎信息
 * 2. 获取Application单例
 * 3. 初始化所有模块（Logger、Config、TaskManager、MqttClient、MqttHandler）
 * 4. 注册信号处理器（SIGINT、SIGTERM）
 * 5. 进入主事件循环（阻塞）
 * 6. 收到退出信号后优雅关闭所有模块
 * 7. 返回退出码
 * 
 * @note 本程序遵循RAII原则，所有资源都会被正确释放
 */
int main() {
    // 1. 打印欢迎界面
    printWelcome();
    
    try {
        // 2. 获取Application单例
        auto& app = esdk_sophon::Application::getInstance();
        
        // 3. 初始化所有模块
        std::cout << "正在初始化系统..." << std::endl;
        if (!app.initialize()) {
            std::cerr << "\n❌ 系统初始化失败，请查看日志文件获取详细信息" << std::endl;
            return 1;
        }
        
        // 4. 初始化成功，开始运行
        std::cout << "\n✅ 系统初始化成功！" << std::endl;
        std::cout << "系统正在运行中... (按 Ctrl+C 退出)\n" << std::endl;
        
        // 5. 运行主事件循环
        // run() 会阻塞在这里，直到收到SIGINT或SIGTERM信号
        app.run();
        
        // 6. 收到退出信号，执行优雅关闭
        std::cout << "\n正在关闭系统..." << std::endl;
        app.shutdown();
        
        // 7. 正常退出
        std::cout << "\n✅ 系统已安全退出，再见！" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        // 捕获异常，打印错误信息
        std::cerr << "\n❌ 程序异常: " << e.what() << std::endl;
        std::cerr << "请查看日志文件获取详细错误信息" << std::endl;
        return 2;
    } catch (...) {
        // 捕获未知异常
        std::cerr << "\n❌ 未知异常发生" << std::endl;
        std::cerr << "请查看日志文件获取详细错误信息" << std::endl;
        return 2;
    }
}

// ============================================================================
// 📚 编译与运行指南
// ============================================================================
// 
// 【编译方法】
// 在Docker容器 stream_lzy 中执行：
//   cd /workspace/build
//   cmake ..
//   make -j8
// 
// 编译产物：
//   ./bin/esdk_sophon  (可执行文件)
//   ./lib/*.a          (静态库文件)
// 
// 【运行方法】
// 方式1 - 直接运行（前台）：
//   cd /workspace/build
//   ./bin/esdk_sophon
// 
// 方式2 - 后台运行：
//   cd /workspace/build
//   nohup ./bin/esdk_sophon > app.log 2>&1 &
// 
// 方式3 - 查看实时日志：
//   tail -f esdk_sophon.log
// 
// 【停止程序】
// 前台运行：按 Ctrl+C
// 后台运行：kill -SIGINT <pid>  或  kill -SIGTERM <pid>
// 
// 【日志文件】
// - esdk_sophon.log: 主日志文件
// - esdk_sophon_*.log.gz: 归档的旧日志（自动轮转）
// 
// ============================================================================
// 📝 设计模式与最佳实践
// ============================================================================
// 
// 【1. Facade 模式 (外观模式)】
// Application类作为外观，隐藏了复杂的模块初始化细节。
// main()函数只需调用3个方法：initialize(), run(), shutdown()
// 
// 优点：
// - 简化客户端代码（main函数非常简洁）
// - 降低耦合度（main不需要知道内部细节）
// - 易于维护（修改初始化逻辑不影响main）
// 
// 【2. Singleton 模式 (单例模式)】
// 所有核心模块都使用Meyers' Singleton：
// - Logger, Config, MqttClient, MqttHandler, TaskManager, Application
// 
// 优点：
// - 确保全局唯一性
// - 线程安全（C++11保证）
// - 延迟初始化（第一次使用时创建）
// 
// 【3. RAII 原则 (资源获取即初始化)】
// 所有资源在构造函数中获取，在析构函数中释放。
// 即使发生异常，C++也会自动调用析构函数清理资源。
// 
// 示例：
//   {
//       auto& logger = Logger::getInstance();  // 获取资源
//       logger.info("hello");                   // 使用资源
//   } // 离开作用域，自动释放资源（如果需要）
// 
// 【4. 信号处理与优雅退出】
// 注册了SIGINT和SIGTERM信号处理器：
// - 用户按Ctrl+C时，程序不会立即终止
// - 而是设置running_标志为false
// - 主循环检测到标志变化后退出
// - 依次关闭所有模块，确保数据完整性
// 
// 【5. 错误处理策略】
// - 初始化失败：返回false，不启动系统
// - 运行时异常：try-catch捕获，记录日志后退出
// - 资源释放：即使失败也继续关闭其他模块
// 
// ============================================================================
// 🎯 面试要点总结
// ============================================================================
// 
// Q1: 为什么使用Application类而不是在main中直接初始化各模块？
// A: 这是Facade模式的应用。将复杂的初始化逻辑封装在Application中，
//    使main函数保持简洁，降低耦合。同时方便单元测试和模块替换。
// 
// Q2: 如何保证信号处理的安全性？
// A: 信号处理器中只设置atomic<bool>标志，不进行复杂操作。
//    实际的关闭逻辑在主线程的shutdown()中执行，避免信号处理的竞态条件。
// 
// Q3: 如果某个模块初始化失败会怎样？
// A: Application::initialize()会立即返回false，main()捕获后返回1退出。
//    已经初始化的模块会在Application析构时自动清理（RAII）。
// 
// Q4: 程序运行在主循环中做什么？
// A: 目前只是sleep等待信号。后续可以在这里添加：
//    - 健康检查（检查各模块状态）
//    - 性能监控（统计FPS、内存等）
//    - 状态上报（定期向云端发送心跳）
// 
// Q5: 如何实现程序的守护进程化？
// A: 可以使用systemd或supervisor管理：
//    [Unit]
//    Description=ESDK Sophon Service
//    [Service]
//    Type=simple
//    ExecStart=/path/to/esdk_sophon
//    Restart=always
//    [Install]
//    WantedBy=multi-user.target
// 
// ============================================================================