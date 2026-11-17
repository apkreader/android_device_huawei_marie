#!/usr/bin/env python3
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import subprocess


device = Path(__file__).resolve().parent
source = device.parents[2]

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
