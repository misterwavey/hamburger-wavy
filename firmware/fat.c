/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
static uint32_t u16(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p) { return u16(p) | (u16(p + 2) << 16); }
static int sector(Fat *f, uint32_t lba) {
  if (lba < f->start || lba - f->start >= f->total)
    return -1;
  if (f->cache_lba != lba) {
    f->cache_lba = 0xffffffffu;
    if (f->read(f->ctx, lba, f->cache))
      return -1;
    f->cache_lba = lba;
  }
  return 0;
}
static int bpb(const uint8_t *p) {
  return p[510] == 85 && p[511] == 170 && u16(p + 11) == 512 && p[13] &&
         !(p[13] & (p[13] - 1)) && p[13] <= 128 && u16(p + 14) && p[16] &&
         p[16] <= 2;
}
/* Mount-time parsing is kept compact; streaming reads retain speed tuning. */
#if defined(__clang__)
__attribute__((minsize))
#endif
int fat_mount(Fat *f, BlockRead read, void *ctx) {
  memset(f, 0, sizeof(*f));
  f->read = read;
  f->ctx = ctx;
  f->cache_lba = f->fat_lba = 0xffffffffu;
  if (read(ctx, 0, f->cache))
    return -1;
  uint32_t partition_size = 0;
  if (!bpb(f->cache)) {
    if (f->cache[510] != 85 || f->cache[511] != 170)
      return -1;
    int found = 0;
    for (unsigned i = 0; i < 4; i++) {
      const uint8_t *p = f->cache + 446 + i * 16;
      uint8_t type = p[4];
      if (type == 4 || type == 6 || type == 14 || type == 11 || type == 12) {
        f->start = u32(p + 8);
        partition_size = u32(p + 12);
        found = 1;
        break;
      }
    }
    if (!found || !f->start || !partition_size ||
        partition_size > 0xffffffffu - f->start ||
        read(ctx, f->start, f->cache) || !bpb(f->cache))
      return -1;
  }
  uint8_t *p = f->cache;
  f->total = u16(p + 19);
  if (!f->total)
    f->total = u32(p + 32);
  if (!f->total || f->total > 0xffffffffu - f->start ||
      (partition_size && f->total > partition_size))
    return -1;
  f->spc = p[13];
  uint32_t reserved = u16(p + 14), nfats = p[16];
  f->fat_sectors = u16(p + 22);
  if (!f->fat_sectors)
    f->fat_sectors = u32(p + 36);
  f->root_entries = u16(p + 17);
  uint32_t root_sectors = (f->root_entries * 32u + 511u) >> 9;
  if (!f->fat_sectors || f->fat_sectors > (f->total - reserved) / nfats ||
      reserved >= f->total)
    return -1;
  uint32_t overhead = reserved + nfats * f->fat_sectors;
  if (root_sectors >= f->total - overhead)
    return -1;
  overhead += root_sectors;
  f->clusters = (f->total - overhead) / f->spc;
  if (f->clusters < 4085)
    return -1;
  f->type = f->clusters < 65525 ? 16 : 32;
  if (f->clusters > 0x0fffffedu)
    return -1;
  uint32_t entries_per_sector = f->type == 16 ? 256 : 128;
  if ((f->clusters + 2 + entries_per_sector - 1) / entries_per_sector >
      f->fat_sectors)
    return -1;
  f->fat = f->fat_base = f->start + reserved;
  f->fat_copies = nfats;
  f->mirrored = 1;
  f->reserved = reserved;
  f->data = f->start + overhead;
  if (f->type == 32) {
    if (f->root_entries || u16(p + 22) || u16(p + 42))
      return -1;
    f->fsinfo = u16(p + 48);
    f->backup_boot = u16(p + 50);
    uint32_t ext = u16(p + 40);
    if (ext & 128) {
      f->mirrored = 0;
      uint32_t active = ext & 15;
      if (active >= nfats)
        return -1;
      f->fat += active * f->fat_sectors;
    }
    f->root = u32(p + 44) & 0x0fffffffu;
    if (f->root < 2 || f->root - 2 >= f->clusters)
      return -1;
  } else {
    if (!f->root_entries || !u16(p + 22))
      return -1;
    f->root = f->start + reserved + nfats * f->fat_sectors;
  }
  f->cache_lba = f->fat_lba = 0xffffffffu;
  return 0;
}
static int next_cluster(Fat *f, uint32_t cluster, uint32_t *out) {
  if (cluster < 2 || cluster - 2 >= f->clusters)
    return -1;
  uint32_t width = f->type == 16 ? 2 : 4, offset = cluster * width,
           lba = f->fat + (offset >> 9);
  if (f->fat_lba != lba) {
    f->fat_lba = 0xffffffffu;
    if (f->read(f->ctx, lba, f->fat_cache))
      return -1;
    f->fat_lba = lba;
  }
  uint32_t value = f->type == 16
                       ? u16(f->fat_cache + (offset & 511))
                       : u32(f->fat_cache + (offset & 511)) & 0x0fffffffu;
  if (value >= (f->type == 16 ? 0xfff8u : 0x0ffffff8u)) {
    *out = 0;
    return 0;
  }
  if (value < 2 || value - 2 >= f->clusters)
    return -1;
  *out = value;
  return 0;
}
static int cluster_lba(Fat *f, uint32_t cluster, uint32_t *out) {
  if (cluster < 2 || cluster - 2 >= f->clusters)
    return -1;
  *out = f->data + (cluster - 2) * f->spc;
  return 0;
}
int file_open(File *r, Fat *f, const Entry *e) {
  if (e->size && (e->cluster < 2 || e->cluster - 2 >= f->clusters))
    return -1;
  r->fs = f;
  r->first = r->cluster = e->cluster;
  r->cluster_index = r->pos = 0;
  r->size = e->size;
  return 0;
}
int file_seek(File *r, uint32_t pos) {
  if (pos > r->size)
    return -1;
  if (pos < r->pos) {
    r->cluster = r->first;
    r->cluster_index = 0;
  }
  r->pos = pos;
  return 0;
}
int file_read(File *r, void *output, uint32_t length, uint32_t *got) {
  Fat *f = r->fs;
  uint8_t *out = output;
  *got = 0;
  if (r->pos > r->size)
    return -1;
  if (length > r->size - r->pos)
    length = r->size - r->pos;
  while (length) {
    uint32_t index = (r->pos >> 9) / f->spc;
    if (index >= f->clusters)
      return -1;
    while (r->cluster_index < index) {
      uint32_t next;
      if (next_cluster(f, r->cluster, &next) || !next)
        return -1;
      r->cluster = next;
      r->cluster_index++;
    }
    uint32_t lba;
    if (cluster_lba(f, r->cluster, &lba))
      return -1;
    lba += (r->pos >> 9) % f->spc;
    if (sector(f, lba))
      return -1;
    uint32_t offset = r->pos & 511, count = 512 - offset;
    if (count > length)
      count = length;
    memcpy(out, f->cache + offset, count);
    out += count;
    r->pos += count;
    *got += count;
    length -= count;
  }
  return 0;
}
static uint8_t upper(uint8_t c) {
  return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c;
}
typedef struct {
  uint32_t valid;
  uint8_t checksum;
  char name[14];
} LongName;
static int name_equal(const char *a, const char *b) {
  while (*a && upper((uint8_t)*a) == upper((uint8_t)*b)) {
    a++;
    b++;
  }
  return !*a && !*b;
}
static void long_name(LongName *l, const uint8_t *p) {
  l->valid = 0;
  /* ONE's documented 13-character names fit in one VFAT name entry.
   * Longer or non-ASCII names remain accessible via their short alias. */
  if (p[0] != 0x41 || p[12] || u16(p + 26))
    return;
  static const uint8_t offsets[13] = {1,  3,  5,  7,  9,  14, 16,
                                      18, 20, 22, 24, 28, 30};
  uint32_t end = 0;
  memset(l->name, 0, sizeof(l->name));
  for (uint32_t i = 0; i < 13; i++) {
    uint32_t c = u16(p + offsets[i]);
    if (!c || c == 0xffff) {
      end = 1;
      continue;
    }
    if (end || c > 127)
      return;
    l->name[i] = (char)c;
  }
  l->checksum = p[13];
  l->valid = l->name[0] != 0;
}
static int add_entry(Fat *f, const uint8_t *p, Entry *out, uint32_t *count,
                     uint32_t max, const char *wanted, LongName *l) {
  if (p[11] == 15 && p[0] != 0xe5 && p[0]) {
    long_name(l, p);
    return 0;
  }
  uint32_t use_long = l->valid;
  l->valid = 0;
  if (!p[0])
    return 1;
  if (p[0] == 0xe5 || p[0] == '.' || (p[11] & 0x1e) || p[11] == 15)
    return 0;
  if (!wanted &&
      (upper(p[8]) != 'W' || upper(p[9]) != 'A' || upper(p[10]) != 'V'))
    return 0;
  if (*count >= max)
    return 1;
  Entry *e = out + *count;
  e->cluster = u16(p + 26);
  if (f->type == 32)
    e->cluster |= u16(p + 20) << 16;
  e->cluster &= 0x0fffffffu;
  e->size = u32(p + 28);
  e->attr = p[11];
  if ((!wanted && !e->size) ||
      (e->size && (e->cluster < 2 || e->cluster - 2 >= f->clusters)))
    return 0;
  unsigned n = 0;
  for (unsigned i = 0; i < 8 && p[i] != ' '; i++)
    e->name[n++] = (char)p[i];
  e->name[n++] = '.';
  for (unsigned i = 8; i < 11; i++)
    e->name[n++] = (char)p[i];
  e->name[n] = 0;
  uint8_t checksum = 0;
  if (use_long) {
    for (uint32_t i = 0; i < 11; i++)
      checksum = (uint8_t)(((checksum & 1u) << 7) + (checksum >> 1) + p[i]);
    use_long = checksum == l->checksum;
  }
  int matched = !wanted || name_equal(wanted, e->name);
  if (use_long && (!wanted || name_equal(wanted, l->name))) {
    memcpy(e->name, l->name, sizeof(e->name));
    matched = 1;
  }
  if (!matched)
    return 0;
  (*count)++;
  return wanted != 0;
}
static int scan(Fat *f, Entry *out, uint32_t max, const char *wanted) {
  uint32_t count = 0, cluster = f->root, hops = 0;
  LongName pending;
  memset(&pending, 0, sizeof(pending));
  if (f->type == 16) {
    for (uint32_t i = 0; i < f->root_entries; i++) {
      if (sector(f, f->root + (i >> 4)))
        return -1;
      if (add_entry(f, f->cache + (i & 15) * 32, out, &count, max, wanted,
                    &pending))
        break;
    }
  } else {
    while (cluster) {
      if (++hops > f->clusters || hops > 4096u / f->spc)
        return -1;
      uint32_t lba;
      if (cluster_lba(f, cluster, &lba))
        return -1;
      for (uint32_t s = 0; s < f->spc; s++) {
        if (sector(f, lba + s))
          return -1;
        for (unsigned i = 0; i < 16; i++)
          if (add_entry(f, f->cache + i * 32, out, &count, max, wanted,
                        &pending))
            return (int)count;
      }
      if (next_cluster(f, cluster, &cluster))
        return -1;
    }
  }
  return (int)count;
}

