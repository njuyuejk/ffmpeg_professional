/**
 * @file application.h
 * @brief 应用程序管理类 - 基于JSON配置文件，支持每日滚动日志
 */

#ifndef FFMPEG_STREAM_APPLICATION_H
#define FFMPEG_STREAM_APPLICATION_H

#include "ffmpeg_base/stream_manager.h"
#include <string>
#include <atomic>
#include <memory>

// 前向声明全局信号处理函数
extern "C" void signalHandler(int signal);

namespace ffmpeg_stream {

/**
 * @class Application
 * @brief 应用程序管理类，负责启动、配置和管理整个应用程序
 */
    class Application {
    public:
        /**
         * @brief 构造函数
         * @param configPath 配置文件路径，默认为"stream_config.json"
         */
        explicit Application(const std::string& configPath = "stream_config.json");

        /**
         * @brief 析构函数
         */
        ~Application();

        /**
         * @brief 初始化应用程序
         * @return 是否成功初始化
         */
        bool initialize();

        /**
         * @brief 运行应用程序
         * @return 应用程序退出码
         */
        int run();

        /**
         * @brief 停止应用程序
         */
        void stop();

        /**
         * @brief 重新加载配置
         * @param configPath 配置文件路径，为空则使用当前配置
         * @return 是否成功重新加载
         */
        bool reload(const std::string& configPath = "");

        /**
         * @brief 获取StreamManager实例
         * @return StreamManager实例引用
         */
        StreamManager& getStreamManager();

        /**
         * @brief 获取应用程序版本号
         * @return 版本号字符串
         */
        static std::string getVersion();

        /**
         * @brief 处理信号的静态方法
         * @param signal 信号编号
         */
        static void handleSignal(int signal);

    private:
        /**
         * @brief 加载配置文件
         * @param filePath 配置文件路径
         * @return 是否成功加载
         */
        bool loadConfig(const std::string& filePath);

        /**
         * @brief 创建默认配置文件
         * @param filePath 配置文件路径
         * @return 是否成功创建
         */
        bool createDefaultConfig(const std::string& filePath);

        /**
         * @brief 配置日志系统
         * @param configJson 配置JSON对象
         */
        void configureLogger(const json& configJson);

        /**
         * @brief 设置信号处理
         */
        void setupSignalHandlers();

        /**
         * @brief 打印系统信息
         */
        void printSystemInfo();

    private:
        // 运行状态
        std::atomic<bool> running_;

        // 配置文件路径
        std::string configFile_;

        // 日志级别
        LogLevel logLevel_;

        // 日志配置
        bool logToFile_;
        std::string logDirectory_;
        std::string logBaseName_;
        int maxLogDays_;

        // 线程池大小
        int threadPoolSize_;

        // 监控间隔
        int monitorInterval_;

        // 流管理器
        std::unique_ptr<StreamManager> streamManager_;

        // 静态实例，用于信号处理
        static Application* instance_;

        // 声明全局信号处理函数为友元
        friend void ::signalHandler(int signal);
    };

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_APPLICATION_H