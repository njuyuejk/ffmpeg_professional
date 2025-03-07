/**
 * @file decoder.cpp
 * @brief 硬件解码器实现
 */

#include "ffmpeg_base/decoder.h"
#include "logger/logger.h"
#include "common/utils.h"

namespace ffmpeg_stream {

    HWDecoder::HWDecoder() : codecContext(nullptr), hwDeviceContext(nullptr), hwPixFmt(AV_PIX_FMT_NONE) {
    }

    HWDecoder::~HWDecoder() {
        cleanup();
    }

    bool HWDecoder::init(AVCodecParameters* codecParams, HWAccelType hwType) {
        // 检查是否使用硬件加速
        if (hwType == HWAccelType::NONE) {
            return initSoftwareDecoder(codecParams);
        }

        AVHWDeviceType avHWType = hwAccelTypeToAVHWDeviceType(hwType);

        // 查找解码器
        const AVCodec* decoder = avcodec_find_decoder(codecParams->codec_id);
        if (!decoder) {
            Logger::error("Failed to find decoder for codec id %d", codecParams->codec_id);
            return false;
        }

        // 检查是否支持硬件加速
        bool hwAccelSupported = false;
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
            if (!config) {
                if (i == 0) {
                    Logger::warning("Decoder %s does not support hardware acceleration", decoder->name);
                }
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
            Logger::warning("Hardware acceleration type %s not supported by decoder %s, falling back to software decoding",
                            hwAccelTypeToString(hwType).c_str(), decoder->name);
            return initSoftwareDecoder(codecParams);
        }

        // 创建硬件设备上下文
        int err = av_hwdevice_ctx_create(&hwDeviceContext, avHWType, nullptr, nullptr, 0);
        if (err < 0) {
            utils::printFFmpegError("Failed to create hardware device context", err);
            Logger::warning("Falling back to software decoding");
            return initSoftwareDecoder(codecParams);
        }

        // 创建解码器上下文
        codecContext = avcodec_alloc_context3(decoder);
        if (!codecContext) {
            Logger::error("Failed to allocate decoder context");
            return false;
        }

        // 复制编解码器参数到上下文
        if ((err = avcodec_parameters_to_context(codecContext, codecParams)) < 0) {
            utils::printFFmpegError("Failed to copy codec parameters to context", err);
            return false;
        }

        // 设置硬件设备上下文
        codecContext->hw_device_ctx = av_buffer_ref(hwDeviceContext);

        // 打开解码器
        if ((err = avcodec_open2(codecContext, decoder, nullptr)) < 0) {
            utils::printFFmpegError("Failed to open codec", err);
            return false;
        }

        Logger::info("Initialized hardware decoder %s with %s acceleration",
                     decoder->name, hwAccelTypeToString(hwType).c_str());
        return true;
    }

    bool HWDecoder::initSoftwareDecoder(AVCodecParameters* codecParams) {
        const AVCodec* decoder = avcodec_find_decoder(codecParams->codec_id);
        if (!decoder) {
            Logger::error("Failed to find decoder for codec id %d", codecParams->codec_id);
            return false;
        }

        codecContext = avcodec_alloc_context3(decoder);
        if (!codecContext) {
            Logger::error("Failed to allocate decoder context");
            return false;
        }

        int err;
        if ((err = avcodec_parameters_to_context(codecContext, codecParams)) < 0) {
            utils::printFFmpegError("Failed to copy codec parameters to context", err);
            return false;
        }

        if ((err = avcodec_open2(codecContext, decoder, nullptr)) < 0) {
            utils::printFFmpegError("Failed to open codec", err);
            return false;
        }

        // 软解码不需要设置hwPixFmt
        hwPixFmt = AV_PIX_FMT_NONE;
        Logger::info("Initialized software decoder %s", decoder->name);
        return true;
    }

    AVFrame* HWDecoder::decode(AVPacket* packet) {
        int ret = avcodec_send_packet(codecContext, packet);
        if (ret < 0) {
            if (ret != AVERROR(EAGAIN)) {
                utils::printFFmpegError("Error sending packet for decoding", ret);
            }
            return nullptr;
        }

        AVFrame* frame = av_frame_alloc();
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                utils::printFFmpegError("Error receiving frame", ret);
            }
            return nullptr;
        }

        // 如果是硬件帧，需要将其转换为软件帧
        if (frame->format == hwPixFmt) {
            AVFrame* swFrame = av_frame_alloc();
            ret = av_hwframe_transfer_data(swFrame, frame, 0);
            if (ret < 0) {
                utils::printFFmpegError("Error transferring data from GPU to CPU", ret);
                av_frame_free(&swFrame);
                av_frame_free(&frame);
                return nullptr;
            }
            av_frame_copy_props(swFrame, frame);
            av_frame_free(&frame);
            return swFrame;
        }

        return frame;
    }

    void HWDecoder::flush() {
        if (codecContext) {
            avcodec_flush_buffers(codecContext);
        }
    }

    void HWDecoder::cleanup() {
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

    AVCodecContext* HWDecoder::getCodecContext() const {
        return codecContext;
    }

} // namespace ffmpeg_stream