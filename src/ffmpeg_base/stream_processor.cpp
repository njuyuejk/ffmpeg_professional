/**
 * @file stream_processor.cpp
 * @brief 流处理器实现
 */

#include "ffmpeg_base/stream_processor.h"
#include "logger/logger.h"
#include "common/utils.h"
#include <thread>

extern "C" {
#include <libavutil/time.h>
}

namespace ffmpeg_stream {

    StreamProcessor::StreamProcessor(int id, const StreamConfig& config,
                                     const StatusCallback& statusCallback,
                                     const FrameCallback& frameCallback)
            : id_(id),
              config_(config),
              status_(StreamStatus::DISCONNECTED),
              running_(false),
              statusCallback_(statusCallback),
              frameCallback_(frameCallback),
              reconnectCount_(0),
              lastActiveTime_(std::chrono::steady_clock::now()),
              inputFormatContext_(nullptr),
              outputFormatContext_(nullptr),
              videoStreamIndex_(-1),
              inputOpened_(false),
              outputOpened_(false),
              ptsOffset_(0) {
    }

    StreamProcessor::~StreamProcessor() {
        stop();
        cleanup();
    }

    bool StreamProcessor::start() {
        if (running_) {
            Logger::warning("Stream %d is already running", id_);
            return false;
        }

        running_ = true;
        reconnectCount_ = 0;
        setStatus(StreamStatus::CONNECTING);

        if (config_.type == StreamType::PULL) {
            // 对于拉流，只需要打开输入
            if (!openInput()) {
                return false;
            }
        } else {
            // 对于推流，需要打开输入和输出
            if (!openInput() || !openOutput()) {
                cleanup();
                return false;
            }
        }

        setStatus(StreamStatus::CONNECTED);
        return true;
    }

    void StreamProcessor::stop() {
        running_ = false;
        cleanup();
        setStatus(StreamStatus::STOPPED);
    }

    StreamStatus StreamProcessor::getStatus() const {
        return status_;
    }

    int StreamProcessor::getId() const {
        return id_;
    }

    const StreamConfig& StreamProcessor::getConfig() const {
        return config_;
    }

    bool StreamProcessor::updateConfig(const StreamConfig& config) {
        if (status_ != StreamStatus::DISCONNECTED &&
            status_ != StreamStatus::ERROR &&
            status_ != StreamStatus::STOPPED) {
            Logger::error("Cannot update config while stream is running");
            return false;
        }

        config_ = config;
        config_.id = id_;  // 确保ID不变

        Logger::info("Updated config for stream %d", id_);
        return true;
    }

    std::chrono::steady_clock::time_point StreamProcessor::getLastActiveTime() const {
        return lastActiveTime_;
    }

    bool StreamProcessor::processPull() {
        if (!running_ || status_ != StreamStatus::CONNECTED) {
            return false;
        }

        if (!inputOpened_) {
            if (!openInput()) {
                return false;
            }
        }

        // 读取一帧并处理
        AVPacket* packet = av_packet_alloc();
        int ret = av_read_frame(inputFormatContext_, packet);

        if (ret < 0) {
            av_packet_free(&packet);

            if (ret == AVERROR_EOF) {
                // 流结束
                setStatus(StreamStatus::DISCONNECTED, "Stream ended");
                return false;
            } else {
                utils::printFFmpegError("Error reading frame", ret);
                setStatus(StreamStatus::ERROR, "Error reading frame");
                return false;
            }
        }

        // 更新最后活动时间
        lastActiveTime_ = std::chrono::steady_clock::now();

        if (packet->stream_index == videoStreamIndex_) {
            // 解码视频帧
            AVFrame* frame = decoder_->decode(packet);
            if (frame) {
                // 如果有帧回调，调用它
                if (frameCallback_) {
                    frameCallback_(id_, frame);
                }
                av_frame_free(&frame);
            }
        }

        av_packet_unref(packet);
        av_packet_free(&packet);

        return true;
    }

