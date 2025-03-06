/**
 * @file pull_stream.cpp
 * @brief 拉流类实现
 */
#include "ffmpeg_base/pull_stream.h"
#include <sstream>

extern "C" {
#include <libavutil/imgutils.h>
}

// 构造函数
PullStream::PullStream(const std::string& id, const StreamConfig& cfg)
        : BaseStream(id, cfg), decoder(nullptr) {
    config.type = StreamType::PULL;
    video_stream_idx = -1;
}

// 析构函数
PullStream::~PullStream() {
    stop();
}

// 初始化拉流
bool PullStream::initStream() {
    avformat_network_init();

    // 设置接收超时和网络选项
    AVDictionary* opts = nullptr;

    // 基本超时设置
    av_dict_set(&opts, "stimeout", "3000000", 0);  // 3秒超时(微秒)
    av_dict_set(&opts, "rtsp_transport", "tcp", 0); // 使用TCP传输RTSP

    // 低延迟模式下的附加优化
    if (config.low_latency) {
        // 减少缓冲
        av_dict_set(&opts, "buffer_size", "16384", 0);      // 较小的缓冲区
        av_dict_set(&opts, "max_delay", "500000", 0);       // 最大延迟500ms
        av_dict_set(&opts, "fflags", "nobuffer+flush_packets", 0); // 禁用缓冲，立即刷新
        av_dict_set(&opts, "reorder_queue_size", "0", 0);   // 禁用重排序队列
        av_dict_set(&opts, "rtsp_flags", "prefer_tcp", 0);   // 优先使用TCP
    }

    input_ctx = avformat_alloc_context();
    if (!input_ctx) {
        setError("无法分配输入上下文");
        return false;
    }

    setState(StreamState::CONNECTING);

    // 打开输入
    int ret = avformat_open_input(&input_ctx, config.url.c_str(), NULL, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        setError(std::string("无法打开输入: ") + err_buf);
        avformat_close_input(&input_ctx);
        return false;
    }

    // 获取流信息
    ret = avformat_find_stream_info(input_ctx, NULL);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        setError(std::string("无法获取流信息: ") + err_buf);
        avformat_close_input(&input_ctx);
        return false;
    }

    // 查找视频流
    for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {
        setError("找不到视频流");
        avformat_close_input(&input_ctx);
        return false;
    }

    // 更新状态信息
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::stringstream ss;
        ss << "视频: " << input_ctx->streams[video_stream_idx]->codecpar->width << "x"
           << input_ctx->streams[video_stream_idx]->codecpar->height << ", ";

        if (input_ctx->streams[video_stream_idx]->codecpar->codec_id == AV_CODEC_ID_H264) {
            ss << "H.264";
        } else if (input_ctx->streams[video_stream_idx]->codecpar->codec_id == AV_CODEC_ID_HEVC) {
            ss << "H.265";
        } else {
            ss << "编解码器ID: " << input_ctx->streams[video_stream_idx]->codecpar->codec_id;
        }

        status_info = ss.str();
    }

    // 创建解码器，启用低延迟模式
    decoder = std::make_unique<HWDecoder>(config.hwaccel_type, config.low_latency);
    if (!decoder->init(config.codec_name)) {
        setError("无法初始化解码器");
        avformat_close_input(&input_ctx);
        return false;
    }

    // 设置解码器参数
    if (!decoder->setParameters(input_ctx->streams[video_stream_idx]->codecpar)) {
        setError("无法设置解码器参数");
        avformat_close_input(&input_ctx);
        return false;
    }

    setState(StreamState::CONNECTED);
    resetReconnectCount();
    log("拉流连接成功: " + config.url, LogLevel::INFO);
    return true;
}

// 关闭拉流
void PullStream::closeStream() {
    if (input_ctx) {
        avformat_close_input(&input_ctx);
        input_ctx = nullptr;
    }

    // 清空帧队列
    std::lock_guard<std::mutex> lock(queue_mutex);
    while (!frame_queue.empty()) {
        av_frame_free(&frame_queue.front());
        frame_queue.pop();
    }
}

