# Updater image layout and the first extension trial

The standard build remains build 21. `make flash-probe` creates a separate experimental build 22 under `build/flash-probe`; it does not replace the standard image. The first hardware trial reached build 22, then rollback reached build 21.

## Hardware capacity versus our current limit

Microchip lists 256 KiB of program flash and 3 KiB of boot flash for the [PIC32MX170F256B](https://www.microchip.com/en-us/product/pic32mx170f256b). Its program-flash physical range is `0x1D000000..0x1D03FFFF`. The [PIC32 programming specification, Table 5-1 and its notes](https://ww1.microchip.com/downloads/en/devicedoc/60001145u.pdf) gives the relevant 28/36/44-pin MX family 256-word (1024-byte) erase pages and 32-word (128-byte) programming rows. Those are hardware operations, not evidence of how ONE's updater uses them.

Our normal main-text allowance of 31812 bytes is an intentionally conservative linker boundary. Code/constants begin at CPU address `0x9D00BE00` and stop before the reset helper retained at `0x9D013A44`. We inherited that placement from the successful update layout, not from a discovered updater size check. The actual resident updater binary has not been obtained. Its acceptance, erase, address filtering and recovery rules remain unknown. Upper program flash has not been dumped and cannot yet be declared unoccupied.

## What image.hex contains

Intel HEX is an addressed list of data records, not a raw 256 KiB flash dump. Each line has a byte count, 16-bit offset, record type, payload and checksum. A type-04 record supplies the upper address bits; type-00 records supply bytes; type-01 ends the file. For example, `:020000041d01dc` selects base `0x1D010000`, so a following data record at offset `0x4000` addresses physical `0x1D014000`.

The CPU executes the cached alias `0x9D014000` of that physical address; the experiment verifies through uncached alias `0xBD014000`. These are three ways to refer to the same flash, not three allocations.

The current normal update addresses 33496 unique bytes, including eight configuration bytes. It is sparse, and its original record ordering is retained:

| Physical addresses | Purpose in our image |
| --- | --- |
| `0x1D00A180`, then sparse stubs `0x1D00A200..0x1D00A767` | Exception/interrupt entry stubs |
| `0x1D00B970..0x1D00BB3B` | Addressed startup span, with unused bytes filled `0xFF` |
| `0x1D00BCF0..0x1D00BCFF` | Addressed exception stub span |
| `0x1D00BE00..0x1D013D83` | Addressed application span: main code/constants, fixed reset helper and unused `0xFF` bytes |
| `0x1FC00BF4..0x1FC00BFB` | Two unchanged clock/hardware configuration words |
| `0x1D014000..0x1D0143FF` | **Build-22 experiment only:** one added page |

Normal reset-vector and resident-updater contents are absent. Boot-debug-vector records at `0x1FC00480` are also absent from our distribution. The two supplied configuration words are not the entire protection/configuration state. Missing addresses do not prove preservation: the updater could erase a larger region before writing the supplied records. The stock HEX backup is consequently not a complete device-recovery image.

Changing our linker alone cannot establish larger-flash support. The output records must cover the extra addresses, the updater must write them, and the CPU must successfully execute the resulting code.

## Why this first test adds only one page

Build 22 adds exactly 1024 addressed bytes at `0x1D014000`, the next page after the old highest addressed page. It does not fill the intervening gap, move the reset helper, or add boot/configuration addresses. The remaining 176 KiB from `0x1D014000` to the device's flash end is a possible investigation area, not established free application space.

A generated linker script puts a 52-byte callable routine at CPU address `0x9D014000` and a 16-byte marker at `0x9D0143F0`; the rest of this page is explicitly `0xFF`. Low flash holds an independent expected copy of all 256 words. Before entering ordinary application startup, low-flash code:

1. Compares every word of the new page through its uncached alias.
2. Calls the actual routine in the new page with two different inputs and checks its results.
3. Proceeds to normal startup and the existing build-record writer only after both checks succeed.

The builder links twice to derive the expected page from the actual compiled extension, checks that the extension does not change between passes, and permits new HEX coverage only in this exact page. The standard build/extractor still reject extension sections. Default normal/profiling HEX output remains byte-for-byte identical to the archived build-21 images.

Build 22 uses 28528 bytes of main text/constants, plus 68 bytes of extension code/data; the new page's total addressed size is 1024. Static RAM remains 55712 bytes. The duplicated expected page is deliberate diagnostic overhead, not a proposed permanent memory layout.

This experiment does not yet link arbitrary main code across the old reset-helper boundary or establish a full upper-flash limit. A successful run establishes this page's acceptance and execution on this device. Further trials can progressively test higher pages with new markers, then place larger code/constants in an extension bank. Moving fixed entry/reset addresses would be a separate experiment.

## Run the hardware trial

The user has authorised a larger-flash experiment. This changes updater address coverage for the first time, with unknown resident contents and erase rules. Verifying the page before executing it avoids a jump into a missing/partial extension, but does not protect against every possible updater failure. SD-only recovery is not guaranteed.

Build with `make flash-probe`. The normal image stays at `build/image.hex` (build 21); the candidate is `build/flash-probe/image.hex` (build 22). No profiling is enabled in the probe. Keep the current playlist/config for familiar playback. On the mounted card:

```sh
cp build/flash-probe/image.hex "/Volumes/TTA ONE/image.hex"
sync
cmp build/flash-probe/image.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Install with the usual updater procedure. A successful probe has the usual one-second blue startup and ordinary playback. Let it boot, play a familiar file, wait five seconds idle, then power down and mount. A **fresh ONEBUILD.TXT saying 22** on this first trial establishes that the new page checked out and its code executed before the application mounted the card. The image.hex file on the card alone does not establish installation.

If low-flash build 22 runs but the new page fails validation, it alternates red/blue at quarter-second intervals and does not enter the application or write build 22. If the updater rejects the whole candidate and old build 21 still runs, the normal build record remains/returns to 21. Other failure appearances are not uniquely diagnosed by the LEDs.

After success, test rollback through the same updater using `build/image.hex` (the unchanged normal build 21), verify ONEBUILD.TXT returns to 21, and check playback. This tests continued update access. The normal image does not explicitly erase the experimental page; successful rollback does not prove that those bytes were erased.


## First hardware result

On the first build-22 trial, the mounted card's image matched the candidate hash and ONEBUILD.TXT identified build 22. Because the build-record writer is reached only after the full-page comparison and two checked extension calls, this supports successful programming/readback and execution of the added page at physical 0x1D014000 on the tested module. The user reported completion of the requested install/playback procedure; detailed playback observations were not separately supplied. It does not establish the updater's erase extent, availability of other upper pages, or continued update access. The next check is rollback to the unchanged build-21 normal image, then confirmation that ONEBUILD.TXT returns to 21 and playback works.


### Rollback result

After the requested rollback procedure, the mounted card's image matched the archived normal build-21 image and ONEBUILD.TXT identified build 21. This confirms continued SD update access and return to normal application startup after the first extension trial. The user reported completing the requested procedure; detailed listening observations were not separately supplied. It does not establish whether the extension page was erased or provide a general recovery guarantee. Only the extra page at physical 0x1D014000 has been verified; the remaining upper-flash range is still untested. A further investigation should check a bounded larger extension bank with executable code and data near its upper end before treating that bank as available application capacity.

## Build 23: bounded 48 KiB bank trial

After build 22 and its rollback succeeded, `make flash-bank` prepares a separate build 23 at `build/flash-bank/image.hex`. Standard builds remain build 21. This extends addressed coverage to the whole bank `0x1D014000..0x1D01FFFF`: 49152 bytes, or 48 hardware erase pages. The legacy gap before that bank stays unaddressed. Code near both ends of the bank is called after verification: the lower routine starts at CPU `0x9D014000`, the upper routine at `0x9D01FF00`, and a marker occupies `0x9D01FFF0..0x9D01FFFF`.

The bank is deliberately filled with varied non-erased words. For each word index from zero, the expected fill is `((index * 0x9E3779B9) ^ 0x48570017) modulo 2^32`, except where the linked code and final marker override it. Low flash stores only the small compiled code/marker expectations and computes the rest of the pattern. It compares all 12288 words through the uncached alias, servicing the watchdog every 256 words, before making two checked calls to each end's routine. This tests missing middle pages as well as the endpoints without duplicating a 48 KiB bank in low flash. It is an exact word comparison, not merely a checksum. The code and marker differ from build 22, so its stale one-page contents cannot pass this trial.

The builder retains the one-page mode's address limits. A separate explicit bank mode permits the larger section placements and HEX span, with two-pass generation and exact coverage tests. Main text/constants use 27908 bytes (3904 free in the legacy main region), the linked bank code/marker occupy 116 bytes, and static RAM remains 55712 bytes. The rest of the addressed bank contains generated test data. This demonstrates available locations if accepted, not that a 48 KiB real-world application has been exercised. It also does not investigate addresses at or above `0x1D020000`.

The bank is not yet hardware validated. It addresses substantially more previously undumped flash than build 22, so that trial's success and rollback do not guarantee this one. Boot/configuration addresses and entry/reset locations are retained. The candidate must not be treated as the new ordinary firmware until acceptance, startup, playback and rollback are checked.

Copy the candidate, compare, eject and install using the usual updater procedure:

```sh
cp build/flash-bank/image.hex "/Volumes/TTA ONE/image.hex"
sync
cmp build/flash-bank/image.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Keep the current config/playlist. Successful ordinary startup and a fresh ONEBUILD.TXT identifying **23** establish that the full bank compared correctly and code at both ends executed. As with build 22, a validation failure alternates red/blue without entering the application or writing the new build record. Let it boot, check playback, stop and wait five seconds idle, then power down and mount. After success, separately test rollback with the unchanged normal build-21 image. No profiler report is expected from the bank probe.


Build-23 software validation: all 16 release tests pass, including full-bank logical execution, first/middle/last-word corruption, a missing final page, exact patterned HEX coverage, legacy record-header preservation, boot/configuration invariants and isolated reconstruction. Testing exposed and corrected the interpreter's prior handling of ROTR as a logical shift; independent immediate/variable rotate and shift encodings now have regression checks. Normal/profile build 21 and the one-page build 22 remain byte-identical to archived artifacts. Hardware acceptance of the bank remains pending.


### Build 23 hardware result

The first bank trial returned a mounted card whose image hash matched the build-23 candidate and whose ONEBUILD.TXT identified build 23. This record is reached only after exact uncached comparison of all 12288 words across physical 0x1D014000..0x1D01FFFF and two checked calls to each endpoint routine. The result supports successful programming/readback of the full 48 KiB bank and execution near both ends on the tested module. The user reported completing the requested installation/playback procedure; detailed listening observations were not separately supplied. Addresses at or above 0x1D020000 remain untested, and no full-size real application has been exercised. Rollback following this larger bank trial remains pending.

If rollback succeeds, the next implementation step can place real application functions/constants in this verified bank, retaining the fixed entry/reset addresses. The combined legacy main-text allowance (31812 bytes) plus this extension bank (49152 bytes) is 80964 bytes, approximately 79.1 KiB. This is a layout capacity supported by the bank experiment, not current occupied code size or a guarantee of all updater behaviours. RAM and processing throughput are unchanged by gaining flash addresses.


### Build 23 rollback result

After the requested rollback procedure, the mounted card's image matched the archived normal build-21 image and ONEBUILD.TXT identified build 21. Continued SD update access and return to normal application startup are therefore confirmed following the full 48 KiB extension-bank trial. The user reported completion of the requested procedure; detailed listening observations were not separately supplied. Whether the bank was erased on rollback remains unknown. Both extension trials and their rollbacks have now reached the expected build records. Real application placement in the verified bank is the next implementation step; the current standard build still uses only the legacy layout.

## Build 24: real application in the verified bank

`make extended` builds a separate normal candidate under `build/extended`, and `make extended-profile` builds its profiling counterpart under `build/extended/profile`. Both identify as build 24. Default normal/profiling builds remain build 21, while the earlier build-22/23 probe targets remain available.

A generated linker script places the complete FAT, WAV, pitch/envelope policy, config/playlist, recall and profiler objects (code and constants) in `.app_extension` starting at CPU `0x9D014000`. Startup, exception/reset entries, application control, low-level SD/SPI/audio drivers and memory helpers stay in the legacy region. The fixed entries and 48 KiB address coverage match the established trials. Unoccupied bank bytes are explicitly `0xFF`, replacing the build-23 test pattern.

A low-flash checker validates CRC32 for each of the 48 bank pages through its uncached alias before calling the relocated application. The expected page CRCs are generated from a first link; a second link inserts them and must leave bank contents unchanged. The comparison uses volatile reads so the compiler cannot replace it with a constant result. A mismatch retains the alternating red/blue failure signal and prevents normal startup/build-record writing. CRC32 provides corruption detection, not cryptographic authenticity; this is distinct from the prior exact-pattern experiment. The image still contains only source-built code/data and the existing configuration words.

| Code/constants resource | Normal build 24 | Profiling build 24 |
| --- | ---: | ---: |
| Legacy bank used / free | 13200 / 18612 bytes | 13728 / 18084 bytes |
| Extension bank used / free | 14576 / 34576 bytes | 18312 / 30840 bytes |
| Combined used / free | 27776 / 53188 bytes | 32040 / 48924 bytes |
| Static RAM | 55712 bytes | 57040 bytes |

The combined limit is 80964 bytes. Code-space gains do not change RAM or sample-stream throughput. Placement can affect instruction timing, so build 24 needs playback and profiling regression on hardware despite unchanged sample logic, buffer sizes, SD clock and deadline guard.

First install the extended profiling image with the AGNT2-only Loop/Quantize configuration under `build/extended/bench-loop`, run at fixed maximum pitch for at least one minute, stop with FILE, wait five seconds, power down and mount. A fresh build-24 ONEPROF.TXT is needed; ONEBUILD.TXT alone cannot distinguish the normal and profiling variants. Analyse with `build/extended/profile/manifest.json` and baseline `build/extended/manifest.json`. Keep the archived build-21 image available for rollback. After streaming/envelope checks, test the extended normal image's file changes and retriggers before adopting this layout as the default.


### Build 24 real-application hardware results

The initial CRC-valid report recorded 8 starts and 8.808 seconds of playback with no errors, but its worst/margin contexts used divider 794, so it was a short lower-pitch check. The initial report was preserved; its build-record file was not archived.

The subsequent fresh CRC-valid report recorded one start and 95.183 seconds of Loop/Quantize PCM24/96000 Hz playback. Both worst-work and tightest-margin contexts used maximum quantized pitch, divider 667 (approximately 143928 output frames/s). Ring, late-commit, stationary-DMA, read and copy-overlap errors were all zero; previous report-write failures were zero. ONEBUILD.TXT identifies build 24. This establishes a successful sustained run with real application functions and constants in the verified upper bank, not a complete mode/card regression.

Completed refill work averaged 5.996 ms (worst 7.353 ms), essentially matching build 21's 5.998 ms average. Minimum observed margin was 963 frames, about 6.691 ms, also matching the baseline. All 78610 SD attempts were below 1 ms (mean 866.40 us, worst 941.21 us). Longest observed background-service gap was 852 us. Stack watermark remained 1696 bytes, with 6792 untouched above the unchanged profiling static allocation. The placement change shows no material measured timing regression for this workload. Fade/Gate and normal-build file/retrigger checks on the new layout remain outstanding; staged configs are in build/extended/bench-fade and bench-gate.


### Build 24 Fade regression

A fresh CRC-valid Fade/Quantize report recorded 10 starts and 66.407 seconds of observed playback at PCM24/96000 Hz, with maximum-pitch divider 667 in both worst-work and tightest-margin contexts. All five streaming error counters and previous report-write failures were zero. Envelope/tail rendering averaged 918.99 us (worst 1345.58 us), closely matching build 21's 918.96/1345.62 us.

Completed refill work averaged 6.579 ms (worst 10.197 ms). Minimum observed margin was 579 frames, approximately 4.023 ms, compared with build 21 Fade's 580 frames/4.030 ms. One SD attempt reached 3.878 ms; the other 56003 were below 1 ms. Read-ahead accommodated that delay. Longest service gap was 1.512 ms; stack watermark remained 1696 bytes with 6792 untouched. The envelope cost and observed margin show no material placement regression for this workload. Gate and extended-normal file/retrigger checks remain outstanding.
