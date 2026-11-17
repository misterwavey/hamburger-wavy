# Flip: interrupt playback direction

Flip is available in the extended build-25 candidate (`make extended` or `make extended-profile`). It is not enabled in the retained legacy build-21 targets. It loops a file and reverses direction from a captured playback position when GATE rises. It does not restart the file at each gate.

## Controls

| Control | Action in Flip |
| --- | --- |
| PLAY tap | Start forwards from the beginning; tap again to stop. A later start begins forwards again. |
| GATE rising edge | Reverse the running sample from the DMA playhead position captured at that edge. |
| GATE falling edge | No direction change. Holding GATE high produces one flip. |
| FILE | Browse as usual; browsing stops playback and selects/primes the next file. |
| Pitch knob / CV | Retain the selected Free/Quantize speed mapping. |
| Held PLAY + FILE | Existing mode menu; Flip is red count 9. |

GATE alone does not start playback in Flip. Start with PLAY, then use an external trigger, clock or manual gate to interrupt direction. Rising edges during the mode menu or a PLAY gesture are suppressed consistently with existing control handling. In Select File CV mode, PLAY starts still select the file from CV at nominal rate; GATE edges flip direction instead of selecting/retriggering a file.

To boot directly into Flip, config.txt can contain:

```text
# Hamburger Wavy configuration
Flip
Quantize
Short
```

Existing mode counts 1–8 and LED colours retain their meaning. Playback stays red; blue is not used as a direction indicator. Power cycling reloads config.txt.

## Transition behaviour

The gate handler records the source frame represented by the current DMA half and offset, rather than the filesystem/read-ahead cursor. Direction changes are serviced after any in-progress SD operation finishes. The first implementation snapshots a short continuation of the currently queued audio, stops the old DMA stream, plays a 256-frame fade to silence, then reads the initial two halves in the requested direction and fades in. The sample wraps at either end and continues looping.

There is a brief refill gap: this is not a seamless crossfade or sample-accurate scratching implementation. Fade time scales with playback rate, and card access adds latency. That latency needs hardware measurement. New edges during fade/refill update the requested direction without reentering the SD driver; multiple edges can coalesce before playback resumes. Very fast gates may therefore produce interrupted or silent output instead of one fully audible reversal per edge. Begin with slow manual pulses or a 1 Hz clock, then increase the rate to find useful behaviour on the tested sample/card.

PLAY, browsing and menu changes cancel pending flips. Errors use the existing recovery/blue signals. Repriming a flip does not write file-recall state; ordinary manual starts retain the existing recall behaviour.

## Streaming implementation

A selected Flip file receives a stopped-audio FAT-chain scan with bounded cycle detection and 32 sparse checkpoints. Contiguous chains are identified and addressed directly. Backward seeks within a cluster retain that cursor. The index is bounded independently of file length; large fragmented files can still have higher seek costs and longer selection/prime times.

Reverse blocks are read in ordinary forward byte order and decoded with frame order reversed, preserving little-endian PCM16/24 samples. Short files and wraps can split a requested output block into several reads. Two boundary sectors are cached in the unused tail of the existing raw-input buffer when space permits. This avoids an extra shared-sector read on steady reverse windows without allocating another audio buffer. Cache ownership is invalidated when the scratch buffer changes or a full-size prime overwrites that tail.

The normal/profiling images keep the existing audio buffers, SD clock, CRC verification, DMA deadline guard and minimum 8 KiB stack reserve. New code is in the verified upper-bank layout. Hardware validation of Flip remains pending.

## First hardware check

Generate the original eight-second rising tone with `python3 tools/generate_flip_tone.py`. It creates build/flip-test/FLIP48.wav, mono PCM16 at 48 kHz. The tone rises from 180 to 900 Hz at native speed, making direction changes much easier to hear than a constant sine. At maximum quantized pitch it is roughly 5.3 seconds long before wrapping.

The staged build/flip-test/config.txt selects Flip/Quantize, and playlist.txt contains only FLIP48.wav. Install the extended profiling image, copy those files and the WAV, sync/eject, then use the usual updater procedure. Start with maximum pitch and PLAY. After hearing the sweep rise, send one GATE pulse: it should sweep down from near that position. The next pulse should turn it upwards. Try slow pulses first, then a clock around 1–5 Hz, and check that PLAY and FILE still stop it. There should be no stuck blue error state.

