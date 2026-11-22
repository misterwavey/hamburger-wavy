#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read a CRC-checked ONEPROF.TXT; report observed timing and memory margins."""
import argparse
import json
from pathlib import Path
import re
import zlib

METRICS = ('sd', 'prefetch', 'decode', 'envelope', 'copy', 'refill_work')
MODES = ('Trigger', 'Gate', 'Fade', 'Loop', 'Sequence', 'Combo',
         'Random Sequence', 'Random Loop', 'Flip', 'CV Flip', 'Tape', 'Chop')
CV_MODES = ('Free', 'Quantize', 'Select File', 'Colour', 'Filter', 'Rate')
OPTIONAL_METRICS = ('colour', 'filter', 'mod', 'phaser', 'flanger')  # builds 33, 35 and 47 onwards
SCALARS = ('build', 'ticks_per_us', 'plays', 'timer_pair_min', 'wall_lo',
           'wall_hi', 'service_gap_max', 'min_slack_frames', 'report_failures', 'saturated',
           'sd_retries')
OPTIONAL_SCALARS = ('sd_retries',)  # build 46 onwards
OPTIONAL_ROWS = ('encoder', 'select', 'encoder_log', 'encoder_log16', 'debounce', 'debounce2', 'read_fail', 'chop')  # build 38: steps, recovered, dropped; 39 onwards: steps, recovered, guessed
ROWS = dict(stack_static_used_free=3, encoder=3, select=2, encoder_log=32, debounce=2, debounce2=3, encoder_log16=16, read_fail=6, chop=7, last=5, worst_work=5, min_slack=5,
            errors_ring_late_dma_read_copy=5, sd_hist=8,
            **{name: 5 for name in METRICS + OPTIONAL_METRICS})


def context(values):
    rate, bits, mode, cv, divider = values
    native, anchor = {44100:(44100,2176), 88200:(88200,1088),
                      96000:(96000,1000)}.get(rate, (48000,2000))
    return dict(header_rate=rate, bits=bits,
                mode=MODES[mode] if mode < len(MODES) else str(mode),
                cv=CV_MODES[cv] if cv < len(CV_MODES) else str(cv),
                divider=divider,
                approximate_output_rate=native*anchor/divider if divider else None)


def parse_report(raw):
    if len(raw) != 1024 or not raw.startswith(b'Hamburger Wavy profile 1\n'):
        raise ValueError('not a complete supported 1024-byte profile report')
    if not re.fullmatch(rb'crc32=[0-9a-f]{8}\n', raw[1009:]):
        raise ValueError('missing CRC footer; report may be incomplete')
    if zlib.crc32(raw[:1009]) != int(raw[1015:1023], 16):
        raise ValueError('CRC mismatch; refuse partial/torn/edited report')
    fields = {}
    for line in raw[:1009].decode('ascii').splitlines()[1:]:
        line = line.strip()
        if not line:
            continue
        if '=' in line:
            name, value = line.split('=', 1)
            values = [value]
            if name not in SCALARS:
                raise ValueError('unknown field '+name)
        else:
            name, *values = line.split(',')
            if name not in ROWS or len(values) != ROWS[name]:
                raise ValueError('invalid row '+name)
        if name in fields or any(not re.fullmatch(r'[0-9]+', v) for v in values):
            raise ValueError('duplicate field or invalid number')
        numbers = list(map(int, values))
        if any(n > 0xffffffff for n in numbers):
            raise ValueError('out-of-range counter')
        fields[name] = numbers[0] if name in SCALARS else numbers
    if set(fields) | set(OPTIONAL_METRICS) | set(OPTIONAL_ROWS) | set(OPTIONAL_SCALARS) != set(SCALARS) | set(ROWS) or not fields['ticks_per_us']:
        raise ValueError('missing required profile fields')
    scale = fields['ticks_per_us']
    wall = (fields['wall_hi'] << 32) | fields['wall_lo']
    metrics = {}
    for name in METRICS + tuple(n for n in OPTIONAL_METRICS if n in fields):
        count, low, high, minimum, maximum = fields[name]
        total = high << 32 | low
        if (not count and any((total,minimum,maximum))) or (count and
                (minimum > maximum or not count*minimum <= total <= count*maximum)):
            raise ValueError('inconsistent metric '+name)
        metrics[name] = dict(samples=count, total_us=total/scale,
                             mean_us=total/scale/count if count else None,
                             minimum_us=minimum/scale if count else None,
                             maximum_us=maximum/scale if count else None)
    slack = fields['min_slack_frames']
    tight = context(fields['min_slack'])
    output_rate = tight['approximate_output_rate']
    return dict(build=fields['build'], plays=fields['plays'],
                playback_seconds=wall/scale/1e6,
                timer_read_pair_min_us=fields['timer_pair_min']/scale,
                service_gap_max_us=fields['service_gap_max']/scale,
                minimum_slack_frames=None if slack == 0xffffffff else slack,
                minimum_slack_approx_us=slack/output_rate*1e6
                    if slack != 0xffffffff and output_rate else None,
                stack=dict(static_bytes=fields['stack_static_used_free'][0],
                           watermark_used_bytes=fields['stack_static_used_free'][1],
                           untouched_bytes=fields['stack_static_used_free'][2]),
                last=context(fields['last']), worst_work=context(fields['worst_work']),
                minimum_slack_context=tight, metrics=metrics,
                sd_histogram=dict(zip(('<=250us','<=500us','<=1ms','<=2ms','<=5ms',
                                      '<=10ms','<=50ms','>50ms'), fields['sd_hist'])),
                errors=dict(zip(('ring','late_commit','dma_stall','read','copy_overlap'),
                                fields['errors_ring_late_dma_read_copy'])),
                encoder=dict(zip(('steps','recovered','dropped' if fields['build'] == 38 else 'guessed'),
                                 fields['encoder']))
                    if 'encoder' in fields else None,
                debounce=dict(zip(('raw_pin_changes','longest_bridged_high_ms'), fields['debounce']))
                    if 'debounce' in fields else
                    dict(zip(('raw_pin_changes','longest_bridged_high_us','shortest_hold_us'), fields['debounce2']))
                    if 'debounce2' in fields else None,
                select=dict(zip(('steps_accepted','selections'), fields['select']))
                    if 'select' in fields else None,
                encoder_log=[dict(ms=round((v >> 4)*0.016, 3), saturated=(v >> 4) == 4095,
                                  ab=v & 3, playing=bool(v & 4), step=bool(v & 8))
                             for v in fields.get('encoder_log') or fields.get('encoder_log16')]
                            if 'encoder_log' in fields or 'encoder_log16' in fields else None,
                read_fail=dict(zip(('site','card_ready','sd_reason','sd_value','frame','reverse'),
                                   fields['read_fail'])) if 'read_fail' in fields else None,
                chop=fields.get('chop'),
                sd_retries=fields.get('sd_retries'),
                previous_report_failures=fields['report_failures'],
                saturated=bool(fields['saturated']))


