# Tape: signed playback speed

Tape is mode 11 in the extended firmware. PLAY starts/stops a looping sample. Positive CV plays forward; negative CV plays backward. Distance from the calibrated zero sets speed. Near zero it fades to silence, holds the position captured when rest was requested, and resumes from there when CV moves away. GATE is ignored; FILE stops and selects another sample.

## Calibrate and use

Set the knob near centre and patch steady 0 V. Hold PLAY for at least half a second without moving either control, then release. Tap PLAY to arm playback. At zero, the button is red but there is no sound; moving CV away starts the sample. PLAY stops both audible playback and an armed zero hold.

Use +0.5 V and −0.5 V first. On the measured module these produce approximately half native speed in opposite directions. About ±1 V gives native speed, with a maximum of 1.5× at approximately ±1.5 V. These are approximate relationships inferred from the measured ADC response, not calibrated 1 V/octave tracking. The speed law is linear in voltage magnitude. The existing Free/Quantize/Select File settings do not affect Tape.

Entering rest requires magnitude below five raw counts (approximately 0.05 V); leaving rest requires eight counts (approximately 0.08 V). Between these thresholds, the previous rest/moving state is retained. ADC resolution can make very slow sweeps sound stepped. The knob offsets zero: moving it after calibration gives manual tape-stop/reversal gestures. Recalibrate to restore the chosen reference. Calibration is RAM-only and should be repeated each power session.

Transitions retain the 512-frame fades and stopped refill gap from Flip. They are scheduled in output frames, so slowing playback also lengthens control response and fades. Near rest this can be hundreds of milliseconds. This is not continuous overlapping crossfade or seamless scrubbing. The position held is the playhead captured on requesting rest, not the later point at which the fade finishes.

## Configuration

```text
# Hamburger Wavy
Tape
Quantize
Short
```

The third line remains required, although pitch/file CV modes are overridden. The usual held-PLAY/FILE menu selects mode 11. Keep CV steady during calibration to avoid entering CV-mode editing.

## Trying it

Use a long rising sweep or a recognisable recording. Calibrate, tap PLAY at zero, move positive to hear forward playback, slowly return to zero and pause, then move negative to resume backwards. Change magnitude on one side to check speed without restarting. Try both knob and external CV gestures.
