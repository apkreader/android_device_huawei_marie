# Huawei P30 lite (marie)

Device configuration for LineageOS 24.0 (Android 17).

## Status

LineageOS boots on the development phone with additional kernel and init
changes. WPA2 Wi-Fi works on that build. This tree is still a work in progress.
Radio HAL 1.1 reports the baseband and both IMEIs. The modem reported both
slots empty during testing. SIM registration, calls, SMS and mobile data
still need verification.

The hardware base is MAR-L21B 9.1.0.399 (C431). Vendor and ODM are kept as
matching stock images while their HALs are brought up. Vendor preparation
adjusts its SELinux policy without recompressing Huawei's hardware files.
The kernel source is Huawei's Kirin 710 4.9.148 release, with the configuration
captured from this phone.

Still needed:

- Kernel changes for Android 17.
- Huawei HAL integration and runtime testing with VNDK 28.
- Init, SELinux policy and VINTF integration.
- OTA installation.
- Device testing, including display, radio, audio, camera and encryption.

The boot image uses header version 1 and includes first-stage init and the
device fstab. It belongs on the 24 MiB `kernel` partition. There is no
conventional `boot` partition. Recovery and OTA builds remain disabled; the
32 MiB `recovery_ramdisk` partition is kept separate.

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

`local_manifest.xml` adds these repositories, the AOSP VNDK 28 snapshot and
the pinned Android 9 GCC 4.9 toolchain to a LineageOS 24.0 source checkout.
The kernel revision includes the BPF support Android 17 needs at boot and
the Hi1102 fix for WPA authentication.

The Android 9 SELinux mapping, FCM 3 matrix and 4.9 kernel requirements come
from LineageOS 20.0. Device additions retain the old `vfat` rules for `fuseblk`
and classify Huawei's vendor Binder services. The ueventd configuration adds
EMUI's `/vendor/ueventd.rc` to the standard platform rules.

## Setup

Start with the [LineageOS 24.0 manifest](https://github.com/LineageOS/android/tree/lineage-24.0).
Place this tree and its dependencies at the paths above. The kernel's host
tools require the system OpenSSL development files (`libssl-dev` on Ubuntu).

Apply the source patches. LineageOS already provides the Protobuf modules
from the older VNDK snapshot, and EMUI stores its ueventd rules at
`/vendor/ueventd.rc`:

```sh
python3 device/huawei/marie/apply-patches.py
```

The kernel build patch orders module installation before system image packaging.

Extract the matching stock images:

```sh
python3 device/huawei/marie/extract-files.py /path/to/stock/images
```

The input directory must contain `vendor.img` and `odm.img` extracted from
MAR-LGRP2-OVS 9.1.0.399. Their checksums are checked before extraction. Vendor
policy is prepared by `vendor/huawei/marie/tools/prepare-vendor.py`; ODM is
copied unchanged.

Build the bring-up images:

```sh
source build/envsetup.sh
breakfast marie userdebug
m bootimage systemimage productimage
```

These targets produce `boot.img`, `system.img` and `product.img` for bring-up.
