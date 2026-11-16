# Provenance and hardware limits

This is a source-built, reverse-engineered application, not a historical clean-room implementation. The private development project examined a manufacturer's update and device documentation. That development repository and its history are not part of this distribution.

## Included implementation

Application startup, peripheral access, SD SPI, FAT, WAV conversion, buffering, controls, configuration, persistence and playback policy are supplied as source. The encoder's seven-state full-step table and masked-state traversal derive from Ben Buxton's GPLv3 Rotary implementation. `firmware/app.c` carries the copyright, adaptation date and upstream reference; NOTICE describes the modification. The entire project is distributed under GPL-3.0-only, with the complete licence in LICENSE.

The previously transcribed pitch tables were removed. `tools/generate_pitch.py` calculates the distributed application's tables from equal temperament and nominal hardware clock anchors. It does not read an extracted table or vendor binary. Shared values at octave/clock ratios are a consequence of the mathematics and hardware settings.

The reset routine follows the PIC32's hardware-defined SYSKEY/software-reset mechanism. Interrupt/exception stubs are source-assembled jumps into this application's fault handling. There is no boot-flash debug handler in the supplied source, linker output or update records.

## Compatibility information

Register addresses, pin/PPS assignments, clock anchors, configuration words, file formats, supported behavior and application/update addresses were learned from hardware documentation and reverse engineering. The layout JSON contains only record headers/addresses and control records, not application instruction bytes. Hardware configuration words are emitted from source. These compatibility facts are retained and are not represented as newly invented hardware behavior.

The build is freestanding: it links the supplied object files directly, with no startup library, libc or proprietary SDK runtime. Undefined symbols and statically encoded function jumps outside the supplied payload are rejected. Such checks do not prove the absence of every possible indirect dependency or provide comprehensive third-party rights clearance.

## Excluded material

No vendor update, original device dump, disassembly, original boot/debug stub, extracted pitch table, vendor sample library, historical hybrid firmware or private Git history is included. Generated objects, HEX/ELF files, test audio and build logs are ignored by Git. The source can generate the application without any of these inputs.

## Hardware and update limits

The module must already have its resident updater. It is neither supplied nor reconstructed here. Its complete binary has not been obtained, and its erase/protection policy remains unknown. Update files omit the reset vector and boot-debug records and are not full-device recovery images. Source-level safeguards and successful updates do not guarantee recovery from corrupt firmware or power interruption.

The exported build-15 image matches its development counterpart byte-for-byte with the local toolchain: Homebrew Clang 23.1.5, Rust-bundled LLD 13.0.0 in GNU mode, and Python 3.15.2 on macOS. Different compiler/linker versions may produce different machine code. Qualitative user trials cover earlier playback/retrigger, quantization, random sequencing, mode feedback and Fade read-ahead. Build 14 click-overlap protection and build 15 build-record writing have software tests but no reported hardware confirmation yet. Fine tuning, every sample-rate/card combination, and precise latency remain unmeasured.

## Export verification — 2026-11-16

At the initial build-15 export, the source-only package passed five release tests, including an isolated rebuild and the C suite under AddressSanitizer/UndefinedBehaviorSanitizer (659375 checks with the local host compiler). The exported firmware's SHA-256 is `2019b6f340b8b49f9f75e6e3a4769d1b91d9b90e02b2a2f8d0bec4f9ad42b2ae`, identical to the private development build 15. This hash is a software-build observation, not a manufacturer checksum or hardware certification.

## References

- [Ben Buxton Rotary source](https://github.com/buxtronix/arduino/blob/master/libraries/Rotary/Rotary.cpp): copyright 2011 Ben Buxton, GNU GPL Version 3; attribution checked 2026-11-16.
- [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.html): full terms included locally in LICENSE.
- [ONE manual](https://www.tiptopaudio.com/manuals/Tiptop_Audio_one.pdf): hardware behavior, configuration and published SD update workflow.
- [PIC32MX1XX/2XX family data sheet](https://ww1.microchip.com/downloads/en/DeviceDoc/PIC32MX1XX2XX-28-36-44-PIN-DS60001168K.pdf): registers, memory layout, pins and reset behavior.
- [Microchip reference-clock documentation](https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/PIC32/mx-arch-cpu-overview/oscillator/refclk/): fractional clock divider.
- [SD simplified physical-layer specification](https://www.sdcard.org/cms/wp-content/themes/sdcard-org/dl.php?f=Part1_Physical_Layer_Simplified_Specification_Ver5.10.pdf): SPI protocol and CRC.
- [Microsoft FAT specification](https://www.microsoft.com/en-us/download/details.aspx?id=53426) and [RIFF description](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-): filesystem/container formats.

References are links, not bundled vendor documents or a grant of rights over those documents. Hardware/product names identify compatibility; the project is unofficial.

## Optional profiler — build 16

Normal build 16 is byte-for-byte identical to build 15 apart from its two embedded build-number constants (15 to 16). Its main text/constants remain 26996 bytes and static RAM 32160 bytes. The optional diagnostic uses 31500 main-text bytes and 33488 static-RAM bytes; main-text headroom is 312 bytes in the existing 31812-byte region. Its total additional source payload is 4544 bytes, including a 40-byte startup watermark initializer; static-RAM overhead is 1328 bytes. No update addresses/configuration words are added or changed.

Eight release tests pass, covering normal and instrumented host C under sanitizers, source-only rebuilds of both images, timer wrap/64-bit carry, CRC/partial-report rejection and independent FAT16/FAT32 filesystem checks with recall/build/profile files. A further targeted check covers divider-weighted margin selection. The startup watermark loop was inspected in the linked MIPS disassembly: it fills only the aligned region from `__bss_end` to the initial stack pointer, before calling C. The first short hardware report was subsequently collected: five starts, 11.91 seconds, no stream errors; aggregate results are recorded in `profiling.md`. Native/highest-rate and envelope stress cases remain unmeasured. See `profiling.md` for measurement scope, instrumentation effects and the idle-only reserved-file write path.

Build 17 adds project-written bulk SPI reception and typed audio copies, retaining the same clock, CRC verification, update layout and buffer safety guards. The source-only test suite includes the project's narrow MIPS instruction interpreter from its private development tests, with a new logical SPI model; no vendor instructions are used as test fixtures. The cold profiler report formatter is size-optimised to keep the optional diagnostic within the existing code region. This optimisation trial has software verification but no measured hardware speedup yet.
