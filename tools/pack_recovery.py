#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pack the recovery runtime into one gzip boot ramdisk."""
import argparse
from dataclasses import dataclass
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import stat
import struct
import subprocess
import tarfile
import tempfile

STOCK_SECURITY_PATCH = '2020-07-01'

@dataclass
class Entry:
    record: bytes
    data: bytes
    mode: int
    links: int


def read_cpio(data):
    entries = {}
    position = 0
    while position + 110 <= len(data):
        start = position
        header = data[position:position + 110]
        if header[:6] not in (b'070701', b'070702'):
            raise ValueError('Invalid newc archive')
        fields = [int(header[i:i + 8], 16) for i in range(6, 110, 8)]
        size, name_size = fields[6], fields[11]
        name_end = position + 110 + name_size
        if name_size < 1 or name_end > len(data) or data[name_end - 1] != 0:
            raise ValueError('Invalid archive path')
        name = data[position + 110:name_end - 1].decode().removeprefix('./')
        if PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts or name in entries:
            raise ValueError('Unsafe or repeated archive path: ' + name)
        position = (name_end + 3) & ~3
        if position + size > len(data):
            raise ValueError('Truncated archive entry: ' + name)
        payload = data[position:position + size]
        position = (position + size + 3) & ~3
        entries[name] = Entry(data[start:position], payload, fields[1], fields[4])
        if name == 'TRAILER!!!':
            return entries
    raise ValueError('Missing archive trailer')


def set_security_patch(entries):
    # Keymaster binds existing key blobs to the installed firmware version.
    # Keep the recovery framework version separate from that firmware identity.
    entry = entries['prop.default']
    key = 'ro.build.version.security_patch='
    lines = entry.data.decode().splitlines()
    if sum(line.startswith(key) for line in lines) != 1:
        raise ValueError('Expected one recovery security patch property')
    data = ('\n'.join(key + STOCK_SECURITY_PATCH if line.startswith(key) else line
                      for line in lines) + '\n').encode()
    name_size = int(entry.record[94:102], 16)
    prefix_size = (110 + name_size + 3) & ~3
    prefix = bytearray(entry.record[:prefix_size])
    prefix[54:62] = ('%08x' % len(data)).encode()
    if prefix[:6] == b'070702':
        prefix[102:110] = ('%08x' % (sum(data) & 0xffffffff)).encode()
    record = bytes(prefix) + data
    record += b'\0' * ((-len(record)) % 4)
    entries['prop.default'] = Entry(record, data, entry.mode, entry.links)



