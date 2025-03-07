/**
 * @file encoder.h
 * @brief 硬件编码器
 */

#ifndef FFMPEG_STREAM_ENCODER_H
#define FFMPEG_STREAM_ENCODER_H

#include "hw_accel.h"
#include "config/config.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace ffmpeg_stream {

// 硬件编码器类
    class HWEncoder {
    public:
        HWEncoder();
        ~HWEncoder();

        // 从流配置初始化编码器
        bool init(const StreamConfig& config);

        // 初始化编码器，使用特定硬件加速
        bool init(int width, int height, AVPixelFormat pixFmt, int bitrate,
                  int fps, HWAccelType hwType = HWAccelType::CUDA, AVCodecID codecId = AV_CODEC_ID_H264);

        // 初始化软件编码器
        bool initSoftwareEncoder(int width, int height, AVPixelFormat pixFmt, int bitrate,
                                 int fps, AVCodecID codecId = AV_CODEC_ID_H264);

        // 编码一帧
        AVPacket* encode(AVFrame* frame);

        // 刷新编码器缓冲
        void flush();

        // 清理资源
        void cleanup();

        // 获取编码器上下文
        AVCodecContext* getCodecContext();

    private:
        AVCodecContext* codecContext;
        AVBufferRef* hwDeviceContext;
        AVPixelFormat hwPixFmt;
    };

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_ENCODER_H