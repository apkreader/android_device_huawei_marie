# SPDX-License-Identifier: Apache-2.0
# Preserve the Huawei stock AVB container the same way MagiskBoot repacking does.
# The AERA Make fork appends a bare --kernel for custom image rules, even when
# BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE leaves recovery_kernel empty.
marie_recovery_ramdisk := $(PRODUCT_OUT)/ramdisk-recovery-marie.img
INTERNAL_RECOVERYIMAGE_ARGS := $(subst $(recovery_ramdisk),$(marie_recovery_ramdisk),$(filter-out --kernel,$(INTERNAL_RECOVERYIMAGE_ARGS)))

# Keep stock-compatible gzip while fitting networking in the 32 MiB ramdisk.
$(marie_recovery_ramdisk): $(recovery_uncompressed_ramdisk) /usr/bin/zopfli
	/usr/bin/zopfli --gzip --i3 -c $(recovery_uncompressed_ramdisk) > $@

$(INSTALLED_RECOVERYIMAGE_TARGET): $(filter-out $(recovery_ramdisk),$(recoveryimage-deps)) $(marie_recovery_ramdisk) $(RECOVERYIMAGE_EXTRA_DEPS) \
        $(DEVICE_PATH)/tools/huawei_container.py \
        $(DEVICE_PATH)/prebuilt/host-magiskboot \
        $(DEVICE_PATH)/prebuilt/stock-recovery_ramdisk.img
	$(call build-recoveryimage-target,$@,)
	python3 $(DEVICE_PATH)/tools/huawei_container.py $@ \
	    $(DEVICE_PATH)/prebuilt/stock-recovery_ramdisk.img \
	    $(DEVICE_PATH)/prebuilt/host-magiskboot
