# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := device/huawei/marie

PRODUCT_SHIPPING_API_LEVEL := 28

$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)
$(call inherit-product, vendor/huawei/marie/marie-vendor.mk)

# The bootloader uses separate kernel and recovery ramdisk partitions.
PRODUCT_BUILD_BOOT_IMAGE := false
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

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/fstab.kirin710:$(TARGET_COPY_OUT_RAMDISK)/fstab.kirin710
