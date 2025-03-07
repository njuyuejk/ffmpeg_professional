/**
 * @file common.h
 * @brief 通用定义、枚举和常量
 */

#ifndef FFMPEG_STREAM_COMMON_H
#define FFMPEG_STREAM_COMMON_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

extern "C" {
#include <libavutil/frame.h>
}

namespace ffmpeg_stream {

// 流状态枚举
    enum class StreamStatus {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING,
        ERROR,
        STOPPED
    };

// 流类型枚举
    enum class StreamType {
        PULL,  // 拉流
        PUSH   // 推流
    };

// 日志级别
    enum class LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        FATAL
    };

// 定义事件回调类型
    using StatusCallback = std::function<void(int streamId, StreamStatus status, const std::string& message)>;
    using FrameCallback = std::function<void(int streamId, AVFrame* frame)>;

// 将枚举转换为字符串
    std::string streamStatusToString(StreamStatus status);
    std::string streamTypeToString(StreamType type);
    std::string logLevelToString(LogLevel level);

// 将字符串转换为枚举
    StreamStatus stringToStreamStatus(const std::string& str);
    StreamType stringToStreamType(const std::string& str);
    LogLevel stringToLogLevel(const std::string& str);

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_COMMON_H