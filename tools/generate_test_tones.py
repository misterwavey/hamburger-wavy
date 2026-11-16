#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create original 440 Hz mono PCM16 tones for pitch/sample-rate bench checks."""
import argparse
import math
from pathlib import Path
import struct
import wave


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    for name, rate in (('Q441', 44100), ('Q480', 48000), ('Q882', 88200), ('Q960', 96000)):
        frames = rate * 3
        ramp = rate // 200
        with wave.open(str(output / (name + '.wav')), 'wb') as wav:
            wav.setparams((1, 2, rate, frames, 'NONE', 'not compressed'))
            wav.writeframes(b''.join(struct.pack('<h', round(
                6000 * math.sin(2 * math.pi * 440 * i / rate) *
                min(1, i / ramp, (frames - 1 - i) / ramp)))
                for i in range(frames)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/test-tones'))
    generate(parser.parse_args().out)
