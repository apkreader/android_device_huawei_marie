# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

DEVICE_PATH := device/huawei/marie

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := cortex-a73
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic
TARGET_2ND_CPU_VARIANT_RUNTIME := cortex-a73

# Platform
TARGET_BOARD_PLATFORM := kirin710
TARGET_BOOTLOADER_BOARD_NAME := kirin710
TARGET_NO_BOOTLOADER := true
TARGET_OTA_ASSERT_DEVICE := marie,HWMAR,MAR-LX1B,MAR-L21B
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE := $(DEVICE_PATH)/vintf/compatibility_matrix.device.xml

# Kernel
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_SOURCE := kernel/huawei/kirin710
TARGET_KERNEL_CONFIG := marie_defconfig
marie_kernel_toolchain := $(abspath prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9)
KERNEL_CROSS_COMPILE := CROSS_COMPILE=$(marie_kernel_toolchain)/bin/aarch64-linux-android-
KERNEL_CC := CC=$(marie_kernel_toolchain)/bin/real-aarch64-linux-android-gcc LD=$(marie_kernel_toolchain)/bin/aarch64-linux-android-ld
TARGET_KERNEL_LIBC_SYSROOT_USE := host
TARGET_KERNEL_ADDITIONAL_FLAGS += HOSTCC=clang HOSTCXX=clang++ HOSTCFLAGS="-O2 -Wall"
BOARD_KERNEL_IMAGE_NAME := Image.gz
# Vendor is prebuilt, so install compiled kernel modules on system.
NEED_KERNEL_MODULE_SYSTEM := true
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_MKBOOTIMG_ARGS += \
    --header_version 1 \
    --kernel_offset 0x00080000 \
    --ramdisk_offset 0x07c00000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x07a00000

# Physical partitions. Huawei's _a GPT names do not indicate A/B updates.
AB_OTA_UPDATER := false
BOARD_USES_RECOVERY_AS_BOOT := false
BOARD_FLASH_BLOCK_SIZE := 4096
BOARD_BOOTIMAGE_PARTITION_SIZE := 25165824
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 2424307712
BOARD_PRODUCTIMAGE_PARTITION_SIZE := 2256535552
BOARD_VENDORIMAGE_PARTITION_SIZE := 503316480
BOARD_ODMIMAGE_PARTITION_SIZE := 100663296
BOARD_CACHEIMAGE_PARTITION_SIZE := 109051904
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 33554432
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := f2fs
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_COPY_OUT_VENDOR := vendor
TARGET_COPY_OUT_ODM := odm
TARGET_COPY_OUT_PRODUCT := product

# Keep the installed recovery; OTA installation is not implemented yet.
TARGET_NO_RECOVERY := true
TARGET_SKIP_OTA_PACKAGE := true

include vendor/huawei/marie/BoardConfigVendor.mk
