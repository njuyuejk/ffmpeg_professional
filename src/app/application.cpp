/**
 * @file application.cpp
 * @brief 应用程序管理类实现 - 支持每日滚动日志
 */

#include "app/application.h"
#include "logger/logger.h"
#include "config/config.h"
#include "common/utils.h"
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <fstream>

extern "C" {
#include <libavformat/version.h>
#include <libavcodec/version.h>
#include <libavutil/version.h>
}

// 全局信号处理函数
extern "C" void signalHandler(int signal) {
    ffmpeg_stream::Application::handleSignal(signal);
}

namespace ffmpeg_stream {

// 初始化静态成员变量
    Application* Application::instance_ = nullptr;

// 处理信号的静态方法
    void Application::handleSignal(int signal) {
        if (instance_) {
            Logger::info("Received signal %d, stopping application...", signal);
            instance_->stop();
        }
    }

    Application::Application(const std::string& configPath)
            : running_(false),
              configFile_(configPath),
              logLevel_(LogLevel::INFO),
              logToFile_(false),
              logDirectory_("logs"),
              logBaseName_("ffmpeg_stream"),
              maxLogDays_(30),
              threadPoolSize_(std::thread::hardware_concurrency()),
              monitorInterval_(5000) {
        // 注册为全局实例，用于信号处理
        instance_ = this;
    }

    Application::~Application() {
        // 停止应用程序
        stop();

        // 关闭日志
        Logger::closeLogFile();

        // 清除全局实例
        if (instance_ == this) {
            instance_ = nullptr;
        }
    }

    bool Application::initialize() {
        // 初始化日志系统的默认设置
        Logger::setLogLevel(LogLevel::INFO);

        Logger::info("FFmpeg Multi-Stream System starting up...");

        // 设置信号处理
        setupSignalHandlers();

        // 尝试从配置文件初始化
        bool configLoaded = false;

        if (utils::fileExists(configFile_)) {
            Logger::info("Loading configuration from %s", configFile_.c_str());
            configLoaded = loadConfig(configFile_);
        } else {
            Logger::warning("Configuration file %s not found, creating default", configFile_.c_str());
            configLoaded = createDefaultConfig(configFile_);
            if (configLoaded) {
                configLoaded = loadConfig(configFile_);
            }
        }

        // 如果配置文件不存在或加载失败，使用默认配置
        if (!configLoaded) {
            Logger::warning("Using default configuration without streams");

            // 创建流管理器
            streamManager_ = std::make_unique<StreamManager>(threadPoolSize_);

            // 启动监控线程
            streamManager_->startMonitoring(monitorInterval_);
        }

        // 打印系统信息
        printSystemInfo();

        return true;
    }

    void Application::configureLogger(const json& configJson) {
        // 设置日志级别
        if (configJson.contains("logLevel")) {
            std::string levelStr = configJson["logLevel"];
            logLevel_ = stringToLogLevel(levelStr);
            Logger::setLogLevel(logLevel_);
        }

        // 日志文件设置
        logToFile_ = false;
        logDirectory_ = "logs";
        logBaseName_ = "ffmpeg_stream";
        maxLogDays_ = 30;

        if (configJson.contains("logToFile")) {
            logToFile_ = configJson["logToFile"];

            if (configJson.contains("logDirectory")) {
                logDirectory_ = configJson["logDirectory"];
            }

            if (configJson.contains("logBaseName")) {
                logBaseName_ = configJson["logBaseName"];
            }

            if (configJson.contains("maxLogDays")) {
                maxLogDays_ = configJson["maxLogDays"];
                if (maxLogDays_ <= 0) maxLogDays_ = 1;  // 至少保留1天
                if (maxLogDays_ > 365) maxLogDays_ = 365;  // 最多保留一年
            }
        }

        // 设置日志文件
        Logger::setLogToFile(logToFile_, logDirectory_, logBaseName_, maxLogDays_);

        if (logToFile_) {
            Logger::info("Log files will be stored in %s directory with base name %s, keeping %d days of history",
                         logDirectory_.c_str(), logBaseName_.c_str(), maxLogDays_);
        }
    }

