/**
 * @file stream_application.cpp
 * @brief 应用类实现
 */
#include "app/stream_application.h"
#include "logger/logger.h"
#include <csignal>
#include <thread>
#include <filesystem>

// 单例指针
static StreamApplication* app_instance = nullptr;

// 构造函数 - 私有
StreamApplication::StreamApplication(const std::string& config_path)
        : config_file(config_path), running(false) {
    stream_manager = std::make_shared<StreamManager>(config_path);
    app_instance = this;
}

// 析构函数
StreamApplication::~StreamApplication() {
    shutdown();
    app_instance = nullptr;
}

// 初始化应用
bool StreamApplication::init() {
    // 初始化信号处理
    setupSignalHandlers();

    // 初始化流管理器
    if (!stream_manager->init()) {
        return false;
    }

    running = true;
    return true;
}

// 运行应用
void StreamApplication::run() {
    if (!running) {
        if (!init()) {
            Logger::fatal("初始化应用失败");
            return;
        }
    }

    Logger::info("应用正在运行，按Ctrl+C退出");

    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// 关闭应用
void StreamApplication::shutdown() {
    if (!running) {
        return;
    }

    Logger::info("关闭应用...");
    running = false;

    if (stream_manager) {
        stream_manager->shutdown();
    }

    Logger::info("应用已关闭");
    Logger::shutdown();
}

// 处理信号
void StreamApplication::signalHandler(int signal) {
    Logger::warning("接收到信号: " + std::to_string(signal));

    if (signal == SIGINT || signal == SIGTERM) {
        if (app_instance) {
            app_instance->shutdown();
        }
    }
}

// 设置信号处理
void StreamApplication::setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

// 获取单例实例
StreamApplication& StreamApplication::getInstance() {
    static StreamApplication instance;
    return instance;
}

// 获取流管理器
std::shared_ptr<StreamManager> StreamApplication::getStreamManager() const {
    return stream_manager;
}

// 创建默认配置文件
bool createDefaultConfigFile(const std::string& filename) {
    // 如果文件已存在，不创建
    if (std::filesystem::exists(filename)) {
        return true;
    }

    // 创建默认配置
    SystemConfig config;
    config.worker_threads = 4;
    config.monitor_interval_ms = 1000;
    config.log_level = "info";
    config.log_file = "logs/stream.log";
    config.log_to_console = true;
    config.log_to_file = true;
    config.realtime_priority = true;

    // 示例拉流配置
    StreamConfig pull_config;
    pull_config.id = "sample-pull";
    pull_config.name = "示例拉流";
    pull_config.url = "rtsp://example.com/stream1";
    pull_config.type = StreamType::PULL;
    pull_config.hwaccel_type = HWAccelType::CUDA;
    pull_config.codec_name = "h264";
    pull_config.auto_reconnect = true;
    pull_config.low_latency = true;
    pull_config.max_queue_size = 5;

    // 示例推流配置
    StreamConfig push_config;
    push_config.id = "sample-push";
    push_config.name = "示例推流";
    push_config.url = "rtmp://example.com/live/stream1";
    push_config.type = StreamType::PUSH;
    push_config.hwaccel_type = HWAccelType::CUDA;
    push_config.width = 1920;
    push_config.height = 1080;
    push_config.bitrate = 4000000;
    push_config.fps = 30;
    push_config.codec_name = "h264";
    push_config.low_latency = true;
    push_config.max_queue_size = 5;

    // 添加示例配置（注释掉，避免自动加载）
    // config.streams.push_back(pull_config);
    // config.streams.push_back(push_config);

    // 创建注释，提供配置信息
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    file << "// 默认配置文件 - 请根据实际需求修改" << std::endl;
    file << "// worker_threads: 工作线程数" << std::endl;
    file << "// monitor_interval: 监控间隔(毫秒)" << std::endl;
    file << "// realtime_priority: 是否使用实时线程优先级" << std::endl;
    file << "// low_latency: 是否使用低延迟模式" << std::endl;
    file << "// max_queue_size: 帧队列大小(低延迟模式建议设置较小值)" << std::endl;
    file << std::endl;

    file << std::setw(4) << config.toJson() << std::endl;
    file.close();

    // 创建日志目录
    std::filesystem::create_directories("logs");

    return true;
}