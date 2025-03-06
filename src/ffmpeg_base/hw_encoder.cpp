/**
 * @file hw_encoder.cpp
 * @brief 硬件编码器实现
 */
#include "ffmpeg_base/hw_encoder.h"
#include "logger/logger.h"

extern "C" {
#include <libavutil/opt.h>
}

// 构造函数
HWEncoder::HWEncoder(HWAccelType type, bool lowLatency)
        : hwaccel_type(type), low_latency(lowLatency) {
    hw_frame = av_frame_alloc();

    if (!hw_frame) {
        Logger::error("无法分配编码器帧");
    }
}

// 析构函数
HWEncoder::~HWEncoder() {
    if (hw_frame) av_frame_free(&hw_frame);
    if (encoder_ctx) avcodec_free_context(&encoder_ctx);
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
}

// 软件转硬件帧
int HWEncoder::swFrameToHwFrame(AVFrame* sw, AVFrame* hw) {
    if (!hw_device_ctx) {
        // 软件编码，直接复制
        av_frame_copy(hw, sw);
        av_frame_copy_props(hw, sw);
        return 0;
    }

    int ret = av_hwframe_get_buffer(encoder_ctx->hw_frames_ctx, hw, 0);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("获取硬件帧缓冲区失败: " + std::string(err_buf));
        return ret;
    }

    ret = av_hwframe_transfer_data(hw, sw, 0);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("数据传输到硬件帧失败: " + std::string(err_buf));
        return ret;
    }

    av_frame_copy_props(hw, sw);
    return 0;
}

// 初始化硬件编码器
bool HWEncoder::init(const StreamConfig& config) {
    // 如果是软件编码
    if (hwaccel_type == HWAccelType::NONE) {
        // 查找编码器
        encoder = avcodec_find_encoder_by_name(config.codec_name.c_str());
        if (!encoder) {
            // 尝试通过短名称查找
            if (config.codec_name == "h264") {
                encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
            } else if (config.codec_name == "h265" || config.codec_name == "hevc") {
                encoder = avcodec_find_encoder(AV_CODEC_ID_HEVC);
            } else {
                Logger::error("找不到编码器: " + config.codec_name);
                return false;
            }

            if (!encoder) {
                Logger::error("找不到编码器: " + config.codec_name);
                return false;
            }
        }

        encoder_ctx = avcodec_alloc_context3(encoder);
        if (!encoder_ctx) {
            Logger::error("无法分配编码器上下文");
            return false;
        }

        // 配置编码器参数
        encoder_ctx->width = config.width;
        encoder_ctx->height = config.height;
        encoder_ctx->time_base = AVRational{1, config.fps};
        encoder_ctx->framerate = AVRational{config.fps, 1};
        encoder_ctx->gop_size = config.gop;
        encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder_ctx->bit_rate = config.bitrate;

        // 低延迟设置
        if (low_latency) {
            encoder_ctx->max_b_frames = 0;  // 没有B帧
            encoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
            encoder_ctx->rc_max_rate = config.bitrate;
            encoder_ctx->rc_buffer_size = config.bitrate / 2;
        } else {
            encoder_ctx->max_b_frames = 1;
        }

        // 打开编码器
        AVDictionary* opts = nullptr;

        if (low_latency) {
            av_dict_set(&opts, "preset", "ultrafast", 0);
            av_dict_set(&opts, "tune", "zerolatency", 0);
        } else {
            av_dict_set(&opts, "preset", "medium", 0);
        }

        int ret = avcodec_open2(encoder_ctx, encoder, &opts);
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            Logger::error("无法打开编码器: " + std::string(err_buf));
            av_dict_free(&opts);
            return false;
        }

        av_dict_free(&opts);
        initialized = true;

        Logger::debug("初始化软件编码器成功: " + config.codec_name +
                      " " + std::to_string(config.width) + "x" +
                      std::to_string(config.height) +
                      (low_latency ? " [低延迟模式]" : ""));

        return true;
    }

    // 硬件编码
    enum AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_NONE;
    enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;

    // 确定硬件类型和像素格式
    switch (hwaccel_type) {
        case HWAccelType::CUDA:
            hw_type = AV_HWDEVICE_TYPE_CUDA;
            hw_pix_fmt = AV_PIX_FMT_CUDA;
            break;
        case HWAccelType::QSV:
            hw_type = AV_HWDEVICE_TYPE_QSV;
            hw_pix_fmt = AV_PIX_FMT_QSV;
            break;
        case HWAccelType::VAAPI:
            hw_type = AV_HWDEVICE_TYPE_VAAPI;
            hw_pix_fmt = AV_PIX_FMT_VAAPI;
            break;
        case HWAccelType::VIDEOTOOLBOX:
            hw_type = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
            hw_pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
            break;
        case HWAccelType::DXVA2:
            hw_type = AV_HWDEVICE_TYPE_DXVA2;
            hw_pix_fmt = AV_PIX_FMT_DXVA2_VLD;
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

        Logger::warning("尝试使用软件编码");
        hwaccel_type = HWAccelType::NONE;
        return init(config);
    }

    // 查找硬件编码器
    std::string hw_encoder_name = config.codec_name + "_" + hwaccelTypeToString(hwaccel_type);
    encoder = avcodec_find_encoder_by_name(hw_encoder_name.c_str());

    // 如果找不到特定的硬件编码器，尝试使用通用编码器
    if (!encoder) {
        Logger::warning("找不到硬件编码器: " + hw_encoder_name + "，尝试使用通用编码器");

        if (config.codec_name == "h264") {
            encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        } else if (config.codec_name == "h265" || config.codec_name == "hevc") {
            encoder = avcodec_find_encoder(AV_CODEC_ID_HEVC);
        } else {
            Logger::error("找不到编码器: " + config.codec_name);
            av_buffer_unref(&hw_device_ctx);
            return false;
        }

        if (!encoder) {
            Logger::error("找不到编码器: " + config.codec_name);
            av_buffer_unref(&hw_device_ctx);
            return false;
        }

        // 检查是否支持该硬件加速
        bool hw_config_found = false;
        for (int i = 0;; i++) {
            const AVCodecHWConfig* hw_config = avcodec_get_hw_config(encoder, i);
            if (!hw_config) {
                break;
            }
            if (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                hw_config->device_type == hw_type) {
                hw_pix_fmt = hw_config->pix_fmt;
                hw_config_found = true;
                break;
            }
        }

        if (!hw_config_found) {
            Logger::warning("编码器不支持硬件加速，使用软件编码");
            av_buffer_unref(&hw_device_ctx);
            hw_device_ctx = nullptr;
            hwaccel_type = HWAccelType::NONE;
            return init(config);  // 重新以软件编码方式初始化
        }
    }

    // 配置编码器上下文
    encoder_ctx = avcodec_alloc_context3(encoder);
    if (!encoder_ctx) {
        Logger::error("无法分配编码器上下文");
        av_buffer_unref(&hw_device_ctx);
        return false;
    }

    encoder_ctx->width = config.width;
    encoder_ctx->height = config.height;
    encoder_ctx->time_base = AVRational{1, config.fps};
    encoder_ctx->framerate = AVRational{config.fps, 1};
    encoder_ctx->gop_size = config.gop;
    encoder_ctx->pix_fmt = hw_pix_fmt;
    encoder_ctx->bit_rate = config.bitrate;

    // 低延迟设置
    if (low_latency) {
        encoder_ctx->max_b_frames = 0;  // 没有B帧
        encoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        encoder_ctx->rc_max_rate = config.bitrate;
        encoder_ctx->rc_buffer_size = config.bitrate / 2;
        encoder_ctx->thread_count = 1; // 单线程编码减少延迟
    } else {
        // 硬件编码器通常不支持B帧
        encoder_ctx->max_b_frames = 0;
    }

    // 创建硬件帧上下文
    AVBufferRef* hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ref) {
        Logger::error("无法创建硬件帧上下文");
        av_buffer_unref(&hw_device_ctx);
        avcodec_free_context(&encoder_ctx);
        return false;
    }

    AVHWFramesContext* frames_ctx = (AVHWFramesContext*)(hw_frames_ref->data);
    frames_ctx->format = hw_pix_fmt;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;  // 大多数硬件编码器支持NV12格式
    frames_ctx->width = config.width;
    frames_ctx->height = config.height;
    frames_ctx->initial_pool_size = 20;  // 初始帧池大小

    ret = av_hwframe_ctx_init(hw_frames_ref);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("无法初始化硬件帧上下文: " + std::string(err_buf));
        av_buffer_unref(&hw_frames_ref);
        av_buffer_unref(&hw_device_ctx);
        avcodec_free_context(&encoder_ctx);
        return false;
    }

    encoder_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    av_buffer_unref(&hw_frames_ref);

    // 打开编码器
    AVDictionary* opts = nullptr;
    if (hwaccel_type == HWAccelType::CUDA) {
        if (low_latency) {
            av_dict_set(&opts, "preset", "p1", 0);  // 最低延迟的CUDA预设
            av_dict_set(&opts, "tune", "ull", 0);   // 超低延迟调优
            av_dict_set(&opts, "delay", "0", 0);    // 无延迟
        } else {
            av_dict_set(&opts, "preset", "p4", 0);  // 平衡的CUDA预设
        }
    }

    ret = avcodec_open2(encoder_ctx, encoder, &opts);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        Logger::error("无法打开编码器: " + std::string(err_buf));
        av_dict_free(&opts);
        return false;
    }

    av_dict_free(&opts);
    initialized = true;

    Logger::debug("初始化硬件编码器成功: " + config.codec_name +
                  " (硬件加速: " + hwaccelTypeToString(hwaccel_type) + ")" +
                  " " + std::to_string(config.width) + "x" +
                  std::to_string(config.height) +
                  (low_latency ? " [低延迟模式]" : ""));

    return true;
}

