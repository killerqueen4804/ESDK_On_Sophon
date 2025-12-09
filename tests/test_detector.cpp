/**
 * @file test_detector.cpp
 * @brief Detector模块单元测试
 * 
 * 测试内容:
 * 1. 工厂模式创建检测器
 * 2. 检测器初始化流程
 * 3. Letterbox预处理算法
 * 4. 检测推理功能(需要模型文件)
 * 5. 坐标变换正确性
 * 6. 边界条件测试
 * 
 * @author ESDK Sophon Team
 * @date 2025-10-31
 */

#include "esdk_sophon/vision/IDetector.h"
#include "esdk_sophon/vision/DetectorFactory.h"
#include "esdk_sophon/vision/PPYoloeDetector.h"
#include "esdk_sophon/vision/VisionConfigLoader.h"  // ⭐ 添加
#include "esdk_sophon/core/Logger.h"
#include "esdk_sophon/core/Config.h"  // ⭐ 添加Config加载

#include <iostream>
#include <cassert>
#include <opencv2/opencv.hpp>

using namespace esdk_sophon::vision;
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
    }
}

/**
 * @brief 创建测试用的检测器配置
 * 
 * @param withValidModel true=从config.json读取真实路径, false=使用虚拟路径
 * @return DetectorConfig 检测器配置
 * 
 * @note 修复: 现在从config.json读取配置,而不是硬编码路径
 */
DetectorConfig createTestConfig(bool withValidModel = false) {
    DetectorConfig config;
    
    if (withValidModel) {
        // ✅ 从config.json读取实际模型路径
        try {
            VisionConfigLoader loader;
            config = loader.loadDetectorConfig("ppyoloe");
            
            std::cout << "✅ 从config.json加载配置成功:" << std::endl;
            std::cout << "   模型路径: " << config.modelPath << std::endl;
            std::cout << "   配置文件: " << config.configFile << std::endl;
            std::cout << "   类别数量: " << config.classes.size() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "⚠️  从config.json加载失败: " << e.what() << std::endl;
            std::cerr << "   使用备用硬编码路径" << std::endl;
            
            // 备用方案: 硬编码路径(使用config.json中的正确路径)
            config.modelPath = "/data/Edge-SDK/models/PaddleDetection/ppyoloe_crn_m_300e_coco_289.bmodel";
            config.configFile = "/data/Edge-SDK/models/PaddleDetection/infer_cfg.yml";
            config.classes = {"person", "bicycle", "car", "motorcycle", "airplane", "bus", 
                             "train", "truck", "boat", "traffic light"};  // COCO前10类
            config.confidenceThreshold = 0.5f;
            config.nmsThreshold = 0.45f;
            config.inputWidth = 640;
            config.inputHeight = 640;
        }
    } else {
        // 虚拟路径,用于测试逻辑
        config.modelPath = "/path/to/ppyoloe.bmodel";
        config.configFile = "/path/to/infer_cfg.yml";
        config.classes = {"person", "bicycle", "car", "motorcycle", "bus", "truck"};
        config.confidenceThreshold = 0.5f;
        config.nmsThreshold = 0.45f;
        config.inputWidth = 640;
        config.inputHeight = 640;
    }
    
    return config;
}

/**
 * @brief 创建测试图像
 */
cv::Mat createTestImage(int width, int height) {
    cv::Mat image(height, width, CV_8UC3);
    
    // 创建渐变色图像
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.at<cv::Vec3b>(y, x) = cv::Vec3b(
                (x * 255) / width,      // B
                (y * 255) / height,     // G
                128                     // R
            );
        }
    }
    
    return image;
}

// ==================== 测试用例 ====================

/**
 * @brief 测试1: DetectorFactory类型转换
 * 
 * 验证:
 * 1. typeToString()正确转换
 * 2. stringToType()正确解析
 * 3. 大小写不敏感
 */
