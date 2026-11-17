/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ONE_CV_CAPTURE_H
#define ONE_CV_CAPTURE_H
#include "core.h"
#define CV_ROWS 12u
#define CV_WINDOW_MS 5000u
#define CV_SETTLE_MS 500u
#define CV_REPORT_MAGIC "Hamburger Wavy CV 1\n"
typedef struct {
  uint32_t n, minimum, maximum, sum, ss_low, ss_high, at0, at1023, elapsed, missed;
} CvRow;
void cv_stats_init(CvRow *);
int cv_stats_add(CvRow *, uint32_t);
int cv_format_report(uint8_t[1024], const CvRow *, uint32_t, uint32_t);
void cv_diag_init(void);
void cv_diag_tick(void);
#endif
