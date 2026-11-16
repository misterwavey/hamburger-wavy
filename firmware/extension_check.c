/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#ifdef ONE_EXTENDED_APP
#include "extension_expected.h"
__attribute__((noinline))
static uint32_t extension_page_crc(const volatile uint8_t *data) {
  uint32_t crc = 0xffffffffu;
  for (uint32_t i = 0; i < 1024; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
int extension_check(void) {
  const volatile uint8_t *bank = (const volatile uint8_t *)0xbd014000u;
  const volatile uint32_t *expected = extension_crc;
  for (uint32_t page = 0; page < 48; ++page) {
    if (extension_page_crc(bank + page * 1024u) != expected[page]) return 0;
    board_watchdog();
  }
  return 1;
}
void extension_startup(void) {
  if (extension_check()) return;
  for (;;) {
    board_led((board_millis() / 250u) & 1u ? 1u : 2u);
    board_watchdog();
  }
}
#endif
