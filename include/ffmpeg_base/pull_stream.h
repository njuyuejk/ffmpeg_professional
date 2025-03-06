/**
 * @file pull_stream.h
 * @brief 拉流类头文件
 */
#pragma once

#include "base_stream.h"
#include "hw_decoder.h"
#include <thread>
#include <queue>
#include <condition_variable>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
}

/**
 * @brief 拉流类
 */
class PullStream : public BaseStream {
private:
    AVFormatContext* input_ctx = nullptr;
    std::unique_ptr<HWDecoder> decoder;
    std::thread stream_thread;
    std::queue<AVFrame*> frame_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cond;
    int video_stream_idx = -1;

    /**
     * @brief 初始化拉流
     * @return 是否成功
     */
    bool initStream();

    /**
     * @brief 关闭拉流
     */
    void closeStream();

    /**
     * @brief 拉流线程函数
     */
    void streamThread();

public:
    /**
     * @brief 构造函数
     * @param id 流ID
     * @param cfg 流配置
     */
    PullStream(const std::string& id, const StreamConfig& cfg);

    /**
     * @brief 析构函数
     */
    ~PullStream();

    /**
     * @brief 启动拉流
     * @return 是否成功
     */
    bool start() override;

    /**
     * @brief 停止拉流
     */
    void stop() override;

    /**
     * @brief 获取帧
     * @param timeout_ms 超时时间（毫秒）
     * @return 视频帧
     */
    AVFrame* getFrame(int timeout_ms = 0);

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