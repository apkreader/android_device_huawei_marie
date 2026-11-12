# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, device/huawei/marie/device.mk)
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_NAME := lineage_marie
PRODUCT_DEVICE := marie
PRODUCT_MANUFACTURER := HUAWEI
PRODUCT_BRAND := HUAWEI
PRODUCT_MODEL := MAR-LX1B
PRODUCT_GMS_CLIENTID_BASE := android-huawei
