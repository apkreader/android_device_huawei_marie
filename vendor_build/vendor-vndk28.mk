# Keep Huawei HALs linked to the VNDK 28 APEX. Modern vendor copies
# take priority over the APEX and can break legacy ELF relocations.
BOARD_VENDORIMAGE_EXCLUDED_FILES += \
    lib/android.hidl.memory@1.0.so \
    lib/hw/android.hidl.memory@1.0-impl.so \
    lib/hw/audio.primary.default.so \
    lib/libaconfig_storage_read_api_cc.so \
    lib/libaudioutils.so \
    lib/libbase.so \
    lib/libc++.so \
    lib/libcutils.so \
    lib/libhidlbase.so \
    lib/libspeexresampler.so \
    lib/libutils.so \
    lib/libvibratorutils.so \
    lib/server_configurable_flags.so \
    lib/soundfx/libhapticgenerator.so \
    lib64/android.hidl.memory@1.0.so \
    lib64/hw/android.hidl.memory@1.0-impl.so \
    lib64/hw/audio.primary.default.so \
    lib64/libaconfig_storage_read_api_cc.so \
    lib64/libaudioutils.so \
    lib64/libbase.so \
    lib64/libbinder.so \
    lib64/libbinderdebug.so \
    lib64/libc++.so \
    lib64/libcrypto.so \
    lib64/libcutils.so \
    lib64/libhidlbase.so \
    lib64/libspeexresampler.so \
    lib64/libutils.so \
    lib64/libvibratorutils.so \
    lib64/libz.so \
    lib64/server_configurable_flags.so \
    lib64/soundfx/libhapticgenerator.so \
    bin/dumpsys \
    bin/logwrapper \
    etc/bpf/filterPowerSupplyEvents.o \
    etc/selinux/genfs_labels_version.txt \
    etc/selinux/selinux_denial_metadata \
    etc/selinux/vendor_keystore2_key_contexts \
    etc/selinux/vendor_tee_service_contexts

# Also exclude Android 17 copies replaced by isolated *7.so shims for HWC3/minigbm/Mesa
BOARD_VENDORIMAGE_EXCLUDED_FILES += \
    lib/libhardware.so \
    lib64/libhardware.so \
    lib/libui.so \
    lib64/libui.so \
    lib/android.hardware.graphics.allocator@2.0.so \
    lib/android.hardware.graphics.mapper@2.0.so \
    lib/android.hardware.graphics.mapper@2.1.so \
    lib/android.hardware.graphics.common@1.0.so \
    lib/android.hardware.graphics.common@1.1.so \
    lib64/android.hardware.graphics.common@1.0.so \
    lib64/android.hardware.graphics.common@1.1.so \
    lib64/android.hardware.graphics.allocator@2.0.so \
    lib64/android.hardware.graphics.mapper@2.0.so \
    lib64/android.hardware.graphics.mapper@2.1.so

# Replaced by device/huawei/marie/graphics/marie-hwc3-drm.rc (SFMainPolicy big-core affinity)
BOARD_VENDORIMAGE_EXCLUDED_FILES += \
    etc/init/hwc3-drm.rc
