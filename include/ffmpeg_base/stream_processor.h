/**
 * @file stream_processor.h
 * @brief 流处理器，处理单个流的编解码和转发
 */

#ifndef FFMPEG_STREAM_PROCESSOR_H
#define FFMPEG_STREAM_PROCESSOR_H

#include "common/common.h"
#include "config/config.h"
#include "decoder.h"
#include "encoder.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
}

namespace ffmpeg_stream {

/**
 * @class StreamProcessor
 * @brief 处理单个流的编解码和转发
 *
 * 该类负责单个流的处理逻辑，包括拉流和推流。
 * 与原始设计不同，处理逻辑从StreamManager移到此类，以便通过线程池调度。
 */
    class StreamProcessor {
    public:
        /**
         * @brief 构造函数
         * @param id 流ID
         * @param config 流配置
         * @param statusCallback 状态回调函数
         * @param frameCallback 帧回调函数（可选）
         */
        StreamProcessor(int id, const StreamConfig& config,
                        const StatusCallback& statusCallback = nullptr,
                        const FrameCallback& frameCallback = nullptr);

        /**
         * @brief 析构函数
         */
        ~StreamProcessor();

        /**
         * @brief 启动流处理
         * @return 是否成功启动
         */
        bool start();

        /**
         * @brief 停止流处理
         */
        void stop();

        /**
         * @brief 获取当前状态
         * @return 流状态
         */
        StreamStatus getStatus() const;

        /**
         * @brief 获取流ID
         * @return 流ID
         */
        int getId() const;

        /**
         * @brief 获取配置
         * @return 流配置
         */
        const StreamConfig& getConfig() const;

        /**
         * @brief 更新配置
         * @param config 新配置
         * @return 是否成功更新
         */
        bool updateConfig(const StreamConfig& config);

        /**
         * @brief 获取最后活动时间
         * @return 最后活动时间点
         */
        std::chrono::steady_clock::time_point getLastActiveTime() const;

        /**
         * @brief 处理一次拉流工作
         * @return 是否需要继续处理
         */
        bool processPull();

        /**
         * @brief 处理一次推流工作
         * @return 是否需要继续处理
         */
        bool processPush();

        /**
         * @brief 处理重连
         * @return 是否成功启动重连
         */
        bool handleReconnect();

        /**
         * @brief 检查是否超时
         * @param timeout 超时时间（秒）
         * @return 是否超时
         */
        bool isTimeout(int timeout = 30) const;

    private:
        // 设置流状态
        void setStatus(StreamStatus status, const std::string& message = "");

        // 打开输入
        bool openInput();

        // 打开输出（仅推流）
        bool openOutput();

        // 清理资源
        void cleanup();

    private:
        int id_;  // 流ID
        StreamConfig config_;  // 流配置
        std::atomic<StreamStatus> status_;  // 当前状态
        std::atomic<bool> running_;  // 是否正在运行

        // 回调函数
        StatusCallback statusCallback_;
        FrameCallback frameCallback_;

        // 重连计数
        int reconnectCount_;

        // 最后活动时间
        std::chrono::steady_clock::time_point lastActiveTime_;

        // FFmpeg上下文
        AVFormatContext* inputFormatContext_;
        AVFormatContext* outputFormatContext_;
        int videoStreamIndex_;

        // 解码器和编码器
        std::unique_ptr<HWDecoder> decoder_;
        std::unique_ptr<HWEncoder> encoder_;

        // 工作状态变量
        bool inputOpened_;
        bool outputOpened_;
        int64_t ptsOffset_;  // 用于PTS校正
    };

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_PROCESSOR_H