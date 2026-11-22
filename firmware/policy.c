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
#ifdef ONE_FLIP
uint32_t cv_frame_offset(uint32_t adc,uint32_t frames) {
  if(frames<2u) return 0;
  if(adc>1023u) adc=1023u;
  uint32_t span=frames-1u,position=1023u-adc;
  /* Exact floor(span*position/1023), without overflow or 64-bit division. */
  return (span/1023u)*position+((span%1023u)*position)/1023u;
}
#endif
#ifdef ONE_COLOUR
/* Drive then crush on the left word; both channels receive the result.
 * Drive: Q8 pre-gain 1x..16x into y = 2x - x|x| with x clamped to full scale,
 * then a drive-dependent level correction. Crush: rounded
 * mid-tread quantising to 16..4 bits and a 1..32 frame sample-and-hold.
 * Zero input stays zero in both stages, so fades still end in silence. */
void colour_apply(Colour *c, uint32_t *stereo, uint32_t frames, uint32_t drive,
                  uint32_t crush) {
  if (drive > 255u) drive = 255u;
  if (crush > 255u) crush = 255u;
  if (!drive && !crush) return;
  int32_t gain = 256 + (int32_t)drive * 15;
  /* Level correction, Q8 and below one: falls quickly towards about -10 dB so
   * mid-level material keeps roughly its loudness as drive rises. Quiet
   * material still gets louder and full-scale material quieter. */
  int32_t trim = 77 + 179 * 256 / (256 + (gain - 256) * 2);
  uint32_t drop = 8u + (crush * 12u + 127u) / 255u;
  uint32_t period = 1u + crush * 31u / 255u;
  int32_t half = (int32_t)(1u << (drop - 1u));
  int32_t mask = (int32_t)~((1u << drop) - 1u), top = 0x7fffff & mask;
  uint32_t hold = c->hold, count = c->count;
  for (uint32_t i = 0; i < frames; i++) {
    int32_t x = (int32_t)stereo[0] >> 8;
    if (drive) {
      x = (int32_t)(((int64_t)x * gain) >> 9);
      if (x > 0x7fffff) x = 0x7fffff;
      else if (x < -0x7fffff) x = -0x7fffff;
      x = 2 * x - (int32_t)(((int64_t)x * (x < 0 ? -x : x)) >> 23);
      if (x > 0x7fffff) x = 0x7fffff;
      x = (x * trim) >> 8;
    }
    if (crush) {
      if (!count) {
        x = (x + half) & mask;
        if (x > top) x = top;
        hold = (uint32_t)x << 8;
        count = period;
      }
      count--;
      stereo[0] = stereo[1] = hold;
    } else
      stereo[0] = stereo[1] = (uint32_t)x << 8;
    stereo += 2;
  }
  c->hold = hold;
  c->count = count;
}
/* Cutoff control 1..255 to a Q15 coefficient f = 2 sin(pi fc / fs), about
 * fs/500 to fs/6.3 over 6.3 octaves, with a piecewise-linear exponential. */
uint32_t filter_coefficient(uint32_t cutoff) {
  if (cutoff > 255u) cutoff = 255u;
  uint32_t octaves = cutoff * 1613u / 255u; /* Q8 */
  uint32_t f = (412u * (256u + (octaves & 255u)) << (octaves >> 8)) >> 8;
  return f > 32000u ? 32000u : f;
}
/* Two-pole state-variable low-pass on the left word; both channels receive
 * the result. The coefficient ramps linearly to the target across the call.
 * Damping falls from 1.41 (no peak) to about 0.08; input is trimmed by up to
 * 6 dB as resonance rises. Zero state and zero input give zero output.
 * The high-pass and band-pass outputs come from the same two states. */
