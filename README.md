# Huawei P30 lite (marie)

Device configuration for LineageOS 24.0 (Android 17) running our Linux 6.12
kernel port.

Huawei's stock kernel is stuck on Android 9 / Linux 4.9, so this tree replaces
it with a Linux 6.12 kernel (`lineage24-6.12.y`) and builds the full ROM —
including `vendor.img`, HWC3/minigbm, AIDL Wi-Fi (`IWifi`, `wpa_supplicant`,
`hostapd`), `libminijail`, and the kernel — natively through LineageOS
(`breakfast marie && mka`).

## Status

Working on Linux 6.12 (cold-boot SELinux Enforcing):

- **Display**: 1080 x 2312 @ 60 Hz via `simpledrm` with zero-copy CMA direct
  scanout on Kirin 710 DSS V320, real hardware VBlank (`GIC SPI 245`), and
  HWC3 DRM (`task_profiles SFMainPolicy`)
- **GPU**: Mali-G51 hardware acceleration via Linux 6.12 `panfrost` + Mesa
  24.3.4 Gallium Panfrost (OpenGL ES 3.1 + EGL, ACE-Lite coherency,
  413–999 MHz `devfreq`)
- **Wi-Fi**: Hi1102 dual-band 2.4/5 GHz 802.11ac via Linux 6.12 `cfg80211`
  driver port with Android 17 AIDL `IWifi` HAL, `wpa_supplicant` v2.12
  (WPA3-SAE & WPA2-PSK), and `hostapd`
- **CPU / Power**: 8-core SMP (4 x Cortex-A73 + 4 x Cortex-A53) with Kirin 710
  `cpufreq`, AIDL Power HAL, HI6555v200 PMIC battery/USB charging, power/volume
  keys, PMIC `reboot-mode`
- **Input / Backlight**: Synaptics TD4320 ZeroFlash SPI touchscreen, TI LM36274
  backlight via `android.hardware.lights-service.marie`
- **Audio**: Speaker playback and microphone recording work. Headphones and
  calls still need testing.
- **Flashlight**: Quick Settings torch with 4-level brightness via
  `leds-marie-flash` and Android 17 AIDL V4 `ICameraProvider` HAL
- **Security**: Hardware File-Based Encryption (FBE), TrustCore TEE with UFS
  RPMB, Hardware Keymaster 3.0, Hardware Gatekeeper (PIN / Pattern / Password
  lock & unlock), `/sec_storage`, SELinux Enforcing
- **System / Storage**: Native BPF & FUSE-BPF, `/storage/emulated/0`,
  screenshots, OMX/Configstore, live USB ADB, 2 GB ZRAM swap
- **Recovery**: LineageOS Recovery works on both stock 4.9 and Linux 6.12
  kernels (UI + touch + ADB). LineageOS itself targets Linux 6.12 exclusively.

Still work in progress on Linux 6.12:

- **Bluetooth**: Hi1102 BFGX Bluetooth bring-up
- **Baseband / RIL**: Balong modem driver port
- **Vibration, Camera, Sensors, microSD**

The hardware base is MAR-L21B 9.1.0.399 (C431). The boot image uses header
version 1 and belongs on the 24 MiB `kernel` partition. The 32 MiB
`recovery_ramdisk` partition uses a separate image.

## Building

1. Sync LineageOS 24.0 with `local_manifest.xml`.
2. Apply the tree patches: `python3 device/huawei/marie/apply-patches.py`
3. Extract vendor blobs from a stock EMUI 9.1.0.399 vendor dump:
   `cd device/huawei/marie && ./extract-files.py /path/to/vendor_dump`
4. Build normally:
   ```sh
   source build/envsetup.sh
   breakfast marie
   mka bootimage vendorimage systemimage productimage
   ```

## Hardware

| Component | Details |
| --- | --- |
| Models | MAR-LX1B / MAR-L21B |
| SoC | HiSilicon Kirin 710 |
| CPU | 4 x Cortex-A73 + 4 x Cortex-A53 |
| GPU | Mali-G51 (Panfrost + Mesa 24.3.4) |
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
