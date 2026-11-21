# Tremolo and ring modulation

Tremolo and ring modulation are the last effect stage in the extended build (`make extended` / `make extended-profile`), after [Colour](colour.md), the [Filter](filter.md) and the [Phaser](phaser.md). Legacy build 21 does not include them.

## What it does

One sine oscillator multiplies the signal.

- **Tremolo** swings the level between full and `1 - depth`: at depth 9 it dips to silence once per cycle. Rates run from 1 to 16 Hz.
- **Ring modulation** crossfades from the dry signal to the signal multiplied by the sine: at depth 9 only the product remains, giving sum and difference tones. Rates run from 16 Hz to about 4.1 kHz.

The gain never exceeds one, so neither can clip, and silence stays silent. The stage runs last, after drive, crush, the filter and the phaser, and before the existing envelopes and Flip fades. The oscillator restarts at its peak whenever playback starts, so tremolo begins at full level. Its sine is a corrected parabola, within about 0.3% of an ideal sine.

Because pitch is set by the playback clock, the oscillator runs in the sample domain: the rates below hold at the file's native pitch and move with it when the file is pitched up or down.

## config.txt

Add lines after the CV mode line (and after the optional delay line, if present). Each takes a single digit:

```text
# Hamburger Wavy configuration
Loop
Quantize
Short
Tremolo 7
Rate 5
```

- `Tremolo n`: depth, 0 (off) to 9.
- `Ring n`: depth, 0 (off) to 9. If both lines are present, Ring is used and Tremolo is ignored.
- `Rate n`: 0 to 9, on the scale of whichever is active. Without a `Rate` line the rate is 0.

| `Rate` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Tremolo (Hz) | 1 | 1.4 | 1.9 | 2.7 | 3.5 | 4.8 | 6.7 | 8.8 | 12 | 16 |
| Ring (Hz) | 16 | 30 | 56 | 107 | 197 | 363 | 682 | 1236 | 2216 | 4096 |

These are calculated from the firmware's rate map. Examples: [tremolo.txt](../examples/config/tremolo.txt), [ring.txt](../examples/config/ring.txt) and [rate-cv.txt](../examples/config/rate-cv.txt).

As with the other effect lines, a mistyped line after line 4 is silently ignored, and any line longer than 63 characters invalidates the whole file.

## Rate CV mode

Put `Rate` on the CV mode line. Playback then stays at the file's native pitch and the pitch knob/CV sweeps the rate across the whole range: clockwise or positive CV is faster. With a `Ring n` line it sweeps ring modulation; otherwise tremolo, at the `Tremolo n` depth. If the file has no `Tremolo`, `Ring`, `Wah` or `Phaser` line, the mode gives full-depth tremolo. The reading is taken once per 1024-frame block (about 21 ms at 48 kHz) and smoothed: very small changes are ignored and larger ones settle in roughly a tenth of a second, so a chosen rate stays put.

In CV Flip, Tape and Chop the CV input belongs to the transport, so the rate stays at the fixed `Rate n` value.

With `Wah` or `Phaser` lines present, the same knob/CV also sets their sweep speed, on their own slower scale.

## Limits

The stage is skipped above 96 kHz output and gives way to the filter and phaser at high rates: see [Limits](user-guide.md#limits) in the user guide.
