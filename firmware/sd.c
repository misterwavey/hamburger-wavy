/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
#include "profile.h"
static int tx(Sd *s, uint8_t v) { return s->io->xfer(s->io->ctx, v); }
static void release(Sd *s) {
  s->io->select(s->io->ctx, 0);
  (void)tx(s, 255);
}
static int wait_ready(Sd *s, uint32_t ms) {
  uint32_t begin = s->io->millis(s->io->ctx);
  do {
    int b = tx(s, 255);
    if (b == 255)
      return 0;
    if (b < 0)
      return -1;
  } while (s->io->millis(s->io->ctx) - begin < ms);
  return -1;
}
static uint8_t crc7(const uint8_t *b) {
  uint8_t crc = 0;
  for (unsigned i = 0; i < 5; i++)
    for (unsigned k = 0; k < 8; k++) {
      uint8_t bit = (uint8_t)((b[i] >> (7 - k)) & 1u);
      uint8_t top = (uint8_t)((crc >> 6) & 1u);
      crc = (uint8_t)((crc << 1) & 127u);
      if (bit != top)
        crc ^= 9;
    }
  return (uint8_t)((crc << 1) | 1u);
}
static int command(Sd *s, uint8_t cmd, uint32_t arg) {
  release(s);
  s->io->select(s->io->ctx, 1);
  if (wait_ready(s, 500))
    return -1;
  uint8_t p[5] = {(uint8_t)(64u | cmd), (uint8_t)(arg >> 24),
                  (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg};
  for (unsigned i = 0; i < 5; i++)
    if (tx(s, p[i]) < 0)
      return -1;
  if (tx(s, crc7(p)) < 0)
    return -1;
  for (unsigned i = 0; i < 16; i++) {
    int r = tx(s, 255);
    if (r < 0)
      return -1;
    if (!(r & 128))
      return r;
  }
  return -1;
}
int sd_init(Sd *s, const SdIo *io) {
  s->io = io;
  s->ready = 0;
  s->high_capacity = 0;
  io->speed(io->ctx, 0);
  io->select(io->ctx, 0);
  for (unsigned i = 0; i < 10; i++)
    if (tx(s, 255) < 0)
      goto fail;
  if (command(s, 0, 0) != 1)
    goto fail;
  int r = command(s, 8, 0x1aa);
  int modern = 0;
  if (r == 1) {
    int a = tx(s, 255), b = tx(s, 255), c = tx(s, 255), d = tx(s, 255);
    if (a < 0 || b < 0 || c != 1 || d != 0xaa)
      goto fail;
    modern = 1;
  } else if (r != 5)
    goto fail;
  /* Request CRC verification explicitly before ACMD41 (SPI specification). */
  r = command(s, 59, 1);
  if (r < 0 || r > 1)
    goto fail;
  uint32_t begin = io->millis(io->ctx);
  do {
    r = command(s, 55, 0);
    if (r < 0 || r > 1)
      goto fail;
    r = command(s, 41, modern ? 0x40000000u : 0);
    if (r == 0)
      break;
    if (r != 1)
      goto fail;
  } while (io->millis(io->ctx) - begin < 1500);
  if (r)
    goto fail;
  if (modern) {
    if (command(s, 58, 0))
      goto fail;
    int ocr = tx(s, 255);
    if (ocr < 0 || !(ocr & 128))
      goto fail;
    for (unsigned i = 0; i < 3; i++)
      if (tx(s, 255) < 0)
        goto fail;
    s->high_capacity = (ocr & 64) != 0;
  }
  if (!s->high_capacity && command(s, 16, 512))
    goto fail;
  release(s);
  io->speed(io->ctx, 1);
  s->ready = 1;
  return 0;
fail:
  release(s);
  return -1;
}
static uint16_t crc16_byte(uint16_t crc, uint8_t byte) {
  static const uint16_t table[16] = {
      0,      0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
      0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef};
  crc = (uint16_t)((crc << 4) ^ table[(crc >> 12) ^ (byte >> 4)]);
  return (uint16_t)((crc << 4) ^ table[(crc >> 12) ^ (byte & 15u)]);
}
int sd_read(void *ctx, uint32_t lba, uint8_t *out) {
  Sd *s = ctx;
  if (!s->ready)
    return -1;
  if (!s->high_capacity && lba > 0x7fffffu)
    return -1;
  PROFILE_BEGIN(read_sector);
  if (command(s, 17, s->high_capacity ? lba : lba << 9))
    goto fail;
  uint32_t begin = s->io->millis(s->io->ctx);
  int token;
  do {
    token = tx(s, 255);
    if (token < 0)
      goto fail;
    if (token != 255)
      break;
  } while (s->io->millis(s->io->ctx) - begin < 250);
  if (token != 0xfe)
    goto fail;
  uint16_t crc = 0;
  for (unsigned i = 0; i < 512; i++) {
    int b = tx(s, 255);
    if (b < 0)
      goto fail;
    out[i] = (uint8_t)b;
    crc = crc16_byte(crc, (uint8_t)b);
  }
  int high = tx(s, 255), low = tx(s, 255);
  if (high < 0 || low < 0 || crc != ((uint16_t)high << 8 | (uint16_t)low))
    goto fail;
  release(s);
  PROFILE_END(PROF_SD, read_sector);
  return 0;
fail:
  release(s);
  s->ready = 0;
  PROFILE_END(PROF_SD, read_sector);
  return -1;
}

/* One CRC-protected sector. Callers only write state data / its allocation. */
int sd_write(void *ctx, uint32_t lba, const uint8_t *data) {
  Sd *s = ctx;
  if (!s->ready || (!s->high_capacity && lba > 0x7fffffu))
    return -1;
  if (command(s, 24, s->high_capacity ? lba : lba << 9))
    goto fail;
  if (tx(s, 255) < 0 || tx(s, 0xfe) < 0)
    goto fail;
  uint16_t crc = 0;
  for (uint32_t i = 0; i < 512; i++) {
    crc = crc16_byte(crc, data[i]);
    if (tx(s, data[i]) < 0)
      goto fail;
  }
  if (tx(s, crc >> 8) < 0 || tx(s, (uint8_t)crc) < 0)
    goto fail;
  int response = -1;
  for (uint32_t i = 0; i < 16; i++) {
    response = tx(s, 255);
    if (response < 0)
      goto fail;
    if (response != 255)
      break;
  }
  if ((response & 31) != 5 || wait_ready(s, 500))
    goto fail;
  /* R2 status catches write-protect/program errors after the accepted token. */
  if (command(s, 13, 0) || tx(s, 255) != 0)
    goto fail;
  release(s);
  return 0;
fail:
  release(s);
  /* A write-protected card can still be read. Do not disable audio reads. */
  return -1;
}
