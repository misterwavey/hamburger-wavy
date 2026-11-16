# SPDX-License-Identifier: GPL-3.0-only
"""Checks that require only this source tree and ordinary build tools."""
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build import extract, template


def read_hex(path):
    memory = {}
    base = 0
    ended = False
    for line in path.read_text().splitlines():
        if ended or not line.startswith(':'):
            raise ValueError('unexpected HEX content')
        raw = bytes.fromhex(line[1:])
        if len(raw) != raw[0] + 5 or sum(raw) & 255:
            raise ValueError('invalid HEX record')
        count, kind = raw[0], raw[3]
        address = int.from_bytes(raw[1:3], 'big')
        if kind == 0:
            for i, byte in enumerate(raw[4:-1]):
                target = base + address + i
                if target in memory:
                    raise ValueError('overlapping HEX data')
                memory[target] = byte
        elif kind == 4 and count == 2:
            base = int.from_bytes(raw[4:-1], 'big') << 16
        elif kind == 1 and count == 0:
            ended = True
        else:
            raise ValueError('unexpected record type')
    if not ended:
        raise ValueError('missing EOF')
    return memory


class ReleaseTests(unittest.TestCase):
    def test_image_contains_source_and_fill_only(self):
        payload, _, layout, _ = extract(ROOT / 'build/hamburger-wavy.elf')
        image = read_hex(ROOT / 'build/image.hex')
        _, contract = template(ROOT / 'board/update-layout.json')
        self.assertEqual(set(image), set(contract))
        for address, byte in image.items():
            self.assertEqual(byte, payload.get(address, 255), hex(address))
        self.assertNotIn('.boot_debug', layout)
        self.assertEqual({a for a in image if a >= 0x1fc00000},
                         set(range(0x1fc00bf4, 0x1fc00bfc)))
        for address, word in ((0x1fc00bf4, 0xfff9fffc), (0x1fc00bf8, 0xff6bcedb)):
            self.assertEqual(bytes(image[address+i] for i in range(4)),
                             word.to_bytes(4, 'little'))

    def test_generated_pitch_tables_against_math(self):
        report = json.loads((ROOT / 'build/pitch_tables.json').read_text())
        self.assertEqual(report['semitones'], list(range(7, -42, -1)))
        for base, row in zip(report['native_dividers'], report['dividers']):
            self.assertEqual(len(row), 49)
            self.assertEqual(row, sorted(row))
            for note, value in zip(report['semitones'], row):
                ideal = base * 2**(-note/12)
                self.assertEqual(value, math.floor(ideal + 0.5))
                self.assertGreaterEqual(value, 512)
                self.assertLessEqual(value, 65535)
                self.assertLess(abs(1200*math.log2(ideal/value)), 1.1)
            self.assertEqual(row[7], base)
            self.assertEqual(row[19], base*2)
            self.assertEqual(row[31], base*4)
            self.assertEqual(row[43], base*8)

    def test_rebuild_from_isolated_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ('firmware', 'tools', 'board'):
                shutil.copytree(ROOT/name, root/name,
                                ignore=shutil.ignore_patterns('__pycache__'))
            result = subprocess.run([sys.executable, str(root/'tools/build.py')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual((root/'build/image.hex').read_bytes(),
                             (ROOT/'build/image.hex').read_bytes())

    def test_host_core_under_sanitizers(self):
        cc = os.environ.get('CC') or shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        sources = [ROOT/'tests/core.c', *[ROOT/'firmware'/f'{name}.c' for name in
                   ('fat', 'wave', 'sd', 'ring', 'memory', 'app', 'advertise',
                    'settings', 'policy', 'ui', 'recall')]]
        binary = ROOT/'build/core-test'
        command = [cc, '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-fno-builtin', '-fsanitize=address,undefined',
                   '-I'+str(ROOT/'build'), *map(str, sources), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertIn('checks passed', result.stdout)
        (ROOT/'build/host-test-log.txt').write_text(result.stdout)

    def test_distribution_has_notices_and_no_private_inputs(self):
        self.assertIn('Version 3, 29 June 2007', (ROOT/'LICENSE').read_text())
        self.assertIn('Copyright 2011 Ben Buxton', (ROOT/'NOTICE').read_text())
        for name in ('vendor', 'backups', 'releases', '.source-history'):
            self.assertFalse((ROOT/name).exists(), name)
        for path in (ROOT/'firmware').iterdir():
            self.assertIn('SPDX-License-Identifier: GPL-3.0-only', path.read_text())


if __name__ == '__main__':
    unittest.main()
