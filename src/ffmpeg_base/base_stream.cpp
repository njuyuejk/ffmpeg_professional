/**
 * @file base_stream.cpp
 * @brief 基础流类实现
 */
#include "ffmpeg_base/base_stream.h"
#include <sstream>
#include <iomanip>

// 构造函数
BaseStream::BaseStream(const std::string& id, const StreamConfig& cfg)
        : stream_id(id), config(cfg), state(StreamState::INIT),
          running(false), reconnect_count(0), fps_counter(0), frame_count(0) {
    last_active_time = std::chrono::steady_clock::now();
    fps_update_time = last_active_time;
}

// 析构函数
BaseStream::~BaseStream() {
    stop();
}

// 记录日志
void BaseStream::log(const std::string& message, LogLevel level) {
    std::string msg = "[Stream " + stream_id + "] " + message;
    switch (level) {
        case LogLevel::DEBUG: Logger::debug(msg); break;
        case LogLevel::INFO:  Logger::info(msg);  break;
        case LogLevel::WARNING:  Logger::warning(msg);  break;
        case LogLevel::ERROR: Logger::error(msg); break;
        case LogLevel::FATAL: Logger::fatal(msg); break;
    }
}

// 状态改变回调函数
void BaseStream::onStateChange(StreamState old_state, StreamState new_state) {
    log("状态变更: " + stateToString(old_state) + " -> " + stateToString(new_state));

    // 记录状态变更时间
    last_active_time = std::chrono::steady_clock::now();
}

// 更新FPS计数
void BaseStream::updateFps() {
    frame_count++;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - fps_update_time).count();

    if (elapsed >= 1) {
        fps_counter = static_cast<double>(frame_count) / elapsed;
        frame_count = 0;
        fps_update_time = now;
    }
}

// 设置状态
void BaseStream::setState(StreamState new_state) {
    StreamState old_state = state.exchange(new_state);
    if (old_state != new_state) {
        onStateChange(old_state, new_state);
    }
}

// 设置错误状态
void BaseStream::setError(const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    error_message = message;
    log("错误: " + message, LogLevel::ERROR);
    setState(StreamState::ERROR);
}

// 获取流ID
std::string BaseStream::getId() const {
    return stream_id;
}

// 获取流配置
const StreamConfig& BaseStream::getConfig() const {
    return config;
}

// 获取流状态
StreamState BaseStream::getState() const {
    return state;
}

// 获取错误信息
std::string BaseStream::getErrorMessage() const {
    return error_message;
}

// 获取状态信息
std::string BaseStream::getStatusInfo() const {
    return status_info;
}

// 获取当前FPS
double BaseStream::getFps() const {
    return fps_counter;
}

// 获取最后活跃时间（毫秒）
int64_t BaseStream::getLastActiveTimeMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_active_time).count();
}

// 停止流
void BaseStream::stop() {
    running = false;
    setState(StreamState::STOPPED);
}

// 重连
bool BaseStream::reconnect() {
    if (state == StreamState::STOPPED) {
        return false;
    }

    if (reconnect_count >= config.max_reconnect_attempts) {
        setError("达到最大重连次数限制");
        return false;
    }

    reconnect_count++;
    log("尝试重连 (" + std::to_string(reconnect_count) + "/" +
        std::to_string(config.max_reconnect_attempts) + ")", LogLevel::WARNING);
    setState(StreamState::RECONNECTING);

    // 实际重连逻辑由子类实现
    return true;
}

// 重置重连计数
void BaseStream::resetReconnectCount() {
    reconnect_count = 0;
}

// 转换为JSON对象用于状态报告
json BaseStream::toJson() const {

    json j = {
            {"id", stream_id},
            {"name", config.name.empty() ? stream_id : config.name},
            {"type", typeToString(config.type)},
            {"url", config.url},
            {"state", stateToString(state)},
            {"fps", fps_counter},
            {"last_active", getLastActiveTimeMs()},
            {"reconnect_count", reconnect_count},
            {"error", error_message}
    };

    return j;
}