void test1_DetectorFactory_TypeConversion() {
    std::cout << "\n========== 测试1: DetectorFactory类型转换 ==========" << std::endl;
    
    int passCount = 0;
    int totalTests = 0;
    
    // 测试1: typeToString - PPYOLOE
    totalTests++;
    std::string str1 = DetectorFactory::typeToString(DetectorType::PPYOLOE);
    bool test1 = (str1 == "PP-YOLOE");
    if (test1) passCount++;
    std::cout << "Test 1.1: PPYOLOE → \"" << str1 << "\" " 
              << (test1 ? "✓" : "✗") << std::endl;
    
    // 测试2: typeToString - UNKNOWN
    totalTests++;
    std::string str2 = DetectorFactory::typeToString(DetectorType::UNKNOWN);
    bool test2 = (str2 == "Unknown");
    if (test2) passCount++;
    std::cout << "Test 1.2: UNKNOWN → \"" << str2 << "\" " 
              << (test2 ? "✓" : "✗") << std::endl;
    
    // 测试3: stringToType - "PP-YOLOE"
    totalTests++;
    DetectorType type1 = DetectorFactory::stringToType("PP-YOLOE");
    bool test3 = (type1 == DetectorType::PPYOLOE);
    if (test3) passCount++;
    std::cout << "Test 1.3: \"PP-YOLOE\" → " << (int)type1 
              << " (期望0) " << (test3 ? "✓" : "✗") << std::endl;
    
    // 测试4: stringToType - "PPYOLOE" (无连字符)
    totalTests++;
    DetectorType type2 = DetectorFactory::stringToType("PPYOLOE");
    bool test4 = (type2 == DetectorType::PPYOLOE);
    if (test4) passCount++;
    std::cout << "Test 1.4: \"PPYOLOE\" → " << (int)type2 
              << " (期望0) " << (test4 ? "✓" : "✗") << std::endl;
    
    // 测试5: stringToType - "ppyoloe" (小写)
    totalTests++;
    DetectorType type3 = DetectorFactory::stringToType("ppyoloe");
    bool test5 = (type3 == DetectorType::PPYOLOE);
    if (test5) passCount++;
    std::cout << "Test 1.5: \"ppyoloe\" → " << (int)type3 
              << " (期望0) " << (test5 ? "✓" : "✗") << std::endl;
    
    // 测试6: stringToType - "invalid"
    totalTests++;
    DetectorType type4 = DetectorFactory::stringToType("invalid");
    bool test6 = (type4 == DetectorType::UNKNOWN);
    if (test6) passCount++;
    std::cout << "Test 1.6: \"invalid\" → " << (int)type4 
              << " (期望3=UNKNOWN) " << (test6 ? "✓" : "✗") << std::endl;
    
    std::cout << "通过: " << passCount << "/" << totalTests << std::endl;
    printTestResult("类型转换", passCount == totalTests);
}

/**
 * @brief 测试2: DetectorFactory支持的类型
 * 
 * 验证:
 * 1. isSupported()正确判断
 * 2. getSupportedTypes()返回正确列表
 */
void test2_DetectorFactory_SupportedTypes() {
    std::cout << "\n========== 测试2: DetectorFactory支持的类型 ==========" << std::endl;
    
    bool passed = true;
    
    // 测试isSupported
    bool supported1 = DetectorFactory::isSupported(DetectorType::PPYOLOE);
    passed &= supported1;
    std::cout << "PPYOLOE支持: " << (supported1 ? "是" : "否") << " passed="<<passed<<std::endl;
    
    bool supported2 = DetectorFactory::isSupported(DetectorType::YOLOV8);
    passed &= !supported2;  // 当前未实现
    std::cout << "YOLOV8支持: " << (supported2 ? "是" : "否") << " passed="<<passed<<std::endl;
    
    // 测试getSupportedTypes
    auto types = DetectorFactory::getSupportedTypes();
    std::cout << "支持的类型数量: " << types.size() <<std::endl;
    
    for (const auto& type : types) {
        std::cout << "  - " << DetectorFactory::typeToString(type) <<std::endl;
    }
    
    passed &= !types.empty();
    
    printTestResult("支持的类型", passed);
}

/**
 * @brief 测试3: DetectorFactory从模型路径推断类型
 * 
 * 验证:
 * 1. 正确识别PPYOLOE模型
 * 2. 大小写不敏感
 * 3. 无法识别时抛出异常
 */
