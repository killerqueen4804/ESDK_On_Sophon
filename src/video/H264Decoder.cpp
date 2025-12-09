/**
 * @file H264Decoder.cpp
 * @brief H.264 视频解码器实现
 * 
 * 参考 DJI Edge-SDK 的 FFmpegStreamDecoder 实现。
 * 核心技术点:
 * - AVCodecParserContext: 自动处理 DJI H.264 流缺少 SPS/PPS 的问题
 * - av_parser_parse2(): 解析连续字节流,提取完整帧
 * - 多线程解码: 4 线程并行
 * 
 * @author 学习者
 * @date 2025-11-18
 */

#include "esdk_sophon/video/H264Decoder.h"
#include "esdk_sophon/core/Logger.h"

// FFmpeg C 库头文件
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>  // av_image_* 函数
}

namespace esdk_sophon {
namespace video {

/**
 * @brief 构造函数 - 初始化所有指针为 nullptr
 */
H264Decoder::H264Decoder()
    : pCodecCtx_(nullptr),
      pCodec_(nullptr),
      pParser_(nullptr),
      pSwsCtx_(nullptr),
      pFrameYUV_(nullptr),
      pFrameRGB_(nullptr),
      pRgbBuffer_(nullptr),
      rgbBufferSize_(0),
      decodeWidth_(0),
      decodeHeight_(0),
      initialized_(false) {
}

/**
 * @brief 析构函数 - 自动清理资源
 */
H264Decoder::~H264Decoder() {
    deinit();
}

/**
 * @brief 初始化解码器
 */
bool H264Decoder::init(int width, int height) {
    std::lock_guard<std::mutex> lock(decodeMutex_);
    
    if (initialized_) {
        core::Logger::getInstance().warning("[H264Decoder] 已经初始化,跳过");
        return true;
    }

    auto& logger = core::Logger::getInstance();
    logger.info("[H264Decoder] 开始初始化解码器...");
    logger.info("  参数: width=" + std::to_string(width) + 
                ", height=" + std::to_string(height));

    // 📌 步骤1: 注册所有编解码器 (FFmpeg 旧版 API,新版不需要)
    #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    avcodec_register_all();
    #endif

    // 📌 步骤2: 查找 H.264 解码器
    pCodec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!pCodec_) {
        logger.error("[H264Decoder] ❌ 找不到 H.264 解码器!");
        return false;
    }
    logger.info("  ✅ 找到 H.264 解码器: " + std::string(pCodec_->name));

    // 📌 步骤3: 创建 Parser ⭐⭐⭐ 关键!!!
    // Parser 的作用:
    // 1. 解析连续字节流,分割出完整的 NAL 单元
    // 2. 自动识别 SPS/PPS/Slice/SEI 等类型
    // 3. 提取 SPS/PPS 并存储到 pCodecCtx->extradata
    // 4. 这就是为什么 Edge-SDK 可以处理 DJI 缺少 SPS/PPS 的视频流!
    pParser_ = av_parser_init(pCodec_->id);
    if (!pParser_) {
        logger.error("[H264Decoder] ❌ 创建 Parser 失败!");
        return false;
    }
    logger.info("  ✅ 创建 Parser 成功");

    // 📌 步骤4: 分配解码器上下文
    pCodecCtx_ = avcodec_alloc_context3(pCodec_);
    if (!pCodecCtx_) {
        logger.error("[H264Decoder] ❌ 分配解码器上下文失败!");
        return false;
    }

    // 📌 步骤5: 配置解码器参数
    pCodecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;  // 输出格式: YUV420P
    pCodecCtx_->width = width;                  // 初始宽度
    pCodecCtx_->height = height;                // 初始高度
    
    // 📌 重要标志: SHOW_ALL - 显示所有帧 (包括损坏的帧)
    // 对于 DJI 视频流很重要,因为可能有些帧不完整
    pCodecCtx_->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
    
    // 📌 多线程解码: 4 线程并行
    pCodecCtx_->thread_count = 4;
    
    logger.info("  配置: pix_fmt=YUV420P, thread_count=4, flags2=SHOW_ALL");

    // 📌 步骤6: 打开解码器
    int ret = avcodec_open2(pCodecCtx_, pCodec_, nullptr);
    if (ret < 0) {
        char errBuf[128];
        av_strerror(ret, errBuf, sizeof(errBuf));
        logger.error("[H264Decoder] ❌ 打开解码器失败: " + 
                     std::string(errBuf) + " (ret=" + std::to_string(ret) + ")");
        return false;
    }
    logger.info("  ✅ 打开解码器成功");

    // 📌 步骤7: 分配帧缓冲区
    pFrameYUV_ = av_frame_alloc();
    if (!pFrameYUV_) {
        logger.error("[H264Decoder] ❌ 分配 YUV 帧缓冲失败!");
        return false;
    }

    pFrameRGB_ = av_frame_alloc();
    if (!pFrameRGB_) {
        logger.error("[H264Decoder] ❌ 分配 RGB 帧缓冲失败!");
        return false;
    }

