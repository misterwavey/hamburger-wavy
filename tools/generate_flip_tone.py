#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate an original rising chirp so direction flips can be heard clearly."""
import argparse
import math
from pathlib import Path
import struct
import wave


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    rate, seconds = 48000, 8
    frames = rate * seconds
    with wave.open(str(output / 'FLIP48.wav'), 'wb') as wav:
        wav.setparams((1, 2, rate, frames, 'NONE', 'not compressed'))
        for first in range(0, frames, 1024):
            samples = []
            for i in range(first, min(first + 1024, frames)):
                t = i / rate
                ramp = min(1, i / 480, (frames - 1 - i) / 480)
                value = round(4900 * ramp * math.sin(2 * math.pi * (180 * t + 45 * t * t)))
                samples.append(struct.pack('<h', value))
            wav.writeframesraw(b''.join(samples))
    print(output / 'FLIP48.wav')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/flip-test'))
    generate(parser.parse_args().out)
