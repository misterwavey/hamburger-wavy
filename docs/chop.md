# Chop: CV-selected playback position

Chop is mode 12 in the extended build. It loops the selected sample forward at native speed. Each GATE rising edge samples the knob/CV input and jumps to that position with the existing short fade-out/re-prime/fade-in transition. CV changes between gates do not seek. PLAY starts from the current CV-selected position, or stops an already running sample. A gate can also start playback while stopped, including after a PLAY stop; stop the gate source if you want it to remain stopped. FILE browsing stops and selects another sample.

## Controls

The full knob/CV ADC span maps over the complete file: fully counterclockwise is approximately the beginning and fully clockwise approximately the end on the measured module. Positive external CV moves the selected position later; negative CV moves it earlier. The knob and CV add in the same analogue circuit, so the knob supplies a position offset. This is not a calibrated voltage-to-position scale; extreme settings can clip. Chop does not use the zero calibration from CV Flip/Tape.

Patch stepped or sequenced CV to the CV input and a clock to GATE. Each pulse jumps to the sampled position and plays onwards until the next pulse. Alternatively, use a slow LFO for CV and manual gates to scan slices. Shorter gate intervals make shorter audible segments. The sample wraps at its end; selecting the very last frame therefore wraps almost immediately to the beginning. There is no separate slice-length or loop-window control.

Start with gates around 1 Hz. Direction-change fades and SD refill still add latency and gaps. Rapid requests during a transition/refill coalesce to the latest target; this is not an audio-rate granular engine or a promise of one audible jump for every gate. The sampled target is preserved if CV moves after the gate edge.

## Configuration

```text
# Hamburger Wavy
Chop
Quantize
Short
```

Free/Quantize/Select File settings and their delay are overridden: playback remains native speed and CV sets position. Line 3 is still required. Select mode 12 with the existing held-PLAY/FILE menu, or use this config on the card. No calibration is needed.

## Trying it

Use a long, recognisable recording. Try different knob positions with PLAY starts, then start playback and send slow gates at a steady CV: each gate should repeat from the same position. Change CV after a gate but before the next one: the jump destination only changes on the next edge. A clock patched to GATE restarts playback after a PLAY stop, so unpatch it when you want the sample to stay stopped.
