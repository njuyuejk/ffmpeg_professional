/**
 * @file stream_manager.cpp
 * @brief 流管理器实现
 */

#include "ffmpeg_base/stream_manager.h"
#include "logger/logger.h"
#include "common/utils.h"

namespace ffmpeg_stream {

    StreamManager::StreamManager(size_t threadPoolSize)
            : nextStreamId_(0), monitorRunning_(false), monitorInterval_(5000) {

        // 初始化线程池
        threadPool_ = std::make_unique<ThreadPool>(threadPoolSize);

        // 注册所有编解码器和格式
        avformat_network_init();

        Logger::info("StreamManager initialized with thread pool size %zu", threadPoolSize);
    }

    StreamManager::~StreamManager() {
        stopAll();
        avformat_network_deinit();

        Logger::info("StreamManager destroyed");
    }

    bool StreamManager::initFromConfig(const std::string& configFilePath) {
        bool loaded = ConfigManager::getInstance().loadFromFile(configFilePath);

        if (!loaded) {
            Logger::error("Failed to load configuration from %s", configFilePath.c_str());
            return false;
        }

        const GlobalConfig& config = ConfigManager::getInstance().getConfig();

        // 设置日志级别
        Logger::setLogLevel(config.logLevel);

        // 调整线程池大小
        resizeThreadPool(config.threadPoolSize);

        // 启动监控
        startMonitoring(config.monitorInterval);

        // 初始化流
        for (const auto& streamConfig : config.streams) {
            int streamId;

            if (streamConfig.type == StreamType::PULL) {
                streamId = addPullStream(streamConfig);
            } else {
                streamId = addPushStream(streamConfig);
            }

            if (streamConfig.autoStart) {
                startStream(streamId);
            }
        }

        Logger::info("StreamManager initialized from config: %s", configFilePath.c_str());
        return true;
    }

    int StreamManager::getNextStreamId() {
        return nextStreamId_++;
    }

    int StreamManager::addPullStream(const StreamConfig& config,
                                     const StatusCallback& statusCb,
                                     const FrameCallback& frameCb) {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        int streamId = config.id >= 0 ? config.id : getNextStreamId();

        // 创建流处理器
        auto processor = std::make_shared<StreamProcessor>(
                streamId, config, statusCb, frameCb);

        streams_[streamId] = processor;

        Logger::info("Added pull stream %d: %s", streamId, config.name.c_str());
        return streamId;
    }

    int StreamManager::addPullStream(const std::string& url, const StatusCallback& statusCb,
                                     const FrameCallback& frameCb) {
        StreamConfig config;
        config.id = -1;  // 自动分配ID
        config.name = "Stream_" + std::to_string(nextStreamId_);
        config.type = StreamType::PULL;
        config.inputUrl = url;

        return addPullStream(config, statusCb, frameCb);
    }

    int StreamManager::addPushStream(const StreamConfig& config,
                                     const StatusCallback& statusCb) {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        int streamId = config.id >= 0 ? config.id : getNextStreamId();

        // 创建流处理器
        auto processor = std::make_shared<StreamProcessor>(
                streamId, config, statusCb);

        streams_[streamId] = processor;

        Logger::info("Added push stream %d: %s", streamId, config.name.c_str());
        return streamId;
    }

    int StreamManager::addPushStream(const std::string& inputUrl, const std::string& outputUrl,
                                     int width, int height, int bitrate, int fps,
                                     const StatusCallback& statusCb) {
        StreamConfig config;
        config.id = -1;  // 自动分配ID
        config.name = "Stream_" + std::to_string(nextStreamId_);
        config.type = StreamType::PUSH;
        config.inputUrl = inputUrl;
        config.outputUrl = outputUrl;
        config.width = width;
        config.height = height;
        config.bitrate = bitrate;
        config.fps = fps;

        return addPushStream(config, statusCb);
    }

    bool StreamManager::startStream(int streamId) {
        std::unique_lock<std::mutex> lock(streamsMutex_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) {
            Logger::error("Stream ID %d not found", streamId);
            return false;
        }

        auto processor = it->second;

        // 检查是否已有任务正在运行
        auto taskIt = streamTasks_.find(streamId);
        if (taskIt != streamTasks_.end()) {
            // 检查任务是否已完成
            auto& task = taskIt->second;
            if (task.valid()) {
                auto status = task.wait_for(std::chrono::milliseconds(0));
                if (status != std::future_status::ready) {
                    Logger::warning("Stream %d is already running", streamId);
                    return false;
                }
            }
        }

        // 启动流处理器
        if (!processor->start()) {
            return false;
        }

        // 在线程池中提交处理任务
        lock.unlock();  // 解锁以避免提交任务时死锁

        auto future = threadPool_->enqueue(
                // 高优先级任务
                TaskPriority::HIGH,
                // 传递StreamProcessor的共享指针到处理循环
                &StreamManager::streamProcessingLoop,
                this,
                processor
        );

        lock.lock();
        streamTasks_[streamId] = std::move(future);

        Logger::info("Started stream %d", streamId);
        return true;
    }

