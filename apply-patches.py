#!/usr/bin/env python3
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import hashlib
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

target = source / "system/core"
patch = device / "patches/ueventd-legacy-vendor.patch"
command = ["git", "-C", str(target), "apply"]
legacy_imports = (target / "rootdir/ueventd.rc").read_text().splitlines().count(
    "import /vendor/ueventd.rc")
if legacy_imports == 0:
    subprocess.run(command + ["--check", str(patch)], check=True)
    subprocess.run(command + [str(patch)], check=True)
    print("ueventd: importing EMUI vendor rules")
elif legacy_imports == 1:
    subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)
    print("ueventd: EMUI vendor import already applied")
else:
    raise SystemExit("ueventd.rc contains duplicate legacy vendor imports")

target = source / "vendor/lineage"
patch = device / "patches/kernel-modules-image-dependency.patch"
command = ["git", "-C", str(target), "apply"]
check = subprocess.run(command + ["--check", str(patch)], capture_output=True)
if check.returncode == 0:
    subprocess.run(command + [str(patch)], check=True)
    print("Kernel modules: system image waits for module installation")
else:
    subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)
    print("Kernel modules: image dependency already applied")

target = source / "system/core"
patch = device / "patches/cpuset-legacy-mount.patch"
command = ["git", "-C", str(target), "apply"]
check = subprocess.run(command + ["--check", str(patch)], capture_output=True)
if check.returncode == 0:
    subprocess.run(command + [str(patch)], check=True)
    print("cpuset: retrying unsupported mount options on legacy kernels")
else:
    subprocess.run(command + ["--reverse", "--check", str(patch)], check=True)
    print("cpuset: legacy mount compatibility already applied")

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

for filename, description in (
    ("audio-hal4-client.patch", "Audio HAL 4.0 client support"),
    ("hintmanager-legacy-power-hal.patch", "HintManager fallback without AIDL Power HAL"),
    ("libmeminfo-optional-gpu-map.patch", "Optional GPU memory statistics"),
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
