/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *a = d;
  const uint8_t *b = s;
  while (n--)
    *a++ = *b++;
  return d;
}
void *memset(void *d, int c, size_t n) {
  uint8_t *a = d;
  while (n--)
    *a++ = (uint8_t)c;
  return d;
}
