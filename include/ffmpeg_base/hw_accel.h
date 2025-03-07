/**
 * @file hw_accel.h
 * @brief 硬件加速类型和转换函数
 */

#ifndef FFMPEG_STREAM_HW_ACCEL_H
#define FFMPEG_STREAM_HW_ACCEL_H

#include <string>
#include <vector>

extern "C" {
#include <libavutil/hwcontext.h>
}

namespace ffmpeg_stream {

// 硬件加速类型枚举
    enum class HWAccelType {
        NONE,   // 不使用硬件加速
        CUDA,   // NVIDIA GPU
        QSV,    // Intel Quick Sync
        VAAPI,  // Video Acceleration API
        DXV,    // DirectX Video Acceleration
        AMF     // AMD Advanced Media Framework
    };

// 硬件加速相关函数声明
    std::string hwAccelTypeToString(HWAccelType type);
    HWAccelType stringToHWAccelType(const std::string& str);
    AVHWDeviceType hwAccelTypeToAVHWDeviceType(HWAccelType type);

// 获取可用的硬件加速类型列表
    std::vector<HWAccelType> getAvailableHWAccelTypes();

// 检查特定硬件加速类型是否可用
    bool isHWAccelAvailable(HWAccelType type);

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_HW_ACCEL_H