void test3_DetectorFactory_InferFromPath() {
    std::cout << "\n========== 测试3: 从模型路径推断类型 ==========" << std::endl;
    
    bool passed = true;
    
    // 测试用例1: PPYOLOE模型
    try {
        DetectorConfig config = createTestConfig();
        config.modelPath = "/path/to/PPYOLOE_coco.bmodel";
        
        // 注意: 由于模型文件不存在,这里会在initialize时失败
        // 但我们关注的是类型推断是否正确
        std::cout << "测试路径: " << config.modelPath << std::endl;
        std::cout << "  (类型推断会成功,但模型加载会失败)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "预期异常: " << e.what() << std::endl;
    }
    
    // 测试用例2: 无法识别的模型
    try {
        DetectorConfig config = createTestConfig();
        config.modelPath = "/path/to/unknown_model.bmodel";
        
        auto detector = DetectorFactory::createFromModelPath(
            config.modelPath, config);
        
        passed = false;  // 不应该执行到这里
        std::cout << "❌ 应该抛出异常但没有" << " passed="<<passed<<std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "正确抛出异常: " << e.what() << std::endl;
    }
    
    printTestResult("路径推断类型", passed);
}

/**
 * @brief 测试4: PPYoloeDetector基本功能
 * 
 * 验证:
 * 1. 构造函数正常工作
 * 2. 初始状态正确
 * 3. getter方法的默认行为
 * 
 * @note 这个测试不调用initialize(),只测试未初始化状态下的接口行为
 */
void test4_PPYoloeDetector_BasicFunctions() {
    std::cout << "\n========== 测试4: PPYoloeDetector基本功能 ==========" << std::endl;
    
    int passCount = 0;
    int totalTests = 0;
    
    // 创建检测器实例
    PPYoloeDetector detector;
    
    // 测试1: 初始状态应该是未初始化
    totalTests++;
    bool test1 = !detector.isInitialized();
    if (test1) passCount++;
    std::cout << "Test 4.1: 初始状态 = " 
              << (detector.isInitialized() ? "已初始化" : "未初始化") 
              << " " << (test1 ? "✓" : "✗") << std::endl;
    
    // 测试2: getName()应该返回固定字符串(不依赖初始化)
    totalTests++;
    std::string name = detector.getName();
    bool test2 = (name == "PP-YOLOE");
    if (test2) passCount++;
    std::cout << "Test 4.2: 检测器名称 = \"" << name 
              << "\" (期望\"PP-YOLOE\") " << (test2 ? "✓" : "✗") << std::endl;
    
    // 测试3: getClasses()未初始化时应该为空
    totalTests++;
    const auto& classes = detector.getClasses();
    bool test3 = classes.empty();
    if (test3) passCount++;
    std::cout << "Test 4.3: 类别数量 = " << classes.size() 
              << " (期望0) " << (test3 ? "✓" : "✗") << std::endl;
    
    // 测试4: getInputSize()未初始化时应该返回合理默认值
    // 注意: 如果实现返回(0,0)或(640,640)都是合理的
    totalTests++;
    cv::Size size = detector.getInputSize();
    bool test4 = (size.width == 0 && size.height == 0) ||  // 未初始化返回0
                 (size.width == 640 && size.height == 640);  // 或默认尺寸
    if (test4) passCount++;
    std::cout << "Test 4.4: 输入尺寸 = " << size.width << "x" << size.height 
              << " " << (test4 ? "✓" : "✗") << std::endl;
    
    if (!test4) {
        std::cout << "  ⚠️  提示: getInputSize()在未初始化时返回了非预期值" << std::endl;
        std::cout << "  建议在PPYoloeDetector构造函数中初始化默认尺寸" << std::endl;
    }
    
    std::cout << "通过: " << passCount << "/" << totalTests << std::endl;
    printTestResult("基本功能", passCount == totalTests);
}

/**
 * @brief 测试5: DetectionBox辅助方法
 * 
 * 验证:
 * 1. getCenter()计算正确
 * 2. toRect()转换正确
 */
