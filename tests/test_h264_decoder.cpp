/**
 * @file test_h264_decoder.cpp
 * @brief H.264 解码器单元测试
 * 
 * 测试内容:
 * 1. 初始化和反初始化
 * 2. 基本解码功能
 * 3. 资源管理
 * 
 * 设计哲学:
 * - **无外部依赖**: 不依赖 GTest/GMock (手动 assert)
 * - **快速验证**: 编译和运行时间短
 * - **CI/CD 友好**: 返回值 0 表示成功, 非 0 表示失败
 * 
 * @author 学习者
 * @date 2025-11-18
 */

#include "esdk_sophon/video/H264Decoder.h"
#include "esdk_sophon/core/Logger.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <cassert>

using namespace esdk_sophon::video;
using namespace esdk_sophon::core;

// ==================== 测试辅助函数 ====================

/**
 * @brief 打印测试结果
 */
void printTestResult(const std::string& testName, bool passed) {
    if (passed) {
        std::cout << "✅ [PASS] " << testName << std::endl;
    } else {
        std::cout << "❌ [FAIL] " << testName << std::endl;
        exit(1);  // 任何测试失败就退出
    }
}

/**
 * @brief 打印测试标题
 */
void printTestHeader(const std::string& testName) {
    std::cout << "\n========== " << testName << " ==========" << std::endl;
}

// ==================== 测试用例 ====================

/**
 * @brief 测试1: 初始化和反初始化
 */
bool test_InitAndDeinit() {
    printTestHeader("测试1: 初始化和反初始化");
    
    H264Decoder decoder;
    
    // 初始化
    std::cout << "  执行: 初始化解码器..." << std::endl;
    if (!decoder.init(1920, 1080)) {
        std::cerr << "  ❌ 初始化失败!" << std::endl;
        return false;
    }
    std::cout << "  ✓ 初始化成功" << std::endl;
    
    // 反初始化
    std::cout << "  执行: 反初始化..." << std::endl;
    decoder.deinit();
    std::cout << "  ✓ 反初始化成功" << std::endl;
    
    return true;
}

/**
 * @brief 测试2: 重复初始化
 */
bool test_DoubleInit() {
    printTestHeader("测试2: 重复初始化");
    
    H264Decoder decoder;
    
    // 第一次初始化
    std::cout << "  执行: 第一次初始化..." << std::endl;
    if (!decoder.init(1920, 1080)) {
        std::cerr << "  ❌ 第一次初始化失败!" << std::endl;
        return false;
    }
    std::cout << "  ✓ 第一次初始化成功" << std::endl;
    
    // 第二次初始化（应该跳过）
    std::cout << "  执行: 第二次初始化（应该跳过）..." << std::endl;
    if (!decoder.init(1920, 1080)) {
        std::cerr << "  ❌ 第二次初始化返回 false!" << std::endl;
        return false;
    }
    std::cout << "  ✓ 第二次初始化返回 true (跳过)" << std::endl;
    
    decoder.deinit();
    return true;
}

/**
 * @brief 测试3: 解码前未初始化
 */
bool test_DecodeWithoutInit() {
    printTestHeader("测试3: 解码前未初始化");
    
    H264Decoder decoder;
    
    uint8_t dummyData[] = {0x00, 0x00, 0x00, 0x01, 0x67};  // 假 SPS
    
    bool callbackCalled = false;
    auto callback = [&callbackCalled](const cv::Mat& /* frame */) {
        callbackCalled = true;  // 参数未使用，用注释标记
    };
    
    // 未初始化就解码（应该失败）
    std::cout << "  执行: 未初始化解码..." << std::endl;
    bool result = decoder.decode(dummyData, sizeof(dummyData), callback);
    
    if (result) {
        std::cerr << "  ❌ 未初始化解码应该返回 false!" << std::endl;
        return false;
    }
    
    if (callbackCalled) {
        std::cerr << "  ❌ 未初始化解码不应该调用回调!" << std::endl;
        return false;
    }
    
    std::cout << "  ✓ 未初始化解码正确返回 false" << std::endl;
    std::cout << "  ✓ 未调用回调" << std::endl;
    
    return true;
}

/**
 * @brief 测试4: 空数据处理
 */
bool test_DecodeEmptyData() {
    printTestHeader("测试4: 空数据处理");
    
    H264Decoder decoder;
    if (!decoder.init(1920, 1080)) {
        std::cerr << "  ❌ 初始化失败!" << std::endl;
        return false;
    }
    
    bool callbackCalled = false;
    auto callback = [&callbackCalled](const cv::Mat& /* frame */) {
        callbackCalled = true;  // 参数未使用，用注释标记
    };
    
    // 测试 nullptr 数据
    std::cout << "  执行: nullptr 数据..." << std::endl;
    if (decoder.decode(nullptr, 100, callback)) {
        std::cerr << "  ❌ nullptr 数据应该返回 false!" << std::endl;
        return false;
    }
    std::cout << "  ✓ nullptr 数据正确返回 false" << std::endl;
    
    // 测试长度为 0
    std::cout << "  执行: 长度为 0..." << std::endl;
    uint8_t data[] = {0x00};
    if (decoder.decode(data, 0, callback)) {
        std::cerr << "  ❌ 长度为 0 应该返回 false!" << std::endl;
        return false;
    }
    std::cout << "  ✓ 长度为 0 正确返回 false" << std::endl;
    
    if (callbackCalled) {
        std::cerr << "  ❌ 空数据不应该调用回调!" << std::endl;
        return false;
    }
    std::cout << "  ✓ 未调用回调" << std::endl;
    
    decoder.deinit();
    return true;
}

