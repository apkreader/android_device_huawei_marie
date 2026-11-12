#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -e

# Use the calibration selected by this phone's device tree and its installed
# firmware. The recovery vendor ramdisk does not contain the MAR board INI.
stock=/wifi/stock-vendor
if [ ! -d "$stock/etc/wifi" ]; then
    mkdir -p "$stock"
    mount -o ro /dev/block/by-name/vendor "$stock"
fi
ini=$(tr -d '\000' < /proc/device-tree/hisi_cust_cfg/ini_file_name)
case "$ini" in
    /vendor/etc/cfg_*_hisi.ini|/vendor/etc/cfg_*_hisi_nxp.ini) ;;
    *) echo "marie Wi-Fi: unsupported board configuration" >&2; exit 1 ;;
esac
mkdir -p /vendor/etc/wifi /vendor/firmware /tmp/recovery/sockets
chmod 0755 /tmp/recovery
chown 1010:1010 /tmp/recovery/sockets
chmod 0770 /tmp/recovery/sockets
if [ ! "$stock/${ini#/vendor/}" -ef "$ini" ]; then
    cp "$stock/${ini#/vendor/}" "$ini"
fi
for firmware in wifi_cfg bfgx_cfg bfgx_and_wifi_cfg cr4_asic.bin CPU_RAM_SCHED.bin CPU_RAM_WBS.bin; do
    if [ ! "$stock/firmware/$firmware" -ef "/vendor/firmware/$firmware" ]; then
        cp "$stock/firmware/$firmware" "/vendor/firmware/$firmware"
    fi
done

# The stock recovery initializes NVE before connectivity. Wi-Fi reads its
# factory calibration and MAC through this driver, even while /data is locked.
if [ ! -c /dev/nve0 ]; then
    printf %s /dev/block/bootdevice/by-name/nvme > /sys/module/hisi_nve/parameters/nve
    for attempt in 1 2 3 4 5; do
        [ -c /dev/nve0 ] && break
        sleep 1
    done
    [ -c /dev/nve0 ]
fi

# Huawei exposes deferred driver initialization through these two controls.
# Re-enabling Wi-Fi leaves the initialized platform and calibration intact.
for driver in plat wifi; do
    control=/sys/hisys/boot/$driver
    if [ "$(cat "$control")" != running ]; then
        echo init > "$control"
    fi
    [ "$(cat "$control")" = running ]
done
[ -d /sys/class/net/wlan0 ]
setprop sys.aera.wlan.driver_ready 1
