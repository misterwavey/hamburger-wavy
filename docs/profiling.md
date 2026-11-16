# Profiling build 16

This optional diagnostic firmware measures observed operating margins on the module. Normal builds compile out the profiling hooks, counters, report writer and stack watermark. A first short hardware run is recorded below; the included `build/test-profile.txt` fixture remains synthetic regression data.

## Build and install

From the repository:

```sh
make profile
```

The diagnostic is `build/profile/image.hex`. The normal application remains at `build/image.hex`; the two have separate ELF files, generated tables and manifests. Both identify as build 16; the diagnostic manifest has `profiling: true`.

Install the diagnostic using the usual SD update procedure:

```sh
cp build/profile/image.hex "/Volumes/TTA ONE/image.hex"
sync
cmp build/profile/image.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Startup remains one second blue. `ONEBUILD.TXT` should show build 16. That number alone does not distinguish normal from profiling firmware: a freshly written `ONEPROF.TXT` after a run confirms the diagnostic writer ran. Preserve or remove an old profile report before testing so that an unsuccessful write is not mistaken for a fresh result.

## Collect a report

Play or loop a sample for at least a minute. Stop external triggers before collecting. For sustained looping, turn FILE once to stop playback; this also works with a single-entry playlist. For a non-looping sound, simply let it finish.

Wait at least one second with playback stopped, PLAY released, no pending trigger and no mode menu open. The firmware attempts to write a report after 250 ms of that idle condition. Allocation, SD writing and readback can take longer, depending on the card. A new trigger arriving during a report write is handled afterward, so diagnostic mode can briefly delay that next trigger. No report writes occur while audio is playing.

Power down, return the card to the computer and preserve the report with a descriptive name:

```sh
cp "/Volumes/TTA ONE/ONEPROF.TXT" ./profile-96k24-fast.txt
python3 tools/analyze_profile.py profile-96k24-fast.txt \
  --manifest build/profile/manifest.json \
  --baseline build/manifest.json
