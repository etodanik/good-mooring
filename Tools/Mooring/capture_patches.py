#!/usr/bin/env python3
"""Capture isolated port changes against the pinned source archives."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[2]

def difference(name, baseline, current):
    lines = difflib.unified_diff(
        baseline.decode().splitlines(True), current.decode().splitlines(True),
        fromfile='a/' + name if baseline else '/dev/null', tofile='b/' + name)
    return ''.join(line if line.endswith('\n') else line + '\n\\ No newline at end of file\n' for line in lines)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apple-source', type=Path, required=True)
    parser.add_argument('--forge-archive', type=Path, required=True)
    parser.add_argument('--jolt-archive', type=Path, required=True)
    args = parser.parse_args()
    output = ROOT / 'Tools/Mooring/patches'
    output.mkdir(exist_ok=True)
    origin = json.loads((ROOT / 'Tools/Mooring/apple-origin.json').read_text())
    changes = []
    for name, expected_hash in origin['sha256'].items():
        path = args.apple_source / name
        baseline = path.read_bytes()
        if hashlib.sha256(baseline).hexdigest() != expected_hash:
            raise ValueError('Modified reference: ' + str(path))
        if path.suffix in ('.h', '.c', '.cpp', '.mm', '.m') and 'ThirdParty' not in name:
            text = re.sub(r'\b\w+\b', lambda m: origin['identifier_translation'].get(m[0], m[0]), baseline.decode())
            baseline = text.replace('Graphics/GraphicsConfig.h', 'Graphics/Interfaces/IGraphicsConfig.h').encode()
        current = (ROOT / name).read_bytes()
        if current != baseline:
            changes.append(difference(name, baseline, current))
    name = 'Common/Graphics/Metal/MetalDescriptors.h'
    changes.append(difference(name, b'', (ROOT / name).read_bytes()))
    (output / 'apple-compat.patch').write_text(''.join(changes))
    print('Apple compatibility files:', len(changes))
    changes = []
    with tarfile.open(args.forge_archive) as archive:
        for member in archive:
            name = '/'.join(member.name.split('/')[1:])
            path = ROOT / name
            if not member.isfile() or not name.startswith('Common/') or name in origin['sha256'] or not path.is_file():
                continue
            if path.suffix not in ('.h', '.hpp', '.cpp', '.c', '.mm', '.m', '.py'):
                continue
            baseline = archive.extractfile(member).read()
            current = path.read_bytes()
            if current != baseline:
                changes.append(difference(name, baseline, current))
    (output / 'forge-compat.patch').write_text(''.join(changes))
    print('Current Forge compatibility files:', len(changes))
    suffix = 'Jolt/Physics/Collision/BroadPhase/QuadTree.cpp'
    with tarfile.open(args.jolt_archive) as archive:
        member = next(m for m in archive if m.name.endswith('/' + suffix))
        baseline = archive.extractfile(member).read()
    name = 'Common/Game/ThirdParty/OpenSource/Jolt/' + suffix
    (output / 'jolt-bounded-broadphase.patch').write_text(difference(name, baseline, (ROOT / name).read_bytes()))
    print('Jolt compatibility files: 1')

if __name__ == '__main__':
    main()
