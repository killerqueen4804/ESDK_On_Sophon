# 08-Utils 工具模块详解

> **学习目标**：掌握 Utils 工具模块中的各个组件实现，包括 EXIF 解析、GPS 计算、坐标系转换、Base64 编码等实用功能

---

## 1. 模块概述

### 1.1 Utils 模块的职责

Utils 模块是项目的**工具集合**，提供了各种通用的辅助功能：

```
src/utils/
├── ImageProcessor.cpp   # 图片处理：EXIF解析、Base64、缩放旋转
├── GeoUtils.cpp         # 地理工具：坐标转换、距离计算、轨迹分析
├── HttpClient.cpp       # HTTP客户端：REST API调用
├── FileUtils.cpp        # 文件操作：路径处理、文件读写
├── StringUtils.cpp      # 字符串工具：编码转换、格式化
└── TimeUtils.cpp        # 时间工具：时间戳转换、格式化
```

### 1.2 在数据流中的位置

```
无人机照片
    ↓
ImageProcessor.parseExif()  ← 从JPEG提取GPS坐标
    ↓
GeoUtils.wgs84ToGcj02()     ← 坐标系转换（WGS84→国测局）
    ↓
HttpClient.post()            ← 调用GeoDecodeAPI计算像素坐标
    ↓
检测结果上报（含GPS信息）
```

---

## 2. ImageProcessor 图片处理器

### 2.1 核心功能

ImageProcessor 是一个**单例工具类**，提供以下功能：

| 功能        | 方法                        | 用途                             |
| ----------- | --------------------------- | -------------------------------- |
| EXIF 解析   | `parseExif()`               | 从 JPEG 提取 GPS、时间、相机信息 |
| Base64 编码 | `encodeBase64()`            | 图片转字符串，用于 MQTT 传输     |
| 图片缩放    | `resize()`                  | 调整图片尺寸                     |
| 图片旋转    | `rotate()` / `autoRotate()` | 根据 EXIF 方向自动校正           |
| 质量压缩    | `compressToSize()`          | 压缩到指定大小                   |

### 2.2 EXIF 元数据结构

```cpp
/**
 * @brief EXIF 元数据结构体
 *
 * 📌 知识点：EXIF（Exchangeable Image File Format）
 * - JPEG图片的元数据标准
 * - 包含拍摄参数、GPS信息、相机信息等
 * - 存储在JPEG文件的APP1段中
 */
struct ExifMetadata {
    // ========== GPS 信息 ==========
    double latitude{0.0};      // 纬度（度）：-90 ~ 90
    double longitude{0.0};     // 经度（度）：-180 ~ 180
    double altitude{0.0};      // 海拔（米）
    double heading{0.0};       // 航向角（度）：0 ~ 360，DJI特有
    bool hasGps{false};        // 是否包含GPS

    // ========== 时间信息 ==========
    std::string timestamp;     // 拍摄时间
    std::string dateTime;      // 原始日期时间字符串

    // ========== 相机参数 ==========
    std::string cameraMake;    // 制造商："DJI"
    std::string cameraModel;   // 型号："Mavic 3"
    int imageWidth{0};         // 图片宽度
    int imageHeight{0};        // 图片高度
    int orientation{1};        // EXIF方向（1-8）

    // ========== 曝光参数 ==========
    double exposureTime{0.0};  // 曝光时间（秒）
    double fNumber{0.0};       // 光圈值 F/2.8
    int iso{0};                // ISO感光度
    double focalLength{0.0};   // 焦距（毫米）
};
```

### 2.3 EXIF 解析实现（exiv2）

```cpp
/**
 * @brief 解析 JPEG 图片的 EXIF 元数据
 *
 * 📌 知识点：exiv2 库
 * - C++ EXIF/IPTC/XMP 元数据处理库
 * - 比 libexif 功能更强大，支持更多格式
 * - 用法：open() → readMetadata() → exifData()
 */
std::optional<ExifMetadata> ImageProcessor::parseExif(const std::string& filePath) const {
    try {
        // 1️⃣ 打开图片文件
        // ImageFactory::open() 返回 Image::UniquePtr
        auto image = Exiv2::ImageFactory::open(filePath);
        if (!image.get()) {
            return std::nullopt;
        }

        // 2️⃣ 读取元数据到内存
        // 必须先调用 readMetadata() 才能访问 exifData()
        image->readMetadata();
        Exiv2::ExifData& exifData = image->exifData();

        if (exifData.empty()) {
            return std::nullopt;
        }

        ExifMetadata metadata;

        // 3️⃣ 解析 GPS 坐标
        // EXIF GPS 标签使用度分秒格式："39/1 54/1 23/1"
        auto gpsLat = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude"));
        auto gpsLatRef = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitudeRef"));

        if (gpsLat != exifData.end()) {
            metadata.hasGps = true;

            // 度分秒 → 十进制度
            // "39/1 54/1 23/1" → 39° 54' 23" → 39.906389°
            metadata.latitude = parseGpsCoordinateExiv2(gpsLat->toString());

            // 南纬取负
            if (gpsLatRef != exifData.end() && gpsLatRef->toString() == "S") {
                metadata.latitude = -metadata.latitude;
            }
        }

        // 4️⃣ 解析 DJI 航向角（特有标签）
        auto gpsImgDir = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSImgDirection"));
        if (gpsImgDir != exifData.end()) {
            metadata.heading = parseRationalExiv2(gpsImgDir->toString());
        }

        return metadata;

    } catch (const Exiv2::Error& e) {
        // exiv2 异常处理
        return std::nullopt;
    }
}
```

