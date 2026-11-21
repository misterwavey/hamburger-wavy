/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "profile.h"
#include "sd_crc.h"
static uint32_t clock_last, clock_ms, spi_poll;
static uint32_t count(void) {
  uint32_t value;
  __asm__ volatile("mfc0 %0,$9" : "=r"(value));
  return value;
}
uint32_t board_millis(void) {
  uint32_t current = count(), elapsed = current - clock_last,
           whole = elapsed / 24000u;
  clock_last += whole * 24000u;
  clock_ms += whole;
  return clock_ms;
}
uint32_t board_pins(void) { return REG(0xbf886120); }
void board_led(uint32_t channels) {
  REG(0xbf886134) = 0x3000;
  REG(0xbf886138) = (channels & 3u) << 12;
}
uint32_t board_adc(void) { return REG(0xbf809070) & 1023u; }
void board_pitch_mode(uint32_t value, uint32_t rate, CvMode mode) {
  uint32_t fixed = pitch_divider(value, rate, mode);
  PROFILE(profile_pitch(fixed));
  uint32_t div = fixed >> 9, trim = fixed & 511u;
  REG(0xbf80f020) &= ~0x100u;
  REG(0xbf80f020) &= ~0x8000u;
  REG(0xbf80f020) = (REG(0xbf80f020) & ~15u) | 7u;
  REG(0xbf80f020) = (REG(0xbf80f020) & 0x8000ffffu) | ((div & 32767u) << 16);
  REG(0xbf80f030) = (REG(0xbf80f030) & 0x7fffffu) | (trim << 23);
  REG(0xbf80f020) |= 0x1000;
  REG(0xbf80f020) |= 0x100;
  REG(0xbf80f020) |= 0x8000;
}
#ifdef ONE_FLIP
void board_pitch_fixed(uint32_t fixed) {
  PROFILE(profile_pitch(fixed));
  uint32_t div = fixed >> 9, trim = fixed & 511u;
  REG(0xbf80f020) &= ~0x100u;
  REG(0xbf80f020) &= ~0x8000u;
  REG(0xbf80f020) = (REG(0xbf80f020) & ~15u) | 7u;
  REG(0xbf80f020) = (REG(0xbf80f020) & 0x8000ffffu) | ((div & 32767u) << 16);
  REG(0xbf80f030) = (REG(0xbf80f030) & 0x7fffffu) | (trim << 23);
  REG(0xbf80f020) |= 0x1000;
  REG(0xbf80f020) |= 0x100;
  REG(0xbf80f020) |= 0x8000;
}
#endif

void board_pitch(uint32_t value, uint32_t rate) {
  board_pitch_mode(value, rate, CV_FREE);
}
void board_audio_stop(void) {
  REG(0xbf886138) = 0x10; /* existing board's stop/mute control */
  REG(0xbf883124) = 0x80;
  REG(0xbf883138) = 0x40;
  uint32_t start = count();
  while ((REG(0xbf883120) & 0x8000u) && count() - start < 240000u) {
  }
  REG(0xbf883134) = 0x40;
  REG(0xbf883144) = 0xff;
}
void board_audio_start(const uint32_t *data) {
  board_audio_stop();
  /* Restart the SPI request source as well as its DMA consumer. Leaving an
   * empty transmitter running can strand a newly armed DMA channel after
   * its earlier request was discarded while the channel was disabled. */
  REG(0xbf805800) = 0;
  (void)REG(0xbf805820);
  REG(0xbf805814) = 0x40;   /* clear receive overflow */
  REG(0xbf883120) = 0x12;   /* priority 2, automatic enable */
  REG(0xbf883130) = 0x2610; /* SPI1 TX request 38, SIRQEN */
  REG(0xbf883150) = (uint32_t)(uintptr_t)data & 0x1fffffffu;
  REG(0xbf883160) = 0x1f805820;
  REG(0xbf883170) = ONE_CACHE_FRAMES * 8;
  REG(0xbf883180) = 4;
  REG(0xbf8831b0) = 4;
  REG(0xbf883144) = 0xff;
  __asm__ volatile("sync" ::: "memory");
  REG(0xbf883128) = 0x80;
  REG(0xbf805800) = 0x20808870; /* request source starts after DMA is armed */
  REG(0xbf886134) = 0x10;
}
uint32_t board_audio_offset(void) { return REG(0xbf883190) & 0xffffu; }
int board_audio_commit_safe(uint32_t half) {
  uint32_t pos = board_audio_offset(), current = pos / (ONE_HALF_FRAMES * 8u);
  uint32_t left = ONE_HALF_FRAMES * 8u - (pos % (ONE_HALF_FRAMES * 8u));
  return current != half && left >= 1024u;
}
static int spi_xfer(void *ctx, uint8_t byte) {
  (void)ctx;
  if ((++spi_poll & 63u) == 0)
    app_background();
  REG(0xbf805a20) = byte;
  uint32_t start = count();
  while (!(REG(0xbf805a10) & 1u)) {
    if (count() - start > 120000u)
      return -1;
  }
  return (int)(REG(0xbf805a20) & 255u);
}
/* One outstanding byte: CRC arithmetic overlaps the next wire transfer.
 * Poll controls every 64 bytes; bound each span's wait and retain 12 MHz. */
