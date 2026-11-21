# Flip: interrupt playback direction

Flip is mode 9 in the extended build (`make extended` or `make extended-profile`); the legacy build-21 targets do not include it. It loops a file and reverses direction from the current playback position when GATE rises. It does not restart the file at each gate.

## Controls

| Control | Action in Flip |
| --- | --- |
| PLAY tap | Start forwards from the beginning; tap again to stop. A later start begins forwards again. |
| GATE rising edge | Reverse the running sample from the DMA playhead position captured at that edge. |
| GATE falling edge | No direction change. Holding GATE high produces one flip. |
| FILE | Browse as usual; browsing stops playback and selects/primes the next file. |
| Pitch knob / CV | Retain the selected Free/Quantize speed mapping. |
| Held PLAY + FILE | Existing mode menu; Flip is red count 9. |

GATE alone does not start playback in Flip. Start with PLAY, then use an external trigger, clock or manual gate to interrupt direction. Rising edges during the mode menu or a PLAY gesture are suppressed consistently with existing control handling. In Select File CV mode, PLAY starts still select the file from CV at nominal rate; GATE edges flip direction instead of selecting/retriggering a file.

To boot directly into Flip, config.txt can contain:

```text
# Hamburger Wavy configuration
Flip
Quantize
Short
```

Existing mode counts 1–8 and LED colours retain their meaning. Playback stays red; blue is not used as a direction indicator. Power cycling reloads config.txt.

## Transition behaviour

A gate records the frame actually being heard, not the read-ahead position. The running audio then fades to zero over 512 output frames, the buffer is refilled from the captured position in the new direction, and playback restarts with a fade-in. Other modes keep their 256-frame envelopes.

This is not a seamless crossfade or sample-accurate scratching. There is a short gap for the refill and a wait for a safe buffer boundary; fade time scales with playback rate, and card access adds latency. Edges arriving during a transition update the requested direction and can coalesce, so very fast gates may produce interrupted or silent output instead of one audible reversal per edge. Begin with slow manual pulses or a 1 Hz clock, then increase the rate to find what is useful on your sample and card.

PLAY, browsing and menu changes cancel pending flips. Errors use the usual recovery and blue signals. A flip does not write file-recall state; ordinary manual starts keep the existing recall behaviour.

## Streaming

Selecting a file in a Flip-family mode scans its FAT chain once, with audio stopped, and keeps a small index so reverse reads can seek. Contiguous files are addressed directly; large fragmented files can take longer to select and to seek. Reverse blocks are read in forward byte order and decoded backwards, for 16-bit and 24-bit PCM alike.

A test sweep for hearing direction changes can be generated with `python3 tools/generate_flip_tone.py`. For polarity-controlled direction, see [CV Flip](cv-flip.md).
