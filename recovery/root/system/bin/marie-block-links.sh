#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
# UFS controller confirmed on this MAR-LX1B. Do not hard-code sdd partition numbers.
controller=/dev/block/platform/ff3c0000.ufs
for attempt in 1 2 3 4 5; do
    [ -d "$controller/by-name" ] && break
    sleep 1
done
if [ ! -d "$controller/by-name" ]; then
    echo 'MAR: UFS partition links unavailable' > /dev/kmsg
    exit 1
fi
if [ ! -e /dev/block/bootdevice ]; then
    ln -s "$controller" /dev/block/bootdevice
fi
# Huawei's _a labels belong to a non-A/B boot layout. Only add absent aliases.
for partition in "$controller"/by-name/*_a; do
    [ -e "$partition" ] || continue
    alias=${partition%_a}
    [ -e "$alias" ] || ln -s "$partition" "$alias"
done
