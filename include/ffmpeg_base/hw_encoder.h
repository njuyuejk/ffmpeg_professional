/**
 * @file hw_encoder.h
 * @brief 硬件编码器头文件
 */
#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

#include "config/stream_types.h"
#include <memory>

/**
 * @brief 硬件编码器类
 */
class HWEncoder {
private:
    AVBufferRef* hw_device_ctx = nullptr;
    AVCodecContext* encoder_ctx = nullptr;
    const AVCodec* encoder = nullptr;
    AVFrame* hw_frame = nullptr;
    HWAccelType hwaccel_type;
    bool initialized = false;
    bool low_latency = false;

    // 软件转硬件帧
    int swFrameToHwFrame(AVFrame* sw, AVFrame* hw);

public:
    /**
     * @brief 构造函数
     * @param type 硬件加速类型
     * @param lowLatency 是否启用低延迟模式
     */
    HWEncoder(HWAccelType type, bool lowLatency = true);

    /**
     * @brief 析构函数
     */
    ~HWEncoder();

    /**
     * @brief 初始化硬件编码器
     * @param config 流配置
     * @return 是否成功
     */
    bool init(const StreamConfig& config);

    /**
     * @brief 编码帧
     * @param frame 原始帧
     * @param pkt 编码后的数据包
     * @return 编码结果
     */
    int encode(AVFrame* frame, AVPacket* pkt);

    /**
     * @brief 刷新编码器
     * @param pkt 编码后的数据包
     * @return 编码结果
     */
    int flush(AVPacket* pkt);

    /**
     * @brief 获取编码器上下文
     * @return 编码器上下文
     */
    AVCodecContext* getContext();

    /**
     * @brief 设置低延迟模式
     * @param enable 是否启用
     */
    void setLowLatency(bool enable);
};