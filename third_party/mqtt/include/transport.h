#include <opencv2/opencv.hpp>
#include "mqtt/async_client.h"
#include <libexif/exif-data.h>
#include <nlohmann/json.hpp>
using namespace std;

// 事件类型枚举
enum class EventType {
    GARBAGE_DUMP = 200004,
    RIVER_POLLUTION = 200005,
    THREE_ILLEGAL_BUILDINGS = 200006,
    VEHICLE_DETECTION = 200007
};

// 事件数据结构
struct DroneEvent {
    std::string UUID;
    int taskID;
    EventType eventType;
    std::string createTime;
    std::string eventDescribe;
    std::string picture;       // base64编码的图片数据
    std::string pictureCode;   // jpg/png
    float longitude;
    float latitude;
    
    // 转换为JSON
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["UUID"] = UUID;
        j["taskID"] = taskID;
        j["eventType"] = static_cast<int>(eventType);
        j["createTime"] = createTime;
        j["eventDescribe"] = eventDescribe;
        j["picture"] = picture;
        j["pictureCode"] = pictureCode;
        j["longitude"] = longitude;
        j["latitude"] = latitude;
        return j;
    }
};


class DroneEventPublisher {
private:
    mqtt::async_client client;
    mqtt::connect_options connOpts;
    std::string deviceSN;
    
    
    // 生成UUID
std::string generateUUID() {}

public:
    DroneEventPublisher(const std::string& serverURI, const std::string& clientID);
    
    // 连接到MQTT服务器
    bool connect();
    
    // 断开连接
    void disconnect();
    
    // 发布事件
    bool publishEvent(const DroneEvent& event);
    
    // 创建并发布事件的便捷方法
    bool createAndPublishEvent(
        int taskID,
        EventType eventType,
        const std::string& eventDescribe,
        const std::string& base64Image,
        const std::string& imageFormat,
        string createTime,
        float longitude,
        float latitude
    );
    // 将 EXIF 的 DMS (度分秒) 格式转换为十进制
double dmsToDecimal(const ExifRational* values, int count, char ref);

void readExifData(string fileName,const cv::Mat& image, std::string& dateTime, float& latitude, float& longitude);

string getFileExtension(const string& fileName);

void send_img(string fileName,cv::Mat img);
};