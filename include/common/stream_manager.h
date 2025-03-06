/**
 * @file stream_manager.h
 * @brief 流管理器头文件
 */
#pragma once

#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include "config/stream_types.h"
#include "threadpool.h"
#include "ffmpeg_base/pull_stream.h"
#include "ffmpeg_base/push_stream.h"
#include "stream_task.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

/**
 * @brief 流管理器类
 */
class StreamManager {
private:
    std::map<std::string, std::shared_ptr<BaseStream>> streams;
    std::map<int, std::shared_ptr<StreamTask>> tasks;
    std::mutex streams_mutex;
    std::mutex tasks_mutex;
    std::unique_ptr<ThreadPool> worker_pool;
    std::unique_ptr<ThreadPool> monitor_pool;
    std::string config_file;
    SystemConfig system_config;
    int next_task_id = 1;
    std::atomic<bool> running;

    /**
     * @brief 监控任务
     */
    void monitorTask();

    /**
     * @brief 开始监控线程
     */
    void startMonitor();

    /**
     * @brief 从JSON创建流
     * @param config 流配置
     * @return 流对象
     */
    std::shared_ptr<BaseStream> createStreamFromConfig(const StreamConfig& config);

    /**
     * @brief 获取当前ISO格式时间字符串
     * @return 时间字符串
     */
    std::string getCurrentISOTimeString();

    /**
     * @brief 获取运行时间字符串
     * @return 运行时间字符串
     */
    std::string getUptimeString();

public:
    /**
     * @brief 构造函数
     * @param config_path 配置文件路径
     */
    StreamManager(const std::string& config_path = "config.json");

    /**
     * @brief 析构函数
     */
    ~StreamManager();

    /**
     * @brief 初始化
     * @return 是否成功
     */
    bool init();

    /**
     * @brief 重新加载配置
     * @return 是否成功
     */
    bool reloadConfig();

    /**
     * @brief 创建拉流
     * @param config 流配置
     * @return 拉流对象
     */
    std::shared_ptr<PullStream> createPullStream(const StreamConfig& config);

    /**
     * @brief 创建推流
     * @param config 流配置
     * @return 推流对象
     */
    std::shared_ptr<PushStream> createPushStream(const StreamConfig& config);

    /**
     * @brief 获取流
     * @param id 流ID
     * @return 流对象
     */
    std::shared_ptr<BaseStream> getStream(const std::string& id);

    /**
     * @brief 获取拉流
     * @param id 流ID
     * @return 拉流对象
     */
    std::shared_ptr<PullStream> getPullStream(const std::string& id);

    /**
     * @brief 获取推流
     * @param id 流ID
     * @return 推流对象
     */
    std::shared_ptr<PushStream> getPushStream(const std::string& id);

    /**
     * @brief 移除流
     * @param id 流ID
     * @return 是否成功
     */
    bool removeStream(const std::string& id);

    /**
     * @brief 启动流
     * @param id 流ID
     * @return 是否成功
     */
    bool startStream(const std::string& id);

    /**
     * @brief 停止流
     * @param id 流ID
     * @return 是否成功
     */
    bool stopStream(const std::string& id);

    /**
     * @brief 创建转发任务
     * @param pull_id 拉流ID
     * @param push_id 推流ID
     * @param task_name 任务名称
     * @param zero_copy 是否启用零拷贝模式
     * @return 任务ID
     */
    int createForwardTask(const std::string& pull_id, const std::string& push_id,
                          const std::string& task_name = "", bool zero_copy = true);

    /**
     * @brief 启动任务
     * @param task_id 任务ID
     * @return 是否成功
     */
    bool startTask(int task_id);

    /**
     * @brief 停止任务
     * @param task_id 任务ID
     * @return 是否成功
     */
    bool stopTask(int task_id);

    /**
     * @brief 移除任务
     * @param task_id 任务ID
     * @return 是否成功
     */
    bool removeTask(int task_id);

    /**
     * @brief 获取任务
     * @param task_id 任务ID
     * @return 任务对象
     */
    std::shared_ptr<StreamTask> getTask(int task_id);

    /**
     * @brief 获取所有流
     * @return 流列表
     */
    std::vector<std::shared_ptr<BaseStream>> getAllStreams();

    /**
     * @brief 获取所有任务
     * @return 任务列表
     */
    std::vector<std::shared_ptr<StreamTask>> getAllTasks();

    /**
     * @brief 获取状态报告
     * @return JSON对象
     */
    json getStatusReport();

    /**
     * @brief 关闭管理器
     */
    void shutdown();
};