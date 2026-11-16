/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
#ifdef ONE_FLASH_PROBE
/* Executed only after low-flash code verifies the entire new page. */
__attribute__((section(".flash_probe"), noinline))
uint32_t flash_extension_mix(uint32_t value) {
  for (uint32_t i = 0; i < 32; ++i)
    value = value * 1664525u + 1013904223u;
#ifdef ONE_FLASH_BANK
  return value ^ 0x48574157u;
#else
  return value ^ 0x48574156u;
#endif
}
#ifdef ONE_FLASH_BANK
__attribute__((section(".flash_probe_high"), noinline))
uint32_t flash_extension_high(uint32_t value) {
  for (uint32_t i = 0; i < 16; ++i) {
    value = (value << 5) | (value >> 27);
    value ^= 0x9e3779b9u + i;
  }
  return value + 0x17000023u;
}
#endif
/* Last row/word coverage is checked too, not only the first code words. */
__attribute__((section(".flash_probe_tail"), used))
const uint32_t flash_extension_tail[4] = {
  #ifdef ONE_FLASH_BANK
  0x48574157u, 0x00000017u, 0x23a53cc3u, 0x5aa5c323u
#else
  0x48574156u, 0x00000016u, 0xa55a3cc3u, 0x5aa5c33cu
#endif
};
#endif
