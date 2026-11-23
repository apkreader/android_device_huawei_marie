#!/usr/bin/env python3
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import hashlib
import os
import subprocess


device = Path(__file__).resolve().parent
source = device.parents[2]
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
    command = ["git", "-C", str(target), "apply"]
    subprocess.run(command + ["--check", str(patch)], check=True)
    subprocess.run(command + [str(patch)], check=True)
    assert hashlib.sha256(blueprint.read_bytes()).hexdigest() == prepared
    print("VNDK 28: using LineageOS Protobuf compatibility modules")
else:
    raise SystemExit("VNDK 28 Android.bp has other changes; the build fix was not applied")

native_vendor = os.environ.get("MARIE_NATIVE_VENDOR") == "true"


def set_legacy_patch(project, filename, strip=1):
    patch = device / "patches" / filename
    command = ["git", "-C", str(source / project), "apply", f"-p{strip}"]
    desired = ["--reverse"] if native_vendor else []
    opposite = [] if native_vendor else ["--reverse"]
    if subprocess.run(command + desired + ["--check", str(patch)], capture_output=True).returncode == 0:
        subprocess.run(command + desired + [str(patch)], check=True)
    else:
        subprocess.run(command + opposite + ["--check", str(patch)], check=True)


set_legacy_patch("system/core", "ueventd-legacy-vendor.patch")
set_legacy_patch("frameworks/base", "hintmanager-legacy-power-hal.patch", strip=3)

target = source / "system/core"
command = ["git", "-C", str(target), "apply"]

for filename in ("init-block-device-lookup.patch", "init-huawei-bootdevice-links.patch"):
    patch = device / "patches" / filename
    check = subprocess.run(command + ["--check", str(patch)], capture_output=True)
    if check.returncode == 0:
        subprocess.run(command + [str(patch)], check=True)
    else:
        subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)

target = source / "system/vold"
patch = device / "patches/vold-optional-metadata.patch"
command = ["git", "-C", str(target), "apply"]
check = subprocess.run(command + ["--check", str(patch)], capture_output=True)
if check.returncode == 0:
    subprocess.run(command + [str(patch)], check=True)
    print("vold: handling devices without a metadata partition")
else:
    subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)
    print("vold: optional metadata entry check already applied")

patch = device / "patches/vold-legacy-keyring.patch"
command = ["git", "-C", str(target), "apply"]
check = subprocess.run(command + ["--check", str(patch)], capture_output=True)
if check.returncode == 0:
    subprocess.run(command + [str(patch)], check=True)
    print("vold: restoring fscrypt v1 keyring support")
else:
    subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)
    print("vold: fscrypt v1 keyring support already applied")

target = source / "frameworks/opt/telephony"
patch = device / "patches/legacy-radio-hal11.patch"
command = ["git", "-C", str(target), "apply"]
check = subprocess.run(command + ["--check", str(patch)], capture_output=True)
if check.returncode == 0:
    subprocess.run(command + [str(patch)], check=True)
    print("Telephony: restored Radio HAL 1.1 support")
else:
    subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)
    print("Telephony: Radio HAL 1.1 support already applied")

for filename, description in (
    ("audio-hal4-client.patch", "Audio HAL 4.0 client support"),
    ("input-vsync-sampling.patch", "Sample batched input at vsync"),
    ("bluetooth-a2dp-close-policy.patch", "Bluetooth stream close compatibility"),
):
    patch = device / "patches" / filename
    command = ["git", "-C", str(source), "apply"]
    check = subprocess.run(command + ["--check", str(patch)], capture_output=True)
    if check.returncode == 0:
        subprocess.run(command + [str(patch)], check=True)
        print(description + ": applied")
    else:
        subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)
        print(description + ": already applied")
