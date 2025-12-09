/**
 * @file test_task_service.cpp
 * @brief TaskService 单元测试
 * 
 * 测试要点:
 * 1. 工具方法测试 (Base64, UUID, ISO8601时间)
 * 2. GPS坐标转换测试
 * 3. 图像编码测试
 * 4. 事件构建测试
 * 5. Mock对象测试 (Vision, MQTT)
 * 
 * @author ESDK Sophon Team
 * @date 2025-11-02 (Day 2)
 */

#include <gtest/gtest.h>
#include "esdk_sophon/task/TaskService.h"
#include "esdk_sophon/task/TaskTypes.h"
#include <opencv2/opencv.hpp>
#include <memory>

using namespace esdk_sophon::task;

// ==================== Mock 类 ====================

/**
 * @brief Mock Vision 类
 * 
 * 用于测试,不依赖真实的检测器。
 */
class MockVision : public esdk_sophon::vision::Vision {
public:
    MockVision() = default;
    ~MockVision() override = default;
    
    // 实现必要的虚函数
    // TODO: 根据实际的 Vision 接口补充
};

/**
 * @brief Mock MQTT 客户端
 * 
 * 用于测试,记录发布的消息。
 */
class MockMqttClient : public esdk_sophon::mqtt::MqttClient {
public:
    MockMqttClient() = default;
    ~MockMqttClient() override = default;
    
    bool publish(const std::string& topic, const std::string& payload) override {
        publishedMessages.push_back({topic, payload});
        return true;
    }
    
    bool isConnected() const override {
        return true;
    }
    
    // 记录发布的消息
    struct Message {
        std::string topic;
        std::string payload;
    };
    std::vector<Message> publishedMessages;
};

// ==================== 测试 Fixture ====================

/**
 * @brief TaskService 测试夹具
 * 
 * 提供通用的测试环境设置。
 */
class TaskServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 Mock 对象
        mockVision = std::make_shared<MockVision>();
        mockMqtt = std::make_shared<MockMqttClient>();
        
        // 创建 TaskService
        service = std::make_unique<TaskService>(mockVision, mockMqtt);
        
        // 准备测试配置
        config.taskId = "test_001";
        config.type = TaskType::DETECTION_LIVESTREAM;
        config.algorithmId = 16;
        config.confidenceThreshold = 0.5f;
        config.nmsThreshold = 0.45f;
        config.source = DataSource::LIVESTREAM;
        config.reportIntervalSec = 10;
        config.enableVisualization = true;
        config.maxDetectionsPerFrame = 100;
        config.deviceSn = "test_device_001";
        
        // 添加事件类型
        EventType eventType;
        eventType.id = 1;
        eventType.mainType = 1;
        eventType.eventDescribe = "测试事件";
        eventType.classIds = {0, 1};  // person, bicycle
        config.eventTypes.push_back(eventType);
    }
    
    void TearDown() override {
        service.reset();
        mockMqtt.reset();
        mockVision.reset();
    }
    
    // 测试对象
    std::shared_ptr<MockVision> mockVision;
    std::shared_ptr<MockMqttClient> mockMqtt;
    std::unique_ptr<TaskService> service;
    TaskConfig config;
};

// ==================== 工具方法测试 ====================

/**
 * @brief 测试 Base64 编码
 */
TEST_F(TaskServiceTest, Base64Encoding) {
    // 测试空数据
    std::vector<uint8_t> empty;
    std::string encoded1 = service->encodeBase64(empty);
    EXPECT_EQ(encoded1, "");
    
    // 测试 "Hello" (5字节,需要1个=填充)
    std::vector<uint8_t> hello = {'H', 'e', 'l', 'l', 'o'};
    std::string encoded2 = service->encodeBase64(hello);
    EXPECT_EQ(encoded2, "SGVsbG8=");
    
    // 测试 "Man" (3字节,正好,不需要填充)
    std::vector<uint8_t> man = {'M', 'a', 'n'};
    std::string encoded3 = service->encodeBase64(man);
    EXPECT_EQ(encoded3, "TWFu");
    
    // 测试二进制数据
    std::vector<uint8_t> binary = {0x00, 0xFF, 0xAB, 0xCD};
    std::string encoded4 = service->encodeBase64(binary);
    EXPECT_FALSE(encoded4.empty());
    EXPECT_EQ(encoded4.length(), 8);  // 4字节 → 8字符 (包含=)
}

