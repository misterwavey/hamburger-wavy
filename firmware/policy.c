/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
#include "pitch_tables.h"

uint32_t pitch_divider(uint32_t value, uint32_t rate, CvMode mode) {
  uint32_t factor = (rate == 44100 || rate == 88200) ? 2176u : 2000u;
  if (mode == CV_SELECT)
    return (rate == 88200 || rate == 96000) ? factor / 2u : factor;
  if (value > 255)
    value = 255;
  if (mode == CV_FREE)
    return ((value + 32u) * factor) >> 6;
  /* Our uniform chromatic mapping: +7 .. -41 semitones over the ADC span.
   * Tables are generated from equal temperament, not recovered stock data. */
  uint32_t index = (value * 48u + 127u) / 255u;
  uint32_t family = rate == 44100 ? 0u : rate == 88200 ? 2u :
                    rate == 96000 ? 3u : 1u;
  return chromatic_dividers[family][index];
}
uint32_t cv_file_offset(uint32_t adc, uint32_t count) {
  if (!count)
    return 0;
  if (adc > 1023)
    adc = 1023;
  if (count > 32)
    count = 32;
  uint32_t offset = (1023u - adc) / (1023u / count);
  return offset < count ? offset : count - 1u;
}
uint32_t random_next(uint32_t *state) {
  uint32_t x = *state;
  if (!x)
    x = 0x6d2b79f5u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *state = x;
}
uint32_t random_below(uint32_t *state, uint32_t limit) {
  if (!limit)
    return 0;
  /* Rejection avoids modulo bias; never require an external random source. */
  uint32_t threshold = (0u - limit) % limit, x;
  do {
    x = random_next(state);
  } while (x < threshold);
  return x % limit;
}
void envelope_apply(Envelope *e, uint32_t *stereo, uint32_t frames) {
  /* V1B7C creates 256-step attack/release ramps (0..255 / 255..0).
   * Reproduce that sample-domain shape; buffer scheduling adds latency. */
  for (uint32_t i = 0; i < frames; i++) {
    uint32_t gain = e->gain;
    if (e->gain < e->target) {
      uint32_t next = e->gain + 128u;
      e->gain = next < e->target ? next : e->target;
    } else if (e->gain > e->target) {
      e->gain = e->gain > 128u ? e->gain - 128u : 0;
      gain = e->gain;
    }
    int32_t sample = (int32_t)stereo[2 * i];
    int64_t scaled = ((int64_t)(sample / 256) * gain) >> 15;
    stereo[2 * i] = stereo[2 * i + 1] = (uint32_t)scaled << 8;
  }
}

void envelope_tail(uint32_t *stereo, uint32_t frames, uint32_t start,
                   uint32_t total) {
  for (uint32_t i = 0; i < frames; i++) {
    uint32_t left = start < total ? total - start : 0;
    if (left <= 256u) {
      uint32_t gain = left ? (left - 1u) * 128u : 0;
      int64_t scaled = ((int64_t)((int32_t)stereo[2 * i] / 256) * gain) >> 15;
      stereo[2 * i] = stereo[2 * i + 1] = (uint32_t)scaled << 8;
    }
    start++;
  }
}
