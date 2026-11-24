// Copyright (C) 2026 apkreader
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "android.hardware.usb-service.marie"

#include <aidl/android/hardware/usb/BnUsb.h>
#include <aidl/android/hardware/usb/gadget/BnUsbGadget.h>
#include <aidl/android/hardware/usb/gadget/GadgetFunction.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using ::ndk::ScopedAStatus;

namespace usb = ::aidl::android::hardware::usb;
namespace gadget = ::aidl::android::hardware::usb::gadget;

constexpr const char* kUdcName = "ff100000.dwc3";
constexpr const char* kGadgetDir = "/config/usb_gadget/g1";
constexpr const char* kConfigDir = "/config/usb_gadget/g1/configs/b.1";
constexpr const char* kFunctionsDir = "/config/usb_gadget/g1/functions";
constexpr const char* kUdcPath = "/config/usb_gadget/g1/UDC";
constexpr const char* kSpeedPath = "/sys/class/udc/ff100000.dwc3/current_speed";
constexpr const char* kTypecPortPath = "/sys/class/typec/port0";
constexpr const char* kTypecPartnerPath = "/sys/class/typec/port0-partner";

bool writeFile(const std::string& path, const std::string& val) {
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGW("open(%s) failed: %s", path.c_str(), strerror(errno));
        return false;
    }
    ssize_t n = write(fd, val.data(), val.size());
    int saved = errno;
    close(fd);
    if (n < 0 || static_cast<size_t>(n) != val.size()) {
        ALOGW("write(%s, %s) failed: %s", path.c_str(), val.c_str(), strerror(saved));
        return false;
    }
    return true;
}

bool readFileTrimmed(const std::string& path, std::string* out) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0) return false;
    buf[n] = '\0';
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    *out = std::move(s);
    return true;
}

bool pathExists(const char* path) {
    struct stat st {};
    return stat(path, &st) == 0;
}

bool waitForEndpoints(const std::vector<std::string>& paths, int64_t timeoutMs) {
    if (paths.empty()) return true;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 2000);
    while (true) {
        bool allReady = true;
        for (const auto& p : paths) {
            if (!pathExists(p.c_str())) {
                allReady = false;
                break;
            }
        }
        if (allReady) return true;
        if (std::chrono::steady_clock::now() >= deadline) {
            for (const auto& p : paths) {
                if (!pathExists(p.c_str())) {
                    ALOGW("Timed out waiting for FunctionFS endpoint: %s", p.c_str());
                }
            }
            return false;
        }
        usleep(20000);
    }
}

void unlinkAllFunctions() {
    DIR* dir = opendir(kConfigDir);
    if (!dir) return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string full = std::string(kConfigDir) + "/" + ent->d_name;
        struct stat lst {};
        if (lstat(full.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) {
            if (unlink(full.c_str()) != 0) {
                ALOGW("unlink(%s) failed: %s", full.c_str(), strerror(errno));
            }
        }
    }
    closedir(dir);
}

bool linkFunction(const char* funcName, int idx) {
    std::string target = std::string(kFunctionsDir) + "/" + funcName;
    if (!pathExists(target.c_str())) {
        if (mkdir(target.c_str(), 0770) != 0 && errno != EEXIST) {
            ALOGE("mkdir(%s) failed: %s", target.c_str(), strerror(errno));
            return false;
        }
    }
    std::string linkPath = std::string(kConfigDir) + "/f" + std::to_string(idx + 1);
    if (symlink(target.c_str(), linkPath.c_str()) != 0) {
        ALOGE("symlink(%s -> %s) failed: %s", linkPath.c_str(), target.c_str(), strerror(errno));
        return false;
    }
    return true;
}

class MarieUsb : public usb::BnUsb {
  public:
    MarieUsb() {
        mMonitorThread = std::thread(&MarieUsb::ueventLoop, this);
    }

    ~MarieUsb() override {
        mStop.store(true);
        if (mMonitorThread.joinable()) mMonitorThread.join();
    }