### 2.4 GPS 坐标解析

```cpp
/**
 * @brief 将 GPS 坐标从度分秒格式转换为十进制度数
 *
 * 📌 知识点：GPS坐标格式
 *
 * 1. 度分秒格式（DMS）：39°54'23"N
 *    - 39 度 54 分 23 秒 北纬
 *    - EXIF 存储格式："39/1 54/1 23/1"
 *
 * 2. 十进制度（DD）：39.906389
 *    - 常用于程序计算
 *    - 转换公式：度 + 分/60 + 秒/3600
 *
 * 3. 为什么用分数表示？
 *    - EXIF 使用 Rational 类型（分子/分母）
 *    - 避免浮点精度问题
 *    - "39/1" 表示 39.0，"2355/100" 表示 23.55
 */
double parseGpsCoordinateExiv2(const std::string& coordStr) {
    // 输入："39/1 54/1 23/1"
    std::istringstream iss(coordStr);
    std::string degStr, minStr, secStr;
    iss >> degStr >> minStr >> secStr;  // 空格分隔

    // 解析 Rational 值
    auto parseRational = [](const std::string& str) -> double {
        size_t pos = str.find('/');
        if (pos == std::string::npos) return 0.0;
        double num = std::stod(str.substr(0, pos));     // 分子
        double den = std::stod(str.substr(pos + 1));    // 分母
        return (den != 0.0) ? (num / den) : 0.0;
    };

    double deg = parseRational(degStr);  // 39.0
    double min = parseRational(minStr);  // 54.0
    double sec = parseRational(secStr);  // 23.0

    // 度分秒 → 十进制度
    // 39 + 54/60 + 23/3600 = 39.906389
    return deg + min / 60.0 + sec / 3600.0;
}
```

### 2.5 Base64 编码实现

```cpp
/**
 * @brief Base64 编码算法实现
 *
 * 📌 知识点：Base64 编码
 *
 * 1. 什么是 Base64？
 *    - 二进制数据 → 可打印ASCII字符
 *    - 用于：邮件附件、JSON传输、URL编码
 *
 * 2. 编码原理：
 *    - 每 3 字节（24bit）→ 4 个字符（每个6bit）
 *    - 字符集：A-Z(26) + a-z(26) + 0-9(10) + +/ = 64个
 *    - 不足3字节用 '=' 填充
 *
 * 3. 示例：
 *    输入: "Man" (ASCII: 77, 97, 110)
 *    二进制: 01001101 01100001 01101110
 *    分组(6bit): 010011 010110 000101 101110
 *    十进制: 19, 22, 5, 46
 *    Base64: T, W, F, u → "TWFu"
 *
 * 4. 为什么项目中需要？
 *    - MQTT消息传输检测图片
 *    - 避免二进制在JSON中的问题
 */
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"   // 0-25
    "abcdefghijklmnopqrstuvwxyz"   // 26-51
    "0123456789+/";                 // 52-63

static std::string base64_encode_impl(const unsigned char* bytes, size_t len) {
    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];  // 输入缓冲（3字节）
    unsigned char char_array_4[4];  // 输出缓冲（4字符）

    while (len--) {
        char_array_3[i++] = *(bytes++);

        if (i == 3) {  // 凑够3字节，编码
            // 第1字符：取第1字节的高6位
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;

            // 第2字符：第1字节低2位 + 第2字节高4位
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
                              ((char_array_3[1] & 0xf0) >> 4);

            // 第3字符：第2字节低4位 + 第3字节高2位
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
                              ((char_array_3[2] & 0xc0) >> 6);

            // 第4字符：第3字节低6位
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    // 处理剩余字节（不足3字节）
    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';  // 补0

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
                          ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
                          ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        // 补 '=' 填充
        while (i++ < 3)
            ret += '=';
    }

    return ret;
}
```

### 2.6 图片转 Base64 的完整流程

