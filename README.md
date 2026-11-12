# AERA Recovery - Huawei P30 lite

Device tree for **marie**, maintained by **apkreader**.

**Status: WIP.** Development is based on MAR-LX1B running EMUI 9.1.0.399
(`C431E2R4P1`). Other firmware versions and P30 lite variants are untested.

## Device

| Component | Specification |
| --- | --- |
| Models | MAR-LX1B / MAR-L21B |
| SoC | HiSilicon Kirin 710 |
| Architecture | ARM64 |
| Display | 1080 x 2312 |
| Touch panel | Synaptics MARI69110 |
| Kernel | Huawei 4.9.148 |
| Storage | UFS; non-A/B |
| Recovery | Separate ramdisk; 32 MiB partition |

## Status

Working in device tests:

- AERA boots with root ADB.
- Display scaling and brightness.
- Battery percentage and charging indicator.
- Power and volume buttons.
- Touch on boot and after waking the screen.
- Boot animation without the mid-animation screen interruption.
- Wi-Fi driver initialization, scanning, WPA2 connection, and DHCP.

Work in progress:

- WPA3 is not enabled by the current Wi-Fi driver.

Untested:

- File-based encryption (FBE) decryption.
- MTP and ADB sideload.
- MicroSD.
- Backup, restore, and flashing from the UI.
- Installation to eRecovery.

Rendering uses the software framebuffer with ARM64 NEON blending. Interface density defaults to **Spacious** and can be changed
under Theme Engine > Text & size.

## Build

Use the [AERA Android 16 manifest](https://github.com/AERA-Recovery/android_manifest),
branch `aera-16.0`. Place this repository at
`device/huawei/marie`.

Install `zopfli` on the build host. The recovery ramdisk uses gzip compression
and must fit the 32 MiB partition.

Proprietary files and build prebuilts are required. See [Stock files](#stock-files).
From the AERA source root:

```bash
export AERA_BUILD_DEVICE=marie

for patch in "$PWD"/device/huawei/marie/patches/*.patch; do
    git -C bootable/recovery apply "$patch"
done

for patch in "$PWD"/device/huawei/marie/patches/lvgl/*.patch; do
    git -C external/lvgl apply "$patch"
done

for patch in "$PWD"/device/huawei/marie/patches/wpa_supplicant/*.patch; do
    git -C external/wpa_supplicant_8 apply "$patch"
done

source build/envsetup.sh
lunch twrp_marie-bp2a-eng
m -j"$(nproc)" recoveryimage
```

Apply the patches once to a clean recovery checkout. The current patches target
recovery commit `42ed6b66808200ce0d388bb0b4f05e4c2af4d207` and supplicant
commit `3ef7b491990ee71f3cbad5c70b274430fa8d5c13`.

Output: `out/target/product/marie/recovery.img`.

## Stock files

Use files from the matching EMUI 9.1.0.399 firmware. The required files are listed
in [proprietary-files.txt](proprietary-files.txt). Keep their relative paths under
a local directory; the `vendor/odm/` entries may also be supplied under `odm/`.

Prepare the tree with the stock recovery ramdisk and the x86_64 MagiskBoot
binary from the official Magisk 31.1 APK:

```bash
python3 device/huawei/marie/tools/setup-vendor.py \
    --blobs /path/to/stock-files \
    --recovery-ramdisk /path/to/stock-recovery_ramdisk.img \
    --magiskboot /path/to/Magisk-31.1/lib/x86_64/libmagiskboot.so
```

This generates `stock-blobs.mk`, `stock-blobs.json`, `proprietary/`, and the
required files under `prebuilt/`. These generated files are excluded from Git.

## Installation

Current tests use `recovery_ramdisk` on an unlocked bootloader:

```bash
fastboot flash recovery_ramdisk out/target/product/marie/recovery.img
```

Huawei uses separate kernel and ramdisk partitions. This image belongs in
`recovery_ramdisk`; it is not a boot or kernel image. Keep a backup of the
existing recovery ramdisk, including any Magisk-patched image.

The image preserves the stock Huawei container metadata and requires an
unlocked bootloader. It does not carry a new Huawei signature.

## Credits

- [AERA Recovery](https://github.com/AERA-Recovery/android_bootable_recovery).
- [Iceows](https://github.com/Iceows) for Huawei recovery configuration references.
- [AOSP](https://source.android.com/) and [TeamWin](https://github.com/TeamWin).
- [Magisk](https://github.com/topjohnwu/Magisk/tree/v31.1) for MagiskBoot.

Device configuration and scripts are licensed under Apache-2.0. Huawei binaries
remain proprietary. MagiskBoot is GPL-3.0; its license is in
[`prebuilt/Magisk-LICENSE`](prebuilt/Magisk-LICENSE).