void filter_apply(Filter *s, uint32_t *stereo, uint32_t frames,
                  uint32_t target, uint32_t resonance) {
  filter_shape(s, stereo, frames, target, resonance, FILTER_LOW);
}
void filter_shape(Filter *s, uint32_t *stereo, uint32_t frames,
                  uint32_t target, uint32_t resonance, uint32_t shape) {
  if (!frames) return;
  if (resonance > 255u) resonance = 255u;
  if (target > 32000u) target = 32000u;
  int32_t damping = 46341 - (int32_t)resonance * 171;
  int32_t trim = 256 - (int32_t)(resonance >> 1);
  int32_t low = s->low, band = s->band, f = (int32_t)s->f;
  int32_t step = ((int32_t)(target << 8) - f) / (int32_t)frames;
  for (uint32_t i = 0; i < frames; i++) {
    f += step;
    int32_t k = f >> 8;
    int32_t in = (((int32_t)stereo[0] >> 8) * trim) >> 8;
    low += (int32_t)(((int64_t)band * k) >> 15);
    int32_t high = in - low - (int32_t)(((int64_t)band * damping) >> 15);
    band += (int32_t)(((int64_t)high * k) >> 15);
    int32_t out = shape == FILTER_HIGH ? high : shape == FILTER_BAND ? band : low;
    if (out > 0x7fffff) out = 0x7fffff;
    else if (out < -0x800000) out = -0x800000;
    stereo[0] = stereo[1] = (uint32_t)out << 8;
    stereo += 2;
  }
  s->low = low;
  s->band = band;
  s->f = target << 8;
}
/* Rate control 0..1023 to a phase step per frame. Tremolo spans 1 to 16 Hz
 * (four octaves), ring modulation 16 Hz to 4.1 kHz (eight), both with a
 * piecewise-linear exponential, at the file's own sample rate. */
uint32_t mod_increment(uint32_t rate, uint32_t ring, uint32_t sample_rate) {
  if (rate > 1023u) rate = 1023u;
  if (!sample_rate) return 0;
  uint32_t octaves = rate * (ring ? 2048u : 1024u) / 1023u; /* Q8 */
  uint32_t hz = ((ring ? 4096u : 256u) * (256u + (octaves & 255u)) << (octaves >> 8)) >> 8; /* Q8 */
  return (uint32_t)(((uint64_t)hz * ((1u << 28) / sample_rate)) >> 4);
}
/* Parabolic sine with one correction term, Q15, about 0.1% error. */
static inline int32_t sine_q15(uint32_t phase) {
  int32_t x = (int32_t)(phase >> 16) - 32768;
  int32_t y = (x * (32768 - (x < 0 ? -x : x))) >> 13;
  return (y * (25395 + ((7373 * (y < 0 ? -y : y)) >> 15))) >> 15;
}
int32_t mod_sine(uint32_t phase) { return sine_q15(phase); }
/* Rate control 0..1023 to a phase step per frame for the wah sweep:
 * 0.125 to 8 Hz (six octaves). */
uint32_t wah_increment(uint32_t rate, uint32_t sample_rate) {
  if (rate > 1023u) rate = 1023u;
  if (!sample_rate) return 0;
  uint32_t octaves = rate * 1536u / 1023u; /* Q8 */
  uint32_t hz = (32u * (256u + (octaves & 255u)) << (octaves >> 8)) >> 8; /* Q8 */
  return (uint32_t)(((uint64_t)hz * ((1u << 28) / sample_rate)) >> 4);
}
/* Cutoff control swung around its centre by up to 100 steps each way
 * (about 2.5 octaves) at full depth, kept inside 1..255. */
uint32_t wah_cutoff(uint32_t centre, uint32_t depth, int32_t sine) {
  if (depth > 255u) depth = 255u;
  if (centre > 255u) centre = 255u;
  int32_t cutoff = (int32_t)centre + (((int32_t)(depth * 100u / 255u) * sine) >> 15);
  return cutoff < 1 ? 1u : cutoff > 255 ? 255u : (uint32_t)cutoff;
}
/* All-pass coefficient (1 - t) / (1 + t), Q15, where t = tan(pi fc / fs) is
 * taken as half the filter coefficient for the same cutoff control. */
