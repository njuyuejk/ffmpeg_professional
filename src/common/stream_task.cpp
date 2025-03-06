/**
 * @file stream_task.cpp
 * @brief 流任务类实现
 */
#include "common/stream_task.h"
#include "logger/logger.h"

extern "C" {
#include <libavutil/frame.h>
}

//================ StreamTask 基类实现 ================

// 构造函数
StreamTask::StreamTask(int id, const std::string& name)
        : task_id(id), task_name(name), is_running(false) {
    Logger::info("创建任务: " + name + " (ID: " + std::to_string(id) + ")");
}

// 析构函数
StreamTask::~StreamTask() {
    stop();
    Logger::debug("销毁任务: " + task_name + " (ID: " + std::to_string(task_id) + ")");
}

// 获取任务ID
int StreamTask::getId() const {
    return task_id;
}

// 获取任务名称
std::string StreamTask::getName() const {
    return task_name;
}

// 任务是否正在运行
bool StreamTask::isRunning() const {
    return is_running;
}

// 启动任务
bool StreamTask::start() {
    if (is_running) {
        Logger::debug("任务已在运行: " + task_name);
        return true;
    }

    Logger::info("启动任务: " + task_name);
    is_running = true;
    return true;
}

// 停止任务
void StreamTask::stop() {
    if (!is_running) {
        return;
    }

    Logger::info("停止任务: " + task_name);
    is_running = false;
}

// 转换为JSON
json StreamTask::toJson() const {
    json j = {
            {"id", task_id},
            {"name", task_name},
            {"running", is_running}
    };
    return j;
}

//================ ForwardStreamTask 实现 ================

// 构造函数
ForwardStreamTask::ForwardStreamTask(int id, const std::string& name,
                                     std::shared_ptr<PullStream> pull,
                                     std::shared_ptr<PushStream> push,
                                     bool zeroCopy)
        : StreamTask(id, name), pull_stream(pull), push_stream(push),
          frame_count(0), zero_copy_mode(zeroCopy) {
}

// 启动任务
bool ForwardStreamTask::start() {
    if (isRunning()) {
        return true;
    }

    if (!pull_stream || !push_stream) {
        Logger::error("转发任务缺少流: " + task_name);
        return false;
    }

    // 启动拉流和推流
    if (!pull_stream->start() || !push_stream->start()) {
        Logger::error("启动拉流或推流失败: " + task_name);
        return false;
    }

    return StreamTask::start();
}

// 停止任务
void ForwardStreamTask::stop() {
    if (!isRunning()) {
        return;
    }

    StreamTask::stop();

    // 停止拉流和推流
    if (pull_stream) pull_stream->stop();
    if (push_stream) push_stream->stop();
}

// 执行任务
void ForwardStreamTask::execute() {
    if (!isRunning()) {
        return;
    }

    // 检查流状态
    if (!pull_stream || !push_stream ||
        pull_stream->getState() != StreamState::CONNECTED ||
        push_stream->getState() != StreamState::CONNECTED) {
        return;
    }

    // 从拉流获取一帧，设置较短超时以提高实时性
    AVFrame* frame = pull_stream->getFrame(30);

    if (frame) {
        if (zero_copy_mode) {
            // 零拷贝模式，只传递引用
            if (push_stream->sendFrame(frame)) {
                frame_count++;
            }
        } else {
            // 标准模式，完全复制帧
            AVFrame* frame_copy = av_frame_alloc();
            if (frame_copy) {
                av_frame_ref(frame_copy, frame);

                if (push_stream->sendFrame(frame_copy)) {
                    frame_count++;
                }

                av_frame_unref(frame_copy);
                av_frame_free(&frame_copy);
            }
        }

        // 释放原始帧
        av_frame_free(&frame);
    }
}

// 获取处理的帧数
int ForwardStreamTask::getFrameCount() const {
    return frame_count;
}

// 设置零拷贝模式
void ForwardStreamTask::setZeroCopyMode(bool enable) {
    zero_copy_mode = enable;
    Logger::debug("任务 " + task_name + " 设置零拷贝模式: " +
                  (enable ? "启用" : "禁用"));
}

// 转换为JSON
json ForwardStreamTask::toJson() const {
    json j = StreamTask::toJson();
    j["frame_count"] = frame_count.load();
    j["zero_copy"] = zero_copy_mode;

    if (pull_stream) {
        j["pull_stream"] = pull_stream->getId();
        j["pull_state"] = stateToString(pull_stream->getState());
        j["pull_queue"] = pull_stream->getQueueSize();
        j["pull_fps"] = pull_stream->getFps();
    }

    if (push_stream) {
        j["push_stream"] = push_stream->getId();
        j["push_state"] = stateToString(push_stream->getState());
        j["push_queue"] = push_stream->getQueueSize();
        j["push_fps"] = push_stream->getFps();
    }

    return j;
}