    bool Application::loadConfig(const std::string& filePath) {
        if (!utils::fileExists(filePath)) {
            Logger::error("Configuration file does not exist: %s", filePath.c_str());
            return false;
        }

        try {
            // 读取配置文件
            std::ifstream file(filePath);
            if (!file.is_open()) {
                Logger::error("Failed to open configuration file: %s", filePath.c_str());
                return false;
            }

            // 解析JSON
            json configJson;
            file >> configJson;
            file.close();

            // 配置日志系统
            configureLogger(configJson);

            // 应用其他全局配置
            if (configJson.contains("threadPoolSize")) {
                threadPoolSize_ = configJson["threadPoolSize"];
            }

            if (configJson.contains("monitorInterval")) {
                monitorInterval_ = configJson["monitorInterval"];
            }

            // 创建或更新流管理器
            if (!streamManager_) {
                streamManager_ = std::make_unique<StreamManager>(threadPoolSize_);
                streamManager_->startMonitoring(monitorInterval_);
            } else {
                streamManager_->resizeThreadPool(threadPoolSize_);
                streamManager_->stopMonitoring();
                streamManager_->startMonitoring(monitorInterval_);
            }

            // 加载流配置
            if (configJson.contains("streams") && configJson["streams"].is_array()) {
                // 处理流配置
                for (const auto& streamJson : configJson["streams"]) {
                    StreamConfig config = StreamConfig::fromJson(streamJson);

                    // 检查是否已存在此ID的流
                    bool exists = false;
                    if (config.id >= 0) {
                        try {
                            StreamStatus status = streamManager_->getStreamStatus(config.id);
                            exists = (status != StreamStatus::ERROR);
                        } catch (...) {
                            exists = false;
                        }
                    }

                    if (exists) {
                        // 更新现有流配置
                        streamManager_->updateStreamConfig(config.id, config);

                        // 如果需要自动启动且未运行
                        if (config.autoStart &&
                            streamManager_->getStreamStatus(config.id) == StreamStatus::STOPPED) {
                            streamManager_->startStream(config.id);
                        }
                    } else {
                        // 添加新流
                        int streamId;
                        if (config.type == StreamType::PULL) {
                            streamId = streamManager_->addPullStream(config);
                        } else {
                            streamId = streamManager_->addPushStream(config);
                        }

                        // 如果需要自动启动
                        if (config.autoStart) {
                            streamManager_->startStream(streamId);
                        }
                    }
                }
            } else {
                Logger::info("No streams configured in the configuration file");
            }

            Logger::info("Configuration loaded successfully from %s", filePath.c_str());
            configFile_ = filePath;
            return true;
        } catch (const std::exception& e) {
            Logger::error("Failed to parse configuration file: %s", e.what());
            return false;
        }
    }

    bool Application::createDefaultConfig(const std::string& filePath) {
        try {
            // 创建默认配置
            json defaultConfig;

            // 全局设置
            defaultConfig["logLevel"] = "INFO";
            defaultConfig["logToFile"] = true;
            defaultConfig["logDirectory"] = "logs";
            defaultConfig["logBaseName"] = "ffmpeg_stream";
            defaultConfig["maxLogDays"] = 30;
            defaultConfig["monitorInterval"] = 5000;
            defaultConfig["threadPoolSize"] = std::thread::hardware_concurrency();
            defaultConfig["preloadLibraries"] = true;
            defaultConfig["defaultDecoderHWAccel"] = "CUDA";
            defaultConfig["defaultEncoderHWAccel"] = "CUDA";

            // 流配置 - 提供示例但默认不启用
            defaultConfig["streams"] = json::array();

            // 示例拉流
            json pullStream;
            pullStream["id"] = 0;
            pullStream["name"] = "ExampleCamera";
            pullStream["type"] = "PULL";
            pullStream["inputUrl"] = "rtsp://example.com/camera1";
            pullStream["autoStart"] = false;  // 默认不自动启动
            pullStream["maxReconnects"] = 10;
            pullStream["reconnectDelay"] = 3000;
            pullStream["decoderHWAccel"] = "CUDA";
            pullStream["networkTimeout"] = 5000;
            pullStream["rtspTransport"] = "tcp";
            pullStream["lowLatency"] = true;

            // 添加示例拉流配置
            defaultConfig["streams"].push_back(pullStream);

            // 示例推流
            json pushStream;
            pushStream["id"] = 1;
            pushStream["name"] = "ExampleRestream";
            pushStream["type"] = "PUSH";
            pushStream["inputUrl"] = "rtsp://example.com/camera1";
            pushStream["outputUrl"] = "rtmp://stream.example.com/live/camera1";
            pushStream["autoStart"] = false;  // 默认不自动启动
            pushStream["width"] = 1920;
            pushStream["height"] = 1080;
            pushStream["bitrate"] = 4000000;
            pushStream["fps"] = 30;
            pushStream["videoCodec"] = "h264";
            pushStream["decoderHWAccel"] = "CUDA";
            pushStream["encoderHWAccel"] = "CUDA";
            pushStream["maxReconnects"] = 10;
            pushStream["reconnectDelay"] = 3000;
            pushStream["networkTimeout"] = 5000;
            pushStream["rtspTransport"] = "tcp";
            pushStream["lowLatency"] = true;

            // 添加示例推流配置
            defaultConfig["streams"].push_back(pushStream);

            // 写入文件
            std::ofstream file(filePath);
            if (!file.is_open()) {
                Logger::error("Failed to create configuration file: %s", filePath.c_str());
                return false;
            }

            file << defaultConfig.dump(4); // 4空格缩进
            file.close();

            Logger::info("Default configuration file created: %s", filePath.c_str());
            return true;
        } catch (const std::exception& e) {
            Logger::error("Failed to create default configuration file: %s", e.what());
            return false;
        }
    }

