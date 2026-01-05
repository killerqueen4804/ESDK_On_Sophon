/**
 * @file Sam2Segmentor.cpp
 * @brief SAM2 (Segment Anything Model 2) 分割器实现
 * 
 * 基于 BMRuntime C API 实现，参考 sophon-demo SAM2 Python 版本
 * 
 * 模型信息:
 * - Encoder: 输入 images[1,3,1024,1024], 输出 image_embed[1,256,64,64] + high_res_feats
 * - Decoder: 输入 embeddings + point_coords + point_labels, 输出 masks + iou_predictions
 * 
 * Prompt 类型:
 * - Box Prompt: 使用检测框的左上角和右下角作为两个点
 *   - point_coords: [x1, y1, x2, y2]
 *   - point_labels: [2, 3] (2=左上角, 3=右下角)
 * 
 * @author ESDK Sophon Team
 * @date 2025-12-15 (更新为 Box Prompt)
 */

#include "esdk_sophon/vision/segmentation/Sam2Segmentor.h"
#include "esdk_sophon/core/Logger.h"
#include <bmruntime_interface.h>
#include <bmlib_runtime.h>
#include <bmdef.h>  // 提供 bm_net_info_t, bm_stage_info_t 等结构体定义
#include <opencv2/opencv.hpp>
#include <cstring>
#include <vector>
#include <algorithm>

namespace esdk_sophon {
namespace vision {

// SAM2 模型参数
const int SAM2_INPUT_SIZE = 1024;
const int SAM2_MASK_SIZE = 256;  // Decoder 输出掩码尺寸
const int SCALE_FACTOR = 4;      // 缩放因子

// ImageNet 归一化参数
const float PIXEL_MEAN[3] = {0.485f, 0.456f, 0.406f};
const float PIXEL_STD[3] = {0.229f, 0.224f, 0.225f};

//=============================================================================
// Sam2Segmentor::Impl - 实现类 (Pimpl 模式)
//=============================================================================
class Sam2Segmentor::Impl {
public:
    Impl() : bmHandle(nullptr), encoderNet(nullptr), decoderNet(nullptr),
             encoderInfo(nullptr), decoderInfo(nullptr) {}
    
    ~Impl() {
        // 释放网络资源
        if (encoderNet) {
            bmrt_destroy(encoderNet);
            encoderNet = nullptr;
        }
        if (decoderNet) {
            bmrt_destroy(decoderNet);
            decoderNet = nullptr;
        }
        // 释放 BMHandle
        if (bmHandle) {
            bm_dev_free(bmHandle);
            bmHandle = nullptr;
        }
    }

    /**
     * @brief 初始化 SAM2 模型
     * @param encoderPath Encoder BModel 路径
     * @param decoderPath Decoder BModel 路径
     */
    bool init(const std::string& encoderPath, const std::string& decoderPath) {
        auto& logger = core::Logger::getInstance();

        // 1. 初始化 BMLib (设备 ID = 0)
        if (bm_dev_request(&bmHandle, 0) != BM_SUCCESS) {
            logger.error("SAM2: 初始化 BM 设备失败");
            return false;
        }

        // 2. 加载 Encoder 模型
        encoderNet = bmrt_create(bmHandle);
        if (!bmrt_load_bmodel(encoderNet, encoderPath.c_str())) {
            logger.error("SAM2: 加载 Encoder 模型失败: " + encoderPath);
            return false;
        }

        // 3. 加载 Decoder 模型
        decoderNet = bmrt_create(bmHandle);
        if (!bmrt_load_bmodel(decoderNet, decoderPath.c_str())) {
            logger.error("SAM2: 加载 Decoder 模型失败: " + decoderPath);
            return false;
        }

        // 4. 获取网络名称
        const char** encoderNames = nullptr;
        bmrt_get_network_names(encoderNet, &encoderNames);
        int encoderCount = bmrt_get_network_number(encoderNet);
        if (encoderCount == 0) {
            logger.error("SAM2: Encoder bmodel 中未找到网络");
            return false;
        }
        encoderNetName = encoderNames[0];

        const char** decoderNames = nullptr;
        bmrt_get_network_names(decoderNet, &decoderNames);
        int decoderCount = bmrt_get_network_number(decoderNet);
        if (decoderCount == 0) {
            logger.error("SAM2: Decoder bmodel 中未找到网络");
            return false;
        }
        decoderNetName = decoderNames[0];

        // 5. 获取网络信息 (返回值是指针)
        encoderInfo = bmrt_get_network_info(encoderNet, encoderNetName.c_str());
        decoderInfo = bmrt_get_network_info(decoderNet, decoderNetName.c_str());
        
        if (!encoderInfo || !decoderInfo) {
            logger.error("SAM2: 获取网络信息失败");
            return false;
        }

        logger.info("SAM2: 初始化成功");
        logger.info("  - Encoder: " + encoderNetName);
        logger.info("  - Decoder: " + decoderNetName);
        
        // 打印 Decoder 期望的输入 shapes
        logger.debug("SAM2 Decoder 期望的输入 shapes:");
        for (int i = 0; i < decoderInfo->input_num; ++i) {
            std::string shapeStr = "[";
            auto& shape = decoderInfo->stages[0].input_shapes[i];
            for (int d = 0; d < shape.num_dims; ++d) {
                if (d > 0) shapeStr += ",";
                shapeStr += std::to_string(shape.dims[d]);
            }
            shapeStr += "]";
            logger.debug("  Input " + std::to_string(i) + ": " + shapeStr);
        }

        return true;
    }