int fat_list(Fat *f, Entry *out, uint32_t max) { return scan(f, out, max, 0); }
int fat_find(Fat *f, const char *name, Entry *out) {
  return scan(f, out, 1, name);
}

/* Reserved files only: recall, running-build text, optional profiling report.
 * No general file writing API. */
static void put16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
  put16(p, v);
  put16(p + 2, v >> 16);
}
static int write_sector(Fat *f, uint32_t lba, const uint8_t *p) {
  if (!f->write || lba < f->start || lba - f->start >= f->total)
    return -1;
  f->cache_lba = f->fat_lba = 0xffffffffu;
  return f->write(f->ctx, lba, p);
}
static int root_sector(Fat *f, uint32_t sector_index, uint32_t *lba) {
  if (f->type == 16) {
    if (sector_index >= (f->root_entries + 15u) / 16u)
      return 1;
    *lba = f->root + sector_index;
    return 0;
  }
  if (sector_index >= 4096u || sector_index / f->spc >= f->clusters)
    return -1;
  uint32_t cluster = f->root;
  for (uint32_t n = 0; n < sector_index / f->spc; n++)
    if (next_cluster(f, cluster, &cluster))
      return -1;
    else if (!cluster)
      return 1;
  if (cluster_lba(f, cluster, lba))
    return -1;
  *lba += sector_index % f->spc;
  return 0;
}
static int fat_value(Fat *f, uint32_t cluster, uint32_t *value) {
  uint32_t width = f->type == 16 ? 2u : 4u;
  uint32_t offset = cluster * width;
  if (sector(f, f->fat + (offset >> 9)))
    return -1;
  *value = f->type == 16 ? u16(f->cache + (offset & 511u))
                         : u32(f->cache + (offset & 511u)) & 0x0fffffffu;
  return 0;
}
static int fat_set(Fat *f, uint32_t cluster, uint32_t value) {
  uint32_t width = f->type == 16 ? 2u : 4u, offset = cluster * width;
  uint32_t copies = f->mirrored ? f->fat_copies : 1;
  for (uint32_t i = 0; i < copies; i++) {
    uint32_t lba = (f->mirrored ? f->fat_base + i * f->fat_sectors : f->fat) +
                   (offset >> 9);
    if (sector(f, lba))
      return -1;
    uint8_t *p = f->cache + (offset & 511u);
    if (width == 2)
      put16(p, value);
    else
      put32(p, (u32(p) & 0xf0000000u) | (value & 0x0fffffffu));
    if (write_sector(f, lba, f->cache))
      return -1;
  }
  return 0;
}
static int invalidate_fsinfo(Fat *f) {
  if (f->type != 32 || !f->fsinfo || f->fsinfo >= f->reserved)
    return 0;
  for (uint32_t i = 0; i < 2; i++) {
    uint32_t rel = f->fsinfo;
    if (i) {
      if (!f->backup_boot || f->backup_boot >= f->reserved ||
          f->fsinfo >= f->reserved - f->backup_boot)
        break;
      rel += f->backup_boot;
    }
    if (sector(f, f->start + rel))
      return -1;
    if (u32(f->cache) == 0x41615252u && u32(f->cache + 484) == 0x61417272u &&
        u32(f->cache + 508) == 0xaa550000u) {
      put32(f->cache + 488, 0xffffffffu);
      put32(f->cache + 492, 0xffffffffu);
      if (write_sector(f, f->start + rel, f->cache))
        return -1;
    }
  }
  return 0;
}
/* Reserved-file allocation occurs only while audio is stopped. */
#if defined(__clang__)
__attribute__((minsize))
#endif
static int reserved_area(Fat *f, const char *name, const char *short_name,
                         uint32_t sectors, uint32_t lbas[2]) {
  Entry e;
  int found = fat_find(f, name, &e);
  if (found < 0)
    return -1;
  if (found) {
    if (e.size != sectors * 512u || (e.attr & 1u) || cluster_lba(f, e.cluster, lbas))
      return -1;
    if (f->type == 32 && e.cluster == f->root)
      return -1;
    if (sectors == 1 || f->spc > 1)
      lbas[1] = lbas[0] + 1;
    else {
      uint32_t second;
      if (next_cluster(f, e.cluster, &second) || !second ||
          second == e.cluster || (f->type == 32 && second == f->root) ||
          cluster_lba(f, second, lbas + 1))
        return -1;
    }
    return 0;
  }
  if (!f->write)
    return -1;
  uint32_t dir_lba = 0, slot = 0, si = 0, end = 0, located = 0;
  for (; si < 4096 && !located; si++) {
    if (root_sector(f, si, &dir_lba) || sector(f, dir_lba))
      return -1;
    for (slot = 0; slot < 16; slot++) {
      if (f->type == 16 && si * 16 + slot >= f->root_entries)
        return -1;
      uint8_t first = f->cache[slot * 32];
      if (!first || first == 0xe5) {
        end = !first;
        located = 1;
        break;
      }
    }
  }
  if (!located)
    return -1;
  uint32_t first = 0, second = 0;
  for (uint32_t c = 2; c - 2 < f->clusters; c++) {
    uint32_t value;
    if (fat_value(f, c, &value))
      return -1;
    if (!value) {
      if (!first)
        first = c;
      else {
        second = c;
        break;
      }
      if (f->spc >= sectors)
        break;
    }
  }
  if (!first || (sectors == 2 && f->spc == 1 && !second) || invalidate_fsinfo(f))
    return -1;
  /* Allocate tail first, then head; publish directory entry only after data.
   * Failure before publication may leak free clusters, never links a WAV. */
  uint32_t eoc = f->type == 16 ? 0xffffu : 0x0fffffffu;
  if (second && fat_set(f, second, eoc))
    return -1;
  if (fat_set(f, first, second ? second : eoc) || cluster_lba(f, first, lbas))
    return -1;
  if (second) {
    if (cluster_lba(f, second, lbas + 1))
      return -1;
  } else
    lbas[1] = lbas[0] + 1;
  memset(f->cache, 0, 512);
  if (write_sector(f, lbas[0], f->cache) || (sectors == 2 && write_sector(f, lbas[1], f->cache)))
    return -1;
  if (end && slot == 15) {
    uint32_t next;
    int r = root_sector(f, si, &next);
    if (r < 0)
      return -1;
    if (!r) {
      if (sector(f, next))
        return -1;
      f->cache[0] = 0;
      if (write_sector(f, next, f->cache))
        return -1;
    }
  }
  if (sector(f, dir_lba))
    return -1;
  uint8_t *p = f->cache + slot * 32;
  memset(p, 0, 32);
  memcpy(p, short_name, 11);
  p[11] = 32;
  put16(p + 26, first);
  if (f->type == 32)
    put16(p + 20, first >> 16);
  put32(p + 28, sectors * 512u);
  if (end && slot < 15)
    p[32] = 0;
  return write_sector(f, dir_lba, f->cache);
}

