// SPDX-License-Identifier: Apache-2.0

#include <aidl/android/hardware/power/BnPower.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
using namespace aidl::android::hardware::power;
using ndk::ScopedAStatus;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr char kTag[] = "MariePower";

struct Frequency {
    const char* minimum;
    const char* maximum;
    unsigned long target;
    std::optional<unsigned long> idle;
    bool boosted = false;
};

std::optional<unsigned long> ReadFrequency(const char* path) {
    FILE* file = fopen(path, "re");
    if (!file) return std::nullopt;
    unsigned long value;
    const int fields = fscanf(file, "%lu", &value);
    fclose(file);
    if (fields != 1) return std::nullopt;
    return value;
}

bool WriteFrequency(const char* path, unsigned long value) {
    const int fd = TEMP_FAILURE_RETRY(open(path, O_WRONLY | O_CLOEXEC));
    if (fd < 0) return false;
    const std::string text = std::to_string(value);
    const ssize_t written = TEMP_FAILURE_RETRY(write(fd, text.data(), text.size()));
    close(fd);
    return written == static_cast<ssize_t>(text.size());
}

class Boosts {
  public:
    enum Hint { Touch, Display, Launch, Count };

    Boosts() {
        for (auto& node : nodes_) {
            node.idle = ReadFrequency(node.minimum);
            available_ |= node.idle.has_value();
        }
        worker_ = std::thread([this] { Run(); });
    }

    ~Boosts() {
        {
            std::lock_guard lock(mutex_);
            exiting_ = true;
        }
        changed_.notify_one();
        worker_.join();
    }

    bool available() const { return available_; }

    void Set(Hint hint, std::chrono::milliseconds timeout) {
        std::lock_guard lock(mutex_);
        auto& deadline = deadlines_[hint];
        deadline = timeout.count() < 0 ? Clock::time_point{} :
                   std::max(deadline, Clock::now() + timeout);
        changed_.notify_one();
    }

  private:
    void Apply(bool boost) {
        for (auto& node : nodes_) {
            if (!node.idle || node.boosted == boost) continue;
            const auto maximum = ReadFrequency(node.maximum).value_or(0);
            unsigned long limit = boost ? std::max(*node.idle, node.target) : *node.idle;
            if (maximum != 0) limit = std::min(limit, maximum);
            if (WriteFrequency(node.minimum, limit)) {
                node.boosted = boost;
            } else {
                __android_log_print(ANDROID_LOG_WARN, kTag, "Unable to write %s", node.minimum);
            }
        }
    }

    void Run() {
        std::unique_lock lock(mutex_);
        while (!exiting_) {
            const auto latest = *std::max_element(deadlines_.begin(), deadlines_.end());
            const bool active = latest > Clock::now();
            Apply(active);
            if (active) {
                changed_.wait_until(lock, latest);
            } else {
                const bool restorePending = std::any_of(nodes_.begin(), nodes_.end(),
                                                       [](const auto& node) { return node.boosted; });
                if (restorePending) changed_.wait_for(lock, 250ms);
                else changed_.wait(lock);
            }
        }
        Apply(false);
    }

    std::array<Frequency, 3> nodes_{{
        {"/sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq",
         "/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq", 1709000, {}},
        {"/sys/devices/system/cpu/cpufreq/policy4/scaling_min_freq",
         "/sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq", 2189000, {}},
        {"/sys/class/devfreq/gpufreq/min_freq", "/sys/class/devfreq/gpufreq/max_freq", 826000000, {}},
    }};
    std::array<Clock::time_point, Count> deadlines_{};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool available_ = false;
    bool exiting_ = false;
    std::thread worker_;
};

ScopedAStatus Unsupported() {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

template <typename Enum>
constexpr int64_t Mask(Enum value) {
    return int64_t{1} << static_cast<int32_t>(value);
}

class Power final : public BnPower {
  public:
    ScopedAStatus setMode(Mode mode, bool enabled) override {
        if (mode == Mode::LAUNCH) boosts_.Set(Boosts::Launch, enabled ? 1500ms : -1ms);
        return ScopedAStatus::ok();
    }

    ScopedAStatus isModeSupported(Mode mode, bool* supported) override {
        *supported = boosts_.available() && mode == Mode::LAUNCH;
        return ScopedAStatus::ok();
    }

    ScopedAStatus setBoost(Boost boost, int32_t durationMs) override {
        switch (boost) {
            case Boost::INTERACTION:
                boosts_.Set(Boosts::Touch, std::chrono::milliseconds(
                    durationMs < 0 ? -1 : durationMs == 0 ? 500 : std::clamp(durationMs, 80, 1200)));
                break;
            case Boost::DISPLAY_UPDATE_IMMINENT:
                boosts_.Set(Boosts::Display, std::chrono::milliseconds(
                    durationMs < 0 ? -1 : durationMs == 0 ? 80 : std::min(durationMs, 1200)));
                break;
            default:
                break;
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus isBoostSupported(Boost boost, bool* supported) override {
        *supported = boosts_.available() &&
                     (boost == Boost::INTERACTION || boost == Boost::DISPLAY_UPDATE_IMMINENT);
        return ScopedAStatus::ok();
    }

    ScopedAStatus getSupportInfo(SupportInfo* info) override {
        *info = {};
        if (boosts_.available()) {
            info->modes = Mask(Mode::LAUNCH);
            info->boosts = Mask(Boost::INTERACTION) | Mask(Boost::DISPLAY_UPDATE_IMMINENT);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus createHintSession(int32_t, int32_t, const std::vector<int32_t>&, int64_t,
                                   std::shared_ptr<IPowerHintSession>*) override {
        return Unsupported();
    }

    ScopedAStatus createHintSessionWithConfig(int32_t, int32_t, const std::vector<int32_t>&,
            int64_t, SessionTag, SessionConfig*, std::shared_ptr<IPowerHintSession>*) override {
        return Unsupported();
    }

    ScopedAStatus getHintSessionPreferredRate(int64_t*) override { return Unsupported(); }
    ScopedAStatus getSessionChannel(int32_t, int32_t, ChannelConfig*) override { return Unsupported(); }
    ScopedAStatus closeSessionChannel(int32_t, int32_t) override { return Unsupported(); }
    ScopedAStatus getCpuHeadroom(const CpuHeadroomParams&, CpuHeadroomResult*) override {
        return Unsupported();
    }
    ScopedAStatus getGpuHeadroom(const GpuHeadroomParams&, GpuHeadroomResult*) override {
        return Unsupported();
    }
    ScopedAStatus sendCompositionData(const std::vector<CompositionData>&) override {
        return Unsupported();
    }
    ScopedAStatus sendCompositionUpdate(const CompositionUpdate&) override { return Unsupported(); }

  private:
    Boosts boosts_;
};
}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto power = ndk::SharedRefBase::make<Power>();
    const std::string instance = std::string(IPower::descriptor) + "/default";
    const binder_status_t status = AServiceManager_addService(power->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Power HAL registration failed: %d", status);
        return 1;
    }
    ABinderProcess_joinThreadPool();
    return 1;
}