    /**
     * @brief 图像预处理: Resize + Normalize
     * @param frame 输入图像
     * @param origH 返回原始高度
     * @param origW 返回原始宽度
     * @return 预处理后的图像 [1024, 1024, 3] Float32
     */
    cv::Mat preprocess(const cv::Mat& frame, int& origH, int& origW) {
        origH = frame.rows;
        origW = frame.cols;

        // 1. BGR -> RGB
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

        // 2. 计算缩放比例 (长边缩放到 1024)
        float scale = static_cast<float>(SAM2_INPUT_SIZE) / std::max(origH, origW);
        int newH = static_cast<int>(origH * scale);
        int newW = static_cast<int>(origW * scale);

        // 3. Resize
        cv::Mat resized;
        cv::resize(rgb, resized, cv::Size(newW, newH));

        // 4. Padding 到 1024x1024 (右下角填充0)
        cv::Mat padded = cv::Mat::zeros(SAM2_INPUT_SIZE, SAM2_INPUT_SIZE, CV_8UC3);
        resized.copyTo(padded(cv::Rect(0, 0, newW, newH)));

        // 5. 归一化: (x/255.0 - mean) / std
        padded.convertTo(padded, CV_32FC3, 1.0 / 255.0);

        std::vector<cv::Mat> channels(3);
        cv::split(padded, channels);
        for (int i = 0; i < 3; ++i) {
            channels[i] = (channels[i] - PIXEL_MEAN[i]) / PIXEL_STD[i];
        }

        cv::Mat preprocessed;
        cv::merge(channels, preprocessed);

        return preprocessed;
    }

