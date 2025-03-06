/**
 * @file hw_decoder.cpp
 * @brief 硬件解码器实现
 */
#include "ffmpeg_base/hw_decoder.h"
#include "logger/logger.h"

extern "C" {
#include <libavutil/opt.h>
}

// 构造函数
HWDecoder::HWDecoder(HWAccelType type, bool lowLatency)
        : hwaccel_type(type), low_latency(lowLatency) {
    hw_frame = av_frame_alloc();
    sw_frame = av_frame_alloc();

    if (!hw_frame || !sw_frame) {
        Logger::error("无法分配解码器帧");
    }
}

// 析构函数
HWDecoder::~HWDecoder() {
    if (sw_frame) av_frame_free(&sw_frame);
    if (hw_frame) av_frame_free(&hw_frame);
    if (decoder_ctx) avcodec_free_context(&decoder_ctx);
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
}

// 硬件转软件帧
int HWDecoder::hwFrameToSwFrame(AVFrame* hw, AVFrame* sw) {
    if (hw->format == AV_PIX_FMT_NV12 ||
        hw->format == AV_PIX_FMT_YUV420P) {
        // 某些格式已经是软件格式，直接复制
        av_frame_copy(sw, hw);
        av_frame_copy_props(sw, hw);
        return 0;
    }

    int ret = av_hwframe_transfer_data(sw, hw, 0);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("硬件帧转软件帧失败: " + std::string(err_buf));
        return ret;
    }

    av_frame_copy_props(sw, hw);
    return 0;
}

// 初始化硬件解码器
bool HWDecoder::init(const std::string& codec_name) {
    // 查找解码器
    decoder = avcodec_find_decoder_by_name(codec_name.c_str());
    if (!decoder) {
        // 尝试通过短名称查找
        if (codec_name == "h264") {
            decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
        } else if (codec_name == "h265" || codec_name == "hevc") {
            decoder = avcodec_find_decoder(AV_CODEC_ID_HEVC);
        } else {
            Logger::error("找不到解码器: " + codec_name);
            return false;
        }

        if (!decoder) {
            Logger::error("找不到解码器: " + codec_name);
            return false;
        }
    }

    // 如果是软件解码，直接创建上下文
    if (hwaccel_type == HWAccelType::NONE) {
        decoder_ctx = avcodec_alloc_context3(decoder);
        if (!decoder_ctx) {
            Logger::error("无法分配解码器上下文");
            return false;
        }

        if (low_latency) {
            // 软件解码器的低延迟设置
            av_opt_set(decoder_ctx->priv_data, "tune", "zerolatency", 0);
            av_opt_set(decoder_ctx->priv_data, "preset", "ultrafast", 0);
            decoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
            decoder_ctx->thread_count = 1; // 单线程解码减少延迟
        }

        initialized = true;
        return true;
    }

    // 硬件解码
    enum AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_NONE;

    switch (hwaccel_type) {
        case HWAccelType::CUDA:
            hw_type = AV_HWDEVICE_TYPE_CUDA;
            break;
        case HWAccelType::QSV:
            hw_type = AV_HWDEVICE_TYPE_QSV;
            break;
        case HWAccelType::VAAPI:
            hw_type = AV_HWDEVICE_TYPE_VAAPI;
            break;
        case HWAccelType::VIDEOTOOLBOX:
            hw_type = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
            break;
        case HWAccelType::DXVA2:
            hw_type = AV_HWDEVICE_TYPE_DXVA2;
            break;
        default:
            Logger::error("不支持的硬件加速类型");
            return false;
    }

    // 创建硬件设备上下文
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, hw_type, NULL, NULL, 0);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("无法创建硬件设备上下文: " + std::string(err_buf));

        Logger::warning("尝试使用软件解码");
        hwaccel_type = HWAccelType::NONE;
        return init(codec_name);
    }

    // 检查解码器是否支持硬件加速
    bool hw_config_found = false;
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            break;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hw_type) {
            hw_config_found = true;
            break;
        }
    }

    if (!hw_config_found) {
        Logger::warning("解码器 " + std::string(decoder->name) + " 不支持硬件加速，使用软件解码");
        if (hw_device_ctx) {
            av_buffer_unref(&hw_device_ctx);
            hw_device_ctx = nullptr;
        }
        hwaccel_type = HWAccelType::NONE;
        return init(codec_name);
    }

    // 配置解码器上下文
    decoder_ctx = avcodec_alloc_context3(decoder);
    if (!decoder_ctx) {
        Logger::error("无法分配解码器上下文");
        return false;
    }

    if (hwaccel_type != HWAccelType::NONE) {
        decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    }

    if (low_latency) {
        // 硬件解码器的低延迟设置
        av_opt_set(decoder_ctx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(decoder_ctx->priv_data, "preset", "ultrafast", 0);
        decoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        decoder_ctx->thread_count = 1; // 单线程解码减少延迟
    }

    initialized = true;
    Logger::debug("初始化解码器成功: " + codec_name +
                  (hwaccel_type != HWAccelType::NONE ?
                   " (硬件加速: " + hwaccelTypeToString(hwaccel_type) + ")" : " (软件)") +
                  (low_latency ? " [低延迟模式]" : ""));

    return true;
}

