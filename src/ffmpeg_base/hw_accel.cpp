/**
 * @file hw_accel.cpp
 * @brief 硬件加速类型和转换函数实现
 */

#include "ffmpeg_base/hw_accel.h"
#include "logger/logger.h"
#include <vector>

namespace ffmpeg_stream {

    std::string hwAccelTypeToString(HWAccelType type) {
        switch (type) {
            case HWAccelType::NONE: return "NONE";
            case HWAccelType::CUDA: return "CUDA";
            case HWAccelType::QSV: return "QSV";
            case HWAccelType::VAAPI: return "VAAPI";
            case HWAccelType::DXV: return "DXV";
            case HWAccelType::AMF: return "AMF";
            default: return "UNKNOWN";
        }
    }

    HWAccelType stringToHWAccelType(const std::string& str) {
        if (str == "NONE") return HWAccelType::NONE;
        if (str == "CUDA") return HWAccelType::CUDA;
        if (str == "QSV") return HWAccelType::QSV;
        if (str == "VAAPI") return HWAccelType::VAAPI;
        if (str == "DXV") return HWAccelType::DXV;
        if (str == "AMF") return HWAccelType::AMF;
        return HWAccelType::NONE;
    }

    AVHWDeviceType hwAccelTypeToAVHWDeviceType(HWAccelType type) {
        switch (type) {
            case HWAccelType::CUDA: return AV_HWDEVICE_TYPE_CUDA;
            case HWAccelType::QSV: return AV_HWDEVICE_TYPE_QSV;
            case HWAccelType::VAAPI: return AV_HWDEVICE_TYPE_VAAPI;
            case HWAccelType::DXV: return AV_HWDEVICE_TYPE_DXVA2;
            case HWAccelType::AMF: return AV_HWDEVICE_TYPE_VDPAU; // 不完全匹配，需要根据实际情况调整
            default: return AV_HWDEVICE_TYPE_NONE;
        }
    }

    std::vector<HWAccelType> getAvailableHWAccelTypes() {
        std::vector<HWAccelType> availableTypes;
        AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;

        // 检查所有硬件加速类型
        while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
            // 尝试创建硬件设备上下文
            AVBufferRef* hw_device_ctx = nullptr;
            int ret = av_hwdevice_ctx_create(&hw_device_ctx, type, nullptr, nullptr, 0);

            if (ret >= 0) {
                // 如果创建成功，添加到可用类型列表
                switch (type) {
                    case AV_HWDEVICE_TYPE_CUDA:
                        availableTypes.push_back(HWAccelType::CUDA);
                        Logger::debug("CUDA hardware acceleration available");
                        break;
                    case AV_HWDEVICE_TYPE_QSV:
                        availableTypes.push_back(HWAccelType::QSV);
                        Logger::debug("QSV hardware acceleration available");
                        break;
                    case AV_HWDEVICE_TYPE_VAAPI:
                        availableTypes.push_back(HWAccelType::VAAPI);
                        Logger::debug("VAAPI hardware acceleration available");
                        break;
                    case AV_HWDEVICE_TYPE_DXVA2:
                        availableTypes.push_back(HWAccelType::DXV);
                        Logger::debug("DXV hardware acceleration available");
                        break;
                    case AV_HWDEVICE_TYPE_VDPAU:
                        availableTypes.push_back(HWAccelType::AMF);
                        Logger::debug("AMF hardware acceleration available");
                        break;
                    default:
                        break;
                }

                // 释放上下文
                av_buffer_unref(&hw_device_ctx);
            }
        }

        // 软件解码总是可用的
        availableTypes.push_back(HWAccelType::NONE);

        return availableTypes;
    }

    bool isHWAccelAvailable(HWAccelType type) {
        if (type == HWAccelType::NONE) {
            return true; // 软件解码总是可用
        }

        AVHWDeviceType avType = hwAccelTypeToAVHWDeviceType(type);
        AVBufferRef* hw_device_ctx = nullptr;
        int ret = av_hwdevice_ctx_create(&hw_device_ctx, avType, nullptr, nullptr, 0);

        if (ret >= 0) {
            av_buffer_unref(&hw_device_ctx);
            return true;
        }

        return false;
    }

} // namespace ffmpeg_stream