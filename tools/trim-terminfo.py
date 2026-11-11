#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep common terminal definitions within the recovery partition budget."""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
database = root / 'system/etc/terminfo'
assert database.resolve().is_relative_to(root)
if not database.is_dir():
    raise SystemExit(0)
entries = [path for path in database.rglob('*') if path.is_file() or path.is_symlink()]
prefixes = ('xterm', 'screen', 'tmux', 'linux', 'vt100', 'vt102', 'vt220', 'ansi', 'rxvt')
keep = {path for path in entries if path.name == 'dumb' or path.name.startswith(prefixes)}
for path in tuple(keep):
    target = path.resolve()
    assert target.is_relative_to(database.resolve())
    keep.add(target)
removed = 0
for path in entries:
    if path not in keep:
        path.unlink()
        removed += 1
for directory in sorted(database.rglob('*'), reverse=True):
    if directory.is_dir() and not any(directory.iterdir()):
        directory.rmdir()
assert (database/'x/xterm-256color').is_file()
assert (database/'l/linux').is_file()
print(f'Terminfo: retained {len(keep)} common definitions, removed {removed} unused definitions')