    int Application::run() {
        // 标记应用程序为运行状态
        running_ = true;

        // 检查是否有流配置
        int streamCount = 0;
        try {
            // 获取StreamManager中的流数量
            // 这里我们需要实现一个获取流数量的方法，但当前StreamManager没有直接提供
            // 所以暂时使用一个简单的消息
            Logger::info("System running, press Ctrl+C to exit");
        } catch (const std::exception& e) {
            Logger::warning("No streams configured, system will remain idle");
        }

        // 主循环
        while (running_) {
            // 这里可以添加定期执行的任务，如状态报告等

            // 休眠以降低CPU使用率
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        Logger::info("Shutting down...");

        // 清理资源
        if (streamManager_) {
            // 停止所有流
            streamManager_->stopAll();

            // 保存配置以保留任何运行时更改
            streamManager_->saveConfig(configFile_);
        }

        // 关闭日志文件
        Logger::closeLogFile();

        return 0;
    }

    void Application::stop() {
        running_ = false;
    }

    bool Application::reload(const std::string& configPath) {
        std::string path = configPath.empty() ? configFile_ : configPath;

        if (!utils::fileExists(path)) {
            Logger::error("Configuration file does not exist: %s", path.c_str());
            return false;
        }

        Logger::info("Reloading configuration from %s", path.c_str());

        // 停止所有流
        if (streamManager_) {
            streamManager_->stopAll();
        }

        // 重新加载配置
        return loadConfig(path);
    }

    StreamManager& Application::getStreamManager() {
        if (!streamManager_) {
            throw std::runtime_error("StreamManager not initialized");
        }

        return *streamManager_;
    }

    std::string Application::getVersion() {
        return "1.0.0";
    }

    void Application::setupSignalHandlers() {
        std::signal(SIGINT, ::signalHandler);
        std::signal(SIGTERM, ::signalHandler);
    }

    void Application::printSystemInfo() {
        Logger::info("System information:");
        Logger::info("  CPU cores: %d", std::thread::hardware_concurrency());
        Logger::info("  Thread pool size: %d", threadPoolSize_);
        Logger::info("  Monitor interval: %d ms", monitorInterval_);

        if (logToFile_) {
            Logger::info("  Log files: %s/%s_*.log", logDirectory_.c_str(), logBaseName_.c_str());
            Logger::info("  Log retention: %d days", maxLogDays_);
        }

        // 打印可用的硬件加速类型
        std::vector<HWAccelType> availableTypes = getAvailableHWAccelTypes();
        std::string hwAccelStr = "  Available hardware acceleration: ";

        if (availableTypes.size() > 1) {  // 至少有一个是NONE
            for (auto type : availableTypes) {
                if (type != HWAccelType::NONE) {
                    hwAccelStr += hwAccelTypeToString(type) + ", ";
                }
            }

            if (hwAccelStr.size() > 2) {
                hwAccelStr = hwAccelStr.substr(0, hwAccelStr.size() - 2);  // 去除最后的逗号和空格
            }
        } else {
            hwAccelStr += "None";
        }

        Logger::info("%s", hwAccelStr.c_str());
        Logger::info("  Configuration file: %s", configFile_.c_str());
    }

} // namespace ffmpeg_stream