uint32_t phaser_coefficient(uint32_t cutoff) {
  uint32_t f = filter_coefficient(cutoff ? cutoff : 1u);
  return 32768u * (65536u - f) / (65536u + f);
}
/* Four first-order all-pass stages on the left word, with feedback from the
 * last stage, mixed with the dry signal; both channels receive the result.
 * Intensity raises the wet share to one half and the feedback to 0.5
 * together, with the wet path scaled by (1 + feedback) so the notches stay
 * deep, and trims the input by up to 4.4 dB against the peaks between them.
 * The coefficient ramps linearly to the target across the call. Zero state
 * and zero input give zero output; zero intensity leaves samples unchanged. */
void phaser_apply(Phaser *s, uint32_t *stereo, uint32_t frames, uint32_t target,
                  uint32_t intensity) {
  if (!frames) return;
  if (intensity > 255u) intensity = 255u;
  if (target > 32767u) target = 32767u;
  int32_t mix = (int32_t)(intensity * 16384u / 255u);      /* Q15, up to 0.5 */
  int32_t feedback = (int32_t)(intensity * 16384u / 255u); /* Q15, up to 0.5 */
  int32_t wet = (mix * (32768 + feedback)) >> 15;          /* Q15, up to 0.75 */
  int32_t trim = 256 - (int32_t)(intensity * 102u / 255u); /* Q8, down to 0.6 */
  int32_t z0 = s->z[0], z1 = s->z[1], z2 = s->z[2], z3 = s->z[3], z4 = s->z[4];
  int32_t a = (int32_t)s->a;
  int32_t step = ((int32_t)(target << 8) - a) / (int32_t)frames;
  for (uint32_t i = 0; i < frames; i++) {
    a += step;
    int32_t k = a >> 8;
    int32_t dry = (((int32_t)stereo[0] >> 8) * trim) >> 8;
    int32_t in = dry + (int32_t)(((int64_t)z4 * feedback) >> 15);
    int32_t y1 = (int32_t)(((int64_t)(z1 - in) * k) >> 15) + z0;
    int32_t y2 = (int32_t)(((int64_t)(z2 - y1) * k) >> 15) + z1;
    int32_t y3 = (int32_t)(((int64_t)(z3 - y2) * k) >> 15) + z2;
    int32_t y4 = (int32_t)(((int64_t)(z4 - y3) * k) >> 15) + z3;
    z0 = in; z1 = y1; z2 = y2; z3 = y3; z4 = y4;
    int32_t out = dry + (int32_t)(((int64_t)y4 * wet - (int64_t)dry * mix) >> 15);
    if (out > 0x7fffff) out = 0x7fffff;
    else if (out < -0x800000) out = -0x800000;
    stereo[0] = stereo[1] = (uint32_t)out << 8;
    stereo += 2;
  }
  s->z[0] = z0; s->z[1] = z1; s->z[2] = z2; s->z[3] = z3; s->z[4] = z4;
  s->a = target << 8;
}
/* Delay for a sweep position: 32 frames at the bottom of the sine to 384 at
 * the top, 3.5 octaves, with a piecewise-linear exponential. Q16 frames. */
uint32_t flanger_delay(int32_t sine) {
  int32_t octaves = 448 + ((448 * sine) >> 15); /* Q8 */
  if (octaves < 0) octaves = 0;
  if (octaves > 896) octaves = 896;
  uint32_t o = (uint32_t)octaves;
  return ((32u * (256u + (o & 255u))) << (o >> 8)) << 8;
}
/* A delay line on the left word, read with linear interpolation at a delay
 * that ramps linearly to the target across the call; both channels receive
 * the result. As for the phaser, intensity raises the wet share to one half
 * and the feedback to 0.5 together, with the wet path scaled by
 * (1 + feedback) so the notches stay deep, and trims the input by up to
 * 4.4 dB. Zero state and zero input give zero output, and the line decays to
 * exact silence; zero intensity leaves samples unchanged. */
