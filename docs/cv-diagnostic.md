# CV input diagnostic (build 29)

This separate firmware measures the combined pitch knob/CV ADC input. It makes no sound. It does not implement CV direction control yet: these measurements will establish how negative and positive voltages reach the processor.

Build with `make cv-diag`. The image is `build/cv-diag/image.hex`; install using the usual updater procedure. The working build 28 Flip image is staged separately at `build/cv-diag/rollback-28.hex` for restoring playback afterwards.

## Take captures

Keep a numbered note of the knob position and the voltage measured at the CV input for each capture. The firmware cannot measure the external voltage independently of the knob.

1. Set the knob and CV source. Start with small voltages within your usual patch levels; this test does not establish the input's safe voltage limits.
2. Tap and release PLAY. Red blinks for half a second to settle, then stays red during a five-second capture.
3. Keep the settings steady until the light goes out. Blue briefly indicates saving.
4. Change the settings and repeat, keeping all captures in the same power session.

Up to 12 captures accumulate in `ONECV.TXT`. Suggested captures:

| Row | Knob | CV |
| --- | --- | --- |
| 1 | Fully counterclockwise | Unplugged |
| 2 | Centre | Unplugged |
| 3 | Fully clockwise | Unplugged |
| 4 | Centre | Patched, measured 0 V |
| 5 | Centre | Negative DC, e.g. measured −1 V |
| 6 | Centre | Positive DC, e.g. measured +1 V |
| 7 | Centre | Another negative DC level |
| 8 | Centre | Another positive DC level |
| 9 | Centre | Bipolar LFO; note measured minimum/maximum |
| 10–12 | Your choice | Extra levels or different knob positions |

For the LFO capture, choose a period shorter than five seconds so both peaks can be observed. The remaining captures are optional: the first six already establish useful behaviour. Report actual measured voltages rather than source dial markings.

FILE and GATE do nothing in this diagnostic. Additional PLAY clicks during a capture are ignored. After 12 captures, another click lights steady red to indicate full. Alternating red/blue indicates an SD/report error; PLAY retries mounting and saving the rows held in RAM.

**Copy the report before starting another power session.** Power cycling clears the capture rows in RAM; the first new capture replaces the previous report on disk. Each successful capture saves all rows collected in the current session.

## Read the report

Mount the card and run:

```sh
python3 tools/analyze_cv.py "/Volumes/TTA ONE/ONECV.TXT"
```

Add `--json` for structured output. Send the report together with your numbered voltage/knob notes. A notes template is staged at `build/cv-diag/notes.csv`.

The report includes raw 10-bit ADC minimum, maximum, average, standard deviation, counts at the two rails (0 and 1023), and missed polls. Counts are not volts. Sampling is nominally once per millisecond using the existing firmware clock, not a calibrated oscilloscope; missing polls are reported rather than invented. LFO extrema depend on the signal period and polling interval.

The report is 1024 bytes with a CRC. The restricted FAT writer creates/updates only the owned `ONECV.TXT`, refuses a foreign file with that name, and verifies its writes. Startup also attempts to update `ONEBUILD.TXT`. No playlist or configuration changes are needed.

## Restore Flip playback

After preserving the report, copy `build/cv-diag/rollback-28.hex` to the card as `image.hex`, verify it with `cmp`, eject and run the usual updater procedure again. This restores the proven build 28 profiling image.

Host tests exercise capture timing, button handling, timer wrap, full-scale statistics, report integrity, write retries and FAT16/FAT32 filesystems. The diagnostic still needs its first hardware run.
