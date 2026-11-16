# Using Hamburger Wavy

## Card and audio files

Use FAT16 or FAT32, with 512-byte sectors, either directly on the card or in an MBR partition. Audio must be mono PCM WAV, 16 or 24 bit, with a header sample rate of 8–96 kHz. Native clock families are 44.1/48/88.2/96 kHz; other accepted header rates use the 48 kHz family. Stereo, floating-point/compressed audio, exFAT, FAT12 and subdirectory browsing are unsupported.

Up to 256 files or playlist entries are supported. Names up to 13 ASCII characters including `.wav` work through checked short/single-entry VFAT names. Longer names may be accessible through their short aliases.

## Playback modes

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

Fade/Gate ramps are short 256-sample linear envelopes: about 5.3 ms at 48 kHz before pitch changes. They mainly suppress clicks, rather than providing long musical fades. Buffering adds release/retrigger latency. Random seeking and file changes require SD access and can take longer than cached Trigger playback. Overlapping voices are not implemented.

## CV modes

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

Line 1 starts with `#`; line 2 is a playback mode; line 3 is a CV mode. Optional line 4 is Select File's settling delay: Short=0 ms, Medium=2 ms, Long=10 ms. Further lines are ignored. `Random` is an alias for Random Sequence; `Looping` aliases Loop and `Quantized` aliases Quantize. Earlier card spellings remain compatibility aliases in the parser.

Case differences, surrounding setting whitespace, CRLF/LF and a UTF-8 BOM are accepted. Missing or malformed settings use Trigger/Quantize/Short. An actual read error invokes card recovery. Config files must fit within 4096 bytes.

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
