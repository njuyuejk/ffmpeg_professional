/**
 * @file stream_types.h
 * @brief 流类型和配置头文件
 */
#pragma once

#include <string>
#include <vector>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

/**
 * @brief 流状态枚举
 */
enum class StreamState {
    INIT,           ///< 初始化
    CONNECTING,     ///< 连接中
    CONNECTED,      ///< 已连接
    DISCONNECTED,   ///< 断开连接
    RECONNECTING,   ///< 重连中
    ERROR,          ///< 错误
    STOPPED         ///< 已停止
};

/**
 * @brief 流类型枚举
 */
enum class StreamType {
    PULL,           ///< 拉流
    PUSH            ///< 推流
};

/**
 * @brief 硬件加速类型枚举
 */
enum class HWAccelType {
    NONE,           ///< 不使用硬件加速
    CUDA,           ///< NVIDIA GPU
    QSV,            ///< Intel Quick Sync Video
    VAAPI,          ///< Video Acceleration API (Linux)
    VIDEOTOOLBOX,   ///< Apple VideoToolbox
    DXVA2           ///< DirectX Video Acceleration (Windows)
};

/**
 * @brief 流状态转换为字符串
 * @param state 流状态
 * @return 状态字符串
 */
std::string stateToString(StreamState state);

/**
 * @brief 流类型转换为字符串
 * @param type 流类型
 * @return 类型字符串
 */
std::string typeToString(StreamType type);

/**
 * @brief 硬件加速类型转换为字符串
 * @param type 硬件加速类型
 * @return 类型字符串
 */
std::string hwaccelTypeToString(HWAccelType type);

/**
 * @brief 从字符串转换为硬件加速类型
 * @param type 类型字符串
 * @return 硬件加速类型
 */
HWAccelType hwaccelTypeFromString(const std::string& type);

/**
 * @brief 流配置结构体
 */
struct StreamConfig {
    std::string id;                 ///< 流标识
    std::string name;               ///< 流名称
    std::string url;                ///< 输入/输出URL
    StreamType type;                ///< 流类型
    HWAccelType hwaccel_type;       ///< 硬件加速类型
    int width = 1920;               ///< 视频宽度
    int height = 1080;              ///< 视频高度
    int bitrate = 4000000;          ///< 比特率
    int fps = 25;                   ///< 帧率
    int gop = 50;                   ///< GOP大小
    std::string codec_name = "h264"; ///< 编解码器名称
    int max_reconnect_attempts = 5; ///< 最大重连次数
    int reconnect_delay_ms = 2000;  ///< 重连延迟(毫秒)
    bool auto_reconnect = true;     ///< 是否自动重连
    bool low_latency = true;        ///< 低延迟模式
    int max_queue_size = 5;         ///< 最大队列大小 (低延迟模式下默认较小)

    /**
     * @brief 从JSON解析配置
     * @param j JSON对象
     * @return 流配置对象
     */
    static StreamConfig fromJson(const json& j);

    /**
     * @brief 转换为JSON
     * @return JSON对象
     */
    json toJson() const;
};

/**
 * @brief 系统配置结构体
 */
struct SystemConfig {
    int worker_threads = 4;            ///< 工作线程数
    int monitor_interval_ms = 1000;    ///< 监控间隔(毫秒)
    std::string log_level = "info";    ///< 日志级别
    std::string log_file = "";         ///< 日志文件路径
    bool log_to_console = true;        ///< 是否输出到控制台
    bool log_to_file = false;          ///< 是否输出到文件
    std::vector<StreamConfig> streams; ///< 流配置列表
    bool realtime_priority = true;     ///< 是否设置实时优先级

    /**
     * @brief 从JSON解析配置
     * @param j JSON对象
     * @return 系统配置对象
     */
    static SystemConfig fromJson(const json& j);

    /**
     * @brief 转换为JSON
     * @return JSON对象
     */
    json toJson() const;

    /**
     * @brief 保存配置到文件
     * @param filename 文件名
     * @return 是否成功
     */
    bool saveToFile(const std::string& filename) const;

    /**
     * @brief 从文件加载配置
     * @param filename 文件名
     * @return 系统配置对象
     */
    static SystemConfig loadFromFile(const std::string& filename);
};