```cpp
/**
 * @brief 图片转 Base64 字符串
 *
 * 流程：cv::Mat → JPEG编码 → Base64编码 → 字符串
 */
std::string ImageProcessor::encodeBase64(const cv::Mat& image,
                                        const std::string& format,
                                        int quality) const {
    if (image.empty()) return "";

    // 1️⃣ OpenCV 编码为 JPEG
    std::vector<uchar> buffer;
    std::vector<int> params;
    if (format == ".jpg" || format == ".jpeg") {
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(quality);  // 默认95
    }

    if (!cv::imencode(format, image, buffer, params)) return "";

    // 2️⃣ Base64 编码
    return base64_encode_impl(buffer.data(), buffer.size());
}
```

### 2.7 批量 EXIF 解析（多线程）

```cpp
/**
 * @brief 批量解析 EXIF 元数据
 *
 * 📌 知识点：std::async 异步任务
 *
 * 1. std::async 是什么？
 *    - C++11 异步编程工具
 *    - 自动管理线程，简化并发代码
 *
 * 2. 启动策略：
 *    - std::launch::async：立即在新线程执行
 *    - std::launch::deferred：延迟到get()时执行
 *    - 默认：由实现决定
 *
 * 3. 为什么用 async 而不是手动线程池？
 *    - 代码简洁
 *    - 自动管理线程生命周期
 *    - 适合 I/O 密集型任务
 *
 * 4. 缺点：
 *    - 每次可能创建新线程（开销）
 *    - 不适合大量短任务
 */
std::vector<std::optional<ExifMetadata>> ImageProcessor::parseExifBatch(
    const std::vector<std::string>& filePaths) const {

    // 1️⃣ 创建异步任务
    std::vector<std::future<std::optional<ExifMetadata>>> futures;
    futures.reserve(filePaths.size());

    for (const auto& path : filePaths) {
        // 每个文件启动一个异步任务
        futures.push_back(std::async(std::launch::async, [this, path]() {
            return parseExif(path);
        }));
    }

    // 2️⃣ 收集结果
    std::vector<std::optional<ExifMetadata>> results;
    results.reserve(filePaths.size());

    for (auto& future : futures) {
        // get() 阻塞等待任务完成
        results.push_back(future.get());
    }

    return results;
}
```

### 2.8 图片压缩（二分查找）

```cpp
/**
 * @brief 压缩图片到目标大小
 *
 * 📌 知识点：二分查找优化
 *
 * 问题：如何找到最佳JPEG质量，使文件大小刚好小于目标值？
 *
 * 方法：
 * 1. 暴力法：从100到1逐个尝试 → O(100)
 * 2. 二分法：每次排除一半 → O(log100) ≈ 7次
 *
 * 为什么JPEG质量适合二分？
 * - 质量↑ → 文件大小↑（单调递增）
 * - 满足二分查找条件
 */
bool ImageProcessor::compressToSize(const cv::Mat& image, int maxSizeKB,
                                   std::vector<uchar>& buffer) const {
    if (image.empty()) return false;

    size_t targetBytes = maxSizeKB * 1024;
    int minQ = 1, maxQ = 100;
    int quality = 95;

    // 二分查找最佳质量
    while (minQ <= maxQ) {
        quality = (minQ + maxQ) / 2;

        // 尝试当前质量
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
        buffer.clear();
        cv::imencode(".jpg", image, buffer, params);

        if (buffer.size() <= targetBytes) {
            // 可以尝试更高质量
            minQ = quality + 1;
        } else {
            // 需要降低质量
            maxQ = quality - 1;
        }
    }

    // 最终编码（使用maxQ保证不超限）
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, maxQ};
    buffer.clear();
    return cv::imencode(".jpg", image, buffer, params);
}
```

---

## 3. GeoUtils 地理工具

### 3.1 核心功能

GeoUtils 提供地理信息处理功能：

| 功能       | 方法                          | 用途                       |
| ---------- | ----------------------------- | -------------------------- |
| 距离计算   | `calculateDistance()`         | Haversine 公式计算两点距离 |
| 高精度距离 | `calculateDistanceVincenty()` | Vincenty 公式（考虑椭球）  |
| 方位角     | `calculateBearing()`          | 计算两点间的方位角         |
| 坐标转换   | `wgs84ToGcj02()`              | WGS84→ 国测局坐标          |
| 区域判断   | `isPointInPolygon()`          | 射线法判断点在多边形内     |
| 轨迹分析   | `simplifyTrajectory()`        | Douglas-Peucker 轨迹简化   |

### 3.2 GPS 坐标结构

