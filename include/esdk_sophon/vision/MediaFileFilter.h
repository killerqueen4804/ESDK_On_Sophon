/**
 * @file MediaFileFilter.h
 * @brief 媒体文件过滤器 - 简化实用方案
 * @details 解决实际问题:
 *          1. 文件名不包含taskID,无法通过名称关联
 *          2. 每张图片本身是唯一的,不需要去重
 *          3. 核心问题:如何区分"本次任务的图片"和"旧任务的图片"
 * 
 * 简化策略:
 *          - 时间戳过滤 (核心!唯一可靠的手段)
 *          - 状态标志过滤 (任务开始后才接收文件)
 *          - 可选:文件序号范围判断 (辅助策略)
 * 
 * @author GitHub Copilot
 * @date 2025-10-28
 */

#ifndef ESDK_SOPHON_VISION_MEDIA_FILE_FILTER_H_
#define ESDK_SOPHON_VISION_MEDIA_FILE_FILTER_H_

#include <string>
#include <chrono>
#include <mutex>
#include <set>
#include <functional>
#include <exiv2/exiv2.hpp>
#include <sys/stat.h>

namespace esdk_sophon {
namespace vision {

/**
 * @brief 文件过滤决策
 */
enum class FilterDecision {
    kAccept,        ///< 接受此文件,应该处理
    kRejectOld,     ///< 拒绝,文件太旧 (任务开始前拍摄)
    kRejectDisabled,///< 拒绝,过滤器未启用
    kRejectError    ///< 拒绝,提取时间戳失败
};

/**
 * @brief 过滤结果
 */
struct FilterResult {
    FilterDecision decision;
    std::string reason;         ///< 决策原因
    int64_t fileTimestamp;      ///< 文件时间戳 (毫秒)
    int64_t taskStartTimestamp; ///< 任务开始时间戳 (毫秒)
    
    bool shouldProcess() const {
        return decision == FilterDecision::kAccept;
    }
};

/**
 * @brief 媒体文件过滤器
 * @details 核心功能:判断文件是否属于当前任务
 * 
 * 核心原理:
 * 1. **时间戳过滤** (主要策略):
 *    - 记录任务开始时间 (taskStartTime)
 *    - 从图片EXIF提取拍摄时间 (photoTimestamp)
 *    - 只处理 photoTimestamp >= taskStartTime 的图片
 * 
 * 2. **状态标志过滤** (辅助策略):
 *    - 只有调用enable()后才接收文件
 *    - 任务结束调用disable()后拒绝所有文件
 * 
 * 3. **可选:文件序号范围** (高级策略,如果文件名规律可用):
 *    - 例如: DJI_20251028_001.JPG, DJI_20251028_002.JPG
 *    - 可以记录首张图片序号,只处理后续连续序号
 * 
 * 使用示例:
 * @code
 * MediaFileFilter filter;
 * 
 * // 任务开始时启用过滤器
 * filter.enable();
 * 
 * // 媒体文件回调
 * ErrorCode onMediaFileUpdate(const MediaFile& file) {
 *     auto result = filter.shouldProcess(file.file_path);
 *     
 *     if (result.shouldProcess()) {
 *         // 处理此文件
 *         processFile(file);
 *     } else {
 *         INFO("Skip file: %s (reason: %s)", 
 *              file.file_name.c_str(), result.reason.c_str());
 *     }
 *     
 *     return kOk;
 * }
 * 
 * // 任务结束时禁用过滤器
 * filter.disable();
 * @endcode
 */
class MediaFileFilter {
public:
    MediaFileFilter() 
        : enabled_(false),
          taskStartTimestamp_(0),
          filesAccepted_(0),
          filesRejected_(0) {}
    
    /**
     * @brief 启用过滤器
     * @details 记录当前时间作为任务开始时间
     *          后续只接收此时间之后拍摄的图片
     */
    void enable() {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = true;
        
        // 记录任务开始时间 (当前时间)
        auto now = std::chrono::system_clock::now();
        taskStartTimestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        // 留出缓冲时间 (往前推5秒,避免误判边界情况)
        // 例如:任务启动时间 14:30:00,图片拍摄时间 14:29:58
        //      由于时钟同步误差,这张图可能实际是任务开始后拍的
        taskStartTimestamp_ -= 5000;  // 减5秒缓冲
        
        filesAccepted_ = 0;
        filesRejected_ = 0;
        acceptedFiles_.clear();
        
        INFO("MediaFileFilter enabled (task start: %lld)", taskStartTimestamp_);
    }
    
    /**
     * @brief 禁用过滤器
     * @details 任务结束后调用,后续所有文件都会被拒绝
     */
    void disable() {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = false;
        INFO("MediaFileFilter disabled (accepted: %d, rejected: %d)", 
             filesAccepted_, filesRejected_);
    }
    
