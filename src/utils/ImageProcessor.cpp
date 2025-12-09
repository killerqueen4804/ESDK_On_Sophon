/**
 * @file ImageProcessor.cpp
 * @brief 图片处理工具类实现
 *
 * 提供图片处理相关的所有功能，包括：
 * - EXIF 元数据解析（GPS、时间戳、相机信息）
 * - 图片变换（缩放、裁剪、旋转）
 * - 格式转换（Base64 编码/解码）
 * - 图片压缩
 * - 批量处理
 *
 * @author ESDK Sophon Team
 * @date 2025-11-20
 * @updated 2025-11-21 (使用 exiv2 替代 libexif)
 */

#include "esdk_sophon/utils/ImageProcessor.h"
#include "esdk_sophon/core/Logger.h"

// ⭐ 使用 exiv2（功能更强大，支持更多格式）
#include <exiv2/exiv2.hpp>

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>

// 标准库
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <future>

namespace esdk_sophon {
namespace utils {

// ==================== EXIF 解析辅助方法（exiv2 版本）====================

/**
 * @brief 将 GPS 坐标从度分秒格式转换为十进制度数
 * 
 * exiv2 返回格式: "39/1 54/1 23/1" (39度 54分 23秒)
 */
double parseGpsCoordinateExiv2(const std::string& coordStr) {
    // 解析格式: "39/1 54/1 23/1"
    std::istringstream iss(coordStr);
    std::string degStr, minStr, secStr;
    iss >> degStr >> minStr >> secStr;
    
    auto parseRational = [](const std::string& str) -> double {
        size_t pos = str.find('/');
        if (pos == std::string::npos) return 0.0;
        double num = std::stod(str.substr(0, pos));
        double den = std::stod(str.substr(pos + 1));
        return (den != 0.0) ? (num / den) : 0.0;
    };
    
    double deg = parseRational(degStr);
    double min = parseRational(minStr);
    double sec = parseRational(secStr);
    
    return deg + min / 60.0 + sec / 3600.0;
}

/**
 * @brief 解析 EXIF Rational 值
 */
double parseRationalExiv2(const std::string& str) {
    size_t pos = str.find('/');
    if (pos == std::string::npos) {
        // 可能已经是十进制数
        try {
            return std::stod(str);
        } catch (...) {
            return 0.0;
        }
    }
    double num = std::stod(str.substr(0, pos));
    double den = std::stod(str.substr(pos + 1));
    return (den != 0.0) ? (num / den) : 0.0;
}

// ==================== EXIF 解析（exiv2 实现）====================

std::optional<ExifMetadata> ImageProcessor::parseExif(const std::string& filePath) const {
    core::Logger::getInstance().debug("📸 解析 EXIF (exiv2): " + filePath);

    try {
        // 1. 读取图片
        auto image = Exiv2::ImageFactory::open(filePath);
        if (!image.get()) {
            core::Logger::getInstance().warning("⚠️ 无法打开图片: " + filePath);
            return std::nullopt;
        }
        
        image->readMetadata();
        Exiv2::ExifData& exifData = image->exifData();
        
        if (exifData.empty()) {
            core::Logger::getInstance().warning("⚠️ 图片无 EXIF 数据: " + filePath);
            return std::nullopt;
        }
        
        ExifMetadata metadata;
        
        // 2. 解析 GPS 信息
        auto gpsLat = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude"));
        auto gpsLatRef = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitudeRef"));
        auto gpsLon = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitude"));
        auto gpsLonRef = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitudeRef"));
        auto gpsAlt = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSAltitude"));
        auto gpsAltRef = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSAltitudeRef"));
        
        if (gpsLat != exifData.end() && gpsLon != exifData.end()) {
            metadata.hasGps = true;
            
            // 纬度
            metadata.latitude = parseGpsCoordinateExiv2(gpsLat->toString());
            if (gpsLatRef != exifData.end() && gpsLatRef->toString() == "S") {
                metadata.latitude = -metadata.latitude;
            }
            
            // 经度
            metadata.longitude = parseGpsCoordinateExiv2(gpsLon->toString());
            if (gpsLonRef != exifData.end() && gpsLonRef->toString() == "W") {
                metadata.longitude = -metadata.longitude;
            }
            
            // 海拔
            if (gpsAlt != exifData.end()) {
                metadata.altitude = parseRationalExiv2(gpsAlt->toString());
                if (gpsAltRef != exifData.end() && gpsAltRef->toInt64() == 1) {
                    metadata.altitude = -metadata.altitude;
                }
            }
            
            core::Logger::getInstance().debug(
                "GPS: lat=" + std::to_string(metadata.latitude) + 
                ", lon=" + std::to_string(metadata.longitude) +
                ", alt=" + std::to_string(metadata.altitude));
        }
        
        // 3. 解析航向角（DJI 特有标签）
        auto gpsImgDir = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSImgDirection"));
        if (gpsImgDir != exifData.end()) {
            metadata.heading = parseRationalExiv2(gpsImgDir->toString());
        }
        
        // 4. 解析时间信息
        auto dateTimeOrig = exifData.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
        if (dateTimeOrig != exifData.end()) {
            metadata.dateTime = dateTimeOrig->toString();
            metadata.timestamp = metadata.dateTime;
        } else {
            auto dateTime = exifData.findKey(Exiv2::ExifKey("Exif.Image.DateTime"));
            if (dateTime != exifData.end()) {
                metadata.dateTime = dateTime->toString();
                metadata.timestamp = metadata.dateTime;
            }
        }
        
        // 5. 解析相机信息
        auto make = exifData.findKey(Exiv2::ExifKey("Exif.Image.Make"));
        if (make != exifData.end()) {
            metadata.cameraMake = make->toString();
        }
        
        auto model = exifData.findKey(Exiv2::ExifKey("Exif.Image.Model"));
        if (model != exifData.end()) {
            metadata.cameraModel = model->toString();
        }
        
        // 6. 解析图片尺寸
        auto width = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelXDimension"));
        if (width != exifData.end()) {
            metadata.imageWidth = width->toInt64();
        } else {
            auto widthImage = exifData.findKey(Exiv2::ExifKey("Exif.Image.ImageWidth"));
            if (widthImage != exifData.end()) {
                metadata.imageWidth = widthImage->toInt64();
            }
        }
        
        auto height = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelYDimension"));
        if (height != exifData.end()) {
            metadata.imageHeight = height->toInt64();
        } else {
            auto heightImage = exifData.findKey(Exiv2::ExifKey("Exif.Image.ImageLength"));
            if (heightImage != exifData.end()) {
                metadata.imageHeight = heightImage->toInt64();
            }
        }
        
        auto orientation = exifData.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (orientation != exifData.end()) {
            metadata.orientation = orientation->toInt64();
        }
        
        // 7. 解析曝光参数
        auto expTime = exifData.findKey(Exiv2::ExifKey("Exif.Photo.ExposureTime"));
        if (expTime != exifData.end()) {
            metadata.exposureTime = parseRationalExiv2(expTime->toString());
        }
        
        auto fNumber = exifData.findKey(Exiv2::ExifKey("Exif.Photo.FNumber"));
        if (fNumber != exifData.end()) {
            metadata.fNumber = parseRationalExiv2(fNumber->toString());
        }
        
        auto iso = exifData.findKey(Exiv2::ExifKey("Exif.Photo.ISOSpeedRatings"));
        if (iso != exifData.end()) {
            metadata.iso = iso->toInt64();
        }
        
        auto focalLen = exifData.findKey(Exiv2::ExifKey("Exif.Photo.FocalLength"));
        if (focalLen != exifData.end()) {
            metadata.focalLength = parseRationalExiv2(focalLen->toString());
        }
        
        core::Logger::getInstance().info(
            "✅ EXIF 解析成功: " + filePath + 
            " [" + metadata.cameraMake + " " + metadata.cameraModel + "]" +
            (metadata.hasGps ? " (有GPS)" : " (无GPS)"));
        
        return metadata;
        
    } catch (const Exiv2::Error& e) {
        core::Logger::getInstance().error("❌ EXIF 解析异常: " + std::string(e.what()));
        return std::nullopt;
    } catch (const std::exception& e) {
        core::Logger::getInstance().error("❌ EXIF 解析异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

// ==================== 批量 EXIF 解析 ====================

std::vector<std::optional<ExifMetadata>> ImageProcessor::parseExifBatch(
    const std::vector<std::string>& filePaths) const {
    
    core::Logger::getInstance().info("📸 批量解析 EXIF: " + std::to_string(filePaths.size()) + " 张图片");

    // 多线程并行处理
    std::vector<std::future<std::optional<ExifMetadata>>> futures;
    futures.reserve(filePaths.size());

    for (const auto& path : filePaths) {
        futures.push_back(std::async(std::launch::async, [this, path]() {
            return parseExif(path);
        }));
    }

    std::vector<std::optional<ExifMetadata>> results;
    results.reserve(filePaths.size());
    for (auto& future : futures) {
        results.push_back(future.get());
    }

    return results;
}

// ==================== 快速GPS提取 ====================

bool ImageProcessor::extractGps(const std::string& filePath, 
                               double& lat, double& lon, double& alt) const {
    // 使用 parseExif 实现（exiv2 已经很快了）
    auto metadata = parseExif(filePath);
    if (!metadata || !metadata->hasGps) {
        return false;
    }
    
    lat = metadata->latitude;
    lon = metadata->longitude;
    alt = metadata->altitude;
    return true;
}

// ==================== 图片变换 ====================

cv::Mat ImageProcessor::resize(const cv::Mat& image, int width, int height, 
                              bool keepAspectRatio) const {
    ResizeOptions options;
    options.targetWidth = width;
    options.targetHeight = height;
    options.keepAspectRatio = keepAspectRatio;
    return resize(image, options);
}

cv::Mat ImageProcessor::resize(const cv::Mat& image, const ResizeOptions& options) const {
    if (image.empty()) return cv::Mat();

    cv::Mat resized;
    if (options.keepAspectRatio) {
        double scale = std::min(
            static_cast<double>(options.targetWidth) / image.cols,
            static_cast<double>(options.targetHeight) / image.rows
        );
        int newWidth = static_cast<int>(image.cols * scale);
        int newHeight = static_cast<int>(image.rows * scale);
        cv::resize(image, resized, cv::Size(newWidth, newHeight), 0, 0, options.interpolation);
    } else {
        cv::resize(image, resized, cv::Size(options.targetWidth, options.targetHeight), 0, 0, options.interpolation);
    }
    return resized;
}

cv::Mat ImageProcessor::crop(const cv::Mat& image, int x, int y, int width, int height) const {
    if (image.empty()) return cv::Mat();
    if (x < 0 || y < 0 || x + width > image.cols || y + height > image.rows) return cv::Mat();
    return image(cv::Rect(x, y, width, height)).clone();
}

cv::Mat ImageProcessor::rotate(const cv::Mat& image, double angle) const {
    if (image.empty()) return cv::Mat();
    
    cv::Point2f center(image.cols / 2.0f, image.rows / 2.0f);
    cv::Mat rotationMatrix = cv::getRotationMatrix2D(center, angle, 1.0);
    
    cv::Rect2f bbox = cv::RotatedRect(cv::Point2f(), image.size(), angle).boundingRect2f();
    rotationMatrix.at<double>(0, 2) += bbox.width / 2.0 - image.cols / 2.0;
    rotationMatrix.at<double>(1, 2) += bbox.height / 2.0 - image.rows / 2.0;

    cv::Mat rotated;
    cv::warpAffine(image, rotated, rotationMatrix, bbox.size());
    return rotated;
}

cv::Mat ImageProcessor::autoRotate(const cv::Mat& image, int orientation) const {
    if (image.empty() || orientation == 1) return image.clone();

    cv::Mat rotated;
    switch (orientation) {
        case 3: cv::rotate(image, rotated, cv::ROTATE_180); break;
        case 6: cv::rotate(image, rotated, cv::ROTATE_90_CLOCKWISE); break;
        case 8: cv::rotate(image, rotated, cv::ROTATE_90_COUNTERCLOCKWISE); break;
        case 2: cv::flip(image, rotated, 1); break;
        case 4: cv::flip(image, rotated, 0); break;
        case 5: cv::flip(image, rotated, 1); cv::rotate(rotated, rotated, cv::ROTATE_90_CLOCKWISE); break;
        case 7: cv::flip(image, rotated, 1); cv::rotate(rotated, rotated, cv::ROTATE_90_COUNTERCLOCKWISE); break;
        default: return image.clone();
    }
    return rotated;
}

// ==================== 格式转换 ====================

bool ImageProcessor::convert(const cv::Mat& image, const std::string& format, 
                            std::vector<uchar>& buffer) const {
    if (image.empty()) return false;
    return cv::imencode(format, image, buffer);
}

bool ImageProcessor::encodeJpeg(const cv::Mat& image, const JpegEncodeOptions& options,
                               std::vector<uchar>& buffer) const {
    if (image.empty()) return false;
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(options.quality);
    if (options.optimize) {
        params.push_back(cv::IMWRITE_JPEG_OPTIMIZE);
        params.push_back(1);
    }
    if (options.progressive) {
        params.push_back(cv::IMWRITE_JPEG_PROGRESSIVE);
        params.push_back(1);
    }
    return cv::imencode(".jpg", image, buffer, params);
}

// ==================== Base64 ====================

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static std::string base64_encode_impl(const unsigned char* bytes, size_t len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

static std::vector<unsigned char> base64_decode_impl(const std::string& encoded_string) {
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::vector<unsigned char> ret;

    while (in_len-- && ( encoded_string[in_] != '=') && (isalnum(encoded_string[in_]) || (encoded_string[in_] == '+') || (encoded_string[in_] == '/'))) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i ==4) {
            for (i = 0; i <4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (j = i; j <4; j++)
            char_array_4[j] = 0;

        for (j = 0; j <4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
    }

    return ret;
}

std::string ImageProcessor::encodeBase64(const cv::Mat& image, 
                                        const std::string& format,
                                        int quality) const {
    if (image.empty()) return "";
    
    std::vector<uchar> buffer;
    std::vector<int> params;
    if (format == ".jpg" || format == ".jpeg") {
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(quality);
    }
    
    if (!cv::imencode(format, image, buffer, params)) return "";
    return base64_encode_impl(buffer.data(), buffer.size());
}

std::optional<cv::Mat> ImageProcessor::decodeBase64(const std::string& base64) const {
    try {
        std::vector<uchar> data = base64_decode_impl(base64);
        cv::Mat image = cv::imdecode(data, cv::IMREAD_COLOR);
        if (image.empty()) return std::nullopt;
        return image;
    } catch (...) {
        return std::nullopt;
    }
}

// ==================== 压缩 ====================

bool ImageProcessor::compressToSize(const cv::Mat& image, int maxSizeKB,
                                   std::vector<uchar>& buffer) const {
    if (image.empty()) return false;
    
    size_t targetBytes = maxSizeKB * 1024;
    int minQ = 1, maxQ = 100;
    int quality = 95;
    
    // Binary search for quality
    while (minQ <= maxQ) {
        quality = (minQ + maxQ) / 2;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
        buffer.clear();
        cv::imencode(".jpg", image, buffer, params);
        
        if (buffer.size() <= targetBytes) {
            minQ = quality + 1;
        } else {
            maxQ = quality - 1;
        }
    }
    
    // Final encode with best quality found (maxQ)
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, maxQ};
    buffer.clear();
    return cv::imencode(".jpg", image, buffer, params);
}

// ==================== 工具方法 ====================

bool ImageProcessor::isImageFile(const std::string& filePath) const {
    std::string ext = filePath.substr(filePath.find_last_of('.'));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp");
}

bool ImageProcessor::getImageSize(const std::string& filePath, int& width, int& height) const {
    // cv::IMREAD_HEADER is not available in all OpenCV versions, use IMREAD_GRAYSCALE | IMREAD_IGNORE_ORIENTATION instead
    cv::Mat image = cv::imread(filePath, cv::IMREAD_GRAYSCALE | cv::IMREAD_IGNORE_ORIENTATION); 
    if (image.empty()) return false;
    width = image.cols;
    height = image.rows;
    return true;
}

}  // namespace utils
}  // namespace esdk_sophon