```

The manifests are optional local build information; use the ones for the exact image you tested. Add `--json` for machine-readable results. Individual compiler stack-frame sizes are emitted as `build/profile/*.su` and `build/*.su`; the analyser lists the largest when a manifest is supplied.

The firmware accumulates statistics over the current boot, across triggers, files and mode changes, and overwrites the report after each subsequent stop. Remounting a card does not intentionally start a new session. Power-cycle between test cases if you want separate results. Save each report before the next case.

## Suggested test order

Start with a quick smoke test: one Q480 playback, let it finish, wait, and check the report. Then run:

| Case | Material and activity |
| --- | --- |
| Baseline | 48 kHz/16-bit Q480, Loop, original pitch; one minute. |
| Higher rate | 96 kHz/16-bit Q960, then a 96 kHz/24-bit ordinary sample. |
| Tight deadline | Same 96 kHz/24-bit sample at highest quantized pitch; several minutes. |
| Envelopes | Fade, repeated quick retriggers, then Gate release, using the same sample. |
| Seeking | Random Loop, repeated triggers on a longer sample. |
| Mixed workload | File changes, multiple sample rates, then varying CV; preserve this as a separate stress case. |

Repeat the useful cases at 44.1/88.2 kHz and on other cards if available. Native pitch depends on the input mapping; a tuner and the generated 440 Hz files help locate it. The clock rates in the report are approximate, not tuning calibration. A report captures the settings at worst-work and tightest-margin observations, not a trace of every file or event.

## What is measured

- `sd`: end-to-end streaming sector read attempts, including command/token waits, transfer, CRC and background polling. Failed attempts can be included. This is not isolated card response latency.
- `prefetch`: raw file read plus decoding into the staging half, including FAT lookups. Some prefetched data may be discarded on stop/retrigger.
- `decode`: PCM conversion calls during streaming.
- `envelope`: combined envelope and tail rendering for a published half.
- `copy`: buffer copy and publication barrier.
- `refill_work`: the corresponding prefetch work plus publication work for completed refills. It excludes the intentional waiting between prefetch and publication, and includes the nested SD/decoder/envelope/copy operations. Do not add the category totals together.

Each metric has sample count, total, minimum and maximum; the host tool calculates means. The SD histogram uses inclusive upper bounds of 250 us, 500 us, 1/2/5/10/50 ms, then an open-ended final bucket. This supports coarse latency-distribution inspection rather than exact percentiles.

The report also records maximum background-service gap, total observed playback interval, stream error counters, and the smallest divider-weighted buffer margin before/after publication. The margin includes unsafe observations: zero can indicate a missed deadline. Its frame count belongs to the settings recorded alongside it; comparing only frame counts across different pitches is misleading. Minimum margin in microseconds is estimated from nominal clock anchors. Polling can miss events between observations; these are measured samples, not exhaustive timing guarantees.

The timer is the CPU Count register, using the existing firmware's nominal 24 ticks/us conversion at a 48 MHz CPU clock. Unsigned short intervals and split 64-bit accumulated totals handle timer wrap. This timebase has not been calibrated against external equipment. A back-to-back timer-read minimum is included, but does not capture all instrumentation overhead. No overhead is subtracted from measurements.

Measured refill-work share includes blocking SD waits and excludes other work and discarded prefills. It is **not CPU utilisation**: the application continuously polls even when there is spare processing time. Use worst work, buffer margin, service gaps and error counts together when assessing whether an added feature is plausible. The profiling build itself perturbs timing.

## Stack and memory

Before the first C function runs, the diagnostic fills unused RAM between the linker-defined static-data end and the initial stack top with a pattern. The report scans for the lowest modified word. This gives an observed stack watermark, including startup/mount work and earlier report activity. It is a lower bound on stack use: a reserved but unwritten frame can retain the pattern. The current report write can deepen the stack after its snapshot; a later report can observe that use. Compiler `.su` frame sizes are supplementary, not a full call-chain bound.

The untouched gap is not a guaranteed safe allocation budget. Longer or different workloads may use more stack, and the diagnostic has extra frames. This build keeps the same 8 KiB minimum linker stack reservation and the same application-address limit. Compare normal/profiling manifests for code and static-RAM overhead; do not mistake the diagnostic's small code-space remainder for the normal firmware's headroom.

For this build, normal main text/constants use 26996 bytes and static RAM 32160 bytes. The diagnostic uses 31500 main-text bytes and 33488 static-RAM bytes, leaving 312 bytes before the fixed reset handler. Total extra source payload is 4544 bytes (including startup instrumentation); extra static RAM is 1328 bytes. These are measured build sizes, not measured hardware timings.

These measurements do not establish whether the resident updater accepts a larger flash layout. That remains a separate investigation.

## Report storage and failure handling

`ONEPROF.TXT` is a reserved 1024-byte text file. First creation can allocate FAT clusters and update a root directory entry and applicable FAT32 FSInfo hints, using the same restricted allocator as the recall file. Updates write two data sectors and verify them by readback. An existing unrelated, read-only or differently sized file is refused. No general-purpose file-writing API is added.

The fixed-position CRC32 footer covers the report body, including padding. The analyser rejects truncated, torn or edited reports rather than presenting potentially inconsistent counters. A failed report is attempted once per stop; a later playback/stop attempts a new report. Previously observed write failures are included in later successful reports. A genuinely unreadable card still needs normal card recovery. Interrupted metadata allocation has the existing FAT limitation: it can leak a cluster and is not transactional.

## Return to normal firmware

```sh
make
cp build/image.hex "/Volumes/TTA ONE/image.hex"
sync
cmp build/image.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Install normally. Normal firmware leaves an existing profile report untouched; it may be stale. Delete it on the computer when no longer needed.

## First hardware observation — 2026-11-16

A CRC-valid build-16 report from five playback starts over 11.91 seconds recorded no streaming errors or report-write failures. The card's image matched the profiling artifact; `ONEBUILD.TXT` identified build 16. The last and worst observed contexts were 96 kHz/24-bit Trigger/Quantize, slowed to approximately 57–64 kHz output. No envelope samples were recorded.

| Operation | Mean | Worst |
| --- | --- | --- |
| Streaming sector read attempt | 1.528 ms | 4.533 ms |
| Prefetch/read and decode | 4.976 ms | 9.191 ms |
| Complete refill work | 5.496 ms | 9.713 ms |
| PCM decoding | 0.181 ms | 0.182 ms |
| Half-buffer copying/publication | 0.513 ms | 0.513 ms |

The minimum observed publication margin was 401 frames, about 6.257 ms at its recorded rate. The maximum background-service gap was 0.534 ms. The stack watermark indicated 1696 bytes used and 30344 bytes untouched in the instrumented build. Refill work occupied about 54.2% of observed playback time; this is not CPU utilisation. SD read attempts dominated that work; the report does not separate card wait from driver/CRC/polling overhead.

This short run supports comfortable observed RAM margin and successful read-ahead, not a guarantee at native/highest output rates. At 96 kHz, a 512-frame half lasts 5.333 ms; at highest quantized pitch it lasts about 3.56 ms. The current 5.496 ms mean suggests those conditions need explicit sustained tests and possibly read/copy optimisation before adding substantial audio processing. Run them as separate cases, followed by Fade/Gate to measure the currently unsampled envelope path.
