/**
 * @file stream_manager.cpp
 * @brief 流管理器实现
 */
#include "common/stream_manager.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
}

// 构造函数
StreamManager::StreamManager(const std::string& config_path)
        : config_file(config_path), running(false) {
    // 初始化FFmpeg网络
    avformat_network_init();
}

// 析构函数
StreamManager::~StreamManager() {
    shutdown();
}

// 初始化
bool StreamManager::init() {
    // 加载配置
    system_config = SystemConfig::loadFromFile(config_file);

    // 初始化日志系统
    LogLevel log_level = LogLevel::INFO;
    if (system_config.log_level == "debug") log_level = LogLevel::DEBUG;
    else if (system_config.log_level == "info") log_level = LogLevel::INFO;
    else if (system_config.log_level == "warn") log_level = LogLevel::WARNING;
    else if (system_config.log_level == "error") log_level = LogLevel::ERROR;
    else if (system_config.log_level == "fatal") log_level = LogLevel::FATAL;

    // 初始化日志系统
    Logger::init(system_config.log_to_file, system_config.log_file, log_level);

    Logger::info("初始化流管理器，工作线程: " + std::to_string(system_config.worker_threads) +
                 ", 监控间隔: " + std::to_string(system_config.monitor_interval_ms) + "ms");

    if (system_config.realtime_priority) {
        Logger::info("已启用实时优先级模式");
    }

    // 创建线程池
    worker_pool = std::make_unique<ThreadPool>(system_config.worker_threads, "WorkerPool");

    // 启动监控线程
    running = true;
    startMonitor();

    // 加载所有预定义的流
    for (const auto& stream_config : system_config.streams) {
        auto stream = createStreamFromConfig(stream_config);
        if (stream) {
            streams[stream->getId()] = stream;
            Logger::info("加载流: " + stream->getId() + " (" +
                         typeToString(stream->getConfig().type) + ")");
        }
    }

    return true;
}

// 监控任务
void StreamManager::monitorTask() {
    // 检查所有流的状态
    std::vector<std::shared_ptr<BaseStream>> stream_list;
    {
        std::lock_guard<std::mutex> lock(streams_mutex);
        for (const auto& pair : streams) {
            stream_list.push_back(pair.second);
        }
    }

    for (auto& stream : stream_list) {
        if (stream->getState() == StreamState::DISCONNECTED) {
            if (stream->getConfig().auto_reconnect) {
                Logger::info("自动重连流: " + stream->getId());
                if (stream->reconnect()) {
                    // 重连延迟由流内部实现
                }
            }
        }

        // 检查长时间不活跃的流
        if (stream->getState() == StreamState::CONNECTED) {
            int64_t inactive_time = stream->getLastActiveTimeMs();
            // 超过5秒不活跃的流进行警告
            if (inactive_time > 5000) {
                Logger::warning("流长时间不活跃: " + stream->getId() +
                             " (" + std::to_string(inactive_time / 1000) + "秒)");
            }
        }
    }

    // 执行所有任务
    std::vector<std::shared_ptr<StreamTask>> task_list;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex);
        for (const auto& pair : tasks) {
            task_list.push_back(pair.second);
        }
    }

    for (auto& task : task_list) {
        if (task->isRunning()) {
            // 提交任务到工作线程池
            worker_pool->enqueue([task]() {
                task->execute();
            });
        }
    }
}

// 开始监控线程
void StreamManager::startMonitor() {
    if (!monitor_pool) {
        monitor_pool = std::make_unique<ThreadPool>(1, "MonitorPool");
    }

    // 定期执行监控任务
    monitor_pool->enqueue([this]() {
        Logger::info("Monitor");

        while (running) {
            try {
                monitorTask();
            } catch (const std::exception& e) {
                Logger::error("监控任务异常: " + std::string(e.what()));
            }

            std::this_thread::sleep_for(
                    std::chrono::milliseconds(system_config.monitor_interval_ms));
        }
    });
}

// 从配置创建流
std::shared_ptr<BaseStream> StreamManager::createStreamFromConfig(const StreamConfig& config) {
    if (config.type == StreamType::PULL) {
        return std::make_shared<PullStream>(config.id, config);
    } else if (config.type == StreamType::PUSH) {
        return std::make_shared<PushStream>(config.id, config);
    }

    Logger::error("不支持的流类型: " + config.id);
    return nullptr;
}

