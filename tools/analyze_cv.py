#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate and summarise ONECV.TXT raw ADC captures; voltage labels come from the bench."""
import argparse
import json
import math
from pathlib import Path
import re
import zlib

MAGIC = b'Hamburger Wavy CV 1\n'
COLUMNS = 'row,n,min,max,sum,ss_lo,ss_hi,at0,at1023,ms,missed'


def parse_report(raw):
    if len(raw) != 1024 or not raw.startswith(MAGIC):
        raise ValueError('not a complete supported CV report')
    if not re.fullmatch(rb'crc32=[0-9a-f]{8}\n', raw[1009:]):
        raise ValueError('missing CV report CRC footer')
    if zlib.crc32(raw[:1009]) != int(raw[1015:1023], 16):
        raise ValueError('CV report CRC mismatch')
    lines = [line.strip() for line in raw[:1009].decode('ascii').splitlines()[1:] if line.strip()]
    header = {}; records = []; in_rows = False
    for line in lines:
        if line == COLUMNS:
            if in_rows: raise ValueError('duplicate row header')
            in_rows = True; continue
        if not in_rows:
            key, separator, value = line.partition('=')
            if not separator or key in header or not re.fullmatch(r'[0-9]+', value):
                raise ValueError('invalid CV header')
            header[key] = int(value)
        else:
            values = line.split(',')
            if len(values) != 11 or any(not re.fullmatch(r'[0-9]+', v) for v in values):
                raise ValueError('invalid CV row')
            fields = dict(zip(COLUMNS.split(','), map(int, values)))
            if any(v > 0xffffffff for v in fields.values()): raise ValueError('counter out of range')
            n, lo, hi = fields['n'], fields['min'], fields['max']
            total = fields['sum']; squared = fields['ss_lo'] + (fields['ss_hi'] << 32)
            if fields['row'] != len(records)+1 or not 1 <= n <= 5000 or not 0 <= lo <= hi <= 1023:
                raise ValueError('invalid ADC range/count/order')
            if not n*lo <= total <= n*hi or not n*lo*lo <= squared <= n*hi*hi or squared*n < total*total:
                raise ValueError('inconsistent ADC moments')
            at0, at1023 = fields['at0'], fields['at1023']
            if at0+at1023 > n or bool(at0) != (lo == 0) or bool(at1023) != (hi == 1023):
                raise ValueError('inconsistent ADC rail counts')
            if fields['ms'] < 5000 or fields['missed'] != 5000-n:
                raise ValueError('inconsistent capture duration/missed polls')
            records.append(dict(row=fields['row'], samples=n, minimum=lo, maximum=hi,
                                mean=total/n, standard_deviation=math.sqrt(squared*n-total*total)/n,
                                at_zero=at0, at_full_scale=at1023, elapsed_ms=fields['ms'],
                                missed_polls=fields['missed'], sum=total, sum_squares=squared))
    required = {'build','adc_bits','settle_ms','window_ms','poll_ms','rows'}
    if set(header) != required or not in_rows or not 1 <= header['rows'] <= 12 or header['rows'] != len(records):
        raise ValueError('missing header or incorrect row count')
    if (header['adc_bits'],header['settle_ms'],header['window_ms'],header['poll_ms']) != (10,500,5000,1):
        raise ValueError('unsupported capture configuration')
    return dict(build=header['build'], adc_bits=10, window_ms=5000, captures=records)


def summary(report):
    lines = [f"Build {report['build']}: {len(report['captures'])} CV captures (raw 10-bit ADC counts)",
             'Row  Samples    Min    Max      Mean    Std.dev  At 0  At 1023  Missed']
    for r in report['captures']:
        lines.append(f"{r['row']:3} {r['samples']:8} {r['minimum']:6} {r['maximum']:6} "
                     f"{r['mean']:9.3f} {r['standard_deviation']:10.3f} "
                     f"{r['at_zero']:5} {r['at_full_scale']:8} {r['missed_polls']:7}")
    lines.append('Match row numbers to your measured voltages and knob positions; ADC counts are not volts.')
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try: result = parse_report(args.report.read_bytes())
    except (ValueError, OSError, UnicodeError) as exc: parser.error(str(exc))
    print(json.dumps(result, indent=2) if args.json else summary(result))
