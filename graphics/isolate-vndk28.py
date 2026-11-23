#!/usr/bin/env python3
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0
"""Isolate Android 17 graphics HAL libraries from the VNDK 28 APEX namespace.

Huawei EMUI 9 vendor HALs link against /apex/com.android.vndk.v28, whereas the
open-source Android 17 HWC3 (drm_hwcomposer) and minigbm allocator/mapper HALs
link against modern Android 17 C++/HIDL/graphics libraries.  Installing both
under the same SONAME in /vendor/lib{,64} would shadow the VNDK 28 APEX and
break legacy OEM HALs.

This post-process hook runs on build_image.py's temporary staging directory
(never mutating out/target/product/marie/vendor).  Starting from the HWC3 and
minigbm entrypoints, it copies the excluded Android 17 libraries under
same-length isolated SONAMEs (*7.so / *8.so) and rewrites DT_NEEDED across the
graphics dependency closure using LineageOS's prebuilt patchelf.
"""

from collections import deque
from pathlib import Path
import os
import shutil
import subprocess
import sys

# Same-length SONAME renames so ELF dynamic tables stay compact and predictable.
ISOLATION_MAP = {
    "libbase.so": "libbas7.so",
    "libc++.so": "libc+7.so",
    "libcutils.so": "libcuti7s.so",
    "libutils.so": "libutil7.so",
    "libhidlbase.so": "libhidlbas7.so",
    "libhardware.so": "libhardwar7.so",
    "libui.so": "libu7.so",
    "libaconfig_storage_read_api_cc.so": "libaconfig_storage_read_api_c7.so",
    "android.hardware.graphics.common@1.0.so": "android.hardware.graphics.common@1.7.so",
    "android.hardware.graphics.common@1.1.so": "android.hardware.graphics.common@1.8.so",
    "android.hardware.graphics.allocator@2.0.so": "android.hardware.graphics.allocator@2.7.so",
    "android.hardware.graphics.mapper@2.0.so": "android.hardware.graphics.mapper@2.7.so",
    "android.hardware.graphics.mapper@2.1.so": "android.hardware.graphics.mapper@2.8.so",
}

ENTRYPOINTS = {
    "lib64": [
        "bin/hw/android.hardware.composer.hwc3-service.drm",
        "bin/hw/android.hardware.graphics.allocator-service.minigbm",
        "lib64/hw/mapper.minigbm.so",
    ],
    "lib": [
        "lib/hw/mapper.minigbm.so",
    ],
}


def find_patchelf() -> str:
    top = Path(__file__).resolve().parents[4]
    candidates = [
        top / "prebuilts/extract-tools/linux-x86/bin/patchelf-0_18",
        top / "prebuilts/extract-tools/linux-x86/bin/patchelf",
    ]
    for cand in candidates:
        if cand.is_file():
            return str(cand)
    found = shutil.which("patchelf")
    if found:
        return found
    raise SystemExit("patchelf not found in prebuilts/extract-tools or PATH")


def print_needed(patchelf: str, elf: Path) -> list[str]:
    out = subprocess.check_output(
        [patchelf, "--print-needed", str(elf)], text=True
    )
    return [line.strip() for line in out.splitlines() if line.strip()]


def break_hardlink(path: Path) -> None:
    st = path.stat()
    if st.st_nlink <= 1:
        return
    tmp = path.with_name(path.name + ".tmp_iso")
    shutil.copyfile(path, tmp)
    os.chmod(tmp, st.st_mode & 0o7777)
    os.replace(tmp, path)


def isolate_arch(patchelf: str, raw_vendor: Path, staging_vendor: Path, libdir: str) -> None:
    queue: deque[Path] = deque()
    visited: set[Path] = set()

    for rel in ENTRYPOINTS[libdir]:
        target = staging_vendor / rel
        if not target.is_file():
            raise SystemExit(f"isolate-vndk28: missing expected entrypoint {target}")
        queue.append(target)

    while queue:
        elf = queue.popleft()
        if elf in visited:
            continue
        visited.add(elf)

        needed = print_needed(patchelf, elf)
        replace_args: list[str] = []

        for dep in needed:
            if dep in ISOLATION_MAP:
                iso_name = ISOLATION_MAP[dep]
                iso_dst = staging_vendor / libdir / iso_name
                if not iso_dst.exists():
                    raw_src = raw_vendor / libdir / dep
                    if not raw_src.is_file():
                        raise SystemExit(
                            f"isolate-vndk28: missing source {raw_src} (required by {elf})"
                        )
                    iso_dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(raw_src, iso_dst)
                    os.chmod(iso_dst, 0o644)
                    subprocess.run(
                        [patchelf, "--set-soname", iso_name, str(iso_dst)],
                        check=True,
                    )
                replace_args.extend(["--replace-needed", dep, iso_name])
                if iso_dst not in visited:
                    queue.append(iso_dst)
            else:
                dep_path = staging_vendor / libdir / dep
                if dep_path.is_file() and dep_path not in visited:
                    queue.append(dep_path)

        if replace_args:
            break_hardlink(elf)
            subprocess.run([patchelf, *replace_args, str(elf)], check=True)

    # Verify zero leaked references to raw VNDK-28-conflicting libraries.
    for elf in sorted(visited):
        remaining = [d for d in print_needed(patchelf, elf) if d in ISOLATION_MAP]
        if remaining:
            raise SystemExit(
                f"isolate-vndk28: {elf} still depends on raw VNDK28 libs: {remaining}"
            )

    for raw_name in ISOLATION_MAP:
        leaked = staging_vendor / libdir / raw_name
        if leaked.exists() or leaked.is_symlink():
            raise SystemExit(
                f"isolate-vndk28: conflicting library {leaked} present in staging vendor"
            )


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"Usage: {argv[0]} <raw_vendor_dir> <staging_vendor_dir>", file=sys.stderr)
        return 2

    raw_vendor = Path(argv[1]).resolve()
    staging_vendor = Path(argv[2]).resolve()
    patchelf = find_patchelf()

    for libdir in ("lib64", "lib"):
        isolate_arch(patchelf, raw_vendor, staging_vendor, libdir)

    print("isolate-vndk28: isolated Android 17 graphics HALs and verified ELF graph")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
