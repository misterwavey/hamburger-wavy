# Colour: drive and crush

Colour is the first effect stage in the extended build (`make extended` / `make extended-profile`), ahead of the [Filter](filter.md), [Phaser](phaser.md) and [tremolo/ring modulation](mod.md). Legacy build 21 does not include it.

## What it does

- **Drive** applies 1× to about 16× gain into a smooth clipper (`y = 2x − x|x|`), followed by a level correction that falls quickly towards about −10 dB as drive rises. Mid-level material keeps roughly its loudness; quiet material still gets louder and full-scale material quieter, each by up to about 8 dB at maximum drive (calculated for sine input, not measured).
- **Crush** rounds samples to between 16 and 4 bits and holds each value for 1 to 32 frames. Low settings do little to 16-bit files.

Drive runs first, then crush, then the existing envelopes and Flip fades. Silence stays silent, so fades still end at zero. Because pitch is set by the playback clock, the crush hold period and its aliasing move with pitch.

## config.txt

Add either or both lines after the CV mode line (and after the optional delay line, if present). Each takes a single digit, 0 (off) to 9 (maximum):

```text
# Hamburger Wavy configuration
Loop
Quantize
Short
Drive 4
Crush 6
```

A line-4 entry that is neither a delay nor a valid colour line still invalidates the file, as before. Later unrecognised lines are still ignored, so a mistyped `Drive 12` on a later line silently leaves drive off.

## Colour CV mode

Put `Colour` (or `Color`) on the CV mode line. Playback then stays at the file's native pitch and the pitch knob/CV sets the amount: clockwise or positive CV gives more. The knob sweeps from clean to the configured `Drive`/`Crush` amounts; with neither line present both sweep to 9.

```text
# Hamburger Wavy configuration
Loop
Colour
```

- The mode is reachable from config.txt only. The held-PLAY pitch menu still offers Free/Quantize/Select File; choosing one of those leaves Colour CV until the card is next mounted.
- In CV Flip, Tape and Chop the CV input already controls transport, so the configured amounts are used unscaled.
- The amount is read once per 1024-frame block as it is fetched, so changes arrive in steps, one to two blocks later (roughly 20–45 ms at 48 kHz).

## Limits

- **Output rate.** Colour is skipped above 96 kHz output, and gives way to other stages at high rates: see [Limits](user-guide.md#limits) in the user guide. Crossing a limit while playing switches the effect off or on at a block boundary, with an audible level change if drive is high.
- **Start latency.** The 2048-frame primed buffer is processed before playback starts. Expect a few milliseconds extra per start or Flip resume when colour is active.
- Parameters do not ramp within a block.
