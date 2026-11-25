// Copyright (C) 2026 apkreader
// SPDX-License-Identifier: Apache-2.0

#include <aidl/android/hardware/camera/common/CameraDeviceStatus.h>
#include <aidl/android/hardware/camera/common/CameraResourceCost.h>
#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/common/TorchModeStatus.h>
#include <aidl/android/hardware/camera/common/VendorTagSection.h>
#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <aidl/android/hardware/camera/device/CameraMetadata.h>
#include <aidl/android/hardware/camera/device/ICameraDeviceCallback.h>
#include <aidl/android/hardware/camera/device/ICameraDeviceSession.h>
#include <aidl/android/hardware/camera/device/ICameraInjectionSession.h>
#include <aidl/android/hardware/camera/device/RequestTemplate.h>
#include <aidl/android/hardware/camera/device/StreamConfiguration.h>
#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <aidl/android/hardware/camera/provider/CameraIdAndStreamCombination.h>
#include <aidl/android/hardware/camera/provider/ConcurrentCameraIdCombination.h>
#include <aidl/android/hardware/camera/provider/ICameraProviderCallback.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using aidl::android::hardware::camera::common::CameraResourceCost;
using aidl::android::hardware::camera::common::Status;
using aidl::android::hardware::camera::common::TorchModeStatus;
using aidl::android::hardware::camera::common::VendorTagSection;
using aidl::android::hardware::camera::device::BnCameraDevice;
using aidl::android::hardware::camera::device::CameraMetadata;
using aidl::android::hardware::camera::device::ICameraDevice;
using aidl::android::hardware::camera::device::ICameraDeviceCallback;
using aidl::android::hardware::camera::device::ICameraDeviceSession;
using aidl::android::hardware::camera::device::ICameraInjectionSession;
using aidl::android::hardware::camera::device::RequestTemplate;
using aidl::android::hardware::camera::device::StreamConfiguration;
using aidl::android::hardware::camera::provider::BnCameraProvider;
using aidl::android::hardware::camera::provider::CameraIdAndStreamCombination;
using aidl::android::hardware::camera::provider::ConcurrentCameraIdCombination;
using aidl::android::hardware::camera::provider::ICameraProvider;
using aidl::android::hardware::camera::provider::ICameraProviderCallback;
using ndk::ScopedAStatus;

constexpr char kTag[] = "MarieTorch";
constexpr char kDeviceName[] = "device@1.1/internal/0";
constexpr char kTorchBrightnessPath[] = "/sys/class/leds/torch/brightness";
constexpr int32_t kMaxStrengthLevel = 4;
constexpr int32_t kDefaultStrengthLevel = 2;

ScopedAStatus ServiceStatus(Status st) {
    return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(st));
}

int32_t StrengthToBrightness(int32_t strength) {
    switch (std::clamp<int32_t>(strength, 1, kMaxStrengthLevel)) {
        case 1:
            return 64;
        case 2:
            return 128;
        case 3:
            return 192;
        case 4:
        default:
            return 255;
    }
}

bool WriteTorchBrightness(int32_t brightness) {
    int fd = TEMP_FAILURE_RETRY(open(kTorchBrightnessPath, O_WRONLY | O_CLOEXEC));
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "open(%s) failed: %s",
                            kTorchBrightnessPath, strerror(errno));
        return false;
    }
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d\n", std::clamp<int32_t>(brightness, 0, 255));
    ssize_t wr = TEMP_FAILURE_RETRY(write(fd, buf, static_cast<size_t>(len)));
    close(fd);
    if (wr < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "write(%s) failed: %s",
                            kTorchBrightnessPath, strerror(errno));
        return false;
    }
    return true;
}

