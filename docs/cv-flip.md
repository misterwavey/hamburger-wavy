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

Use the existing long rising `FLIP96.wav` chirp and a playlist containing only that file. Test PLAY stop/start, both DC polarities, zero stability, slow LFO, then faster LFO. Stop with PLAY, wait five seconds, power down and mount the card to collect `ONEPROF.TXT`. This build needs its first hardware run. Restore the staged build-28 rollback image if required.
