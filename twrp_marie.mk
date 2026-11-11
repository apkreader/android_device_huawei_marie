# Copyright (C) 2026 apkreader MAR recovery project
# SPDX-License-Identifier: Apache-2.0
$(call inherit-product, $(SRC_TARGET_DIR)/product/base.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)
$(call inherit-product, vendor/twrp/config/common.mk)
$(call inherit-product, device/huawei/marie/device.mk)

PRODUCT_NAME := twrp_marie
PRODUCT_DEVICE := marie
PRODUCT_RELEASE_NAME := marie
PRODUCT_BRAND := HUAWEI
PRODUCT_MANUFACTURER := HUAWEI
PRODUCT_MODEL := MAR-LX1B
PRODUCT_SHIPPING_API_LEVEL := 28
