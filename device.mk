# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := device/huawei/marie

PRODUCT_SHIPPING_API_LEVEL := 28
PRODUCT_EXTRA_VNDK_VERSIONS += 28

$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)
$(call inherit-product, vendor/huawei/marie/marie-vendor.mk)
$(call inherit-product, vendor/huawei/marie/mesa/mesa.mk)
$(call inherit-product, $(LOCAL_PATH)/vendor_build/symlinks.mk)

# Normal boot images belong on the Huawei kernel partition.
PRODUCT_BUILD_BOOT_IMAGE := true
PRODUCT_BUILD_RAMDISK_IMAGE := true
ifeq ($(MARIE_BUILD_RECOVERY),true)
PRODUCT_BUILD_RECOVERY_IMAGE := true
PRODUCT_SYSTEM_PROPERTIES += ro.minui.pixel_format=BGRA_8888
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/init.recovery.kirin710.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.kirin710.rc
else
PRODUCT_BUILD_RECOVERY_IMAGE := false
endif
PRODUCT_BUILD_VENDOR_BOOT_IMAGE := false
PRODUCT_BUILD_INIT_BOOT_IMAGE := false
PRODUCT_BUILD_VENDOR_IMAGE := true
PRODUCT_BUILD_ODM_IMAGE := false
PRODUCT_BUILD_SYSTEM_EXT_IMAGE := false

# Display
TARGET_SCREEN_WIDTH := 1080
TARGET_SCREEN_HEIGHT := 2312
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xxhdpi
PRODUCT_PRODUCT_PROPERTIES += \
    ro.sf.lcd_density=480

# Bluetooth
PRODUCT_PRODUCT_PROPERTIES += \
    bluetooth.profile.a2dp.source.enabled=true \
    bluetooth.profile.hfp.ag.enabled=true \
    bluetooth.a2dp.restore_link_policy_on_close=false

PRODUCT_PACKAGES += WifiOverlayMarie

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/android.hardware.telephony.gsm.xml

PRODUCT_SYSTEM_PROPERTIES += \
    ro.hardware.egl=mesa \
    persist.graphics.egl=mesa \
    drm.gpu.vendor_name=panfrost \
    ro.opengles.version=196609 \
    ro.hwui.use_vulkan=false \
    debug.hwui.renderer=skiagl \
    ro.zygote.disable_gl_preload=true \
    ro.surface_flinger.max_frame_buffer_acquired_buffers=3 \
    ro.surface_flinger.supports_background_blur=0 \
    persist.sys.sf.disable_blurs=1 \
    debug.sf.use_phase_offsets_as_durations=1 \
    debug.sf.late.sf.duration=15500000 \
    debug.sf.early.sf.duration=15500000 \
    debug.sf.earlyGl.sf.duration=16000000 \
    debug.sf.late.app.duration=16600000 \
    debug.sf.early.app.duration=16600000 \
    debug.sf.earlyGl.app.duration=16600000 \
    persist.radio.multisim.config=dsds

# Init, fstab, cgroups, and SELinux mapping
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/fstab.kirin710:$(TARGET_COPY_OUT_RAMDISK)/fstab.kirin710 \
    $(LOCAL_PATH)/configs/init.marie.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.marie.rc \
    $(LOCAL_PATH)/configs/init.marie.usb.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.marie.usb.rc \
    $(LOCAL_PATH)/configs/cgroups.json:$(TARGET_COPY_OUT_VENDOR)/etc/cgroups.json \
    $(LOCAL_PATH)/configs/task_profiles.json:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/task_profiles.json \
    $(LOCAL_PATH)/configs/vendor.huawei.hardware.wifi@1.1-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.huawei.hardware.wifi@1.1-service.rc \
    $(LOCAL_PATH)/sepolicy/mapping/28.0.cil:$(TARGET_COPY_OUT_SYSTEM)/etc/selinux/mapping/28.0.cil

# Graphics HAL configs (HWC3 big-core affinity, Panfrost early-init, configstore seccomp, mapper service context)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/graphics/marie-hwc3-drm.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/marie-hwc3-drm.rc \
    $(LOCAL_PATH)/graphics/marie-panfrost.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/marie-panfrost.rc \
    $(LOCAL_PATH)/graphics/configstore@1.1.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/configstore@1.1.policy \
    $(LOCAL_PATH)/graphics/vendor_service_contexts:$(TARGET_COPY_OUT_VENDOR)/etc/selinux/vendor_service_contexts

# VINTF & linker configuration
PRODUCT_PACKAGES += framework_compatibility_matrix.3.xml
PRODUCT_VENDOR_LINKER_CONFIG_FRAGMENTS += $(LOCAL_PATH)/power/linker.config.json

# Open-source HALs, shims, and Android 17 graphics/minijail stack built natively from source
PRODUCT_PACKAGES += \
    android.hardware.composer.hwc3-service.drm \
    android.hardware.graphics.allocator-service.minigbm \
    mapper.minigbm \
    libdrm.vendor \
    libminijail.vendor \
    android.hardware.lights-service.marie \
    android.hardware.power-service.marie \
    libwifi_fdsan_compat
