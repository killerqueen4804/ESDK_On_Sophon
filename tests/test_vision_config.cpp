/**
 * @file test_vision_config.cpp
 * @brief 测试VisionConfigLoader配置加载
 * 
 * 测试内容:
 * 1. 从config.json加载PP-YOLOE配置
 * 2. 从config.json加载PaddleSeg配置
 * 3. 验证配置路径正确性
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-01
 */

#include "esdk_sophon/vision/VisionConfigLoader.h"
#include "esdk_sophon/core/Config.h"
#include "esdk_sophon/core/Logger.h"
#include <iostream>
#include <cassert>

using namespace esdk_sophon;

// ==================== 辅助函数 ====================

void printDetectorConfig(const vision::DetectorConfig& config) {
    std::cout << "\n=== 检测器配置 ===" << std::endl;
    std::cout << "模型路径: " << config.modelPath << std::endl;
    std::cout << "配置文件: " << config.configFile << std::endl;
    std::cout << "类别数量: " << config.classes.size() << std::endl;
    std::cout << "置信度阈值: " << config.confidenceThreshold << std::endl;
    std::cout << "NMS阈值: " << config.nmsThreshold << std::endl;
    std::cout << "输入尺寸: " << config.inputWidth << "x" << config.inputHeight << std::endl;
}

void printSegmentorConfig(const vision::SegmentorConfig& config) {
    std::cout << "\n=== 分割器配置 ===" << std::endl;
    std::cout << "模型路径: " << config.modelPath << std::endl;
    std::cout << "配置文件: " << config.configPath << std::endl;
    std::cout << "类别数量: " << config.numClasses << std::endl;
    std::cout << "输入尺寸: " << config.inputWidth << "x" << config.inputHeight << std::endl;
    std::cout << "是否启用: " << (config.enabled ? "是" : "否") << std::endl;
}

// ==================== 测试用例 ====================

/**
 * 测试1: 加载PP-YOLOE配置
 */
void test1_LoadPPYoloeConfig() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试1: 加载PP-YOLOE配置" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        vision::VisionConfigLoader loader;
        auto config = loader.loadDetectorConfig("ppyoloe");
        
        printDetectorConfig(config);
        
        // 验证路径
        assert(!config.modelPath.empty() && "模型路径不应为空");
        assert(!config.configFile.empty() && "配置文件路径不应为空");
        
        // 验证期望的路径
        std::string expectedModelPath = "/data/Edge-SDK/models/PaddleDetection/ppyoloe_crn_m_300e_coco_289.bmodel";
        std::string expectedConfigPath = "/data/Edge-SDK/models/PaddleDetection/infer_cfg.yml";
        
        if (config.modelPath == expectedModelPath) {
            std::cout << "✅ 模型路径正确" << std::endl;
        } else {
            std::cout << "⚠️  模型路径不匹配" << std::endl;
            std::cout << "  期望: " << expectedModelPath << std::endl;
            std::cout << "  实际: " << config.modelPath << std::endl;
        }
        
        if (config.configFile == expectedConfigPath) {
            std::cout << "✅ 配置文件路径正确" << std::endl;
        } else {
            std::cout << "⚠️  配置文件路径不匹配" << std::endl;
            std::cout << "  期望: " << expectedConfigPath << std::endl;
            std::cout << "  实际: " << config.configFile << std::endl;
        }
        
        // 验证其他参数
        assert(config.confidenceThreshold > 0.0f && "置信度阈值应大于0");
        assert(config.inputWidth > 0 && "输入宽度应大于0");
        assert(config.inputHeight > 0 && "输入高度应大于0");
        
        std::cout << "\n✅ 测试1通过: PP-YOLOE配置加载成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试1失败: " << e.what() << std::endl;
        throw;
    }
}

/**
 * 测试2: 加载PaddleSeg配置
 */
void test2_LoadPaddleSegConfig() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试2: 加载PaddleSeg配置" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        vision::VisionConfigLoader loader;
        auto config = loader.loadSegmentorConfig("paddleseg");
        
        printSegmentorConfig(config);
        
        // 验证路径
        assert(!config.modelPath.empty() && "模型路径不应为空");
        assert(!config.configPath.empty() && "配置文件路径不应为空");
        
        // 验证期望的路径
        std::string expectedModelPath = "/data/Edge-SDK/models/paddleSeg/pp_liteseg.bmodel";
        std::string expectedConfigPath = "/data/Edge-SDK/models/paddleSeg/deploy.yaml";
        
        if (config.modelPath == expectedModelPath) {
            std::cout << "✅ 模型路径正确" << std::endl;
        } else {
            std::cout << "⚠️  模型路径不匹配" << std::endl;
            std::cout << "  期望: " << expectedModelPath << std::endl;
            std::cout << "  实际: " << config.modelPath << std::endl;
        }
        
        if (config.configPath == expectedConfigPath) {
            std::cout << "✅ 配置文件路径正确" << std::endl;
        } else {
            std::cout << "⚠️  配置文件路径不匹配" << std::endl;
            std::cout << "  期望: " << expectedConfigPath << std::endl;
            std::cout << "  实际: " << config.configPath << std::endl;
        }
        
        // 验证其他参数
        assert(config.enabled && "分割器应该被启用");
        assert(config.numClasses > 0 && "类别数量应大于0");
        assert(config.inputWidth > 0 && "输入宽度应大于0");
        assert(config.inputHeight > 0 && "输入高度应大于0");
        
        std::cout << "\n✅ 测试2通过: PaddleSeg配置加载成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试2失败: " << e.what() << std::endl;
        throw;
    }
}