// 拉流线程函数
void PullStream::streamThread() {
    Logger::info("Pull-" + stream_id);

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
    int ret;
    while (running) {
        ret = av_read_frame(input_ctx, pkt);

        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                log("到达文件末尾", LogLevel::DEBUG);
                // 对于文件输入，我们可以寻回开始位置
                av_seek_frame(input_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
                continue;
            } else {
                char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
                log(std::string("读取帧错误: ") + err_buf, LogLevel::ERROR);

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
        }

        // 如果是视频包，解码它
        if (pkt->stream_index == video_stream_idx) {
            int got_frame = 0;
            AVFrame* frame = decoder->decode(pkt, got_frame);

            if (got_frame && frame) {
                // 更新FPS计数
                updateFps();

                // 复制帧，因为解码器会重用内部帧缓冲区
                AVFrame* frame_copy = av_frame_alloc();
                av_frame_move_ref(frame_copy, frame);

                std::unique_lock<std::mutex> lock(queue_mutex);

                // 如果队列已满
                if (frame_queue.size() >= config.max_queue_size && running) {
                    // 低延迟模式下，丢弃所有旧帧以确保最新帧能够得到处理
                    if (config.low_latency) {
                        log("队列已满，丢弃旧帧以保证低延迟", LogLevel::DEBUG);
                        while (!frame_queue.empty()) {
                            av_frame_free(&frame_queue.front());
                            frame_queue.pop();
                        }
                    } else {
                        // 标准模式下只丢弃最旧的帧
                        av_frame_free(&frame_queue.front());
                        frame_queue.pop();
                    }
                }

                // 如果还在运行，添加帧到队列
                if (running) {
                    frame_queue.push(frame_copy);
                    lock.unlock();
                    queue_cond.notify_one();
                } else {
                    av_frame_free(&frame_copy);
                }

                // 更新活跃时间
                last_active_time = std::chrono::steady_clock::now();
            }
        }

        av_packet_unref(pkt);
    }

    // 刷新解码器
    int got_frame = 0;
    while (running) {
        AVFrame* frame = decoder->flush(got_frame);
        if (!got_frame) break;

        if (frame) {
            AVFrame* frame_copy = av_frame_alloc();
            av_frame_move_ref(frame_copy, frame);

            std::lock_guard<std::mutex> lock(queue_mutex);
            frame_queue.push(frame_copy);
        }
    }

    av_packet_free(&pkt);
    closeStream();
}

// 启动拉流
bool PullStream::start() {
    if (running) {
        return true;
    }

    running = true;
    stream_thread = std::thread(&PullStream::streamThread, this);
    return true;
}

// 停止拉流
void PullStream::stop() {
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

// 获取帧
AVFrame* PullStream::getFrame(int timeout_ms) {
    std::unique_lock<std::mutex> lock(queue_mutex);

    if (frame_queue.empty()) {
        if (timeout_ms <= 0) {
            return nullptr;
        }

        bool success = queue_cond.wait_for(lock,
                                           std::chrono::milliseconds(timeout_ms),
                                           [this] { return !frame_queue.empty() || !running; });

        if (!success || !running || frame_queue.empty()) {
            return nullptr;
        }
    }

    AVFrame* frame = frame_queue.front();
    frame_queue.pop();

    // 更新活跃时间
    last_active_time = std::chrono::steady_clock::now();

    return frame;
}

// 获取队列大小
int PullStream::getQueueSize() const {
    return frame_queue.size();
}

// 转换为JSON
json PullStream::toJson() const {
    json j = BaseStream::toJson();
    j["queue_size"] = getQueueSize();
    j["resolution"] = input_ctx ?
                      std::to_string(input_ctx->streams[video_stream_idx]->codecpar->width) + "x" +
                      std::to_string(input_ctx->streams[video_stream_idx]->codecpar->height) : "未知";
    j["low_latency"] = config.low_latency;

    return j;
}