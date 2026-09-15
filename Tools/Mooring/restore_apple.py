#!/usr/bin/env python3
"""Restore the public Apple sources, then translate the current Forge names.

Run against an extracted cd5046893faba2dc7869243873bf01f02a6f0df9 archive.
Subsequent compatibility changes are maintained as patches in this directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
FILES = [
    'Common_3/Graphics/Metal',
    'Common_3/OS/Darwin',
    'Common_3/Graphics/FSL/metal_srt.h',
    'Common_3/Graphics/ThirdParty/OpenSource/VulkanMemoryAllocator',
    'Common_3/Tools/ForgeShadingLanguage/generators/metal.py',
    'Common_3/Tools/ForgeShadingLanguage/includes/metal.h',
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('reference', type=Path)
    args = parser.parse_args()
    names = set()
    for directory in ['Graphics/Interfaces', 'OS/Interfaces', 'Application/Interfaces', 'Utilities/Interfaces']:
        for path in (ROOT / 'Common_3' / directory).glob('*.h'):
            names.update(re.findall(r'\bTF(?:_[A-Z]\w*|[A-Z]\w*)\b', path.read_text()))
    rename = {}
    for name in sorted(names):
        rename[name[3:] if name.startswith('TF_') else name[2:]] = name
    rename.update({'GraphicsConfig.h':'Interfaces/IGraphicsConfig.h'})
    manifest = {}
    for name in FILES:
        source = args.reference / name
        paths = source.rglob('*') if source.is_dir() else [source]
        for path in paths:
            if not path.is_file():
                continue
            relative = path.relative_to(args.reference)
            data = path.read_bytes()
            manifest[str(relative)] = hashlib.sha256(data).hexdigest()
            if path.suffix in ('.h', '.c', '.cpp', '.mm', '.m') and 'ThirdParty' not in str(relative):
                text = data.decode()
                text = re.sub(r'\b\w+\b', lambda m: rename.get(m[0], m[0]), text)
                text = text.replace('Graphics/GraphicsConfig.h', 'Graphics/Interfaces/IGraphicsConfig.h')
                data = text.encode()
            destination = ROOT / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    (ROOT / 'Tools/Mooring/apple-origin.json').write_text(json.dumps({
        'revision':'cd5046893faba2dc7869243873bf01f02a6f0df9',
        'upstream':'https://github.com/ConfettiFX/The-Forge',
        'sha256':manifest,
        'identifier_translation':rename,
    }, indent=2) + '\n')
    print(f'Restored {len(manifest)} Apple source files; translated {len(rename)} API names.')

if __name__ == '__main__':
    main()