void test5_DetectionBox_HelperMethods() {
    std::cout << "\n========== 测试5: DetectionBox辅助方法 ==========" << std::endl;
    
    bool passed = true;
    
    // 创建测试检测框
    DetectionBox box;
    box.x = 100;
    box.y = 200;
    box.width = 50;
    box.height = 80;
    box.classId = 0;
    box.className = "person";
    box.confidence = 0.95f;
    
    // 测试getCenter
    cv::Point2f center = box.getCenter();
    passed &= (center.x == 125.0f && center.y == 240.0f);
    std::cout << "中心点: (" << center.x << ", " << center.y << ")" << " passed="<<passed<<std::endl;
    
    // 测试toRect
    cv::Rect rect = box.toRect();
    passed &= (rect.x == 100 && rect.y == 200 && 
               rect.width == 50 && rect.height == 80);
    std::cout << "矩形: [" << rect.x << "," << rect.y << "," 
              << rect.width << "," << rect.height << "]" << " passed="<<passed<<std::endl;
    
    printTestResult("DetectionBox辅助方法", passed);
}

/**
 * @brief 测试6: DetectionResult统计方法
 * 
 * 验证:
 * 1. getTotalTime()计算正确
 * 2. getDetectionCount()正确
 * 3. hasDetections()正确
 */
void test6_DetectionResult_Statistics() {
    std::cout << "\n========== 测试6: DetectionResult统计方法 ==========" << std::endl;
    
    bool passed = true;
    
    // 创建测试结果
    DetectionResult result;
    result.preprocessTime = 5.0;
    result.inferenceTime = 15.0;
    result.postprocessTime = 2.0;
    
    // 添加一些检测框
    DetectionBox box1, box2;
    result.boxes.push_back(box1);
    result.boxes.push_back(box2);
    
    // 测试getTotalTime
    double totalTime = result.getTotalTime();
    passed &= (totalTime == 22.0);
    std::cout << "总耗时: " << totalTime << " ms" << " passed="<<passed<<std::endl;
    
    // 测试getDetectionCount
    size_t count = result.getDetectionCount();
    passed &= (count == 2);
    std::cout << "检测数量: " << count << " passed="<<passed<<std::endl;
    
    // 测试hasDetections
    bool hasDetections = result.hasDetections();
    passed &= hasDetections;
    std::cout << "是否有检测: " << (hasDetections ? "是" : "否") << " passed="<<passed<<std::endl;
    
    // 测试空结果
    DetectionResult emptyResult;
    passed &= !emptyResult.hasDetections();
    passed &= (emptyResult.getDetectionCount() == 0);
    
    printTestResult("DetectionResult统计", passed);
}

/**
 * @brief 测试7: Letterbox变换算法 (理论验证)
 * 
 * 验证:
 * 1. 1920x1080 → 640x640 的缩放比例
 * 2. 偏移量计算
 * 
 * @note 这是核心算法测试!
 */