int fat_state_area(Fat *f, uint32_t lbas[2]) {
  return reserved_area(f, "ONESTATE.BIN", "ONESTATEBIN", 2, lbas);
}

/* Best effort at mount with audio stopped. Return 1 updated, 0 unchanged,
 * -1 unavailable/foreign/read-only/full/error. Never overwrite unrelated text. */
int fat_build_info(Fat *f, uint32_t number) {
  uint32_t lbas[2];
  if (reserved_area(f, "ONEBUILD.TXT", "ONEBUILDTXT", 1, lbas))
    return -1;
  uint8_t current[512], wanted[512];
  if (f->read(f->ctx, lbas[0], current))
    return -1;
  const char *prefix = "ONE firmware build ";
  uint32_t length = 0, zero = 1, own = 1;
  while (prefix[length]) {
    if (current[length] != (uint8_t)prefix[length])
      own = 0;
    length++;
  }
  for (uint32_t i = 0; i < 512; i++)
    if (current[i])
      zero = 0;
  if (!own && !zero)
    return -1;
  memset(wanted, ' ', sizeof(wanted));
  memcpy(wanted, prefix, length);
  uint8_t digits[10];
  uint32_t n = 0;
  do {
    digits[n++] = (uint8_t)('0' + number % 10u);
    number /= 10u;
  } while (number);
  while (n)
    wanted[length++] = digits[--n];
  wanted[length++] = '\r';
  wanted[length] = '\n';
  wanted[511] = '\n';
  uint32_t same = 1;
  for (uint32_t i = 0; i < 512; i++)
    if (current[i] != wanted[i])
      same = 0;
  if (same)
    return 0;
  if (write_sector(f, lbas[0], wanted) || f->read(f->ctx, lbas[0], current))
    return -1;
  for (uint32_t i = 0; i < 512; i++)
    if (current[i] != wanted[i])
      return -1;
  return 1;
}

