# Filter and wah

The filter is the second effect stage in the extended build (`make extended` / `make extended-profile`), after [Colour](colour.md) and before the [Phaser](phaser.md) and [tremolo/ring modulation](mod.md). Legacy build 21 does not include it.

## What it does

A two-pole (12 dB/octave) state-variable filter with adjustable resonance and a choice of three outputs.

- **Low-pass** removes treble above the cutoff. It can soften crush aliasing and drive harmonics from the colour stage.
- **High-pass** removes everything below the cutoff: digit 1 takes off only the lowest bass, 9 leaves a thin top end.
- **Band-pass** keeps a region around the cutoff. Without resonance it is broad and slightly quieter than the input; resonance narrows and raises it.
- **Cutoff** spans about 6.3 octaves, from roughly 1/500 to 1/6.3 of the playback rate: about 95 Hz to 7.6 kHz at 48 kHz output. Because pitch is set by the playback clock, the cutoff tracks pitch like a tape machine: pitching a sample down an octave lowers the cutoff an octave.
- **Resonance** runs from none (maximally flat) to a strong peak of roughly +16 dB at the cutoff. It does not self-oscillate. The input is trimmed by up to 6 dB as resonance rises; loud material can still clip at the peak.
- The brightest low-pass setting is not fully open: there is no bypass at the top of the range.

## config.txt

Add lines after the CV mode line (and the optional delay line). Each takes a single digit:

```text
# Hamburger Wavy configuration
Loop
Quantize
Short
Filter 5
Resonance 6
```

| Line | Meaning |
| --- | --- |
| `Filter n` | Low-pass cutoff. 0 (or no line) is off; 1 is darkest and 9 brightest. `Lowpass n` is the same. |
| `Highpass n` | High-pass cutoff, 1 lowest to 9 highest. |
| `Bandpass n` | Band-pass centre, 1 lowest to 9 highest. |
| `Resonance n` | 0 to 9, for whichever shape is active. No effect without a filter. |
| `Wah n` | Depth of an automatic cutoff sweep; see below. |

Only one shape applies: the last `Filter`, `Highpass` or `Bandpass` line in the file. As with the other effect lines, a mistyped line is silently ignored.

Examples: [filter.txt](../examples/config/filter.txt), [highpass.txt](../examples/config/highpass.txt), [filter-cv.txt](../examples/config/filter-cv.txt) and [bandpass-cv.txt](../examples/config/bandpass-cv.txt).

## Filter CV mode

Put `Filter` on the CV mode line. Playback stays at the file's native pitch and the pitch knob/CV sets the cutoff of whichever shape the config names: clockwise or positive CV is higher. The digit on the cutoff line is then ignored, though the line still chooses the shape (`Highpass 0` works).

```text
# Hamburger Wavy configuration
Loop
Filter
Bandpass 5
Resonance 6
```

- This mode is reachable from config.txt only; choosing a mode in the held-PLAY pitch menu leaves it until the card is next mounted.
- In CV Flip, Tape and Chop the CV input controls transport, so the configured cutoff digit is used.
- The cutoff is read once per 1024-frame block and ramps across that block, so sweeps are smooth but arrive one to two blocks late (roughly 20–45 ms at 48 kHz).

## Wah

`Wah n` makes a slow sine move the cutoff up and down around its set value. It adds no processing stage, so it costs nothing beyond the filter and still runs wherever the filter does.

```text
# Hamburger Wavy: slow wah
Loop
Quantize
Short
Bandpass 5
Resonance 6
Wah 6
Rate 4
```

- **Depth.** `Wah 0` (or no line) is off; 9 swings the cutoff about 2.5 octaves each way. The swing stops at the ends of the cutoff range, so a centre near either end gives a lopsided sweep.
- **Centre and shape** come from the `Filter`, `Highpass` or `Bandpass` line. With no such line the wah uses a low-pass centred mid-range.
- **Speed** comes from `Rate n`: 0.125, 0.2, 0.31, 0.5, 0.79, 1.3, 2, 3.2, 5 and 8 Hz for digits 0 to 9. This scale is shared with the phaser and is separate from the tremolo's; if `Tremolo` or `Ring` lines are also present, each follows the same `Rate` value on its own scale.
- **Rate CV mode** (`Rate` on the CV line) puts the sweep speed on the knob/CV, clockwise faster.
- **Filter CV mode** puts the centre on the knob/CV while the sweep runs at the fixed `Rate`.
- The sweep restarts from the centre, rising, each time playback starts and after each Flip/Tape/Chop transition.
- The cutoff moves in straight lines between block ends (about 21 ms at 48 kHz), so the fastest sweeps are slightly angular.

Examples: [wah.txt](../examples/config/wah.txt) and [wah-cv.txt](../examples/config/wah-cv.txt).

## Limits

- The filter is skipped above 96 kHz output, and between about 64 and 96 kHz it is the only stage that runs: see [Limits](user-guide.md#limits) in the user guide. This includes 88.2/96 kHz files at native pitch.
- Crossing a limit while playing switches the filter off or on at a block boundary. On a steady tone through a resonant band-pass that is a clear click.
- At the darkest settings integer truncation leaves a DC error below −90 dBFS.
