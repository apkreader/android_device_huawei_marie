#!/usr/bin/env python3
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import hashlib
import subprocess


device = Path(__file__).resolve().parent
source = device.parents[2]


def apply_patch(project_rel, filename, description):
    target = source / project_rel if project_rel else source
    patch = device / "patches" / filename
    cmd = ["git", "-C", str(target), "apply"]
    if subprocess.run(cmd + ["--check", str(patch)], capture_output=True).returncode == 0:
        subprocess.run(cmd + [str(patch)], check=True)
        print(f"{description}: applied")
    else:
        subprocess.run(cmd + ["--reverse", "--check", str(patch)], check=True)
        print(f"{description}: already applied")


# VNDK 28 protobuf compatibility
target = source / "prebuilts/vndk/v28"
patch = device / "patches/vndk28-protobuf.patch"
revision = subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"], text=True).strip()
if revision != "1cb210981d2baf325aaab36cdd84afa40c775f00":
    raise SystemExit("Sync the VNDK 28 revision from local_manifest.xml first")

blueprint = target / "Android.bp"
original = "1323103c9ab3667ac40bba02ea63bcb1479e73b533d56ea85c795b2fe2f5a98d"
prepared = "935f3b3970d33e8ceeaf6ae7a185c30fd7deafe8a72595fc484b5148361b29d2"
current = hashlib.sha256(blueprint.read_bytes()).hexdigest()
if current == prepared:
    print("VNDK 28: Protobuf compatibility fix already applied")
elif current == original:
    cmd = ["git", "-C", str(target), "apply"]
    subprocess.run(cmd + ["--check", str(patch)], check=True)
    subprocess.run(cmd + [str(patch)], check=True)
    assert hashlib.sha256(blueprint.read_bytes()).hexdigest() == prepared
    print("VNDK 28: using LineageOS Protobuf compatibility modules")
else:
    raise SystemExit("VNDK 28 Android.bp has other changes; the build fix was not applied")

apply_patch("build/make", "build-make-native-vendor.patch", "build/make: native vendor image helpers")
apply_patch("external/minigbm", "minigbm-simpledrm.patch", "minigbm: simpledrm backend support")
apply_patch("system/core", "init-block-device-lookup.patch", "init: block device lookup")
apply_patch("system/core", "init-huawei-bootdevice-links.patch", "init: Huawei bootdevice links")
apply_patch("system/vold", "vold-optional-metadata.patch", "vold: optional metadata partition")
apply_patch("system/vold", "vold-legacy-keyring.patch", "vold: fscrypt v1 keyring support")
apply_patch("frameworks/opt/telephony", "legacy-radio-hal11.patch", "Telephony: Radio HAL 1.1 support")
apply_patch("", "audio-hal4-client.patch", "Audio HAL 4.0 client support")
apply_patch("", "input-vsync-sampling.patch", "Sample batched input at vsync")
apply_patch("", "bluetooth-a2dp-close-policy.patch", "Bluetooth stream close compatibility")
