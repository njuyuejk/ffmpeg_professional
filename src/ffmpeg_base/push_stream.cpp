/**
 * @file push_stream.cpp
 * @brief 推流类实现
 */
#include "ffmpeg_base/push_stream.h"
#include <sstream>
#include <cstring>

// 构造函数
PushStream::PushStream(const std::string& id, const StreamConfig& cfg)
        : BaseStream(id, cfg), encoder(nullptr) {
    config.type = StreamType::PUSH;
    video_stream_idx = -1;
    muxing_ready = false;
    next_pts = 0;
}

// 析构函数
PushStream::~PushStream() {
    stop();
}

// 初始化推流
bool PushStream::initStream() {
    avformat_network_init();

    // 创建输出上下文
    const char* output_format = NULL;

    // 根据URL确定输出格式
    if (strstr(config.url.c_str(), "rtmp://")) {
        output_format = "flv";
    } else if (strstr(config.url.c_str(), "rtsp://")) {
        output_format = "rtsp";
    } else if (strstr(config.url.c_str(), "udp://") ||
               strstr(config.url.c_str(), "rtp://")) {
        output_format = "mpegts";
    } else if (strstr(config.url.c_str(), ".mp4")) {
        output_format = "mp4";
    } else {
        setError("不支持的URL格式");
        return false;
    }

    setState(StreamState::CONNECTING);

    int ret = avformat_alloc_output_context2(&output_ctx, NULL,
                                             output_format, config.url.c_str());
    if (ret < 0 || !output_ctx) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        setError(std::string("无法创建输出上下文: ") + err_buf);
        return false;
    }

    // 创建视频流
    AVStream* stream = avformat_new_stream(output_ctx, NULL);
    if (!stream) {
        setError("无法创建视频流");
        avformat_free_context(output_ctx);
        output_ctx = nullptr;
        return false;
    }

    video_stream_idx = stream->index;

    // 创建编码器，启用低延迟模式
    encoder = std::make_unique<HWEncoder>(config.hwaccel_type, config.low_latency);
    if (!encoder->init(config)) {
        setError("无法初始化编码器");
        avformat_free_context(output_ctx);
        output_ctx = nullptr;
        return false;
    }

    // 设置流参数
    avcodec_parameters_from_context(stream->codecpar, encoder->getContext());
    stream->time_base = encoder->getContext()->time_base;

    // 更新状态信息
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::stringstream ss;
        ss << "视频: " << config.width << "x" << config.height << ", "
           << config.codec_name << ", " << config.bitrate / 1000 << "Kbps, "
           << config.fps << "fps";

        if (config.low_latency) {
            ss << " [低延迟模式]";
        }

        status_info = ss.str();
    }

    // 打开输出
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_ctx->pb, config.url.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            setError(std::string("无法打开输出文件: ") + err_buf);
            avformat_free_context(output_ctx);
            output_ctx = nullptr;
            return false;
        }
    }

    // 写入文件头
    AVDictionary* opts = NULL;
    if (strcmp(output_format, "rtsp") == 0) {
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    } else if (strcmp(output_format, "flv") == 0) {
        av_dict_set(&opts, "flvflags", "no_duration_filesize", 0);

        // 低延迟RTMP配置
        if (config.low_latency) {
            av_dict_set(&opts, "live", "1", 0);
        }
    }

    // 低延迟设置
    if (config.low_latency) {
        // 减少缓冲区大小
        av_dict_set(&opts, "fflags", "nobuffer", 0);
        // 更低的缓冲大小
        av_dict_set(&opts, "flush_packets", "1", 0);
    }

    ret = avformat_write_header(output_ctx, &opts);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        setError(std::string("无法写入文件头: ") + err_buf);

        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_ctx->pb);
        }

        avformat_free_context(output_ctx);
        output_ctx = nullptr;
        av_dict_free(&opts);
        return false;
    }

    av_dict_free(&opts);
    muxing_ready = true;
    setState(StreamState::CONNECTED);
    resetReconnectCount();
    next_pts = 0;
    log("推流连接成功: " + config.url, LogLevel::INFO);
    return true;
}

// 关闭推流
void PushStream::closeStream() {
    if (output_ctx && muxing_ready) {
        av_write_trailer(output_ctx);
        muxing_ready = false;
    }

    if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_ctx->pb);
    }

    if (output_ctx) {
        avformat_free_context(output_ctx);
        output_ctx = nullptr;
    }

    // 清空帧队列
    std::lock_guard<std::mutex> lock(queue_mutex);
    while (!frame_queue.empty()) {
        av_frame_free(&frame_queue.front());
        frame_queue.pop();
    }
}

