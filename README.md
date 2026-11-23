# Huawei P30 lite (marie)

Device configuration for LineageOS 24.0 (Android 17) on Linux 6.12.

## Status

LineageOS 24.0 boots to a full SELinux Enforcing desktop on Linux 6.12 with:

- **Display**: 1080 x 2312 @ 60.00 Hz tear-free triple-buffered Kirin 710 DSS V320 via `simpledrm` + HWC3 DRM + real hardware VBlank (`GIC SPI 245`)
- **GPU**: Hardware Mali-G51 acceleration via Linux 6.12 `panfrost` + Mesa Gallium Panfrost (OpenGL ES 3.1 + EGL, ACE-Lite coherency, 413–999 MHz `devfreq`)
- **CPU / Power**: 8-core SMP (4 x Cortex-A73 + 4 x Cortex-A53) with Kirin 710 `cpufreq`, HI6555v200 PMIC battery/USB charging, power/volume keys, PMIC `reboot-mode`
- **Input / Backlight**: Synaptics TD4320 ZeroFlash SPI touchscreen, TI LM36274 backlight via `android.hardware.lights-service.marie`
- **System / Storage**: SELinux Enforcing, native BPF & FUSE-BPF, `/storage/emulated/0`, screenshots, OMX/Configstore, live USB ADB
- **Recovery**: LineageOS Recovery works on both stock/EMUI 4.9 kernels and Linux 6.12 (UI + touch + ADB) so users can flash LineageOS from stock 4.9. LineageOS itself targets Linux 6.12 exclusively.

The hardware base is MAR-L21B 9.1.0.399 (C431). The boot image uses header
version 1 and belongs on the 24 MiB `kernel` partition. The 32 MiB
`recovery_ramdisk` partition uses a separate image.

## Hardware

| Component | Details |
| --- | --- |
| Models | MAR-LX1B / MAR-L21B |
| SoC | HiSilicon Kirin 710 |
| CPU | 4 x Cortex-A73 + 4 x Cortex-A53 |
| GPU | Mali-G51 (Panfrost + Mesa) |
| RAM | 6 GB on the development device |
| Display | 1080 x 2312 @ 60 Hz, 480 dpi |
| Kernel | Linux 6.12 (`lineage24-6.12.y`), 4 KiB pages |
| Shipping Android version | 9 (API 28) |
| Partition layout | Physical, non-A/B |

## Sources

| Repository | Checkout path | Branch |
| --- | --- | --- |
| [Device](https://github.com/apkreader/android_device_huawei_marie) | `device/huawei/marie` | `lineage-24.0` |
| [Vendor](https://github.com/apkreader/android_vendor_huawei_marie) | `vendor/huawei/marie` | `lineage-24.0` |
| [Kernel](https://github.com/apkreader/android_kernel_huawei_kirin710) | `kernel/huawei/kirin710` | `lineage24-6.12.y` |
