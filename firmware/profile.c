/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "profile.h"
#include "build.h"
#ifdef ONE_PROFILE
/* All report storage is fixed; no allocation or disk access while playing. */
typedef struct { uint32_t count, low, high, minimum, maximum; } Metric;
static struct {
  Metric m[PROF_METRICS];
  uint32_t playing, dirty, stopped, last_tick, wall_low, wall_high, gap_max;
  uint32_t plays, failed_reports, saturated, prefetch, latest_margin, timer_min;
  uint32_t errors[PROF_ERRORS], histogram[8];
  uint32_t context[5], worst[5], tightest[5], slack, slack_weight;
  uint32_t stack[3];
  char text[1024];
} p;
static void increment(uint32_t *n) {
  if (*n != 0xffffffffu) ++*n;
  else p.saturated = 1;
}
static void add(uint32_t *low, uint32_t *high, uint32_t ticks) {
  uint32_t old = *low;
  *low += ticks;
  *high += *low < old;
}
void profile_init(void) {
  memset(&p, 0, sizeof(p));
  p.slack = p.slack_weight = 0xffffffffu; p.timer_min = 0xffffffffu;
  for (uint32_t i = 0; i < 16; ++i) {
    uint32_t start = board_ticks(), elapsed = board_ticks() - start;
    if (elapsed < p.timer_min) p.timer_min = elapsed;
  }
}
uint32_t profile_running(void) { return p.playing; }
void profile_poll(void) {
  if (!p.playing) return;
  uint32_t now = board_ticks(), elapsed = now - p.last_tick;
  add(&p.wall_low, &p.wall_high, elapsed);
  if (elapsed > p.gap_max) p.gap_max = elapsed;
  p.last_tick = now;
}
void profile_start(uint32_t rate, uint32_t bits, uint32_t mode, uint32_t cv,
                   uint32_t divider) {
  p.context[0] = rate; p.context[1] = bits; p.context[2] = mode;
  p.context[3] = cv; p.context[4] = divider;
  p.playing = 1; p.prefetch = 0; p.last_tick = board_ticks();
  increment(&p.plays);
}
void profile_stop(uint32_t now) {
  if (p.playing) {
    profile_poll();
    p.playing = 0; p.dirty = 1; p.stopped = now;
  }
}
void profile_pitch(uint32_t divider) {
  if (p.playing) p.context[4] = divider;
}
void profile_sample(uint32_t which, uint32_t ticks) {
  Metric *m = p.m + which;
  if (m->count == 0xffffffffu) { p.saturated = 1; return; }
  if (!m->count || ticks < m->minimum) m->minimum = ticks;
  if (ticks > m->maximum) {
    m->maximum = ticks;
    if (which == PROF_WORK) memcpy(p.worst, p.context, sizeof(p.worst));
  }
  ++m->count;
  add(&m->low, &m->high, ticks);
  if (which == PROF_PREFETCH) p.prefetch = ticks;
  if (which == PROF_SD) {
    static const uint32_t limits[7] = {6000,12000,24000,48000,120000,240000,1200000};
    uint32_t bucket = 0;
    while (bucket < 7 && ticks > limits[bucket]) ++bucket;
    increment(p.histogram + bucket);
  }
}
void profile_publish(uint32_t ticks) {
  if (!p.latest_margin) increment(p.errors + PROF_COPY_ERROR);
  if (ticks > 0xffffffffu - p.prefetch) {
    ticks = 0xffffffffu; p.saturated = 1;
  } else ticks += p.prefetch;
  profile_sample(PROF_WORK, ticks);
  p.prefetch = 0;
}
void profile_margin(uint32_t half) {
  uint32_t pos = board_audio_offset();
  uint32_t frames = pos < 8192 && pos / 4096u != half ?
                    (4096u - pos % 4096u) / 8u : 0;
  p.latest_margin = frames;
  /* Current divider fits 16 bits: this weighted product cannot overflow. */
  uint32_t weight = frames * p.context[4];
  if (weight < p.slack_weight) {
    p.slack_weight = weight; p.slack = frames;
    memcpy(p.tightest, p.context, sizeof(p.tightest));
  }
}
void profile_error(uint32_t reason) { increment(p.errors + reason); }
static void append(const char *s, uint32_t *at) {
  while (*s && *at < 1009u) p.text[(*at)++] = *s++;
}
static void number(uint32_t n, uint32_t *at) {
  char digits[10]; uint32_t used = 0;
  do { digits[used++] = (char)('0' + n % 10u); n /= 10u; } while (n);
  while (used && *at < 1009u) p.text[(*at)++] = digits[--used];
}
static void field(const char *name, uint32_t n, uint32_t *at) {
  append(name, at); number(n, at); append("\n", at);
}
static void row(const char *name, const uint32_t *values, uint32_t n, uint32_t *at) {
  append(name, at);
  for (uint32_t i = 0; i < n; ++i) {
    append(",", at); number(values[i], at);
  }
  append("\n", at);
}
void profile_idle(Fat *fs, uint32_t now, uint32_t allowed) {
  if (!allowed || p.playing || !p.dirty || now - p.stopped < 250u) return;
  p.dirty = 0; /* One attempt per stop; no repeated idle write failures. */
  board_stack_info(p.stack);
  memset(p.text, ' ', sizeof(p.text));
  uint32_t at = 0;
  append("Hamburger Wavy profile 1\n", &at);
  field("build=", ONE_BUILD_NUMBER, &at);
  field("ticks_per_us=", 24, &at);
  field("plays=", p.plays, &at);
  field("timer_pair_min=", p.timer_min, &at);
  field("wall_lo=", p.wall_low, &at); field("wall_hi=", p.wall_high, &at);
  field("service_gap_max=", p.gap_max, &at);
  field("min_slack_frames=", p.slack == 0xffffffffu ? 0xffffffffu : p.slack, &at);
  field("report_failures=", p.failed_reports, &at);
  field("saturated=", p.saturated, &at);
  row("stack_static_used_free", p.stack, 3, &at);
  row("last", p.context, 5, &at); row("worst_work", p.worst, 5, &at);
  row("min_slack", p.tightest, 5, &at);
  row("errors_ring_late_dma_read_copy", p.errors, PROF_ERRORS, &at);
  row("sd_hist", p.histogram, 8, &at);
  static const char *const names[PROF_METRICS] = {"sd","prefetch","decode","envelope","copy","refill_work"};
  for (uint32_t i = 0; i < PROF_METRICS; ++i) {
    Metric *m = p.m + i;
    uint32_t fields[5] = {m->count,m->low,m->high,m->minimum,m->maximum};
    row(names[i], fields, 5, &at);
  }
  if (at >= 1009u) { increment(&p.failed_reports); return; }
  /* A fixed-position CRC detects partial/torn two-sector report updates. */
  uint32_t crc = 0xffffffffu;
  for (uint32_t i = 0; i < 1009u; ++i) {
    crc ^= (uint8_t)p.text[i];
    for (uint32_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  crc = ~crc; at = 1009;
  memcpy(p.text + at, "crc32=", 6); at += 6;
  for (uint32_t i = 0; i < 8; ++i)
    p.text[at++] = "0123456789abcdef"[(crc >> (28u - i * 4u)) & 15u];
  p.text[at] = '\n';
  if (fat_profile_write(fs, (const uint8_t *)p.text)) increment(&p.failed_reports);
}
#endif
