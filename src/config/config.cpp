/**
 * @file config.cpp
 * @brief 配置系统实现
 */

#include "config/config.h"
#include "logger/logger.h"
#include <fstream>

namespace ffmpeg_stream {

// StreamConfig 实现
    StreamConfig::StreamConfig()
            : id(-1), type(StreamType::PULL), autoStart(false),
              maxReconnects(10), reconnectDelay(3000),
              width(1920), height(1080), bitrate(4000000), fps(30),
              videoCodec("h264"),
              decoderHWAccel(HWAccelType::CUDA), encoderHWAccel(HWAccelType::CUDA),
              networkTimeout(5000), rtspTransport("tcp"), lowLatency(true) {
    }

    StreamConfig StreamConfig::fromJson(const json& j) {
        StreamConfig config;

        if (j.contains("id")) config.id = j["id"];
        if (j.contains("name")) config.name = j["name"];
        if (j.contains("type")) config.type = j["type"] == "PUSH" ? StreamType::PUSH : StreamType::PULL;
        if (j.contains("inputUrl")) config.inputUrl = j["inputUrl"];
        if (j.contains("outputUrl")) config.outputUrl = j["outputUrl"];
        if (j.contains("outputFormat")) config.outputFormat = j["outputFormat"];
        if (j.contains("autoStart")) config.autoStart = j["autoStart"];

        if (j.contains("maxReconnects")) config.maxReconnects = j["maxReconnects"];
        if (j.contains("reconnectDelay")) config.reconnectDelay = j["reconnectDelay"];

        if (j.contains("width")) config.width = j["width"];
        if (j.contains("height")) config.height = j["height"];
        if (j.contains("bitrate")) config.bitrate = j["bitrate"];
        if (j.contains("fps")) config.fps = j["fps"];
        if (j.contains("videoCodec")) config.videoCodec = j["videoCodec"];

        if (j.contains("decoderHWAccel")) config.decoderHWAccel = stringToHWAccelType(j["decoderHWAccel"]);
        if (j.contains("encoderHWAccel")) config.encoderHWAccel = stringToHWAccelType(j["encoderHWAccel"]);

        if (j.contains("networkTimeout")) config.networkTimeout = j["networkTimeout"];
        if (j.contains("rtspTransport")) config.rtspTransport = j["rtspTransport"];
        if (j.contains("lowLatency")) config.lowLatency = j["lowLatency"];

        if (j.contains("extraOptions") && j["extraOptions"].is_object()) {
            for (auto& [key, value] : j["extraOptions"].items()) {
                config.extraOptions[key] = value;
            }
        }

        return config;
    }

    json StreamConfig::toJson() const {
        json j;

        j["id"] = id;
        j["name"] = name;
        j["type"] = type == StreamType::PUSH ? "PUSH" : "PULL";
        j["inputUrl"] = inputUrl;
        j["outputUrl"] = outputUrl;
        j["outputFormat"] = outputFormat;
        j["autoStart"] = autoStart;

        j["maxReconnects"] = maxReconnects;
        j["reconnectDelay"] = reconnectDelay;

        j["width"] = width;
        j["height"] = height;
        j["bitrate"] = bitrate;
        j["fps"] = fps;
        j["videoCodec"] = videoCodec;

        j["decoderHWAccel"] = hwAccelTypeToString(decoderHWAccel);
        j["encoderHWAccel"] = hwAccelTypeToString(encoderHWAccel);

        j["networkTimeout"] = networkTimeout;
        j["rtspTransport"] = rtspTransport;
        j["lowLatency"] = lowLatency;

        j["extraOptions"] = extraOptions;

        return j;
    }

// GlobalConfig 实现
    GlobalConfig::GlobalConfig()
            : logLevel(LogLevel::INFO), logToFile(false), logFilePath("ffmpeg_stream.log"),
              monitorInterval(5000), threadPoolSize(4), preloadLibraries(true),
              defaultDecoderHWAccel(HWAccelType::CUDA), defaultEncoderHWAccel(HWAccelType::CUDA) {
    }

    GlobalConfig GlobalConfig::fromJson(const json& j) {
        GlobalConfig config;

        if (j.contains("logLevel")) {
            std::string levelStr = j["logLevel"];
            config.logLevel = stringToLogLevel(levelStr);
        }

        if (j.contains("logToFile")) config.logToFile = j["logToFile"];
        if (j.contains("logFilePath")) config.logFilePath = j["logFilePath"];

        if (j.contains("monitorInterval")) config.monitorInterval = j["monitorInterval"];
        if (j.contains("threadPoolSize")) config.threadPoolSize = j["threadPoolSize"];
        if (j.contains("preloadLibraries")) config.preloadLibraries = j["preloadLibraries"];

        if (j.contains("defaultDecoderHWAccel"))
            config.defaultDecoderHWAccel = stringToHWAccelType(j["defaultDecoderHWAccel"]);
        if (j.contains("defaultEncoderHWAccel"))
            config.defaultEncoderHWAccel = stringToHWAccelType(j["defaultEncoderHWAccel"]);

        if (j.contains("streams") && j["streams"].is_array()) {
            for (const auto& streamJson : j["streams"]) {
                config.streams.push_back(StreamConfig::fromJson(streamJson));
            }
        }

        return config;
    }

    json GlobalConfig::toJson() const {
        json j;

        j["logLevel"] = logLevelToString(logLevel);
        j["logToFile"] = logToFile;
        j["logFilePath"] = logFilePath;

        j["monitorInterval"] = monitorInterval;
        j["threadPoolSize"] = threadPoolSize;
        j["preloadLibraries"] = preloadLibraries;

        j["defaultDecoderHWAccel"] = hwAccelTypeToString(defaultDecoderHWAccel);
        j["defaultEncoderHWAccel"] = hwAccelTypeToString(defaultEncoderHWAccel);

        j["streams"] = json::array();
        for (const auto& stream : streams) {
            j["streams"].push_back(stream.toJson());
        }

        return j;
    }

// ConfigManager 实现
    ConfigManager& ConfigManager::getInstance() {
        static ConfigManager instance;
        return instance;
    }

    bool ConfigManager::loadFromFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            Logger::error("Failed to open config file: %s", filePath.c_str());
            return false;
        }

        try {
            json j;
            file >> j;
            config = GlobalConfig::fromJson(j);

            // 应用日志级别
            Logger::setLogLevel(config.logLevel);
            if (config.logToFile) {
                Logger::setLogToFile(true, config.logFilePath);
            }

            Logger::info("Configuration loaded from %s", filePath.c_str());
            return true;
        } catch (const std::exception& e) {
            Logger::error("Failed to parse config file: %s", e.what());
            return false;
        }
    }

    bool ConfigManager::saveToFile(const std::string& filePath) {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            Logger::error("Failed to open file for writing: %s", filePath.c_str());
            return false;
        }

        try {
            json j = config.toJson();
            file << j.dump(4);  // 格式化输出，缩进4个空格
            Logger::info("Configuration saved to %s", filePath.c_str());
            return true;
        } catch (const std::exception& e) {
            Logger::error("Failed to save config file: %s", e.what());
            return false;
        }
    }

    GlobalConfig& ConfigManager::getConfig() {
        return config;
    }

    void ConfigManager::setConfig(const GlobalConfig& newConfig) {
        config = newConfig;

        // 应用日志级别
        Logger::setLogLevel(config.logLevel);
        if (config.logToFile) {
            Logger::setLogToFile(true, config.logFilePath);
        } else {
            Logger::closeLogFile();
        }
    }

} // namespace ffmpeg_stream