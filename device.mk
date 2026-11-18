# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := device/huawei/marie

PRODUCT_SHIPPING_API_LEVEL := 28
PRODUCT_EXTRA_VNDK_VERSIONS += 28

$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)
$(call inherit-product, vendor/huawei/marie/marie-vendor.mk)

# Normal boot images belong on the Huawei kernel partition.
PRODUCT_BUILD_BOOT_IMAGE := true
PRODUCT_BUILD_RAMDISK_IMAGE := true
PRODUCT_BUILD_RECOVERY_IMAGE := false
PRODUCT_BUILD_VENDOR_BOOT_IMAGE := false
PRODUCT_BUILD_INIT_BOOT_IMAGE := false
PRODUCT_BUILD_VENDOR_IMAGE := false
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
    bluetooth.profile.hfp.ag.enabled=true

PRODUCT_PACKAGES += WifiOverlayMarie

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/android.hardware.telephony.gsm.xml

PRODUCT_SYSTEM_PROPERTIES += \
    ro.hardware.egl=mali \
    ro.zygote.disable_gl_preload=true \
    persist.radio.multisim.config=dsds

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/fstab.kirin710:$(TARGET_COPY_OUT_RAMDISK)/fstab.kirin710 \
    $(LOCAL_PATH)/configs/init.marie.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.marie.rc \
    $(LOCAL_PATH)/configs/init.marie.usb.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.marie.usb.rc \
    $(LOCAL_PATH)/configs/task_profiles.json:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/task_profiles.json \
    $(LOCAL_PATH)/sepolicy/mapping/28.0.cil:$(TARGET_COPY_OUT_SYSTEM)/etc/selinux/mapping/28.0.cil

PRODUCT_PACKAGES += framework_compatibility_matrix.3.xml

PRODUCT_PACKAGES += \
    android.hardware.lights-service.marie \
    libwifi_fdsan_compat

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/vendor.huawei.hardware.wifi@1.1-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.huawei.hardware.wifi@1.1-service.rc