void flanger_apply(Flanger *s, uint32_t *stereo, uint32_t frames, uint32_t target,
                   uint32_t intensity) {
  if (!frames) return;
  if (intensity > 255u) intensity = 255u;
  if (target > (FLANGER_FRAMES - 2u) << 16) target = (FLANGER_FRAMES - 2u) << 16;
  if (target < 1u << 16) target = 1u << 16;
  int32_t mix = (int32_t)(intensity * 128u / 255u);        /* Q8, up to 0.5 */
  int32_t feedback = mix;                                  /* Q8, up to 0.5 */
  int32_t wet_gain = (mix * (256 + feedback)) >> 8;        /* Q8, up to 0.75 */
  int32_t trim = 256 - (int32_t)(intensity * 102u / 255u); /* Q8, down to 0.6 */
  uint32_t at = s->at & (FLANGER_FRAMES - 1u);
  int32_t delay = (int32_t)s->delay;
  if (delay < 1 << 16) delay = (int32_t)target;
  int32_t step = ((int32_t)target - delay) / (int32_t)frames;
  for (uint32_t i = 0; i < frames; i++) {
    delay += step;
    uint32_t whole = (uint32_t)delay >> 16;
    int32_t fraction = (int32_t)(((uint32_t)delay >> 1) & 32767u);
    int32_t near = s->line[(at - whole) & (FLANGER_FRAMES - 1u)];
    int32_t far = s->line[(at - whole - 1u) & (FLANGER_FRAMES - 1u)];
    int32_t wet = near + (((far - near) * fraction) >> 15);
    int32_t dry = (((int32_t)stereo[0] >> 8) * trim) >> 8;
    int32_t keep = dry + wet * feedback;
    if (keep > 0x7fffff) keep = 0x7fffff;
    else if (keep < -0x800000) keep = -0x800000;
    s->line[at] = (int16_t)(keep / 256); /* towards zero: no residual offset */
    at = (at + 1u) & (FLANGER_FRAMES - 1u);
    int32_t out = dry + wet * wet_gain - ((dry * mix) >> 8);
    if (out > 0x7fffff) out = 0x7fffff;
    else if (out < -0x800000) out = -0x800000;
    stereo[0] = stereo[1] = (uint32_t)out << 8;
    stereo += 2;
  }
  s->at = at;
  s->delay = target;
}
/* Multiply the left word by a gain that follows a sine; both channels
 * receive the result. Tremolo swings the gain between 1 and 1 - depth.
 * Ring modulation crossfades from the dry signal to signal times sine, so
 * at full depth the gain swings between +1 and -1. The gain never exceeds
 * one and zero input stays zero. */
void mod_apply(Mod *m, uint32_t *stereo, uint32_t frames, uint32_t increment,
               uint32_t depth, uint32_t ring) {
  if (depth > 255u) depth = 255u;
  int32_t amount = (int32_t)((depth * 32768u + 127u) / 255u);
  uint32_t phase = m->phase;
  for (uint32_t i = 0; i < frames; i++) {
    phase += increment;
    int32_t sine = sine_q15(phase);
    int32_t gain = ring ? 32768 - amount + ((amount * sine) >> 15)
                        : 32768 - (int32_t)(((uint32_t)amount * (uint32_t)(32768 - sine)) >> 16);
    int32_t out = (int32_t)(((int64_t)((int32_t)stereo[0] >> 8) * gain) >> 15);
    stereo[0] = stereo[1] = (uint32_t)out << 8;
    stereo += 2;
  }
  m->phase = phase;
}
#endif
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
#ifdef ONE_TOLERANT_ENCODER
/* Count quarter steps round the cycle 3 -> 2 -> 0 -> 1 -> 3 (positive).
 * A poll that lands two states on means one state went unseen: take it in
 * the direction already travelled, or, straight off the detent, in the
 * direction of the next single move. Report a step on reaching the detent
 * with at least three of the four quarters accounted for; contact bounce
 * nets to zero and reports nothing. Bounce moves one line at a time, so a
 * return to the detent after an unseen state that does not add up is still
 * a real click: report it in the direction last seen moving, else in the
 * direction of the previous step. */
