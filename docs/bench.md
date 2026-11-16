# Fade/Gate and normal-build bench checks

Prepare with `make`, `make profile`, then `python3 tools/prepare_bench.py --card "/Volumes/TTA ONE"`. The tool stages files locally under `build/bench`, verifies image hashes and configuration, and preserves the first pre-test card config, playlist and reports under `build/bench/card-backup`. It never writes the card. Generated audio is original; no card samples are redistributed. Rebuild and re-prepare after changing firmware.

## First case: Fade profiling

From the repository, with the card mounted:

```sh
cp build/bench/audio/*.wav "/Volumes/TTA ONE/"
cp build/bench/01-fade/config.txt "/Volumes/TTA ONE/config.txt"
cp build/bench/01-fade/playlist.txt "/Volumes/TTA ONE/playlist.txt"
cp build/bench/01-fade/image.hex "/Volumes/TTA ONE/image.hex"
sync
cmp build/bench/01-fade/image.hex "/Volumes/TTA ONE/image.hex"
diskutil eject "/Volumes/TTA ONE"
```

Install the image using the usual updater procedure. Set maximum pitch before playing, leave pitch CV disconnected, and keep pitch fixed during the run. The playlist has just W9624: a 90-second native-duration, 220 Hz mono PCM24 tone at 96 kHz, with modest amplitude. At maximum quantized pitch it lasts about 60 seconds and sounds approximately 330 Hz. It does not loop in Fade mode.

Tap PLAY once and let it finish. Then make about ten quick retriggers while it is playing: leave approximately half a second between taps and check that it restarts without a stuck blue light. These ramps are click suppression, only about 1.8 ms at this pitch, not long fades. Finally turn FILE with PLAY released until playback stops. Wait five seconds, power down and mount the card. Preserve the report before running the next case:

```sh
cp "/Volumes/TTA ONE/ONEPROF.TXT" build/bench/fade-report.txt
python3 tools/analyze_profile.py build/bench/fade-report.txt \
  --manifest build/bench/01-fade/manifest.json \
  --baseline build/bench/03-normal/manifest.json
```

Check the report build number, nonzero envelope samples and error counters. The report contexts can confirm Fade, but they record worst/margin observations rather than every action. Archive and investigate any unexpected blue stop; do not mix recovery retries into a clean result.

## Second case: Gate profiling

After preserving the Fade report, copy `build/bench/02-gate/config.txt` and `playlist.txt` to the card as `config.txt` and `playlist.txt`. Keep the profiling firmware installed; its image is identical to the Fade case. Sync and eject. Power-cycle to start separate counters and load Gate configuration.

At maximum pitch, tap PLAY once to latch manual playback. Let the tone finish once. Then make about ten start/stop pairs, approximately half a second apart. In Gate mode PLAY toggles playback; release should fade and stop cleanly, and the next tap should start again. If an external gate source is available, separately check held-high playback and release-low stopping; use enough low time for release to finish. No external gate source is required for the manual test. Stop triggers, turn FILE if necessary, release PLAY, wait five seconds, power down and mount.

Save as `build/bench/gate-report.txt` and analyse with the Gate manifest and normal baseline, as above. Do not proceed to normal firmware until both profiling reports have been preserved and assessed.

## Third case: normal firmware

Copy `build/bench/03-normal/config.txt`, `playlist.txt` and `image.hex` to the card under those names. Sync, compare the image as in the first case, eject, and install through the updater. This image has profiling disabled even though its build number is the same. An existing ONEPROF.TXT will remain stale and is not evidence about this run.

The playlist order is W9624 (96 kHz/24-bit), Q480 (48 kHz/16-bit), Q960 (96 kHz/16-bit), Q441 (44.1 kHz/16-bit), Q882 (88.2 kHz/16-bit). Manual file recall may select a different initial entry. The long low tone identifies W9624; the Q tones are about two seconds long at maximum pitch and roughly twice its frequency. Browse to each entry and tap PLAY immediately after browsing, then repeat quick retriggers. Check that the first tap after a file change works, starts remain responsive, each file finishes and later taps still work. Listen for clicks, truncation or unexpected silence, and check for blue errors. These observations are subjective; they do not establish numerical latency.

For normal-build envelope feel, power down and copy the Fade or Gate config alone onto the card while keeping the normal image. Power-cycle and repeat a few manual retriggers/toggles. Retain the normal playlist so file-switch behaviour can also be checked. No profiler report is expected.

## Restore the previous setup

After testing, restore `build/bench/card-backup/config.txt` and `playlist.txt` to the card if those backup files exist. Keep normal firmware installed for ordinary use, sync and eject. Restoring config/playlist does not restore the selected-file recall. Generated WAVs can remain on the card without appearing in the restored playlist.
