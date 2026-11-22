# Phaser

The phaser is an effect stage in the extended build (`make extended` / `make extended-profile`). It runs after [Colour](colour.md) and the [Filter](filter.md) and before [tremolo/ring modulation](mod.md). A [Flanger](flanger.md) can be used in its place. Legacy build 21 does not include it.

## What it does

Four all-pass stages shift the phase of the signal, and the result is mixed with the dry signal so that two notches appear in the spectrum, with a peak between them. A slow sine moves the notches up and down by about two octaves each way. Some of the output is fed back to the input, which sharpens the peak.

- At 48 kHz output the centre of the sweep is about 750 Hz, moving between roughly 190 Hz and 2.9 kHz (the all-pass turnover; the notches sit at about 0.4 and 2.4 times that). Like the filter, these positions follow the playback pitch.
- The sweep restarts from its centre, rising, whenever playback starts and after each Flip/Tape/Chop transition.
- Silence stays silent. The input is trimmed by up to 4.4 dB as intensity rises, because the peak between the notches reaches about +1.6 dB at full intensity; very loud material can still clip there.

## config.txt

```text
# Hamburger Wavy: phaser
Loop
Quantize
Short
Phaser 7
Rate 3
```

- `Phaser n`: intensity, 0 (off) to 9. Higher values deepen the notches and raise the feedback together; at 9 the notches are complete.
- `Rate n`: sweep speed on the same slow scale as `Wah`: 0.125, 0.2, 0.31, 0.5, 0.79, 1.3, 2, 3.2, 5 and 8 Hz for 0 to 9. If `Wah`, `Tremolo` or `Ring` lines are also present they follow the same `Rate` value on their own scales, and a wah and the phaser move together.
- **Rate CV mode** (`Rate` on the CV line) puts the sweep speed on the knob/CV, clockwise faster. With a `Phaser` line and no `Tremolo`/`Ring` line, that mode adds no tremolo.

Examples: [phaser.txt](../examples/config/phaser.txt) and [phaser-cv.txt](../examples/config/phaser-cv.txt).

## Limits

With four stages the rule for how many run depends on the output rate (about 96,000,000 divided by the pitch divider):

| Output rate | Stages that run |
| --- | --- |
| Up to 48 kHz | All four |
| 48 kHz to about 64 kHz | Three: colour gives way first, then tremolo/ring |
| About 64 kHz to 96 kHz | One: the filter if on, otherwise the phaser, otherwise tremolo/ring, otherwise colour |
| Above 96 kHz | None |

A 44.1 or 48 kHz file at native pitch or below gets everything. Crossing a limit while playing switches a stage at a block boundary, audibly.