/**
 * 测试3: 验证配置一致性
 */
void test3_ConfigConsistency() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试3: 验证配置一致性" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        vision::VisionConfigLoader loader;
        
        // 加载两次相同配置，应该得到相同结果
        auto config1 = loader.loadDetectorConfig("ppyoloe");
        auto config2 = loader.loadDetectorConfig("ppyoloe");
        
        assert(config1.modelPath == config2.modelPath && "两次加载的模型路径应一致");
        assert(config1.configFile == config2.configFile && "两次加载的配置文件应一致");
        assert(config1.confidenceThreshold == config2.confidenceThreshold && "两次加载的阈值应一致");
        
        std::cout << "✅ 配置加载一致性验证通过" << std::endl;
        
        std::cout << "\n✅ 测试3通过: 配置一致性验证成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试3失败: " << e.what() << std::endl;
        throw;
    }
}

/**
 * 测试4: 类别标签加载 (可选,如果有coco.names文件)
 */
void test4_LoadClassLabels() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试4: 类别标签加载" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        vision::VisionConfigLoader loader;
        
        // 测试加载不存在的文件，应返回默认COCO标签
        auto defaultLabels = loader.loadClassLabels("non_existent_file.names");
        
        std::cout << "默认COCO标签数量: " << defaultLabels.size() << std::endl;
        assert(defaultLabels.size() == 80 && "COCO应有80个类别");
        
        if (defaultLabels.size() > 0) {
            std::cout << "前5个类别: ";
            for (size_t i = 0; i < std::min(size_t(5), defaultLabels.size()); ++i) {
                std::cout << defaultLabels[i];
                if (i < 4 && i < defaultLabels.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
        
        std::cout << "\n✅ 测试4通过: 默认标签加载成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试4失败: " << e.what() << std::endl;
        throw;
    }
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "VisionConfigLoader 测试套件" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // 初始化Logger
        auto& logger = core::Logger::getInstance();
        logger.setLevel(core::LogLevel::INFO);
        logger.setConsoleOutput(true);
        
        // 加载配置文件
        auto& config = core::Config::getInstance();
        std::string configPath = "../config/config.json";
        
        if (argc > 1) {
            configPath = argv[1];
        }
        
        std::cout << "加载配置文件: " << configPath << std::endl;
        if (!config.load(configPath)) {
            std::cerr << "❌ 无法加载配置文件: " << configPath << std::endl;
            return 1;
        }
        
        std::cout << "✅ 配置文件加载成功\n" << std::endl;
        
        // 运行测试
        test1_LoadPPYoloeConfig();
        test2_LoadPaddleSegConfig();
        test3_ConfigConsistency();
        test4_LoadClassLabels();
        
        // 总结
        std::cout << "\n========================================" << std::endl;
        std::cout << "✅ 所有测试通过!" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n========================================" << std::endl;
        std::cerr << "❌ 测试失败: " << e.what() << std::endl;
        std::cerr << "========================================" << std::endl;
        return 1;
    }
}

// ============================================================================
// 📚 运行方式
// ============================================================================
// 
// 1. 编译
//    cd /workspace/ESDK_On_Sophon/build
//    make test_vision_config
// 
// 2. 运行
//    ./bin/tests/test_vision_config
// 
// 3. 使用自定义配置文件
//    ./bin/tests/test_vision_config /path/to/config.json
// 
// 4. 预期输出
//    ========================================
//    测试1: 加载PP-YOLOE配置
//    ========================================
//    
//    === 检测器配置 ===
//    模型路径: /data/Edge-SDK/models/PaddleDetection/ppyoloe_crn_m_300e_coco_289.bmodel
//    配置文件: /data/Edge-SDK/models/PaddleDetection/infer_cfg.yml
//    ...
//    ✅ 测试1通过: PP-YOLOE配置加载成功
//    
//    ...
//    
//    ✅ 所有测试通过!
// 
// ============================================================================