static int spi_read_bytes(void *ctx, uint8_t *out, uint32_t length,
                          uint16_t *checksum) {
  (void)ctx;
  uint16_t crc = 0;
  if (!length) { *checksum = crc; return 0; }
  uint32_t start = count(), poll = spi_poll;
  REG(0xbf805a20) = 255;
  do {
    while (!(REG(0xbf805a10) & 1u)) {
      if (count() - start > 120000u) { spi_poll = poll; return -1; }
    }
    uint8_t value = (uint8_t)REG(0xbf805a20);
    if (--length) REG(0xbf805a20) = 255;
    *out++ = value;
    crc = crc16_byte(crc, value);
    if ((++poll & 63u) == 0) {
      app_background();
      start = count();
    }
  } while (length);
  spi_poll = poll;
  *checksum = crc;
  return 0;
}
static void spi_select(void *ctx, int selected) {
  (void)ctx;
  REG(selected ? 0xbf886134 : 0xbf886138) = 0x400;
}
static void spi_speed(void *ctx, int fast) {
  (void)ctx;
  REG(0xbf805a00) = 0;
  (void)REG(0xbf805a20);
  REG(0xbf805a14) = 0x40;
  REG(0xbf805a30) = fast ? 1u : 64u; /* 12 MHz / ~369 kHz with a 48 MHz PBCLK */
  REG(0xbf805a00) = 0x8260;          /* mode 3, as recovered from stock */
}
static uint32_t spi_millis(void *ctx) {
  (void)ctx;
  return board_millis();
}
const SdIo board_sd_io = {0, spi_xfer, spi_select, spi_speed, spi_millis, spi_read_bytes};
void board_watchdog(void) { REG(0xbf800008) = 1; }
void board_audio_publish(void) { __asm__ volatile("sync" ::: "memory"); }
void board_init(void) {
  __asm__ volatile("di\n\tehb" ::: "memory");
  REG(0xbf881060) = 0;
  REG(0xbf881070) = 0;
  REG(0xbf881080) = 0;
  REG(0xbf881030) = 0;
  REG(0xbf881040) = 0;
  REG(0xbf881050) = 0;
  REG(0xbf80f230) = 0;
  REG(0xbf80f230) = 0xaa996655;
  REG(0xbf80f230) = 0x556699aa;
  REG(0xbf80f204) = 0x2008; /* IOLOCK and JTAGEN clear */
  REG(0xbf886000) = 2;
  REG(0xbf886100) = 0;
  REG(0xbf886040) = 0;
  REG(0xbf886140) = 0; /* push-pull outputs */
  REG(0xbf886010) = 0xffff;
  /* Explicit output directions for SDO1/SS1/REFCLKO/SDO2/CS/SCK1/SCK2,
   * plus LEDs and mute. Do not inherit the loader's TRIS configuration. */
  REG(0xbf886110) = 0x0aa3;
  REG(0xbf886138) = 0x410;
  REG(0xbf886134) = 0x3000;
  /* Exact recovered PPS writes; not an inferred generic PIC32 pin assignment.
   */
  REG(0xbf80fb44) = 7;
  REG(0xbf80fb34) = 3;
  REG(0xbf80fb38) = 3;
  REG(0xbf80fb54) = 4;
  REG(0xbf80fb4c) = 4;
  REG(0xbf80fa90) = 2;
  REG(0xbf80f230) = 0;
  REG(0xbf809000) = 0;
  REG(0xbf809010) = 0x404;
  REG(0xbf809020) = 0x104;
  REG(0xbf809050) = 2;
  REG(0xbf809000) = 0x84e4;
  REG(0xbf805800) = 0;
  REG(0xbf805840) = 0x80;
  REG(0xbf805830) = 1;
  (void)REG(0xbf805820);
  REG(0xbf805814) = 0x40;
  board_pitch(32, 48000);
  REG(0xbf805800) = 0x20808870;
  REG(0xbf883000) = 0x8000;
  for (uint32_t channel = 0; channel < 4; channel++) {
    uint32_t base = 0xbf883060u + channel * 0xc0u;
    REG(base + 4) = 0x80;
    REG(base + 0x18) = 0x40;
    uint32_t start = count();
    while ((REG(base + 0x10) & 0x40) && count() - start < 240000u) {
    }
    REG(base + 0x14) = 0x40;
    REG(base + 0x20) = 0;
    REG(base + 0x24) = 0xff;
    REG(base) = 0;
    REG(base + 0x10) = 0;
  }
  REG(0xbf883120) = 2;
  REG(0xbf883130) = 0x2610;
  REG(0xbf883140) = 0;
  board_audio_stop();
  clock_last = count();
  clock_ms = 0;
}
void standalone_fault(void) {
  __asm__ volatile("di\n\tehb" ::: "memory");
  REG(0xbf883124) = 0x80;
  REG(0xbf886138) = 0x10;
  for (;;) {
    REG(0xbf800008) = 1;
    board_led((count() >> 22) & 1u);
  }
}

#if defined(ONE_TOLERANT_ENCODER) && !defined(ONE_PROFILE)
uint32_t board_ticks(void) { return count(); }
#endif
#ifdef ONE_PROFILE
uint32_t board_ticks(void) { return count(); }
void board_stack_info(uint32_t out[3]) {
  extern uint32_t __bss_end;
  uintptr_t scan = (uintptr_t)&__bss_end;
  const uintptr_t top = 0xa000fff8u;
  while (scan < top && *(volatile uint32_t *)scan == 0xa55a5aa5u) scan += 4;
  out[0] = (uint32_t)((uintptr_t)&__bss_end - 0xa0000000u);
  out[1] = (uint32_t)(top - (uintptr_t)scan);
  out[2] = (uint32_t)((uintptr_t)scan - (uintptr_t)&__bss_end);
}
#endif
