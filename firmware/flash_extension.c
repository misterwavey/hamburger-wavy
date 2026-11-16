/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
#ifdef ONE_FLASH_PROBE
/* Executed only after low-flash code verifies the entire new page. */
__attribute__((section(".flash_probe"), noinline))
uint32_t flash_extension_mix(uint32_t value) {
  for (uint32_t i = 0; i < 32; ++i)
    value = value * 1664525u + 1013904223u;
  return value ^ 0x48574156u;
}
/* Last row/word coverage is checked too, not only the first code words. */
__attribute__((section(".flash_probe_tail"), used))
const uint32_t flash_extension_tail[4] = {
  0x48574156u, 0x00000016u, 0xa55a3cc3u, 0x5aa5c33cu
};
#endif