// 重新加载配置
bool StreamManager::reloadConfig() {
    Logger::info("重新加载配置文件: " + config_file);

    // 加载新配置
    SystemConfig new_config = SystemConfig::loadFromFile(config_file);

    // 更新系统配置
    if (new_config.worker_threads != system_config.worker_threads) {
        Logger::info("更新线程池大小: " + std::to_string(new_config.worker_threads));
        worker_pool->shutdown();
        worker_pool = std::make_unique<ThreadPool>(new_config.worker_threads, "WorkerPool");
    }

    if (new_config.log_level != system_config.log_level ||
        new_config.log_file != system_config.log_file ||
        new_config.log_to_console != system_config.log_to_console ||
        new_config.log_to_file != system_config.log_to_file) {

        LogLevel log_level = LogLevel::INFO;
        if (new_config.log_level == "debug") log_level = LogLevel::DEBUG;
        else if (new_config.log_level == "info") log_level = LogLevel::INFO;
        else if (new_config.log_level == "warn") log_level = LogLevel::WARNING;
        else if (new_config.log_level == "error") log_level = LogLevel::ERROR;
        else if (new_config.log_level == "fatal") log_level = LogLevel::FATAL;

        Logger::info("更新日志配置");
        Logger::init(new_config.log_to_file, new_config.log_file,
                     log_level);
    }

    // 处理流配置
    std::map<std::string, StreamConfig> new_streams;
    for (const auto& config : new_config.streams) {
        new_streams[config.id] = config;
    }

    // 停止和移除不再需要的流
    std::vector<std::string> streams_to_remove;
    {
        std::lock_guard<std::mutex> lock(streams_mutex);
        for (const auto& pair : streams) {
            if (new_streams.find(pair.first) == new_streams.end()) {
                Logger::info("移除流: " + pair.first);
                pair.second->stop();
                streams_to_remove.push_back(pair.first);
            }
        }

        for (const auto& id : streams_to_remove) {
            streams.erase(id);
        }
    }

    // 添加或更新流
    for (const auto& pair : new_streams) {
        std::lock_guard<std::mutex> lock(streams_mutex);
        auto it = streams.find(pair.first);

        if (it == streams.end()) {
            // 添加新流
            auto stream = createStreamFromConfig(pair.second);
            if (stream) {
                streams[stream->getId()] = stream;
                Logger::info("添加新流: " + stream->getId());
            }
        } else {
            // 检查配置是否改变
            const auto& old_config = it->second->getConfig();
            const auto& new_config = pair.second;

            // 如果关键配置改变，需要重新创建流
            if (old_config.url != new_config.url ||
                old_config.type != new_config.type ||
                old_config.hwaccel_type != new_config.hwaccel_type ||
                old_config.width != new_config.width ||
                old_config.height != new_config.height ||
                old_config.codec_name != new_config.codec_name) {

                Logger::info("更新流配置: " + pair.first);
                it->second->stop();
                auto stream = createStreamFromConfig(pair.second);
                if (stream) {
                    streams[stream->getId()] = stream;
                }
            }
        }
    }

    system_config = new_config;
    return true;
}

// 创建拉流
std::shared_ptr<PullStream> StreamManager::createPullStream(const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex);

    // 检查ID是否已存在
    if (streams.find(config.id) != streams.end()) {
        Logger::error("流ID已存在: " + config.id);
        return nullptr;
    }

    auto stream = std::make_shared<PullStream>(config.id, config);
    streams[config.id] = stream;

    // 保存到配置
    system_config.streams.push_back(config);
    system_config.saveToFile(config_file);

    return stream;
}

// 创建推流
std::shared_ptr<PushStream> StreamManager::createPushStream(const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex);

    // 检查ID是否已存在
    if (streams.find(config.id) != streams.end()) {
        Logger::error("流ID已存在: " + config.id);
        return nullptr;
    }

    auto stream = std::make_shared<PushStream>(config.id, config);
    streams[config.id] = stream;

    // 保存到配置
    system_config.streams.push_back(config);
    system_config.saveToFile(config_file);

    return stream;
}

// 获取流
std::shared_ptr<BaseStream> StreamManager::getStream(const std::string& id) {
    std::lock_guard<std::mutex> lock(streams_mutex);
    auto it = streams.find(id);
    if (it != streams.end()) {
        return it->second;
    }
    return nullptr;
}

// 获取拉流
std::shared_ptr<PullStream> StreamManager::getPullStream(const std::string& id) {
    auto stream = getStream(id);
    if (stream && stream->getConfig().type == StreamType::PULL) {
        return std::static_pointer_cast<PullStream>(stream);
    }
    return nullptr;
}

// 获取推流
std::shared_ptr<PushStream> StreamManager::getPushStream(const std::string& id) {
    auto stream = getStream(id);
    if (stream && stream->getConfig().type == StreamType::PUSH) {
        return std::static_pointer_cast<PushStream>(stream);
    }
    return nullptr;
}

// 移除流
bool StreamManager::removeStream(const std::string& id) {
    std::lock_guard<std::mutex> lock(streams_mutex);
    auto it = streams.find(id);
    if (it != streams.end()) {
        it->second->stop();
        streams.erase(it);

        // 从配置中删除
        auto& stream_configs = system_config.streams;
        for (auto it = stream_configs.begin(); it != stream_configs.end(); ++it) {
            if (it->id == id) {
                stream_configs.erase(it);
                break;
            }
        }
        system_config.saveToFile(config_file);

        return true;
    }
    return false;
}

// 启动流
bool StreamManager::startStream(const std::string& id) {
    auto stream = getStream(id);
    if (stream) {
        return stream->start();
    }
    return false;
}

