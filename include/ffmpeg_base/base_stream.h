/**
 * @file base_stream.h
 * @brief 基础流类头文件
 */
#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include "config/stream_types.h"
#include "logger/logger.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

/**
 * @brief 基础流类，用于共享推流和拉流的公共功能
 */
class BaseStream {
protected:
    std::string stream_id;           ///< 流ID
    StreamConfig config;             ///< 流配置
    std::atomic<StreamState> state;  ///< 流状态
    std::atomic<bool> running;       ///< 运行标志
    std::string error_message;       ///< 错误信息
    int reconnect_count;             ///< 当前重连次数
    std::mutex mtx;                ///< 互斥锁
    std::string status_info;         ///< 状态信息
    std::chrono::steady_clock::time_point last_active_time; ///< 最后活跃时间
    double fps_counter;              ///< FPS计数器
    int frame_count;                 ///< 帧计数
    std::chrono::steady_clock::time_point fps_update_time; ///< FPS更新时间

    /**
     * @brief 记录日志
     * @param message 日志消息
     * @param level 日志级别
     */
    void log(const std::string& message, LogLevel level = LogLevel::INFO);

    /**
     * @brief 状态改变回调函数
     * @param old_state 旧状态
     * @param new_state 新状态
     */
    virtual void onStateChange(StreamState old_state, StreamState new_state);

    /**
     * @brief 更新FPS计数
     */
    void updateFps();

public:
    /**
     * @brief 构造函数
     * @param id 流ID
     * @param cfg 流配置
     */
    BaseStream(const std::string& id, const StreamConfig& cfg);

    /**
     * @brief 析构函数
     */
    virtual ~BaseStream();

    /**
     * @brief 设置状态
     * @param new_state 新状态
     */
    void setState(StreamState new_state);

    /**
     * @brief 设置错误状态
     * @param message 错误消息
     */
    void setError(const std::string& message);

    /**
     * @brief 获取流ID
     * @return 流ID
     */
    std::string getId() const;

    /**
     * @brief 获取流配置
     * @return 流配置
     */
    const StreamConfig& getConfig() const;

    /**
     * @brief 获取流状态
     * @return 流状态
     */
    StreamState getState() const;

    /**
     * @brief 获取错误信息
     * @return 错误信息
     */
    std::string getErrorMessage() const;

    /**
     * @brief 获取状态信息
     * @return 状态信息
     */
    virtual std::string getStatusInfo() const;

    /**
     * @brief 获取当前FPS
     * @return 当前FPS
     */
    double getFps() const;

    /**
     * @brief 获取最后活跃时间（毫秒）
     * @return 最后活跃时间
     */
    int64_t getLastActiveTimeMs() const;

    /**
     * @brief 启动流
     * @return 是否成功
     */
    virtual bool start() = 0;

    /**
     * @brief 停止流
     */
    virtual void stop();

    /**
     * @brief 重连
     * @return 是否成功
     */
    virtual bool reconnect();

    /**
     * @brief 重置重连计数
     */
    void resetReconnectCount();

    /**
     * @brief 转换为JSON对象用于状态报告
     * @return JSON对象
     */
    virtual json toJson() const;
};