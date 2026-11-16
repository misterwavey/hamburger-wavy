/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ONE_SD_CRC_H
#define ONE_SD_CRC_H
#include <stdint.h>
/* Expand the x^16+x^12+x^5+1 recurrence one byte at a time. The folded
 * high byte removes both flash-table lookups; CRC checking stays enabled. */
static inline __attribute__((always_inline)) uint16_t crc16_byte(uint16_t crc, uint8_t byte) {
  uint32_t x = (crc >> 8) ^ byte;
  x ^= x >> 4;
  return (uint16_t)((crc << 8) ^ (x << 12) ^ (x << 5) ^ x);
}

#endif