def attach_build_info(report, manifest_path, baseline_path=None):
    manifest = json.loads(manifest_path.read_text())
    if manifest.get('build_number') != report['build'] or not manifest.get('profiling'):
        raise ValueError('local manifest must be the profiling build matching the report number')
    report['local_build_resources'] = manifest['resources']
    if baseline_path:
        baseline = json.loads(baseline_path.read_text())
        if baseline.get('profiling') or baseline.get('build_number') != report['build']:
            raise ValueError('baseline must be the same build number without profiling')
        if bool(manifest.get('extended_application')) != bool(baseline.get('extended_application')):
            raise ValueError('baseline must use the same application layout')
        report['profiling_overhead_bytes'] = dict(
            source_payload=manifest['source_payload_bytes']-baseline['source_payload_bytes'],
            main_text=manifest['sections']['.text']['bytes']-baseline['sections']['.text']['bytes'],
            static_ram=manifest['sections']['.bss']['bytes']-baseline['sections']['.bss']['bytes'])
        if manifest.get('extended_application'):
            report['profiling_overhead_bytes']['extension_text'] = (
                manifest['sections']['.app_extension']['bytes']-baseline['sections']['.app_extension']['bytes'])
            report['profiling_overhead_bytes']['combined_text'] = (
                manifest['resources']['combined_text_bytes']-baseline['resources']['combined_text_bytes'])
    frames = []
    for path in manifest_path.parent.glob('*.su'):
        for line in path.read_text().splitlines():
            parts = line.split('\t')
            if len(parts) == 3 and parts[1].isdigit():
                frames.append((int(parts[1]), parts[0].rsplit(':',1)[-1]))
    report['largest_compiler_frames'] = sorted(frames, reverse=True)[:5]


