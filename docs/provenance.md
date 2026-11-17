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

Build 18 uses a folded algebraic CRC16 polynomial update rather than table reads. It is project-written code implementing the same SD CRC recurrence, verified against a bitwise reference over all 16777216 possible state/byte pairs. CRC verification has not been disabled. Normal/profiling software checks pass; hardware performance of this revision remains unmeasured.


Build 19 extends the project-written memcpy with alignment-checked word accesses and a byte fallback, using a may_alias type for arbitrary object representations. No vendor code or library implementation was imported. All 11 release tests pass, including compiled-MIPS copy bounds/alignment tests; hardware performance awaits measurement. The build-18 hardware report confirmed improved transfer/refill timing but still recorded a late-commit stop at maximum pitch.


Build 20 doubles the project's audio buffer sizes and uses project-written separate PCM16/PCM24 decode loops. Profiler margin calculations use the same constants as DMA playback. No vendor implementation is imported. File-header parsing and profiler setup/stop code use compiler size optimisation to retain the existing image layout. The normal/profiling static allocations are 55712/57040 bytes; both preserve the 8192-byte minimum stack reserve. Hardware performance and larger-buffer latency effects await measurement.


Build 21 shares the existing project-written folded CRC16 helper between the SD driver and bulk SPI receiver. The receiver calculates the checksum while receiving, and the driver retains the received-CRC comparison; no vendor code or library routine is added. Compiled MIPS checks compare received checksums against an independent Python CRC implementation, with delayed readiness and timeout cases. Mount parsing and stopped-audio allocation/report code use size optimisation. Build 20's hardware report still recorded a deadline miss; build 21 hardware performance awaits measurement.


Build 21 subsequently completed a CRC-valid 152.681-second maximum-quantized-pitch Loop/PCM24/96 kHz hardware run with zero recorded streaming errors. Mean completed refill work was 5.998 ms; the profiling guide records the measured margins and limits. This is validation of the tested file/card/settings combination, not a complete mode/rate/card matrix or formal timing proof.


The separate build-22 flash-extension probe adds a project-written routine and marker in one new program-flash page and a low-flash verifier generated from the compiled page. It imports no vendor implementation or data. The normal image still uses the previous address contract. See flash-layout.md for address coverage, generated expected-data provenance, execution checks and the distinction between hardware capacity and unverified updater acceptance.


The build-23 bank probe fills 48 KiB with a project-generated index-dependent word pattern, overridden only by the project's compiled endpoint routines and final marker. Low-flash code compares every word against the generated expectations before making checked calls near both ends. No vendor code/data are imported. The standard build-21 images remain unchanged; the expanded address contract is explicit and experimental. See flash-layout.md for coverage and pending hardware validation.


Build 24 relocates existing project application objects and constants into the hardware-tested extension bank. Its new low-flash per-page CRC32 checker and generated expected values are project-written and derived from the linked application, with no imported vendor implementation. Standard build-21 outputs remain unchanged. The generated linker/header and source hashes are archived with each candidate. Hardware playback/timing regression for the new placement is pending.


Extended build 25 adds project-written bidirectional PCM streaming, a bounded selected-file FAT index, a shared-sector cache within existing scratch space, and a Flip transport driven by DMA position snapshots. No vendor implementation or data is introduced. The standalone reverse PCM tests, fragmented FAT16/FAT32 fixtures and generated chirp are original project work. Retained legacy build/probe targets compile the new mode out; the extended candidate enables it explicitly. See flip.md for transitions, resource limits and pending hardware validation.


Extended build 26 changes only the project-written Flip transition scheduling and ramps. The old stream is faded in an inactive half before being stopped, with zero output checked in host tests and compiled PCM ramp arithmetic checked in the MIPS model. No vendor code/data are added. The hardware click-reduction result remains pending; build 25's latest chirp report recorded no streaming errors despite the reported small clicks.


Extended build 27 corrects the project-written fade-preparation deadline handling introduced in build 26. Preparation uses a typed union view of existing scratch storage, preserves queued decoded audio on deferral, and retains the DMA publication guard. A simulated guard-closing regression case and a late-gate case exercise the fix. No vendor implementation or new RAM buffer is introduced; hardware confirmation remains pending.


Extended build 28 adds DMA-half/ring-state agreement checks to the project's fade preparation and silence hand-off. A new regression reproduces build 27's stale-bookkeeping failure by advancing DMA between software observations, and covers both half boundaries and the silent-half hand-off. No vendor code or data are added. Hardware confirmation remains pending; the validated build-25 artifact is retained as rollback.


Build 28 subsequently passed a 32.005-second maximum-quantized-pitch PCM24/96000 Hz Flip hardware run with 31 starts/resumes and zero recorded streaming errors. The user reported it working great. The report and observed limits are recorded in flip.md; residual clicks and transition latency were not separately quantified.
