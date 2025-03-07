/**
 * @file logger.h
 * @brief 日志系统 - 支持每日滚动日志文件（不使用filesystem库）
 */

#ifndef FFMPEG_STREAM_LOGGER_H
#define FFMPEG_STREAM_LOGGER_H

#include "common/common.h"
#include <string>
#include <fstream>
#include <mutex>
#include <ctime>
#include <vector>
#include <iostream>

namespace ffmpeg_stream {

// 日志系统类
    class Logger {
    public:
        // 设置日志级别
        static void setLogLevel(LogLevel level);

        // 获取当前日志级别
        static LogLevel getLogLevel();

        /**
         * @brief 设置输出到日志文件
         * @param toFile 是否输出到文件
         * @param logDir 日志目录路径
         * @param baseName 日志文件基本名称
         * @param maxDays 最大保留天数
         */
        static void setLogToFile(bool toFile,
                                 const std::string& logDir = "logs",
                                 const std::string& baseName = "ffmpeg_stream",
                                 int maxDays = 30);

        // 关闭日志文件
        static void closeLogFile();

        // 各级别日志接口
        template<typename... Args>
        static void debug(const char* format, Args... args) {
            if (logLevel <= LogLevel::DEBUG) {
                log(LogLevel::DEBUG, format, args...);
            }
        }

        template<typename... Args>
        static void info(const char* format, Args... args) {
            if (logLevel <= LogLevel::INFO) {
                log(LogLevel::INFO, format, args...);
            }
        }

        template<typename... Args>
        static void warning(const char* format, Args... args) {
            if (logLevel <= LogLevel::WARNING) {
                log(LogLevel::WARNING, format, args...);
            }
        }

        template<typename... Args>
        static void error(const char* format, Args... args) {
            if (logLevel <= LogLevel::ERROR) {
                log(LogLevel::ERROR, format, args...);
            }
        }

        template<typename... Args>
        static void fatal(const char* format, Args... args) {
            if (logLevel <= LogLevel::FATAL) {
                log(LogLevel::FATAL, format, args...);
            }
        }

    private:
        // 单例模式
        Logger();
        ~Logger();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        static Logger& getInstance();

        /**
         * @brief 滚动日志文件(检查日期并创建新文件)
         * @return 是否创建了新文件
         */
        bool rollLogFile();

        /**
         * @brief 清理旧日志文件
         */
        void cleanOldLogFiles();

        /**
         * @brief 创建日志目录
         * @return 是否成功创建
         */
        bool createLogDirectory();

        /**
         * @brief 检查文件或目录是否存在
         * @param path 路径
         * @return 是否存在
         */
        bool fileExists(const std::string& path);

        /**
         * @brief 检查路径是否是目录
         * @param path 路径
         * @return 是否是目录
         */
        bool isDirectory(const std::string& path);

        /**
         * @brief 获取当前日期字符串
         * @return 格式化的日期字符串 (YYYY-MM-DD)
         */
        std::string getCurrentDateString();

        /**
         * @brief 获取完整的日志文件路径
         * @param dateStr 日期字符串
         * @return 完整的文件路径
         */
        std::string getLogFilePath(const std::string& dateStr);

        /**
         * @brief 获取指定目录下的所有日志文件
         * @return 日志文件路径列表
         */
        std::vector<std::string> getLogFiles();

        /**
         * @brief 删除文件
         * @param filePath 文件路径
         * @return 是否成功删除
         */
        bool removeFile(const std::string& filePath);

        // 日志处理核心函数
        template<typename... Args>
        static void log(LogLevel level, const char* format, Args... args) {
            Logger& instance = getInstance();
            std::lock_guard<std::mutex> lock(instance.logMutex);

            // 如果启用了日志文件，检查是否需要滚动
            if (instance.logToFile) {
                instance.rollLogFile();
            }

            char buffer[1024];
            snprintf(buffer, sizeof(buffer), format, args...);

            const char* levelStr = logLevelToString(level).c_str();

            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            struct tm now_tm;
#ifdef _WIN32
            localtime_s(&now_tm, &now_time_t);
#else
            localtime_r(&now_time_t, &now_tm);
#endif

            char time_str[20];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &now_tm);

            // 构造日志消息
            std::string logMessage = std::string("[") + time_str + "] [" + levelStr + "]: " + buffer;

            // 输出到控制台
            std::cout << logMessage << std::endl;

            // 如果需要，输出到文件
            if (instance.logToFile && instance.logFile.is_open()) {
                instance.logFile << logMessage << std::endl;
                instance.logFile.flush();
            }
        }

        static LogLevel logLevel;
        static bool initialized;

        bool logToFile;
        std::string logDirectory;
        std::string logBaseName;
        int maxLogDays;
        std::string currentDate;
        std::string currentLogFile;
        std::ofstream logFile;
        std::mutex logMutex;
    };

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_LOGGER_H