    /**
     * @brief 判断文件是否应该处理
     * @param filePath 文件完整路径
     * @return 过滤结果
     */
    FilterResult shouldProcess(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        FilterResult result;
        
        // 策略1: 检查过滤器是否启用
        if (!enabled_) {
            result.decision = FilterDecision::kRejectDisabled;
            result.reason = "Filter disabled (task not started or already ended)";
            filesRejected_++;
            return result;
        }
        
        // 策略2: 提取文件时间戳
        int64_t fileTimestamp = extractTimestamp(filePath);
        if (fileTimestamp == 0) {
            result.decision = FilterDecision::kRejectError;
            result.reason = "Failed to extract timestamp";
            result.fileTimestamp = 0;
            result.taskStartTimestamp = taskStartTimestamp_;
            filesRejected_++;
            return result;
        }
        
        result.fileTimestamp = fileTimestamp;
        result.taskStartTimestamp = taskStartTimestamp_;
        
        // 策略3: 时间戳比对 (核心!)
        if (fileTimestamp < taskStartTimestamp_) {
            result.decision = FilterDecision::kRejectOld;
            
            int64_t diff = (taskStartTimestamp_ - fileTimestamp) / 1000;  // 转为秒
            result.reason = "File too old (taken " + std::to_string(diff) + 
                          "s before task start)";
            filesRejected_++;
            return result;
        }
        
        // 通过所有检查,接受此文件
        result.decision = FilterDecision::kAccept;
        result.reason = "Accepted";
        filesAccepted_++;
        acceptedFiles_.insert(filePath);
        
        return result;
    }
    
    /**
     * @brief 获取统计信息
     */
    struct Statistics {
        bool enabled;
        int64_t taskStartTimestamp;
        int filesAccepted;
        int filesRejected;
    };
    
    Statistics getStatistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Statistics stats;
        stats.enabled = enabled_;
        stats.taskStartTimestamp = taskStartTimestamp_;
        stats.filesAccepted = filesAccepted_;
        stats.filesRejected = filesRejected_;
        return stats;
    }

private:
    /**
     * @brief 提取文件时间戳
     * @param filePath 文件路径
     * @return 时间戳 (毫秒),失败返回0
     * 
     * @details 优先级:
     *          1. EXIF DateTimeOriginal (拍摄时间,最准确)
     *          2. EXIF DateTime (修改时间)
     *          3. 文件系统修改时间 (兜底方案)
     */
    int64_t extractTimestamp(const std::string& filePath) {
        // 方法1: 尝试读取EXIF
        try {
            auto image = Exiv2::ImageFactory::open(filePath);
            if (image.get() != nullptr) {
                image->readMetadata();
                auto& exifData = image->exifData();
                
                // 优先使用原始拍摄时间
                auto iter = exifData.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
                if (iter != exifData.end()) {
                    std::string timeStr = iter->toString();
                    return parseExifTime(timeStr);
                }
                
                // 其次使用修改时间
                iter = exifData.findKey(Exiv2::ExifKey("Exif.Image.DateTime"));
                if (iter != exifData.end()) {
                    std::string timeStr = iter->toString();
                    return parseExifTime(timeStr);
                }
            }
        } catch (const std::exception& e) {
            WARN("EXIF extraction failed: %s, fallback to file mtime", e.what());
        }
        
        // 方法2: 使用文件修改时间 (兜底)
        struct stat st;
        if (stat(filePath.c_str(), &st) == 0) {
            return static_cast<int64_t>(st.st_mtime) * 1000;  // 转为毫秒
        }
        
        ERROR("Failed to extract timestamp from: %s", filePath.c_str());
        return 0;
    }
    
    /**
     * @brief 解析EXIF时间字符串
     * @param timeStr EXIF时间格式: "2025:10:28 14:30:15"
     * @return 时间戳 (毫秒)
     */
    int64_t parseExifTime(const std::string& timeStr) {
        // EXIF格式: "YYYY:MM:DD HH:MM:SS"
        struct tm tm_info = {0};
        
        if (sscanf(timeStr.c_str(), "%d:%d:%d %d:%d:%d",
                   &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
                   &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec) == 6) {
            
            tm_info.tm_year -= 1900;  // tm_year是从1900开始
            tm_info.tm_mon -= 1;      // tm_mon是0-11
            
            time_t t = mktime(&tm_info);
            if (t != -1) {
                return static_cast<int64_t>(t) * 1000;  // 转为毫秒
            }
        }
        
        WARN("Failed to parse EXIF time: %s", timeStr.c_str());
        return 0;
    }
    
    // 状态
    bool enabled_;
    int64_t taskStartTimestamp_;  ///< 任务开始时间戳 (毫秒)
    
    // 统计
    int filesAccepted_;
    int filesRejected_;
    std::set<std::string> acceptedFiles_;  ///< 已接受的文件路径 (用于调试)
    
    // 线程安全
    mutable std::mutex mutex_;
};

}  // namespace vision
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VISION_MEDIA_FILE_FILTER_H_