    /**
     * @brief 运行 Encoder 得到 Image Embeddings
     * @param frame 预处理后的图像
     * @param outputs 返回的输出 Tensors (image_embed, high_res_feats_0/1)
     */
    bool runEncoder(const cv::Mat& preprocessed, std::vector<bm_tensor_t>& outputs) {
        auto& logger = core::Logger::getInstance();

        // 1. 准备输入 Tensor
        bm_tensor_t inputTensor;
        inputTensor.dtype = BM_FLOAT32;
        inputTensor.shape = encoderInfo->stages[0].input_shapes[0];  // [1, 3, 1024, 1024]
        inputTensor.st_mode = BM_STORE_1N;

        if (bm_malloc_device_byte(bmHandle, &inputTensor.device_mem,
                                  bmrt_tensor_bytesize(&inputTensor)) != BM_SUCCESS) {
            logger.error("SAM2 Encoder: 分配输入内存失败");
            return false;
        }

        // 2. 拷贝数据到设备 (HWC -> CHW)
        std::vector<float> inputData(SAM2_INPUT_SIZE * SAM2_INPUT_SIZE * 3);
        int idx = 0;
        for (int c = 0; c < 3; ++c) {
            for (int h = 0; h < SAM2_INPUT_SIZE; ++h) {
                for (int w = 0; w < SAM2_INPUT_SIZE; ++w) {
                    inputData[idx++] = preprocessed.at<cv::Vec3f>(h, w)[c];
                }
            }
        }

        if (bm_memcpy_s2d(bmHandle, inputTensor.device_mem, inputData.data()) != BM_SUCCESS) {
            logger.error("SAM2 Encoder: 数据拷贝到设备失败");
            bm_free_device(bmHandle, inputTensor.device_mem);
            return false;
        }

        // 3. 准备输出 Tensors (使用 output_num)
        outputs.resize(encoderInfo->output_num);
        for (int i = 0; i < encoderInfo->output_num; ++i) {
            outputs[i].dtype = BM_FLOAT32;
            outputs[i].shape = encoderInfo->stages[0].output_shapes[i];
            outputs[i].st_mode = BM_STORE_1N;
            if (bm_malloc_device_byte(bmHandle, &outputs[i].device_mem,
                                      bmrt_tensor_bytesize(&outputs[i])) != BM_SUCCESS) {
                logger.error("SAM2 Encoder: 分配输出内存失败");
                bm_free_device(bmHandle, inputTensor.device_mem);
                return false;
            }
        }

        // 4. 执行推理
        bool success = bmrt_launch_tensor_ex(encoderNet, encoderNetName.c_str(),
                                             &inputTensor, 1,
                                             outputs.data(), outputs.size(),
                                             true, false);
        if (!success) {
            logger.error("SAM2 Encoder: 推理失败");
            bm_free_device(bmHandle, inputTensor.device_mem);
            for (auto& t : outputs) bm_free_device(bmHandle, t.device_mem);
            return false;
        }

        // 5. 等待完成
        bm_thread_sync(bmHandle);

        // 打印 Encoder 输出的 shape
        logger.debug("SAM2 Encoder 输出张量 shapes:");
        for (int i = 0; i < encoderInfo->output_num; ++i) {
            std::string shapeStr = "[";
            for (int d = 0; d < outputs[i].shape.num_dims; ++d) {
                if (d > 0) shapeStr += ",";
                shapeStr += std::to_string(outputs[i].shape.dims[d]);
            }
            shapeStr += "]";
            logger.debug("  Output " + std::to_string(i) + ": " + shapeStr);
        }

        // 释放输入内存
        bm_free_device(bmHandle, inputTensor.device_mem);

        return true;
    }

