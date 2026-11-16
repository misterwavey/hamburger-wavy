/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"

static void wait_ms(uint32_t duration) {
  uint32_t start = board_millis();
  while ((uint32_t)(board_millis() - start) < duration)
    board_watchdog();
}

void advertise_build(uint32_t number) {
  board_led(1); /* RB12: blue. */
  wait_ms(1000);
  (void)number; /* Build identification is now written to ONEBUILD.TXT. */
  board_led(0);
}