// 设置解码器参数
bool HWDecoder::setParameters(AVCodecParameters* codecpar) {
    if (!initialized || !decoder_ctx) {
        Logger::error("解码器未初始化");
        return false;
    }

    int ret = avcodec_parameters_to_context(decoder_ctx, codecpar);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("无法设置解码器参数: " + std::string(err_buf));
        return false;
    }

    if (hwaccel_type != HWAccelType::NONE && hw_device_ctx) {
        decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    }

    // 低延迟设置
    if (low_latency) {
        decoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        // 减少缓冲区大小
        decoder_ctx->delay = 0;
    }

    ret = avcodec_open2(decoder_ctx, decoder, NULL);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("无法打开解码器: " + std::string(err_buf));
        return false;
    }

    Logger::debug("解码器参数设置成功: " + std::string(decoder->name) +
                  " " + std::to_string(codecpar->width) + "x" +
                  std::to_string(codecpar->height));

    return true;
}

// 解码数据包
AVFrame* HWDecoder::decode(AVPacket* pkt, int& got_frame) {
    got_frame = 0;

    if (!initialized || !decoder_ctx) {
        Logger::error("解码器未初始化");
        return nullptr;
    }

    int ret = avcodec_send_packet(decoder_ctx, pkt);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            Logger::error("解码发送数据包错误: " + std::string(err_buf));
        }
        return nullptr;
    }

    ret = avcodec_receive_frame(decoder_ctx, hw_frame);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            Logger::error("解码接收帧错误: " + std::string(err_buf));
        }
        return nullptr;
    }

    got_frame = 1;

    // 如果是硬件帧，需要转换为软件帧
    if (hwaccel_type != HWAccelType::NONE &&
        decoder_ctx->hw_frames_ctx &&
        hw_frame->format == ((AVHWFramesContext*)decoder_ctx->hw_frames_ctx->data)->format) {
        if (hwFrameToSwFrame(hw_frame, sw_frame) < 0) {
            return nullptr;
        }
        return sw_frame;
    }

    return hw_frame;
}

// 刷新解码器
AVFrame* HWDecoder::flush(int& got_frame) {
    return decode(NULL, got_frame);
}

// 获取解码器上下文
AVCodecContext* HWDecoder::getContext() {
    return decoder_ctx;
}

// 设置低延迟模式
void HWDecoder::setLowLatency(bool enable) {
    low_latency = enable;
    if (decoder_ctx) {
        if (enable) {
            decoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        } else {
            decoder_ctx->flags &= ~AV_CODEC_FLAG_LOW_DELAY;
        }
    }
}