```cpp
/**
 * @brief GPS 坐标结构体
 *
 * 📌 知识点：GPS坐标系统
 *
 * 1. 纬度（Latitude）：
 *    - 范围：-90° ~ 90°
 *    - 正数：北纬  负数：南纬
 *    - 北京：39.9°N
 *
 * 2. 经度（Longitude）：
 *    - 范围：-180° ~ 180°
 *    - 正数：东经  负数：西经
 *    - 北京：116.4°E
 *
 * 3. 海拔（Altitude）：
 *    - 相对于海平面的高度（米）
 *    - 可以为负（如死海）
 *
 * 4. 航向角（Heading）：
 *    - 范围：0° ~ 360°
 *    - 0°=北，90°=东，180°=南，270°=西
 *    - DJI无人机特有，表示拍摄方向
 */
struct GpsCoordinate {
    double latitude{0.0};   // 纬度
    double longitude{0.0};  // 经度
    double altitude{0.0};   // 海拔（米）
    double heading{0.0};    // 航向角（度）
};
```

### 3.3 Haversine 距离公式

```cpp
/**
 * @brief 计算两点间的距离（Haversine公式）
 *
 * 📌 知识点：Haversine 公式
 *
 * 1. 什么是 Haversine？
 *    - 球面上两点间的大圆距离
 *    - 假设地球是完美球体
 *    - 精度：约 0.5% 误差
 *
 * 2. 公式推导：
 *    a = sin²(Δlat/2) + cos(lat1) × cos(lat2) × sin²(Δlon/2)
 *    c = 2 × atan2(√a, √(1-a))
 *    d = R × c
 *
 * 3. 为什么叫 Haversine？
 *    - haversin(θ) = sin²(θ/2)
 *    - 历史上用于避免小角度计算误差
 *
 * 4. 适用场景：
 *    - 短距离（<100km）精度足够
 *    - 计算简单，性能好
 */
// 常量定义
constexpr double EARTH_RADIUS_M = 6371000.0;  // 地球平均半径（米）
constexpr double DEG_TO_RAD = M_PI / 180.0;

double GeoUtils::calculateDistance(const GpsCoordinate& from,
                                   const GpsCoordinate& to) const {
    // 1️⃣ 度转弧度
    double lat1 = from.latitude * DEG_TO_RAD;
    double lon1 = from.longitude * DEG_TO_RAD;
    double lat2 = to.latitude * DEG_TO_RAD;
    double lon2 = to.longitude * DEG_TO_RAD;

    // 2️⃣ 计算差值
    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;

    // 3️⃣ Haversine 公式
    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dlon / 2.0) * std::sin(dlon / 2.0);

    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    // 4️⃣ 距离 = 半径 × 弧度
    return EARTH_RADIUS_M * c;
}
```

### 3.4 Vincenty 高精度距离公式

```cpp
/**
 * @brief 计算两点间的距离（Vincenty公式）
 *
 * 📌 知识点：Vincenty 公式
 *
 * 1. 与 Haversine 的区别：
 *    - Haversine：假设地球是球体
 *    - Vincenty：考虑地球是椭球体
 *    - 精度：Vincenty 误差 < 0.5mm
 *
 * 2. WGS84 椭球参数：
 *    - 长半轴 a = 6378137.0 m（赤道半径）
 *    - 短半轴 b = 6356752.314245 m（极半径）
 *    - 扁率 f = 1/298.257223563
 *
 * 3. 迭代求解：
 *    - Vincenty 是迭代算法
 *    - 通常 3-5 次迭代收敛
 *    - 极少数情况不收敛（对跖点）
 *
 * 4. 适用场景：
 *    - 长距离（>100km）测量
 *    - 需要高精度的应用
 *    - 测绘、导航等专业领域
 *
 * 5. 项目中为什么提供两种？
 *    - 短距离用 Haversine（快）
 *    - 高精度需求用 Vincenty
 */
// WGS84 椭球参数
constexpr double WGS84_A = 6378137.0;          // 长半轴
constexpr double WGS84_B = 6356752.314245;     // 短半轴
constexpr double WGS84_F = 1.0 / 298.257223563; // 扁率

double GeoUtils::calculateDistanceVincenty(const GpsCoordinate& from,
                                           const GpsCoordinate& to) const {
    // 转换为弧度
    double lat1 = from.latitude * DEG_TO_RAD;
    double lon1 = from.longitude * DEG_TO_RAD;
    double lat2 = to.latitude * DEG_TO_RAD;
    double lon2 = to.longitude * DEG_TO_RAD;

    // 归化纬度（考虑椭球扁率）
    double U1 = std::atan((1.0 - WGS84_F) * std::tan(lat1));
    double U2 = std::atan((1.0 - WGS84_F) * std::tan(lat2));
    double L = lon2 - lon1;

    double sinU1 = std::sin(U1), cosU1 = std::cos(U1);
    double sinU2 = std::sin(U2), cosU2 = std::cos(U2);

    // 迭代求解
    double lambda = L;
    double lambdaP = 2.0 * PI;
    int iterLimit = 100;

    // ... 迭代过程（省略详细代码）...

    if (iterLimit == 0) {
        // 未收敛，降级使用 Haversine
        return calculateDistance(from, to);
    }

    // 计算最终距离
    // ... 省略复杂计算 ...

    return WGS84_B * A * (sigma - deltaSigma);
}
```