#ifdef ONE_PROFILE
/* Reports are written only while audio is stopped. */
#if defined(__clang__)
__attribute__((minsize))
#endif
int fat_profile_write(Fat *f, const uint8_t data[1024]) {
  uint32_t lbas[2], zero = 1, own = 1;
  if (reserved_area(f, "ONEPROF.TXT", "ONEPROF TXT", 2, lbas)) return -1;
  f->cache_lba = f->fat_lba = 0xffffffffu;
  const char *magic = "Hamburger Wavy profile 1\n";
  for (uint32_t sector_index = 0; sector_index < 2; ++sector_index) {
    if (f->read(f->ctx, lbas[sector_index], f->cache)) return -1;
    if (!sector_index)
      for (uint32_t i = 0; magic[i]; ++i)
        if (f->cache[i] != (uint8_t)magic[i]) own = 0;
    for (uint32_t i = 0; i < 512; ++i) if (f->cache[i]) zero = 0;
  }
  if (!zero && !own) return -1;
  for (uint32_t sector_index = 0; sector_index < 2; ++sector_index) {
    const uint8_t *expected = data + sector_index * 512u;
    if (write_sector(f, lbas[sector_index], expected) ||
        f->read(f->ctx, lbas[sector_index], f->cache)) return -1;
    for (uint32_t i = 0; i < 512; ++i)
      if (f->cache[i] != expected[i]) return -1;
  }
  f->cache_lba = f->fat_lba = 0xffffffffu;
  return 0;
}
#endif