/**
 * @brief 测试5: 基本解码流程
 * 
 * 注意: 这个测试需要真实的 H.264 数据才能验证完整功能
 * 目前只测试流程是否正确
 */
bool test_BasicDecode() {
    printTestHeader("测试5: 基本解码流程");
    
    H264Decoder decoder;
    if (!decoder.init(1920, 1080)) {
        std::cerr << "  ❌ 初始化失败!" << std::endl;
        return false;
    }
    std::cout << "  ✓ 初始化成功" << std::endl;
    
    // 📌 构造一个简单的 H.264 数据包
    // 实际项目中会从 DJI SDK 获取真实数据
    std::vector<uint8_t> h264Data = {
        // NAL 起始码
        0x00, 0x00, 0x00, 0x01,
        // NAL 类型 7 (SPS)
        0x67, 0x42, 0x00, 0x1F, 0xAB
        // ... (完整的 SPS 数据)
    };
    
    std::cout << "  注意: 数据不完整,可能不会触发回调" << std::endl;
    std::cout << "  执行: 解码..." << std::endl;
    
    bool callbackCalled = false;
    auto callback = [&callbackCalled](const cv::Mat& frame) {
        callbackCalled = true;
        
        std::cout << "  ✓ 解码回调成功!" << std::endl;
        std::cout << "    图像尺寸: " << frame.cols << "x" << frame.rows << std::endl;
        std::cout << "    图像类型: " << frame.type() << " (期望: " << CV_8UC3 << ")" << std::endl;
        
        if (frame.empty()) {
            std::cerr << "  ❌ 解码出的图像为空!" << std::endl;
        }
        
        if (frame.type() != CV_8UC3) {
            std::cerr << "  ⚠️  图像类型不是 CV_8UC3!" << std::endl;
        }
    };
    
    // 解码
    // 注意: 由于数据不完整,可能不会触发回调
    decoder.decode(h264Data.data(), h264Data.size(), callback);
    
    if (callbackCalled) {
        std::cout << "  ✅ 成功解码并调用回调" << std::endl;
    } else {
        std::cout << "  ⚠️  数据不完整,未触发回调 (正常)" << std::endl;
    }
    
    decoder.deinit();
    std::cout << "  ✓ 反初始化成功" << std::endl;
    
    return true;
}

/**
 * @brief 测试6: 析构函数自动清理
 */
bool test_AutoCleanup() {
    printTestHeader("测试6: 析构函数自动清理");
    
    std::cout << "  执行: 创建解码器并初始化..." << std::endl;
    {
        H264Decoder decoder;
        if (!decoder.init(1920, 1080)) {
            std::cerr << "  ❌ 初始化失败!" << std::endl;
            return false;
        }
        std::cout << "  ✓ 初始化成功" << std::endl;
        
        // decoder 在这里离开作用域,应该自动调用 deinit()
        std::cout << "  执行: 离开作用域,触发析构..." << std::endl;
    }
    
    std::cout << "  ✓ 析构函数成功执行 (无崩溃)" << std::endl;
    
    return true;
}

// ==================== 主函数 ====================

/**
 * @brief 主函数
 */
int main(int /* argc */, char** /* argv */) {  // 参数未使用，用注释标记
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  H.264 解码器单元测试\n";
    std::cout << "========================================\n";
    
    // 注意: Logger 是单例，会自动初始化
    // 不需要手动调用 init()
    
    int passedTests = 0;
    int totalTests = 6;
    
    // 运行测试
    if (test_InitAndDeinit()) passedTests++;
    printTestResult("测试1: 初始化和反初始化", test_InitAndDeinit());
    
    if (test_DoubleInit()) passedTests++;
    printTestResult("测试2: 重复初始化", test_DoubleInit());
    
    if (test_DecodeWithoutInit()) passedTests++;
    printTestResult("测试3: 解码前未初始化", test_DecodeWithoutInit());
    
    if (test_DecodeEmptyData()) passedTests++;
    printTestResult("测试4: 空数据处理", test_DecodeEmptyData());
    
    if (test_BasicDecode()) passedTests++;
    printTestResult("测试5: 基本解码流程", test_BasicDecode());
    
    if (test_AutoCleanup()) passedTests++;
    printTestResult("测试6: 析构函数自动清理", test_AutoCleanup());
    
    // 输出结果
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  测试完成: " << passedTests << "/" << totalTests << " 通过\n";
    std::cout << "========================================\n";
    std::cout << "\n";
    
    if (passedTests == totalTests) {
        std::cout << "✅ 所有测试通过!\n" << std::endl;
        return 0;
    } else {
        std::cout << "❌ 部分测试失败!\n" << std::endl;
        return 1;
    }
}
