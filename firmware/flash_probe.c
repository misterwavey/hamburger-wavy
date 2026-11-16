/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#ifdef ONE_FLASH_PROBE
#include "flash_probe_expected.h"
uint32_t flash_extension_mix(uint32_t);
int flash_probe_check(void) {
  /* Uncached alias checks what flash actually contains before executing it. */
  const volatile uint32_t *page = (const volatile uint32_t *)0xbd014000u;
  for (uint32_t i = 0; i < 256; ++i)
    if (page[i] != flash_expected[i]) return 0;
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