### 3.5 坐标系转换

```cpp
/**
 * @brief WGS84 → GCJ02 坐标转换
 *
 * 📌 知识点：中国坐标系统
 *
 * 1. WGS84（World Geodetic System 1984）：
 *    - GPS 原始坐标系
 *    - 全球通用标准
 *    - 无人机、手机GPS返回的是 WGS84
 *
 * 2. GCJ02（国测局坐标/火星坐标）：
 *    - 中国强制使用的加密坐标
 *    - 对 WGS84 进行非线性偏移
 *    - 高德地图、腾讯地图使用
 *
 * 3. BD09（百度坐标）：
 *    - 百度在 GCJ02 基础上再次加密
 *    - 只有百度地图使用
 *
 * 4. 为什么需要转换？
 *    - 法律要求：中国境内地图必须使用 GCJ02
 *    - 不转换会导致定位偏移 100-700 米
 *
 * 5. 转换关系：
 *    WGS84 ↔ GCJ02 ↔ BD09
 *    GPS    高德/腾讯  百度
 */

// GCJ02 转换参数
constexpr double GCJ02_A = 6378245.0;
constexpr double GCJ02_EE = 0.00669342162296594323;

// 判断是否在中国境内
static bool isInChina(double lat, double lon) {
    // 中国经纬度范围（粗略）
    return lat >= 0.8293 && lat <= 55.8271 &&
           lon >= 72.004 && lon <= 137.8347;
}

// 坐标偏移计算
static void transformLatLon(double lat, double lon, double& dlat, double& dlon) {
    // 复杂的非线性变换公式
    double dLat = -100.0 + 2.0 * lon + 3.0 * lat + 0.2 * lat * lat +
                  0.1 * lon * lat + 0.2 * std::sqrt(std::abs(lon));
    dLat += (20.0 * std::sin(6.0 * lon * PI) + 20.0 * std::sin(2.0 * lon * PI)) * 2.0 / 3.0;
    // ... 更多复杂计算 ...

    dlat = (dLat * 180.0) / ((GCJ02_A / sqrtMagic) * (1.0 - GCJ02_EE) / magic * PI);
    dlon = (dLon * 180.0) / (GCJ02_A / sqrtMagic * std::cos(radLat) * PI);
}

GpsCoordinate GeoUtils::wgs84ToGcj02(const GpsCoordinate& wgs84) const {
    // 中国境外不需要转换
    if (!isInChina(wgs84.latitude, wgs84.longitude)) {
        return wgs84;
    }

    double dlat, dlon;
    transformLatLon(wgs84.latitude, wgs84.longitude, dlat, dlon);

    GpsCoordinate gcj02;
    gcj02.latitude = wgs84.latitude + dlat;
    gcj02.longitude = wgs84.longitude + dlon;
    gcj02.altitude = wgs84.altitude;
    gcj02.heading = wgs84.heading;

    return gcj02;
}
```

### 3.6 射线法判断点在多边形内

```cpp
/**
 * @brief 判断点是否在多边形内
 *
 * 📌 知识点：射线法（Ray Casting Algorithm）
 *
 * 1. 算法原理：
 *    - 从点向右（或任意方向）发射一条射线
 *    - 统计射线与多边形边的交点数
 *    - 奇数次：在内部  偶数次：在外部
 *
 * 2. 为什么有效？
 *    - 多边形是闭合的
 *    - 从外部进入必须穿越边界
 *    - 穿越一次进入，再穿越一次出去
 *
 * 3. 边界情况：
 *    - 点在边上：需要特殊处理
 *    - 射线穿过顶点：需要特殊处理
 *
 * 4. 项目应用：
 *    - 判断检测目标是否在指定区域内
 *    - 地理围栏功能
 */
bool GeoUtils::isPointInPolygon(const GpsCoordinate& point,
                                const Polygon& polygon) const {
    if (polygon.vertices.size() < 3) {
        return false;  // 至少需要3个顶点
    }

    bool inside = false;
    size_t j = polygon.vertices.size() - 1;  // 最后一个顶点

    for (size_t i = 0; i < polygon.vertices.size(); j = i++) {
        double xi = polygon.vertices[i].longitude;
        double yi = polygon.vertices[i].latitude;
        double xj = polygon.vertices[j].longitude;
        double yj = polygon.vertices[j].latitude;

        // 检查射线是否与边相交
        // 条件1：点的纬度在边的纬度范围内
        // 条件2：点在边的左侧
        bool intersect = ((yi > point.latitude) != (yj > point.latitude)) &&
                        (point.longitude < (xj - xi) * (point.latitude - yi) / (yj - yi) + xi);

        if (intersect) {
            inside = !inside;  // 翻转状态
        }
    }

    return inside;
}
```

