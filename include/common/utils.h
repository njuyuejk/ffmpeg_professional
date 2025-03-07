/**
 * @file utils.h
 * @brief 实用工具函数
 */

#ifndef FFMPEG_STREAM_UTILS_H
#define FFMPEG_STREAM_UTILS_H

#include <string>
#include <vector>

extern "C" {
#include <libavutil/error.h>
}

namespace ffmpeg_stream {
    namespace utils {

// 打印FFmpeg错误
        void printFFmpegError(const std::string& prefix, int errorCode);

// 检查文件是否存在
        bool fileExists(const std::string& filePath);

// 创建目录(如果不存在)
        bool createDirectory(const std::string& dirPath);

// 获取当前时间字符串
        std::string getCurrentTimeString(const std::string& format = "%Y-%m-%d %H:%M:%S");

// 将毫秒转换为可读时间字符串 (如 "01:23:45.678")
        std::string formatTime(int64_t milliseconds);

// URL编码/解码
        std::string urlEncode(const std::string& str);
        std::string urlDecode(const std::string& str);

// 分割字符串
        std::vector<std::string> splitString(const std::string& str, char delimiter);

// 替换字符串
        std::string replaceAll(std::string str, const std::string& from, const std::string& to);

// 检查字符串是否以特定前缀开始
        bool startsWith(const std::string& str, const std::string& prefix);

// 检查字符串是否以特定后缀结束
        bool endsWith(const std::string& str, const std::string& suffix);

// 转换字符串为大写
        std::string toUpper(const std::string& str);

// 转换字符串为小写
        std::string toLower(const std::string& str);

// 从文件名获取扩展名
        std::string getFileExtension(const std::string& filePath);

    } // namespace utils
} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_UTILS_H