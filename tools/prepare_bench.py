#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage local Fade/Gate profiling and normal-firmware bench files; never write a card."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import wave

from generate_test_tones import generate

ROOT = Path(__file__).resolve().parents[1]


def prepare(output, card=None):
    output.mkdir(parents=True, exist_ok=True)
    audio = output / 'audio'
    generate(audio)
    # A whole second of 220 Hz repeats exactly, avoiding seams. Original audio.
    rate = 96000
    second = b''.join(round(0.15 * 8388607 * math.sin(2 * math.pi * 220 * i / rate))
                      .to_bytes(3, 'little', signed=True) for i in range(rate))
    with wave.open(str(audio / 'W9624.wav'), 'wb') as wav:
        wav.setparams((1, 3, rate, rate * 90, 'NONE', 'not compressed'))
        for _ in range(90):
            wav.writeframesraw(second)
    manifests = {}
    for kind, source in [('normal', ROOT / 'build'), ('profile', ROOT / 'build/profile')]:
        manifest = json.loads((source / 'manifest.json').read_text())
        image = source / 'image.hex'
        if manifest['profiling'] != (kind == 'profile'):
            raise ValueError(f'{kind}: wrong image configuration')
        if hashlib.sha256(image.read_bytes()).hexdigest() != manifest['image_sha256']:
            raise ValueError(f'{kind}: image does not match manifest')
        manifests[kind] = manifest
    if manifests['normal']['build_number'] != manifests['profile']['build_number']:
        raise ValueError('normal/profile build numbers differ; rebuild both')
    for name, mode, kind, playlist in [
        ('01-fade', 'Fade', 'profile', 'W9624.wav\n'),
        ('02-gate', 'Gate', 'profile', 'W9624.wav\n'),
        ('03-normal', 'Trigger', 'normal',
         'W9624.wav\nQ480.wav\nQ960.wav\nQ441.wav\nQ882.wav\n'),
    ]:
        case = output / name
        case.mkdir(exist_ok=True)
        source = ROOT / ('build/profile' if kind == 'profile' else 'build')
        shutil.copy2(source / 'image.hex', case / 'image.hex')
        shutil.copy2(source / 'manifest.json', case / 'manifest.json')
        (case / 'config.txt').write_text(f'# Hamburger Wavy bench\n{mode}\nQuantize\nShort\n')
        (case / 'playlist.txt').write_text(playlist)
    if card:
        backup = output / 'card-backup'
        backup.mkdir(exist_ok=True)
        for name in ('config.txt', 'playlist.txt', 'ONEPROF.TXT', 'ONEBUILD.TXT'):
            if (card / name).is_file():
                target = backup / name
                # Preserve the first pre-test snapshot across repeated preparation.
                if not target.exists():
                    shutil.copy2(card / name, target)
    shutil.copy2(ROOT / 'docs/bench.md', output / 'INSTRUCTIONS.md')
    print(f"Build {manifests['normal']['build_number']} bench pack: {output}")
    print('Cases: 01-fade (profile), 02-gate (profile), 03-normal (normal).')
    print('Card not modified. See docs/bench.md for copy and test instructions.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/bench')
    parser.add_argument('--card', type=Path, help='read-only source of pre-test config/report backups')
    args = parser.parse_args()
    prepare(args.out, args.card)