After a useful run, stop with PLAY, release it, wait five seconds, power down and mount. The report should identify build 25 and Flip. Profiler starts include each successfully resumed direction segment, not only PLAY starts. Refill metrics combine forward/reverse streaming; the stopped refill gap is excluded from streaming-work metrics, so use a scope or listening for transition latency. Flip's fade-out rendering contributes envelope samples. A later stress playlist can use AGNT2.wav for the established 96 kHz/24-bit maximum-pitch case.


## Candidate resources and software checks

Build 25 uses 32016 bytes of combined code/constants normally (48948 free) and 36664 with profiling (44300 free). Static RAM is 55920/57248 bytes, 208 bytes above build 24. The 8 KiB minimum stack reserve is retained; profiling has 96 bytes of static growth budget beyond it. The two-sector boundary cache uses existing scratch space, not an extra 1 KiB static allocation.

The 18-test release suite and final focused checks pass. Sanitizer fixtures cover contiguous/fragmented FAT16/FAT32 files, cycles/truncation, sample order and wraps, scratch-cache ownership changes, warm reverse sector-read counts, PLAY/FILE/menu behaviour, gates during SD reads/refills, stationary fade DMA and card-error recovery. Actual compiled reverse PCM16/24 loops and relocated functions also run in the project's logical MIPS interpreter. These are software checks; Flip sound, timing, rapid-clock behaviour and long fragmented-file performance still need hardware observations.


## First hardware result — 2026-11-17

The user reported Flip working very well. A fresh CRC-valid build-25 report recorded 7 streaming starts/resumes and 30.829 seconds of observed playback in Flip/Quantize, PCM16/48000 Hz. Worst-work and tightest-margin contexts both used divider 1335, approximately 71910 output frames/s. Ring, late-commit, stationary-DMA, read and copy-overlap error counters were all zero, as were previous report-write failures.

Completed refill work averaged 5.301 ms and peaked at 7.548 ms, against approximately 14.240 ms per half-buffer. Minimum observed margin was 1012 frames, about 14.073 ms. Six fade-out envelope measurements each took about 197.88 us. These starts can include PLAY restarts and direction resumes; the report is not an edge-by-edge direction trace. It does not measure the stopped refill gap or establish transition latency.

SD attempts averaged 1.153 ms with a 3.766 ms maximum; 7591 attempts were below 1 ms and 1080 between 2 and 5 ms. The existing buffers accommodated these delays. Longest observed service gap was 649 us. Stack watermark was 1704 bytes with 6576 untouched above the 57248-byte static allocation; this remains a sampled lower bound. This validates the reported chirp workload and user-observed flips, not maximum-pitch 96 kHz/24-bit performance or arbitrary rapid clocks/fragmentation. The next stress case uses AGNT2 via build/flip-test/playlist-stress.txt with the same firmware/config.


### Longer stress material

Card inspection found NOISELONG.wav to be mono PCM24/96000 Hz, approximately 11.92 seconds at native speed and 7.95 seconds at maximum quantized pitch. It is a longer alternative to AGNT2 (approximately 1.82 seconds at that setting). The local build/flip-test/playlist-stress.txt and playlist-long.txt now select NOISELONG.wav. The card's W9624 tone is longer still, but its steady pitch is less useful for hearing direction changes. No user audio is copied into this source distribution.


### 96 kHz/24-bit hardware result

The user reported the longer noise sample seemed to work, but was a poor listening choice. A fresh CRC-valid report recorded 18 starts/resumes and 40.615 seconds in Flip/Quantize PCM24/96000 Hz, with divider 667 in worst-work and tightest-margin contexts. All streaming errors and previous report-write failures were zero. Seventeen fade-out measurements were recorded. Completed refills averaged 6.096 ms (worst 7.335 ms), against approximately 7.115 ms per half, with 991 frames/6.885 ms minimum observed buffer margin. Read-ahead accommodated the occasional longer refill in this run.

SD attempts averaged 876.63 us and peaked at 1081.83 us. Longest service gap was 649 us; stack watermark remained 1704 bytes with 6576 untouched. This establishes the tested high-rate Flip workload, not arbitrary clock rates or fragmentation. Steady noise does not make direction audible, so it is insufficient for judging transition sound or latency. The generator now also creates FLIP96.wav: an original 30-second mono PCM24/96000 Hz rising chirp, about 20 seconds at maximum quantized pitch, for listening at the same sample format.
