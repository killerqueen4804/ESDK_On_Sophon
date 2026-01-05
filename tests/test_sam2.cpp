/**
 * @file test_sam2.cpp
 * @brief SAM2 分割器测试程序
 * 
 * 测试 SAM2 与 YOLO 检测器的集成
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-11
 */

#include "esdk_sophon/vision/segmentation/Sam2Segmentor.h"
#include "esdk_sophon/vision/DetectorFactory.h"
#include "esdk_sophon/core/Logger.h"
#include <opencv2/opencv.hpp>
#include <iostream>

using namespace esdk_sophon::vision;
using namespace esdk_sophon::core;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "用法: " << argv[0] 
                  << " <encoder.bmodel> <decoder.bmodel> <test_image.jpg>" << std::endl;
        return -1;
    }

    std::string encoderPath = argv[1];
    std::string decoderPath = argv[2];
    std::string imagePath = argv[3];

    auto& logger = Logger::getInstance();
    logger.info("========================================");
    logger.info("SAM2 分割器测试");
    logger.info("========================================");

    // 1. 加载测试图像
    cv::Mat frame = cv::imread(imagePath);
    if (frame.empty()) {
        logger.error("无法加载图像: " + imagePath);
        return -1;
    }
    logger.info("✓ 图像已加载: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows));

    // 2. 初始化检测器 (使用项目现有的 YOLO)
    logger.info("初始化 YOLO 检测器...");
    
    std::unique_ptr<IDetector> detector;
    try {
        // 创建检测器配置
        DetectorConfig detConfig;
        detConfig.modelPath = "/data/models/yolo/ppyoloe.bmodel";
        detConfig.confidenceThreshold = 0.5f;  // 正确的字段名
        detConfig.nmsThreshold = 0.5f;
        
        // DetectorFactory::create() 会自动调用 initialize()
        detector = DetectorFactory::create(DetectorType::PPYOLOE, detConfig);
        logger.info("✓ YOLO 检测器创建并初始化成功");
    } catch (const std::exception& e) {
        logger.error("YOLO 检测器初始化失败: " + std::string(e.what()));
        return -1;
    }

    // 3. 执行检测
    logger.info("执行目标检测...");
    auto detResult = detector->detect(frame);
    logger.info("✓ 检测完成: 找到 " + std::to_string(detResult.boxes.size()) + " 个目标");

    if (detResult.boxes.empty()) {
        logger.warning("未检测到目标，退出测试");
        return 0;
    }

    // 4. 初始化 SAM2 分割器
    logger.info("初始化 SAM2 分割器...");
    auto segmentor = std::make_unique<Sam2Segmentor>();
    if (!segmentor->init(encoderPath, decoderPath)) {
        logger.error("SAM2 初始化失败");
        return -1;
    }
    logger.info("✓ SAM2 分割器初始化成功");

    // 5. 执行分割
    logger.info("执行分割...");
    auto segResults = segmentor->segmentWithPrompts(frame, detResult.boxes);
    logger.info("✓ 分割完成: 生成 " + std::to_string(segResults.size()) + " 个掩码");

    // 6. 可视化结果
    logger.info("绘制结果...");
    cv::Mat visFrame = frame.clone();
    
    for (size_t i = 0; i < segResults.size(); ++i) {
        const auto& res = segResults[i];
        
        // 绘制检测框
        cv::rectangle(visFrame, res.box, cv::Scalar(0, 255, 0), 2);
        
        // 绘制类别标签
        std::string label = res.className + " " + std::to_string(res.confidence).substr(0, 4);
        cv::putText(visFrame, label, cv::Point(res.box.x, res.box.y - 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
        
        // 绘制轮廓 (contours 现在是单个轮廓 vector<Point>)
        if (!res.contours.empty()) {
            std::vector<std::vector<cv::Point>> contoursList;
            contoursList.push_back(res.contours);
            cv::drawContours(visFrame, contoursList, -1, cv::Scalar(255, 0, 0), 2);
        }
        
        // 绘制半透明掩码
        cv::Mat colorMask = cv::Mat::zeros(frame.size(), CV_8UC3);
        cv::Scalar color(rand() % 256, rand() % 256, rand() % 256);
        colorMask.setTo(color, res.mask);
        cv::addWeighted(visFrame, 0.7, colorMask, 0.3, 0, visFrame);
        
        logger.info("  [" + std::to_string(i) + "] " + res.className + 
                    ", IOU=" + std::to_string(res.confidence) + 
                    ", 轮廓点数=" + std::to_string(res.contours.size()));
    }

    // 7. 保存结果
    std::string outputPath = "sam2_result.jpg";
    cv::imwrite(outputPath, visFrame);
    logger.info("✓ 结果已保存: " + outputPath);

    logger.info("========================================");
    logger.info("测试完成！");
    logger.info("========================================");

    return 0;
}
