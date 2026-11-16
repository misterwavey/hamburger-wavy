# Updater image layout and the first extension trial

The standard build remains build 21. `make flash-probe` creates a separate experimental build 22 under `build/flash-probe`; it does not replace the standard image. The first hardware trial reached build 22; rollback is pending.

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