def new_entry(name, data, mode=0o100644, uid=0, gid=0):
    encoded = name.encode() + b'\0'
    values = [0, mode, uid, gid, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
    record = b'070701' + b''.join(('%08x' % value).encode() for value in values) + encoded
    record += b'\0' * ((-len(record)) % 4)
    record += data
    record += b'\0' * ((-len(record)) % 4)
    return record


def dependencies(data):
    assert data[:6] == b'\x7fELF\x02\x01', 'Expected little-endian ELF64'
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', data, 54)
    headers = [struct.unpack_from('<IIQQQQQQ', data, phoff + i*phsize)
               for i in range(phnum)]
    interpreter = None
    for header in headers:
        if header[0] == 3:
            interpreter = data[header[2]:header[2]+header[5]].rstrip(b'\0').decode()
    dynamic = next((h for h in headers if h[0] == 2), None)
    if dynamic is None:
        return interpreter, []
    entries = []
    for offset in range(dynamic[2], dynamic[2]+dynamic[5], 16):
        tag, value = struct.unpack_from('<qQ', data, offset)
        if not tag:
            break
        entries.append((tag, value))
    needed = [value for tag, value in entries if tag == 1]
    if not needed:
        return interpreter, []
    address = next(value for tag, value in entries if tag == 5)
    segment = next(h for h in headers if h[0] == 1 and h[3] <= address < h[3]+h[5])
    strings = segment[2] + address-segment[3]
    return interpreter, [data[strings+n:data.index(b'\0', strings+n)].decode() for n in needed]



def pack(cpio, bootstrap, output):
    entries = read_cpio(cpio)
    set_security_patch(entries)
    if dependencies(entries['system/bin/7za'].data) != (None, []):
        raise ValueError('Runtime decompressor must be static')
    if not stat.S_ISLNK(entries['init'].mode) or entries['init'].data != b'/system/bin/init':
        raise ValueError('Unexpected recovery init entry')
    if 'system/bin/recovery.bin' in entries:
        raise ValueError('Recovery input is already packed')
    recovery = entries['system/bin/recovery']
    if not recovery.data.startswith(b'\x7fELF') or recovery.links != 1:
        raise ValueError('Expected an unlinked recovery ELF')
    binary = 'system/bin/recovery.bin'
    record = new_entry(binary, recovery.data, recovery.mode,
                       int(recovery.record[22:30], 16), int(recovery.record[30:38], 16))
    entries[binary] = Entry(record, recovery.data, recovery.mode, 1)
    # First-stage init checks this path before any init.rc action can run.
    launcher = b'#!/system/bin/sh\nexec -a /system/bin/recovery /system/bin/recovery.bin "$@"\n'
    entries['system/bin/recovery'] = Entry(
        new_entry('system/bin/recovery', launcher, 0o100755), launcher, 0o100755, 1)
    init_rc = 'system/etc/init/hw/init.rc'
    action = b'on early-init\n    exec_start marie-runtime-unpack\n\n'
    if b'marie-runtime-unpack' in entries[init_rc].data:
        raise ValueError('Runtime action already present in init.rc')
    if b'service marie-runtime-unpack ' not in entries['init.recovery.marie.runtime.rc'].data:
        raise ValueError('Missing runtime service definition')
    data = action + entries[init_rc].data
    entries[init_rc] = Entry(new_entry(init_rc, data), data, 0o100644, 1)
    for name in bootstrap:
        if name not in entries:
            raise ValueError('Missing bootstrap file: ' + name)
    for name in bootstrap:
        data = entries[name].data
        if not data.startswith(b'\x7fELF'):
            raise ValueError('Bootstrap file is not ELF: ' + name)
        interpreter, libraries = dependencies(data)
        if interpreter and interpreter.lstrip('/') not in bootstrap:
            raise ValueError('Bootstrap linker was packed: ' + name)
        for library in libraries:
            provider = next((directory + '/' + library for directory in
                ('system/lib64', 'vendor/lib64', 'vendor/lib64/hw')
                if directory + '/' + library in entries), None)
            if provider not in bootstrap:
                raise ValueError('Bootstrap dependency would be packed: ' + name + ': ' + library)
    packed = sorted(name for name, entry in entries.items()
        if stat.S_ISREG(entry.mode) and name not in bootstrap and
        (name.startswith(('system/lib64/', 'crypto/', 'twres/fonts/')) or
         name in {'system/bin/recovery.bin', 'system/bin/keystore2'}))
    if 'system/bin/recovery.bin' not in packed or 'system/bin/keystore2' not in packed:
        raise ValueError('Full recovery and crypto build input is required')
    if any(entries[name].links != 1 for name in packed):
        raise ValueError('Packed runtime contains hard-linked files')
    with tempfile.TemporaryDirectory(prefix='marie-runtime-', dir=output.parent) as temporary:
        work = Path(temporary)
        archive = work/'runtime.tar.xz'
        with tarfile.open(archive, 'w:xz', format=tarfile.USTAR_FORMAT, preset=9) as tar:
            for name in packed:
                entry = entries[name]
                info = tarfile.TarInfo(name)
                info.size = len(entry.data)
                info.mode = entry.mode & 0o777
                info.uid = int(entry.record[22:30], 16)
                info.gid = int(entry.record[30:38], 16)
                info.mtime = 0
                tar.addfile(info, io.BytesIO(entry.data))
        # Verify the archive used on the phone, including executable permissions.
        with tarfile.open(archive, 'r:xz') as tar:
            members = tar.getmembers()
            if [entry.name for entry in members] != packed:
                raise ValueError('Runtime archive file list changed')
            for member in members:
                expected = entries[member.name]
                if (not member.isfile() or tar.extractfile(member).read() != expected.data or
                        member.mode != expected.mode & 0o777):
                    raise ValueError('Runtime archive changed: ' + member.name)
        hashes = ''.join(hashlib.sha256(entries[name].data).hexdigest() + '  ' + name + '\n'
                         for name in packed).encode()
        packed_set = set(packed)
        outer = b''.join(entry.record for name, entry in entries.items()
                         if name not in packed_set and name != 'TRAILER!!!')
        outer += new_entry('runtime.tar.xz', archive.read_bytes())
        outer += new_entry('runtime.sha256', hashes)
        outer += entries['TRAILER!!!'].record
        parsed = read_cpio(outer)
        for name, entry in entries.items():
            if name not in packed_set and parsed[name].data != entry.data:
                raise ValueError('Outer archive changed: ' + name)
        uncompressed = work/'outer.cpio'
        uncompressed.write_bytes(outer)
        compressed = work/'outer.gz'
        with compressed.open('wb') as stream:
            subprocess.run(['/usr/bin/zopfli', '--gzip', '--i3', '-c', str(uncompressed)],
                           stdout=stream, check=True)
        result = compressed.read_bytes()
        if gzip.decompress(result) != outer:
            raise ValueError('Outer compression changed the archive')
        if len(result) + 65536 >= 33554432:
            raise ValueError('Recovery exceeds its 32 MiB partition budget')
        output.write_bytes(result)
        report = dict(packed_files=packed, bootstrap_files=sorted(bootstrap),
                      launcher_sha256=hashlib.sha256(launcher).hexdigest(),
                      input_cpio_sha256=hashlib.sha256(cpio).hexdigest(),
                      runtime_xz_size=archive.stat().st_size, ramdisk_size=len(result))
        output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Single recovery ramdisk verified: {} bytes'.format(len(result)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpio', type=Path, required=True)
    parser.add_argument('--bootstrap', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    bootstrap = {line.strip() for line in args.bootstrap.read_text().splitlines()
                 if line.strip() and not line.startswith('#')}
    pack(args.cpio.read_bytes(), bootstrap, args.output)


if __name__ == '__main__':
    main()
