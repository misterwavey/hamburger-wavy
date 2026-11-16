/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ONE_PROFILE_H
#define ONE_PROFILE_H
#include "core.h"
enum { PROF_SD, PROF_PREFETCH, PROF_DECODE, PROF_ENVELOPE, PROF_COPY,
       PROF_WORK, PROF_METRICS };
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
