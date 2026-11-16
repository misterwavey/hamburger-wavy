/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v) {
  for (uint32_t i = 0; i < 4; i++)
    p[i] = (uint8_t)(v >> (i * 8));
}
static uint32_t crc32(const uint8_t *p) {
  uint32_t crc = 0xffffffffu;
  for (uint32_t i = 0; i < 508; i++) {
    crc ^= p[i];
    for (uint32_t j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
static int valid(const uint8_t *p) {
  const char *magic = "ONESTAT1";
  for (uint32_t i = 0; i < 8; i++)
    if (p[i] != (uint8_t)magic[i])
      return 0;
  return get32(p + 508) == crc32(p) && get32(p + 12) < ONE_MAX_FILES &&
         p[29] == 0;
}
static int name_equal(const char *a, const char *b) {
  for (uint32_t i = 0; i < 14; i++) {
    uint8_t x = (uint8_t)a[i], y = (uint8_t)b[i];
    if (x >= 'a' && x <= 'z')
      x -= 32;
    if (y >= 'a' && y <= 'z')
      y -= 32;
    if (x != y)
      return 0;
    if (!x)
      return 1;
  }
  return 0;
}
int recall_open(Recall *s, Fat *f, const Entry *entries, uint32_t count,
                uint32_t *index) {
  memset(s, 0, sizeof(*s));
  s->saved_index = 0xffffffffu;
  if (fat_state_area(f, s->lba))
    return -1;
  uint8_t data[512];
  uint32_t have = 0, known = 0;
  for (uint32_t slot = 0; slot < 2; slot++) {
    if (f->read(f->ctx, s->lba[slot], data))
      return -1;
    if (valid(data)) {
      known = 1;
      uint32_t seq = get32(data + 8);
      if (!have || (int32_t)(seq - s->sequence) > 0) {
        have = 1;
        s->sequence = seq;
        s->slot = slot;
        s->saved_index = get32(data + 12);
        s->size = get32(data + 32);
        memcpy(s->name, data + 16, 14);
      }
    } else {
      uint32_t zero = 1;
      for (uint32_t i = 0; i < 512; i++)
        if (data[i])
          zero = 0;
      if (zero)
        known = 1;
    }
  }
  /* Do not overwrite an unrelated existing file with the reserved name. */
  s->available = known && f->write != 0;
  if (!have)
    return 0;
  uint32_t preferred = s->saved_index;
  if (preferred < count && entries[preferred].size == s->size &&
      name_equal(entries[preferred].name, s->name)) {
    *index = preferred;
    return 1;
  }
  for (uint32_t i = 0; i < count; i++) {
    if (entries[i].size == s->size && name_equal(entries[i].name, s->name)) {
      *index = i;
      return 1;
    }
  }
  return 0;
}
int recall_save(Recall *s, Fat *f, const Entry *e, uint32_t index) {
  if (!s->available || !f->write || index >= ONE_MAX_FILES)
    return -1;
  if (s->saved_index == index && s->size == e->size &&
      name_equal(s->name, e->name))
    return 0;
  uint8_t data[512], check[512];
  memset(data, 0, sizeof(data));
  memcpy(data, "ONESTAT1", 8);
  put32(data + 8, s->sequence + 1);
  put32(data + 12, index);
  for (uint32_t i = 0; i < 13 && e->name[i]; i++)
    data[16 + i] = (uint8_t)e->name[i];
  put32(data + 32, e->size);
  put32(data + 508, crc32(data));
  uint32_t slot = s->slot ^ 1u;
  f->cache_lba = f->fat_lba = 0xffffffffu;
  if (f->write(f->ctx, s->lba[slot], data) ||
      f->read(f->ctx, s->lba[slot], check))
    goto fail;
  for (uint32_t i = 0; i < 512; i++)
    if (check[i] != data[i])
      goto fail;
  s->slot = slot;
  s->sequence++;
  s->saved_index = index;
  s->size = e->size;
  memcpy(s->name, data + 16, 14);
  return 0;
fail:
  s->available = 0; /* Retry only after remount, never stall every trigger. */
  return -1;
}
