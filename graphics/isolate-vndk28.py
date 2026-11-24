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
(never mutating out/target/product/marie/vendor).  Every isolated SONAME has
the exact same byte length as the original, so we patch DT_NEEDED and DT_SONAME
entries strictly in-place inside the ELF .dynstr string table — keeping LLD's
original program headers, alignments, and PT_INTERP/PT_GNU_RELRO 100% intact.
"""

from collections import deque
from pathlib import Path
import os
import shutil
import struct
import sys

# Exact same-length SONAME renames for zero-relocation in-place .dynstr patching.
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
    "libcrypto.so": "libcrypt7.so",
    "libssl.so": "libs7l.so",
    "libxml2.so": "libxm72.so",
    "libnl.so": "libn7.so",
    "libz.so": "lib7.so",
    "libbinder.so": "libbinde7.so",
}

for _k, _v in ISOLATION_MAP.items():
    assert len(_k) == len(_v), f"Length mismatch: {_k} vs {_v}"

ENTRYPOINTS = {
    "lib64": [
        "bin/hw/android.hardware.composer.hwc3-service.drm",
        "bin/hw/android.hardware.graphics.allocator-service.minigbm",
        "lib64/hw/mapper.minigbm.so",
        "bin/hw/android.hardware.wifi-service",
        "bin/hw/wpa_supplicant",
        "bin/hw/hostapd",
    ],
    "lib": [
        "lib/hw/mapper.minigbm.so",
    ],
}

SHT_DYNAMIC = 6
DT_NULL = 0
DT_NEEDED = 1
DT_SONAME = 14


def parse_elf_dynamic(data: bytes):
    """Return (dynstr_offset, dynstr_size, entries) where entries are (tag, str_offset)."""
    if data[:4] != b"\x7fELF":
        raise ValueError("Not an ELF file")
    ei_class = data[4]
    ei_data = data[5]
    if ei_data != 1:
        raise ValueError("Only little-endian ELF supported")

    if ei_class == 2:  # ELF64
        e_shoff = struct.unpack_from("<Q", data, 40)[0]
        e_shentsize, e_shnum = struct.unpack_from("<HH", data, 58)
        sh_fmt = "<IIQQQQIIQQ"
        dyn_fmt, dyn_sz = "<qQ", 16
    elif ei_class == 1:  # ELF32
        e_shoff = struct.unpack_from("<I", data, 32)[0]
        e_shentsize, e_shnum = struct.unpack_from("<HH", data, 46)
        sh_fmt = "<IIIIIIIIII"
        dyn_fmt, dyn_sz = "<iI", 8
    else:
        raise ValueError(f"Unsupported ELF class {ei_class}")

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sections.append(struct.unpack_from(sh_fmt, data, off))

    dyn_sec = None
    for s in sections:
        if s[1] == SHT_DYNAMIC:
            dyn_sec = s
            break
    if dyn_sec is None:
        return 0, 0, []

    dyn_off, dyn_size, dyn_link = dyn_sec[4], dyn_sec[5], dyn_sec[6]
    str_sec = sections[dyn_link]
    str_off, str_size = str_sec[4], str_sec[5]

    entries = []
    for pos in range(dyn_off, dyn_off + dyn_size, dyn_sz):
        tag, val = struct.unpack_from(dyn_fmt, data, pos)
        if tag == DT_NULL:
            break
        if tag in (DT_NEEDED, DT_SONAME):
            entries.append((tag, val))
    return str_off, str_size, entries


def read_cstr(data: bytes, start: int) -> str:
    end = data.index(b"\x00", start)
    return data[start:end].decode("ascii")


def get_needed(elf: Path) -> list[str]:
    data = elf.read_bytes()
    str_off, _, entries = parse_elf_dynamic(data)
    return [read_cstr(data, str_off + val) for tag, val in entries if tag == DT_NEEDED]


def patch_elf_inplace(elf: Path, rename_soname: str | None = None) -> bool:
    """Patch DT_NEEDED (and optional DT_SONAME) strictly in-place inside .dynstr."""
    raw = elf.read_bytes()
    str_off, _, entries = parse_elf_dynamic(raw)
    buf = bytearray(raw)
    modified = False

    for tag, val in entries:
        pos = str_off + val
        cur = read_cstr(raw, pos)
        new = None
        if tag == DT_NEEDED and cur in ISOLATION_MAP:
            new = ISOLATION_MAP[cur]
        elif tag == DT_SONAME and rename_soname is not None and cur != rename_soname:
            if len(cur) != len(rename_soname):
                raise ValueError(f"SONAME length mismatch in {elf}: {cur} -> {rename_soname}")
            new = rename_soname
        if new is not None:
            nb = new.encode("ascii")
            assert len(nb) == len(cur)
            buf[pos : pos + len(nb)] = nb
            modified = True

    if modified:
        st = elf.stat()
        tmp = elf.with_name(elf.name + ".tmp_iso")
        tmp.write_bytes(buf)
        os.chmod(tmp, st.st_mode & 0o7777)
        os.replace(tmp, elf)
    return modified


def isolate_arch(raw_vendor: Path, staging_vendor: Path, libdir: str) -> None:
    queue: deque[tuple[Path, str | None]] = deque()
    visited: set[Path] = set()

    for rel in ENTRYPOINTS[libdir]:
        target = staging_vendor / rel
        if not target.is_file():
            raise SystemExit(f"isolate-vndk28: missing expected entrypoint {target}")
        queue.append((target, None))

    while queue:
        elf, expected_soname = queue.popleft()
        if elf in visited:
            continue
        visited.add(elf)

        needed = get_needed(elf)
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
                if iso_dst not in visited:
                    queue.append((iso_dst, iso_name))
            else:
                dep_path = staging_vendor / libdir / dep
                if dep_path.is_file() and dep_path not in visited:
                    queue.append((dep_path, None))

        patch_elf_inplace(elf, rename_soname=expected_soname)

    # Verify zero leaked references to raw VNDK-28-conflicting libraries.
    for elf in sorted(visited):
        remaining = [d for d in get_needed(elf) if d in ISOLATION_MAP]
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

    for libdir in ("lib64", "lib"):
        isolate_arch(raw_vendor, staging_vendor, libdir)

    if not (staging_vendor / "etc/vintf/manifest.xml").is_file():
        raise SystemExit("isolate-vndk28: missing etc/vintf/manifest.xml in staging vendor")
    for forbidden in ("bin/boringssl_self_test64", "bin/boringssl_self_test32"):
        if (staging_vendor / forbidden).exists():
            raise SystemExit(f"isolate-vndk28: forbidden {forbidden} present (OVERRIDES failed?)")

    print("isolate-vndk28: isolated Android 17 graphics HALs in-place and verified ELF graph")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
