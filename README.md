# Hamburger Wavy

An unofficial sampler firmware for the Tiptop Audio ONE module, built from source for its PIC32MX170F256B. Eight playback modes, chromatic pitch quantization, random file selection and random loop seeking. Licensed under GPLv3; see [LICENSE](LICENSE) and [NOTICE](NOTICE).

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

See [the user guide](docs/user-guide.md) for modes, configuration, playlists, SD writes and quantization. Examples are in `examples/config/`.

## Validation status

The source snapshot is build 15. Its firmware output matches the private development build 15 byte-for-byte with the tested toolchain. Earlier hardware trials confirmed basic playback, quantization, random sequencing, mode feedback and the Fade read-ahead improvement. Build 14's click protection and build 15's build-record writing have software regression coverage but await hardware confirmation. This is not a claim of testing every combination of file, mode, card and sample rate.

Hamburger Wavy is independent of Tiptop Audio and is not endorsed by the manufacturer. Hardware/product names identify compatibility.