/**
 * @brief 测试 UUID 生成
 */
TEST_F(TaskServiceTest, UUIDGeneration) {
    // 生成 UUID
    std::string uuid1 = service->generateUUID();
    std::string uuid2 = service->generateUUID();
    
    // 检查格式 (xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx)
    EXPECT_EQ(uuid1.length(), 36);
    EXPECT_EQ(uuid1[8], '-');
    EXPECT_EQ(uuid1[13], '-');
    EXPECT_EQ(uuid1[18], '-');
    EXPECT_EQ(uuid1[23], '-');
    
    // 检查版本号 (第15个字符应该是'4')
    EXPECT_EQ(uuid1[14], '4');
    
    // 检查 variant (第20个字符应该是8/9/a/b)
    char variant = uuid1[19];
    EXPECT_TRUE(variant == '8' || variant == '9' || 
                variant == 'a' || variant == 'b');
    
    // 两次生成的 UUID 应该不同 (碰撞概率极低)
    EXPECT_NE(uuid1, uuid2);
}

/**
 * @brief 测试 ISO 8601 时间格式
 */
TEST_F(TaskServiceTest, ISO8601TimeFormat) {
    std::string time = service->getCurrentTimeISO8601();
    
    // 检查长度 (2025-11-02T10:30:00.123Z)
    EXPECT_EQ(time.length(), 24);
    
    // 检查格式
    EXPECT_EQ(time[4], '-');   // 年份后
    EXPECT_EQ(time[7], '-');   // 月份后
    EXPECT_EQ(time[10], 'T');  // 日期和时间分隔符
    EXPECT_EQ(time[13], ':');  // 小时后
    EXPECT_EQ(time[16], ':');  // 分钟后
    EXPECT_EQ(time[19], '.');  // 秒后
    EXPECT_EQ(time[23], 'Z');  // UTC 标记
}

/**
 * @brief 测试 JPEG 编码
 */
TEST_F(TaskServiceTest, JPEGEncoding) {
    // 创建测试图像 (640x480, 纯红色)
    cv::Mat testImage(480, 640, CV_8UC3, cv::Scalar(0, 0, 255));
    
    // 编码为 JPEG
    std::vector<uint8_t> buffer;
    bool success = service->encodeImageToJPEG(testImage, 85, buffer);
    
    EXPECT_TRUE(success);
    EXPECT_FALSE(buffer.empty());
    
    // JPEG 文件头应该是 0xFF 0xD8
    EXPECT_EQ(buffer[0], 0xFF);
    EXPECT_EQ(buffer[1], 0xD8);
    
    // JPEG 文件尾应该是 0xFF 0xD9
    EXPECT_EQ(buffer[buffer.size()-2], 0xFF);
    EXPECT_EQ(buffer[buffer.size()-1], 0xD9);
}

/**
 * @brief 测试 GPS 坐标转换
 */