// 停止流
bool StreamManager::stopStream(const std::string& id) {
    auto stream = getStream(id);
    if (stream) {
        stream->stop();
        return true;
    }
    return false;
}

// 创建转发任务
int StreamManager::createForwardTask(const std::string& pull_id, const std::string& push_id,
                                     const std::string& task_name, bool zero_copy) {
    auto pull_stream = getPullStream(pull_id);
    auto push_stream = getPushStream(push_id);

    if (!pull_stream || !push_stream) {
        Logger::error("创建转发任务失败: 无效的流ID");
        return -1;
    }

    std::string name = task_name;
    if (name.empty()) {
        name = "Forward-" + pull_id + "-to-" + push_id;
    }

    std::lock_guard<std::mutex> lock(tasks_mutex);
    int task_id = next_task_id++;
    auto task = std::make_shared<ForwardStreamTask>(task_id, name, pull_stream, push_stream, zero_copy);
    tasks[task_id] = task;

    Logger::info("创建转发任务: " + std::to_string(task_id) + " (" + name + ")" +
                 (zero_copy ? " [零拷贝模式]" : ""));
    return task_id;
}

// 启动任务
bool StreamManager::startTask(int task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex);
    auto it = tasks.find(task_id);
    if (it != tasks.end()) {
        return it->second->start();
    }
    return false;
}

// 停止任务
bool StreamManager::stopTask(int task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex);
    auto it = tasks.find(task_id);
    if (it != tasks.end()) {
        it->second->stop();
        return true;
    }
    return false;
}

// 移除任务
bool StreamManager::removeTask(int task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex);
    auto it = tasks.find(task_id);
    if (it != tasks.end()) {
        it->second->stop();
        tasks.erase(it);
        return true;
    }
    return false;
}

// 获取任务
std::shared_ptr<StreamTask> StreamManager::getTask(int task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex);
    auto it = tasks.find(task_id);
    if (it != tasks.end()) {
        return it->second;
    }
    return nullptr;
}

// 获取所有流
std::vector<std::shared_ptr<BaseStream>> StreamManager::getAllStreams() {
    std::lock_guard<std::mutex> lock(streams_mutex);
    std::vector<std::shared_ptr<BaseStream>> result;
    result.reserve(streams.size());
    for (const auto& pair : streams) {
        result.push_back(pair.second);
    }
    return result;
}

// 获取所有任务
std::vector<std::shared_ptr<StreamTask>> StreamManager::getAllTasks() {
    std::lock_guard<std::mutex> lock(tasks_mutex);
    std::vector<std::shared_ptr<StreamTask>> result;
    result.reserve(tasks.size());
    for (const auto& pair : tasks) {
        result.push_back(pair.second);
    }
    return result;
}

// 获取当前ISO格式时间字符串
std::string StreamManager::getCurrentISOTimeString() {
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

// 获取运行时间字符串
std::string StreamManager::getUptimeString() {
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

    int days = uptime / 86400;
    uptime %= 86400;
    int hours = uptime / 3600;
    uptime %= 3600;
    int minutes = uptime / 60;
    int seconds = uptime % 60;

    std::stringstream ss;
    if (days > 0) ss << days << "d ";
    ss << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setfill('0') << std::setw(2) << minutes << ":"
       << std::setfill('0') << std::setw(2) << seconds;

    return ss.str();
}

// 获取状态报告
json StreamManager::getStatusReport() {
    json report;

    // 系统信息
    report["system"] = {
            {"time", getCurrentISOTimeString()},
            {"uptime", getUptimeString()},
            {"worker_threads", system_config.worker_threads},
            {"worker_queue_size", worker_pool ? worker_pool->getQueueSize() : 0},
            {"worker_active_tasks", worker_pool ? worker_pool->getActiveTaskCount() : 0},
            {"realtime_priority", system_config.realtime_priority}
    };

    // 流信息
    json streams_json = json::array();
    auto all_streams = getAllStreams();
    for (const auto& stream : all_streams) {
        streams_json.push_back(stream->toJson());
    }
    report["streams"] = streams_json;

    // 任务信息
    json tasks_json = json::array();
    auto all_tasks = getAllTasks();
    for (const auto& task : all_tasks) {
        tasks_json.push_back(task->toJson());
    }
    report["tasks"] = tasks_json;

    return report;
}

// 关闭管理器
void StreamManager::shutdown() {
    if (!running) {
        return;
    }

    Logger::info("关闭流管理器...");
    running = false;

    // 停止所有任务
    {
        std::lock_guard<std::mutex> lock(tasks_mutex);
        for (auto& pair : tasks) {
            pair.second->stop();
        }
        tasks.clear();
    }

    // 停止所有流
    {
        std::lock_guard<std::mutex> lock(streams_mutex);
        for (auto& pair : streams) {
            pair.second->stop();
        }
        streams.clear();
    }

    // 关闭线程池
    if (monitor_pool) {
        monitor_pool->shutdown();
        monitor_pool.reset();
    }

    if (worker_pool) {
        worker_pool->shutdown();
        worker_pool.reset();
    }

    Logger::info("流管理器已关闭");
}