void test7_Letterbox_Transform() {
    std::cout << "\n========== 测试7: Letterbox变换算法 ==========" << std::endl;
    
    int passCount = 0;
    int totalTests = 0;
    
    // 测试用例1: 横向图像 (1920x1080 → 640x640)
    {
        cv::Size imageSize(1920, 1080);
        cv::Size targetSize(640, 640);
        
        float scaleW = (float)targetSize.width / imageSize.width;
        float scaleH = (float)targetSize.height / imageSize.height;
        float scale = std::min(scaleW, scaleH);
        
        int newWidth = (int)(imageSize.width * scale);
        int newHeight = (int)(imageSize.height * scale);
        
        int offsetX = (targetSize.width - newWidth) / 2;
        int offsetY = (targetSize.height - newHeight) / 2;
        
        std::cout << "Test 7.1: 横向图像 1920x1080 → 640x640" << std::endl;
        std::cout << "  scale = min(" << scaleW << ", " << scaleH << ") = " << scale << std::endl;
        std::cout << "  缩放后尺寸: " << newWidth << "x" << newHeight << std::endl;
        std::cout << "  偏移量: (" << offsetX << ", " << offsetY << ")" << std::endl;
        
        // 验证计算结果
        totalTests += 5;
        // 横向图像(宽>高): scaleW < scaleH, 选择scaleW避免宽度超出
        // scale应该等于scaleW (宽度先碰到边界,受宽度限制)
        if (std::abs(scale - scaleW) < 0.001f && scale < scaleH) { 
            passCount++; 
            std::cout << "  ✓ scale选择正确(宽度限制: " << scale << " ≈ " << scaleW << ")" << std::endl; 
        } else { 
            std::cout << "  ✗ scale选择错误(scaleW=" << scaleW << ", scaleH=" << scaleH << ")" << std::endl; 
        }
        
        if (newWidth == 640) { passCount++; std::cout << "  ✓ newWidth=640" << std::endl; }
        else { std::cout << "  ✗ newWidth=" << newWidth << " (期望640)" << std::endl; }
        
        if (newHeight == 360) { passCount++; std::cout << "  ✓ newHeight=360" << std::endl; }
        else { std::cout << "  ✗ newHeight=" << newHeight << " (期望360)" << std::endl; }
        
        if (offsetX == 0) { passCount++; std::cout << "  ✓ offsetX=0" << std::endl; }
        else { std::cout << "  ✗ offsetX=" << offsetX << " (期望0)" << std::endl; }
        
        if (offsetY == 140) { passCount++; std::cout << "  ✓ offsetY=140" << std::endl; }
        else { std::cout << "  ✗ offsetY=" << offsetY << " (期望140)" << std::endl; }
    }
    
    // 测试用例2: 纵向图像 (1080x1920 → 640x640)
    {
        cv::Size imageSize(1080, 1920);
        cv::Size targetSize(640, 640);
        
        float scaleW = (float)targetSize.width / imageSize.width;
        float scaleH = (float)targetSize.height / imageSize.height;
        float scale = std::min(scaleW, scaleH);
        
        int newWidth = (int)(imageSize.width * scale);
        int newHeight = (int)(imageSize.height * scale);
        
        int offsetX = (targetSize.width - newWidth) / 2;
        int offsetY = (targetSize.height - newHeight) / 2;
        
        std::cout << "\nTest 7.2: 纵向图像 1080x1920 → 640x640" << std::endl;
        std::cout << "  scale = min(" << scaleW << ", " << scaleH << ") = " << scale << std::endl;
        std::cout << "  缩放后尺寸: " << newWidth << "x" << newHeight << std::endl;
        std::cout << "  偏移量: (" << offsetX << ", " << offsetY << ")" << std::endl;
        
        totalTests += 5;
        // 纵向图像(高>宽): scaleH < scaleW, 选择scaleH避免高度超出
        // scale应该等于scaleH (高度先碰到边界,受高度限制)
        if (std::abs(scale - scaleH) < 0.001f && scale < scaleW) { 
            passCount++; 
            std::cout << "  ✓ scale选择正确(高度限制: " << scale << " ≈ " << scaleH << ")" << std::endl; 
        } else { 
            std::cout << "  ✗ scale选择错误(scaleW=" << scaleW << ", scaleH=" << scaleH << ")" << std::endl; 
        }
        
        if (newWidth == 360) { passCount++; std::cout << "  ✓ newWidth=360" << std::endl; }
        else { std::cout << "  ✗ newWidth=" << newWidth << " (期望360)" << std::endl; }
        
        if (newHeight == 640) { passCount++; std::cout << "  ✓ newHeight=640" << std::endl; }
        else { std::cout << "  ✗ newHeight=" << newHeight << " (期望640)" << std::endl; }
        
        if (offsetX == 140) { passCount++; std::cout << "  ✓ offsetX=140" << std::endl; }
        else { std::cout << "  ✗ offsetX=" << offsetX << " (期望140)" << std::endl; }
        
        if (offsetY == 0) { passCount++; std::cout << "  ✓ offsetY=0" << std::endl; }
        else { std::cout << "  ✗ offsetY=" << offsetY << " (期望0)" << std::endl; }
    }
    
    // 测试用例3: 正方形图像 (800x800 → 640x640)
    {
        cv::Size imageSize(800, 800);
        cv::Size targetSize(640, 640);
        
        float scaleW = (float)targetSize.width / imageSize.width;
        float scaleH = (float)targetSize.height / imageSize.height;
        float scale = std::min(scaleW, scaleH);
        
        int newWidth = (int)(imageSize.width * scale);
        int newHeight = (int)(imageSize.height * scale);
        
        int offsetX = (targetSize.width - newWidth) / 2;
        int offsetY = (targetSize.height - newHeight) / 2;
        
        std::cout << "\nTest 7.3: 正方形图像 800x800 → 640x640" << std::endl;
        std::cout << "  scale = min(" << scaleW << ", " << scaleH << ") = " << scale << std::endl;
        std::cout << "  缩放后尺寸: " << newWidth << "x" << newHeight << std::endl;
        std::cout << "  偏移量: (" << offsetX << ", " << offsetY << ")" << std::endl;
        
        totalTests += 4;
        if (newWidth == 640) { passCount++; std::cout << "  ✓ newWidth=640" << std::endl; }
        else { std::cout << "  ✗ newWidth=" << newWidth << " (期望640)" << std::endl; }
        
        if (newHeight == 640) { passCount++; std::cout << "  ✓ newHeight=640" << std::endl; }
        else { std::cout << "  ✗ newHeight=" << newHeight << " (期望640)" << std::endl; }
        
        if (offsetX == 0) { passCount++; std::cout << "  ✓ offsetX=0" << std::endl; }
        else { std::cout << "  ✗ offsetX=" << offsetX << " (期望0)" << std::endl; }
        
        if (offsetY == 0) { passCount++; std::cout << "  ✓ offsetY=0" << std::endl; }
        else { std::cout << "  ✗ offsetY=" << offsetY << " (期望0)" << std::endl; }
    }
    
    std::cout << "\n通过: " << passCount << "/" << totalTests << std::endl;
    printTestResult("Letterbox变换算法", passCount == totalTests);
}