### 3.7 Douglas-Peucker 轨迹简化

```cpp
/**
 * @brief 简化 GPS 轨迹（Douglas-Peucker 算法）
 *
 * 📌 知识点：Douglas-Peucker 算法
 *
 * 1. 问题背景：
 *    - GPS 轨迹包含大量冗余点
 *    - 存储和传输开销大
 *    - 需要在保持形状的前提下减少点数
 *
 * 2. 算法思想：
 *    - 连接首尾两点形成直线
 *    - 找到距离直线最远的点
 *    - 如果最大距离 > 阈值，保留该点，递归处理两段
 *    - 如果最大距离 < 阈值，丢弃中间所有点
 *
 * 3. 复杂度：
 *    - 最好：O(n log n)
 *    - 最坏：O(n²)
 *
 * 4. 阈值选择：
 *    - 阈值越大，简化越激进
 *    - 通常 1-10 米
 */
std::vector<GpsCoordinate> GeoUtils::simplifyTrajectory(
    const std::vector<GpsCoordinate>& trajectory,
    double toleranceMeters) const {

    if (trajectory.size() < 3 || toleranceMeters <= 0.0) {
        return trajectory;
    }

    // 递归 Douglas-Peucker 实现
    std::function<void(int, int, std::vector<bool>&)> douglasPeucker =
        [&](int start, int end, std::vector<bool>& keep) {
            if (end - start < 2) return;

            // 找距离直线最远的点
            double maxDistance = 0.0;
            int maxIndex = start;

            for (int i = start + 1; i < end; ++i) {
                double d = perpendicularDistance(
                    trajectory[i],
                    trajectory[start],
                    trajectory[end]
                );
                if (d > maxDistance) {
                    maxDistance = d;
                    maxIndex = i;
                }
            }

            // 超过阈值，保留该点并递归
            if (maxDistance > toleranceMeters) {
                keep[maxIndex] = true;
                douglasPeucker(start, maxIndex, keep);
                douglasPeucker(maxIndex, end, keep);
            }
            // 否则丢弃中间所有点
        };

    // 初始化：首尾必须保留
    std::vector<bool> keep(trajectory.size(), false);
    keep[0] = true;
    keep[trajectory.size() - 1] = true;

    // 执行简化
    douglasPeucker(0, trajectory.size() - 1, keep);

    // 收集保留的点
    std::vector<GpsCoordinate> simplified;
    for (size_t i = 0; i < trajectory.size(); ++i) {
        if (keep[i]) {
            simplified.push_back(trajectory[i]);
        }
    }

    return simplified;
}
```

---

## 4. HTTP 客户端

### 4.1 GeoDecodeAPI 调用

```cpp
/**
 * @brief HTTP POST 实现（简化版，使用 curl 命令）
 *
 * 📌 知识点：为什么用 system() + curl？
 *
 * 1. 简单场景的权衡：
 *    - 完整 libcurl 集成需要大量代码
 *    - 调用频率低（每帧一次）
 *    - system("curl") 简单可靠
 *
 * 2. 缺点：
 *    - 性能差（启动进程开销）
 *    - 需要系统安装 curl
 *    - 错误处理有限
 *
 * 3. 更好的方案：
 *    - libcurl 直接调用
 *    - cpp-httplib（头文件库）
 *    - cpr 库（C++ Requests）
 *
 * 4. 项目中为何如此实现？
 *    - 快速原型阶段
 *    - 后续可优化为 libcurl
 */
std::optional<nlohmann::json> GeoUtils::httpPost(
    const std::string& url,
    const nlohmann::json& requestBody) const {

    // 1️⃣ 创建临时文件（避免命令行长度限制）
    std::string tempInputFile = "/tmp/geo_request_" +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
        ".json";
    std::string tempOutputFile = "/tmp/geo_response_" +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
        ".json";

    // 2️⃣ 写入请求数据
    {
        std::ofstream ofs(tempInputFile);
        ofs << requestBody.dump();
    }

    // 3️⃣ 构建 curl 命令
    std::ostringstream cmd;
    cmd << "curl -s -X POST "           // -s: silent
        << "\"" << url << "\" "
        << "-H \"Content-Type: application/json\" "
        << "-d @" << tempInputFile << " "      // 从文件读取body
        << "-o " << tempOutputFile << " "      // 输出到文件
        << "--connect-timeout " << (httpTimeoutMs_ / 1000) << " "
        << "--max-time " << (httpTimeoutMs_ / 1000 * 2);

    // 4️⃣ 执行命令
    int result = system(cmd.str().c_str());

    // 5️⃣ 清理并解析响应
    std::remove(tempInputFile.c_str());

    if (result != 0) {
        std::remove(tempOutputFile.c_str());
        return std::nullopt;
    }

    // 读取响应
    std::string responseStr;
    {
        std::ifstream ifs(tempOutputFile);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        responseStr = ss.str();
    }
    std::remove(tempOutputFile.c_str());

    try {
        return nlohmann::json::parse(responseStr);
    } catch (...) {
        return std::nullopt;
    }
}
```

