/**
 * @file logger.cpp
 * @brief 日志系统实现 - 支持每日滚动日志文件（不使用filesystem库）
 */

#include "logger/logger.h"
#include "common/utils.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <sstream>

// 平台特定的头文件
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(dir) _mkdir(dir)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#define MKDIR(dir) mkdir(dir, 0777)
#endif

namespace ffmpeg_stream {

// 静态成员初始化
    LogLevel Logger::logLevel = LogLevel::INFO;
    bool Logger::initialized = false;

    Logger::Logger()
            : logToFile(false),
              logDirectory("logs"),
              logBaseName("ffmpeg_stream"),
              maxLogDays(30) {

        // 获取当前日期
        currentDate = getCurrentDateString();
    }

    Logger::~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    Logger& Logger::getInstance() {
        static Logger instance;
        return instance;
    }

    void Logger::setLogLevel(LogLevel level) {
        logLevel = level;
    }

    LogLevel Logger::getLogLevel() {
        return logLevel;
    }

    void Logger::setLogToFile(bool toFile, const std::string& logDir, const std::string& baseName, int maxDays) {
        Logger& instance = getInstance();
        std::lock_guard<std::mutex> lock(instance.logMutex);

        // 更新配置
        instance.logToFile = toFile;
        instance.logDirectory = logDir;
        instance.logBaseName = baseName;
        instance.maxLogDays = maxDays;

        // 如果需要关闭当前日志文件
        if (!toFile && instance.logFile.is_open()) {
            instance.logFile.close();
            return;
        }

        if (toFile) {
            // 确保日志目录存在
            if (!instance.createLogDirectory()) {
                std::cerr << "Failed to create log directory: " << logDir << std::endl;
                instance.logToFile = false;
                return;
            }

            // 获取当前日期并设置日志文件
            instance.currentDate = instance.getCurrentDateString();
            instance.currentLogFile = instance.getLogFilePath(instance.currentDate);

            // 打开日志文件
            if (instance.logFile.is_open()) {
                instance.logFile.close();
            }

            instance.logFile.open(instance.currentLogFile, std::ios::out | std::ios::app);
            if (!instance.logFile.is_open()) {
                std::cerr << "Failed to open log file: " << instance.currentLogFile << std::endl;
                instance.logToFile = false;
                return;
            }

            // 写入日志头
            instance.logFile << "=== Log started at " << utils::getCurrentTimeString() << " ===" << std::endl;

            // 清理旧日志文件
            instance.cleanOldLogFiles();
        }
    }

    void Logger::closeLogFile() {
        Logger& instance = getInstance();
        std::lock_guard<std::mutex> lock(instance.logMutex);

        if (instance.logFile.is_open()) {
            // 写入日志尾
            instance.logFile << "=== Log ended at " << utils::getCurrentTimeString() << " ===" << std::endl;
            instance.logFile.close();
        }

        instance.logToFile = false;
    }

    bool Logger::rollLogFile() {
        // 获取当前日期
        std::string newDate = getCurrentDateString();

        // 如果日期变化，创建新的日志文件
        if (newDate != currentDate) {
            // 关闭当前文件
            if (logFile.is_open()) {
                logFile << "=== Log ended at " << utils::getCurrentTimeString() << " ===" << std::endl;
                logFile.close();
            }

            // 更新日期和文件路径
            currentDate = newDate;
            currentLogFile = getLogFilePath(currentDate);

            // 打开新文件
            logFile.open(currentLogFile, std::ios::out | std::ios::app);
            if (!logFile.is_open()) {
                std::cerr << "Failed to open new log file: " << currentLogFile << std::endl;
                logToFile = false;
                return false;
            }

            // 写入日志头
            logFile << "=== Log started at " << utils::getCurrentTimeString() << " ===" << std::endl;

            // 清理旧日志文件
            cleanOldLogFiles();

            return true;
        }

        return false;
    }

    void Logger::cleanOldLogFiles() {
        try {
            // 获取所有日志文件
            std::vector<std::string> logFiles = getLogFiles();

            // 如果日志文件数量小于等于最大保留天数，不需要清理
            if (logFiles.size() <= static_cast<size_t>(maxLogDays)) {
                return;
            }

            // 按名称排序（日期格式可以按字母排序）
            std::sort(logFiles.begin(), logFiles.end());

            // 删除最老的文件，直到达到保留天数
            size_t filesToDelete = logFiles.size() - static_cast<size_t>(maxLogDays);
            for (size_t i = 0; i < filesToDelete; ++i) {
                const std::string& oldFile = logFiles[i];
                if (removeFile(oldFile)) {
                    std::cout << "Deleted old log file: " << oldFile << std::endl;
                } else {
                    std::cerr << "Failed to delete old log file: " << oldFile << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error cleaning old log files: " << e.what() << std::endl;
        }
    }

    bool Logger::createLogDirectory() {
        // 如果目录已存在，直接返回成功
        if (fileExists(logDirectory) && isDirectory(logDirectory)) {
            return true;
        }

        // 创建目录
        int result = MKDIR(logDirectory.c_str());
        return (result == 0);
    }

    bool Logger::fileExists(const std::string& path) {
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES);
#else
        struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
#endif
    }

    bool Logger::isDirectory(const std::string& path) {
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
        struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0 && S_ISDIR(buffer.st_mode));
#endif
    }

    std::string Logger::getCurrentDateString() {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        struct tm now_tm;
#ifdef _WIN32
        localtime_s(&now_tm, &now_time_t);
#else
        localtime_r(&now_time_t, &now_tm);
#endif

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << (now_tm.tm_year + 1900) << "-"
            << std::setfill('0') << std::setw(2) << (now_tm.tm_mon + 1) << "-"
            << std::setfill('0') << std::setw(2) << now_tm.tm_mday;
        return oss.str();
    }

    std::string Logger::getLogFilePath(const std::string& dateStr) {
        return logDirectory + "/" + logBaseName + "_" + dateStr + ".log";
    }

    std::vector<std::string> Logger::getLogFiles() {
        std::vector<std::string> result;

        // 检查目录是否存在
        if (!fileExists(logDirectory) || !isDirectory(logDirectory)) {
            return result;
        }

#ifdef _WIN32
        // Windows实现
        WIN32_FIND_DATAA findData;
        std::string searchPattern = logDirectory + "/*.*";

        HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) {
            return result;
        }

        do {
            // 跳过"."和".."
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
                continue;
            }

            // 检查是否是文件
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string filename = findData.cFileName;
                // 检查是否符合日志文件格式
                if (filename.find(logBaseName + "_") == 0 &&
                    filename.find(".log") != std::string::npos) {
                    result.push_back(logDirectory + "/" + filename);
                }
            }
        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);
#else
        // POSIX实现
    DIR* dir = opendir(logDirectory.c_str());
    if (dir == nullptr) {
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // 跳过"."和".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string filename = entry->d_name;
        std::string fullPath = logDirectory + "/" + filename;

        // 检查是否是文件
        struct stat statbuf;
        if (stat(fullPath.c_str(), &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
            // 检查是否符合日志文件格式
            if (filename.find(logBaseName + "_") == 0 &&
                filename.find(".log") != std::string::npos) {
                result.push_back(fullPath);
            }
        }
    }

    closedir(dir);
#endif

        return result;
    }

    bool Logger::removeFile(const std::string& filePath) {
#ifdef _WIN32
        return DeleteFileA(filePath.c_str()) != 0;
#else
        return unlink(filePath.c_str()) == 0;
#endif
    }

} // namespace ffmpeg_stream