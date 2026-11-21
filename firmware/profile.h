/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ONE_PROFILE_H
#define ONE_PROFILE_H
#include "core.h"
enum { PROF_SD, PROF_PREFETCH, PROF_DECODE, PROF_ENVELOPE, PROF_COPY,
       PROF_WORK,
#ifdef ONE_COLOUR
       PROF_COLOUR, PROF_FILTER, PROF_MOD, PROF_PHASER,
#endif
       PROF_METRICS };
enum { PROF_RING_ERROR, PROF_LATE_ERROR, PROF_DMA_ERROR, PROF_READ_ERROR,
       PROF_COPY_ERROR, PROF_ERRORS };
#ifdef ONE_PROFILE
uint32_t board_ticks(void);
void board_stack_info(uint32_t[3]);
void profile_init(void);
uint32_t profile_running(void);
void profile_start(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void profile_stop(uint32_t);
void profile_poll(void);
void profile_pitch(uint32_t);
void profile_sample(uint32_t, uint32_t);
void profile_publish(uint32_t);
void profile_margin(uint32_t);
void profile_error(uint32_t);
void profile_idle(Fat *, uint32_t, uint32_t);
#ifdef ONE_TOLERANT_ENCODER
void profile_encoder(uint32_t, uint32_t, uint32_t);
void profile_encoder_edge(uint32_t ab, uint32_t playing, uint32_t stepped);
void profile_debounce(uint32_t raw_changes, uint32_t bridged_max_us, uint32_t hold_min_us);
/* Last failed sector read: 1 command, 2 token transfer, 3 wrong token,
 * 4 data transfer, 5 CRC; value is the response or token where relevant. */
void profile_sd_fail(uint32_t reason, uint32_t value);
void profile_sd_retry(void);
/* Last counted read error: 1 prefetch, 2 flip resume, 3 flip start. */
void profile_read_fail(uint32_t site, uint32_t card_ready, uint32_t frame, uint32_t reverse);
/* Chop gates: count, then the last six as 16 ms units since the previous
 * gate (max 1023) << 11 | jumped while playing << 10 | ADC reading. */
void profile_chop(uint32_t adc, uint32_t jumped);
void profile_count(uint32_t which); /* 0 step accepted, 1 selection performed */
#endif
#define PROFILE_BEGIN(tag) uint32_t tag = board_ticks(), tag##_on = profile_running()
#define PROFILE_END(metric, tag) do { if (tag##_on) profile_sample(metric, board_ticks() - tag); } while (0)
#define PROFILE_PUBLISH(tag) do { if (tag##_on) profile_publish(board_ticks() - tag); } while (0)
#define PROFILE(call) do { call; } while (0)
#else
#define PROFILE_BEGIN(tag)
#define PROFILE_END(metric, tag) do {} while (0)
#define PROFILE_PUBLISH(tag) do {} while (0)
#define PROFILE(call) do {} while (0)
#endif
#endif
