/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#ifdef ONE_FLASH_PROBE
#include "flash_probe_expected.h"
uint32_t flash_extension_mix(uint32_t);
#ifdef ONE_FLASH_BANK
uint32_t flash_extension_high(uint32_t);
#endif
int flash_probe_check(void) {
  /* Uncached alias checks what flash actually contains before executing it. */
  const volatile uint32_t *page = (const volatile uint32_t *)0xbd014000u;
#ifdef ONE_FLASH_BANK
  for (uint32_t i = 0; i < FLASH_BANK_WORDS; ++i) {
    uint32_t expected = (i * 0x9e3779b9u) ^ 0x48570017u;
    if (i < FLASH_LOW_WORDS) expected = flash_low_expected[i];
    else if (i >= FLASH_HIGH_FIRST && i < FLASH_HIGH_FIRST + FLASH_HIGH_WORDS)
      expected = flash_high_expected[i - FLASH_HIGH_FIRST];
    else if (i >= FLASH_TAIL_FIRST) expected = flash_tail_expected[i - FLASH_TAIL_FIRST];
    if (page[i] != expected) return 0;
    if ((i & 255u) == 0) board_watchdog();
  }
  if (flash_extension_high(0x13579bdfu) != FLASH_HIGH_RESULT_A ||
      flash_extension_high(0x2468ace0u) != FLASH_HIGH_RESULT_B) return 0;
#else
  for (uint32_t i = 0; i < 256; ++i)
    if (page[i] != flash_expected[i]) return 0;
#endif
  return flash_extension_mix(0x13579bdfu) == FLASH_RESULT_A &&
         flash_extension_mix(0x2468ace0u) == FLASH_RESULT_B;
}
void flash_probe_startup(void) {
  if (flash_probe_check()) return;
  /* No jump into missing code and no normal build record on failure. */
  for (;;) {
    board_led((board_millis() / 250u) & 1u ? 1u : 2u);
    board_watchdog();
  }
}
#endif
