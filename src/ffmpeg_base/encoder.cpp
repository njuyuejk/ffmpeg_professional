/**
 * @file encoder.cpp
 * @brief 硬件编码器实现
 */

#include "ffmpeg_base/encoder.h"
#include "logger/logger.h"
#include "common/utils.h"

namespace ffmpeg_stream {

    HWEncoder::HWEncoder() : codecContext(nullptr), hwDeviceContext(nullptr), hwPixFmt(AV_PIX_FMT_NONE) {
    }

    HWEncoder::~HWEncoder() {
        cleanup();
    }

    bool HWEncoder::init(const StreamConfig& config) {
        return init(config.width, config.height, AV_PIX_FMT_YUV420P, config.bitrate,
                    config.fps, config.encoderHWAccel);
    }

    bool HWEncoder::init(int width, int height, AVPixelFormat pixFmt, int bitrate,
                         int fps, HWAccelType hwType, AVCodecID codecId) {

        if (hwType == HWAccelType::NONE) {
            return initSoftwareEncoder(width, height, pixFmt, bitrate, fps, codecId);
        }

        AVHWDeviceType avHWType = hwAccelTypeToAVHWDeviceType(hwType);

        // 查找硬件编码器
        const AVCodec* encoder = nullptr;

        // 根据硬件加速类型选择合适的编码器
        switch (hwType) {
            case HWAccelType::CUDA:
                encoder = avcodec_find_encoder_by_name("h264_nvenc");
                break;
            case HWAccelType::QSV:
                encoder = avcodec_find_encoder_by_name("h264_qsv");
                break;
            case HWAccelType::VAAPI:
                encoder = avcodec_find_encoder_by_name("h264_vaapi");
                break;
            case HWAccelType::AMF:
                encoder = avcodec_find_encoder_by_name("h264_amf");
                break;
            default:
                break;
        }

        // 如果找不到硬件编码器，回退到软件编码
        if (!encoder) {
            Logger::warning("Hardware encoder for %s not found, falling back to software encoding",
                            hwAccelTypeToString(hwType).c_str());
            return initSoftwareEncoder(width, height, pixFmt, bitrate, fps, codecId);
        }

        // 检查是否支持硬件加速 - 使用avcodec_get_hw_config代替直接访问encoder->hw_configs
        bool hwAccelSupported = false;
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(encoder, i);
            if (!config) {
                // 没有更多配置或不支持硬件加速
                break;
            }

            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == avHWType) {
                hwPixFmt = config->pix_fmt;
                hwAccelSupported = true;
                break;
            }
        }

        if (!hwAccelSupported) {
            Logger::warning("Hardware acceleration type %s not supported by encoder %s, falling back to software encoding",
                            hwAccelTypeToString(hwType).c_str(), encoder->name);
            return initSoftwareEncoder(width, height, pixFmt, bitrate, fps, codecId);
        }

        // 创建编码器上下文
        codecContext = avcodec_alloc_context3(encoder);
        if (!codecContext) {
            Logger::error("Failed to allocate encoder context");
            return false;
        }

        // 设置编码参数
        codecContext->width = width;
        codecContext->height = height;
        codecContext->time_base = AVRational{1, fps};
        codecContext->framerate = AVRational{fps, 1};
        codecContext->bit_rate = bitrate;
        codecContext->gop_size = fps; // 每秒一个关键帧
        codecContext->max_b_frames = 0; // B帧可能增加延迟
        codecContext->pix_fmt = hwPixFmt;

        // 对于H.264，设置profile
        if (codecId == AV_CODEC_ID_H264) {
            codecContext->profile = FF_PROFILE_H264_MAIN;
        }

        // 创建硬件设备上下文
        int err = av_hwdevice_ctx_create(&hwDeviceContext, avHWType, nullptr, nullptr, 0);
        if (err < 0) {
            utils::printFFmpegError("Failed to create hardware device context", err);
            Logger::warning("Falling back to software encoding");
            cleanup();
            return initSoftwareEncoder(width, height, pixFmt, bitrate, fps, codecId);
        }

        // 设置硬件设备上下文
        codecContext->hw_device_ctx = av_buffer_ref(hwDeviceContext);

        // 打开编码器
        AVDictionary* options = nullptr;
        // 设置低延迟选项
        av_dict_set(&options, "tune", "zerolatency", 0);
        // 设置预设
        av_dict_set(&options, "preset", "fast", 0);

        err = avcodec_open2(codecContext, encoder, &options);
        av_dict_free(&options);

        if (err < 0) {
            utils::printFFmpegError("Failed to open encoder", err);
            cleanup();
            Logger::warning("Falling back to software encoding");
            return initSoftwareEncoder(width, height, pixFmt, bitrate, fps, codecId);
        }

        Logger::info("Initialized hardware encoder %s with %s acceleration",
                     encoder->name, hwAccelTypeToString(hwType).c_str());
        return true;
    }

    bool HWEncoder::initSoftwareEncoder(int width, int height, AVPixelFormat pixFmt, int bitrate,
                                        int fps, AVCodecID codecId) {
        // 查找软件编码器
        const AVCodec* encoder = avcodec_find_encoder(codecId);
        if (!encoder) {
            Logger::error("Failed to find encoder for codec id %d", codecId);
            return false;
        }

        // 创建编码器上下文
        codecContext = avcodec_alloc_context3(encoder);
        if (!codecContext) {
            Logger::error("Failed to allocate encoder context");
            return false;
        }

        // 设置编码参数
        codecContext->width = width;
        codecContext->height = height;
        codecContext->time_base = AVRational{1, fps};
        codecContext->framerate = AVRational{fps, 1};
        codecContext->bit_rate = bitrate;
        codecContext->gop_size = fps; // 每秒一个关键帧
        codecContext->max_b_frames = 2;

        // 确保选择编码器支持的像素格式
        codecContext->pix_fmt = pixFmt;
        if (encoder->pix_fmts) {
            codecContext->pix_fmt = encoder->pix_fmts[0];
            for (int i = 0; encoder->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
                if (encoder->pix_fmts[i] == pixFmt) {
                    codecContext->pix_fmt = pixFmt;
                    break;
                }
            }
        }

        // 对于H.264，设置profile
        if (codecId == AV_CODEC_ID_H264) {
            codecContext->profile = FF_PROFILE_H264_MAIN;
        }

        // 打开编码器
        AVDictionary* options = nullptr;
        // 设置低延迟选项
        av_dict_set(&options, "tune", "zerolatency", 0);
        // 设置预设
        av_dict_set(&options, "preset", "medium", 0);

        int err = avcodec_open2(codecContext, encoder, &options);
        av_dict_free(&options);

        if (err < 0) {
            utils::printFFmpegError("Failed to open encoder", err);
            cleanup();
            return false;
        }

        Logger::info("Initialized software encoder %s", encoder->name);
        return true;
    }

    AVPacket* HWEncoder::encode(AVFrame* frame) {
        int ret = avcodec_send_frame(codecContext, frame);
        if (ret < 0) {
            utils::printFFmpegError("Error sending frame for encoding", ret);
            return nullptr;
        }

        AVPacket* packet = av_packet_alloc();
        ret = avcodec_receive_packet(codecContext, packet);
        if (ret < 0) {
            av_packet_free(&packet);
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                utils::printFFmpegError("Error receiving packet from encoder", ret);
            }
            return nullptr;
        }

        return packet;
    }

    void HWEncoder::flush() {
        // 刷新编码器缓冲区
        int ret = avcodec_send_frame(codecContext, nullptr);
        if (ret < 0) {
            utils::printFFmpegError("Error flushing encoder", ret);
            return;
        }

        AVPacket* packet = av_packet_alloc();
        while (ret >= 0) {
            ret = avcodec_receive_packet(codecContext, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                utils::printFFmpegError("Error receiving packet while flushing", ret);
                break;
            }

            // 处理刷出的包
            // ...

            av_packet_unref(packet);
        }
        av_packet_free(&packet);
    }

    void HWEncoder::cleanup() {
        if (codecContext) {
            avcodec_free_context(&codecContext);
            codecContext = nullptr;
        }

        if (hwDeviceContext) {
            av_buffer_unref(&hwDeviceContext);
            hwDeviceContext = nullptr;
        }

        hwPixFmt = AV_PIX_FMT_NONE;
    }

    AVCodecContext* HWEncoder::getCodecContext() {
        return codecContext;
    }

} // namespace ffmpeg_stream