TEST_F(TaskServiceTest, PixelToGPSConversion) {
    // 测试参数
    int imageWidth = 640;
    int imageHeight = 480;
    double droneLat = 31.230391;   // 上海
    double droneLon = 121.473701;
    double droneAlt = 100.0;       // 100米高度
    double droneYaw = 0.0;         // 正北方向
    
    // 测试图像中心点 (应该返回飞机位置)
    double targetLat1, targetLon1;
    bool success1 = service->pixelToGPS(
        imageWidth/2, imageHeight/2,
        imageWidth, imageHeight,
        droneLat, droneLon, droneAlt, droneYaw,
        targetLat1, targetLon1
    );
    
    EXPECT_TRUE(success1);
    EXPECT_NEAR(targetLat1, droneLat, 0.0001);   // 中心点应该接近飞机位置
    EXPECT_NEAR(targetLon1, droneLon, 0.0001);
    
    // 测试偏离中心的点
    double targetLat2, targetLon2;
    bool success2 = service->pixelToGPS(
        imageWidth, imageHeight,  // 右下角
        imageWidth, imageHeight,
        droneLat, droneLon, droneAlt, droneYaw,
        targetLat2, targetLon2
    );
    
    EXPECT_TRUE(success2);
    EXPECT_NE(targetLat2, droneLat);  // 应该有偏移
    EXPECT_NE(targetLon2, droneLon);
}

/**
 * @brief 测试上报间隔检查
 */
TEST_F(TaskServiceTest, ReportIntervalCheck) {
    std::string taskId = "test_task_001";
    int intervalSec = 5;
    
    // 第一次应该返回 true (第一次上报)
    EXPECT_TRUE(service->shouldReportEvent(taskId, intervalSec));
    
    // 立即再次调用应该返回 false (未到间隔)
    EXPECT_FALSE(service->shouldReportEvent(taskId, intervalSec));
    
    // 等待 6 秒后应该返回 true
    std::this_thread::sleep_for(std::chrono::seconds(6));
    EXPECT_TRUE(service->shouldReportEvent(taskId, intervalSec));
}

// ==================== 集成测试 ====================

/**
 * @brief 测试事件构建
 */
TEST_F(TaskServiceTest, BuildEvent) {
    // 准备测试数据
    cv::Mat testImage(480, 640, CV_8UC3, cv::Scalar(128, 128, 128));
    
    std::vector<BoundingBox> boxes;
    BoundingBox box1;
    box1.x = 100;
    box1.y = 200;
    box1.w = 50;
    box1.h = 80;
    box1.classId = 0;
    box1.className = "person";
    box1.confidence = 0.95f;
    boxes.push_back(box1);
    
    // 构建事件
    DetectionEvent event = service->buildEvent(
        testImage, boxes, config, config.eventTypes[0]
    );
    
    // 验证结果
    EXPECT_FALSE(event.uuid.empty());
    EXPECT_EQ(event.taskId, config.taskId);
    EXPECT_EQ(event.eventType, config.eventTypes[0].id);
    EXPECT_FALSE(event.pictureBase64.empty());
    EXPECT_EQ(event.pictureCode, ".jpg");
    EXPECT_FALSE(event.createTime.empty());
    EXPECT_EQ(event.points.size(), 1);
    EXPECT_EQ(event.points[0].classId, 0);
    EXPECT_FLOAT_EQ(event.points[0].confidence, 0.95f);
}

/**
 * @brief 测试事件发布
 */
TEST_F(TaskServiceTest, PublishEvent) {
    // 准备事件
    DetectionEvent event;
    event.uuid = "test-uuid-001";
    event.taskId = config.taskId;
    event.eventType = 1;
    event.mainType = 1;
    event.eventDescribe = "测试事件";
    event.latitude = 31.23;
    event.longitude = 121.47;
    event.createTime = "2025-11-02T10:30:00.000Z";
    
    // 发布事件
    bool success = service->publishEvent(event, config.deviceSn);
    
    EXPECT_TRUE(success);
    EXPECT_EQ(mockMqtt->publishedMessages.size(), 1);
    
    // 验证 topic
    std::string expectedTopic = "drone/" + config.deviceSn + "/info/event";
    EXPECT_EQ(mockMqtt->publishedMessages[0].topic, expectedTopic);
    
    // 验证 payload 包含 UUID
    EXPECT_NE(mockMqtt->publishedMessages[0].payload.find("test-uuid-001"), 
              std::string::npos);
}

// ==================== 运行所有测试 ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