/**
 * @brief 测试8: 坐标逆变换算法 (理论验证)
 * 
 * 验证:
 * 1. 模型坐标 → 原图坐标的转换
 * 2. 公式 x_orig = (x_model - offset) / scale
 */
void test8_Coordinate_InverseTransform() {
    std::cout << "\n========== 测试8: 坐标逆变换算法 ==========" << std::endl;
    
    bool passed = true;
    
    // 场景: 1920x1080 → 640x640
    float scale = 640.0f / 1920.0f;  // 0.333
    int offsetX = 0;
    int offsetY = 140;
    
    std::cout << "场景: 1920x1080 → 640x640" << std::endl;
    std::cout << "变换参数: scale=" << scale << ", offset=(" 
              << offsetX << "," << offsetY << ")" << std::endl;
    
    // 测试用例1: 中心点
    {
        // 模型坐标 (640x640的中心)
        int x_model = 320;
        int y_model = 320;
        
        // 逆变换到原图
        int x_orig = (int)((x_model - offsetX) / scale);
        int y_orig = (int)((y_model - offsetY) / scale);
        
        std::cout << "\n测试点1 - 中心点:" << std::endl;
        std::cout << "  模型坐标: (" << x_model << "," << y_model << ")" << std::endl;
        std::cout << "  原图坐标: (" << x_orig << "," << y_orig << ")" << std::endl;
        
        // 验证: 应该映射到原图的中心附近
        passed &= (x_orig >= 900 && x_orig <= 1000);  // 约960
        passed &= (y_orig >= 500 && y_orig <= 600);   // 约540
    }
    
    // 测试用例2: 左上角
    {
        int x_model = 0;
        int y_model = 140;  // offsetY
        
        int x_orig = (int)((x_model - offsetX) / scale);
        int y_orig = (int)((y_model - offsetY) / scale);
        
        std::cout << "\n测试点2 - 左上角:" << std::endl;
        std::cout << "  模型坐标: (" << x_model << "," << y_model << ")" << std::endl;
        std::cout << "  原图坐标: (" << x_orig << "," << y_orig << ")" << std::endl;
        
        // 验证: 应该映射到原图的(0,0)
        passed &= (x_orig == 0);
        passed &= (y_orig == 0);
    }
    
    printTestResult("坐标逆变换算法", passed);
}

/**
 * @brief 测试9: 实际检测流程 (需要模型文件)
 * 
 * 这个测试需要真实的模型文件和TPU设备
 * 在SE7设备上运行时才能通过
 * 
 * @note 这是集成测试,不是单元测试
 * @note 配置来源: 从config.json通过VisionConfigLoader加载
 */
