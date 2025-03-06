/**
 * @file stream_application.h
 * @brief 应用类头文件
 */
#pragma once

#include <string>
#include <memory>
#include <atomic>
#include "common/stream_manager.h"
#include "config/stream_types.h"

/**
 * @brief 流应用类
 */
class StreamApplication {
private:
    std::shared_ptr<StreamManager> stream_manager; ///< 流管理器
    std::string config_file;                      ///< 配置文件路径
    std::atomic<bool> running;                    ///< 运行标志

    StreamApplication(const std::string& config_path = "config.json");

public:
    /**
     * @brief 析构函数
     */
    ~StreamApplication();

    /**
     * @brief 初始化应用
     * @return 是否成功
     */
    bool init();

    /**
     * @brief 运行应用
     */
    void run();

    /**
     * @brief 关闭应用
     */
    void shutdown();

    /**
     * @brief 处理信号
     * @param signal 信号
     */
    static void signalHandler(int signal);

    /**
     * @brief 设置信号处理
     */
    static void setupSignalHandlers();

    /**
     * @brief 获取单例实例
     * @return 应用实例
     */
    static StreamApplication& getInstance();

    /**
     * @brief 获取流管理器
     * @return 流管理器
     */
    std::shared_ptr<StreamManager> getStreamManager() const;
};

/**
 * @brief 创建默认配置文件
 * @param filename 文件名
 * @return 是否成功
 */
bool createDefaultConfigFile(const std::string& filename);