### 4.2 GPS 到像素坐标转换

```cpp
/**
 * @brief GPS坐标 → 像素坐标
 *
 * 📌 项目特定功能
 *
 * 调用后端 GeoDecodeAPI 服务，将无人机GPS坐标转换为
 * 正射影像上的像素坐标，用于在地图上标注检测位置。
 *
 * 请求：
 * {
 *     "latitude": 39.906389,
 *     "longitude": 116.397477,
 *     "altitude": 100.0,
 *     "heading": 45.0
 * }
 *
 * 响应：
 * {
 *     "x": 1234.5,
 *     "y": 5678.9
 * }
 */
PixelCoordinate GeoUtils::convertGpsToPixel(const GpsCoordinate& gps) const {
    // 1️⃣ 检查缓存（避免重复请求）
    if (cacheEnabled_) {
        std::string key = generateCacheKey(gps);
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
    }

    // 2️⃣ 构建请求
    nlohmann::json request;
    request["latitude"] = gps.latitude;
    request["longitude"] = gps.longitude;
    request["altitude"] = gps.altitude;
    request["heading"] = gps.heading;

    // 3️⃣ HTTP 请求（带重试）
    std::optional<nlohmann::json> response;
    for (int retry = 0; retry < httpRetryCount_; ++retry) {
        response = httpPost(gpsServiceUrl_, request);
        if (response) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 4️⃣ 解析响应
    PixelCoordinate pixel;
    if (!response || !response->contains("x") || !response->contains("y")) {
        pixel.success = false;
        pixel.errorMsg = "Request failed";
        return pixel;
    }

    pixel.x = (*response)["x"].get<double>();
    pixel.y = (*response)["y"].get<double>();
    pixel.success = true;

    // 5️⃣ 缓存结果
    if (cacheEnabled_) {
        std::string key = generateCacheKey(gps);
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.size() >= MAX_CACHE_SIZE) {
            cache_.erase(cache_.begin());  // LRU简化版
        }
        cache_[key] = pixel;
    }

    return pixel;
}
```

---

## 5. 面试知识点总结

### 5.1 EXIF 相关

**Q: 什么是 EXIF？如何从 JPEG 中提取 GPS 信息？**

A: EXIF（Exchangeable Image File Format）是数码相机拍摄的 JPEG 图片中嵌入的元数据标准，存储在 APP1 段中。包含拍摄参数、GPS 信息、相机型号等。

```cpp
// 使用 exiv2 库提取 GPS
auto image = Exiv2::ImageFactory::open(filePath);
image->readMetadata();
auto gpsLat = image->exifData().findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude"));
```

GPS 坐标以度分秒格式存储（如"39/1 54/1 23/1"），需要转换为十进制度：

```
度 + 分/60 + 秒/3600 = 39 + 54/60 + 23/3600 = 39.906389°
```

---

### 5.2 Base64 编码

**Q: Base64 编码原理是什么？编码后数据会增大多少？**

A:

1. **原理**：每 3 字节（24 bit）编码为 4 个字符（每个 6 bit），使用 64 个可打印 ASCII 字符
2. **字符集**：A-Z(26) + a-z(26) + 0-9(10) + +/ = 64
3. **大小变化**：原始大小 × 4/3 ≈ 增大 33%
4. **用途**：二进制数据在文本协议（JSON、XML、邮件）中传输

```cpp
// Base64 编码核心：3字节→4字符
char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;  // 第1字节高6位
char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
// ...
```

---

### 5.3 距离计算

**Q: Haversine 公式和 Vincenty 公式有什么区别？**

A:
| 对比 | Haversine | Vincenty |
|------|-----------|----------|
| 地球模型 | 球体 | 椭球体（WGS84） |
| 精度 | 误差 ~0.5% | 误差 < 0.5mm |
| 计算 | 简单公式 | 迭代求解 |
| 性能 | 快 | 慢 |
| 适用 | 短距离(<100km) | 高精度测量 |

