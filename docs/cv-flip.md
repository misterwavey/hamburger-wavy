# CV Flip — build 30

CV Flip is playback mode 10 in the extended application. It loops at the WAV's native speed; CV chooses direction from the current playhead, using the existing 512-frame fades. Positive CV selects forward, negative selects reverse. Around zero, it keeps its last direction. A short PLAY click starts/stops; FILE selects another sample and stops playback. GATE is ignored in this mode. Ordinary Flip remains mode 9 with gate-controlled direction and the existing pitch controls.

## Set up

Use `make extended-profile` for `build/extended/profile/image.hex`, or `make extended` for the normal image. Install with the usual updater procedure. Configuration:

```text
# Hamburger Wavy
CV Flip
Quantize
Short
```

The CV and delay settings remain valid config fields, but CV Flip overrides their pitch/file-selection behaviour: speed stays native and CV controls only direction. On the panel, hold PLAY for 400 ms before turning FILE to select mode 10 (ten red flashes). Release to apply.

## Calibrate and play

1. Set the pitch knob near centre and patch measured 0 V from your bipolar source.
2. Hold PLAY for at least half a second, without moving either control, then release. This stops playback and captures the zero reference. The usual mode-menu light feedback is retained.
3. Tap PLAY to start forward.
4. Apply +0.5 V: it should continue forward. Apply −0.5 V: it should fade into reverse from near the current position. Return to +0.5 V to turn forward again.
5. Try a slow bipolar LFO. Its polarity should change direction; its amplitude does not change playback speed.

A new reverse start begins at the last sample frame. Returning to zero holds the last direction rather than stopping. Changes during a fade coalesce to the most recently requested direction; rapid modulation need not produce one audible turn per crossing. Existing fade scheduling/refill gaps remain.

Calibration is held only in RAM, survives file selection, and resets on power-up to an approximate 327-count reference from the first bench measurements. Recalibrate each session. Use patched 0 V when calibrating; unplugged and patched-zero readings differed slightly on the tested hardware. Moving the knob afterwards shifts the switching threshold, which can deliberately bias an LFO's forward/reverse duty cycle. Recalibrate to restore the chosen zero.

The switching thresholds are five raw ADC counts either side of the captured zero (approximately ±0.05 V on the measured module). Values inside that band retain direction. An extreme knob position can clip the combined input and prevent one polarity from crossing the threshold; centre is the recommended starting point. The UI recognises large knob/CV movement during a hold as CV-mode editing, so keep 0 V steady throughout calibration.

## Hardware check

Use the existing long rising `FLIP96.wav` chirp and a playlist containing only that file. Test PLAY stop/start, both DC polarities, zero stability, slow LFO, then faster LFO. Stop with PLAY, wait five seconds, power down and mount the card to collect `ONEPROF.TXT`. Manual and LFO hardware runs are recorded below. Restore the staged build-28 rollback image if required.

## Roll back this trial

Restore both the build-28 image and the card settings saved before this trial, since build 28 does not recognise `CV Flip` in config.txt:

```sh
cp build/cv-flip-test/rollback-28.hex "/Volumes/TTA ONE/image.hex"
cp build/cv-flip-test/card-backup/config.txt "/Volumes/TTA ONE/config.txt"
cp build/cv-flip-test/card-backup/playlist.txt "/Volumes/TTA ONE/playlist.txt"
sync
cmp build/cv-flip-test/rollback-28.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Run the usual updater procedure again. The `card-backup` directory is a local bench backup, not part of the source distribution; when preparing your own trial, preserve your own config and playlist before changing them.

## First hardware run — manual CV changes

The user completed the trial using manual back-and-forth CV changes rather than an LFO. A fresh CRC-valid build-30 report recorded 12 starts/resumes and 85.121 seconds of CV Flip playback, PCM24/96000 Hz at native speed (divider 1000). Eleven fade preparations were recorded, each approximately 245.83 us. Starts/resumes and fade preparations are separate counters, not an edge-by-edge direction trace.

Ring, late-commit, stationary-DMA, read and copy-overlap errors were all zero, with no previous report-write failures. Completed refills averaged 6.048 ms and peaked at 9.484 ms, against approximately 10.667 ms per half-buffer. Minimum observed normal publication margin was 1007 frames (10.490 ms). The report excludes stopped refill gaps and does not establish transition latency or click audibility.

SD attempts averaged 877.32 us and peaked at 3.921 ms. Stack watermark was 1712 bytes with 6552 untouched above the 57264-byte static allocation; this is an observed lower bound. These results support the tested manual-CV workload. They do not yet validate LFO crossings, rapid modulation, every calibration/knob position, or arbitrary card fragmentation.

## Second hardware run — LFO trial

After the requested slow bipolar LFO trial, a new CRC-valid build-30 report recorded 52 starts/resumes and 29.622 seconds of CV Flip playback, PCM24/96000 Hz at native speed. Fifty-one fade preparations were measured at approximately 245.83 us each. All five streaming error counters and previous report-write failures were zero.

Completed refills averaged 5.990 ms and peaked at 9.424 ms, within the 10.667 ms half-buffer period. Minimum observed normal publication margin remained 1007 frames (10.490 ms). SD attempts averaged 845.95 us and peaked at 3.777 ms. Stack watermark remained 1712 bytes with 6552 untouched. The report differs from the manual trial and supports repeated automatic direction changes without recorded streaming faults. Exact LFO frequency/amplitude, chatter at zero, sound quality and transition latency were not separately measured; the report does not prove an audible turn for every input crossing or establish a maximum modulation rate.