// 推流线程函数
void PushStream::streamThread() {
    Logger::info("Push-" + stream_id);

    // 设置实时优先级（如果配置启用）
    if (config.low_latency) {
        return;
    }

    if (!initStream()) {
        return;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        setError("无法分配数据包");
        closeStream();
        return;
    }

    // 主循环
    while (running) {
        // 从队列获取帧
        AVFrame* frame = nullptr;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            if (frame_queue.empty()) {
                // 等待新帧，使用较短超时以便快速响应状态变化
                queue_cond.wait_for(lock, std::chrono::milliseconds(100),
                                    [this] { return !frame_queue.empty() || !running; });

                if (!running) {
                    break;
                }

                if (frame_queue.empty()) {
                    continue;
                }
            }

            frame = frame_queue.front();
            frame_queue.pop();
        }

        if (frame) {
            // 设置PTS
            frame->pts = next_pts++;

            // 编码帧
            int ret = encoder->encode(frame, pkt);
            av_frame_free(&frame);

            if (ret < 0) {
                if (ret != AVERROR(EAGAIN)) {
                    char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
                    log(std::string("编码错误: ") + err_buf, LogLevel::ERROR);
                }
                continue;
            }

            // 更新FPS计数
            updateFps();

            // 写入数据包
            pkt->stream_index = video_stream_idx;

            // 将时间基转换为输出流的时间基
            av_packet_rescale_ts(pkt, encoder->getContext()->time_base,
                                 output_ctx->streams[video_stream_idx]->time_base);

            ret = av_interleaved_write_frame(output_ctx, pkt);
            if (ret < 0) {
                char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
                log(std::string("写入帧错误: ") + err_buf, LogLevel::ERROR);

                setState(StreamState::DISCONNECTED);

                if (config.auto_reconnect) {
                    if (reconnect()) {
                        // 关闭当前流并等待重连延迟
                        closeStream();
                        std::this_thread::sleep_for(
                                std::chrono::milliseconds(config.reconnect_delay_ms));

                        // 重新初始化流
                        if (initStream()) {
                            continue;
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }

            // 低延迟模式下，立即刷新输出
            if (config.low_latency && output_ctx->pb) {
                avio_flush(output_ctx->pb);
            }

            // 更新活跃时间
            last_active_time = std::chrono::steady_clock::now();
        }
    }

    // 刷新编码器
    if (encoder && muxing_ready) {
        while (running) {
            int ret = encoder->flush(pkt);
            if (ret < 0) {
                break;
            }

            pkt->stream_index = video_stream_idx;

            // 将时间基转换为输出流的时间基
            av_packet_rescale_ts(pkt, encoder->getContext()->time_base,
                                 output_ctx->streams[video_stream_idx]->time_base);

            ret = av_interleaved_write_frame(output_ctx, pkt);
            if (ret < 0) {
                break;
            }
        }
    }

    av_packet_free(&pkt);
    closeStream();
}

// 启动推流
bool PushStream::start() {
    if (running) {
        return true;
    }

    running = true;
    stream_thread = std::thread(&PushStream::streamThread, this);
    return true;
}

// 停止推流
void PushStream::stop() {
    if (!running) {
        return;
    }

    running = false;

    // 唤醒可能阻塞的线程
    queue_cond.notify_all();

    if (stream_thread.joinable()) {
        stream_thread.join();
    }

    closeStream();
    BaseStream::stop();
}

// 发送帧
bool PushStream::sendFrame(AVFrame* frame) {
    if (!running || state != StreamState::CONNECTED) {
        return false;
    }

    // 复制帧
    AVFrame* frame_copy = av_frame_alloc();
    av_frame_ref(frame_copy, frame);

    std::unique_lock<std::mutex> lock(queue_mutex);

    // 低延迟模式且队列已满，清空队列
    if (config.low_latency && frame_queue.size() >= config.max_queue_size) {
        log("推流队列已满，丢弃旧帧以保证低延迟", LogLevel::DEBUG);
        while (!frame_queue.empty()) {
            av_frame_free(&frame_queue.front());
            frame_queue.pop();
        }
    }

    frame_queue.push(frame_copy);
    lock.unlock();
    queue_cond.notify_one();
    return true;
}

// 获取队列大小
int PushStream::getQueueSize() const {
    return frame_queue.size();
}

// 转换为JSON
json PushStream::toJson() const {
    json j = BaseStream::toJson();
    j["queue_size"] = getQueueSize();
    j["bitrate"] = config.bitrate / 1000;
    j["resolution"] = std::to_string(config.width) + "x" + std::to_string(config.height);
    j["fps_target"] = config.fps;
    j["low_latency"] = config.low_latency;

    return j;
}