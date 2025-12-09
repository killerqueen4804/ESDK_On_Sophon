/**
 * @file H264Decoder.h
 * @brief H.264 视频解码器 - 基于 FFmpeg
 * 
 * 参考 DJI Edge-SDK 的实现,使用 FFmpeg 解码 H.264 视频流。
 * 核心特性:
 * - 使用 AVCodecParserContext 自动提取 SPS/PPS
 * - 多线程解码 (4 线程)
 * - 自动处理分辨率变化
 * - 输出 cv::Mat (BGR24 格式)
 * 
 * @author 学习者
 * @date 2025-11-18
 */

#ifndef ESDK_SOPHON_VIDEO_H264_DECODER_H_
#define ESDK_SOPHON_VIDEO_H264_DECODER_H_

#include <memory>
#include <mutex>
#include <functional>
#include <opencv2/opencv.hpp>

// 前置声明 FFmpeg 类型,避免在头文件中暴露 FFmpeg 依赖
struct AVCodecContext;
struct AVCodec;
struct AVCodecParserContext;
struct SwsContext;
struct AVFrame;

namespace esdk_sophon {
namespace video {

/**
 * @brief H.264 解码器类
 * 
 * 线程安全的 H.264 解码器,支持:
 * - DJI Liveview 视频流 (缺少 SPS/PPS 头的流)
 * - 自动提取和缓存 SPS/PPS
 * - 动态分辨率调整
 * - YUV420P → RGB24 → BGR24 转换
 * 
 * 示例用法:
 * @code
 * H264Decoder decoder;
 * decoder.init(1920, 1080);
 * 
 * auto callback = [](const cv::Mat& frame) {
 *     // 处理解码后的图像
 *     cv::imshow("Video", frame);
 * };
 * 
 * decoder.decode(h264Data, dataLen, callback);
 * decoder.deinit();
 * @endcode
 */
class H264Decoder {
public:
    /**
     * @brief 解码结果回调函数类型
     * 
     * @param frame 解码后的图像 (cv::Mat, BGR24 格式)
     * 
     * @note 回调在解码线程中执行,需要注意线程安全
     */
    using DecodeCallback = std::function<void(const cv::Mat& frame)>;

    /**
     * @brief 构造函数
     */
    H264Decoder();

    /**
     * @brief 析构函数 - 自动清理资源
     */
    ~H264Decoder();

    /**
     * @brief 初始化解码器
     * 
     * @param width 视频宽度 (默认 1920)
     * @param height 视频高度 (默认 1080)
     * @return true 初始化成功
     * @return false 初始化失败 (找不到解码器/分配内存失败等)
     * 
     * @note 必须在 decode() 前调用
     * 
     * 工作流程:
     * 1. 查找 H.264 解码器 (avcodec_find_decoder)
     * 2. 创建 Parser 上下文 (av_parser_init) ⭐ 关键!
     * 3. 分配解码器上下文 (avcodec_alloc_context3)
     * 4. 配置参数 (pix_fmt, thread_count, flags2)
     * 5. 打开解码器 (avcodec_open2)
     */
    bool init(int width = 1920, int height = 1080);

    /**
     * @brief 反初始化 - 释放所有资源
     * 
     * 清理顺序 (避免内存泄漏):
     * 1. SwsContext (颜色转换)
     * 2. AVFrame (帧缓冲)
     * 3. AVCodecParserContext (Parser)
     * 4. AVCodecContext (解码器上下文)
     * 5. RGB 缓冲区
     */
    void deinit();

    /**
     * @brief 解码 H.264 数据
     * 
     * @param data H.264 数据指针 (可以是连续字节流,不需要帧边界)
     * @param length 数据长度
     * @param callback 解码成功回调 (每解码一帧就调用一次)
     * @return true 解码成功
     * @return false 解码失败
     * 
     * 📌 核心流程:
     * 1. av_parser_parse2() - 解析字节流,提取完整帧
     *    - 自动识别 NAL 单元 (SPS/PPS/Slice/SEI)
     *    - 自动提取 SPS/PPS 到 pCodecCtx->extradata
     *    - 分割帧边界
     * 
     * 2. avcodec_decode_video2() - 解码 H.264 → YUV420P
     * 
     * 3. sws_scale() - 转换 YUV → RGB24
     * 
     * 4. cvtColor() - 转换 RGB → BGR (OpenCV 格式)
     * 
     * 5. 调用 callback(cv::Mat)
     * 
     * @note 线程安全 (内部使用 mutex 保护)
     * @note 自动处理分辨率变化 (重新创建 SwsContext)
     */
    bool decode(const uint8_t* data, size_t length, DecodeCallback callback);

    /**
     * @brief 获取当前解码宽度
     * @return int 宽度 (像素)
     */
    int getWidth() const { return decodeWidth_; }

    /**
     * @brief 获取当前解码高度
     * @return int 高度 (像素)
     */
    int getHeight() const { return decodeHeight_; }

    /**
     * @brief 检查是否已初始化
     * @return true 已初始化
     * @return false 未初始化
     */
    bool isInitialized() const { return initialized_; }

private:
    // 📌 FFmpeg 核心组件
    AVCodecContext* pCodecCtx_;        ///< 解码器上下文
    AVCodec* pCodec_;                  ///< H.264 解码器
    AVCodecParserContext* pParser_;    ///< Parser 上下文 ⭐ 关键!提取 SPS/PPS
    SwsContext* pSwsCtx_;              ///< 颜色空间转换上下文 (YUV → RGB)

    AVFrame* pFrameYUV_;               ///< YUV 帧缓冲
    AVFrame* pFrameRGB_;               ///< RGB 帧缓冲
    uint8_t* pRgbBuffer_;              ///< RGB 数据缓冲区
    size_t rgbBufferSize_;             ///< RGB 缓冲区大小

    // 状态信息
    int decodeWidth_;                  ///< 当前解码宽度
    int decodeHeight_;                 ///< 当前解码高度
    bool initialized_;                 ///< 是否已初始化

    // 线程安全
    mutable std::mutex decodeMutex_;   ///< 解码互斥锁

    /**
     * @brief 处理分辨率变化
     * 
     * 当检测到视频分辨率变化时:
     * 1. 释放旧的 SwsContext 和 RGB 缓冲区
     * 2. 创建新的 SwsContext (新分辨率)
     * 3. 分配新的 RGB 缓冲区
     * 
     * @param newWidth 新宽度
     * @param newHeight 新高度
     */
    void handleResolutionChange(int newWidth, int newHeight);
};

}  // namespace video
}  // namespace esdk_sophon

#endif  // ESDK_SOPHON_VIDEO_H264_DECODER_H_