    bool StreamManager::stopStream(int streamId) {
        std::unique_lock<std::mutex> lock(streamsMutex_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) {
            Logger::error("Stream ID %d not found", streamId);
            return false;
        }

        auto processor = it->second;

        // 通知处理器停止
        processor->stop();

        // 移除任务
        auto taskIt = streamTasks_.find(streamId);
        if (taskIt != streamTasks_.end()) {
            streamTasks_.erase(taskIt);
        }

        Logger::info("Stopped stream %d", streamId);
        return true;
    }

    void StreamManager::stopAll() {
        std::vector<int> streamIds;

        {
            std::lock_guard<std::mutex> lock(streamsMutex_);
            for (const auto& pair : streams_) {
                streamIds.push_back(pair.first);
            }
        }

        for (int id : streamIds) {
            stopStream(id);
        }

        // 停止监控
        stopMonitoring();

        // 等待所有任务完成
        threadPool_->waitAll();

        Logger::info("All streams stopped");
    }

    StreamStatus StreamManager::getStreamStatus(int streamId) {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) {
            return StreamStatus::ERROR;
        }

        return it->second->getStatus();
    }

    StreamConfig StreamManager::getStreamConfig(int streamId) {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) {
            return StreamConfig();
        }

        return it->second->getConfig();
    }

    bool StreamManager::updateStreamConfig(int streamId, const StreamConfig& config) {
        std::lock_guard<std::mutex> lock(streamsMutex_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) {
            Logger::error("Stream ID %d not found", streamId);
            return false;
        }

        return it->second->updateConfig(config);
    }

    void StreamManager::resizeThreadPool(size_t numThreads) {
        threadPool_->resize(numThreads);
        Logger::info("Thread pool resized to %zu threads", numThreads);
    }

    size_t StreamManager::getThreadPoolSize() const {
        return threadPool_->size();
    }

    size_t StreamManager::getActiveThreads() const {
        return threadPool_->activeThreads();
    }

    void StreamManager::startMonitoring(int checkIntervalMs) {
        monitorInterval_ = checkIntervalMs;
        monitorRunning_ = true;
        monitorThread_ = std::thread([this]() {
            while (monitorRunning_) {
                checkStreams();
                std::this_thread::sleep_for(std::chrono::milliseconds(monitorInterval_));
            }
        });

        Logger::info("Started monitoring thread with interval %d ms", checkIntervalMs);
    }

    void StreamManager::stopMonitoring() {
        monitorRunning_ = false;
        if (monitorThread_.joinable()) {
            monitorThread_.join();
        }

        Logger::info("Stopped monitoring thread");
    }

    bool StreamManager::saveConfig(const std::string& filePath) {
        GlobalConfig& config = ConfigManager::getInstance().getConfig();

        // 更新线程池大小
        config.threadPoolSize = threadPool_->size();

        // 更新流配置
        config.streams.clear();
        {
            std::lock_guard<std::mutex> lock(streamsMutex_);
            for (const auto& pair : streams_) {
                config.streams.push_back(pair.second->getConfig());
            }
        }

        return ConfigManager::getInstance().saveToFile(filePath);
    }

    void StreamManager::streamProcessingLoop(std::shared_ptr<StreamProcessor> processor) {
        int streamId = processor->getId();
        StreamType type = processor->getConfig().type;

        Logger::debug("Stream processing loop started for stream %d", streamId);

        while (processor->getStatus() != StreamStatus::STOPPED) {
            bool continueProcessing = false;

            // 根据流类型调用不同的处理函数
            if (type == StreamType::PULL) {
                continueProcessing = processor->processPull();
            } else {
                continueProcessing = processor->processPush();
            }

            // 如果处理失败，尝试重连
            if (!continueProcessing) {
                if (processor->getStatus() != StreamStatus::STOPPED) {
                    if (!processor->handleReconnect()) {
                        // 重连失败，退出循环
                        break;
                    }
                } else {
                    // 已手动停止，退出循环
                    break;
                }
            }

            // 短暂休眠，避免CPU使用率过高
            // 对于实时流，应当保持非常短的休眠时间
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        Logger::debug("Stream processing loop ended for stream %d", streamId);
    }

    void StreamManager::checkStreams() {
        std::lock_guard<std::mutex> lock(streamsMutex_);

        for (auto& pair : streams_) {
            auto& processor = pair.second;

            if (processor->getStatus() == StreamStatus::CONNECTED) {
                // 检查是否超时
                if (processor->isTimeout(30)) {
                    Logger::warning("Stream %d timed out, attempting to reconnect", processor->getId());

                    // 停止当前流处理器
                    processor->stop();

                    // 在线程池中提交重连任务
                    threadPool_->enqueue(
                            TaskPriority::HIGH,
                            [this, processor]() {
                                if (processor->handleReconnect()) {
                                    // 重连成功，继续处理
                                    streamProcessingLoop(processor);
                                }
                            }
                    );
                }
            }
        }
    }

} // namespace ffmpeg_stream