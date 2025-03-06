/**
 * @file stream_types.cpp
 * @brief 流类型和配置实现
 */
#include "config/stream_types.h"
#include "logger/logger.h"
#include <fstream>
#include <iostream>
#include <filesystem>

// 流状态转换为字符串
std::string stateToString(StreamState state) {
    switch (state) {
        case StreamState::INIT: return "初始化";
        case StreamState::CONNECTING: return "连接中";
        case StreamState::CONNECTED: return "已连接";
        case StreamState::DISCONNECTED: return "断开连接";
        case StreamState::RECONNECTING: return "重连中";
        case StreamState::ERROR: return "错误";
        case StreamState::STOPPED: return "已停止";
        default: return "未知";
    }
}

// 流类型转换为字符串
std::string typeToString(StreamType type) {
    switch (type) {
        case StreamType::PULL: return "拉流";
        case StreamType::PUSH: return "推流";
        default: return "未知";
    }
}

// 硬件加速类型转换为字符串
std::string hwaccelTypeToString(HWAccelType type) {
    switch (type) {
        case HWAccelType::NONE: return "none";
        case HWAccelType::CUDA: return "cuda";
        case HWAccelType::QSV: return "qsv";
        case HWAccelType::VAAPI: return "vaapi";
        case HWAccelType::VIDEOTOOLBOX: return "videotoolbox";
        case HWAccelType::DXVA2: return "dxva2";
        default: return "unknown";
    }
}

// 从字符串转换为硬件加速类型
HWAccelType hwaccelTypeFromString(const std::string& type) {
    if (type == "cuda") return HWAccelType::CUDA;
    if (type == "qsv") return HWAccelType::QSV;
    if (type == "vaapi") return HWAccelType::VAAPI;
    if (type == "videotoolbox") return HWAccelType::VIDEOTOOLBOX;
    if (type == "dxva2") return HWAccelType::DXVA2;
    return HWAccelType::NONE;
}

// 从JSON解析流配置
StreamConfig StreamConfig::fromJson(const json& j) {
    StreamConfig config;

    // 必需字段
    if (j.contains("id")) config.id = j["id"].get<std::string>();
    if (j.contains("url")) config.url = j["url"].get<std::string>();

    // 类型字段
    if (j.contains("type")) {
        std::string type_str = j["type"].get<std::string>();
        config.type = (type_str == "push") ? StreamType::PUSH : StreamType::PULL;
    }

    // 可选字段
    if (j.contains("name")) config.name = j["name"].get<std::string>();
    if (j.contains("hwaccel")) config.hwaccel_type = hwaccelTypeFromString(j["hwaccel"].get<std::string>());
    if (j.contains("width")) config.width = j["width"].get<int>();
    if (j.contains("height")) config.height = j["height"].get<int>();
    if (j.contains("bitrate")) config.bitrate = j["bitrate"].get<int>();
    if (j.contains("fps")) config.fps = j["fps"].get<int>();
    if (j.contains("gop")) config.gop = j["gop"].get<int>();
    if (j.contains("codec")) config.codec_name = j["codec"].get<std::string>();
    if (j.contains("max_reconnect")) config.max_reconnect_attempts = j["max_reconnect"].get<int>();
    if (j.contains("reconnect_delay")) config.reconnect_delay_ms = j["reconnect_delay"].get<int>();
    if (j.contains("auto_reconnect")) config.auto_reconnect = j["auto_reconnect"].get<bool>();
    if (j.contains("low_latency")) config.low_latency = j["low_latency"].get<bool>();
    if (j.contains("max_queue_size")) config.max_queue_size = j["max_queue_size"].get<int>();

    return config;
}

// 流配置转换为JSON
json StreamConfig::toJson() const {
    json j = {
            {"id", id},
            {"name", name.empty() ? id : name},
            {"url", url},
            {"type", (type == StreamType::PUSH) ? "push" : "pull"},
            {"hwaccel", hwaccelTypeToString(hwaccel_type)},
            {"width", width},
            {"height", height},
            {"bitrate", bitrate},
            {"fps", fps},
            {"gop", gop},
            {"codec", codec_name},
            {"max_reconnect", max_reconnect_attempts},
            {"reconnect_delay", reconnect_delay_ms},
            {"auto_reconnect", auto_reconnect},
            {"low_latency", low_latency},
            {"max_queue_size", max_queue_size}
    };
    return j;
}

// 从JSON解析系统配置
SystemConfig SystemConfig::fromJson(const json& j) {
    SystemConfig config;

    // 系统配置
    if (j.contains("system")) {
        const auto& sys = j["system"];
        if (sys.contains("worker_threads")) config.worker_threads = sys["worker_threads"].get<int>();
        if (sys.contains("monitor_interval")) config.monitor_interval_ms = sys["monitor_interval"].get<int>();
        if (sys.contains("realtime_priority")) config.realtime_priority = sys["realtime_priority"].get<bool>();

        // 日志配置
        if (sys.contains("log")) {
            const auto& log = sys["log"];
            if (log.contains("level")) config.log_level = log["level"].get<std::string>();
            if (log.contains("file")) config.log_file = log["file"].get<std::string>();
            if (log.contains("console")) config.log_to_console = log["console"].get<bool>();
            if (log.contains("file_output")) config.log_to_file = log["file_output"].get<bool>();
        }
    }

    // 流配置
    if (j.contains("streams") && j["streams"].is_array()) {
        for (const auto& stream_json : j["streams"]) {
            config.streams.push_back(StreamConfig::fromJson(stream_json));
        }
    }

    return config;
}

// 系统配置转换为JSON
json SystemConfig::toJson() const {
    json streams_json = json::array();
    for (const auto& stream : streams) {
        streams_json.push_back(stream.toJson());
    }

    json j = {
            {"system", {
                               {"worker_threads", worker_threads},
                               {"monitor_interval", monitor_interval_ms},
                               {"realtime_priority", realtime_priority},
                               {"log", {
                                               {"level", log_level},
                                               {"file", log_file},
                                               {"console", log_to_console},
                                               {"file_output", log_to_file}
                                       }}
                       }},
            {"streams", streams_json}
    };

    return j;
}

// 保存配置到文件
bool SystemConfig::saveToFile(const std::string& filename) const {
    try {
        // 确保目录存在
        std::filesystem::path path(filename);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream file(filename);
        if (!file.is_open()) {
            Logger::error("无法打开配置文件进行写入: " + filename);
            return false;
        }

        file << std::setw(4) << toJson() << std::endl;
        file.close();
        return true;
    } catch (const std::exception& e) {
        Logger::error("保存配置文件异常: " + std::string(e.what()));
        return false;
    }
}

// 从文件加载配置
SystemConfig SystemConfig::loadFromFile(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            Logger::error("无法打开配置文件: " + filename);
            return SystemConfig();
        }

        json j;
        file >> j;
        file.close();

        return fromJson(j);
    } catch (const std::exception& e) {
        Logger::error("加载配置文件异常: " + std::string(e.what()));
        return SystemConfig();
    }
}