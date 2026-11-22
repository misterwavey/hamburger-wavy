# Flanger

The flanger is an effect stage in the extended build (`make extended` / `make extended-profile`). It takes the [Phaser](phaser.md)'s place in the chain, after [Colour](colour.md) and the [Filter](filter.md) and before [tremolo/ring modulation](mod.md): a config can have one or the other. Legacy build 21 does not include it.

## What it does

The signal is mixed with a copy of itself delayed by a very short, slowly changing time. That cuts a comb of evenly spaced notches into the spectrum, which sweep up and down as the delay changes: the classic jet sound. Some of the delayed signal is fed back, which sharpens the peaks between the notches.

- The delay sweeps between 32 and 384 frames: about 0.7 to 8 ms at 48 kHz output, half that at 96 kHz. Like the other effects it follows the playback pitch.
- The sweep restarts from its middle whenever playback starts and after each Flip/Tape/Chop transition, with an empty delay line.
- The delayed copy is kept at 16-bit resolution; the dry signal is untouched.
- Silence stays silent. The input is trimmed by up to 4.4 dB as intensity rises, because the peaks reach about +1.6 dB at full intensity; very loud material can still clip there.

## config.txt

```text
# Hamburger Wavy: flanger
Loop
Quantize
Short
Flanger 7
Rate 2
```

- `Flanger n`: intensity, 0 (off) to 9. Higher values deepen the notches and raise the feedback together; at 9 the notches are complete.
- `Rate n`: sweep speed on the slow scale shared with `Wah` and `Phaser`: 0.125, 0.2, 0.31, 0.5, 0.79, 1.3, 2, 3.2, 5 and 8 Hz for 0 to 9. Slow settings suit a flanger best.
- If both `Flanger` and `Phaser` lines are present, the flanger is used.
- **Rate CV mode** (`Rate` on the CV line) puts the sweep speed on the knob/CV, clockwise faster.

Examples: [flanger.txt](../examples/config/flanger.txt) and [flanger-cv.txt](../examples/config/flanger-cv.txt).

## Limits

The flanger counts as the same stage as the phaser in the [limits](user-guide.md#limits): it runs with everything up to 48 kHz output, stays on when stages are dropped up to about 64 kHz, and above that runs only if no filter is set. Crossing a limit while playing switches it at a block boundary, audibly.
