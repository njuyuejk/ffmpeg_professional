/**
 * @file stream_manager.h
 * @brief 流管理器
 */

#ifndef FFMPEG_STREAM_MANAGER_H
#define FFMPEG_STREAM_MANAGER_H

#include "common/common.h"
#include "config/config.h"
#include "common/threadpool.h"
#include "ffmpeg_base/stream_processor.h"
#include <mutex>
#include <thread>
#include <map>
#include <atomic>
#include <chrono>
#include <memory>
#include <future>

namespace ffmpeg_stream {

/**
 * @class StreamManager
 * @brief 管理多个流的处理和监控
 *
 * 使用线程池处理多个流，确保实时性和低延迟
 */
    class StreamManager {
    public:
        /**
         * @brief 构造函数
         * @param threadPoolSize 线程池大小，默认为硬件并发线程数
         */
        explicit StreamManager(size_t threadPoolSize = std::thread::hardware_concurrency());

        /**
         * @brief 析构函数
         */
        ~StreamManager();

        /**
         * @brief 从配置初始化
         * @param configFilePath 配置文件路径
         * @return 是否成功初始化
         */
        bool initFromConfig(const std::string& configFilePath);

        /**
         * @brief 添加一个拉流
         * @param config 流配置
         * @param statusCb 状态回调
         * @param frameCb 帧回调
         * @return 流ID
         */
        int addPullStream(const StreamConfig& config,
                          const StatusCallback& statusCb = nullptr,
                          const FrameCallback& frameCb = nullptr);

        /**
         * @brief 添加一个拉流(传统接口，保持兼容)
         * @param url 输入URL
         * @param statusCb 状态回调
         * @param frameCb 帧回调
         * @return 流ID
         */
        int addPullStream(const std::string& url, const StatusCallback& statusCb = nullptr,
                          const FrameCallback& frameCb = nullptr);

        /**
         * @brief 添加一个推流
         * @param config 流配置
         * @param statusCb 状态回调
         * @return 流ID
         */
        int addPushStream(const StreamConfig& config,
                          const StatusCallback& statusCb = nullptr);

        /**
         * @brief 添加一个推流(传统接口，保持兼容)
         * @param inputUrl 输入URL
         * @param outputUrl 输出URL
         * @param width 宽度
         * @param height 高度
         * @param bitrate 码率
         * @param fps 帧率
         * @param statusCb 状态回调
         * @return 流ID
         */
        int addPushStream(const std::string& inputUrl, const std::string& outputUrl,
                          int width, int height, int bitrate, int fps,
                          const StatusCallback& statusCb = nullptr);

        /**
         * @brief 启动流
         * @param streamId 流ID
         * @return 是否成功启动
         */
        bool startStream(int streamId);

        /**
         * @brief 停止流
         * @param streamId 流ID
         * @return 是否成功停止
         */
        bool stopStream(int streamId);

        /**
         * @brief 停止所有流
         */
        void stopAll();

        /**
         * @brief 获取流状态
         * @param streamId 流ID
         * @return 流状态
         */
        StreamStatus getStreamStatus(int streamId);

        /**
         * @brief 获取流配置
         * @param streamId 流ID
         * @return 流配置
         */
        StreamConfig getStreamConfig(int streamId);

        /**
         * @brief 更新流配置
         * @param streamId 流ID
         * @param config 新配置
         * @return 是否成功更新
         */
        bool updateStreamConfig(int streamId, const StreamConfig& config);

        /**
         * @brief 调整线程池大小
         * @param numThreads 新的线程数
         */
        void resizeThreadPool(size_t numThreads);

        /**
         * @brief 获取线程池大小
         * @return 线程池大小
         */
        size_t getThreadPoolSize() const;

        /**
         * @brief 获取活跃线程数
         * @return 活跃线程数
         */
        size_t getActiveThreads() const;

        /**
         * @brief 启动监控线程
         * @param checkIntervalMs 检查间隔（毫秒）
         */
        void startMonitoring(int checkIntervalMs = 5000);

        /**
         * @brief 停止监控线程
         */
        void stopMonitoring();

        /**
         * @brief 保存当前配置到文件
         * @param filePath 文件路径
         * @return 是否成功保存
         */
        bool saveConfig(const std::string& filePath);

    private:
        // 流处理循环函数
        void streamProcessingLoop(std::shared_ptr<StreamProcessor> processor);

        // 检查流状态
        void checkStreams();

        // 获取下一个流ID
        int getNextStreamId();

    private:
        // 线程池
        std::unique_ptr<ThreadPool> threadPool_;

        // 流处理器映射
        std::mutex streamsMutex_;
        std::map<int, std::shared_ptr<StreamProcessor>> streams_;

        // 流处理任务映射
        std::map<int, std::future<void>> streamTasks_;

        // 流ID计数器
        std::atomic<int> nextStreamId_;

        // 监控线程控制
        std::atomic<bool> monitorRunning_;
        std::thread monitorThread_;
        int monitorInterval_;
    };

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_MANAGER_H