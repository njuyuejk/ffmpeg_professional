/**
 * @file decoder.h
 * @brief 硬件解码器
 */

#ifndef FFMPEG_STREAM_DECODER_H
#define FFMPEG_STREAM_DECODER_H

#include "hw_accel.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace ffmpeg_stream {

// 硬件解码器类
    class HWDecoder {
    public:
        HWDecoder();
        ~HWDecoder();

        // 初始化解码器，使用特定硬件加速
        bool init(AVCodecParameters* codecParams, HWAccelType hwType = HWAccelType::CUDA);

        // 初始化软件解码器
        bool initSoftwareDecoder(AVCodecParameters* codecParams);

        // 解码一个包
        AVFrame* decode(AVPacket* packet);

        // 刷新解码器缓冲
        void flush();

        // 清理资源
        void cleanup();

        // 获取解码器上下文
        AVCodecContext* getCodecContext() const;

    private:
        AVCodecContext* codecContext;
        AVBufferRef* hwDeviceContext;
        AVPixelFormat hwPixFmt;
    };

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_DECODER_H