// 编码帧
int HWEncoder::encode(AVFrame* frame, AVPacket* pkt) {
    if (!initialized || !encoder_ctx) {
        Logger::error("编码器未初始化");
        return AVERROR(EINVAL);
    }

    int ret;

    // 如果是软件帧且我们使用硬件编码，需要转换
    if (frame && hwaccel_type != HWAccelType::NONE &&
        frame->format != encoder_ctx->pix_fmt) {
        ret = swFrameToHwFrame(frame, hw_frame);
        if (ret < 0) {
            return ret;
        }
        frame = hw_frame;
    }

    ret = avcodec_send_frame(encoder_ctx, frame);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            Logger::error("编码发送帧错误: " + std::string(err_buf));
        }
        return ret;
    }

    ret = avcodec_receive_packet(encoder_ctx, pkt);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            Logger::error("编码接收数据包错误: " + std::string(err_buf));
        }
        return ret;
    }

    return 0;
}

// 刷新编码器
int HWEncoder::flush(AVPacket* pkt) {
    return encode(NULL, pkt);
}

// 获取编码器上下文
AVCodecContext* HWEncoder::getContext() {
    return encoder_ctx;
}

// 设置低延迟模式
void HWEncoder::setLowLatency(bool enable) {
    low_latency = enable;
    if (encoder_ctx) {
        if (enable) {
            encoder_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
            av_opt_set(encoder_ctx->priv_data, "tune", "zerolatency", 0);
        } else {
            encoder_ctx->flags &= ~AV_CODEC_FLAG_LOW_DELAY;
        }
    }
}