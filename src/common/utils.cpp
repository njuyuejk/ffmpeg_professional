/**
 * @file utils.cpp
 * @brief 实用工具函数实现
 */

#include "common/utils.h"
#include "logger/logger.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

namespace ffmpeg_stream {
    namespace utils {

        void printFFmpegError(const std::string& prefix, int errorCode) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(errorCode, errBuf, AV_ERROR_MAX_STRING_SIZE);
            Logger::error("%s: %s", prefix.c_str(), errBuf);
        }

        bool fileExists(const std::string& filePath) {
            std::ifstream file(filePath);
            return file.good();
        }

        bool createDirectory(const std::string& dirPath) {
            int status = mkdir(dirPath.c_str(), 0777);
            if (status != 0 && errno != EEXIST) {
                Logger::error("Failed to create directory %s: %s", dirPath.c_str(), strerror(errno));
                return false;
            }
            return true;
        }

        std::string getCurrentTimeString(const std::string& format) {
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            struct tm now_tm;
#ifdef _WIN32
            localtime_s(&now_tm, &now_time_t);
#else
            localtime_r(&now_time_t, &now_tm);
#endif

            char time_str[100];
            strftime(time_str, sizeof(time_str), format.c_str(), &now_tm);
            return std::string(time_str);
        }

        std::string formatTime(int64_t milliseconds) {
            int ms = milliseconds % 1000;
            int seconds = (milliseconds / 1000) % 60;
            int minutes = (milliseconds / (1000 * 60)) % 60;
            int hours = (milliseconds / (1000 * 60 * 60));

            std::stringstream ss;
            ss << std::setfill('0') << std::setw(2) << hours << ":"
               << std::setfill('0') << std::setw(2) << minutes << ":"
               << std::setfill('0') << std::setw(2) << seconds << "."
               << std::setfill('0') << std::setw(3) << ms;
            return ss.str();
        }

        std::string urlEncode(const std::string& str) {
            std::ostringstream escaped;
            escaped.fill('0');
            escaped << std::hex;

            for (char c : str) {
                if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    escaped << c;
                } else {
                    escaped << '%' << std::setw(2) << int((unsigned char)c);
                }
            }

            return escaped.str();
        }

        std::string urlDecode(const std::string& str) {
            std::string result;
            result.reserve(str.size());

            for (std::size_t i = 0; i < str.size(); ++i) {
                if (str[i] == '%') {
                    if (i + 2 < str.size()) {
                        int value;
                        std::istringstream is(str.substr(i + 1, 2));
                        if (is >> std::hex >> value) {
                            result += static_cast<char>(value);
                            i += 2;
                        } else {
                            result += '%';
                        }
                    } else {
                        result += '%';
                    }
                } else if (str[i] == '+') {
                    result += ' ';
                } else {
                    result += str[i];
                }
            }

            return result;
        }

        std::vector<std::string> splitString(const std::string& str, char delimiter) {
            std::vector<std::string> tokens;
            std::string token;
            std::istringstream tokenStream(str);

            while (std::getline(tokenStream, token, delimiter)) {
                tokens.push_back(token);
            }

            return tokens;
        }

        std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
            return str;
        }

        bool startsWith(const std::string& str, const std::string& prefix) {
            return str.size() >= prefix.size() &&
                   str.compare(0, prefix.size(), prefix) == 0;
        }

        bool endsWith(const std::string& str, const std::string& suffix) {
            return str.size() >= suffix.size() &&
                   str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::string toUpper(const std::string& str) {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            return result;
        }

        std::string toLower(const std::string& str) {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return result;
        }

        std::string getFileExtension(const std::string& filePath) {
            size_t pos = filePath.find_last_of('.');
            if (pos == std::string::npos) {
                return "";
            }
            return filePath.substr(pos + 1);
        }

    } // namespace utils
} // namespace ffmpeg_stream