    bool StreamProcessor::processPush() {
        if (!running_ || status_ != StreamStatus::CONNECTED) {
            return false;
        }

        if (!inputOpened_ || !outputOpened_) {
            if (!openInput() || !openOutput()) {
                return false;
            }
        }

        // 读取一帧并处理
        AVPacket* inPacket = av_packet_alloc();
        int ret = av_read_frame(inputFormatContext_, inPacket);

        if (ret < 0) {
            av_packet_free(&inPacket);

            if (ret == AVERROR_EOF) {
                // 流结束
                setStatus(StreamStatus::DISCONNECTED, "Stream ended");
                return false;
            } else {
                utils::printFFmpegError("Error reading frame", ret);
                setStatus(StreamStatus::ERROR, "Error reading frame");
                return false;
            }
        }

        // 更新最后活动时间
        lastActiveTime_ = std::chrono::steady_clock::now();

        if (inPacket->stream_index == videoStreamIndex_) {
            // 解码视频帧
            AVFrame* decodedFrame = decoder_->decode(inPacket);
            if (decodedFrame) {
                // 设置帧时间戳
                if (ptsOffset_ == 0) {
                    ptsOffset_ = decodedFrame->pts;
                }

                decodedFrame->pts = decodedFrame->pts - ptsOffset_;

                // 编码视频帧
                AVPacket* outPacket = encoder_->encode(decodedFrame);
                if (outPacket) {
                    // 调整输出包的时间戳
                    outPacket->stream_index = 0;

                    // 写入输出包
                    ret = av_interleaved_write_frame(outputFormatContext_, outPacket);
                    if (ret < 0) {
                        utils::printFFmpegError("Error writing frame", ret);
                        av_packet_free(&outPacket);
                        av_frame_free(&decodedFrame);
                        av_packet_unref(inPacket);
                        av_packet_free(&inPacket);
                        return false;
                    }

                    av_packet_free(&outPacket);
                }

                av_frame_free(&decodedFrame);
            }
        }

        av_packet_unref(inPacket);
        av_packet_free(&inPacket);

        return true;
    }