    /**
     * @brief 运行 Decoder 得到单个目标的 Mask
     * @param embeddings Encoder 输出的 Embeddings
     * @param box 检测框
     * @param origH 原始图像高度
     * @param origW 原始图像宽度
     * @param mask 返回的掩码
     * @param iou 返回的 IOU 分数
     */
    bool runDecoder(const std::vector<bm_tensor_t>& embeddings,
                    const DetectionBox& box,
                    int origH, int origW,
                    cv::Mat& mask, float& iou) {
        auto& logger = core::Logger::getInstance();

        // 1. 计算 Box Prompt: 左上角 + 右下角 (映射到 1024x1024)
        // SAM2 的 Box Prompt 就是用左上角和右下角两个点
        float scale = static_cast<float>(SAM2_INPUT_SIZE) / std::max(origH, origW);
        float x1 = box.x * scale;                        // 左上角 X
        float y1 = box.y * scale;                        // 左上角 Y
        float x2 = (box.x + box.width) * scale;          // 右下角 X
        float y2 = (box.y + box.height) * scale;         // 右下角 Y

        logger.debug("SAM2 Decoder: Box [" + std::to_string(box.x) + "," + std::to_string(box.y) + 
                     "," + std::to_string(box.width) + "x" + std::to_string(box.height) + 
                     "] → Box Prompt [(" + std::to_string(x1) + "," + std::to_string(y1) + "), " +
                     "(" + std::to_string(x2) + "," + std::to_string(y2) + ")] " +
                     "(scale=" + std::to_string(scale) + ")");

        // 2. 准备 Decoder 输入 Tensors (7个输入)
        std::vector<bm_tensor_t> inputs(7);

        // ⚠️ 注意: Encoder 输出顺序 != Decoder 输入顺序,需要重新映射!
        // Encoder 输出顺序: [high_res_0, high_res_1, image_embed]
        // Decoder 输入顺序: [image_embed, high_res_0, high_res_1]
        // 
        // 根据日志:
        // Encoder Output 0: [1,32,256,256]  → high_res_feats_0
        // Encoder Output 1: [1,64,128,128]  → high_res_feats_1
        // Encoder Output 2: [1,256,64,64]   → image_embed
        //
        // Decoder 期望:
        // Decoder Input 0: [1,256,64,64]    → image_embed (Encoder Output 2)
        // Decoder Input 1: [1,32,256,256]   → high_res_0 (Encoder Output 0)
        // Decoder Input 2: [1,64,128,128]   → high_res_1 (Encoder Output 1)
        
        inputs[0] = embeddings[2];  // image_embed
        inputs[1] = embeddings[0];  // high_res_feats_0
        inputs[2] = embeddings[1];  // high_res_feats_1

        // Input 3: point_coords [1, 2, 2] - Box Prompt (左上角 + 右下角)
        // 第一个点: 左上角 (x1, y1), 第二个点: 右下角 (x2, y2)
        inputs[3].dtype = BM_FLOAT32;
        inputs[3].shape = decoderInfo->stages[0].input_shapes[3];
        inputs[3].st_mode = BM_STORE_1N;
        
        // 打印期望的 shape
        logger.debug("SAM2 Decoder Input 3 (point_coords) shape: [" + 
                     std::to_string(inputs[3].shape.dims[0]) + "," +
                     std::to_string(inputs[3].shape.dims[1]) + "," +
                     std::to_string(inputs[3].shape.dims[2]) + "]");
        
        if (bm_malloc_device_byte(bmHandle, &inputs[3].device_mem,
                              inputs[3].shape.dims[0] * inputs[3].shape.dims[1] * 
                              inputs[3].shape.dims[2] * sizeof(float)) != BM_SUCCESS) {
            logger.error("SAM2 Decoder: 分配 point_coords 内存失败");
            return false;
        }
        
        // shape 是 [1, 2, 2],需要 4 个 float: [x1, y1, x2, y2]
        float pointCoords[4] = {x1, y1, x2, y2};
        bm_memcpy_s2d(bmHandle, inputs[3].device_mem, pointCoords);

        // Input 4: point_labels [1, 2] - Label: 2=左上角, 3=右下角 (Box Prompt)
        inputs[4].dtype = BM_FLOAT32;
        inputs[4].shape = decoderInfo->stages[0].input_shapes[4];
        inputs[4].st_mode = BM_STORE_1N;
        
        logger.debug("SAM2 Decoder Input 4 (point_labels) shape: [" + 
                     std::to_string(inputs[4].shape.dims[0]) + "," +
                     std::to_string(inputs[4].shape.dims[1]) + "]");
        
        if (bm_malloc_device_byte(bmHandle, &inputs[4].device_mem,
                              inputs[4].shape.dims[0] * inputs[4].shape.dims[1] * sizeof(float)) != BM_SUCCESS) {
            logger.error("SAM2 Decoder: 分配 point_labels 内存失败");
            bm_free_device(bmHandle, inputs[3].device_mem);
            return false;
        }
        // SAM2 Box Prompt: 2=左上角(top-left), 3=右下角(bottom-right)
        float pointLabels[2] = {2.0f, 3.0f};
        bm_memcpy_s2d(bmHandle, inputs[4].device_mem, pointLabels);

        // Input 5: mask_input [1, 1, 256, 256] - 全 0
        int maskInputSize = 1 * 1 * 256 * 256;
        std::vector<float> maskInput(maskInputSize, 0.0f);
        inputs[5].dtype = BM_FLOAT32;
        inputs[5].shape = decoderInfo->stages[0].input_shapes[5];
        inputs[5].st_mode = BM_STORE_1N;
        
        size_t maskByteSize = bmrt_tensor_bytesize(&inputs[5]);
        logger.debug("SAM2 Decoder Input 5 (mask_input) bytesize: " + std::to_string(maskByteSize) + 
                     " (expected: " + std::to_string(maskInputSize * sizeof(float)) + ")");
        
        if (bm_malloc_device_byte(bmHandle, &inputs[5].device_mem, maskByteSize) != BM_SUCCESS) {
            logger.error("SAM2 Decoder: 分配 mask_input 内存失败");
            for (int i = 3; i < 5; ++i) bm_free_device(bmHandle, inputs[i].device_mem);
            return false;
        }
        bm_memcpy_s2d(bmHandle, inputs[5].device_mem, maskInput.data());

        // Input 6: has_mask_input [1] - 值为 0
        float hasMaskInput[1] = {0.0f};
        inputs[6].dtype = BM_FLOAT32;
        inputs[6].shape = decoderInfo->stages[0].input_shapes[6];
        inputs[6].st_mode = BM_STORE_1N;
        
        size_t hasMaskByteSize = bmrt_tensor_bytesize(&inputs[6]);
        logger.debug("SAM2 Decoder Input 6 (has_mask_input) bytesize: " + std::to_string(hasMaskByteSize) + 
                     " (expected: " + std::to_string(sizeof(float)) + ")");
        
        if (bm_malloc_device_byte(bmHandle, &inputs[6].device_mem, hasMaskByteSize) != BM_SUCCESS) {
            logger.error("SAM2 Decoder: 分配 has_mask_input 内存失败");
            for (int i = 3; i < 6; ++i) bm_free_device(bmHandle, inputs[i].device_mem);
            return false;
        }
        bm_memcpy_s2d(bmHandle, inputs[6].device_mem, hasMaskInput);

        // 3. 准备输出 Tensors (使用 output_num)
        std::vector<bm_tensor_t> outputs(decoderInfo->output_num);
        for (int i = 0; i < decoderInfo->output_num; ++i) {
            outputs[i].dtype = BM_FLOAT32;
            outputs[i].shape = decoderInfo->stages[0].output_shapes[i];
            outputs[i].st_mode = BM_STORE_1N;
            bm_malloc_device_byte(bmHandle, &outputs[i].device_mem,
                                  bmrt_tensor_bytesize(&outputs[i]));
        }

        // 打印所有输入的 shape 用于调试
        logger.debug("SAM2 Decoder 输入张量 shapes:");
        for (int i = 0; i < 7; ++i) {
            std::string shapeStr = "[";
            for (int d = 0; d < inputs[i].shape.num_dims; ++d) {
                if (d > 0) shapeStr += ",";
                shapeStr += std::to_string(inputs[i].shape.dims[d]);
            }
            shapeStr += "]";
            logger.debug("  Input " + std::to_string(i) + ": " + shapeStr);
        }

        // 4. 执行推理
        bool success = bmrt_launch_tensor_ex(decoderNet, decoderNetName.c_str(),
                                             inputs.data(), inputs.size(),
                                             outputs.data(), outputs.size(),
                                             true, false);
        
        // 释放 Decoder 输入内存 (3-6, 0-2 是共享的 Encoder 输出)
        for (int i = 3; i < 7; ++i) {
            bm_free_device(bmHandle, inputs[i].device_mem);
        }

        if (!success) {
            logger.error("SAM2 Decoder: 推理失败");
            for (auto& t : outputs) bm_free_device(bmHandle, t.device_mem);
            return false;
        }

        bm_thread_sync(bmHandle);

        // 5. 解析输出: masks [1, 3, 256, 256], iou_predictions [1, 3]
        // 先拷贝全部 masks 数据到 host
        int totalMaskSize = 3 * SAM2_MASK_SIZE * SAM2_MASK_SIZE;  // 3 个 256x256 的 mask
        std::vector<float> allMasksData(totalMaskSize);
        bm_memcpy_d2s(bmHandle, allMasksData.data(), outputs[0].device_mem);
        
        // 拷贝 IOU 数据
        std::vector<float> ious(3);
        bm_memcpy_d2s(bmHandle, ious.data(), outputs[1].device_mem);

        // 选择 IOU 最高的 Mask
        int bestIdx = std::max_element(ious.begin(), ious.end()) - ious.begin();
        iou = ious[bestIdx];
        
        logger.debug("SAM2 Decoder: 选择 Mask #" + std::to_string(bestIdx) + 
                     ", IOU=" + std::to_string(iou));

        // 提取对应的 Mask (256x256)
        int maskSize = SAM2_MASK_SIZE * SAM2_MASK_SIZE;
        std::vector<float> maskData(maskSize);
        int offset = bestIdx * maskSize;  // 元素偏移,不是字节偏移
        std::copy(allMasksData.begin() + offset, 
                  allMasksData.begin() + offset + maskSize, 
                  maskData.begin());

        // 6. Resize Mask 回原始尺寸
        cv::Mat mask256(SAM2_MASK_SIZE, SAM2_MASK_SIZE, CV_32FC1, maskData.data());
        cv::threshold(mask256, mask256, 0.0f, 1.0f, cv::THRESH_BINARY);

        // 计算有效区域 (去除 Padding) - 复用之前计算的 scale
        int validH = static_cast<int>(origH * scale);
        int validW = static_cast<int>(origW * scale);
        int maskValidH = validH / SCALE_FACTOR;
        int maskValidW = validW / SCALE_FACTOR;

        cv::Mat maskCrop = mask256(cv::Rect(0, 0, maskValidW, maskValidH));
        cv::resize(maskCrop, mask, cv::Size(origW, origH), 0, 0, cv::INTER_LINEAR);

        // 释放输出内存
        for (auto& t : outputs) {
            bm_free_device(bmHandle, t.device_mem);
        }

        return true;
    }