    logger.info("  ✅ 分配帧缓冲成功");

    // SwsContext 和 RGB 缓冲区延迟创建 (等真正解码时根据实际分辨率创建)
    pSwsCtx_ = nullptr;
    pRgbBuffer_ = nullptr;
    rgbBufferSize_ = 0;

    decodeWidth_ = 0;
    decodeHeight_ = 0;
    initialized_ = true;

    logger.info("[H264Decoder] ✅ 初始化完成!");
    return true;
}

/**
 * @brief 反初始化 - 释放资源
 */
void H264Decoder::deinit() {
    std::lock_guard<std::mutex> lock(decodeMutex_);

    if (!initialized_) {
        return;
    }

    auto& logger = core::Logger::getInstance();
    logger.info("[H264Decoder] 开始清理资源...");

    // 📌 释放顺序很重要!避免 double-free 和悬空指针

    // 1. 释放颜色转换上下文
    if (pSwsCtx_) {
        sws_freeContext(pSwsCtx_);
        pSwsCtx_ = nullptr;
        logger.debug("  释放 SwsContext");
    }

    // 2. 释放帧缓冲
    if (pFrameYUV_) {
        av_frame_free(&pFrameYUV_);
        pFrameYUV_ = nullptr;
        logger.debug("  释放 YUV 帧缓冲");
    }

    if (pFrameRGB_) {
        av_frame_free(&pFrameRGB_);
        pFrameRGB_ = nullptr;
        logger.debug("  释放 RGB 帧缓冲");
    }

    // 3. 释放 RGB 数据缓冲区
    if (pRgbBuffer_) {
        av_free(pRgbBuffer_);
        pRgbBuffer_ = nullptr;
        rgbBufferSize_ = 0;
        logger.debug("  释放 RGB 数据缓冲区");
    }

    // 4. 关闭并释放 Parser
    if (pParser_) {
        av_parser_close(pParser_);
        pParser_ = nullptr;
        logger.debug("  关闭 Parser");
    }

    // 5. 关闭并释放解码器上下文
    if (pCodecCtx_) {
        avcodec_close(pCodecCtx_);
        av_free(pCodecCtx_);
        pCodecCtx_ = nullptr;
        logger.debug("  关闭解码器上下文");
    }

    // 6. 解码器本身不需要手动释放 (由 FFmpeg 管理)
    pCodec_ = nullptr;

    initialized_ = false;
    logger.info("[H264Decoder] ✅ 清理完成");
}

/**
 * @brief 处理分辨率变化
 */
void H264Decoder::handleResolutionChange(int newWidth, int newHeight) {
    auto& logger = core::Logger::getInstance();
    
    logger.info("[H264Decoder] 🔄 检测到分辨率变化:");
    logger.info("  旧: " + std::to_string(decodeWidth_) + "x" + 
                std::to_string(decodeHeight_));
    logger.info("  新: " + std::to_string(newWidth) + "x" + 
                std::to_string(newHeight));

    decodeWidth_ = newWidth;
    decodeHeight_ = newHeight;

    // 释放旧资源
    if (pSwsCtx_) {
        sws_freeContext(pSwsCtx_);
        pSwsCtx_ = nullptr;
    }

    if (pRgbBuffer_) {
        av_free(pRgbBuffer_);
        pRgbBuffer_ = nullptr;
        rgbBufferSize_ = 0;
    }

    logger.info("  ✅ 旧资源已释放,等待重新创建");
}

/**
 * @brief 解码 H.264 数据 ⭐⭐⭐ 核心函数!
 * 
 * 参考 Edge-SDK 的 FFmpegStreamDecoder::Decode() 实现
 */
bool H264Decoder::decode(const uint8_t* data, size_t length, 
                         DecodeCallback callback) {
    if (!initialized_) {
        core::Logger::getInstance().error("[H264Decoder] ❌ 未初始化!");
        return false;
    }

    if (!data || length == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(decodeMutex_);

    auto& logger = core::Logger::getInstance();
    
    // 📌 开始解析和解码
    const uint8_t* pData = data;
    int remainingLen = static_cast<int>(length);
    int processedLen = 0;

    AVPacket pkt;
    av_init_packet(&pkt);

    // 📌 循环处理所有数据 (一次 decode 调用可能包含多帧)
    while (remainingLen > 0) {
        // 检查上下文有效性
        if (!pParser_ || !pCodecCtx_) {
            logger.error("[H264Decoder] ❌ Parser 或解码器上下文为空!");
            break;
        }

        // 📌⭐⭐⭐ 核心 API: av_parser_parse2()
        // 
        // 功能:
        // 1. 解析连续字节流,提取完整的 NAL 单元
        // 2. 自动识别 NAL 类型:
        //    - Type 7: SPS (Sequence Parameter Set)
        //    - Type 8: PPS (Picture Parameter Set)
        //    - Type 1: Slice (P/B 帧)
        //    - Type 5: IDR Slice (I 帧)
        //    - Type 6: SEI (补充信息)
        // 3. 自动提取 SPS/PPS 到 pCodecCtx->extradata
        // 4. 输出完整帧到 pkt.data 和 pkt.size
        //
        // 这就是为什么 DJI 视频流虽然缺少 SPS/PPS 头,
        // 但 FFmpeg 仍然可以正常解码!
        processedLen = av_parser_parse2(
            pParser_,           // Parser 上下文
            pCodecCtx_,         // 解码器上下文 (Parser 会填充 extradata)
            &pkt.data,          // 输出: 完整帧数据指针
            &pkt.size,          // 输出: 完整帧大小
            pData,              // 输入: 原始字节流
            remainingLen,       // 输入: 剩余字节数
            AV_NOPTS_VALUE,     // PTS (我们不关心时间戳)
            AV_NOPTS_VALUE,     // DTS
            AV_NOPTS_VALUE      // POS
        );

        // 更新指针和长度
        remainingLen -= processedLen;
        pData += processedLen;

        // 📌 如果 Parser 提取到了完整帧 (pkt.size > 0)
        if (pkt.size > 0) {
            int gotPicture = 0;

            // 📌 解码 H.264 → YUV420P
            // FFmpeg 4.x 仍支持 avcodec_decode_video2,虽然标记为 deprecated
            // 但为了兼容性和简单性,我们继续使用
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            avcodec_decode_video2(pCodecCtx_, pFrameYUV_, &gotPicture, &pkt);
            #pragma GCC diagnostic pop

            // 📌 如果成功解码出一帧图像
            if (gotPicture) {
                // 检查分辨率是否变化
                if (pFrameYUV_->width != decodeWidth_ || 
                    pFrameYUV_->height != decodeHeight_) {
                    handleResolutionChange(pFrameYUV_->width, pFrameYUV_->height);
                }

                int w = decodeWidth_;
                int h = decodeHeight_;

                // 📌 延迟创建 SwsContext (颜色空间转换器)
                if (!pSwsCtx_) {
                    pSwsCtx_ = sws_getContext(
                        w, h, pCodecCtx_->pix_fmt,  // 源: YUV420P
                        w, h, AV_PIX_FMT_BGR24,      // 目标: BGR24 (OpenCV 格式)
                        SWS_BILINEAR,                // 缩放算法: 双线性插值
                        nullptr, nullptr, nullptr
                    );
                    logger.info("[H264Decoder] 创建 SwsContext: " + 
                               std::to_string(w) + "x" + std::to_string(h));
                }

                // 📌 延迟分配 RGB 缓冲区
                if (!pRgbBuffer_) {
                    #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 0, 0)
                    rgbBufferSize_ = avpicture_get_size(AV_PIX_FMT_BGR24, w, h);
                    #else
                    rgbBufferSize_ = av_image_get_buffer_size(AV_PIX_FMT_BGR24, w, h, 1);
                    #endif
                    
                    pRgbBuffer_ = static_cast<uint8_t*>(av_malloc(rgbBufferSize_));
                    
                    #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 0, 0)
                    avpicture_fill(reinterpret_cast<AVPicture*>(pFrameRGB_), 
                                  pRgbBuffer_, AV_PIX_FMT_BGR24, w, h);
                    #else
                    av_image_fill_arrays(pFrameRGB_->data, pFrameRGB_->linesize,
                                        pRgbBuffer_, AV_PIX_FMT_BGR24, w, h, 1);
                    #endif
                    
                    logger.info("[H264Decoder] 分配 BGR 缓冲区: " + 
                               std::to_string(rgbBufferSize_) + " 字节");
                }

                // 📌 YUV → BGR 转换
                if (pSwsCtx_ && pRgbBuffer_) {
                    sws_scale(
                        pSwsCtx_,
                        const_cast<const uint8_t**>(pFrameYUV_->data),
                        pFrameYUV_->linesize,
                        0,
                        pFrameYUV_->height,
                        pFrameRGB_->data,
                        pFrameRGB_->linesize
                    );

                    pFrameRGB_->width = w;
                    pFrameRGB_->height = h;

                    // 📌 创建 cv::Mat (BGR24, 引用 pFrameRGB 数据,不拷贝)
                    if (pFrameRGB_->data[0]) {
                        // 直接创建 BGR Mat，无需再次转换
                        cv::Mat bgrMat(h, w, CV_8UC3, pFrameRGB_->data[0], pFrameRGB_->linesize[0]);
                        
                        // 📌 调用回调函数
                        if (callback) {
                            callback(bgrMat);
                        }
                    }
                }
            }
        }

        // 📌 释放 AVPacket (FFmpeg 旧版 API)
        #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 0, 0)
        av_free_packet(&pkt);
        #else
        av_packet_unref(&pkt);
        #endif
    }

    return true;
}

}  // namespace video
}  // namespace esdk_sophon