    bool StreamProcessor::handleReconnect() {
        if (!running_ || reconnectCount_ >= config_.maxReconnects) {
            setStatus(StreamStatus::STOPPED, "Max reconnect attempts reached");
            running_ = false;
            return false;
        }

        reconnectCount_++;
        setStatus(StreamStatus::RECONNECTING,
                  "Reconnecting... Attempt " + std::to_string(reconnectCount_));

        // 先清理资源
        cleanup();

        // 延迟重连
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnectDelay));

        // 重新启动
        return start();
    }

    bool StreamProcessor::isTimeout(int timeout) const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - lastActiveTime_).count();

        return elapsed > timeout;
    }

    void StreamProcessor::setStatus(StreamStatus status, const std::string& message) {
        status_ = status;
        lastActiveTime_ = std::chrono::steady_clock::now();

        Logger::info("Stream %d (%s) status changed to %s: %s",
                     id_, config_.name.c_str(),
                     streamStatusToString(status).c_str(), message.c_str());

        if (statusCallback_) {
            statusCallback_(id_, status, message);
        }
    }

    bool StreamProcessor::openInput() {
        // 检查是否已经打开
        if (inputOpened_ && inputFormatContext_) {
            return true;
        }

        // 清理之前的资源
        if (inputFormatContext_) {
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
        }

        // 打开输入流
        AVFormatContext* inputFormatContext = nullptr;
        AVDictionary* options = nullptr;

        // 设置网络超时选项
        int timeoutMicros = config_.networkTimeout * 1000;
//        av_dict_set_int(&options, "timeout", timeoutMicros, 0);
        av_dict_set_int(&options, "stimeout", timeoutMicros, 0);
        av_dict_set(&options, "rtsp_transport", config_.rtspTransport.c_str(), 0);

        // 增加探测大小和分析持续时间，解决"not enough frames to estimate rate"问题
        av_dict_set(&options, "probesize", "10485760", 0);     // 10MB (默认是5MB)
        av_dict_set(&options, "analyzeduration", "5000000", 0); // 5秒 (默认是0.5秒)

        // 应用额外选项
        for (const auto& [key, value] : config_.extraOptions) {
            av_dict_set(&options, key.c_str(), value.c_str(), 0);
        }

        int ret = avformat_open_input(&inputFormatContext, config_.inputUrl.c_str(), nullptr, &options);
        av_dict_free(&options);

        if (ret < 0) {
            utils::printFFmpegError("Failed to open input", ret);
            setStatus(StreamStatus::ERROR, "Failed to open input");
            return false;
        }

        inputFormatContext_ = inputFormatContext;

        // 获取流信息
        ret = avformat_find_stream_info(inputFormatContext_, nullptr);
        if (ret < 0) {
            utils::printFFmpegError("Failed to find stream info", ret);
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            setStatus(StreamStatus::ERROR, "Failed to find stream info");
            return false;
        }

        // 查找视频流
        videoStreamIndex_ = -1;
        for (unsigned int i = 0; i < inputFormatContext_->nb_streams; i++) {
            if (inputFormatContext_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIndex_ = i;
                break;
            }
        }

        if (videoStreamIndex_ == -1) {
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            setStatus(StreamStatus::ERROR, "No video stream found");
            return false;
        }

        // 初始化解码器
        decoder_ = std::make_unique<HWDecoder>();
        if (!decoder_->init(inputFormatContext_->streams[videoStreamIndex_]->codecpar,
                            config_.decoderHWAccel)) {
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            setStatus(StreamStatus::ERROR, "Failed to initialize decoder");
            return false;
        }

        inputOpened_ = true;
        return true;
    }

    bool StreamProcessor::openOutput() {
        // 只有推流模式需要输出
        if (config_.type != StreamType::PUSH) {
            return true;
        }

        // 检查是否已经打开
        if (outputOpened_ && outputFormatContext_) {
            return true;
        }

        // 检查输入是否已经打开
        if (!inputOpened_ || !inputFormatContext_) {
            Logger::error("Cannot open output when input is not opened");
            return false;
        }

        // 清理之前的资源
        if (outputFormatContext_) {
            if (outputFormatContext_->pb) {
                avio_closep(&outputFormatContext_->pb);
            }
            avformat_free_context(outputFormatContext_);
            outputFormatContext_ = nullptr;
        }

        // 创建输出格式上下文
        avformat_alloc_output_context2(&outputFormatContext_, nullptr, config_.outputFormat.c_str(),
                                       config_.outputUrl.c_str());
        if (!outputFormatContext_) {
            decoder_->cleanup();
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            inputOpened_ = false;
            setStatus(StreamStatus::ERROR, "Failed to create output context");
            return false;
        }

        // 初始化编码器
        encoder_ = std::make_unique<HWEncoder>();
        if (!encoder_->init(config_)) {
            decoder_->cleanup();
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            avformat_free_context(outputFormatContext_);
            outputFormatContext_ = nullptr;
            inputOpened_ = false;
            setStatus(StreamStatus::ERROR, "Failed to initialize encoder");
            return false;
        }

        // 创建输出流
        AVStream* outStream = avformat_new_stream(outputFormatContext_, nullptr);
        if (!outStream) {
            encoder_->cleanup();
            decoder_->cleanup();
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            avformat_free_context(outputFormatContext_);
            outputFormatContext_ = nullptr;
            inputOpened_ = false;
            setStatus(StreamStatus::ERROR, "Failed to create output stream");
            return false;
        }

        // 复制编码器参数到输出流
        int ret = avcodec_parameters_from_context(outStream->codecpar, encoder_->getCodecContext());
        if (ret < 0) {
            utils::printFFmpegError("Failed to copy encoder parameters", ret);
            encoder_->cleanup();
            decoder_->cleanup();
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            avformat_free_context(outputFormatContext_);
            outputFormatContext_ = nullptr;
            inputOpened_ = false;
            setStatus(StreamStatus::ERROR, "Failed to copy encoder parameters");
            return false;
        }

        // 如果输出格式需要全局头信息
        if (outputFormatContext_->oformat->flags & AVFMT_GLOBALHEADER) {
            outputFormatContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // 打开输出URL
        if (!(outputFormatContext_->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&outputFormatContext_->pb, config_.outputUrl.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                utils::printFFmpegError("Failed to open output file", ret);
                encoder_->cleanup();
                decoder_->cleanup();
                avformat_close_input(&inputFormatContext_);
                inputFormatContext_ = nullptr;
                avformat_free_context(outputFormatContext_);
                outputFormatContext_ = nullptr;
                inputOpened_ = false;
                setStatus(StreamStatus::ERROR, "Failed to open output file");
                return false;
            }
        }

        // 写入流头信息
        AVDictionary* options = nullptr;
        ret = avformat_write_header(outputFormatContext_, &options);
        av_dict_free(&options);

        if (ret < 0) {
            utils::printFFmpegError("Failed to write header", ret);
            if (!(outputFormatContext_->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&outputFormatContext_->pb);
            }
            encoder_->cleanup();
            decoder_->cleanup();
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
            avformat_free_context(outputFormatContext_);
            outputFormatContext_ = nullptr;
            inputOpened_ = false;
            setStatus(StreamStatus::ERROR, "Failed to write header");
            return false;
        }

        outputOpened_ = true;
        ptsOffset_ = 0;  // 重置PTS偏移
        return true;
    }

    void StreamProcessor::cleanup() {
        // 清理输出资源
        if (outputFormatContext_) {
            // 若已打开输出，写入流尾部
            if (outputOpened_) {
                av_write_trailer(outputFormatContext_);
            }

            if (!(outputFormatContext_->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&outputFormatContext_->pb);
            }

            avformat_free_context(outputFormatContext_);
            outputFormatContext_ = nullptr;
        }

        // 清理编码器
        if (encoder_) {
            encoder_->cleanup();
        }

        // 清理解码器
        if (decoder_) {
            decoder_->cleanup();
        }

        // 清理输入资源
        if (inputFormatContext_) {
            avformat_close_input(&inputFormatContext_);
            inputFormatContext_ = nullptr;
        }

        inputOpened_ = false;
        outputOpened_ = false;
    }

} // namespace ffmpeg_stream