    /**
     * @brief 对多个检测框执行分割
     * @param frame 原始图像
     * @param boxes 检测框列表
     * @return 分割结果列表
     */
    std::vector<SegmentationResult> run(const cv::Mat& frame,
                                         const std::vector<DetectionBox>& boxes) {
        auto& logger = core::Logger::getInstance();
        std::vector<SegmentationResult> results;

        if (boxes.empty()) {
            return results;
        }

        // 1. 预处理
        int origH, origW;
        cv::Mat preprocessed = preprocess(frame, origH, origW);

        // 2. 运行 Encoder (一次)
        std::vector<bm_tensor_t> embeddings;
        if (!runEncoder(preprocessed, embeddings)) {
            logger.error("SAM2: Encoder 执行失败");
            return results;
        }

        // 3. 对每个 Box 运行 Decoder
        for (const auto& box : boxes) {
            cv::Mat mask;
            float iou = 0.0f;

            if (runDecoder(embeddings, box, origH, origW, mask, iou)) {
                SegmentationResult result;
                result.classId = box.classId;
                result.className = box.className;
                result.confidence = iou;  // 使用 IOU 作为置信度
                result.box = cv::Rect(box.x, box.y, box.width, box.height);
                result.mask = mask.clone();

                // 提取轮廓 (选择面积最大的轮廓)
                std::vector<std::vector<cv::Point>> contours;
                cv::Mat mask8u;
                mask.convertTo(mask8u, CV_8UC1, 255.0);
                cv::findContours(mask8u, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

                if (!contours.empty()) {
                    // 找到面积最大的轮廓
                    auto maxContour = std::max_element(contours.begin(), contours.end(),
                        [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                            return cv::contourArea(a) < cv::contourArea(b);
                        });
                    result.contours = *maxContour;
                }

                results.push_back(result);
            } else {
                logger.warning("SAM2: Box [" + std::to_string(box.x) + "," +
                               std::to_string(box.y) + "] 分割失败");
            }
        }

        // 4. 释放 Encoder 输出内存
        for (auto& t : embeddings) {
            bm_free_device(bmHandle, t.device_mem);
        }

        logger.info("SAM2: 分割完成, 输入 " + std::to_string(boxes.size()) + 
                    " 个框, 输出 " + std::to_string(results.size()) + " 个掩码");

        return results;
    }

private:
    bm_handle_t bmHandle;
    void* encoderNet;
    void* decoderNet;
    std::string encoderNetName;
    std::string decoderNetName;
    const bm_net_info_t* encoderInfo;
    const bm_net_info_t* decoderInfo;
};

//=============================================================================
// Sam2Segmentor - 公共接口实现
//=============================================================================

Sam2Segmentor::Sam2Segmentor() : impl_(std::make_unique<Impl>()) {}

Sam2Segmentor::~Sam2Segmentor() = default;

bool Sam2Segmentor::init(const std::string& modelPath, const std::string& configPath) {
    // configPath 用于传递 Decoder 模型路径
    // modelPath = Encoder, configPath = Decoder
    
    std::string encoderPath = modelPath;
    std::string decoderPath = configPath;

    if (decoderPath.empty()) {
        core::Logger::getInstance().error("SAM2: 请在 configPath 参数中提供 Decoder 模型路径");
        return false;
    }

    return impl_->init(encoderPath, decoderPath);
}

std::vector<SegmentationResult> Sam2Segmentor::segment(const cv::Mat& frame) {
    core::Logger::getInstance().warning("SAM2: 自动全图分割尚未实现");
    return {};
}

std::vector<SegmentationResult> Sam2Segmentor::segmentWithPrompts(
    const cv::Mat& frame,
    const std::vector<DetectionBox>& boxes) {
    return impl_->run(frame, boxes);
}

}  // namespace vision
}  // namespace esdk_sophon