def summary(report):
    lines = [f"Build {report['build']}: {report['plays']} starts, "
             f"{report['playback_seconds']:.3f} s observed playback",
             'Timing in microseconds (nested categories; do not add their totals):',
             f"{'Operation':14} {'Samples':>10} {'Mean':>12} {'Worst':>12}"]
    for name, values in report['metrics'].items():
        if values['samples']:
            lines.append(f"{name:14} {values['samples']:10} "
                         f"{values['mean_us']:12.2f} {values['maximum_us']:12.2f}")
    lines.append(f"Longest background-service gap: {report['service_gap_max_us']:.2f} us")
    slack = report['minimum_slack_approx_us']
    lines.append('Minimum observed buffer slack: '+
                 (f"{report['minimum_slack_frames']} frames, approximately {slack:.2f} us"
                  if slack is not None else 'no valid observations'))
    lines.append('Worst-work context: '+json.dumps(report['worst_work']))
    lines.append('Tightest-slack context: '+json.dumps(report['minimum_slack_context']))
    stack = report['stack']
    lines.append(f"RAM: {stack['static_bytes']} static bytes; stack watermark "
                 f"{stack['watermark_used_bytes']} used, {stack['untouched_bytes']} untouched")
    lines.append('Stack watermark is a lower bound on use, not a proven worst-case stack bound.')
    lines.append('SD read-attempt histogram: '+json.dumps(report['sd_histogram']))
    lines.append('Errors: '+json.dumps(report['errors']))
    if report.get('encoder'):
        lines.append('FILE encoder since power-up: '+json.dumps(report['encoder'])+
                     ' (recovered/guessed/dropped = clicks where a quadrature state went unseen)')
    if report.get('chop') and report['chop'][0]:
        c = report['chop']
        print(f'Chop gates since power-up: {c[0]}; last six, oldest first (ms since previous gate, knob/CV reading, position as % of file, path):')
        for v in c[1:][-min(c[0], 6):]:
            adc, units = v & 1023, v >> 11
            gap = '>16368' if units == 1023 else str(units * 16)
            print(f"  {gap:>7}  adc {adc:4d}  {100 * (1023 - adc) / 1023:5.1f}%  {'jump while playing' if v & 1024 else 'start from stopped'}")
    if report.get('read_fail') and report['read_fail']['site']:
        r = report['read_fail']
        site = {1:'prefetch',2:'flip resume',3:'flip start'}.get(r['site'], str(r['site']))
        reason = {0:'none recorded',1:'command response',2:'token transfer timeout',3:'wrong data token',
                  4:'data transfer timeout',5:'CRC mismatch'}.get(r['sd_reason'], str(r['sd_reason']))
        lines.append(f"Last read error: at {site}, card {'still ready (not an SD failure)' if r['card_ready'] else 'marked failed'}, "
                     f"last SD failure: {reason} (value {r['sd_value']}), frame {r['frame']}, "
                     f"{'reverse' if r['reverse'] else 'forward'}")
    if report.get('sd_retries') is not None:
        lines.append(f"Sector reads retried after a failure: {report['sd_retries']}")
    if report.get('select'):
        lines.append('FILE selection: '+json.dumps(report['select']))
    if report.get('debounce'):
        lines.append('FILE contact filter: '+json.dumps(report['debounce']))
    if report.get('encoder_log'):
        lines.append(('Last filtered FILE pin changes' if report.get('debounce') else 'Last raw FILE pin changes')+', oldest first (ms since previous, AB, P=playing, *=step):')
        for e in report['encoder_log']:
            lines.append(f"  {'>' if e['saturated'] else ' '}{e['ms']:8.3f}  {e['ab']>>1}{e['ab']&1}"
                         f"  {'P' if e['playing'] else '-'} {'*' if e['step'] else ''}")
    lines.append(f"Previous report-write failures: {report['previous_report_failures']}")
    wall_us = report['playback_seconds'] * 1e6
    if wall_us:
        share = 100*report['metrics']['refill_work']['total_us']/wall_us
        lines.append(f"Measured refill work / observed playback time: {share:.2f}% "
                     '(completed refills only; includes SD waits; excludes discarded prefills/other work; not CPU utilisation)')
    lines.append(f"Back-to-back timer-read minimum: {report['timer_read_pair_min_us']:.3f} us; "
                 'instrumentation overhead is not subtracted.')
    if 'local_build_resources' in report:
        mem = report['local_build_resources']
        if 'combined_text_bytes' in mem:
            lines.append(f"Local build: {mem['combined_text_bytes']} combined code/constant bytes; "
                         f"{mem['combined_text_free']} bytes free across both banks")
            lines.append(f"Bank use/free: legacy {mem['text_bytes']}/{mem['text_free']}; "
                         f"extension {mem['extension_text_bytes']}/{mem['extension_free']}")
        else:
            lines.append(f"Local build: {mem['text_bytes']} code/constant bytes; "
                         f"{mem['text_free']} bytes left in the current application layout")
        lines.append('Profiling overhead versus normal build: '+
                     json.dumps(report.get('profiling_overhead_bytes', {})))
        lines.append('Largest compiler stack frames (not a call-chain bound): '+
                     json.dumps(report.get('largest_compiler_frames', [])))
    if report['saturated']:
        lines.append('COUNTERS SATURATED: start a fresh run before interpreting aggregates.')
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--manifest', type=Path, help='local manifest for this exact test image')
    parser.add_argument('--baseline', type=Path, help='normal-build manifest for overhead comparison')
    args = parser.parse_args()
    try:
        report = parse_report(args.report.read_bytes())
        if args.baseline and not args.manifest:
            raise ValueError("--baseline requires --manifest")
        if args.manifest:
            attach_build_info(report,args.manifest,args.baseline)
    except (OSError, ValueError, UnicodeError) as exc:
        parser.error(str(exc))
    print(json.dumps(report, indent=2) if args.json else summary(report))
