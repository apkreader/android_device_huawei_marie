# Huawei P30 lite (marie)

Device configuration for LineageOS 24.0 (Android 17).

## Status

Initial bring-up. This tree has not been built or booted.

The hardware base is MAR-L21B 9.1.0.399 (C431). Vendor and ODM are kept as
matching stock images while their HALs are brought up. The kernel source is
Huawei's Kirin 710 4.9.148 release, with the configuration captured from this
phone.

Still needed:

- Kernel changes for Android 17 and a compatible build toolchain.
- VNDK 28 compatibility and Huawei HAL integration.
- Init, SELinux policy and VINTF integration.
- Huawei split-boot image packaging and OTA installation.
- Device testing, including display, radio, audio, camera and encryption.

Boot, recovery and OTA images are disabled until the split-boot layout is
implemented. The existing firmware has a separate 24 MiB `kernel` partition
and a 32 MiB `recovery_ramdisk` partition; there is no conventional `boot`
partition.

## Hardware

| Component | Details |
| --- | --- |
| Models | MAR-LX1B / MAR-L21B |
| SoC | HiSilicon Kirin 710 |
| CPU | 4 x Cortex-A73 + 4 x Cortex-A53 |
| GPU | Mali-G51 |
| RAM | 6 GB on the development device |
| Display | 1080 x 2312, 480 dpi |
| Kernel | 4.9.148, 4 KiB pages |
| Shipping Android version | 9 (API 28) |
| Partition layout | Physical, non-A/B |

## Sources

| Repository | Checkout path | Branch |
| --- | --- | --- |
| [Device](https://github.com/apkreader/android_device_huawei_marie) | `device/huawei/marie` | `lineage-24.0` |
| [Vendor](https://github.com/apkreader/android_vendor_huawei_marie) | `vendor/huawei/marie` | `lineage-24.0` |
| [Kernel](https://github.com/apkreader/android_kernel_huawei_kirin710) | `kernel/huawei/kirin710` | `lineage-24.0` |

`local_manifest.xml` adds these repositories to a LineageOS 24.0 source
checkout. The branches need to be published before it can be used remotely.

## Setup

Start with the [LineageOS 24.0 manifest](https://github.com/LineageOS/android/tree/lineage-24.0).
Place this tree and its dependencies at the paths above, then extract the
matching stock images:

```sh
python3 device/huawei/marie/extract-files.py /path/to/stock/images
```

The input directory must contain `vendor.img` and `odm.img` extracted from
MAR-LGRP2-OVS 9.1.0.399. Their checksums are checked before copying.

Once the kernel and vendor compatibility work is in place:

```sh
source build/envsetup.sh
breakfast marie userdebug
m systemimage productimage
```

These are bring-up targets, not an installable ROM package.
