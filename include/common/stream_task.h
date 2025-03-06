/**
 * @file stream_task.h
 * @brief 流任务类头文件
 */
#pragma once

#include <string>
#include <atomic>
#include <memory>
#include "nlohmann/json.hpp"
#include "ffmpeg_base/pull_stream.h"
#include "ffmpeg_base/push_stream.h"

using json = nlohmann::json;

/**
 * @brief 流任务基类
 */
class StreamTask {
protected:
    int task_id;                  ///< 任务ID
    std::string task_name;        ///< 任务名称
    std::atomic<bool> is_running; ///< 运行标志

public:
    /**
     * @brief 构造函数
     * @param id 任务ID
     * @param name 任务名称
     */
    StreamTask(int id, const std::string& name);

    /**
     * @brief 析构函数
     */
    virtual ~StreamTask();

    /**
     * @brief 获取任务ID
     * @return 任务ID
     */
    int getId() const;

    /**
     * @brief 获取任务名称
     * @return 任务名称
     */
    std::string getName() const;

    /**
     * @brief 任务是否正在运行
     * @return 是否运行
     */
    bool isRunning() const;

    /**
     * @brief 启动任务
     * @return 是否成功
     */
    virtual bool start();

    /**
     * @brief 停止任务
     */
    virtual void stop();

    /**
     * @brief 执行任务(由线程池调用)
     */
    virtual void execute() = 0;

    /**
     * @brief 转换为JSON
     * @return JSON对象
     */
    virtual json toJson() const;
};

/**
 * @brief 转发流任务类(拉流到推流)
 */
class ForwardStreamTask : public StreamTask {
private:
    std::shared_ptr<PullStream> pull_stream; ///< 拉流对象
    std::shared_ptr<PushStream> push_stream; ///< 推流对象
    std::atomic<int> frame_count;            ///< 帧计数
    bool zero_copy_mode;                     ///< 零拷贝模式

public:
    /**
     * @brief 构造函数
     * @param id 任务ID
     * @param name 任务名称
     * @param pull 拉流对象
     * @param push 推流对象
     * @param zeroCopy 是否使用零拷贝模式
     */
    ForwardStreamTask(int id, const std::string& name,
                      std::shared_ptr<PullStream> pull,
                      std::shared_ptr<PushStream> push,
                      bool zeroCopy = true);

    /**
     * @brief 启动任务
     * @return 是否成功
     */
    bool start() override;

    /**
     * @brief 停止任务
     */
    void stop() override;

    /**
     * @brief 执行任务
     */
    void execute() override;

    /**
     * @brief 获取处理的帧数
     * @return 帧数
     */
    int getFrameCount() const;

    /**
     * @brief 设置零拷贝模式
     * @param enable 是否启用
     */
    void setZeroCopyMode(bool enable);

    /**
     * @brief 转换为JSON
     * @return JSON对象
     */
    json toJson() const override;
};