#!/usr/bin/env python3
"""Generate Debug-only FSL preview entry points and their runtime registry."""
from pathlib import Path
import hashlib
import json
import re
import zlib

REGISTRATION = re.compile(r'SHADER_PREVIEW_(1D|2D|3D_SLICE)\s*\(\s*(\w+)\s*,\s*"([^"\n]+)"\s*,\s*PREVIEW_(CONTINUOUS|INTEGER_GRID)\s*\)\s*;?')
PROBE = re.compile(r'\bSHADER_PREVIEW\s*\(\s*"([^"\n]+)"\s*,')
INCLUDE = re.compile(r'^(\s*#\s*include\s*)"([^"\n]+)"', re.MULTILINE)


def write_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def without_comments(text):
    # Keep strings and line numbers intact while ignoring commented registrations.
    return re.sub(r'"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\n]*',
                  lambda match: match[0] if match[0].startswith('"') else re.sub(r'[^\n]', ' ', match[0]), text)


def layout_signature(root):
    digest = hashlib.sha256()
    for path in sorted((root / 'Mooring/Shaders').glob('*.srt.h')):
        digest.update(path.name.encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


def generate(root, output):
    root, output = Path(root).resolve(), Path(output).resolve()
    sources = root / 'Mooring/Shaders'
    mirror = output / 'shader-lab/sources'
    entries, probes, labels, symbols, ids = [], [], set(), set(), set()
    paths = sorted(path for path in sources.rglob('*') if path.suffix in ('.fsl', '.h', '.list'))
    for path in paths:
        original = path.read_text()
        clean = without_comments(original)
        if path.name.endswith('.preview.fsl'):
            markers = list(re.finditer(r'\bSHADER_PREVIEW_(?:1D|2D|3D_SLICE)\s*\(', clean))
            registrations = list(REGISTRATION.finditer(clean))
            if len(markers) != len(registrations):
                raise ValueError(f'{path}: use SHADER_PREVIEW_2D(function, "label", PREVIEW_CONTINUOUS); or its 1D/3D_SLICE form')
        for match in REGISTRATION.finditer(clean):
            kind, function, label, sampling = match.groups()
            if not path.name.endswith('.preview.fsl'):
                raise ValueError(f'{path}: function registrations belong in a .preview.fsl file')
            if function in symbols or label in labels:
                raise ValueError(f'{path}: duplicate preview function or label: {function}, {label}')
            symbols.add(function)
            labels.add(label)
            entries.append(dict(function=function, label=label, kind=kind, integer=sampling == 'INTEGER_GRID',
                                source=str(path), line=clean[:match.start()].count('\n') + 1,
                                binary='shader_preview_' + function + '.frag'))
        for match in PROBE.finditer(clean):
            label = match[1]
            if path.name != 'Water.frag.fsl':
                raise ValueError(f'{path}: scene probes currently belong in Water.frag.fsl PS_MAIN, after the helper call')
            main = re.search(r'\bPS_MAIN\s*\(', clean)
            if not main or match.start() < main.start():
                raise ValueError(f'{path}: place SHADER_PREVIEW inside PS_MAIN, after the helper call')
            identity = 1000 + zlib.crc32(label.encode()) % 7000000
            if identity in ids or label in labels:
                raise ValueError(f'{path}: duplicate scene probe label or ID collision: {label}')
            ids.add(identity)
            labels.add(label)
            probes.append(dict(label=label, id=identity, source=str(path), line=clean[:match.start()].count('\n') + 1))
        # Substitute only active markers; matching the original offsets also avoids comments.
        rewritten = original
        for match in reversed(list(PROBE.finditer(clean))):
            identity = 1000 + zlib.crc32(match[1].encode()) % 7000000
            rewritten = rewritten[:match.start()] + f'SHADER_PREVIEW_VALUE({identity},' + rewritten[match.end():]

        def resolve_include(match):
            included = (path.parent / match[2]).resolve()
            if included.is_relative_to(sources):
                included = mirror / included.relative_to(sources)
            return match[1] + '"' + included.as_posix() + '"'

        rewritten = INCLUDE.sub(resolve_include, rewritten)
        prefix = '#define MOORING_SHADER_LAB 1\n' if path.suffix == '.list' else ''
        write_changed(mirror / path.relative_to(sources), prefix + f'#line 1 "{path.as_posix()}"\n' + rewritten)

    expected = {mirror / path.relative_to(sources) for path in paths}
    for stale in mirror.rglob('*'):
        if stale.is_file() and stale not in expected:
            stale.unlink()

    defaults = root / 'Common/Graphics/FSL/defaults.h'
    shader_list = [f'#include "{defaults}"', '#define MOORING_SHADER_LAB 1', '#vert shader_lab.vert',
                   f'#include "{mirror / "ShaderLab.vert.fsl"}"', '#end', '#frag shader_lab_resource.frag',
                   f'#include "{mirror / "ShaderLabResource.frag.fsl"}"', '#end']
    for entry in entries:
        kind = entry['kind']
        context = {'1D': 'ShaderPreview1D', '2D': 'ShaderPreview2D', '3D_SLICE': 'ShaderPreview3D'}[kind]
        coordinate = {'1D': 'coordinate.x', '2D': 'coordinate.xy', '3D_SLICE': 'coordinate'}[kind]
        wrapper = f'''#include "{mirror / 'Water.srt.h'}"
#include "{mirror / 'ShaderPreview.h.fsl'}"
#include "{mirror / Path(entry['source']).relative_to(sources)}"
float4 PS_MAIN(ShaderLabFragment input) : SV_Target0
{{
    INIT_MAIN;
    float3 coordinate = shaderPreviewCoordinate(input.uv);
    {context} preview;
    preview.coordinate = {coordinate};
    preview.time = gShaderLab.origin.w;
    preview.parameters = gShaderLab.parameters;
    float4 value = {entry['function']}(preview);
    shaderPreviewStore(input.position.xy, value);
    RETURN(float4(shaderPreviewDisplay(value), 1));
}}
'''
        generated = output / 'shader-lab' / (entry['binary'] + '.fsl')
        write_changed(generated, wrapper)
        shader_list.extend(['#frag ' + entry['binary'], f'#include "{generated}"', '#end'])
    generated_list = output / 'shader-lab/ShaderLab.list'
    write_changed(generated_list, '\n'.join(shader_list) + '\n')
    active_binaries = {entry['binary'] for entry in entries}
    for stale in (output / 'shader-lab').glob('shader_preview_*.frag.fsl'):
        if stale.name.removesuffix('.fsl') not in active_binaries:
            stale.unlink()
    for directory in (output / 'shaders/MACOS', output / 'shaders/bin/MACOS'):
        for stale in directory.glob('shader_preview_*.frag.metal'):
            if stale.name.removesuffix('.metal') not in active_binaries:
                stale.unlink()
    manifest = dict(version=1, previews=entries, probes=probes)
    write_changed(output / 'shaders/bin/shader-lab.json', json.dumps(manifest, indent=2) + '\n')
    return [mirror / path.name for path in sources.glob('*.list')] + [generated_list]
