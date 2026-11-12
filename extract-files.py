#!/usr/bin/env python3
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Extract matching stock vendor images.")
    parser.add_argument("source", type=Path, help="Directory containing vendor.img and odm.img")
    parser.add_argument("--vendor-tree", type=Path,
                        default=Path(__file__).resolve().parents[3] / "vendor/huawei/marie")
    args = parser.parse_args()
    manifest = args.vendor_tree / "firmware.json"
    if not manifest.is_file():
        parser.error("Check out android_vendor_huawei_marie at vendor/huawei/marie first")
    firmware = json.loads(manifest.read_text())

    # Check every input before updating the vendor checkout.
    for image in firmware["images"]:
        name = image["name"]
        if name not in ("vendor.img", "odm.img"):
            parser.error(f"Unexpected image in firmware.json: {name}")
        source = args.source / name
        if not source.is_file():
            parser.error(f"Missing image: {source}")
        if source.stat().st_size != image["size"] or sha256(source) != image["sha256"]:
            parser.error(f"{name} does not match {firmware['firmware']}")

    destination = args.vendor_tree / "proprietary"
    destination.mkdir(exist_ok=True)
    for image in firmware["images"]:
        target = destination / image["name"]
        if target.is_file() and sha256(target) == image["sha256"]:
            print(f"{target.name}: already extracted")
            continue
        with tempfile.NamedTemporaryFile(dir=destination, delete=False) as stream:
            temporary = Path(stream.name)
        try:
            shutil.copyfile(args.source / image["name"], temporary)
            if sha256(temporary) != image["sha256"]:
                parser.error(f"Copy verification failed for {image['name']}")
            temporary.replace(target)
        finally:
            temporary.unlink(missing_ok=True)
        print(f"{target.name}: extracted")


if __name__ == "__main__":
    main()
