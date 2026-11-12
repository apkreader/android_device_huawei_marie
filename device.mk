# Copyright (C) 2026 apkreader MAR recovery project
# SPDX-License-Identifier: Apache-2.0
DEVICE_PATH := device/huawei/marie
PRODUCT_SOONG_NAMESPACES += $(DEVICE_PATH)
PRODUCT_CHECK_PREBUILT_MAX_PAGE_SIZE := false
PRODUCT_SHIPPING_API_LEVEL := 28

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/recovery/root/init.recovery.marie.wifi.rc:recovery/root/init.recovery.marie.wifi.rc \
    $(DEVICE_PATH)/recovery/root/system/bin/marie-wifi-init.sh:recovery/root/system/bin/marie-wifi-init.sh \
    $(DEVICE_PATH)/recovery/root/system/etc/wifi/wpa_supplicant.conf:recovery/root/system/etc/wifi/wpa_supplicant.conf \
    $(DEVICE_PATH)/recovery/root/system/etc/vintf/manifest.xml:recovery/root/system/etc/vintf/manifest.xml \
    $(DEVICE_PATH)/recovery/root/vendor/etc/vintf/manifest.xml:recovery/root/vendor/etc/vintf/manifest.xml \
    $(DEVICE_PATH)/recovery/root/init.recovery.kirin710.rc:recovery/root/init.recovery.kirin710.rc \
    $(DEVICE_PATH)/recovery/root/init.recovery.kirin710.usb.rc:recovery/root/init.recovery.kirin710.usb.rc \
    $(DEVICE_PATH)/recovery/root/ueventd.kirin710.rc:recovery/root/ueventd.kirin710.rc \
    $(DEVICE_PATH)/recovery/root/system/bin/marie-block-links.sh:recovery/root/system/bin/marie-block-links.sh \
    $(DEVICE_PATH)/recovery/root/system/etc/twrp.flags:recovery/root/system/etc/twrp.flags \
    system/core/libprocessgroup/profiles/task_profiles.json:recovery/root/system/etc/task_profiles.json

PRODUCT_SYSTEM_PROPERTIES += \
    ro.hardware=kirin710 \
    ro.board.platform=kirin710 \
    ro.sys.usb.storage.type=mtp,adb

# Override base_vendor.mk's optional recovery USB defaults in the same namespace.
PRODUCT_VENDOR_PROPERTIES += \
    ro.recovery.usb.vid=12d1 \
    ro.recovery.usb.adb.pid=103a \
    ro.recovery.usb.mtp.pid=107e

PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    ro.secure=0 \
    ro.adb.secure=0 \
    ro.debuggable=1 \
    service.adb.root=1 \
    ro.aera.ui.default_interface_size=3 \
    persist.sys.usb.config=adb \
    sys.usb.configfs=1 \
    sys.usb.controller=ff100000.dwc3

# Generated from stock recovery ramdisks, with SHA-256 provenance.
include $(DEVICE_PATH)/stock-blobs.mk

PRODUCT_PACKAGES += marie_crypto_service
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/recovery/root/init.recovery.marie.crypto.rc:recovery/root/init.recovery.marie.crypto.rc \
    $(DEVICE_PATH)/recovery/root/system/bin/marie-crypto-init.sh:recovery/root/system/bin/marie-crypto-init.sh

include $(DEVICE_PATH)/crypto-blobs.mk
PRODUCT_COPY_FILES += $(DEVICE_PATH)/recovery/root/crypto/system/system/etc/ld.config.txt:recovery/root/crypto/system/system/etc/ld.config.txt

include $(DEVICE_PATH)/wifi-blobs.mk

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/recovery/root/init.recovery.marie.runtime.rc:recovery/root/init.recovery.marie.runtime.rc \
    $(DEVICE_PATH)/recovery/root/system/bin/marie-unpack-runtime.sh:recovery/root/system/bin/marie-unpack-runtime.sh