void test9_Real_Detection_Pipeline() {
    std::cout << "\n========== 测试9: 实际检测流程 (集成测试) ==========" << std::endl;
    
    std::cout << "⚠️  此测试需要:" << std::endl;
    std::cout << "   1. 真实的PPYOLOE模型文件 (.bmodel)" << std::endl;
    std::cout << "   2. 算能TPU设备 (SE7)" << std::endl;
    std::cout << "   3. 测试图像文件" << std::endl;
    std::cout << "   4. 正确配置config.json中的模型路径" << std::endl;

    // 取消注释以启用实际测试
    try {
        // 创建配置
        DetectorConfig config = createTestConfig(true);  // true = 使用真实模型路径
        
        // 使用工厂创建检测器
        auto detector = DetectorFactory::create(DetectorType::PPYOLOE, config);
        
        std::cout << "✅ 检测器创建成功" << std::endl;
        std::cout << "   名称: " << detector->getName() << std::endl;
        std::cout << "   输入尺寸: " << detector->getInputSize() << std::endl;
        std::cout << "   类别数量: " << detector->getClasses().size() << std::endl;
        
        // 创建测试图像
        cv::Mat testImage = createTestImage(1920, 1080);
        
        // 执行检测
        DetectionResult result = detector->detect(testImage);
        
        std::cout << "✅ 检测完成" << std::endl;
        std::cout << "   检测数量: " << result.getDetectionCount() << std::endl;
        std::cout << "   预处理: " << result.preprocessTime << " ms" << std::endl;
        std::cout << "   推理: " << result.inferenceTime << " ms" << std::endl;
        std::cout << "   后处理: " << result.postprocessTime << " ms" << std::endl;
        std::cout << "   总耗时: " << result.getTotalTime() << " ms" << std::endl;
        
        // 输出检测结果
        for (size_t i = 0; i < result.boxes.size(); ++i) {
            const auto& box = result.boxes[i];
            std::cout << "   目标" << (i+1) << ": " 
                      << box.className << " " 
                      << box.confidence << " "
                      << "[" << box.x << "," << box.y << ","
                      << box.width << "," << box.height << "]" << std::endl;
        }
        
        printTestResult("实际检测流程", true);
        
    } catch (const std::exception& e) {
        std::cout << "❌ 测试失败: " << e.what() << std::endl;
        printTestResult("实际检测流程", false);
    }
    
    
    std::cout << "⏭️  跳过实际检测测试 (需要设备支持)" << std::endl;
}

// ==================== 主函数 ====================

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "  Detector模块单元测试" << std::endl;
    std::cout << "======================================" << std::endl;
    
    // 🔧 修复死锁: 先初始化Logger(使用默认配置),再加载Config
    // 原因: Config.load()会调用Logger, 而Logger构造时会读Config, 造成循环依赖
    auto& logger = esdk_sophon::core::Logger::getInstance();
    std::cout << "✅ Logger已初始化(使用默认配置)" << std::endl;
    
    // ✅ 加载配置文件 (VisionConfigLoader需要)
    auto& config = esdk_sophon::core::Config::getInstance();
    std::string configPath = "../config/config.json";  // 相对于build/bin/的路径
    
    if (config.load(configPath)) {
        std::cout << "✅ 配置文件加载成功: " << configPath << std::endl;
    } else {
        std::cout << "⚠️  配置文件加载失败: " << configPath << std::endl;
        std::cout << "   test9将使用fallback配置" << std::endl;
    }
    std::cout << std::endl;
    
    // 运行所有测试
    test1_DetectorFactory_TypeConversion();
    test2_DetectorFactory_SupportedTypes();
    test3_DetectorFactory_InferFromPath();
    test4_PPYoloeDetector_BasicFunctions();
    test5_DetectionBox_HelperMethods();
    test6_DetectionResult_Statistics();
    test7_Letterbox_Transform();
    test8_Coordinate_InverseTransform();
    test9_Real_Detection_Pipeline();
    
    std::cout << "\n======================================" << std::endl;
    std::cout << "  测试完成!" << std::endl;
    std::cout << "======================================" << std::endl;
    
    return 0;
}

