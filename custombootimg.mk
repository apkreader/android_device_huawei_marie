# SPDX-License-Identifier: Apache-2.0
# Preserve the Huawei stock AVB container the same way MagiskBoot repacking does.
# The AERA Make fork appends a bare --kernel for custom image rules, even when
# BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE leaves recovery_kernel empty.
marie_recovery_ramdisk := $(PRODUCT_OUT)/ramdisk-recovery-marie.img
INTERNAL_RECOVERYIMAGE_ARGS := $(subst $(recovery_ramdisk),$(marie_recovery_ramdisk),$(filter-out --kernel,$(INTERNAL_RECOVERYIMAGE_ARGS)))

# Keep the kernel-compatible gzip wrapper. The extra runtime is unpacked into
# RAM during early-init, before the recovery UI starts.
$(marie_recovery_ramdisk): $(recovery_uncompressed_ramdisk) /usr/bin/zopfli \
        $(DEVICE_PATH)/tools/pack_recovery.py $(DEVICE_PATH)/crypto/bootstrap-files.txt
	/usr/bin/python3 $(DEVICE_PATH)/tools/pack_recovery.py \
	    --cpio $(recovery_uncompressed_ramdisk) \
	    --bootstrap $(DEVICE_PATH)/crypto/bootstrap-files.txt --output $@

$(INSTALLED_RECOVERYIMAGE_TARGET): $(filter-out $(recovery_ramdisk),$(recoveryimage-deps)) $(marie_recovery_ramdisk) $(RECOVERYIMAGE_EXTRA_DEPS) \
        $(DEVICE_PATH)/tools/huawei_container.py \
        $(DEVICE_PATH)/prebuilt/host-magiskboot \
        $(DEVICE_PATH)/prebuilt/stock-recovery_ramdisk.img
	$(call build-recoveryimage-target,$@,)
	python3 $(DEVICE_PATH)/tools/huawei_container.py $@ \
	    $(DEVICE_PATH)/prebuilt/stock-recovery_ramdisk.img \
	    $(DEVICE_PATH)/prebuilt/host-magiskboot