    ScopedAStatus enableContaminantPresenceDetection(const std::string& portName, bool /*enable*/,
                                                     int64_t txId) override {
        std::shared_ptr<usb::IUsbCallback> cb;
        {
            std::lock_guard<std::mutex> lk(mLock);
            cb = mCallback;
        }
        if (cb) {
            cb->notifyContaminantEnabledStatus(portName, false, usb::Status::NOT_SUPPORTED, txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus enableUsbData(const std::string& portName, bool enable, int64_t txId) override {
        std::shared_ptr<usb::IUsbCallback> cb;
        {
            std::lock_guard<std::mutex> lk(mLock);
            mUsbDataEnabled = enable;
            cb = mCallback;
        }
        if (cb) {
            cb->notifyEnableUsbDataStatus(portName, enable, usb::Status::SUCCESS, txId);
            notifyPortsLocked(cb);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus enableUsbDataWhileDocked(const std::string& portName, int64_t txId) override {
        std::shared_ptr<usb::IUsbCallback> cb;
        {
            std::lock_guard<std::mutex> lk(mLock);
            cb = mCallback;
        }
        if (cb) {
            cb->notifyEnableUsbDataWhileDockedStatus(portName, usb::Status::NOT_SUPPORTED, txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus queryPortStatus(int64_t txId) override {
        std::shared_ptr<usb::IUsbCallback> cb;
        {
            std::lock_guard<std::mutex> lk(mLock);
            cb = mCallback;
        }
        if (cb) {
            notifyPortsLocked(cb);
            cb->notifyQueryPortStatus("all", usb::Status::SUCCESS, txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus setCallback(const std::shared_ptr<usb::IUsbCallback>& callback) override {
        {
            std::lock_guard<std::mutex> lk(mLock);
            mCallback = callback;
        }
        if (callback) {
            notifyPortsLocked(callback);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus switchRole(const std::string& portName, const usb::PortRole& role,
                             int64_t txId) override {
        std::shared_ptr<usb::IUsbCallback> cb;
        {
            std::lock_guard<std::mutex> lk(mLock);
            cb = mCallback;
        }
        if (cb) {
            cb->notifyRoleSwitchStatus(portName, role, usb::Status::NOT_SUPPORTED, txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus limitPowerTransfer(const std::string& portName, bool /*limit*/,
                                     int64_t txId) override {
        std::shared_ptr<usb::IUsbCallback> cb;
        {
            std::lock_guard<std::mutex> lk(mLock);
            cb = mCallback;
        }
        if (cb) {
            cb->notifyLimitPowerTransferStatus(portName, false, usb::Status::NOT_SUPPORTED, txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus resetUsbPort(const std::string& portName, int64_t txId) override {
        std::shared_ptr<usb::IUsbCallback> cb;
        {
            std::lock_guard<std::mutex> lk(mLock);
            cb = mCallback;
        }
        if (cb) {
            cb->notifyResetUsbPortStatus(portName, usb::Status::NOT_SUPPORTED, txId);
        }
        return ScopedAStatus::ok();
    }

  private:
    std::vector<usb::PortStatus> buildPortStatus() {
        usb::PortStatus ps;
        ps.portName = "port0";
        ps.supportedModes = {usb::PortMode::UFP};
        ps.supportedContaminantProtectionModes = {usb::ContaminantProtectionMode::NONE};
        ps.supportsEnableContaminantPresenceProtection = false;
        ps.contaminantProtectionStatus = usb::ContaminantProtectionStatus::NONE;
        ps.supportsEnableContaminantPresenceDetection = false;
        ps.contaminantDetectionStatus = usb::ContaminantDetectionStatus::NOT_SUPPORTED;
        ps.canChangeMode = false;
        ps.canChangeDataRole = false;
        ps.canChangePowerRole = false;
        ps.powerTransferLimited = false;
        ps.powerBrickStatus = usb::PowerBrickStatus::UNKNOWN;
        ps.usbDataStatus = {mUsbDataEnabled ? usb::UsbDataStatus::ENABLED
                                            : usb::UsbDataStatus::DISABLED_FORCE};

        bool connected = pathExists(kTypecPartnerPath);
        if (!connected && !pathExists(kTypecPortPath)) {
            // Fallback to USB power_supply if kernel typec port is absent
            std::string online;
            connected = readFileTrimmed("/sys/class/power_supply/USB/online", &online) &&
                        online == "1";
        }

        if (connected) {
            ps.currentMode = usb::PortMode::UFP;
            ps.currentDataRole = usb::PortDataRole::DEVICE;
            ps.currentPowerRole = usb::PortPowerRole::SINK;
        } else {
            ps.currentMode = usb::PortMode::NONE;
            ps.currentDataRole = usb::PortDataRole::NONE;
            ps.currentPowerRole = usb::PortPowerRole::NONE;
        }

        ALOGI("PortStatus port0: connected=%d dataRole=%d powerRole=%d",
              connected ? 1 : 0, static_cast<int>(ps.currentDataRole),
              static_cast<int>(ps.currentPowerRole));
        return {ps};
    }

    void notifyPortsLocked(const std::shared_ptr<usb::IUsbCallback>& cb) {
        if (!cb) return;
        auto status = buildPortStatus();
        cb->notifyPortStatusChange(status, usb::Status::SUCCESS);
    }

    void ueventLoop() {
        int sock = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
        if (sock < 0) {
            ALOGE("netlink uevent socket failed: %s", strerror(errno));
            return;
        }
        struct sockaddr_nl addr {};
        addr.nl_family = AF_NETLINK;
        addr.nl_pid = getpid();
        addr.nl_groups = 1;
        if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            ALOGE("netlink bind failed: %s", strerror(errno));
            close(sock);
            return;
        }

        char buf[4096];
        while (!mStop.load()) {
            struct pollfd pfd {sock, POLLIN, 0};
            int ret = poll(&pfd, 1, 1000);
            if (ret <= 0) continue;
            ssize_t len = recv(sock, buf, sizeof(buf) - 1, 0);
            if (len <= 0) continue;
            buf[len] = '\0';

            bool relevant = false;
            for (ssize_t i = 0; i < len;) {
                const char* line = buf + i;
                if (strstr(line, "SUBSYSTEM=typec") != nullptr ||
                    strstr(line, "port0-partner") != nullptr ||
                    strstr(line, "POWER_SUPPLY_NAME=USB") != nullptr) {
                    relevant = true;
                    break;
                }
                i += strlen(line) + 1;
            }
            if (relevant) {
                std::shared_ptr<usb::IUsbCallback> cb;
                {
                    std::lock_guard<std::mutex> lk(mLock);
                    cb = mCallback;
                }
                notifyPortsLocked(cb);
            }
        }
        close(sock);
    }

    std::mutex mLock;
    std::shared_ptr<usb::IUsbCallback> mCallback;
    bool mUsbDataEnabled = true;
    std::atomic<bool> mStop{false};
    std::thread mMonitorThread;
};

class MarieUsbGadget : public gadget::BnUsbGadget {
  public:
    ScopedAStatus setCurrentUsbFunctions(
            int64_t functions, const std::shared_ptr<gadget::IUsbGadgetCallback>& callback,
            int64_t timeoutMs, int64_t txId) override {
        std::lock_guard<std::mutex> lk(mLock);
        ALOGI("setCurrentUsbFunctions(0x%llx, timeout=%lldms)",
              static_cast<unsigned long long>(functions), static_cast<long long>(timeoutMs));

        mCurrentFunctions = functions;
        mFunctionsApplied = false;

        // 1. Tear down active gadget binding & symlinks
        writeFile(kUdcPath, "\n");
        unlinkAllFunctions();
        writeFile(std::string(kGadgetDir) + "/bDeviceClass", "0x00");
        writeFile(std::string(kGadgetDir) + "/bDeviceSubClass", "0x00");
        writeFile(std::string(kGadgetDir) + "/bDeviceProtocol", "0x00");
        writeFile(std::string(kGadgetDir) + "/os_desc/use", "0");

        if (functions == gadget::GadgetFunction::NONE) {
            mFunctionsApplied = true;
            if (callback) {
                callback->setCurrentUsbFunctionsCb(functions, gadget::Status::SUCCESS, txId);
            }
            return ScopedAStatus::ok();
        }

        // 2. Select distinct VID/PID per composition so Windows loads the matching driver cleanly
        const char* vid = "0x12d1";
        const char* pid = "0x103a";
        const bool hasAdb = (functions & gadget::GadgetFunction::ADB) != 0;
        const bool hasMtp = (functions & gadget::GadgetFunction::MTP) != 0;
        const bool hasPtp = (functions & gadget::GadgetFunction::PTP) != 0;
        const bool hasRndis = (functions & gadget::GadgetFunction::RNDIS) != 0;
        const bool hasNcm = (functions & gadget::GadgetFunction::NCM) != 0;
        const bool hasMidi = (functions & gadget::GadgetFunction::MIDI) != 0;
        const bool hasAcc = (functions & gadget::GadgetFunction::ACCESSORY) != 0;

        if (hasAcc) {
            vid = "0x18d1";
            pid = hasAdb ? "0x2d01" : "0x2d00";
        } else if (hasMtp) {
            pid = hasAdb ? "0x107f" : "0x107e";
        } else if (hasPtp) {
            pid = hasAdb ? "0x1081" : "0x1080";
        } else if (hasRndis) {
            pid = hasAdb ? "0x1083" : "0x1082";
        } else if (hasNcm) {
            pid = hasAdb ? "0x1085" : "0x1084";
        } else if (hasMidi) {
            pid = hasAdb ? "0x1087" : "0x1086";
        } else if (hasAdb) {
            pid = "0x103a";
        } else {
            ALOGW("Unsupported USB function mask: 0x%llx",
                  static_cast<unsigned long long>(functions));
            if (callback) {
                callback->setCurrentUsbFunctionsCb(
                        functions, gadget::Status::CONFIGURATION_NOT_SUPPORTED, txId);
            }
            return ScopedAStatus::ok();
        }

        writeFile(std::string(kGadgetDir) + "/idVendor", vid);
        writeFile(std::string(kGadgetDir) + "/idProduct", pid);

        // 3. Link requested functions
        int idx = 0;
        std::string configName;
        std::vector<std::string> ffsEndpoints;
        auto appendName = [&](const char* n) {
            if (!configName.empty()) configName += "_";
            configName += n;
        };

        bool ok = true;
        if (hasMtp) {
            writeFile(std::string(kGadgetDir) + "/os_desc/use", "1");
            ok = ok && linkFunction("ffs.mtp", idx++);
            ffsEndpoints.push_back("/dev/usb-ffs/mtp/ep1");
            ffsEndpoints.push_back("/dev/usb-ffs/mtp/ep2");
            ffsEndpoints.push_back("/dev/usb-ffs/mtp/ep3");
            appendName("mtp");
        } else if (hasPtp) {
            writeFile(std::string(kGadgetDir) + "/os_desc/use", "1");
            ok = ok && linkFunction("ffs.ptp", idx++);
            ffsEndpoints.push_back("/dev/usb-ffs/ptp/ep1");
            ffsEndpoints.push_back("/dev/usb-ffs/ptp/ep2");
            ffsEndpoints.push_back("/dev/usb-ffs/ptp/ep3");
            appendName("ptp");
        }

        if (hasRndis) {
            writeFile(std::string(kGadgetDir) + "/bDeviceClass", "0xef");
            writeFile(std::string(kGadgetDir) + "/bDeviceSubClass", "0x02");
            writeFile(std::string(kGadgetDir) + "/bDeviceProtocol", "0x01");
            writeFile(std::string(kGadgetDir) + "/os_desc/use", "1");
            ok = ok && linkFunction("rndis.gs4", idx++);
            appendName("rndis");
        }

        if (hasNcm) {
            writeFile(std::string(kGadgetDir) + "/bDeviceClass", "0xef");
            writeFile(std::string(kGadgetDir) + "/bDeviceSubClass", "0x02");
            writeFile(std::string(kGadgetDir) + "/bDeviceProtocol", "0x01");
            ok = ok && linkFunction("ncm.gs6", idx++);
            appendName("ncm");
        }

        if (hasMidi) {
            ok = ok && linkFunction("midi.gs5", idx++);
            appendName("midi");
        }

        if (hasAcc) {
            ok = ok && linkFunction("accessory.gs2", idx++);
            appendName("accessory");
        }

        if (hasAdb) {
            ok = ok && linkFunction("ffs.adb", idx++);
            ffsEndpoints.push_back("/dev/usb-ffs/adb/ep1");
            ffsEndpoints.push_back("/dev/usb-ffs/adb/ep2");
            appendName("adb");
        }

        writeFile(std::string(kConfigDir) + "/strings/0x409/configuration",
                  configName.empty() ? "none" : configName);

        // 4. Wait for FunctionFS endpoints (MTP/PTP/ADB) before binding UDC
        if (ok && !ffsEndpoints.empty()) {
            ok = waitForEndpoints(ffsEndpoints, timeoutMs);
        }

        // 5. Bind to DWC3 UDC
        if (ok && idx > 0) {
            ok = writeFile(kUdcPath, kUdcName);
        }

        mFunctionsApplied = ok;
        ALOGI("setCurrentUsbFunctions(0x%llx -> %s, vid=%s pid=%s): %s",
              static_cast<unsigned long long>(functions), configName.c_str(), vid, pid,
              ok ? "SUCCESS" : "ERROR");

        if (callback) {
            callback->setCurrentUsbFunctionsCb(
                    functions, ok ? gadget::Status::SUCCESS : gadget::Status::ERROR, txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus getCurrentUsbFunctions(
            const std::shared_ptr<gadget::IUsbGadgetCallback>& callback, int64_t txId) override {
        std::lock_guard<std::mutex> lk(mLock);
        if (callback) {
            callback->getCurrentUsbFunctionsCb(
                    mCurrentFunctions,
                    mFunctionsApplied ? gadget::Status::FUNCTIONS_APPLIED
                                      : gadget::Status::FUNCTIONS_NOT_APPLIED,
                    txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus getUsbSpeed(const std::shared_ptr<gadget::IUsbGadgetCallback>& callback,
                              int64_t txId) override {
        gadget::UsbSpeed speed = gadget::UsbSpeed::UNKNOWN;
        std::string s;
        if (readFileTrimmed(kSpeedPath, &s)) {
            if (s == "low-speed") speed = gadget::UsbSpeed::LOWSPEED;
            else if (s == "full-speed") speed = gadget::UsbSpeed::FULLSPEED;
            else if (s == "high-speed") speed = gadget::UsbSpeed::HIGHSPEED;
            else if (s == "super-speed") speed = gadget::UsbSpeed::SUPERSPEED;
        }
        if (callback) {
            callback->getUsbSpeedCb(speed, txId);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus reset(const std::shared_ptr<gadget::IUsbGadgetCallback>& callback,
                        int64_t txId) override {
        std::lock_guard<std::mutex> lk(mLock);
        writeFile(kUdcPath, "\n");
        usleep(50000);
        bool ok = writeFile(kUdcPath, kUdcName);
        if (callback) {
            callback->resetCb(ok ? gadget::Status::SUCCESS : gadget::Status::ERROR, txId);
        }
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mLock;
    int64_t mCurrentFunctions = gadget::GadgetFunction::NONE;
    bool mFunctionsApplied = false;
};

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(2);

    auto usbSvc = ndk::SharedRefBase::make<MarieUsb>();
    std::string usbName = std::string(usb::IUsb::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(usbSvc->asBinder().get(), usbName.c_str());
    if (status != STATUS_OK) {
        ALOGE("Failed to register %s: %d", usbName.c_str(), status);
        return 1;
    }

    auto gadgetSvc = ndk::SharedRefBase::make<MarieUsbGadget>();
    std::string gadgetName = std::string(gadget::IUsbGadget::descriptor) + "/default";
    status = AServiceManager_addService(gadgetSvc->asBinder().get(), gadgetName.c_str());
    if (status != STATUS_OK) {
        ALOGE("Failed to register %s: %d", gadgetName.c_str(), status);
        return 1;
    }

    ALOGI("Registered %s and %s", usbName.c_str(), gadgetName.c_str());
    ABinderProcess_joinThreadPool();
    return 0;
}
