/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
/* may_alias permits word access to arbitrary byte/structure representations. */
typedef uint32_t __attribute__((may_alias)) CopyWord;
void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *a = d;
  const uint8_t *b = s;
  if ((((uintptr_t)a | (uintptr_t)b) & 3u) == 0) {
    CopyWord *words = (CopyWord *)a;
    const CopyWord *source = (const CopyWord *)b;
    while (n >= 4u) { *words++ = *source++; n -= 4u; }
    a = (uint8_t *)words; b = (const uint8_t *)source;
  }
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

/* Non-overlapping, word-aligned audio arrays. General memcpy stays byte-safe. */
void audio_copy(uint32_t *dst, const uint32_t *src, uint32_t words) {
  while (words >= 4u) {
    uint32_t a=src[0], b=src[1], c=src[2], d=src[3];
    dst[0]=a; dst[1]=b; dst[2]=c; dst[3]=d;
    src+=4; dst+=4; words-=4;
  }
  while (words--) *dst++=*src++;
}
