# Hamburger Wavy

THIS CUSTOM FIRMWARE FOR THE DISCONTINUED TIPTOP AUDIO ONE EURORACK MODULE IS NOT AFFILIATED WITH TIPTOP AUDIO IN ANY WAY. IF YOU USE THIS FIRMWARE, DO NOT APPROACH TIPTOP AUDIO FOR SUPPORT. THEY ARE AN AWESOME COMPANY AND DON'T NEED ANY HEADACHES CAUSED BY THIS PROJECT.

This is an unofficial sampler firmware for the Tiptop Audio ONE module, built from source for its PIC32MX170F256B. Eight playback modes, chromatic pitch quantization, random file selection and random loop seeking. Licensed under GPLv3; see [LICENSE](LICENSE) and [NOTICE](NOTICE).

This repository contains the application, build tools, tests and documentation. No vendor firmware, boot/debug stub, extracted pitch tables or sample library is supplied. The module's existing updater is required. See [provenance](docs/provenance.md) for what was learned through reverse engineering and what is included.

## Build

You need Python 3.11 or newer, LLVM Clang with the MIPS backend, and LLD with ELF/MIPS support. No Python packages, proprietary SDK, XC32 compiler, vendor HEX, libc or runtime library is required. `make` is convenient but optional.

```sh
make
```

Output is `build/image.hex`, with an ELF, generated pitch tables and a build manifest alongside it. Nothing is copied to a card automatically. The builder rejects unresolved symbols, unexpected allocated sections, out-of-range code/RAM, boot-debug payload and changed hardware configuration words.

On macOS, install LLVM and LLD with Homebrew if needed. Apple's bundled Clang alone generally lacks the required target. On other systems, use an LLVM distribution with the MIPS target. Explicit tool paths are supported:

```sh
python3 tools/build.py --cc /path/to/clang --ld /path/to/ld.lld
```

The builder also accepts Rust's bundled `rust-lld` when available. To pass explicit paths through make:

```sh
make BUILD_FLAGS='--cc /path/to/clang --ld /path/to/ld.lld'
```

## Test

```sh
make test
```

Tests validate the HEX/ELF address contract, generated pitch mathematics, a rebuild in an isolated source tree, and the host C suite under AddressSanitizer/UndefinedBehaviorSanitizer when a suitable host compiler is available. These tests do not emulate the complete device or certify recovery from an interrupted update.

Optional original 440 Hz test WAVs can be generated with `python3 tools/generate_test_tones.py`. No audio files are included in the repository.

## Measure hardware headroom

`make profile` builds an optional diagnostic at `build/profile/image.hex`, leaving the normal image separate. It measures streaming/refill timing, observed buffer margin and stack watermark, then writes `ONEPROF.TXT` only after playback stops. See [profiling instructions and limitations](docs/profiling.md), and analyse a collected report with `python3 tools/analyze_profile.py ONEPROF.TXT`.

## Install

This is experimental replacement firmware. Preserve your existing firmware and card files separately. The application does not include the resident updater or a full recovery image. See [hardware and update limits](docs/provenance.md#hardware-and-update-limits).

Prepare a FAT16/FAT32 sample card with supported mono PCM16/24 WAVs. Copy the built image to its root as `image.hex`, verify it, then eject. Example for a macOS volume named TTA ONE:

```sh
cp build/image.hex "/Volumes/TTA ONE/image.hex"
sync
cmp build/image.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Use the [manufacturer's update procedure](https://www.tiptopaudio.com/manuals/Tiptop_Audio_one.pdf): power down, insert the card, hold PLAY while powering up, and allow the updater's rapid flashing to complete. The application then starts. Do not interrupt updating.

At startup, the PLAY button lights blue for one second. The firmware writes its build number to `ONEBUILD.TXT` on the SD card, which you can read on your computer to check which version is installed. If the file cannot be written, playback can still start.

## Use

PLAY taps trigger; FILE browses. Hold PLAY for at least 400 ms before turning FILE to edit the playback mode, or moving pitch to edit the CV mode. Stop turning and keep PLAY held to read complete red/blue pulse groups; release to apply. A card's `config.txt` is reloaded at each mount.

See [the user guide](docs/user-guide.md) for modes, configuration, playlists, SD writes and quantization. Examples are in `examples/config/`. The [bench guide](docs/bench.md) prepares Fade/Gate profiling and normal-build playback/retrigger checks.

## Validation status

Current build 21 offers normal and optional profiling images. It retains build 20's 1024-frame audio halves and specialised PCM decoding, and overlaps SD CRC calculation with SPI reception after build 20 still missed high-pitch refill deadlines. A profiling run sustained 152.7 seconds of maximum-pitch 96 kHz/24-bit looping with zero recorded errors; the larger buffers use more RAM and can increase loop-trigger preparation and envelope/retrigger scheduling latency. The [profiling guide](docs/profiling.md) records measurements and testing instructions. Earlier user trials cover playback, quantization, random sequencing, mode feedback and Fade read-ahead. Subsequent maximum-pitch Fade and manual Gate profiling runs also recorded zero errors. A complete mode/card/rate matrix has not been reported. See [provenance](docs/provenance.md) for the initial source-export verification.

Hamburger Wavy is independent of Tiptop Audio and is not endorsed by the manufacturer. Hardware/product names identify compatibility.

An optional [flash-layout experiment](docs/flash-layout.md) builds a separate build-22 image to test one extra application page. Its first hardware trial verified and executed that page, and rollback to build 21 also succeeded. Standard builds remain build 21.

`make flash-bank` prepares the next separate experiment, build 23, to verify a 48 KiB extension bank and execute code near both ends. Its first hardware trial verified the full bank and executed both endpoint routines; rollback to build 21 also succeeded after this larger trial. See the flash-layout guide.

`make extended` / `make extended-profile` build the separate build-24 candidate with real application code/constants in the verified bank. A 95.2-second maximum-pitch profiling run recorded no errors and matched build-21 refill timing; envelope and normal-build regressions remain pending. The flash-layout guide gives resource figures. See [reverse playback feasibility](docs/reverse-playback.md) for the sample-reading and RAM work that feature would require.