```cpp
// Haversine 核心公式
a = sin²(Δlat/2) + cos(lat1)×cos(lat2)×sin²(Δlon/2)
c = 2 × atan2(√a, √(1-a))
d = R × c  // R = 6371km
```

---

### 5.4 坐标系转换

**Q: 为什么在中国要进行坐标系转换？WGS84、GCJ02、BD09 有什么区别？**

A:

1. **法律要求**：中国测绘法规定，在中国境内使用的地图必须使用 GCJ02 坐标系
2. **三种坐标系**：
   - **WGS84**：GPS 原始坐标，全球通用
   - **GCJ02（火星坐标）**：国测局坐标，高德/腾讯使用
   - **BD09**：百度坐标，百度地图专用
3. **不转换的后果**：定位偏移 100-700 米

转换关系：

```
WGS84 ↔ GCJ02 ↔ BD09
 GPS    高德    百度
```

---

### 5.5 射线法

**Q: 如何判断一个点是否在多边形内？**

A: **射线法（Ray Casting）**：

1. 从点向任意方向（通常向右）发射一条射线
2. 统计射线与多边形边的交点数
3. **奇数次**：在内部；**偶数次**：在外部

原理：多边形是闭合的，从外部进入必须穿越边界，穿越一次进入，再穿越一次出去。

```cpp
bool inside = false;
for (每条边) {
    if (射线与边相交) {
        inside = !inside;  // 翻转状态
    }
}
```

时间复杂度：O(n)，n 为多边形边数

---

### 5.6 Douglas-Peucker 算法

**Q: 什么是 Douglas-Peucker 轨迹简化算法？**

A: 一种**递归分治**算法，用于简化曲线/轨迹：

1. 连接首尾两点形成直线
2. 找到距离直线最远的点
3. 如果最大距离 > 阈值：保留该点，递归处理两段
4. 如果最大距离 < 阈值：丢弃中间所有点

```
原始：● ● ● ● ● ● ● ●
           ↓ (阈值=1m)
简化：●       ●     ●
```

复杂度：O(n log n) ~ O(n²)

---

### 5.7 std::async 异步

**Q: std::async 和手动创建线程有什么区别？**

A:
| 对比 | std::async | std::thread |
|------|------------|-------------|
| 线程管理 | 自动 | 手动 |
| 返回值 | std::future | 需要共享变量 |
| 异常 | 自动传播到 future | 需要手动处理 |
| 复杂度 | 简单 | 复杂 |

```cpp
// std::async 方式
auto future = std::async(std::launch::async, []{ return compute(); });
auto result = future.get();  // 自动阻塞等待

// std::thread 方式
int result;
std::thread t([&result]{ result = compute(); });
t.join();
```

**注意**：`std::async` 可能每次创建新线程，不适合大量短任务。

---

## 6. 实战练习

### 练习 1：实现 GPS 坐标验证

```cpp
// 验证 GPS 坐标是否有效
bool GeoUtils::isValidCoordinate(const GpsCoordinate& coord) const {
    // TODO: 实现
    // 纬度：-90 ~ 90
    // 经度：-180 ~ 180
}
```

### 练习 2：计算轨迹总长度

```cpp
// 计算 GPS 轨迹的总长度（米）
double GeoUtils::calculateTrajectoryLength(
    const std::vector<GpsCoordinate>& trajectory) const {
    // TODO: 遍历相邻点，累加距离
}
```

### 练习 3：实现简单的 LRU 缓存

```cpp
// 当前 cache_ 只是简单删除第一个元素
// 改进：实现真正的 LRU（最近最少使用）缓存
```

---

## 7. 总结

### 本章学习的知识点

| 知识点                    | 类别     | 面试频率   |
| ------------------------- | -------- | ---------- |
| EXIF 元数据解析           | 图像处理 | ⭐⭐       |
| Base64 编码原理           | 编码     | ⭐⭐⭐⭐   |
| Haversine 距离公式        | 算法     | ⭐⭐⭐     |
| 坐标系转换（WGS84/GCJ02） | 地理     | ⭐⭐       |
| 射线法判断点在多边形内    | 算法     | ⭐⭐⭐⭐   |
| Douglas-Peucker 简化      | 算法     | ⭐⭐⭐     |
| std::async 异步编程       | C++      | ⭐⭐⭐⭐   |
| 二分查找应用              | 算法     | ⭐⭐⭐⭐⭐ |

### 代码质量要点

1. **单例模式**：工具类使用单例，避免重复初始化
2. **const 正确性**：工具方法都是 const
3. **可选返回值**：使用 `std::optional` 表示可能失败
4. **缓存机制**：避免重复计算/请求
5. **错误处理**：HTTP 请求有重试机制

---

**下一章**：[09-Device 设备模块详解](./09-Device设备模块详解.md)（设备控制接口）
