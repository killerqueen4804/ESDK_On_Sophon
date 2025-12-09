/**
 * @file test_utils.cpp
 * @brief 工具类模块测试程序
 * 
 * @details
 * 验证最近修复和开发的工具类功能：
 * 1. GeoUtils: Vincenty 距离计算公式、坐标转换
 * 2. ImageProcessor: Base64 编解码、图片尺寸获取 (验证 OpenCV 兼容性修复)
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-20
 */

#include "esdk_sophon/utils/GeoUtils.h"
#include "esdk_sophon/utils/ImageProcessor.h"
#include "esdk_sophon/core/Logger.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <vector>

using namespace esdk_sophon::utils;
using namespace esdk_sophon::core;

void test_geo_utils() {
    std::cout << "\n========== 测试 GeoUtils ==========\n";
    
    // ✅ 正确用法：使用 getInstance() 获取单例引用
    auto& geo = GeoUtils::getInstance();

    // Test 1: Distance Calculation (Vincenty vs Haversine)
    // 北京 (39.9042, 116.4074) -> 上海 (31.2304, 121.4737)
    GpsCoordinate p1{39.9042, 116.4074, 0.0}; 
    GpsCoordinate p2{31.2304, 121.4737, 0.0}; 

    double distVincenty = geo.calculateDistanceVincenty(p1, p2);
    double distHaversine = geo.calculateDistance(p1, p2);

    std::cout << "北京 -> 上海 (Vincenty): " << distVincenty << " meters\n";
    std::cout << "北京 -> 上海 (Haversine): " << distHaversine << " meters\n";

    // 验证两者差异不大 (Vincenty 更精确，但 Haversine 也不应差太多，误差 < 0.5%)
    double diffRatio = std::abs(distVincenty - distHaversine) / distVincenty;
    assert(diffRatio < 0.005);
    std::cout << "✓ 距离计算验证通过 (误差: " << diffRatio * 100 << "%)\n";
    
    // Test 2: Coordinate Conversion (WGS84 <-> GCJ02)
    // 在中国境内，转换后坐标应有变化
    GpsCoordinate p1_gcj = geo.wgs84ToGcj02(p1);
    assert(std::abs(p1.latitude - p1_gcj.latitude) > 0.0001);
    assert(std::abs(p1.longitude - p1_gcj.longitude) > 0.0001);
    std::cout << "✓ 坐标转换验证通过 (WGS84 -> GCJ02)\n";
}

void test_image_processor() {
    std::cout << "\n========== 测试 ImageProcessor ==========\n";
    
    // ✅ 正确用法：使用 getInstance() 获取单例引用
    auto& imgProc = ImageProcessor::getInstance();

    // Test 1: Base64 Encode/Decode (使用 cv::Mat 接口)
    // 创建一个简单的测试图片
    cv::Mat originalImg(100, 200, CV_8UC3, cv::Scalar(255, 0, 0)); // 蓝色图片
    
    std::string encoded = imgProc.encodeBase64(originalImg);
    auto decodedOpt = imgProc.decodeBase64(encoded);
    
    std::cout << "Original Image Size: " << originalImg.size() << "\n";
    std::cout << "Encoded Base64 Length: " << encoded.length() << " chars\n";
    
    assert(decodedOpt.has_value());
    cv::Mat decoded = decodedOpt.value();
    std::cout << "Decoded Image Size: " << decoded.size() << "\n";
    
    // 验证尺寸一致
    assert(originalImg.rows == decoded.rows);
    assert(originalImg.cols == decoded.cols);
    std::cout << "✓ Base64 编解码验证通过 (尺寸一致)\n";

    // Test 2: Get Image Size (验证 OpenCV 兼容性修复)
    // 创建一个临时图片文件
    std::string tempPath = "test_image_utils.jpg";
    int expectedW = 200;
    int expectedH = 100;
    cv::Mat testImg(expectedH, expectedW, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::imwrite(tempPath, testImg);

    int w = 0, h = 0;
    bool success = imgProc.getImageSize(tempPath, w, h);
    
    std::cout << "Image Size from File: " << w << "x" << h << "\n";
    
    // 清理临时文件
    if (std::filesystem::exists(tempPath)) {
        std::filesystem::remove(tempPath);
    }

    assert(success == true);
    assert(w == expectedW);
    assert(h == expectedH);
    std::cout << "✓ getImageSize 验证通过 (OpenCV IMREAD_GRAYSCALE 读取正常)\n";
}

int main() {
    try {
        // 初始化日志（避免 GeoUtils 内部调用报错）
        Logger::getInstance().info("开始工具类测试");

        test_geo_utils();
        test_image_processor();
        
        std::cout << "\n========================================\n";
        std::cout << "✅ 所有工具类测试通过!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "❌ 测试失败: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    }
}
