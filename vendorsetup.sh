#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# AERA_BUILD_DEVICE must be set before sourcing build/envsetup.sh.
if [[ ${AERA_BUILD_DEVICE:-} == marie || ${1:-} == marie ]]; then
    export LC_ALL=C
    export AERA_BUILD_STATUS=Unofficial
    export AERA_MAINTAINER=apkreader
    export AERA_BUILD_TYPE=Alpha
    export AERA_VANILLA_BUILD=1
    export AERA_ENABLE_ROOT_MANAGER=0
    export AERA_DELETE_AROMAFM=1
    export AERA_DELETE_MAGISK_ADDON=1
    export AERA_EXCLUDE_NANO_EDITOR=1
    export AERA_RECOVERY_INSTALL_PARTITION=/dev/block/bootdevice/by-name/recovery_ramdisk
    export TARGET_DEVICE_ALT=HWMAR
    export AERA_TARGET_DEVICES=HWMAR
fi