void encoder_init(Encoder *e, uint32_t ab) {
  memset(e, 0, sizeof(*e));
  e->last = e->filtered = e->raw = (uint8_t)(ab & 3u);
  e->previous = 1;
  e->fall_age[0] = e->fall_age[1] = e->period = ENCODER_AGE_MAX;
  e->hold_min = ENCODER_HOLD_MAX;
}
/* The contacts pull a line low. A closed contact chatters open for up to a
 * few milliseconds, but an open one does not read closed, so low is believed
 * immediately and high only once it has lasted. Chatter is tied to contact
 * make, not to speed, while a quarter cycle shrinks as the knob spins faster:
 * the hold follows the cycle time so the two lines still read high together
 * at each detent. */
uint32_t encoder_filter(Encoder *e, uint32_t ab, uint32_t now) {
  ab &= 3u;
  if (ab != e->raw) { e->raw = (uint8_t)ab; e->raw_changes++; }
  uint32_t elapsed = now - e->polled;
  e->polled = now;
  for (uint32_t bit = 0; bit < 2u; ++bit) {
    uint32_t age = e->fall_age[bit] + elapsed;
    e->fall_age[bit] = age < e->fall_age[bit] || age > ENCODER_AGE_MAX ? ENCODER_AGE_MAX : age;
  }
  /* A turn slower than the last measured cycle is judged by its own age. */
  uint32_t cycle = e->fall_age[0] < e->fall_age[1] ? e->fall_age[0] : e->fall_age[1];
  if (cycle < e->period) cycle = e->period;
  uint32_t hold = cycle / 8u;
  if (hold > ENCODER_HOLD_MAX) hold = ENCODER_HOLD_MAX;
  if (hold < ENCODER_HOLD_MIN) hold = ENCODER_HOLD_MIN;
  for (uint32_t bit = 0; bit < 2u; ++bit) {
    uint8_t mask = (uint8_t)(1u << bit);
    if (!(ab & mask)) {
      if (e->timing & mask) {
        uint32_t gap = now - e->since[bit];
        if (gap > e->bridged_max) e->bridged_max = gap;
        e->timing &= (uint8_t)~mask;
      }
      if (e->filtered & mask) { /* this line's closure: one per cycle */
        e->period = e->fall_age[bit];
        e->fall_age[bit] = 0;
        e->filtered &= (uint8_t)~mask;
      }
    } else if (!(e->filtered & mask)) {
      if (!(e->timing & mask)) { e->timing |= mask; e->since[bit] = now; }
      else if (now - e->since[bit] >= hold) {
        e->filtered |= mask; e->timing &= (uint8_t)~mask;
        if (hold < e->hold_min) e->hold_min = hold;
      }
    }
  }
  return e->filtered;
}
int32_t encoder_step(Encoder *e, uint32_t ab) {
  static const uint8_t position[4] = {2, 3, 1, 0};
  ab &= 3u;
  if (ab == e->last) return 0;
  uint32_t moved = (position[ab] - position[e->last]) & 3u;
  e->last = (uint8_t)ab;
  if (moved == 2u) {
    e->jumped = 1;
    if (e->dir) e->acc = (int8_t)(e->acc + 2 * e->dir);
    else e->skipped = 1;
  } else {
    int32_t q = moved == 1u ? 1 : -1;
    e->acc = (int8_t)(e->acc + (e->skipped ? 3 * q : q));
    e->skipped = 0;
    e->dir = (int8_t)q;
  }
  if (ab != 3u) return 0;
  int32_t step = e->acc >= 3 ? 1 : (e->acc <= -3 ? -1 : 0);
  if (step) e->recovered += e->jumped;
  else if (e->jumped) {
    step = e->dir ? e->dir : e->previous;
    e->guessed++;
  }
  if (step) { e->steps++; e->previous = (int8_t)step; }
  e->acc = e->dir = 0;
  e->skipped = e->jumped = 0;
  return step;
}
#endif
