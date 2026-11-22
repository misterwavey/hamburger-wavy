# Using Hamburger Wavy

## Card and audio files

Use FAT16 or FAT32, with 512-byte sectors, either directly on the card or in an MBR partition. Audio must be mono PCM WAV, 16 or 24 bit, with a header sample rate of 8–96 kHz. Native clock families are 44.1/48/88.2/96 kHz; other accepted header rates use the 48 kHz family. Stereo, floating-point/compressed audio, exFAT, FAT12 and subdirectory browsing are unsupported.

Up to 256 files or playlist entries are supported. Names up to 13 ASCII characters including `.wav` work through checked short/single-entry VFAT names. Longer names may be accessible through their short aliases.

## Playback modes

Extended build 53 provides modes 1–12 and the effects described under [Effects](#effects): drive/crush, a resonant filter with wah, a phaser or flanger, and tremolo/ring modulation. Build with `make extended` (normal) or `make extended-profile` (profiling). The retained legacy build-21 targets (`make` and `make profile`) expose modes 1–8.

| Red count | Mode | Behavior |
| --- | --- | --- |
| 1 | Trigger | Each PLAY tap or gate trigger plays from the beginning once. |
| 2 | Gate | Plays while external gate is held, fading on release. PLAY taps toggle manual playback. |
| 3 | Fade | Retriggers fade out the current sound, then restart with a fade-in. |
| 4 | Loop | Repeats the selected file; triggers restart it. Browsing stops it. |
| 5 | Sequence | Advances one file before playing on each trigger. |
| 6 | Combo | Advances one file on each trigger, then loops it. |
| 7 | Random Sequence | Chooses a random file per trigger; repeats are allowed. |
| 8 | Random Loop | Loops a file; subsequent triggers jump to random frame positions within it. |
| 9 | Flip | PLAY starts/stops; GATE rising edges reverse from the captured playback position. See [Flip](flip.md) for transitions and testing. |
| 10 | CV Flip | PLAY starts/stops; positive CV selects forward and negative selects reverse at native speed. Near zero, keeps its last direction. GATE is ignored. Calibrate first; see below. |
| 11 | Tape | Signed CV controls direction and speed; near zero fades to silence and holds position. PLAY starts/stops; GATE ignored. Calibrate as for CV Flip. See [Tape](tape.md). |
| 12 | Chop | Each gate jumps to the CV-selected position with a short fade, then plays forward at native speed. PLAY starts/stops. See [Chop](chop.md). |

Fade/Gate ramps are short 256-sample linear envelopes: about 5.3 ms at 48 kHz before pitch changes. They mainly suppress clicks, rather than providing long musical fades. Buffering adds release/retrigger latency. Random seeking and file changes require SD access and can take longer than cached Trigger playback. Overlapping voices are not implemented.

Flip, CV Flip, Tape and Chop use 512-frame transition fades. These reduce direction-change clicks; they are not overlapping crossfades, and buffering/refill adds response delay.

## CV Flip and Tape calibration

Select mode 10 (`CV Flip`) or 11 (`Tape`), or put that name on line 2 of `config.txt`. Examples: [cv-flip.txt](../examples/config/cv-flip.txt) and [tape.txt](../examples/config/tape.txt).

1. Centre the pitch knob and patch a steady 0 V from your bipolar CV source.
2. Hold PLAY for at least half a second without turning either control, then release. Playback stops and the current input becomes the zero reference.
3. Tap PLAY to start. Positive CV plays forward and negative plays reverse. CV Flip retains the last direction near zero; Tape fades to silence and holds position there.

A reverse start begins at the end of the sample. Calibration survives file selection but resets on power-up, so repeat it each session. Keep the knob at its calibrated position, or move it deliberately to bias the switching threshold. Keep CV steady during calibration: large movement while PLAY is held can enter CV-mode editing instead.

CV Flip always uses native sample speed and overrides Free/Quantize/Select File behaviour. The third config line is still required, but does not change that behaviour. FILE browsing stops playback; GATE is ignored. See [CV Flip](cv-flip.md) for details. [Tape](tape.md) adds signed variable speed and a zero hold. [Chop](chop.md) samples CV position on each gate. These are separate playback modes, not CV-mode options.

## CV modes

These apply to playback modes 1–9. CV Flip, Tape and Chop override them with their dedicated controls.

| Blue count | Mode | Behavior |
| --- | --- | --- |
| 1 | Free | Continuous playback pitch/speed control. |
| 2 | Quantize | Chromatic semitone steps using mathematically generated tables. |
| 3 | Select File | Samples pitch/CV at each trigger to select an offset above FILE's base selection; plays at nominal native rate. |

Select File spans up to 32 offsets, wraps the file list, and takes precedence over automatic Sequence/random selection. CV alone does not retrigger or loop playback in Trigger mode. An LFO at GATE can retrigger on each rising edge.

Quantize provides 49 notes from +7 through −41 semitones. Software input is `value = ADC >> 2`, clamped to 0–255; the note is `7 - floor((value*48 + 127)/255)`. Increasing ADC lowers pitch; the physical voltage/control relationship is not calibrated. Original pitch occupies ADC 140–159, away from the control's centre. Free mode uses a separate continuous mapping.

The divider for note `n` is `round(native_divider * 2**(-n/12))`, with half values rounded upward. Nominal anchors are 2176, 2000, 1088 and 1000 for 44.1, 48, 88.2 and 96 kHz. The hardware divider is encoded in steps of 1/512. Maximum rounding error relative to those anchors is about 1.09 cents. Oscillator error, approximate anchors and voltage calibration are additional; calibrated 1 V/octave tracking is not claimed. Runtime floating point is unnecessary.

## Panel editing

Hold PLAY for at least 400 ms before moving a control to edit modes. Earlier FILE movement remains ordinary browsing and releasing PLAY remains a playback click.

Turn FILE for playback mode. After the last detent, keep PLAY held and pause 350 ms: the complete red count starts from one. Pulses are 200 ms on/off, with at least a one-second gap between groups. Release PLAY to apply. Once FILE has moved during an editing hold, CV/LFO movement cannot change CV mode or take over the display; any provisional CV edit in that hold is discarded.

To edit CV mode, release and hold PLAY again, then move pitch without turning FILE. Low ADC selects Free, centre selects Quantize, high selects Select File, with intervening dead bands. Blue counts identify the selection. Power cycling or remounting reloads the card configuration.

## config.txt

Plain text in the card root, for example:

```text
# Hamburger Wavy configuration
Trigger
Quantize
Short
```

Line 1 starts with `#`; line 2 is a playback mode; line 3 is a CV mode. Optional line 4 is Select File's settling delay: Short=0 ms, Medium=2 ms, Long=10 ms. In the extended build, effect lines follow (see [Effects](#effects)) and `Colour`, `Filter` or `Rate` are also accepted as the CV mode. Other further lines are ignored. `Random` is an alias for Random Sequence; `Looping` aliases Loop and `Quantized` aliases Quantize. Earlier card spellings remain compatibility aliases in the parser.

Case differences, surrounding setting whitespace, CRLF/LF and a UTF-8 BOM are accepted. Missing or malformed settings use Trigger/Quantize/Short. An actual read error invokes card recovery. Config files must fit within 4096 bytes.

## Effects

Extended build 53 only. Effects are set by extra lines in `config.txt`, after the CV mode line and the optional delay line. Each line is a name and a single digit, 0 (off) to 9. There is no panel control for them; change the file and remount. A mistyped effect line is silently ignored, and any line longer than 63 characters invalidates the whole file.

```text
# Hamburger Wavy configuration
Loop
Quantize
Short
Bandpass 5
Resonance 6
Wah 6
Rate 4
```

| Line | What it does | Details |
| --- | --- | --- |
| `Drive n` | Gain into a smooth clipper, 1× to about 16× | [Colour](colour.md) |
| `Crush n` | Fewer bits (16 down to 4) and a sample hold of 1 to 32 frames | [Colour](colour.md) |
| `Filter n` | Low-pass cutoff, 1 darkest to 9 brightest (`Lowpass n` is the same) | [Filter](filter.md) |
| `Highpass n` | High-pass cutoff, 1 removes only the lowest bass, 9 leaves a thin top | [Filter](filter.md) |
| `Bandpass n` | Band-pass centre, low to high | [Filter](filter.md) |
| `Resonance n` | Emphasis at the cutoff for any of the three filter shapes | [Filter](filter.md) |
| `Wah n` | A slow sine sweeps the filter cutoff, up to about 2.5 octaves each way | [Filter](filter.md) |
| `Phaser n` | Swept notches with feedback; intensity | [Phaser](phaser.md) |
| `Flanger n` | Swept short delay with feedback, the jet sound; intensity. Replaces the phaser if both are present | [Flanger](flanger.md) |
| `Tremolo n` | Level dips at the `Rate`; depth, 9 reaches silence | [Tremolo and ring modulation](mod.md) |
| `Ring n` | Multiplies the sound by a sine at the `Rate`; depth. Replaces tremolo if both are present | [Tremolo and ring modulation](mod.md) |
| `Rate n` | Speed for wah, phaser, flanger, tremolo and ring, each on its own scale | below |

Only one of `Filter`, `Highpass` and `Bandpass` applies: the last one in the file. The signal passes through drive and crush, then the filter, the phaser or flanger, and tremolo or ring modulation, before the existing fades.

`Rate n` scales (calculated, at the file's native pitch):

| `Rate` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Wah, phaser, flanger (Hz) | 0.125 | 0.2 | 0.31 | 0.5 | 0.79 | 1.3 | 2 | 3.2 | 5 | 8 |
| Tremolo (Hz) | 1 | 1.4 | 1.9 | 2.7 | 3.5 | 4.8 | 6.7 | 8.8 | 12 | 16 |
| Ring (Hz) | 16 | 30 | 56 | 107 | 197 | 363 | 682 | 1236 | 2216 | 4096 |

### Effect CV modes

Put one of these on the CV mode line (line 3) to control an effect from the pitch knob/CV. Playback then stays at the file's native pitch, and clockwise or positive CV gives more. They can only be chosen in `config.txt`; picking a CV mode in the held-PLAY menu leaves them until the card is next mounted.

| CV mode | Knob/CV controls |
| --- | --- |
| `Colour` | Amount of drive and crush, from clean up to the configured digits (both to 9 if neither line is present) |
| `Filter` | Cutoff of whichever filter shape is named; with `Wah`, the centre of the sweep |
| `Rate` | Speed of the wah, phaser, flanger, tremolo or ring modulation. With none of those lines present it gives full-depth tremolo |

In CV Flip, Tape and Chop the CV input controls the transport, so the effects use their fixed config values.

### Limits

How many effect stages run depends on the output rate, which rises as a file is pitched up. Drive and crush count as one stage; the filter (with or without wah), the phaser or flanger, and tremolo/ring are one each.

| Output rate | Stages that run |
| --- | --- |
| Up to 48 kHz | All four |
| 48 kHz to about 64 kHz | Three: drive/crush gives way first, then tremolo/ring |
| About 64 kHz to 96 kHz | One: the filter if on, otherwise the phaser or flanger, otherwise tremolo/ring, otherwise drive/crush |
| Above 96 kHz | None |

A 44.1 or 48 kHz file at native pitch or below gets everything; an 88.2 or 96 kHz file at native pitch gets one stage. Crossing a limit while playing switches a stage on or off with an audible step, most obvious on steady tones.

Filter cutoff, phaser and flanger positions and all rates are tied to the playback clock, so they move with pitch: pitching a file down an octave lowers the cutoff an octave and halves the rates.

Example files for each effect are in [examples/config](../examples/config).

## playlist.txt

An optional root file lists one WAV filename per line and replaces directory order. Duplicate entries are allowed and weight random selection. The first missing/invalid name ends the usable prefix; card read errors abort mounting. An empty usable list does not play. For a single-tone test, a playlist may simply contain:

```text
Q480.wav
```

Generate that original test tone with `python3 tools/generate_test_tones.py` and copy it from `build/test-tones/` to the card. Delete `playlist.txt` to restore directory browsing.

## Persistent files and lights

WAV, config and playlist files remain read-only. Two reserved root files can be created/updated, with audio stopped:

- `ONESTATE.BIN`: 1024 bytes, two alternating CRC-protected records remembering the last manually played file. External gate triggers do not save it. Old `STATUS.BIN` recall is imported read-only.
- `ONEBUILD.TXT`: 512-byte space-padded plain text identifying the running build. An already-correct record is not rewritten. It identifies the last firmware that successfully wrote the card, not an image merely copied from a computer. Older firmware without this feature can leave the record stale.

First creation also updates allocated FAT entries, a root directory entry and applicable FAT32 FSInfo hints. Existing unrelated/read-only same-name files are refused. Build logging is best effort; a failed write does not deliberately block playback. Interrupted metadata creation can leak a cluster; this is not transactional FAT storage.

Startup: one second blue, then no build count. Red indicates playback. Solid blue indicates a playback error, including a missed refill deadline; flashing blue can indicate card recovery or stationary DMA. These signals do not uniquely diagnose the underlying fault. PLAY retries after a playback error reopen/re-prime the selected file.

The optional diagnostic build adds `ONEPROF.TXT` reporting after playback stops. Normal builds do not create or update it. See [profiling](profiling.md) for collection instructions and the additional reserved write scope.
