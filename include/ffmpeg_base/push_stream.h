/**
 * @file push_stream.h
 * @brief 推流类头文件
 */
#pragma once

#include "base_stream.h"
#include "hw_encoder.h"
#include <thread>
#include <queue>
#include <condition_variable>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
}

/**
 * @brief 推流类
 */
class PushStream : public BaseStream {
private:
    AVFormatContext* output_ctx = nullptr;
    std::unique_ptr<HWEncoder> encoder;
    std::thread stream_thread;
    std::queue<AVFrame*> frame_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cond;
    int video_stream_idx = -1;
    bool muxing_ready = false;
    int64_t next_pts = 0;

    /**
     * @brief 初始化推流
     * @return 是否成功
     */
    bool initStream();

    /**
     * @brief 关闭推流
     */
    void closeStream();

    /**
     * @brief 推流线程函数
     */
    void streamThread();

public:
    /**
     * @brief 构造函数
     * @param id 流ID
     * @param cfg 流配置
     */
    PushStream(const std::string& id, const StreamConfig& cfg);

    /**
     * @brief 析构函数
     */
    ~PushStream();

    /**
     * @brief 启动推流
     * @return 是否成功
     */
    bool start() override;

    /**
     * @brief 停止推流
     */
    void stop() override;

    /**
     * @brief 发送帧
     * @param frame 视频帧
     * @return 是否成功
     */
    bool sendFrame(AVFrame* frame);

    /**
     * @brief 获取队列大小
     * @return 队列大小
     */
    int getQueueSize() const;

    /**
     * @brief 转换为JSON
     * @return JSON对象
     */
    json toJson() const override;
};