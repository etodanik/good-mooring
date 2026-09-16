#!/usr/bin/env python3
"""Build native resources through Forge's FSL compiler and font converter."""
import argparse
from pathlib import Path
import subprocess
import sys
import re
import json
import hashlib
from shader_previews import generate, layout_signature

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--font-tool', type=Path, required=True)
parser.add_argument('--configuration', default='Release')
args = parser.parse_args()
root, output = args.root.resolve(), args.output.resolve()
shader_options = ['--debug'] if args.configuration.lower() == 'debug' else []
source = root / 'Mooring'
fsl = root / 'Common/Tools/ForgeShadingLanguage/fsl.py'
shader_lists = [
    root / 'Common/Application/UI/Shaders/FSL/UIShaders.list',
    root / 'Common/Application/Fonts/Shaders/FSL/FontShader.list',
    root / 'Common/Application/Screenshot/Shaders/FSL/ScreenshotShaders.list',
]
debug_shaders = args.configuration.lower() == 'debug'
shader_lists += generate(root, output) if debug_shaders else sorted((source / 'Shaders').glob('*.list'))
if debug_shaders:
    shader_options += ['--cache-args']
    # Source mirrors replace the original lists. Do not reload stale commands
    # from an older build configuration into the same output directory.
    for cached in (output / 'shaders/intermediate/fsl_cmds').glob('*.json'):
        cached.unlink()
for header in (source / 'Shaders').glob('*.srt.h'):
    if re.search(r'DECL_BUFFER\([^\n]*RWBuffer|DECL_TEXTURE\([^\n]*RWTex', header.read_text()):
        raise SystemExit(f'Read/write resource declared read-only in {header}')
for shader_list in shader_lists:
    command = ['-l', 'MACOS',
                    '-d', str(output / 'shaders'), '-b', str(output / 'shaders/bin'),
                    '-i', str(output / 'shaders/intermediate'), '--compile', '--incremental', '--mp', '1',
                    *shader_options, str(shader_list)]
    subprocess.run([sys.executable, str(fsl), *command], cwd=root, check=True)
    if debug_shaders:
        # FSL returns early for unchanged lists, before its --cache-args hook.
        # Cache every list even on an incremental build.
        cache = output / 'shaders/intermediate/fsl_cmds'
        cache.mkdir(parents=True, exist_ok=True)
        (cache / (hashlib.md5(str(shader_list).encode()).hexdigest() + '.json')).write_text(json.dumps(command))
if debug_shaders:
    (output / 'shaders/intermediate/mooring-reload.json').write_text(json.dumps({
        'output': str(output), 'layout': layout_signature(root)}))
    (output / 'shaders/bin/reload-server.txt').write_text(
        f'127.0.0.1\n6543\n{output / "shaders/intermediate"}\n')

font_dir = output / 'fonts'
font_dir.mkdir(parents=True, exist_ok=True)
font = source / 'Assets/Fonts/Inter-Regular.ttf'
converted = font_dir / 'Inter-Regular.msdf'
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
print(f'Native shader and font resources verified ({args.configuration}, shader debug info: {bool(shader_options)}).')
