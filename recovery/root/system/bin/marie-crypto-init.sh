#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -e
mkdir -p /crypto/system /crypto/vendor /tmp/misc/keystore
chmod 0755 /crypto /crypto/system /crypto/vendor
chmod -R a+rX /crypto
chmod 0700 /tmp/misc/keystore
chmod 0755 /crypto/system/system/bin/linker64 \
    /crypto/vendor/bin/hw/android.hardware.keymaster@3.0-service \
    /crypto/vendor/bin/hw/android.hardware.gatekeeper@1.0-service
test -x /crypto/system/system/bin/linker64
test -x /crypto/vendor/bin/hw/android.hardware.keymaster@3.0-service
test -x /crypto/vendor/bin/hw/android.hardware.gatekeeper@1.0-service
chcon -R u:object_r:system_file:s0 /crypto/system
chcon -R u:object_r:vendor_file:s0 /crypto/vendor
chcon u:object_r:hal_keymaster_default_exec:s0 /crypto/vendor/bin/hw/android.hardware.keymaster@3.0-service
chcon u:object_r:hal_gatekeeper_default_exec:s0 /crypto/vendor/bin/hw/android.hardware.gatekeeper@1.0-service
restorecon -R /system/bin /system/lib64 /system/etc /vendor/lib64 /vendor/etc
chcon u:object_r:system_file:s0 /system/bin/linker64
chcon u:object_r:hwservicemanager_exec:s0 /system/bin/hwservicemanager
chcon u:object_r:tee_exec:s0 /vendor/bin/teecd
chcon u:object_r:tee_device:s0 /dev/tc_ns_client
# Gatekeeper's trusted application uses the stock secure-storage filesystem.
mkdir -p /sec_storage
if ! /system/bin/busybox mountpoint -q /sec_storage; then
    mount -t ext4 -o rw,nosuid,nodev,noatime,discard,auto_da_alloc,data=journal,context=u:object_r:teecd_data_file:s0 \
        /dev/block/by-name/secure_storage /sec_storage || exit 1
fi
if [ -e /dev/0:0:0:49476 ]; then
    chcon u:object_r:marie_rpmb_device:s0 /dev/0:0:0:49476
fi
setprop sys.aera.crypto.ready 1
