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
    for name, rate, bits, seconds in (('FLIP48', 48000, 16, 8),
                                      ('FLIP96', 96000, 24, 30)):
        frames = rate * seconds
        ramp_frames = rate // 100
        with wave.open(str(output / (name + '.wav')), 'wb') as wav:
            wav.setparams((1, bits // 8, rate, frames, 'NONE', 'not compressed'))
            for first in range(0, frames, 1024):
                samples = []
                for i in range(first, min(first + 1024, frames)):
                    t = i / rate
                    ramp = min(1, i / ramp_frames, (frames - 1 - i) / ramp_frames)
                    amplitude = 4900 * (256 if bits == 24 else 1)
                    phase = 2 * math.pi * (180 * t + (360 / seconds) * t * t)
                    value = round(amplitude * ramp * math.sin(phase))
                    samples.append(struct.pack('<h', value) if bits == 16 else
                                   value.to_bytes(3, 'little', signed=True))
                wav.writeframesraw(b''.join(samples))
        print(output / (name + '.wav'))



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/flip-test'))
    generate(parser.parse_args().out)
