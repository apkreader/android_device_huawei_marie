# Copyright (C) 2026 apkreader MAR recovery project
# SPDX-License-Identifier: Apache-2.0
DEVICE_PATH := device/huawei/marie

# Kirin 710, EMUI 9.1.0.399.
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
TARGET_BOARD_PLATFORM := kirin710
TARGET_BOOTLOADER_BOARD_NAME := kirin710
TARGET_NO_BOOTLOADER := true
TARGET_OTA_ASSERT_DEVICE := marie,mar,HWMAR,MAR-LX1B,MAR-L21B

# Huawei split recovery uses an installed kernel and a separate ramdisk.
# Stock image header has kernel_size=0, header v0, page size 2048.
# Do not combine the stock KERNEL image into this partition.
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/empty-kernel
BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE := true
BOARD_CUSTOM_BOOTIMG_MK := $(DEVICE_PATH)/custombootimg.mk
BOARD_BOOT_HEADER_VERSION := 0
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_CMDLINE :=
BOARD_RECOVERY_MKBOOTIMG_ARGS += --header_version 0
BOARD_RECOVERY_MKBOOTIMG_ARGS += --kernel_offset 0x00008000
BOARD_RECOVERY_MKBOOTIMG_ARGS += --ramdisk_offset 0x01000000
BOARD_RECOVERY_MKBOOTIMG_ARGS += --second_offset 0x00f00000
BOARD_RECOVERY_MKBOOTIMG_ARGS += --tags_offset 0x00000100
BOARD_RECOVERY_MKBOOTIMG_ARGS += --os_version 9.0.0 --os_patch_level 2020-07

# Physical, non-A/B partitions. The stock GPT's _a names are aliases, not OTA slots.
AB_OTA_UPDATER := false
BOARD_USES_RECOVERY_AS_BOOT := false
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 33554432
BOARD_RECOVERY_IMAGE_PREPARE = python3 $(DEVICE_PATH)/tools/trim-terminfo.py $(TARGET_RECOVERY_ROOT_OUT)
BOARD_BOOTIMAGE_PARTITION_SIZE := 25165824
BOARD_FLASH_BLOCK_SIZE := 4096
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_PARTITION_SIZE := 16777216
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := f2fs
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := erofs
TARGET_COPY_OUT_VENDOR := vendor
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_USERIMAGES_USE_EROFS := true
TARGET_USES_MKE2FS := true
# Additional recovery runtime libraries.
TARGET_RECOVERY_DEVICE_MODULES += \
    libboot_control_client \
    android.hardware.boot-V1-ndk \
    android.hardware.security.secureclock-V1-ndk \
    libincfs
TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += \
    $(TARGET_OUT_SHARED_LIBRARIES)/libboot_control_client.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.boot-V1-ndk.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.security.secureclock-V1-ndk.so \
    $(TARGET_OUT_SHARED_LIBRARIES)/libincfs.so
# Huawei container repacking is handled by custombootimg.mk.
BOARD_AVB_ENABLE := false

TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/recovery/root/system/etc/recovery.fstab
TARGET_RECOVERY_PIXEL_FORMAT := BGRA_8888
RECOVERY_SDCARD_ON_DATA := true
BOARD_HAS_LARGE_FILESYSTEM := true
BOARD_HAS_NO_SELECT_BUTTON := true
BOARD_ROOT_EXTRA_FOLDERS := hw_odm splash2 sec_storage version cust preload

# EMUI 9 uses fscrypt v1 policies.
AERA_INCLUDE_CRYPTO := true
AERA_INCLUDE_CRYPTO_FBE := true
AERA_USE_FSCRYPT_POLICY := 1
$(call soong_config_set,makeVarsPlugin,TW_USE_FSCRYPT_POLICY,$(AERA_USE_FSCRYPT_POLICY))
AERA_SKIP_ADDITIONAL_FSTAB := true
AERA_EXCLUDE_DEFAULT_USB_INIT := true
# AERA pages use fixed 1440-wide design metrics. Scale the complete canvas to
# this panel; the required framebuffer patch avoids pixelflinger per frame.
AERA_UI_ADAPTIVE_RESOLUTION := true
AERA_SCREEN_H := 2312
AERA_STATUS_H := 96
AERA_STATUS_INDENT_LEFT := 96
AERA_STATUS_INDENT_RIGHT := 96
AERA_DEFAULT_LANGUAGE := en
AERA_MAINTAINER := apkreader
AERA_BRIGHTNESS_PATH := /sys/class/leds/lcd_backlight0/brightness
AERA_MAX_BRIGHTNESS := 7350
AERA_DEFAULT_BRIGHTNESS := 2205
AERA_CUSTOM_BATTERY_PATH := /sys/class/power_supply/Battery
AERA_NO_HAPTICS := true
AERA_ENABLE_WLAN := 1
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
WPA_SUPPLICANT_VERSION := VER_0_8_X
AERA_NO_REFLASH_CURRENT_RECOVERY := 1
AERA_INCLUDE_LOGCAT := true
TARGET_USES_LOGD := true
AERA_ENABLE_ROOT_MANAGER := 0

# Huawei's proprietary touch helper is staged from matching stock firmware.
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
BUILD_BROKEN_PLUGIN_VALIDATION := soong-libaosprecovery_defaults soong-libguitwrp_defaults soong-libminuitwrp_defaults soong-vold_defaults

AERA_WPA_SUPPLICANT_MODULE := wpa_supplicant_recovery

# Recovery-only permissions for the stock crypto services.
BOARD_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy
