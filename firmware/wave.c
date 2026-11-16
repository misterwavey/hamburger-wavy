/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
static uint32_t u16(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p) { return u16(p) | (u16(p + 2) << 16); }
static int exact(File *f, void *p, uint32_t n) {
  uint32_t got;
  return file_read(f, p, n, &got) || got != n ? -1 : 0;
}
int wave_open(File *f, Wave *w) {
  uint8_t h[40];
  memset(w, 0, sizeof(*w));
  if (file_seek(f, 0) || exact(f, h, 12))
    return -1;
  if (u32(h) != 0x46464952u || u32(h + 8) != 0x45564157u)
    return -1;
  uint32_t bytes = u32(h + 4);
  if (bytes < 4 || bytes > f->size - 8)
    return -1;
  uint32_t end = bytes + 8, have_fmt = 0, have_data = 0, pos = 12;
  for (unsigned chunks = 0; chunks < 1024 && pos <= end && end - pos >= 8;
       chunks++) {
    if (file_seek(f, pos) || exact(f, h, 8))
      return -1;
    uint32_t id = u32(h), n = u32(h + 4), body = pos + 8;
    if (n > end - body)
      return -1;
    if (id == 0x20746d66u) {
      if (have_fmt || n < 16)
        return -1;
      uint32_t take = n < 40 ? n : 40;
      if (exact(f, h, take))
        return -1;
      uint32_t tag = u16(h), channels = u16(h + 2), rate = u32(h + 4),
               align = u16(h + 12), bits = u16(h + 14);
      if (tag == 0xfffe) {
        static const uint8_t guid[16] = {1,   0, 0, 0,   0, 0,  16,  0,
                                         128, 0, 0, 170, 0, 56, 155, 113};
        if (n < 40 || u16(h + 16) < 22 || u16(h + 18) != bits)
          return -1;
        for (unsigned i = 0; i < 16; i++)
          if (h[24 + i] != guid[i])
            return -1;
      } else if (tag != 1)
        return -1;
      if (channels != 1 || (bits != 16 && bits != 24) || align != bits / 8 ||
          rate < 8000 || rate > 96000 || u32(h + 8) != rate * align)
        return -1;
      w->bits = bits;
      w->rate = rate;
      have_fmt = 1;
    } else if (id == 0x61746164u && !have_data) {
      w->offset = body;
      w->bytes = n;
      have_data = 1;
    }
    if (have_fmt && have_data) {
      if (w->bytes % (w->bits / 8))
        return -1;
      w->remaining = w->bytes;
      return file_seek(f, w->offset);
    }
    if ((n & 1) && n == end - body)
      return -1;
    pos = body + n + (n & 1u);
  }
  return -1;
}
int pcm_decode(const uint8_t *src, uint32_t bits, uint32_t *dst,
               uint32_t frames) {
  if (bits != 16 && bits != 24)
    return -1;
  for (uint32_t i = 0; i < frames; i++) {
    uint32_t word = bits == 16
                        ? ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 24)
                        : ((uint32_t)src[0] << 8) | ((uint32_t)src[1] << 16) |
                              ((uint32_t)src[2] << 24);
    dst[i * 2] = dst[i * 2 + 1] = word;
    src += bits / 8;
  }
  return 0;
}
int wave_read(File *f, Wave *w, uint32_t *dst, uint32_t frames, uint32_t *got,
              uint8_t *scratch, uint32_t capacity) {
  *got = 0;
  if (w->bits != 16 && w->bits != 24)
    return -1;
  uint32_t width = w->bits / 8;
  if (frames > capacity / width)
    return -1;
  uint32_t count = w->remaining / width;
  if (count > frames)
    count = frames;
  uint32_t bytes = count * width, n;
  if (file_read(f, scratch, bytes, &n) || n != bytes)
    return -1;
  if (pcm_decode(scratch, w->bits, dst, count))
    return -1;
  memset(dst + count * 2, 0, (frames - count) * 8);
  w->remaining -= bytes;
  *got = count;
  return 0;
}

int wave_seek_frame(File *f, Wave *w, uint32_t frame) {
  if ((w->bits != 16 && w->bits != 24) || frame > w->bytes / (w->bits / 8))
    return -1;
  uint32_t skip = frame * (w->bits / 8);
  if (file_seek(f, w->offset + skip))
    return -1;
  w->remaining = w->bytes - skip;
  return 0;
}
int wave_read_loop(File *f, Wave *w, uint32_t *dst, uint32_t frames,
                   uint8_t *scratch, uint32_t capacity) {
  if (!w->bytes || (w->bits != 16 && w->bits != 24))
    return -1;
  while (frames) {
    if (!w->remaining && wave_seek_frame(f, w, 0))
      return -1;
    uint32_t got;
    if (wave_read(f, w, dst, frames, &got, scratch, capacity) || !got)
      return -1;
    dst += got * 2;
    frames -= got;
  }
  return 0;
}
