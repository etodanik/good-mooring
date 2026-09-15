#!/usr/bin/env python3
"""Check the fixed Breeze camera-motion case for render-pass regressions."""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageStat

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
parser.add_argument('--preset', choices=('balanced', 'ultra'), default='ultra')
args = parser.parse_args()
prefix = f'MooringSimulator_{args.preset}-motion-'
sheet = Image.new('RGB', (1920, 1050))
previous = None
maximum_jump = 0.0
for frame in range(30):
    path = args.directory / f'{prefix}{frame:02}.png'
    with Image.open(path) as image:
        image = image.convert('RGB')
        width, height = image.size
        # Open water, outside the vessel: a seabed-coloured tile or a sudden
        # full-surface foam mask caused large jumps here in the broken pass.
        region = image.crop((0, height * 2 // 3, width // 3, height))
        mean = ImageStat.Stat(region).mean
        if previous is not None:
            maximum_jump = max(maximum_jump, max(abs(a-b) for a, b in zip(mean, previous)))
        previous = mean
        image.thumbnail((320, 200))
        sheet.paste(image, ((frame % 6) * 320, (frame // 6) * 210))
        ImageDraw.Draw(sheet).text(((frame % 6) * 320, (frame // 6) * 210 + 198), str(frame), fill='white')
sheet.save(args.directory / f'{args.preset}-motion-review.png')
print(f'Maximum adjacent-frame channel-mean jump: {maximum_jump:.2f} / 255')
if maximum_jump > 8:
    raise SystemExit('FAIL: inspect the motion contact sheet for a render-pass regression.')
print('PASS: no large motion-frame discontinuity. Visual review is still required.')

# One representative capture for every effect. The sheets assist inspection;
# their creation is not an assertion that an effect meets the visual target.
cases = [
    ('whitecaps', 'Beauty'), ('rough', 'LOD'), ('wake', 'Combined foam'), ('rough', 'Normals'),
    ('whitecaps', 'FFT foam'), ('wake', 'Local foam'), ('dock', 'Contact foam'), ('dock', 'Shadows'),
    ('calm', 'Reflection'), ('dock', 'Depth'), ('whitecaps', 'Wave volume scattering'), ('calm', 'Refraction'),
    ('calm', 'Sun glints'), ('dock', 'Underwater rays'), ('light', 'Cloud shadows'), ('wake', 'Local displacement'),
    ('whitecaps', 'Compression'), ('rough', 'Ripples'), ('whitecaps', 'Spray (actual size)'), ('storm', 'Rain'),
    ('light', 'Atmospheric rays'), ('light', 'Fog'), ('whitecaps', 'FFT displacement'), ('wake', 'Local normals (8x)'),
    ('light', 'Sky'), ('light', 'Cloud opacity'), ('storm', 'Displaced mesh lighting'), ('rough', 'Reflection coverage'),
    ('whitecaps', 'Foam relief normals'),
    ('wake', 'Foam flow / density'),
    ('light', 'Cloud noise slice'), ('light', 'Cloud density section'),
    ('whitecaps', 'Water light path (white = 12 m)'),
    ('wake', 'Under-foam scattering'),
    ('storm', 'Active breaking'),
    ('wake', 'Transported foam age'),
    ('wake', 'Entrained air / depth'),
    ('calm', 'Highlight bloom'),
    ('storm', 'Airborne spray / actual material'),
    ('storm', 'Raised foam / actual material'),
    ('storm', 'Mist / actual material'),
    ('storm', 'Persistent crest sources / emission and age'),
    ('whitecaps', 'Environment reflection contribution'),
    ('whitecaps', 'Water body contribution'),
    ('whitecaps', 'Mean environment Fresnel'),
]
# Preserve review of the 42-view archives saved before the optical split.
optical_paths = [args.directory / f'MooringSimulator_{args.preset}-{scene}-{index:02}.png'
                 for index, (scene, _) in enumerate(cases) if index >= 42]
if not any(path.exists() for path in optical_paths):
    cases = cases[:42]
for page in range((len(cases) + 5) // 6):
    sheet = Image.new('RGB', (1440, 1440))
    draw = ImageDraw.Draw(sheet)
    for slot in range(6):
        index = page * 6 + slot
        if index >= len(cases):
            break
        scene, name = cases[index]
        path = args.directory / f'MooringSimulator_{args.preset}-{scene}-{index:02}.png'
        with Image.open(path) as capture:
            capture.thumbnail((720, 450))
            x, y = (slot % 2) * 720, (slot // 2) * 480
            sheet.paste(capture, (x, y))
        draw.text((x + 8, y + 455), f'{index:02} {name} / {scene}', fill='white')
    sheet.save(args.directory / f'{args.preset}-effects-{page}.png')

scenes = ('calm', 'rough', 'storm', 'wake', 'dock', 'light', 'calm-low', 'glass', 'whitecaps', 'catamaran')
sheet = Image.new('RGB', (1920, 1680))
draw = ImageDraw.Draw(sheet)
for index, scene in enumerate(scenes):
    with Image.open(args.directory / f'MooringSimulator_{args.preset}-{scene}-00.png') as capture:
        capture.thumbnail((640, 400))
        x, y = (index % 3) * 640, (index // 3) * 420
        sheet.paste(capture, (x, y))
    draw.text((x + 8, y + 402), scene, fill='white')
sheet.save(args.directory / f'{args.preset}-beauty-review.png')
