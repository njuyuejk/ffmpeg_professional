/**
 * @file hw_decoder.h
 * @brief 硬件解码器头文件
 */
#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

#include "config/stream_types.h"
#include <memory>

/**
 * @brief 硬件解码器类
 */
class HWDecoder {
private:
    AVBufferRef* hw_device_ctx = nullptr;
    AVCodecContext* decoder_ctx = nullptr;
    const AVCodec* decoder = nullptr;
    AVFrame* hw_frame = nullptr;
    AVFrame* sw_frame = nullptr;
    HWAccelType hwaccel_type;
    bool initialized = false;
    bool low_latency = false;

    // 硬件转软件帧
    int hwFrameToSwFrame(AVFrame* hw, AVFrame* sw);

public:
    /**
     * @brief 构造函数
     * @param type 硬件加速类型
     * @param lowLatency 是否启用低延迟模式
     */
    HWDecoder(HWAccelType type, bool lowLatency = true);

    /**
     * @brief 析构函数
     */
    ~HWDecoder();

    /**
     * @brief 初始化硬件解码器
     * @param codec_name 解码器名称
     * @return 是否成功
     */
    bool init(const std::string& codec_name);

    /**
     * @brief 设置解码器参数
     * @param codecpar 编解码参数
     * @return 是否成功
     */
    bool setParameters(AVCodecParameters* codecpar);

    /**
     * @brief 解码数据包
     * @param pkt 数据包
     * @param got_frame 是否获取到帧
     * @return 解码后的帧
     */
    AVFrame* decode(AVPacket* pkt, int& got_frame);

    /**
     * @brief 刷新解码器
     * @param got_frame 是否获取到帧
     * @return 解码后的帧
     */
    AVFrame* flush(int& got_frame);

    /**
     * @brief 获取解码器上下文
     * @return 解码器上下文
     */
    AVCodecContext* getContext();

    /**
     * @brief 设置低延迟模式
     * @param enable 是否启用
     */
    void setLowLatency(bool enable);
};