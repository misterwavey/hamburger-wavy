# Profiling diagnostics

This optional diagnostic firmware measures observed operating margins on the module. Normal builds compile out the profiling hooks, counters, report writer and stack watermark. A first short hardware run is recorded below; the included `build/test-profile.txt` fixture remains synthetic regression data.

## Build and install

From the repository:

```sh
make profile
```

The diagnostic is `build/profile/image.hex`. The normal application remains at `build/image.hex`; the two have separate ELF files, generated tables and manifests. Both currently identify as build 20; the diagnostic manifest has `profiling: true`.

Install the diagnostic using the usual SD update procedure:

```sh
cp build/profile/image.hex "/Volumes/TTA ONE/image.hex"
sync
cmp build/profile/image.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Startup remains one second blue. `ONEBUILD.TXT` should show build 20. That number alone does not distinguish normal from profiling firmware: a freshly written `ONEPROF.TXT` after a run confirms the diagnostic writer ran. Preserve or remove an old profile report before testing so that an unsuccessful write is not mistaken for a fresh result.

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

For build 16, normal main text/constants use 26996 bytes and static RAM 32160 bytes. The diagnostic uses 31500 main-text bytes and 33488 static-RAM bytes, leaving 312 bytes before the fixed reset handler. Total extra source payload is 4544 bytes (including startup instrumentation); extra static RAM is 1328 bytes. These are measured build sizes, not measured hardware timings.

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

## High-pitch failure observation — 2026-11-16

A second CRC-valid report recorded eight starts over 1.751 seconds, with five ring underruns and three late-commit rejections. There were no SD read errors, stationary-DMA errors, copy-overlap observations or report-write failures. The recorded mode was Trigger/Quantize, not Loop. The short failed starts already demonstrate the throughput limit; they are not a completed sustained-loop test.

At the tightest observation, a 96 kHz/24-bit sample ran at approximately 135785 output frames/s (divider 707). A 512-frame half therefore lasted 3.771 ms. Completed refill work averaged 5.157 ms (minimum 5.006 ms, maximum 6.594 ms). Failed/unused prefetch attempts are excluded from that completed-work metric; prefetch alone reached 7.579 ms. The smallest observed margin was 21 frames, about 155 us. Stack watermark was 1736 bytes, leaving 30304 bytes untouched.

This confirms a streaming/refill scheduling-capacity failure in the diagnostic build at that setting, rather than evidence of corrupt reads or a stalled DMA engine. Mean sector-read attempt time was 1.415 ms and buffer copying remained 0.513 ms per half. The SD/SPI read path and bytewise audio copies are the next optimisation targets. This does not isolate card wait from driver CPU cost, and profiling itself adds overhead. Normal-firmware high-rate limits still need testing; sustained maximum-pitch 96 kHz/24-bit operation is not established as supported.

## Build 17 throughput trial

Build 17 adds an optional bulk-receive transport to the SD driver. The board implementation pipelines one SPI2 byte at a time at the same 12 MHz clock, starts the next byte before storing/polling, and avoids the per-payload-byte function-pointer call. Controls/watchdog continue to be polled every 64 transferred bytes. Each receive span has a bounded timeout; received sector data still undergo CRC16 checking. The ordinary byte transport remains the fallback and is still used for commands, responses and writes.

Audio cache and staging copies now use a typed, four-word-unrolled copy for non-overlapping uint32_t arrays. General-purpose `memcpy` remains byte-safe. Buffer sizes, copy deadline guards, pitch mapping and SD write scope are unchanged. The profiling report formatter uses size optimisation only while stopped, keeping the diagnostic within the existing application layout. Normal code/constants use 27480 bytes (4332 free); the diagnostic uses 31796 bytes (16 free). Static RAM is unchanged at 32160 / 33488 bytes respectively. Hardware speedup remains unmeasured until a new run is collected.

For the next comparison, keep the AGNT2-only playlist and install the build-17 profiling image. Copy `examples/config/loop-quantized.txt` to the card as `config.txt` so the run reliably starts in Loop/Quantize rather than Trigger. Preserve your previous config if needed. Use the same highest-pitch setting that failed in build 16, run for a minute if it remains playing, then turn FILE to stop and wait one second. If it turns blue earlier, stop triggering and let the diagnostic save its failure report. The new report must say build 17; old build-16 data can remain on the card until a new report is successfully written.

Regression coverage includes bulk and fallback card protocols, CRC rejection, partial transfer failure, control polling cadence and timeout wraparound. A narrow MIPS32 instruction model executes the actual linked receive/copy functions to check clock/receive counts, one outstanding byte, guard bytes and poll placement. This is a logical register model, not a cycle-accurate SPI/card simulation. Independent measurements on the module are still required.

## Build 17 hardware result and build 18 CRC trial

A CRC-valid build-17 report recorded one Loop/Quantize start with a 96 kHz/24-bit file at approximately 143928 output frames/s. It stopped after 31.24 ms with one late-commit rejection and no read, stationary-DMA, ring-underrun or copy-overlap errors. Copy time fell to 75.333 us from build 16's roughly 512.6 us. Mean SD read attempt time was 1.137 ms, prefetch 3.812 ms and completed refill work 3.903 ms. The half-buffer period was 3.557 ms; the improvement was insufficient for sustained operation. Minimum observed margin was 112 frames (778 us), below the unchanged 128-frame copy guard. Reducing that guard alone would not correct an average refill slower than the playback period.

Build 18 replaces the CRC16 nibble-table update with a folded polynomial calculation, eliminating both per-byte flash-table reads. The polynomial, initial state, received CRC bytes, CRC comparisons and write CRCs are unchanged. All 65536 CRC states times 256 input bytes were checked against an independent bitwise recurrence (16777216 combinations), alongside the existing normal/instrumented protocol, corruption, transport-failure and filesystem tests. The measured 544 us maximum service gap in the previous run is consistent with a costly software section, but does not by itself isolate that section's duration; hardware measurement is required to quantify the CRC change.

For the next trial install the build-18 profiling image and keep the existing AGNT2-only playlist and Loop/Quantize config. Repeat the same full-pitch run, stop by browsing if it remains playing, wait one second, and collect the report. Normal main code/constants now use 27368 bytes (4444 free); profiling uses 31684 (128 free). RAM is unchanged. No clock, buffer, deadline guard or update-layout change was made.


## Build 18 hardware result and build 19 cache-copy trial

The CRC-valid build-18 report recorded 0.487 seconds of playback with one late-commit rejection and no read, ring, stationary-DMA or copy-overlap errors. Mean SD read time fell to 1.026 ms, prefetch to 3.486 ms and completed refill work to 3.565 ms. The tightest half-buffer period remained 3.557 ms at divider 667 (approximately 143928 output frames/s). The worst completed refill was 4.686 ms; pitch varied during this run. These measurements show improvement, but insufficient sustained margin. Stack watermark remained 1680 bytes.

Build 19 adds an alignment-checked word path to the project-written general-purpose memcpy, speeding aligned copies such as the FAT sector cache into the decoder input buffer. Both addresses must be word aligned; unaligned copies and trailing bytes retain byte accesses. A compiler-supported may_alias word type permits access to arbitrary object representations. Host sanitizer and compiled-MIPS tests cover every source/destination alignment combination, tails and boundary guards. This does not change the SD clock, CRC checks, buffer sizes or copy deadline guard.

Normal code/constants use 27464 bytes (4348 free); profiling uses 31780 bytes (32 free). Static RAM is unchanged. All 11 release tests pass. Hardware performance remains unmeasured. Install the build-19 profiling image, retain the AGNT2-only playlist and Loop/Quantize config, and repeat the maximum-pitch run. If playback continues, turn FILE to stop, then wait one second before powering down. A fresh report must identify build 19.


## Build 19 hardware result

The CRC-valid report recorded one start and 2.980 seconds of playback, ending with one late-commit rejection. Read, stationary-DMA, ring and copy-overlap counters were zero. Mean completed refill time was 3.535 ms, worst 4.675 ms; mean prefetch was 3.456 ms and worst 4.593 ms. SD reads averaged 1.045 ms (worst 1.120 ms), with all attempts below 2 ms. Minimum slack was 122 frames, approximately 848 us at divider 667, below the unchanged 128-frame guard. Stack watermark remained 1680 bytes.

The mean is now slightly below the tightest 3.557 ms half-buffer period, but leaves only about 22 us before other work and timing variation. Pitch varied: the worst completed refill context used divider 707, while the tightest slack used 667. The report does not record failure position or attribute individual stalls to a loop boundary. AGNT2 contains 261862 frames, lasting about 1.819 seconds at maximum pitch; the observed duration is consistent with crossing a loop boundary and stopping later, rather than proving failure exactly on wrap. More buffering can absorb occasional slow refills, but average throughput margin still needs improvement.


## Build 20 buffer and decoder trial

Build 20 doubles each DMA half from 512 to 1024 frames, the whole DMA ring from 1024 to 2048 frames, and the associated initial cache, staging and raw-input buffers. At divider 667 a half lasts approximately 7.115 ms. Each refill now processes twice as much audio: a larger half provides more tolerance of individual delays, not twice the sustainable throughput. The SD clock remains 12 MHz, received sectors retain CRC checks, and the commit guard remains 128 frames (1024 bytes). Profiler margin calculations and the host DMA model now use the actual buffer constants. Build manifests record the buffer sizes.

PCM16 and PCM24 decoding use separate loops selected once per buffer. For 512 frames, the compiled normal-image logical MIPS model executes 5137 steps for PCM16 and 6672 for PCM24, compared with 7182 and 8206 in build 19. This is an instruction-path comparison, not a cycle-accurate speed measurement. Both formats, zero-length input, unaligned input and output boundary guards are checked through the compiled MIPS code. Loop continuity, PCM tails, prefetch, envelope/retrigger behaviour and deadline-error recovery are also exercised under host sanitizers with the larger buffers.

Normal main code/constants use 27472 bytes (4340 free); profiling uses 31812 (zero free in the existing main-text region). Size optimisation of file-header parsing and profiler setup/stop code keeps the diagnostic within the fixed layout. No linker boundary, update coverage or boot configuration is expanded. Static RAM rises to 55712 bytes normally and 57040 with profiling, leaving 9824 and 8496 bytes respectively for the stack, including the existing 8192-byte minimum reserve. This deliberately spends most of the previous RAM headroom on streaming. Future feature work must revisit the buffer/cache allocation rather than assuming the earlier spare RAM remains available.

Larger buffers can increase loop-trigger preparation latency because twice as much initial audio is read before playback starts. Buffered envelope/retrigger scheduling and the block-rounded end-of-playback interval can also lengthen. Trigger-mode starts still use the preloaded cache. Existing mode/light choices are retained; the effects on feel need a hardware check after the throughput trial.

Install the build-20 profiling image and retain the AGNT2-only playlist and Loop/Quantize config. Set maximum pitch before starting and leave it there, so the report is a clean comparison. Aim for one minute of looping; if it stays playing, turn FILE to stop. Whether it stops itself or you stop it, wait one second before powering down and mounting. Check that the fresh report identifies build 20. Hardware performance remains unmeasured.
