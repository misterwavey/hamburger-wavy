# Reverse playback feasibility

Build 24 does not implement reverse playback. The extended build-25 candidate now implements [Flip](flip.md), with indexed backward streaming and gate-controlled direction changes. Its first 48 kHz/16-bit hardware run passed with zero recorded errors; higher-rate limits remain unmeasured. The larger verified flash bank makes a fuller implementation easier to fit, but does not itself add sample memory, processing speed or card bandwidth.

## What the measurements support

Build 21 sustained maximum-quantized-pitch 96 kHz/24-bit forward looping for 152.681 seconds with no recorded errors. Fade and manual Gate also passed their tested high-rate runs. This establishes a useful streaming baseline on the tested card, not reverse performance. Build 24 then completed a 95.183-second maximum-pitch Loop run without recorded errors; its 5.996 ms mean refill and 6.691 ms minimum observed margin closely match build 21. Envelope and normal-build regressions on the new layout are still pending.

The extended build-24 normal image leaves 53188 bytes of combined code/constants capacity, while static RAM is 55712 bytes. It retains the 8192-byte minimum stack reserve and only 1632 bytes of static growth budget; profiling leaves 304 bytes of static growth budget. Extra flash is room for implementation, not room to cache an entire recording. W9624 alone contains 25.92 MB of PCM audio.

## How to stream in reverse

The SD driver can already read individual sectors by address. A reverse reader can fetch the last chunk of audio using ordinary forward byte reads, decode that chunk's frames in reverse order into the existing staging buffer, then fetch the preceding chunk. Frames must be reversed, not the bytes inside each 16/24-bit little-endian sample. Decoding directly into reverse frame order can avoid a separate whole-buffer swap. The audio DMA still transmits forward from the same two halves; pitch-rate hardware remains unchanged.

The initial reverse chunk can be preloaded from the end of the file, using the existing cache allocation. Direction selected before triggering is therefore simpler than changing direction during playback. A live flip must also reconcile the current DMA position, prefetched data and file cursor. Reverse loop wrap and short-file handling need separate frame-accurate logic.

## Current filesystem obstacle

In firmware/fat.c, file_seek resets the cluster cursor to the file's first cluster for any backward seek. file_read then follows the forward FAT chain to the requested cluster. Repeating this for every reverse block makes the work grow with file length. Even backward reads within the same cluster currently reset the cursor unnecessarily. A FAT chain has no direct previous-cluster link.

Build 25 implements the first seek improvements for Flip: retain the cursor when the new position is in the same cluster, then use a bounded sparse cluster index and/or contiguous-run mapping to reach earlier clusters without repeatedly traversing the whole file. Build this information while playback is stopped. A bounded design must handle fragmentation, long files and corrupt chains without unbounded RAM allocation. The current index uses 32 uint32_t checkpoints (128 bytes), plus identity, contiguous-chain and boundary-cache metadata. Other state is included in the tight RAM budget. A full in-memory cluster list is not a general solution for long files.

## Suggested implementation order

1. Improve backward seeking and cover fragmented FAT16/FAT32 files with known sample-order fixtures.
2. Add reverse one-shot playback, with direction chosen before the trigger and the tail preloaded. Start hardware checks at 48 kHz/16-bit, then test 96 kHz/24-bit at maximum pitch.
3. Add reverse looping, including short files, non-sector-aligned data and exact wrap continuity.
4. Evaluate live direction changes and optional transition fades only after streaming margins and control behaviour are measured.

Reverse sector access can have different timing from forward reads, so the existing profiler must measure it on hardware. The observed forward refill margin is not a guaranteed reverse/DSP budget. Expected samples, EOF/wrap positions, envelopes, retrigger recovery and card-error handling all need tests; musical latency and transition quality need listening or scope checks.

Build 25 adds 208 bytes of static RAM for Flip/index state, leaving 48948 normal code bytes free. The index and boundary-cache implementation are software tested; the first chirp hardware run sounded good to the user and had zero recorded errors. Transition latency and higher-rate limits still need measurement.
