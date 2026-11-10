#!/usr/bin/env python3
"""Prepare the pinned core and this USB overlay in a new directory."""
from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--destination', type=Path, default=root / 'build/Atmosphere')
parser.add_argument('--source-repository', help='Optional local clone of the pinned base')
args = parser.parse_args()
base = json.loads((root / 'base.json').read_text())
commit = base['commit']
if not re.fullmatch(r'[0-9a-f]{40}', commit):
    parser.error('base.json must pin a full commit ID')
destination = args.destination.resolve()
if destination.exists():
    parser.error(f'Destination already exists: {destination}')
repository = args.source_repository or base['repository']
subprocess.run(['git', 'clone', '-c', 'core.autocrlf=false', '--no-checkout', '--', repository, str(destination)], check=True)
subprocess.run(['git', '-C', str(destination), 'checkout', '--detach', commit], check=True)
actual = subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'HEAD'], text=True).strip()
if actual != commit:
    raise SystemExit('The checked-out base does not match base.json')
count = 0
for path in sorted((root / 'overlay').rglob('*')):
    if path.is_symlink():
        raise SystemExit(f'Unexpected symlink in overlay: {path}')
    if path.is_file():
        relative = path.relative_to(root / 'overlay')
        target = destination / relative
        if not target.resolve().is_relative_to(destination):
            raise SystemExit(f'Overlay path escapes destination: {relative}')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        count += 1
print(f'Prepared {destination} at {commit} with {count} overlay files.')
