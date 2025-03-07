/**
 * @file main.cpp
 * @brief FFmpeg多路流推拉系统主程序入口 - 无默认流
 */

#include "app/application.h"
#include <iostream>
#include <string>

using namespace ffmpeg_stream;

/**
 * @brief 程序入口点
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 应用程序退出码
 */
int main(int argc, char* argv[]) {
    try {
        // 获取配置文件路径（如果提供）
        std::string configPath = "D:\\project\\C++\\my\\ffmpeg_professional_git/config.json";

        if (argc > 1) {
            configPath = argv[1];
        }

        // 创建应用程序实例
        Application app(configPath);

        // 初始化应用程序
        if (!app.initialize()) {
            std::cerr << "Failed to initialize application" << std::endl;
            return 1;
        }

        std::cout << "FFmpeg Multi-Stream System initialized" << std::endl;
        std::cout << "Using configuration file: " << configPath << std::endl;
        std::cout << "Add streams in the configuration file to begin processing" << std::endl;

        // 运行应用程序
        return app.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}