/**
 * @file common.cpp
 * @brief 通用定义的实现
 */

#include "common/common.h"

namespace ffmpeg_stream {

    std::string streamStatusToString(StreamStatus status) {
        switch (status) {
            case StreamStatus::DISCONNECTED: return "DISCONNECTED";
            case StreamStatus::CONNECTING: return "CONNECTING";
            case StreamStatus::CONNECTED: return "CONNECTED";
            case StreamStatus::RECONNECTING: return "RECONNECTING";
            case StreamStatus::ERROR: return "ERROR";
            case StreamStatus::STOPPED: return "STOPPED";
            default: return "UNKNOWN";
        }
    }

    std::string streamTypeToString(StreamType type) {
        switch (type) {
            case StreamType::PULL: return "PULL";
            case StreamType::PUSH: return "PUSH";
            default: return "UNKNOWN";
        }
    }

    std::string logLevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    StreamStatus stringToStreamStatus(const std::string& str) {
        if (str == "DISCONNECTED") return StreamStatus::DISCONNECTED;
        if (str == "CONNECTING") return StreamStatus::CONNECTING;
        if (str == "CONNECTED") return StreamStatus::CONNECTED;
        if (str == "RECONNECTING") return StreamStatus::RECONNECTING;
        if (str == "ERROR") return StreamStatus::ERROR;
        if (str == "STOPPED") return StreamStatus::STOPPED;
        return StreamStatus::DISCONNECTED;
    }

    StreamType stringToStreamType(const std::string& str) {
        if (str == "PUSH") return StreamType::PUSH;
        return StreamType::PULL;
    }

    LogLevel stringToLogLevel(const std::string& str) {
        if (str == "DEBUG") return LogLevel::DEBUG;
        if (str == "INFO") return LogLevel::INFO;
        if (str == "WARNING") return LogLevel::WARNING;
        if (str == "ERROR") return LogLevel::ERROR;
        if (str == "FATAL") return LogLevel::FATAL;
        return LogLevel::INFO;
    }

} // namespace ffmpeg_stream