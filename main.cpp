/**
 * @file main.cpp
 * @brief 主函数实现
 */
#include "app/stream_application.h"
#include <iostream>
#include <string>

/**
 * @brief 主函数
 * @param argc 参数数量
 * @param argv 参数列表
 * @return 程序退出码
 */
int main(int argc, char* argv[]) {
    // 配置文件路径
    std::string config_file = "config.json";

    // 运行应用
    StreamApplication& app = StreamApplication::getInstance();
    app.run();

    return 0;
}