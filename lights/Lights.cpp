// Copyright (C) 2026 apkreader
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "lights.marie"

#include <aidl/android/hardware/light/BnLights.h>
#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

using aidl::android::hardware::light::BnLights;
using aidl::android::hardware::light::HwLight;
using aidl::android::hardware::light::HwLightState;
using aidl::android::hardware::light::LightType;

namespace {

constexpr char kBacklight[] = "/sys/class/leds/lcd_backlight0/brightness";
constexpr char kMaxBacklight[] = "/sys/class/leds/lcd_backlight0/max_brightness";

class Lights final : public BnLights {
public:
    explicit Lights(int maximum) : mMaximum(maximum) {}

    ndk::ScopedAStatus getLights(std::vector<HwLight>* lights) override {
        HwLight backlight;
        backlight.id = 0;
        backlight.ordinal = 0;
        backlight.type = LightType::BACKLIGHT;
        *lights = {backlight};
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus setLightState(int32_t id, const HwLightState& state) override {
        if (id != 0) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }

        const uint32_t color = static_cast<uint32_t>(state.color);
        const uint32_t red = (color >> 16) & 0xff;
        const uint32_t green = (color >> 8) & 0xff;
        const uint32_t blue = color & 0xff;
        const uint32_t brightness = (77 * red + 150 * green + 29 * blue) >> 8;
        const uint32_t level = brightness * static_cast<uint32_t>(mMaximum) / 255;

        std::lock_guard lock(mMutex);
        if (!android::base::WriteStringToFile(std::to_string(level) + "\n", kBacklight)) {
            const int error = errno;
            ALOGE("Failed to set backlight: %s", strerror(error));
            return ndk::ScopedAStatus::fromServiceSpecificError(error);
        }
        return ndk::ScopedAStatus::ok();
    }

private:
    const int mMaximum;
    std::mutex mMutex;
};

}  // namespace

int main() {
    std::string value;
    int maximum;
    if (!android::base::ReadFileToString(kMaxBacklight, &value) ||
        !android::base::ParseInt(android::base::Trim(value), &maximum, 1, 65535)) {
        ALOGE("Failed to read the backlight range");
        return EXIT_FAILURE;
    }

    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto lights = ndk::SharedRefBase::make<Lights>(maximum);
    const std::string instance = std::string(Lights::descriptor) + "/default";
    const binder_status_t status = AServiceManager_addService(lights->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("Failed to register %s: %d", instance.c_str(), status);
        return EXIT_FAILURE;
    }
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
