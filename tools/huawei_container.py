#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Repack AERA's CPIO into the matching Huawei stock boot-image container.

This preserves the original OEM AVB metadata.
It does not produce a new OEM signature or authenticate the modified payload;
the original hash descriptor remains stock. An unlocked bootloader is required.
"""
import argparse
import gzip
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

STOCK_SHA256 = 'bf0569021ab89199f0b20c880fa4998b687ad26553cf98ef07e403a819345e27'
PARTITION_SIZE = 33554432


def compressed_ramdisk(data):
    assert data[:8] == b'ANDROID!'
    kernel_size = struct.unpack_from('<I', data, 8)[0]
    size = struct.unpack_from('<I', data, 16)[0]
    page = struct.unpack_from('<I', data, 36)[0]
    assert kernel_size == 0 and page == 2048
    return data[page:page+size]


def ramdisk(data):
    return gzip.decompress(compressed_ramdisk(data))


def vbmeta(data):
    assert data[-64:-60] == b'AVBf', 'Missing Huawei-compatible AVB footer'
    _, major, minor, original_size, offset, size, _ = struct.unpack('>4sIIQQQ28s', data[-64:])
    assert major == 1 and offset + size <= len(data)-64
    block = data[offset:offset+size]
    assert block[:4] == b'AVB0'
    return block


def wrap(image, donor, executable):
    image, donor, executable = image.resolve(), donor.resolve(), executable.resolve()
    original = image.read_bytes()
    stock = donor.read_bytes()
    assert hashlib.sha256(stock).hexdigest() == STOCK_SHA256, 'Wrong stock donor'
    assert len(stock) == PARTITION_SIZE
    expected_cpio = ramdisk(original)
    expected_compressed = compressed_ramdisk(original)
    executable.chmod(executable.stat().st_mode | 0o111)
    with tempfile.TemporaryDirectory(prefix='marie-container-', dir=image.parent) as temporary:
        work = Path(temporary)
        # Retain the build's compressed payload; recompressing here would lose
        # Zopfli's size savings before adding the Huawei container metadata.
        subprocess.run([str(executable), 'unpack', '-n', str(image)], cwd=work, check=True)
        result = work / 'huawei-recovery.img'
        environment = dict(os.environ, PATCHVBMETAFLAG='false')
        subprocess.run([str(executable), 'repack', '-n', str(donor), str(result)],
                       cwd=work, env=environment, check=True)
        output = result.read_bytes()
        assert len(output) == PARTITION_SIZE
        assert ramdisk(output) == expected_cpio, 'AERA CPIO changed during container repack'
        assert compressed_ramdisk(output) == expected_compressed, 'Compressed ramdisk changed during repack'
        assert vbmeta(output) == vbmeta(stock), 'OEM metadata was not preserved byte for byte'
        assert output[:8] == b'ANDROID!'
        result.replace(image)
    print('HUAWEI_CONTAINER_REPACK_VERIFIED: unchanged AERA CPIO; OEM AVB metadata preserved')
    print('SHA256=' + hashlib.sha256(image.read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('stock_donor', type=Path)
    parser.add_argument('magiskboot', type=Path)
    args = parser.parse_args()
    wrap(args.image, args.stock_donor, args.magiskboot)
