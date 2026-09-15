#!/usr/bin/env python3
"""Build native resources through Forge's FSL compiler and font converter."""
import argparse
from pathlib import Path
import subprocess
import sys
import re

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--font-tool', type=Path, required=True)
args = parser.parse_args()
root, output = args.root.resolve(), args.output.resolve()
source = root / 'Mooring'
fsl = root / 'Common/Tools/ForgeShadingLanguage/fsl.py'
shader_lists = [
    root / 'Common/Application/UI/Shaders/FSL/UIShaders.list',
    root / 'Common/Application/Fonts/Shaders/FSL/FontShader.list',
    root / 'Common/Application/Screenshot/Shaders/FSL/ScreenshotShaders.list',
]
shader_lists += sorted((source / 'Shaders').glob('*.list'))
for header in (source / 'Shaders').glob('*.srt.h'):
    if re.search(r'DECL_BUFFER\([^\n]*RWBuffer|DECL_TEXTURE\([^\n]*RWTex', header.read_text()):
        raise SystemExit(f'Read/write resource declared read-only in {header}')
for shader_list in shader_lists:
    subprocess.run([sys.executable, str(fsl), '-l', 'MACOS',
                    '-d', str(output / 'shaders'), '-b', str(output / 'shaders/bin'),
                    '-i', str(output / 'shaders/intermediate'), '--compile', '--incremental', '--mp', '1',
                    str(shader_list)], cwd=root, check=True)

font_dir = output / 'fonts'
font_dir.mkdir(parents=True, exist_ok=True)
font = source / 'Assets/Fonts/AtkinsonHyperlegible-Regular.ttf'
converted = font_dir / 'AtkinsonHyperlegible-Regular.msdf'
if not converted.exists() or converted.stat().st_mtime < max(font.stat().st_mtime, args.font_tool.stat().st_mtime):
    subprocess.run([str(args.font_tool.resolve()), str(font.parent) + '/', str(font_dir) + '/'],
                   cwd=output, check=True)
if not converted.exists() or converted.stat().st_size < 1024:
    raise SystemExit('Font conversion did not produce a valid resource.')

for shader_list in shader_lists:
    for line in shader_list.read_text().splitlines():
        fields = line.split()
        if not fields or fields[0] not in ('#vert', '#frag', '#comp'):
            continue
        binary = output / 'shaders/bin/MACOS' / (fields[-1] + '.metal')
        if not binary.exists() or binary.read_bytes()[:4] != b'@FSL':
            raise SystemExit(f'Missing or invalid FSL binary: {binary}')
print('Native shader and font resources verified.')
