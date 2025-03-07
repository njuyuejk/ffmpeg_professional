/**
 * @file config.h
 * @brief 配置系统
 */

#ifndef FFMPEG_STREAM_CONFIG_H
#define FFMPEG_STREAM_CONFIG_H

#include "common/common.h"
#include "ffmpeg_base/hw_accel.h"
#include <string>
#include <vector>
#include <map>

// JSON库头文件
#include "nlohmann/json.hpp"
using json = nlohmann::json;

namespace ffmpeg_stream {

// 流配置结构体
    struct StreamConfig {
        // 基本信息
        int id;
        std::string name;
        StreamType type;
        std::string inputUrl;
        std::string outputUrl;
        bool autoStart;

        // 重连设置
        int maxReconnects;
        int reconnectDelay;  // 毫秒

        // 视频参数
        int width;
        int height;
        int bitrate;
        int fps;
        std::string videoCodec;  // "h264", "h265", "vp9", etc.

        // 硬件加速
        HWAccelType decoderHWAccel;
        HWAccelType encoderHWAccel;

        // 网络设置
        int networkTimeout;  // 毫秒
        std::string rtspTransport;  // "tcp", "udp", "http", etc.
        bool lowLatency;

        // 其他选项
        std::map<std::string, std::string> extraOptions;

        // 默认构造函数
        StreamConfig();

        // 从JSON加载配置
        static StreamConfig fromJson(const json& j);

        // 转换为JSON
        json toJson() const;
    };

// 全局配置结构体
    struct GlobalConfig {
        // 日志设置
        LogLevel logLevel;
        bool logToFile;
        std::string logFilePath;

        // 监控设置
        int monitorInterval;  // 毫秒

        // 性能设置
        int threadPoolSize;
        bool preloadLibraries;

        // 默认硬件加速
        HWAccelType defaultDecoderHWAccel;
        HWAccelType defaultEncoderHWAccel;

        // 流列表
        std::vector<StreamConfig> streams;

        // 默认构造函数
        GlobalConfig();

        // 从JSON加载配置
        static GlobalConfig fromJson(const json& j);

        // 转换为JSON
        json toJson() const;
    };

// 配置管理器
    class ConfigManager {
    public:
        // 获取单例实例
        static ConfigManager& getInstance();

        // 从文件加载配置
        bool loadFromFile(const std::string& filePath);

        // 保存配置到文件
        bool saveToFile(const std::string& filePath);

        // 获取全局配置
        GlobalConfig& getConfig();

        // 设置全局配置
        void setConfig(const GlobalConfig& newConfig);

    private:
        // 单例模式
        ConfigManager() {}
        ConfigManager(const ConfigManager&) = delete;
        ConfigManager& operator=(const ConfigManager&) = delete;

        GlobalConfig config;
    };

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_CONFIG_H