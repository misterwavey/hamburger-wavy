/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
void ring_start(Ring *r) {
  r->half = 0;
  r->pending = 0;
  r->blocks = 0;
  r->underruns = 0;
  r->running = 1;
}
int ring_boundary(Ring *r, uint32_t half) {
  if (half > 1)
    return -1;
  if (!r->running)
    return 0;
  if (half == r->half)
    return 0;
  if (r->pending & (1u << half)) {
    r->underruns++;
    r->running = 0;
    return -1;
  }
  r->pending |= 1u << r->half;
  r->half = half;
  r->blocks++;
  return 1;
}
int ring_commit(Ring *r, uint32_t half) {
  if (half > 1 || !r->running || half == r->half ||
      !(r->pending & (1u << half)))
    return -1;
  r->pending &= ~(1u << half);
  return 0;
}