std::vector<uint8_t> BuildTorchCameraCharacteristics() {
    camera_metadata_t* raw = allocate_camera_metadata(64, 2048);
    if (!raw) {
        return {};
    }

    auto add_u8 = [&](uint32_t tag, const uint8_t* data, size_t count) {
        add_camera_metadata_entry(raw, tag, data, count);
    };
    auto add_i32 = [&](uint32_t tag, const int32_t* data, size_t count) {
        add_camera_metadata_entry(raw, tag, data, count);
    };
    auto add_i64 = [&](uint32_t tag, const int64_t* data, size_t count) {
        add_camera_metadata_entry(raw, tag, data, count);
    };
    auto add_f = [&](uint32_t tag, const float* data, size_t count) {
        add_camera_metadata_entry(raw, tag, data, count);
    };

    const uint8_t facing = ANDROID_LENS_FACING_BACK;
    add_u8(ANDROID_LENS_FACING, &facing, 1);

    const int32_t orientation = 90;
    add_i32(ANDROID_SENSOR_ORIENTATION, &orientation, 1);

    const uint8_t flash_avail = ANDROID_FLASH_INFO_AVAILABLE_TRUE;
    add_u8(ANDROID_FLASH_INFO_AVAILABLE, &flash_avail, 1);

    const int32_t flash_max = kMaxStrengthLevel;
    add_i32(ANDROID_FLASH_INFO_STRENGTH_MAXIMUM_LEVEL, &flash_max, 1);

    const int32_t flash_def = kDefaultStrengthLevel;
    add_i32(ANDROID_FLASH_INFO_STRENGTH_DEFAULT_LEVEL, &flash_def, 1);

    const uint8_t hw_level = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;
    add_u8(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &hw_level, 1);

    const uint8_t capabilities[] = {
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE,
    };
    add_u8(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, capabilities, 1);

    const int32_t active_array[] = {0, 0, 1920, 1080};
    add_i32(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array, 4);
    add_i32(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, active_array, 4);

    const int32_t pixel_array[] = {1920, 1080};
    add_i32(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixel_array, 2);

    const float phys_size[] = {4.80f, 3.60f};
    add_f(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, phys_size, 2);

    const uint8_t ts_source = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
    add_u8(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &ts_source, 1);

    const float focal_lengths[] = {3.81f};
    add_f(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, focal_lengths, 1);

    const float apertures[] = {1.8f};
    add_f(ANDROID_LENS_INFO_AVAILABLE_APERTURES, apertures, 1);

    const float min_focus = 0.0f;
    add_f(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, &min_focus, 1);
    add_f(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, &min_focus, 1);

    const float max_zoom = 4.0f;
    add_f(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &max_zoom, 1);

    const uint8_t crop_type = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
    add_u8(ANDROID_SCALER_CROPPING_TYPE, &crop_type, 1);

    const int32_t out_dir = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
    const int32_t stream_configs[] = {
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, out_dir,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, out_dir,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480, out_dir,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, out_dir,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720, out_dir,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480, out_dir,
        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, out_dir,
        HAL_PIXEL_FORMAT_BLOB, 1280, 720, out_dir,
        HAL_PIXEL_FORMAT_BLOB, 640, 480, out_dir,
    };
    add_i32(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, stream_configs,
            sizeof(stream_configs) / sizeof(stream_configs[0]));

    const int64_t frame_ns = 33333333LL;
    const int64_t min_durations[] = {
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, frame_ns,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, frame_ns,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480, frame_ns,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, frame_ns,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720, frame_ns,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480, frame_ns,
        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, frame_ns,
        HAL_PIXEL_FORMAT_BLOB, 1280, 720, frame_ns,
        HAL_PIXEL_FORMAT_BLOB, 640, 480, frame_ns,
    };
    add_i64(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, min_durations,
            sizeof(min_durations) / sizeof(min_durations[0]));

    const int64_t stall_durations[] = {
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, 0LL,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, 0LL,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480, 0LL,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, 0LL,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720, 0LL,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480, 0LL,
        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, frame_ns,
        HAL_PIXEL_FORMAT_BLOB, 1280, 720, frame_ns,
        HAL_PIXEL_FORMAT_BLOB, 640, 480, frame_ns,
    };
    add_i64(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stall_durations,
            sizeof(stall_durations) / sizeof(stall_durations[0]));

    const int32_t thumb_sizes[] = {0, 0, 160, 120, 320, 180};
    add_i32(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, thumb_sizes, 6);

    const int32_t jpeg_max_size = 1920 * 1080 * 3 / 2;
    add_i32(ANDROID_JPEG_MAX_SIZE, &jpeg_max_size, 1);

    const int32_t fps_ranges[] = {15, 30, 30, 30};
    add_i32(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, fps_ranges, 4);

    const int32_t ae_comp_range[] = {-4, 4};
    add_i32(ANDROID_CONTROL_AE_COMPENSATION_RANGE, ae_comp_range, 2);

    const camera_metadata_rational_t ae_step = {1, 2};
    add_camera_metadata_entry(raw, ANDROID_CONTROL_AE_COMPENSATION_STEP, &ae_step, 1);

    const uint8_t ae_modes[] = {
        ANDROID_CONTROL_AE_MODE_OFF,
        ANDROID_CONTROL_AE_MODE_ON,
        ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH,
        ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH,
    };
    add_u8(ANDROID_CONTROL_AE_AVAILABLE_MODES, ae_modes, sizeof(ae_modes));

    const uint8_t af_modes[] = {ANDROID_CONTROL_AF_MODE_OFF};
    add_u8(ANDROID_CONTROL_AF_AVAILABLE_MODES, af_modes, 1);

    const uint8_t awb_modes[] = {
        ANDROID_CONTROL_AWB_MODE_OFF,
        ANDROID_CONTROL_AWB_MODE_AUTO,
    };
    add_u8(ANDROID_CONTROL_AWB_AVAILABLE_MODES, awb_modes, 2);

    const uint8_t control_modes[] = {
        ANDROID_CONTROL_MODE_OFF,
        ANDROID_CONTROL_MODE_AUTO,
    };
    add_u8(ANDROID_CONTROL_AVAILABLE_MODES, control_modes, 2);

    const uint8_t face_modes[] = {ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
    add_u8(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, face_modes, 1);

    const int32_t max_faces = 0;
    add_i32(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &max_faces, 1);

    const int32_t sync_latency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
    add_i32(ANDROID_SYNC_MAX_LATENCY, &sync_latency, 1);

    const int32_t partial_count = 1;
    add_i32(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partial_count, 1);

    const uint8_t pipeline_depth = 4;
    add_u8(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &pipeline_depth, 1);

    const int32_t max_out_streams[] = {0, 2, 1};
    add_i32(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, max_out_streams, 3);

    const int32_t char_keys[] = {
        ANDROID_LENS_FACING,
        ANDROID_SENSOR_ORIENTATION,
        ANDROID_FLASH_INFO_AVAILABLE,
        ANDROID_FLASH_INFO_STRENGTH_MAXIMUM_LEVEL,
        ANDROID_FLASH_INFO_STRENGTH_DEFAULT_LEVEL,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
        ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
        ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
        ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
    };
    add_i32(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, char_keys,
            sizeof(char_keys) / sizeof(char_keys[0]));

    const int32_t req_keys[] = {
        ANDROID_CONTROL_AE_MODE,
        ANDROID_CONTROL_AF_MODE,
        ANDROID_CONTROL_AWB_MODE,
        ANDROID_FLASH_MODE,
        ANDROID_FLASH_STRENGTH_LEVEL,
    };
    add_i32(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, req_keys,
            sizeof(req_keys) / sizeof(req_keys[0]));

    const int32_t res_keys[] = {
        ANDROID_CONTROL_AE_STATE,
        ANDROID_CONTROL_AF_STATE,
        ANDROID_CONTROL_AWB_STATE,
        ANDROID_FLASH_STATE,
        ANDROID_FLASH_STRENGTH_LEVEL,
    };
    add_i32(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, res_keys,
            sizeof(res_keys) / sizeof(res_keys[0]));

    const int32_t session_keys[] = {
        ANDROID_CONTROL_AE_MODE,
    };
    add_i32(ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, session_keys, 1);

    sort_camera_metadata(raw);

    const size_t sz = get_camera_metadata_size(raw);
    const auto* bytes = reinterpret_cast<const uint8_t*>(raw);
    std::vector<uint8_t> out(bytes, bytes + sz);
    free_camera_metadata(raw);
    return out;
}

class MarieCameraProvider;

class MarieTorchDevice final : public BnCameraDevice {
  public:
    explicit MarieTorchDevice(MarieCameraProvider* provider)
        : provider_(provider), characteristics_(BuildTorchCameraCharacteristics()) {}

    ScopedAStatus getCameraCharacteristics(CameraMetadata* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        out->metadata = characteristics_;
        return ScopedAStatus::ok();
    }

    ScopedAStatus getPhysicalCameraCharacteristics(const std::string&,
                                                   CameraMetadata*) override {
        return ServiceStatus(Status::ILLEGAL_ARGUMENT);
    }

    ScopedAStatus getResourceCost(CameraResourceCost* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        out->resourceCost = 100;
        out->conflictingDevices.clear();
        return ScopedAStatus::ok();
    }

    ScopedAStatus isStreamCombinationSupported(const StreamConfiguration&, bool* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        *out = false;
        return ScopedAStatus::ok();
    }

    ScopedAStatus open(const std::shared_ptr<ICameraDeviceCallback>&,
                       std::shared_ptr<ICameraDeviceSession>*) override {
        return ServiceStatus(Status::MAX_CAMERAS_IN_USE);
    }

    ScopedAStatus openInjectionSession(const std::shared_ptr<ICameraDeviceCallback>&,
                                       std::shared_ptr<ICameraInjectionSession>*) override {
        return ServiceStatus(Status::OPERATION_NOT_SUPPORTED);
    }

    ScopedAStatus setTorchMode(bool on) override;
    ScopedAStatus turnOnTorchWithStrengthLevel(int32_t torchStrength) override;

    ScopedAStatus getTorchStrengthLevel(int32_t* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        std::lock_guard<std::mutex> lock(mutex_);
        *out = strength_level_;
        return ScopedAStatus::ok();
    }

    ScopedAStatus constructDefaultRequestSettings(RequestTemplate,
                                                  CameraMetadata*) override {
        return ServiceStatus(Status::OPERATION_NOT_SUPPORTED);
    }

    ScopedAStatus isStreamCombinationWithSettingsSupported(const StreamConfiguration&,
                                                           bool* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        *out = false;
        return ScopedAStatus::ok();
    }

    ScopedAStatus getSessionCharacteristics(const StreamConfiguration&,
                                            CameraMetadata* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        out->metadata = characteristics_;
        return ScopedAStatus::ok();
    }

    ScopedAStatus warmUp() override {
        return ScopedAStatus::ok();
    }

  private:
    MarieCameraProvider* provider_;
    std::vector<uint8_t> characteristics_;
    std::mutex mutex_;
    bool torch_on_ = false;
    int32_t strength_level_ = kDefaultStrengthLevel;
};

class MarieCameraProvider final : public BnCameraProvider {
  public:
    MarieCameraProvider() {
        device_ = ndk::SharedRefBase::make<MarieTorchDevice>(this);
    }

    ScopedAStatus setCallback(
            const std::shared_ptr<ICameraProviderCallback>& callback) override {
        if (!callback) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        std::lock_guard<std::mutex> lock(cb_mutex_);
        callback_ = callback;
        return ScopedAStatus::ok();
    }

    ScopedAStatus getVendorTags(std::vector<VendorTagSection>* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        out->clear();
        return ScopedAStatus::ok();
    }

    ScopedAStatus getCameraIdList(std::vector<std::string>* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        *out = {kDeviceName};
        return ScopedAStatus::ok();
    }

    ScopedAStatus getCameraDeviceInterface(
            const std::string& name, std::shared_ptr<ICameraDevice>* out) override {
        if (!out || name != kDeviceName) {
            return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        }
        *out = device_;
        return ScopedAStatus::ok();
    }

    ScopedAStatus notifyDeviceStateChange(int64_t) override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus getConcurrentCameraIds(
            std::vector<ConcurrentCameraIdCombination>* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        out->clear();
        return ScopedAStatus::ok();
    }

    ScopedAStatus isConcurrentStreamCombinationSupported(
            const std::vector<CameraIdAndStreamCombination>&, bool* out) override {
        if (!out) return ServiceStatus(Status::ILLEGAL_ARGUMENT);
        *out = false;
        return ScopedAStatus::ok();
    }

    void NotifyTorchStatus(TorchModeStatus status) {
        std::shared_ptr<ICameraProviderCallback> cb;
        {
            std::lock_guard<std::mutex> lock(cb_mutex_);
            cb = callback_;
        }
        if (cb) {
            cb->torchModeStatusChange(kDeviceName, status);
        }
    }

  private:
    std::mutex cb_mutex_;
    std::shared_ptr<ICameraProviderCallback> callback_;
    std::shared_ptr<MarieTorchDevice> device_;
};

ScopedAStatus MarieTorchDevice::setTorchMode(bool on) {
    int32_t brightness = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (on) {
            if (strength_level_ < 1 || strength_level_ > kMaxStrengthLevel) {
                strength_level_ = kDefaultStrengthLevel;
            }
            brightness = StrengthToBrightness(strength_level_);
            if (!WriteTorchBrightness(brightness)) {
                return ServiceStatus(Status::INTERNAL_ERROR);
            }
            torch_on_ = true;
        } else {
            if (!WriteTorchBrightness(0)) {
                return ServiceStatus(Status::INTERNAL_ERROR);
            }
            torch_on_ = false;
            strength_level_ = kDefaultStrengthLevel;
        }
    }
    provider_->NotifyTorchStatus(on ? TorchModeStatus::AVAILABLE_ON
                                    : TorchModeStatus::AVAILABLE_OFF);
    return ScopedAStatus::ok();
}

ScopedAStatus MarieTorchDevice::turnOnTorchWithStrengthLevel(int32_t torchStrength) {
    if (torchStrength < 1 || torchStrength > kMaxStrengthLevel) {
        return ServiceStatus(Status::ILLEGAL_ARGUMENT);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!WriteTorchBrightness(StrengthToBrightness(torchStrength))) {
            return ServiceStatus(Status::INTERNAL_ERROR);
        }
        strength_level_ = torchStrength;
        torch_on_ = true;
    }
    provider_->NotifyTorchStatus(TorchModeStatus::AVAILABLE_ON);
    return ScopedAStatus::ok();
}

}  // namespace

extern "C" __attribute__((weak)) void __cfi_slowpath(uint64_t, void*) {}
extern "C" __attribute__((weak)) void __cfi_slowpath_diag(uint64_t, void*, void*) {}

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    auto provider = ndk::SharedRefBase::make<MarieCameraProvider>();
    const std::string instance = std::string(ICameraProvider::descriptor) + "/internal/0";
    const binder_status_t status =
            AServiceManager_addService(provider->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
                            "Torch CameraProvider registration failed: %d", status);
        return 1;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "Registered %s", instance.c_str());
    ABinderProcess_joinThreadPool();
    return 1;
}
