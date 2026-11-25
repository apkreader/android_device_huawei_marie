// Copyright (C) 2026 apkreader
// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android/binder_interface_utils.h>
#include <health-impl/ChargerUtils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

using aidl::android::hardware::health::BatteryHealthData;
using aidl::android::hardware::health::BatteryPartStatus;
using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;
using aidl::android::hardware::health::HealthInfo;
using aidl::android::hardware::health::StorageInfo;
using aidl::android::hardware::health::charger::ChargerCallback;
using aidl::android::hardware::health::charger::ChargerModeMain;

static constexpr const char* kInstanceName = "default";
static constexpr std::string_view kChargerArg{"--charger"};
static constexpr const char* kBatteryCapPath = "/sys/class/power_supply/Battery/capacity";
static constexpr const char* kBacklightPath = "/sys/class/leds/lcd_backlight0/brightness";
static constexpr const char* kUfsHealthDir = "/sys/devices/platform/ff3c0000.ufs/health_descriptor";
static constexpr const char* kUfsSpecPath =
        "/sys/devices/platform/ff3c0000.ufs/device_descriptor/spec_version";

static bool ReadHexInt32(const std::string& path, int32_t* out) {
    std::string raw;
    if (!android::base::ReadFileToString(path, &raw)) {
        return false;
    }
    raw = android::base::Trim(raw);
    if (raw.empty()) {
        return false;
    }
    char* end = nullptr;
    long val = std::strtol(raw.c_str(), &end, 0);
    if (end == raw.c_str() || val < 0) {
        return false;
    }
    *out = static_cast<int32_t>(val);
    return true;
}

class MarieHealth : public Health {
  public:
    MarieHealth(std::string_view instance_name, std::unique_ptr<struct healthd_config>&& config)
        : Health(instance_name, std::move(config)) {}

    ndk::ScopedAStatus getBatteryHealthData(BatteryHealthData* out) override {
        Health::getBatteryHealthData(out);
        FillMarieBatteryHealth(out);
        return ndk::ScopedAStatus::ok();
    }

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override {
        if (!health_info->batteryHealthData.has_value()) {
            health_info->batteryHealthData.emplace();
        }
        FillMarieBatteryHealth(&(*health_info->batteryHealthData));
    }

  private:
    void FillMarieBatteryHealth(BatteryHealthData* out) {
        int64_t full = getFullChargeUah();
        int64_t design = getFullChargeDesignCapacityUah();
        if (full <= 0) full = 3240000;
        if (design <= 0) design = 3340000;
        if (out->batteryStateOfHealth <= 0) {
            int64_t soh = (full * 100 + design / 2) / design;
            out->batteryStateOfHealth = std::clamp<int64_t>(soh, 1, 100);
        }
        if (out->batteryPartStatus == BatteryPartStatus::UNSUPPORTED) {
            out->batteryPartStatus = BatteryPartStatus::ORIGINAL;
        }
        if (!out->batteryManufacturer.has_value() || out->batteryManufacturer->empty()) {
            out->batteryManufacturer = "Huawei";
        }
        if (!out->batteryModelName.has_value() || out->batteryModelName->empty()) {
            out->batteryModelName = "HB356687ECW";
        }
        if (out->batteryVoltageMinDesignUv <= 0) {
            out->batteryVoltageMinDesignUv = 3400000;
        }
    }

  public:

    ndk::ScopedAStatus getStorageInfo(std::vector<StorageInfo>* out) override {
        StorageInfo info{};
        bool ok_eol = ReadHexInt32(std::string(kUfsHealthDir) + "/eol_info", &info.eol);
        bool ok_a = ReadHexInt32(std::string(kUfsHealthDir) + "/life_time_estimation_a",
                                 &info.lifetimeA);
        bool ok_b = ReadHexInt32(std::string(kUfsHealthDir) + "/life_time_estimation_b",
                                 &info.lifetimeB);
        if (!ok_eol && !ok_a && !ok_b) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        std::string ver;
        if (android::base::ReadFileToString(kUfsSpecPath, &ver)) {
            info.version = "ufs " + android::base::Trim(ver);
        } else {
            info.version = "ufs";
        }
        out->clear();
        out->push_back(std::move(info));
        return ndk::ScopedAStatus::ok();
    }
};

static void WaitForBatterySysfs() {
    for (int i = 0; i < 75; ++i) {
        if (access(kBatteryCapPath, R_OK) == 0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    LOG(WARNING) << "Timed out waiting for " << kBatteryCapPath;
}

int main(int argc, char** argv) {
    bool charger_mode = (argc >= 2 && argv[1] == kChargerArg);
    if (charger_mode) {
        android::base::InitLogging(argv, android::base::KernelLogger);
    }

    WaitForBatterySysfs();

    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    auto binder = ndk::SharedRefBase::make<MarieHealth>(kInstanceName, std::move(config));

    if (charger_mode) {
        android::base::WriteStringToFile("128", kBacklightPath);
        LOG(INFO) << "Starting marie offline charger UI.";
        return ChargerModeMain(binder, std::make_shared<ChargerCallback>(binder));
    }

    LOG(INFO) << "Starting marie AIDL health HAL.";
    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
