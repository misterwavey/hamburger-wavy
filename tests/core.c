/* SPDX-License-Identifier: GPL-3.0-only */
#include "../firmware/platform.h"
#include "../firmware/profile.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                  \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static uint8_t blocks[128][512];
static uint32_t lbas[128], used, reads, fail_lba;
static uint32_t writes, fail_write, tear_write, write_lbas[1024];
static int audio_is_active(void);
static void put16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
  put16(p, v);
  put16(p + 2, v >> 16);
}
static uint8_t *block(uint32_t lba) {
  for (unsigned i = 0; i < used; i++)
    if (lbas[i] == lba)
      return blocks[i];
  CHECK(used < 128);
  lbas[used] = lba;
  memset(blocks[used], 0, 512);
  return blocks[used++];
}
static int disk(void *ctx, uint32_t lba, uint8_t *out) {
  (void)ctx;
  if (++reads > 10000 || lba == fail_lba) {
    memset(out, 0xcc, 512);
    return -1;
  }
  for (unsigned i = 0; i < used; i++)
    if (lbas[i] == lba) {
      memcpy(out, blocks[i], 512);
      return 0;
    }
  memset(out, 0, 512);
  return 0;
}
static int disk_write(void *ctx, uint32_t lba, const uint8_t *data) {
  (void)ctx;
  CHECK(writes < 1024);
  write_lbas[writes++] = lba;
  if (writes == fail_write)
    return -1;
  if (writes == tear_write) {
    memcpy(block(lba), data, 256);
    return -1;
  }
  memcpy(block(lba), data, 512);
  return 0;
}
static void clear_disk(void) {
  used = reads = writes = 0;
  fail_write = tear_write = 0;
  fail_lba = 0xffffffffu;
}
static void boot(uint8_t *p, uint32_t total, uint32_t fat_sectors,
                 uint32_t reserved, uint32_t roots, uint8_t fats) {
  memset(p, 0, 512);
  p[0] = 0xeb;
  put16(p + 11, 512);
  p[13] = 1;
  put16(p + 14, reserved);
  p[16] = fats;
  put16(p + 17, roots);
  put32(p + 32, total);
  p[510] = 85;
  p[511] = 170;
  if (roots)
    put16(p + 22, fat_sectors);
  else {
    put32(p + 36, fat_sectors);
    put32(p + 44, 2);
  }
}
static void entry(uint8_t *p, const char *name, uint32_t cluster,
                  uint32_t size) {
  memset(p, 0, 32);
  memcpy(p, name, 11);
  p[11] = 32;
  put16(p + 26, cluster);
  put16(p + 20, cluster >> 16);
  put32(p + 28, size);
}
static void disk16(void) {
  clear_disk();
  boot(block(0), 5000, 20, 1, 32, 1);
  put16(block(1) + 6, 7);
  put16(block(1) + 14, 0xffff);
  entry(block(21), "SAMPLE  WAV", 3, 800);
}
static void write_file(const uint8_t *data, uint32_t n) {
  memcpy(block(24), data, n < 512 ? n : 512);
  if (n > 512)
    memcpy(block(28), data + 512, n - 512);
  put32(block(21) + 28, n);
}
static uint32_t wav(uint8_t *p, uint32_t bits, uint32_t frames) {
  uint32_t bytes = frames * (bits / 8), size = 44 + bytes;
  memset(p, 0, size);
  memcpy(p, "RIFF", 4);
  put32(p + 4, size - 8);
  memcpy(p + 8, "WAVEfmt ", 8);
  put32(p + 16, 16);
  put16(p + 20, 1);
  put16(p + 22, 1);
  put32(p + 24, 48000);
  put32(p + 28, 48000 * (bits / 8));
  put16(p + 32, bits / 8);
  put16(p + 34, bits);
  memcpy(p + 36, "data", 4);
  put32(p + 40, bytes);
  for (uint32_t i = 0; i < bytes; i++)
    p[44 + i] = (uint8_t)(i * 13u);
  return size;
}
static void test_fat(void) {
  Fat f;
  File r;
  Entry es[8];
  uint8_t data[800], out[800];
  uint32_t n;
  for (unsigned i = 0; i < 800; i++)
    data[i] = (uint8_t)i;
  disk16();
  write_file(data, 800);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(f.type == 16);
  CHECK(f.data == 23);
  CHECK(fat_list(&f, es, 8) == 1);
  CHECK(!strcmp(es[0].name, "SAMPLE.WAV"));
  CHECK(!file_open(&r, &f, es));
  CHECK(!file_read(&r, out, 800, &n) && n == 800 && !memcmp(out, data, 800));
  CHECK(!file_seek(&r, 500));
  CHECK(!file_read(&r, out, 40, &n) && n == 40 && !memcmp(out, data + 500, 40));
  CHECK(file_seek(&r, 801) < 0);
  CHECK(!file_seek(&r, 799));
  CHECK(!file_read(&r, out, 80, &n) && n == 1);
  CHECK(!file_read(&r, out, 80, &n) && n == 0);
  put16(block(1) + 6, 0xffff);
  f.fat_lba = 0xffffffffu;
  CHECK(!file_seek(&r, 0));
  CHECK(file_read(&r, out, 800, &n) < 0 && n == 512);
  disk16();
  block(21)[11] = 2;
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(fat_list(&f, es, 8) == 0);
  disk16();
  block(0)[13] = 3;
  CHECK(fat_mount(&f, disk, 0) < 0);
  disk16();
  put32(block(0) + 32, 100);
  CHECK(fat_mount(&f, disk, 0) < 0);
  disk16();
  put16(block(0) + 11, 4096);
  CHECK(fat_mount(&f, disk, 0) < 0);
  disk16();
  put16(block(0) + 14, 65000);
  CHECK(fat_mount(&f, disk, 0) < 0);
  /* MBR volume offset; no writes through the filesystem API. */
  disk16();
  for (unsigned i = 0; i < used; i++)
    lbas[i] += 2048;
  uint8_t *mbr = block(0);
  mbr[510] = 85;
  mbr[511] = 170;
  mbr[450] = 14;
  put32(mbr + 454, 2048);
  put32(mbr + 458, 5000);
  CHECK(!fat_mount(&f, disk, 0) && f.start == 2048 && f.data == 2071);
  CHECK(fat_list(&f, es, 8) == 1);
  CHECK(!file_open(&r, &f, es));
  fail_lba = 2072;
  CHECK(file_read(&r, out, 8, &n) < 0);
  CHECK(f.cache_lba == 0xffffffffu);
  /* FAT32 active second FAT, fragmented root directory chain. */
  clear_disk();
  boot(block(0), 70000, 600, 32, 0, 2);
  put16(block(0) + 40, 0x81);
  put32(block(632) + 8, 4);
  put32(block(632) + 16, 0x0fffffff);
  put32(block(632) + 20, 0x0fffffff);
  memset(block(1232), 0xe5, 512);
  entry(block(1234), "SECOND  WAV", 5, 200);
  CHECK(!fat_mount(&f, disk, 0) && f.type == 32 && f.fat == 632);
  CHECK(fat_list(&f, es, 8) == 1 && es[0].cluster == 5);
  /* A cyclic root is rejected with a bounded traversal, not an endless boot. */
  memset(block(1234), 0xe5, 512);
  put32(block(632) + 16, 2);
  f.fat_lba = f.cache_lba = 0xffffffffu;
  reads = 0;
  CHECK(fat_list(&f, es, 8) < 0);
  CHECK(reads < 10000);
}
static void test_wave(void) {
  Fat f;
  File r;
  Entry es[2];
  Wave w;
  uint8_t data[1000], scratch[1000];
  uint32_t output[700], got;
  for (uint32_t bits = 16; bits <= 24; bits += 8) {
    disk16();
    uint32_t size = wav(data, bits, 200);
    write_file(data, size);
    CHECK(!fat_mount(&f, disk, 0));
    CHECK(fat_list(&f, es, 2) == 1);
    CHECK(!file_open(&r, &f, es));
    CHECK(!wave_open(&r, &w));
    CHECK(w.bits == bits && w.offset == 44 && w.bytes == 200 * (bits / 8));
    CHECK(!wave_read(&r, &w, output, 300, &got, scratch, sizeof(scratch)) &&
          got == 200);
    for (unsigned i = 0; i < 200; i++) {
      const uint8_t *p = data + 44 + i * (bits / 8);
      uint32_t v = bits == 16 ? ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 24)
                              : ((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) |
                                    ((uint32_t)p[2] << 24);
      CHECK(output[i * 2] == v && output[i * 2 + 1] == v);
    }
    for (unsigned i = 400; i < 600; i++)
      CHECK(output[i] == 0);
    CHECK(!wave_read(&r, &w, output, 100, &got, scratch, sizeof(scratch)) &&
          got == 0);
  }
  /* Unknown odd-sized chunk must be padded; data need not follow fmt directly.
   */
  disk16();
  uint32_t size = wav(data, 16, 10);
  memmove(data + 46, data + 36, size - 36);
  memcpy(data + 36, "JUNK", 4);
  put32(data + 40, 1);
  data[44] = 42;
  data[45] = 0;
  size += 10;
  put32(data + 4, size - 8);
  write_file(data, size);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(fat_list(&f, es, 2) == 1);
  CHECK(!file_open(&r, &f, es));
  CHECK(!wave_open(&r, &w) && w.offset == 54);
  /* PCM extensible fmt and data-before-fmt chunk ordering. */
  disk16();
  size = wav(data, 16, 16);
  memmove(data + 60, data + 36, size - 36);
  size += 24;
  put32(data + 4, size - 8);
  put32(data + 16, 40);
  put16(data + 20, 0xfffe);
  put16(data + 36, 22);
  put16(data + 38, 16);
  put32(data + 40, 0);
  static const uint8_t pcm_guid[16] = {1,   0, 0, 0,   0, 0,  16,  0,
                                       128, 0, 0, 170, 0, 56, 155, 113};
  memcpy(data + 44, pcm_guid, 16);
  write_file(data, size);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(fat_list(&f, es, 2) == 1);
  CHECK(!file_open(&r, &f, es));
  CHECK(!wave_open(&r, &w) && w.offset == 68);
  disk16();
  size = wav(data, 16, 16);
  uint8_t fmt[24];
  memcpy(fmt, data + 12, 24);
  memmove(data + 12, data + 36, size - 36);
  memcpy(data + size - 24, fmt, 24);
  write_file(data, size);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(fat_list(&f, es, 2) == 1);
  CHECK(!file_open(&r, &f, es));
  CHECK(!wave_open(&r, &w) && w.offset == 20);
  /* Malformed / unsupported files. */
  for (unsigned variant = 0; variant < 7; variant++) {
    disk16();
    size = wav(data, 16, 8);
    if (variant == 0)
      put16(data + 20, 3);
    if (variant == 1)
      put16(data + 22, 2);
    if (variant == 2)
      put16(data + 34, 8);
    if (variant == 3)
      put32(data + 40, 0xffffffffu);
    if (variant == 4)
      put32(data + 24, 0);
    if (variant == 5)
      put32(data + 4, 4);
    if (variant == 6)
      put16(data + 32, 3);
    write_file(data, size);
    CHECK(!fat_mount(&f, disk, 0));
    CHECK(fat_list(&f, es, 2) == 1);
    CHECK(!file_open(&r, &f, es));
    CHECK(wave_open(&r, &w) < 0);
  }
  uint8_t p16[] = {0, 0, 255, 127, 0, 128, 255, 255};
  uint32_t expected16[] = {0, 0x7fff0000, 0x80000000, 0xffff0000};
  CHECK(!pcm_decode(p16, 16, output, 4));
  for (unsigned i = 0; i < 4; i++)
    CHECK(output[i * 2] == expected16[i] && output[i * 2 + 1] == expected16[i]);
  uint8_t p24[] = {1, 0, 0, 255, 255, 127, 0, 0, 128, 255, 255, 255};
  uint32_t expected24[] = {0x100, 0x7fffff00, 0x80000000, 0xffffff00};
  CHECK(!pcm_decode(p24, 24, output, 4));
  for (unsigned i = 0; i < 4; i++)
    CHECK(output[i * 2] == expected24[i] && output[i * 2 + 1] == expected24[i]);
  CHECK(pcm_decode(p24, 8, output, 4) < 0);
  /* Deterministic malformed RIFF mutation corpus, exercised under sanitizers.
   */
  uint32_t random = 17;
  for (unsigned i = 0; i < 1000; i++) {
    disk16();
    size = wav(data, 16, 32);
    random = random * 1664525u + 1013904223u;
    unsigned at = random % 44;
    data[at] ^= (uint8_t)(random >> 24);
    write_file(data, size);
    CHECK(!fat_mount(&f, disk, 0));
    CHECK(fat_list(&f, es, 2) == 1);
    CHECK(!file_open(&r, &f, es));
    (void)wave_open(&r, &w);
  }
}
typedef struct {
  int selected, pos, head, tail, ready, modern, hc, echo_bad, token_bad,
      crc_bad, fail, disk_mode, background, crc_on, crc_bad_once;
  uint32_t write_phase, write_count, write_lba, write_reject, write_status,
      write_timeout;
  uint8_t write_data[514];
  uint8_t packet[6], queue[520];
  uint32_t bulk_calls, bulk_bytes, bulk_fail_at;
  uint32_t time, commands[512], arguments[512], n, attempts, transfers;
} Mock;
static void enqueue(Mock *m, int b) {
  CHECK(m->tail < 520);
  m->queue[m->tail++] = (uint8_t)b;
}
static uint16_t reference_crc16(const uint8_t *data) {
  uint16_t crc = 0;
  for (unsigned i = 0; i < 512; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (unsigned k = 0; k < 8; k++)
      crc = (uint16_t)((crc & 0x8000u) ? (crc << 1) ^ 0x1021u : crc << 1);
  }
  return crc;
}
static void process(Mock *m) {
  uint32_t cmd = m->packet[0] & 63, arg = ((uint32_t)m->packet[1] << 24) |
                                          ((uint32_t)m->packet[2] << 16) |
                                          ((uint32_t)m->packet[3] << 8) |
                                          m->packet[4];
  CHECK(m->n < 512);
  m->commands[m->n] = cmd;
  m->arguments[m->n++] = arg;
  if (cmd == 0) {
    m->ready = 0;
    m->attempts = 0;
    m->crc_on = 0;
    CHECK(m->packet[5] == 0x95);
    enqueue(m, 1);
  } else if (cmd == 8) {
    CHECK(m->packet[5] == 0x87);
    enqueue(m, m->modern ? 1 : 5);
    if (m->modern) {
      enqueue(m, 0);
      enqueue(m, 0);
      enqueue(m, 1);
      enqueue(m, m->echo_bad ? 0 : 170);
    }
  } else if (cmd == 59) {
    CHECK(arg == 1);
    m->crc_on = 1;
    enqueue(m, m->ready ? 0 : 1);
  } else if (cmd == 55)
    enqueue(m, m->ready ? 0 : 1);
  else if (cmd == 41) {
    m->ready = ++m->attempts >= 2;
    enqueue(m, m->ready ? 0 : 1);
  } else if (cmd == 58) {
    enqueue(m, 0);
    enqueue(m, m->hc ? 192 : 128);
    enqueue(m, 0);
    enqueue(m, 0);
    enqueue(m, 0);
  } else if (cmd == 16) {
    CHECK(arg == 512);
    enqueue(m, 0);
  } else if (cmd == 24) {
    CHECK(m->crc_on);
    m->write_lba = m->hc ? arg : arg >> 9;
    m->write_phase = 1;
    m->write_count = 0;
    enqueue(m, 0);
  } else if (cmd == 13) {
    enqueue(m, 0);
    enqueue(m, m->write_status);
  } else if (cmd == 17) {
    CHECK(m->crc_on);
    enqueue(m, 0);
    enqueue(m, 255);
    enqueue(m, m->token_bad ? 0xfd : 0xfe);
    uint8_t data[512];
    if (m->disk_mode)
      CHECK(!disk(0, m->hc ? arg : arg >> 9, data));
    for (unsigned i = 0; i < 512; i++) {
      if (!m->disk_mode)
        data[i] = (uint8_t)i;
      enqueue(m, data[i]);
    }
    uint16_t crc = reference_crc16(data);
    enqueue(m, crc >> 8);
    uint32_t spoil = m->crc_bad || m->crc_bad_once;
    if (m->crc_bad_once) m->crc_bad_once--;
    enqueue(m, (crc & 255) ^ (spoil ? 1 : 0));
  } else {
    fprintf(stderr, "unexpected command %u\n", cmd);
    exit(1);
  }
}
static void audio_progress_on_transfer(void);
static int xfer(void *ctx, uint8_t b) {
  Mock *m = ctx;
  if (m->background && ((++m->transfers & 63u) == 0)) {
    audio_progress_on_transfer();
    app_background();
  }
  if (m->fail)
    return -1;
  if (!m->selected)
    return 255;
  if (m->head < m->tail)
    return m->queue[m->head++];
  if (m->write_phase == 3 && m->write_timeout)
    return 0;
  if (m->write_phase == 1) {
    if (b == 0xfe)
      m->write_phase = 2;
    return 255;
  }
  if (m->write_phase == 2) {
    CHECK(m->write_count < 514);
    m->write_data[m->write_count++] = b;
    if (m->write_count == 514) {
      CHECK(reference_crc16(m->write_data) ==
            ((uint16_t)m->write_data[512] << 8 | m->write_data[513]));
      if (m->background)
        CHECK(!audio_is_active());
      uint32_t rejected = m->write_reject;
      if (!rejected && m->disk_mode &&
          disk_write(0, m->write_lba, m->write_data))
        rejected = 13;
      m->head = m->tail = 0;
      enqueue(m, rejected ? rejected : 5);
      enqueue(m, 0);
      enqueue(m, 0);
      enqueue(m, m->write_timeout ? 0 : 255);
      m->write_phase = 3;
    }
    return 255;
  }
  if (m->pos || ((b & 0xc0) == 0x40)) {
    m->packet[m->pos++] = b;
    if (m->pos == 6) {
      m->pos = 0;
      m->head = m->tail = 0;
      process(m);
    }
    return 255;
  }
  return 255;
}
static int bulk_mock(void *ctx, uint8_t *out, uint32_t length, uint16_t *checksum) {
  uint16_t crc = 0;
  Mock *m=ctx;
  ++m->bulk_calls;
  for (uint32_t i=0; i<length; ++i) {
    if (m->bulk_fail_at && i==m->bulk_fail_at) return -1;
    int value=xfer(ctx,255);
    if (value<0) return -1;
    out[i]=(uint8_t)value;
    crc ^= (uint16_t)value << 8;
    for (unsigned bit=0; bit<8; ++bit)
      crc=(uint16_t)((crc & 0x8000u) ? (crc << 1) ^ 0x1021u : crc << 1);
    ++m->bulk_bytes;
  }
  *checksum=crc;
  return 0;
}
static void select_mock(void *ctx, int yes) {
  Mock *m = ctx;
  m->selected = yes;
  if (!yes) {
    m->pos = m->head = m->tail = m->write_phase = 0;
  }
}
static void speed_mock(void *ctx, int fast) {
  (void)ctx;
  (void)fast;
}
static uint32_t time_mock(void *ctx) { return ++((Mock *)ctx)->time; }
static void test_sd(void) {
  for (unsigned kind = 0; kind < 3; kind++) {
    Mock m = {0};
    m.modern = kind != 0;
    m.hc = kind == 2;
    SdIo io = {&m, xfer, select_mock, speed_mock, time_mock, 0};
    Sd s;
    CHECK(!sd_init(&s, &io));
    CHECK(s.ready && s.high_capacity == (uint32_t)m.hc);
    uint8_t out[512];
    CHECK(!sd_read(&s, 123, out));
    CHECK(m.commands[m.n - 1] == 17 &&
          m.arguments[m.n - 1] == (m.hc ? 123u : 123u * 512));
    for (unsigned i = 0; i < 512; i++)
      CHECK(out[i] == (i & 255));
    for (unsigned i = 0; i < m.n; i++)
      CHECK(m.commands[i] != 24 && m.commands[i] != 25 && m.commands[i] != 32 &&
            m.commands[i] != 38);
    m.crc_bad = 1;
    CHECK(sd_read(&s, 124, out) < 0 && !s.ready);
    m.crc_bad = 0;
    CHECK(!sd_init(&s, &io));
#ifdef ONE_SD_RETRY
    /* One bad transfer is retried and succeeds; two in a row give up. */
    unsigned before = m.n;
    memset(out, 0xee, sizeof(out));
    m.crc_bad_once = 1;
    CHECK(!sd_read(&s, 125, out) && s.ready && m.n == before + 2u);
    CHECK(m.commands[m.n - 1] == 17 && m.commands[m.n - 2] == 17 &&
          m.arguments[m.n - 1] == m.arguments[m.n - 2]);
    for (unsigned i = 0; i < 512; i++)
      CHECK(out[i] == (i & 255));
    before = m.n;
    m.crc_bad_once = 2;
    CHECK(sd_read(&s, 125, out) < 0 && !s.ready && m.n == before + 2u);
    CHECK(sd_read(&s, 125, out) < 0 && m.n == before + 2u);
    CHECK(!m.crc_bad_once && !sd_init(&s, &io));
#else
    unsigned before = m.n;
    m.crc_bad_once = 1;
    CHECK(sd_read(&s, 125, out) < 0 && !s.ready && m.n == before + 1u);
    CHECK(!sd_init(&s, &io));
#endif
    m.token_bad = 1;
    CHECK(sd_read(&s, 124, out) < 0 && !s.ready);
  }
  Mock m = {0};
  m.modern = 1;
  m.echo_bad = 1;
  SdIo io = {&m, xfer, select_mock, speed_mock, time_mock, 0};
  Sd s;
  CHECK(sd_init(&s, &io) < 0 && !s.ready);
  memset(&m, 0, sizeof(m));
  m.fail = 1;
  CHECK(sd_init(&s, &io) < 0);
}
static void test_bulk_sd_and_audio_copy(void) {
  for (uint32_t hc=0; hc<2; ++hc) {
    Mock m={0}; m.modern=1; m.hc=hc;
    SdIo io={&m,xfer,select_mock,speed_mock,time_mock,bulk_mock};
    Sd card;
    uint8_t bytes[514]; memset(bytes,0x7b,sizeof(bytes));
    CHECK(!sd_init(&card,&io));
    CHECK(!sd_read(&card,123,bytes+1));
    CHECK(m.bulk_calls==1 && m.bulk_bytes==512);
    CHECK(bytes[0]==0x7b && bytes[513]==0x7b);
    for (uint32_t i=0; i<512; ++i) CHECK(bytes[i+1]==(uint8_t)i);
    CHECK(m.arguments[m.n-1]==(hc?123u:123u*512u));
    m.crc_bad=1;
    CHECK(sd_read(&card,124,bytes+1)<0 && !card.ready);
    m.crc_bad=0;
    CHECK(!sd_init(&card,&io)); m.bulk_fail_at=101;
    CHECK(sd_read(&card,124,bytes+1)<0 && !card.ready);
    CHECK(bytes[0]==0x7b && bytes[513]==0x7b);
    m.bulk_fail_at=0;
    CHECK(!sd_init(&card,&io)); m.token_bad=1;
    uint32_t calls=m.bulk_calls;
    CHECK(sd_read(&card,124,bytes+1)<0 && !card.ready && m.bulk_calls==calls);
  }
  static uint32_t source[2050],destination[2050];
  static const uint32_t sizes[]={0,1,2,3,4,5,7,8,9,511,512,1023,1024,2048};
  for (uint32_t i=0; i<2050; ++i) source[i]=i*0x1020305u;
  for (uint32_t k=0; k<sizeof(sizes)/sizeof(*sizes); ++k) {
    uint32_t n=sizes[k];
    for (uint32_t i=0; i<2050; ++i) destination[i]=0xdeadbeefu;
    audio_copy(destination+1,source+1,n);
    for (uint32_t i=0; i<n; ++i) CHECK(destination[i+1]==source[i+1]);
    CHECK(destination[0]==0xdeadbeefu && destination[n+1]==0xdeadbeefu);
  }
}
static void test_aligned_and_unaligned_memcpy(void) {
  static uint8_t source[4104] __attribute__((aligned(16)));
  static uint8_t destination[4104] __attribute__((aligned(16)));
  static const uint32_t sizes[]={0,1,2,3,4,5,7,8,9,31,32,33,511,512,513,1536,4096};
  for (uint32_t i=0; i<sizeof(source); ++i) source[i]=(uint8_t)(i*7+3);
  for (uint32_t a=0; a<4; ++a) for (uint32_t b=0; b<4; ++b)
    for (uint32_t k=0; k<sizeof(sizes)/sizeof(*sizes); ++k) {
      uint32_t n=sizes[k];
      for (uint32_t i=0; i<sizeof(destination); ++i) destination[i]=0xa5;
      CHECK(memcpy(destination+4+a,source+4+b,n)==destination+4+a);
      for (uint32_t i=0; i<n; ++i) CHECK(destination[4+a+i]==source[4+b+i]);
      CHECK(destination[3+a]==0xa5 && destination[4+a+n]==0xa5);
    }
}
static void test_ring(void) {
  Ring r = {0};
  ring_start(&r);
  CHECK(r.running && ring_boundary(&r, 0) == 0);
  CHECK(ring_boundary(&r, 1) == 1 && r.pending == 1);
  CHECK(ring_commit(&r, 1) < 0);
  CHECK(!ring_commit(&r, 0));
  CHECK(ring_boundary(&r, 0) == 1 && r.pending == 2);
  CHECK(!ring_commit(&r, 1));
  CHECK(ring_boundary(&r, 1) == 1);
  CHECK(ring_boundary(&r, 0) < 0 && !r.running && r.underruns == 1);
  CHECK(ring_commit(&r, 0) < 0);
  ring_start(&r);
  CHECK(ring_boundary(&r, 2) < 0);
  CHECK(ring_commit(&r, 2) < 0);
}

static Mock app_card;
static uint32_t host_pins = 0xaa0, host_adc = 128, host_offset, host_starts,
                host_stops, host_led, host_pitch, host_rate, host_active,
                host_unsafe;
static uint32_t host_transfer_advance, host_transfer_budget;
static const uint32_t *host_audio;
#ifdef ONE_FLIP
static uint32_t host_flip_edges, host_flip_inject, host_flip_captured, host_silent_stop, host_commit_move, host_offset_move, host_offset_target;
#endif
static void audio_progress_on_transfer(void) {
#ifdef ONE_FLIP
  if (host_flip_edges && !host_active) { host_pins ^= 0x800u; --host_flip_edges; }
#endif
  if (host_active) {
    uint32_t advance = host_transfer_advance;
    if (host_transfer_budget && advance > host_transfer_budget)
      advance = host_transfer_budget;
    host_offset = (host_offset + advance) % (ONE_CACHE_FRAMES * 8u);
    if (host_transfer_budget) {
      host_transfer_budget -= advance;
      if (!host_transfer_budget)
        host_transfer_advance = 0;
    }
  }
#ifdef ONE_FLIP
  if (host_active && host_flip_inject) {
    host_flip_captured = host_audio[(host_offset / 8u) * 2u];
    host_pins &= ~0x800u; host_flip_inject = 0;
  }
#endif
}
static CvMode host_mode;
const SdIo board_sd_io = {&app_card, xfer, select_mock, speed_mock, time_mock, bulk_mock};
static int audio_is_active(void) { return host_active != 0; }
#if defined(ONE_PROFILE) || defined(ONE_TOLERANT_ENCODER)
static uint32_t profile_mock_ticks;
uint32_t board_ticks(void) { return app_card.time * 24000u + ++profile_mock_ticks; }
#endif
#ifdef ONE_PROFILE
void board_stack_info(uint32_t out[3]) {
  out[0] = 33488; out[1] = 2048; out[2] = 29992;
}
#endif
void board_init(void) {}
static uint32_t boot_clock, boot_watchdogs, boot_events;
static uint32_t boot_times[16], boot_colours[16];
void board_watchdog(void) {
  if (boot_clock) {
    app_card.time++;
    boot_watchdogs++;
  }
}
void board_audio_publish(void) {}
uint32_t board_millis(void) { return app_card.time; }
uint32_t board_pins(void) { return host_pins; }
uint32_t board_adc(void) { return host_adc; }
void board_pitch(uint32_t value, uint32_t rate) {
  host_pitch = value;
  host_rate = rate;
}
#ifdef ONE_FLIP
static uint32_t host_fixed;
void board_pitch_fixed(uint32_t fixed) {host_fixed=fixed;PROFILE(profile_pitch(fixed));}
#endif
void board_pitch_mode(uint32_t value, uint32_t rate, CvMode mode) {
  host_mode = mode;
  PROFILE(profile_pitch(pitch_divider(value, rate, mode)));
  board_pitch(value, rate);
}
void board_led(uint32_t value) {
  host_led = value;
  if (boot_clock &&
      (boot_events == 0 || boot_colours[boot_events - 1] != value)) {
    CHECK(boot_events < 16);
    boot_times[boot_events] = app_card.time;
    boot_colours[boot_events++] = value;
  }
}
static void test_advertisement(void) {
  for (uint32_t number = 1; number <= 3; number += 2) {
    uint32_t start = 0xfffffe00u;
    app_card.time = start;
    boot_clock = 1;
    boot_events = boot_watchdogs = 0;
    advertise_build(number);
    CHECK(boot_events == 2);
    CHECK(boot_colours[0] == 1 && boot_times[0] == start);
    CHECK(boot_colours[1] == 0 && boot_times[1] - start == 1000);
    CHECK(app_card.time - start == 1000);
    CHECK(boot_watchdogs == 1000);
    CHECK(host_led == 0);
    boot_clock = 0;
  }
}

void board_audio_stop(void) {
#ifdef ONE_FLIP
  if (host_silent_stop && host_active) {
    uint32_t position=(host_offset/8u)*2u;
    CHECK(host_audio[position]==0 && host_audio[(position+ONE_CACHE_FRAMES*2u-2u)%(ONE_CACHE_FRAMES*2u)]==0);
    host_silent_stop=0;
  }
#endif
  host_stops++;
  host_active = 0;
}
void board_audio_start(const uint32_t *data) {
  host_audio = data;
  host_active = 1;
  host_offset = 0;
  host_starts++;
}
uint32_t board_audio_offset(void) {
#ifdef ONE_FLIP
  if(host_offset_move && !--host_offset_move) host_offset=host_offset_target;
#endif
  return host_offset;
}
int board_audio_commit_safe(uint32_t half) {
#ifdef ONE_FLIP
  if(host_commit_move && !--host_commit_move) host_offset=(ONE_HALF_FRAMES-64u)*8u;
#endif
  return !host_unsafe && host_offset / (ONE_HALF_FRAMES * 8u) != half &&
         ONE_HALF_FRAMES * 8u - host_offset % (ONE_HALF_FRAMES * 8u) >= 1024u;
}
static void test_application(void) {
  static uint8_t data[9000];
  clear_disk();
  boot(block(0), 5000, 20, 1, 32, 1);
  uint32_t size = wav(data, 16, ONE_HALF_FRAMES * 4u - 48u);
  entry(block(21), "PLAYER  WAV", 3, size);
  for (uint32_t off = 0, cluster = 3; off < size; cluster++, off += 512) {
    uint32_t n = size - off;
    if (n > 512)
      n = 512;
    memcpy(block(23 + cluster - 2), data + off, n);
    put16(block(1) + cluster * 2, off + n == size ? 0xffff : cluster + 1);
  }
  app_card.modern = app_card.hc = app_card.disk_mode = app_card.background = 1;
  app_init();
  app_tick();
  CHECK(!host_active && host_starts == 0 && host_rate == 48000);
  host_pins &= ~0x800u;
  app_tick();
  CHECK(host_starts == 1 && host_active && host_audio);
  uint32_t first = ((uint32_t)data[44] << 16) | ((uint32_t)data[45] << 24);
  CHECK(host_audio[0] == first && host_audio[1] == first);
  app_tick();
  CHECK(host_starts == 1);
  host_pins |= 0x800;
  app_tick();
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  uint32_t offset = 44 + ONE_CACHE_FRAMES * 2u,
           expected = ((uint32_t)data[offset] << 16) |
                      ((uint32_t)data[offset + 1] << 24);
  CHECK(host_active && host_audio[0] == expected && host_audio[1] == expected);
  host_offset = 0;
  app_tick();
  CHECK(host_active);
  offset = 44 + (ONE_HALF_FRAMES * 4u - 49u) * 2u;
  expected =
      ((uint32_t)data[offset] << 16) | ((uint32_t)data[offset + 1] << 24);
  CHECK(host_audio[ONE_HALF_FRAMES * 2u + (ONE_HALF_FRAMES - 49u) * 2u] == expected &&
        host_audio[ONE_HALF_FRAMES * 2u + (ONE_HALF_FRAMES - 48u) * 2u] == 0);
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  CHECK(host_audio[0] == 0 && host_active);
  host_pins &= ~0x200u;
  for (unsigned i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  CHECK(host_starts == 1 && host_active);
  host_pins |= 0x200u;
  for (unsigned i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  CHECK(host_starts == 2 && host_active);
  for (unsigned i = 0; i < 8; i++) {
    app_card.time++;
    app_tick();
  }
  CHECK(host_starts == 2);
  host_adc = 512;
  app_tick();
  CHECK(host_pitch == 128);
  /* Refuse a late unsafe buffer copy rather than overwriting active DMA data.
   */
  host_offset = ONE_HALF_FRAMES * 8u;
  host_unsafe = 1;
  app_tick();
  CHECK(!host_active && host_led == 1);
  host_unsafe = 0;
  host_pins |= 0x200;
  for (unsigned i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  host_pins &= ~0x800u;
  app_tick();
  CHECK(host_active && host_starts == 3);
  app_card.token_bad = 1;
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick(); /* The already-prefetched half may publish without card access. */
  app_tick(); /* The following prefetch encounters the injected read error. */
  CHECK(!host_active);
  app_card.token_bad = 0;
  app_card.time += 1001;
  app_card.n = 0;
  app_tick();
  CHECK(!host_active);
  host_pins |= 0x800;
  app_tick();
  host_pins &= ~0x800u;
  app_tick();
  CHECK(host_active && host_starts == 4);
  CHECK(host_stops > 0);
  /* A stationary DMA pointer must not leave playback red indefinitely. */
  app_card.time += 1001;
  app_tick();
  CHECK(!host_active);
  app_card.time = (app_card.time / 500u + 1u) * 500u;
  app_tick();
  CHECK(host_led == 0);
  app_card.time += 250;
  app_tick();
  CHECK(host_led == 1);
  host_pins |= 0x800u;
  app_tick();
  host_pins &= ~0x800u;
  app_tick();
  CHECK(host_active && host_starts == 5 && host_led == 2);
}

static uint32_t feature_frames, feature_bits;
static uint32_t sample_word(uint32_t which, uint32_t frame) {
  if (feature_bits == 16)
    return (which * 4000u + frame + 1u) << 16;
  return (which * 100000u + frame * 37u + 1u) << 8;
}
static void put_sample_file(uint32_t slot, const char *name, uint32_t cluster,
                            uint32_t which, uint32_t frames, uint32_t bits) {
  static uint8_t data[16000];
  uint32_t size = wav(data, bits, frames);
  CHECK(size <= sizeof(data));
  for (uint32_t i = 0; i < frames; i++) {
    uint32_t value = sample_word(which, i);
    if (bits == 16)
      put16(data + 44 + i * 2, value >> 16);
    else {
      data[44 + i * 3] = value >> 8;
      data[45 + i * 3] = value >> 16;
      data[46 + i * 3] = value >> 24;
    }
  }
  entry(block(21) + slot * 32, name, cluster, size);
  for (uint32_t off = 0; off < size; off += 512, cluster++) {
    uint32_t n = size - off;
    if (n > 512)
      n = 512;
    memcpy(block(23 + cluster - 2), data + off, n);
    put16(block(1) + cluster * 2, off + n == size ? 0xffff : cluster + 1);
  }
}
static void put_text(uint32_t slot, const char *name, uint32_t cluster,
                     const char *text) {
  uint32_t size = (uint32_t)strlen(text);
  CHECK(size <= 512);
  entry(block(21) + slot * 32, name, cluster, size);
  memcpy(block(23 + cluster - 2), text, size);
  put16(block(1) + cluster * 2, 0xffff);
}
static void feature_disk(const char *config, const char *playlist,
                         uint32_t frames, uint32_t bits) {
  clear_disk();
  feature_frames = frames;
  feature_bits = bits;
  boot(block(0), 5000, 20, 1, 32, 1);
  put_sample_file(0, "A       WAV", 3, 1, frames, bits);
  put_sample_file(1, "B       WAV", 35, 2, frames, bits);
  put_sample_file(2, "C       WAV", 67, 3, frames, bits);
  /* Keep unused directory slots deleted, not end-of-directory markers. */
  block(21)[3 * 32] = block(21)[4 * 32] = 0xe5;
  if (config)
    put_text(3, "CONFIG  TXT", 100, config);
  if (playlist)
    put_text(4, "PLAYLISTTXT", 101, playlist);
}
static void feature_mount(void) {
  memset(&app_card, 0, sizeof(app_card));
  app_card.modern = app_card.hc = app_card.disk_mode = app_card.background = 1;
  app_card.time = 100;
  host_pins = 0xaa0;
  host_adc = 128;
  host_offset = host_starts = host_stops = host_active = host_unsafe = 0;
  host_audio = 0;
  app_init();
  app_tick();
  CHECK(!host_active && !host_starts);
}
static void pulse(void) {
  host_pins |= 0x800u;
  app_tick();
  host_pins &= ~0x800u;
  app_tick();
}
static void advance_half(void) {
  host_offset ^= ONE_HALF_FRAMES * 8u;
  app_card.time++;
  app_tick();
}
/* The contact filter releases a line only after it has read high for
 * ENCODER_RELEASE_MS: give each simulated state that long. */
static void encoder_settle(void) {
#ifdef ONE_TOLERANT_ENCODER
  app_background();
  app_card.time += ENCODER_RELEASE_MS + 1u;
#endif
}
static void browse_next(void) {
  static const uint32_t sequence[4] = {2, 0, 1, 3};
  for (uint32_t i = 0; i < 4; i++) {
    host_pins = (host_pins & ~0xa0u) | ((sequence[i] & 1u) << 5) |
                ((sequence[i] & 2u) << 6);
    encoder_settle();
    app_tick();
  }
  encoder_settle();
  app_tick();
}
static void test_settings_playlist(void) {
  Fat f;
  Settings s;
  Entry list[256], e;
  static const char *names[] = {
      "Trigger", "Gate",        "Fade",    "Loop",   "Looping",    "Sequence",
      "Combo",   "TG Sequence", "TG Loop", "Random", "Random Loop", "Random Sequence"};
  static const PlayMode modes[] = {
      PLAY_TRIGGER, PLAY_GATE,        PLAY_FADE,   PLAY_LOOP,
      PLAY_LOOP,    PLAY_SEQUENCE,    PLAY_COMBO,  PLAY_RANDOM_SEQUENCE,
      PLAY_RANDOM_LOOP, PLAY_RANDOM_SEQUENCE, PLAY_RANDOM_LOOP, PLAY_RANDOM_SEQUENCE};
  char config[256];
  for (uint32_t i = 0; i < sizeof(modes) / sizeof(*modes); i++) {
    snprintf(config, sizeof(config),
             "\xef\xbb\xbf# card\r\n%s\r\nQuantized\r\nMedium\r\nignored\n",
             names[i]);
    feature_disk(config, "C.wav\r\nA.wav\r\nA.wav\r\nmissing.wav\r\nB.wav\n",
                 37, 16);
    CHECK(!fat_mount(&f, disk, 0));
    CHECK(settings_load(&f, &s) == 1);
    CHECK(s.play == modes[i] && s.cv == CV_QUANTIZE && s.delay_ms == 2);
    CHECK(fat_find(&f, "config.txt", &e) == 1);
    CHECK(fat_find(&f, "absent.txt", &e) == 0);
    CHECK(playlist_load(&f, list, 256) == 3);
    CHECK(!strcmp(list[0].name, "C.WAV") && !strcmp(list[1].name, "A.WAV"));
    CHECK(list[1].cluster == list[2].cluster);
  }
  feature_disk("# card\nSequence\nSelect File\nLong", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_SELECT && s.delay_ms == 10);
  CHECK(playlist_load(&f, list, 256) == -2);
  feature_disk("# card\nLooping\nFree", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.play == PLAY_LOOP && s.cv == CV_FREE &&
        !s.delay_ms);
  static const char *bad[] = {"",
                              "comment\nLoop\nFree",
                              "# card\nInvalid\nFree",
                              "# card\nTG Loop\nBogus",
                              "# card\nGate\nFree\nBogus",
                              "# card\n",
                              "# card"};
  for (uint32_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    feature_disk(bad[i], 0, 37, 16);
    CHECK(!fat_mount(&f, disk, 0));
    CHECK(settings_load(&f, &s) == -2);
    CHECK(s.play == PLAY_TRIGGER && s.cv == CV_QUANTIZE && !s.delay_ms);
  }
  memset(config, 'X', sizeof(config));
  config[0] = '#';
  config[sizeof(config) - 1] = 0;
  feature_disk(config, 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == -2);
  feature_disk(0, 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 0);
  feature_disk("# card\nLoop\nFree\nShort", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  fail_lba = 121;
  CHECK(settings_load(&f, &s) == -1);
  fail_lba = 0xffffffffu;
  CHECK(settings_load(&f, &s) == 1);
  /* A list can contain the same file in every slot. */
  feature_disk(0, 0, 37, 16);
  char lines[1537];
  for (uint32_t i = 0; i < 256; i++)
    memcpy(lines + i * 6, "A.wav\n", 6);
  lines[1536] = 0;
  entry(block(21) + 4 * 32, "PLAYLISTTXT", 101, 1536);
  for (uint32_t i = 0; i < 3; i++) {
    memcpy(block(122 + i), lines + i * 512, 512);
    put16(block(1) + (101 + i) * 2, i == 2 ? 0xffff : 102 + i);
  }
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(playlist_load(&f, list, 256) == 256);
  CHECK(list[0].cluster == list[255].cluster);
}
static void test_long_playlist_name(void) {
  Fat f;
  Entry list[8], e;
  feature_disk(0, "ABCDEFGHI.wav\nABCDEF~1.wav\nA.wav\n", 37, 16);
  uint8_t *p = block(21) + 5 * 32;
  memset(p, 0, 32);
  p[0] = 0x41;
  p[11] = 15;
  static const uint8_t offsets[13] = {1,  3,  5,  7,  9,  14, 16,
                                      18, 20, 22, 24, 28, 30};
  const char *name = "ABCDEFGHI.WAV";
  const char *alias = "ABCDEF~1WAV";
  uint8_t checksum = 0;
  for (uint32_t i = 0; i < 11; i++)
    checksum =
        (uint8_t)(((checksum & 1u) << 7) + (checksum >> 1) + (uint8_t)alias[i]);
  p[13] = checksum;
  for (uint32_t i = 0; i < 13; i++)
    put16(p + offsets[i], (uint8_t)name[i]);
  entry(block(21) + 6 * 32, alias, 3, 44 + 37 * 2);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(fat_list(&f, list, 8) == 4 && !strcmp(list[3].name, name));
  CHECK(fat_find(&f, "ABCDEFGHI.wav", &e) == 1 && e.cluster == 3);
  CHECK(fat_find(&f, "abcdef~1.wav", &e) == 1 && e.cluster == 3);
  CHECK(playlist_load(&f, list, 8) == 3 && list[0].cluster == list[1].cluster);
  p[13] ^= 1;
  f.cache_lba = 0xffffffffu;
  CHECK(fat_find(&f, "ABCDEFGHI.wav", &e) == 0);
  CHECK(fat_find(&f, "ABCDEF~1.wav", &e) == 1);
}
static void test_policy(void) {
  for (uint32_t rate = 0; rate < 4; rate++) {
    uint32_t hz[] = {44100, 48000, 88200, 96000};
    uint32_t factor = rate % 2 ? 2000 : 2176;
    uint32_t previous = 0;
    for (uint32_t value = 0; value < 256; value++) {
      CHECK(pitch_divider(value, hz[rate], CV_FREE) ==
            ((value + 32) * factor) >> 6);
      uint32_t q = pitch_divider(value, hz[rate], CV_QUANTIZE);
      CHECK(q >= previous);
      previous = q;
    }
    CHECK(pitch_divider(37, hz[rate], CV_QUANTIZE) ==
          (rate >= 2 ? factor / 2 : factor));
    CHECK(pitch_divider(0, hz[rate], CV_SELECT) ==
          (rate >= 2 ? factor / 2 : factor));
  }
  uint32_t rng = 0, seen = 0;
  for (uint32_t i = 0; i < 10000; i++) {
    uint32_t r = random_below(&rng, 3);
    CHECK(r < 3);
    seen |= 1u << r;
  }
  CHECK(seen == 7 && random_below(&rng, 1) == 0 && random_below(&rng, 0) == 0);
  uint32_t samples[2048];
  Envelope env = {0, 32768};
  for (uint32_t i = 0; i < 1024; i++)
    samples[2 * i] = samples[2 * i + 1] = 0x40000000;
  envelope_apply(&env, samples, 1024);
  CHECK(env.gain == 32768 && samples[0] == 0 && samples[0] < 0x40000000);
  CHECK(samples[2046] == 0x40000000);
  for (uint32_t i = 1; i < 1024; i++)
    CHECK(samples[2 * i] >= samples[2 * i - 2]);
  env.target = 0;
  for (uint32_t i = 0; i < 1024; i++)
    samples[2 * i] = samples[2 * i + 1] = 0x80000000;
  envelope_apply(&env, samples, 1024);
  CHECK(env.gain == 0 && samples[2046] == 0);
  CHECK((int32_t)samples[0] < 0 && samples[0] != 0x80000000);
  for (uint32_t i = 0; i < 1024; i++)
    CHECK(samples[2 * i] == samples[2 * i + 1]);
  for (uint32_t i = 0; i < 1024; i++)
    samples[2 * i] = samples[2 * i + 1] = 0x40000000;
  envelope_tail(samples, 1024, 0, 1024);
  CHECK(samples[0] == 0x40000000);
  CHECK(samples[2046] == 0 && samples[1024] > 0);
  envelope_tail(samples, 1024, 1024, 1024);
  for (uint32_t i = 0; i < 2048; i++)
    CHECK(samples[i] == 0);
}
static void test_modes(void) {
  /* Loop byte continuity, including files shorter than a DMA half and PCM24. */
  for (uint32_t bits = 16; bits <= 24; bits += 8) {
    feature_disk("# card\nLooping\nFree\nShort", 0, 37, bits);
    feature_mount();
    pulse();
    CHECK(host_active && host_starts == 1);
    for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
      CHECK(host_audio[2 * i] == sample_word(1, i % 37));
    for (uint32_t half = 0; half < 8; half++) {
      advance_half();
      CHECK(host_active);
      uint32_t offset = (half & 1u) * ONE_HALF_FRAMES * 2u;
      for (uint32_t i = 0; i < ONE_HALF_FRAMES; i++)
        CHECK(host_audio[offset + 2 * i] ==
              sample_word(1, (ONE_CACHE_FRAMES + half * ONE_HALF_FRAMES + i) % 37));
    }
    pulse();
    CHECK(host_active && host_audio[0] == sample_word(1, 0));
    browse_next();
    CHECK(!host_active);
    pulse();
    CHECK(host_audio[0] == sample_word(2, 0));
  }
  feature_disk("# card\nSequence\nFree\nShort", "A.wav\nB.wav\nB.wav\nC.wav\n",
               37, 16);
  feature_mount();
  const uint32_t order[] = {2, 2, 3, 1, 2};
  for (uint32_t i = 0; i < 5; i++) {
    pulse();
    CHECK(host_audio[0] == sample_word(order[i], 0));
  }
  browse_next();
  CHECK(!host_active);
  pulse(); /* Browse from first B to duplicate B, then Sequence advances to C.
            */
  CHECK(host_audio[0] == sample_word(3, 0));
  feature_disk("# card\nCombo\nFree\nShort", 0, 37, 16);
  feature_mount();
  pulse();
  CHECK(host_audio[0] == sample_word(2, 0));
  for (uint32_t i = 0; i < 4; i++) {
    advance_half();
    CHECK(host_active);
  }
  pulse();
  CHECK(host_audio[0] == sample_word(3, 0));
  feature_disk("# card\nRandom Sequence\nFree\nShort", 0, 37, 16);
  feature_mount();
  uint32_t seen = 0;
  for (uint32_t i = 0; i < 30; i++) {
    app_card.time += 7;
    pulse();
    uint32_t which = ((host_audio[0] >> 16) - 1) / 4000;
    CHECK(which >= 1 && which <= 3);
    seen |= 1u << which;
  }
  CHECK(seen == 14);
  feature_disk("# card\nRandom Loop\nFree\nShort", 0, 3001, 24);
  feature_mount();
  pulse();
  CHECK(host_audio[0] == sample_word(1, 0));
  uint32_t different = 0;
  for (uint32_t i = 0; i < 12; i++) {
    app_card.time += 3;
    pulse();
    uint32_t frame = ((host_audio[0] >> 8) - 100001u) / 37u;
    CHECK(frame < feature_frames);
    if (frame)
      different++;
    for (uint32_t k = 0; k < ONE_CACHE_FRAMES; k++)
      CHECK(host_audio[2 * k] == sample_word(1, (frame + k) % feature_frames));
    advance_half();
    for (uint32_t k = 0; k < ONE_HALF_FRAMES; k++)
      CHECK(host_audio[2 * k] ==
            sample_word(1, (frame + ONE_CACHE_FRAMES + k) % feature_frames));
  }
  CHECK(different > 0);
  feature_disk("# card\nTrigger\nSelect File\nMedium", 0, 37, 16);
  feature_mount();
  host_adc = 600; /* With three files, this selects offset 1 after settling. */
  pulse();
  CHECK(host_starts == 0);
  app_card.time++;
  app_tick();
  CHECK(host_starts == 0);
  app_card.time++;
  app_tick();
  CHECK(host_starts == 1 && host_audio[0] == sample_word(2, 0));
  CHECK(host_mode == CV_SELECT);
  host_adc = 10;
  app_tick();
  CHECK(host_audio[0] == sample_word(2, 0));
  pulse();
  app_card.time += 2;
  app_tick();
  CHECK(host_audio[0] == sample_word(3, 0));
  feature_disk("# card\nTrigger\nFree\nShort", 0, 37, 16);
  feature_mount();
  pulse();
  advance_half();
  CHECK(!host_active && host_led == 0);
  pulse();
  CHECK(host_active && host_starts == 2);
  feature_disk("# card\nGate\nFree\nShort", 0, 5000, 16);
  feature_mount();
  pulse();
  CHECK(host_active);
  host_pins |= 0x800u;
  app_tick(); /* Gate release fades queued samples then stops. */
  for (uint32_t i = 0; i < 4; i++)
    advance_half();
  CHECK(!host_active && host_led == 0);
  pulse();
  CHECK(host_active && host_starts == 2);
  feature_disk("# card\nFade\nFree\nShort", 0, 5000, 16);
  feature_mount();
  pulse();
  CHECK(host_audio[0] < sample_word(1, 0));
  pulse();
  CHECK(host_starts == 1); /* Retrigger waits for the old fade-out. */
  for (uint32_t i = 0; i < 4; i++)
    advance_half();
  CHECK(host_active && host_starts == 2);
  /* Config/playlist removal on a remount must not retain old policy. */
  feature_disk(0, 0, 37, 16);
  feature_mount();
  CHECK(host_mode == CV_QUANTIZE);
  pulse();
  CHECK(host_audio[0] == sample_word(1, 0));
}

static void test_sd_write(void) {
  for (uint32_t hc = 0; hc < 2; hc++) {
    Mock m = {0};
    m.modern = 1;
    m.hc = hc;
    SdIo io = {&m, xfer, select_mock, speed_mock, time_mock, 0};
    Sd s;
    uint8_t data[512];
    for (uint32_t i = 0; i < 512; i++)
      data[i] = (uint8_t)(i * 7u);
    CHECK(!sd_init(&s, &io));
    CHECK(!sd_write(&s, 77, data));
    CHECK(m.commands[m.n - 2] == 24 &&
          m.arguments[m.n - 2] == (hc ? 77u : 77u * 512u));
    CHECK(m.commands[m.n - 1] == 13);
    CHECK(!memcmp(m.write_data, data, 512));
    m.write_reject = 11;
    CHECK(sd_write(&s, 77, data) < 0 && s.ready);
    m.write_reject = 13;
    CHECK(sd_write(&s, 77, data) < 0 && s.ready);
    m.write_reject = 0;
    m.write_status = 32;
    CHECK(sd_write(&s, 77, data) < 0);
    m.write_status = 0;
    m.write_timeout = 1;
    CHECK(sd_write(&s, 77, data) < 0);
    m.write_timeout = 0;
    CHECK(!sd_write(&s, 77, data));
  }
}
static uint32_t poll_ui(Ui *u, uint32_t down, int32_t step, uint32_t adc,
                        uint32_t *time, const Settings *s) {
  return ui_poll(u, down, step, adc, ++*time, s);
}
static void test_ui(void) {
  Ui u;
  Settings s;
  settings_default(&s);
  s.play = PLAY_TRIGGER;
  s.cv = CV_FREE;
  uint32_t t = 0xfffffff0u;
  ui_init(&u, 0, 128, t, &s);
  CHECK(!poll_ui(&u, 1, 0, 128, &t, &s));
  CHECK(!poll_ui(&u, 0, 0, 128, &t, &s)); /* contact bounce */
  for (uint32_t i = 0; i < 3; i++)
    CHECK(!poll_ui(&u, 1, 0, 128, &t, &s));
  CHECK(u.button && !u.menu);
  CHECK(!ui_poll(&u, 0, 0, 128, t, &s)); /* repeated same ms is not debounce */
  CHECK(!poll_ui(&u, 0, 0, 128, &t, &s));
  CHECK(!poll_ui(&u, 0, 0, 128, &t, &s));
  CHECK(poll_ui(&u, 0, 0, 128, &t, &s) == UI_CLICK);
  for (uint32_t i = 0; i < 3; i++)
    CHECK(!poll_ui(&u, 1, 0, 128, &t, &s));
  t += 400;
  CHECK(ui_poll(&u, 1, 0, 128, t, &s) == UI_ENTER && u.menu);
  CHECK(!ui_poll(&u, 1, 1, 128, t, &s) && u.play == PLAY_GATE);
  CHECK(!ui_poll(&u, 1, -2, 128, t, &s) && u.play == (PlayMode)(PLAY_COUNT - 1));
  CHECK(!ui_poll(&u, 1, 0, 900, t, &s) && u.cv == CV_FREE && u.kind == 0); /* FILE gesture locks out LFO edits. */
  CHECK(!poll_ui(&u, 0, 0, 900, &t, &s));
  CHECK(!poll_ui(&u, 0, 0, 900, &t, &s));
  CHECK(poll_ui(&u, 0, 0, 900, &t, &s) == UI_COMMIT && !u.menu);
  /* Every mode can be selected in either direction, with no PLAY click. */
  for (uint32_t mode = 0; mode < PLAY_COUNT; mode++) {
    s.play = (PlayMode)mode;
    ui_init(&u, 0, 500, t, &s);
    for (uint32_t i = 0; i < 3; i++)
      CHECK(!poll_ui(&u, 1, 0, 500, &t, &s));
    t += 400;
    CHECK(poll_ui(&u, 1, 1, 500, &t, &s) == UI_ENTER);
    CHECK(u.play == (PlayMode)((mode + 1) % PLAY_COUNT));
    CHECK(!poll_ui(&u, 1, -1, 500, &t, &s) && u.play == (PlayMode)mode);
    u.kind = 0;
    u.led_time = 0;
    CHECK(ui_led(&u, 0) == 0 && ui_led(&u, 349) == 0);
    CHECK(ui_led(&u, 350) == 2 && ui_led(&u, 550) == 0);
    u.kind = 1;
    u.cv = CV_SELECT;
    CHECK(ui_led(&u, 350) == 1 && ui_led(&u, 550) == 0);
  }
  /* Every displayed selection begins at pulse one after a settling pause,
   * with a full count and gap, even across timer wrap. */
  for (uint32_t mode = 0; mode < PLAY_COUNT; mode++) {
    s.play = (PlayMode)mode;
    ui_init(&u, 0, 500, 0xfffffff0u, &s);
    u.button = 1;
    u.press_time = 0xfffffff0u;
    uint32_t when = 0x200u;
    CHECK(ui_poll(&u, 1, 0, 500, when, &s) == UI_ENTER);
    for (uint32_t ms = 0; ms < (mode + 1) * 400u + 1000u; ms++) {
      uint32_t expected = ms < (mode + 1) * 400u && ms % 400u < 200u ? 2u : 0u;
      CHECK(ui_led(&u, when + 350u + ms) == expected);
    }
    when += 577;
    CHECK(!ui_poll(&u, 1, 1, 900, when, &s));
    CHECK(u.kind == 0 && u.cv == s.cv && u.file_edit);
    CHECK(ui_led(&u, when + 349) == 0 && ui_led(&u, when + 350) == 2);
    CHECK(!ui_poll(&u, 1, 0, 0, when + 10, &s));
    CHECK(u.kind == 0 && u.cv == s.cv && u.led_time == when);
  }
  /* Pitch-only editing still selects CV mode and restarts its blue count. */
  ui_init(&u, 0, 128, 100, &s);
  u.button = 1;
  u.press_time = 100;
  CHECK(ui_poll(&u, 1, 0, 900, 501, &s) == UI_ENTER);
  CHECK(u.kind == 1 && u.cv == CV_SELECT && !u.file_edit);
  CHECK(ui_led(&u, 850) == 0 && ui_led(&u, 851) == 1);
  CHECK(!ui_poll(&u, 1, 1, 900, 900, &s));
  CHECK(u.kind == 0 && u.file_edit && u.cv == s.cv);
  /* A trailing FILE detent during a quick press must not eat the click or
   * enter mode editing, even when pitch CV moves at the same time. */
  ui_init(&u, 0, 128, 1000, &s);
  t = 1000;
  for (uint32_t i = 0; i < 3; i++)
    CHECK(!poll_ui(&u, 1, 0, 128, &t, &s));
  t += 50;
  CHECK(!ui_poll(&u, 1, 1, 900, t, &s));
  CHECK(!u.menu && u.play == s.play && u.cv == s.cv);
  for (uint32_t i = 0; i < 2; i++)
    CHECK(!poll_ui(&u, 0, 0, 900, &t, &s));
  CHECK(poll_ui(&u, 0, 0, 900, &t, &s) == UI_CLICK);
  /* A button held across reset must never trigger or enter a menu. */
  ui_init(&u, 1, 128, t, &s);
  t += 1000;
  CHECK(!ui_poll(&u, 1, 0, 128, t, &s));
  for (uint32_t i = 0; i < 3; i++)
    CHECK(!poll_ui(&u, 0, 0, 128, &t, &s));
  CHECK(!u.boot_held && !u.button);
}
static void test_recall(void) {
  Fat f;
  Recall r;
  Entry es[8], e;
  uint32_t idx = 999;
  feature_disk("# card\nTrigger\nFree\nShort", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  CHECK(fat_list(&f, es, 8) == 3);
  uint8_t wav_before[512];
  memcpy(wav_before, block(24), 512);
  CHECK(recall_open(&r, &f, es, 3, &idx) == 0 && r.available);
  CHECK(fat_find(&f, "ONESTATE.BIN", &e) == 1 && e.size == 1024);
  CHECK(!memcmp(wav_before, block(24), 512));
  uint32_t created_writes = writes;
  CHECK(!recall_save(&r, &f, es + 1, 1));
  CHECK(writes == created_writes + 1 && write_lbas[writes - 1] == r.lba[1]);
  CHECK(recall_open(&r, &f, es, 3, &idx) == 1 && idx == 1);
  CHECK(!recall_save(&r, &f, es + 2, 2));
  CHECK(recall_open(&r, &f, es, 3, &idx) == 1 && idx == 2);
  uint32_t before = writes;
  CHECK(!recall_save(&r, &f, es + 2, 2) && writes == before);
  /* Reordering the playlist restores the name, not an unrelated file index. */
  Entry swap = es[0];
  es[0] = es[2];
  es[2] = swap;
  CHECK(recall_open(&r, &f, es, 3, &idx) == 1 && idx == 0);
  swap = es[0];
  es[0] = es[2];
  es[2] = swap;
  CHECK(recall_open(&r, &f, es, 3, &idx) == 1 && idx == 2);
  /* A torn next record must leave the previous sector valid. */
  tear_write = writes + 1;
  CHECK(recall_save(&r, &f, es, 0) < 0);
  tear_write = 0;
  CHECK(recall_open(&r, &f, es, 3, &idx) == 1 && idx == 2);
  CHECK(!memcmp(wav_before, block(24), 512));
  /* Corruption of the latest record falls back to the other valid one. */
  CHECK(!recall_save(&r, &f, es + 1, 1));
  block(r.lba[r.slot])[80] ^= 1;
  CHECK(recall_open(&r, &f, es, 3, &idx) == 1 && idx == 2);
  /* Read-only flags and unrelated content are never overwritten. */
  CHECK(fat_find(&f, "ONESTATE.BIN", &e) == 1);
  for (uint32_t i = 0; i < 16; i++)
    if (!memcmp(block(21) + i * 32, "ONESTATEBIN", 11))
      block(21)[i * 32 + 11] |= 1;
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  before = writes;
  CHECK(recall_open(&r, &f, es, 3, &idx) < 0 && !r.available &&
        writes == before);
}
static void test_build_info(void) {
  Fat f;
  Entry e;
  File file;
  uint8_t text[512];
  uint32_t got;
  disk16();
  memset(block(24), 0x73, 512);
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  CHECK(fat_build_info(&f, 15) == 1);
  uint32_t creation_writes = writes;
  CHECK(fat_find(&f, "ONEBUILD.TXT", &e) == 1 && e.size == 512);
  CHECK(!file_open(&file, &f, &e));
  CHECK(!file_read(&file, text, sizeof(text), &got) && got == 512);
  CHECK(!memcmp(text, "ONE firmware build 15\r\n", 23));
  CHECK(fat_build_info(&f, 15) == 0 && writes == creation_writes);
  CHECK(fat_build_info(&f, 16) == 1 && writes == creation_writes + 1);
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  CHECK(fat_build_info(&f, 16) == 0 && writes == creation_writes + 1);
  CHECK(fat_build_info(&f, 15) == 1); /* Downgrades report the running build. */
  CHECK(fat_build_info(&f, 4294967295u) == 1);
  uint32_t build_lba = f.data + (e.cluster - 2) * f.spc;
  CHECK(!memcmp(block(build_lba), "ONE firmware build 4294967295\r\n", 31));
  for (uint32_t i = 0; i < 512; i++)
    CHECK(block(24)[i] == 0x73);
  /* A torn data update is detected; the next mount repairs or recognizes
   * the complete desired record without touching the existing sample. */
  tear_write = writes + 1;
  CHECK(fat_build_info(&f, 15) < 0);
  tear_write = 0;
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  CHECK(fat_build_info(&f, 15) >= 0);
  CHECK(!memcmp(block(build_lba), "ONE firmware build 15\r\n", 23));
  uint32_t before_read_failure = writes;
  fail_lba = build_lba;
  CHECK(fat_build_info(&f, 16) < 0 && writes == before_read_failure);
  fail_lba = 0xffffffffu;
  /* Refuse existing foreign content and read-only files. */
  memset(block(build_lba), 'X', 512);
  uint32_t before = writes;
  CHECK(fat_build_info(&f, 15) < 0 && writes == before);
  for (uint32_t i = 0; i < 16; i++)
    if (!memcmp(block(21) + i * 32, "ONEBUILDTXT", 11))
      block(21)[i * 32 + 11] |= 1;
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  CHECK(fat_build_info(&f, 15) < 0 && writes == before);
  /* Inject failure at each creation write. Existing sample/data stay intact. */
  for (uint32_t fail = 1; fail <= creation_writes; fail++) {
    disk16();
    memset(block(24), 0x73, 512);
    CHECK(!fat_mount(&f, disk, 0));
    f.write = disk_write;
    fail_write = fail;
    CHECK(fat_build_info(&f, 15) < 0);
    CHECK(!memcmp(block(21), "SAMPLE  WAV", 11));
    for (uint32_t i = 0; i < 512; i++)
      CHECK(block(24)[i] == 0x73);
  }
  disk16();
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(fat_build_info(&f, 15) < 0 && !writes); /* No writer. */
}
#ifdef ONE_PROFILE
static void test_profile_file(void) {
  Fat f;
  Entry e;
  uint8_t report[1024];
  memset(report, ' ', sizeof(report));
  memcpy(report, "Hamburger Wavy profile 1\n", sizeof("Hamburger Wavy profile 1\n")-1);
  disk16();
  memset(block(24), 0x73, 512);
  CHECK(!fat_mount(&f, disk, 0)); f.write = disk_write;
  CHECK(!fat_profile_write(&f, report));
  CHECK(fat_find(&f, "ONEPROF.TXT", &e) == 1 && e.size == 1024);
  CHECK(!fat_profile_write(&f, report));
  for (uint32_t i=0; i<512; ++i) CHECK(block(24)[i]==0x73);
  uint32_t lba = f.data + (e.cluster-2)*f.spc;
  memset(block(lba), 'X', 512);
  uint32_t before=writes;
  CHECK(fat_profile_write(&f,report)<0 && writes==before);
  /* A refused foreign report must leave the shared directory cache invalid. */
  CHECK(fat_find(&f,"SAMPLE.WAV",&e)==1 && e.cluster==3);
}
#endif
#ifdef ONE_CV_DIAG
static void test_cv_file(void) {
  Fat f;
  Entry e;
  uint8_t report[1024];
  memset(report, ' ', sizeof(report));
  memcpy(report, "Hamburger Wavy CV 1\n", sizeof("Hamburger Wavy CV 1\n")-1);
  disk16();
  memset(block(24), 0x73, 512);
  CHECK(!fat_mount(&f, disk, 0)); f.write = disk_write;
  CHECK(!fat_cv_write(&f, report));
  CHECK(fat_find(&f, "ONECV.TXT", &e) == 1 && e.size == 1024);
  CHECK(!fat_cv_write(&f, report));
  for (uint32_t i=0; i<512; ++i) CHECK(block(24)[i]==0x73);
  uint32_t lba = f.data + (e.cluster-2)*f.spc;
  memset(block(lba), 'X', 512);
  uint32_t before=writes;
  CHECK(fat_cv_write(&f,report)<0 && writes==before);
  /* A refused foreign report must leave the shared directory cache invalid. */
  CHECK(fat_find(&f,"SAMPLE.WAV",&e)==1 && e.cluster==3);
}
#endif
static void test_state_allocation(void) {
  Fat f;
  uint32_t area[2];
  /* FAT16 mirrored copies: creation changes only free allocation + directory.
   */
  clear_disk();
  boot(block(0), 5000, 20, 1, 32, 2);
  put16(block(1) + 6, 0xffff);
  put16(block(21) + 6, 0xffff);
  entry(block(41), "KEEP    WAV", 3, 32);
  memset(block(44), 0x6a, 512);
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  CHECK(!fat_state_area(&f, area) && area[0] == 43 && area[1] == 45);
  CHECK(!memcmp(block(1), block(21), 512));
  for (uint32_t i = 0; i < 512; i++)
    CHECK(block(44)[i] == 0x6a);
  for (uint32_t i = 0; i < writes; i++)
    CHECK(write_lbas[i] == 1 || write_lbas[i] == 21 || write_lbas[i] == 41 ||
          write_lbas[i] == 43 || write_lbas[i] == 45);
  /* Failure at every creation write never touches the sample's data. */
  uint32_t steps = writes;
  for (uint32_t fail = 1; fail <= steps; fail++) {
    clear_disk();
    boot(block(0), 5000, 20, 1, 32, 2);
    put16(block(1) + 6, 0xffff);
    put16(block(21) + 6, 0xffff);
    entry(block(41), "KEEP    WAV", 3, 32);
    memset(block(44), 0x6a, 512);
    CHECK(!fat_mount(&f, disk, 0));
    f.write = disk_write;
    fail_write = fail;
    CHECK(fat_state_area(&f, area) < 0);
    for (uint32_t i = 0; i < 512; i++)
      CHECK(block(44)[i] == 0x6a);
    CHECK(!memcmp(block(41), "KEEP    WAV", 11));
  }
  /* FAT32 active FAT, FSInfo and its backup, both 1- and 2-sector clusters. */
  for (uint32_t spc = 1; spc <= 2; spc++) {
    clear_disk();
    boot(block(0), spc == 1 ? 70000 : 140000, 600, 32, 0, 2);
    block(0)[13] = (uint8_t)spc;
    put16(block(0) + 40, 0x81);
    put16(block(0) + 48, 1);
    put16(block(0) + 50, 6);
    put32(block(632) + 8, 0x0fffffff);
    put32(block(632) + 12, 0x0fffffff);
    put32(block(632) + 16, 0xa0000000); /* free entry, reserved high nibble */
    memset(block(32), 0x55, 512);       /* inactive FAT must remain untouched */
    for (uint32_t i = 1; i <= 7; i += 6) {
      put32(block(i), 0x41615252);
      put32(block(i) + 484, 0x61417272);
      put32(block(i) + 508, 0xaa550000);
      put32(block(i) + 488, 100);
    }
    entry(block(1232), "KEEP    WAV", 3, 32);
    uint32_t sample = 1232 + spc;
    memset(block(sample), 0x7b, 512);
    CHECK(!fat_mount(&f, disk, 0));
    f.write = disk_write;
    CHECK(!fat_state_area(&f, area));
    CHECK(area[0] == 1232 + 2 * spc && area[1] == area[0] + 1);
    for (uint32_t i = 0; i < 512; i++)
      CHECK(block(sample)[i] == 0x7b && block(32)[i] == 0x55);
    CHECK(block(632)[19] == (spc == 1 ? 0xa0 : 0xaf));
    CHECK(block(1)[488] == 255 && block(7)[488] == 255);
  }
  /* Full FAT / full fixed root => no writes and normal read APIs remain usable.
   */
  clear_disk();
  boot(block(0), 5000, 20, 1, 32, 1);
  for (uint32_t i = 0; i < 32; i++)
    entry(block(21 + (i >> 4)) + (i & 15) * 32, "FULL    WAV", 3, 32);
  CHECK(!fat_mount(&f, disk, 0));
  f.write = disk_write;
  CHECK(fat_state_area(&f, area) < 0 && !writes);
  CHECK(fat_build_info(&f, 15) < 0 && !writes);
}
static void click_play(void) {
  host_pins &= ~0x200u;
  for (uint32_t i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  host_pins |= 0x200u;
  for (uint32_t i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
}
static void test_playback_error_recovery_and_read_ahead(void) {
  feature_disk("# test\nTrigger\nQuantize\nShort", 0, 5000, 24);
  feature_mount();
  host_adc = 512;
  click_play();
  CHECK(host_active && host_led == 2);
  /* Forced deadline failure must recover on another quick button click. */
  host_offset = ONE_HALF_FRAMES * 8u;
  host_unsafe = 1;
  app_tick();
  CHECK(!host_active && host_led == 1);
  host_unsafe = 0;
  click_play();
  CHECK(host_active && host_led == 2 && host_audio[0] == sample_word(1, 0));
  /* Start a fresh run with DMA advancing while sectors arrive. Reading this
   * 24-bit half consumes >384 frames: too late if first started at a boundary.
   * Pre-reading while the initial cache plays leaves time to publish safely. */
  feature_mount();
  host_transfer_advance = 160;
  click_play();
  CHECK(host_active && host_led == 2);
  if (host_offset < ONE_HALF_FRAMES * 8u)
    host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  CHECK(host_active && host_audio[0] == sample_word(1, ONE_CACHE_FRAMES));
  host_transfer_advance = 0;
}
static void test_fade_gate_read_ahead(void) {
  static const char *configs[] = {"# test\nFade\nQuantize\nShort",
                                  "# test\nGate\nQuantize\nShort"};
  for (uint32_t mode = 0; mode < 2; mode++) {
    feature_disk(configs[mode], 0, 5000, 24);
    feature_mount();
    host_transfer_budget = 3584; /* 448 frames, below one half. */
    host_transfer_advance = 128;
    click_play();
    CHECK(host_active && host_led == 2 && host_audio[0] == 0);
    if (host_offset < ONE_HALF_FRAMES * 8u)
      host_offset = ONE_HALF_FRAMES * 8u;
    app_tick();
    CHECK(host_active && host_audio[0] == sample_word(1, ONE_CACHE_FRAMES));
    host_transfer_advance = 0;
    /* Prefetch the next raw half, then request release/retrigger. The first
     * published release half must fade this data, rather than skipping it or
     * playing a prefetched full-volume half before starting the envelope. */
    app_tick();
    click_play();
    CHECK(host_active && host_starts == 1);
    host_offset = 0;
    app_tick();
    CHECK(host_audio[ONE_HALF_FRAMES * 2u] < sample_word(1, ONE_CACHE_FRAMES + ONE_HALF_FRAMES));
    CHECK(host_audio[ONE_HALF_FRAMES * 2u + 2 * 255] == 0);
    for (uint32_t i = 0; i < 3; i++)
      advance_half();
    if (mode == 0)
      CHECK(host_active && host_starts == 2 && host_led == 2);
    else
      CHECK(!host_active && host_starts == 1 && host_led == 0);
  }
}
static void test_click_overlapping_file_browse(void) {
  static const char *configs[] = {"# test\nTrigger\nQuantize\nShort",
                                "# test\nGate\nQuantize\nShort",
                                "# test\nFade\nQuantize\nShort"};
  for (uint32_t mode = 0; mode < 3; mode++) {
    feature_disk(configs[mode], 0, 5000, 16);
    feature_mount();
    host_pins &= ~0x200u;
    for (uint32_t i = 0; i < 3; i++) {
      app_card.time++;
      app_tick();
    }
    browse_next(); /* Encoder completes its last detent during the press. */
    host_pins |= 0x200u;
    for (uint32_t i = 0; i < 3; i++) {
      app_card.time++;
      app_tick();
    }
    CHECK(host_active && host_starts == 1 && host_led == 2);
    if (mode == 0)
      CHECK(host_audio[0] == sample_word(2, 0));
    for (uint32_t i = 0; i < 3; i++)
      advance_half();
    CHECK(host_active && host_starts == 1 && host_led == 2);
    /* Coalesce the last encoder transition with the debounced button release.
     * In Gate mode the old click-before-browse ordering cleared manual_gate. */
    feature_mount();
    host_pins &= ~0x200u;
    for (uint32_t i = 0; i < 3; i++) {
      app_card.time++;
      app_tick();
    }
    static const uint32_t ab[4] = {2, 0, 1, 3};
    for (uint32_t i = 0; i < 4; i++) {
      host_pins = (host_pins & ~0xa0u) | ((ab[i] & 1u) << 5) |
                  ((ab[i] & 2u) << 6);
      if (i)
        host_pins |= 0x200u;
      app_card.time++;
      app_tick();
    }
#ifdef ONE_TOLERANT_ENCODER
    /* The click waits for the filtered detent, then follows the browse. */
    CHECK(!host_active);
    for (uint32_t i = 0; i < ENCODER_RELEASE_MS + 2u; i++) {
      app_card.time++;
      app_tick();
    }
#endif
    CHECK(host_active && host_starts == 1 && host_led == 2);
    for (uint32_t i = 0; i < 4; i++)
      advance_half();
    CHECK(host_active && host_starts == 1 && host_led == 2);
  }
}
static void test_build_info_write_failure_does_not_block_playback(void) {
  feature_disk("# test\nTrigger\nQuantize\nShort", 0, 5000, 16);
  feature_mount();
  Fat f;
  Entry e;
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(fat_find(&f, "ONEBUILD.TXT", &e) == 1);
  uint32_t lba = f.data + (e.cluster - 2) * f.spc;
  block(lba)[19] = '0'; /* Owned record now differs, requiring an update. */
  app_card.write_reject = 13;
  app_init();
  app_tick();
  CHECK(!host_active && !host_starts);
  click_play();
  CHECK(host_active && host_starts == 1 && host_led == 2);
  CHECK(block(lba)[19] == '0');
  app_card.write_reject = 0;
}
static void test_front_panel_and_remember(void) {
  feature_disk("# card\nTrigger\nFree\nShort", 0, 37, 16);
  feature_mount();
  browse_next();
  click_play();
  CHECK(host_active && host_audio[0] == sample_word(2, 0));
  feature_mount();
  pulse();
  CHECK(host_audio[0] == sample_word(2, 0)); /* power cycle recalls B */
  browse_next();
  pulse(); /* external gate plays C but must not save it */
  CHECK(host_audio[0] == sample_word(3, 0));
  feature_mount();
  pulse();
  CHECK(host_audio[0] == sample_word(2, 0));
  feature_mount();
  host_pins &= ~0x200u;
  for (uint32_t i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  app_card.time += 400;
  app_tick();
  CHECK(!host_active && !host_starts);
  browse_next(); /* While held this changes Trigger -> Gate, not the file. */
  host_pins |= 0x200u;
  for (uint32_t i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  CHECK(!host_starts && !host_active);
  click_play();
  CHECK(host_active && host_starts == 1);
  for (uint32_t i = 0; i < 4 && host_active; i++)
    advance_half();
  /* Menu changes are live but config still wins after reset. */
  feature_mount();
  CHECK(host_mode == CV_FREE);
  host_pins &= ~0x200u;
  for (uint32_t i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  app_card.time += 400;
  host_adc = 900;
  app_tick();
  CHECK(!host_active);
  host_pins |= 0x200u;
  for (uint32_t i = 0; i < 3; i++) {
    app_card.time++;
    app_tick();
  }
  CHECK(host_mode == CV_SELECT && !host_starts);
}

#ifdef ONE_FLIP
static void test_reverse_index(void) {
  for (uint32_t type = 16; type <= 32; type += 16) {
    for (uint32_t fragmented = 0; fragmented <= 1; ++fragmented) {
      clear_disk();
      uint32_t fat = type == 16 ? 1u : 32u;
      boot(block(0), type == 16 ? 5000 : 70000, type == 16 ? 20 : 600,
           fat, type == 16 ? 32 : 0, 1);
      uint32_t clusters[70];
      for (uint32_t i = 0; i < 70; ++i) clusters[i] = fragmented ? 3u + i * 37u % 101u : i + 3u;
      for (uint32_t i = 0; i < 70; ++i) {
        uint32_t next = i == 69 ? (type == 16 ? 0xffffu : 0x0fffffffu) : clusters[i+1];
        if (type == 16) put16(block(fat) + clusters[i]*2u, next);
        else put32(block(fat) + clusters[i]*4u, next);
      }
      Fat f; File file; FileIndex index;
      Entry e = {clusters[0],70u*512u,32,{0}};
      CHECK(!fat_mount(&f,disk,0) && f.type == type);
      for (uint32_t i=0; i<70; ++i) {
        uint8_t *p=block(f.data+(clusters[i]-2u)*f.spc);
        for (uint32_t j=0; j<512; ++j) p[j]=(uint8_t)((i*512u+j)*17u+3u);
      }
      CHECK(!file_open(&file,&f,&e) && !file_index_build(&file,&index));
      CHECK(index.count <= 32 && index.contiguous == !fragmented);
      uint8_t data[128]; uint32_t got;
      for (uint32_t i=70; i; --i) {
        uint32_t position=(i-1u)*512u+117u;
        CHECK(!file_seek_indexed(&file,&index,position));
        CHECK(!file_read(&file,data,sizeof(data),&got) && got==sizeof(data));
        for (uint32_t j=0;j<got;++j) CHECK(data[j]==(uint8_t)((position+j)*17u+3u));
        uint32_t saved=file.cluster;
        CHECK(!file_seek(&file,position) && file.cluster==saved);
      }
      uint8_t window[700], boundary[1024];
      for(uint32_t i=68;i;i-=2) {
        uint32_t position=(i-1u)*512u+117u;
        CHECK(!file_seek_indexed(&file,&index,position));
        CHECK(!file_read_window(&file,&index,window,sizeof(window),boundary));
        for(uint32_t j=0;j<sizeof(window);++j)
          CHECK(window[j]==(uint8_t)((position+j)*17u+3u));
      }
      CHECK(!file_seek_indexed(&file,&index,file.size));
      CHECK(file_seek_indexed(&file,&index,file.size+1u)<0);
      uint32_t last=clusters[69];
      if(type==16) put16(block(fat)+last*2u,clusters[0]);
      else put32(block(fat)+last*4u,clusters[0]);
      f.fat_lba=0xffffffffu;
      CHECK(file_index_build(&file,&index)<0 && !index.count);
      if(type==16) put16(block(fat)+clusters[10]*2u,0xffffu);
      else put32(block(fat)+clusters[10]*4u,0x0fffffffu);
      f.fat_lba=0xffffffffu;
      CHECK(file_index_build(&file,&index)<0);
    }
  }
}
static void test_reverse_wave(void) {
  static uint32_t output[ONE_CACHE_FRAMES*2u+2u];
  static uint8_t scratch[ONE_CACHE_FRAMES*3u], second_scratch[ONE_CACHE_FRAMES*3u];
  const uint32_t lengths[]={1,37,1025,5000};
  for (uint32_t bits=16;bits<=24;bits+=8) {
    for (uint32_t k=0;k<4;++k) {
      uint32_t length=lengths[k];
      feature_disk("# test\nFlip\nQuantize\nShort",0,length,bits);
      Fat f; File file; Entry e; Wave w; FileIndex index; Settings settings;
      CHECK(!fat_mount(&f,disk,0));
      CHECK(settings_load(&f,&settings)==1 && settings.play==PLAY_FLIP);
      CHECK(fat_find(&f,"A.wav",&e)==1 && !file_open(&file,&f,&e));
      CHECK(!wave_open(&file,&w) && !file_index_build(&file,&index));
      for (uint32_t reverse=0;reverse<2;++reverse) {
        uint32_t cursor=length-1u, start=cursor;
        output[0]=0x12345678;output[ONE_CACHE_FRAMES*2u+1u]=0x87654321;
        CHECK(!wave_read_direction(&file,&w,&index,&cursor,reverse,output+1,
                                   ONE_CACHE_FRAMES,scratch,sizeof(scratch)));
        for(uint32_t i=0;i<ONE_CACHE_FRAMES;++i) {
          uint32_t frame=reverse ? (start+length-(i%length))%length : (start+i)%length;
          CHECK(output[1+i*2u]==sample_word(1,frame) && output[2+i*2u]==sample_word(1,frame));
        }
        CHECK(cursor==wave_frame_offset(start,ONE_CACHE_FRAMES,length,reverse));
        CHECK(output[0]==0x12345678 && output[ONE_CACHE_FRAMES*2u+1u]==0x87654321);
      }
      if(length==5000) {
        uint32_t cursor=4999;
        CHECK(!file_index_build(&file,&index));
        for(uint32_t block_index=0;block_index<4;++block_index) {
          uint32_t before=reads,start=cursor;
          uint8_t *raw=block_index==3 ? second_scratch : scratch;
          CHECK(!wave_read_direction(&file,&w,&index,&cursor,1,output+1,
                                     ONE_HALF_FRAMES,raw,sizeof(scratch)));
          for(uint32_t i=0;i<ONE_HALF_FRAMES;++i)
            CHECK(output[1+i*2u]==sample_word(1,start-i));
          /* Warm reverse windows share their boundary without an extra read. */
          if(block_index==1 || block_index==2) CHECK(reads-before==ONE_HALF_FRAMES*(bits/8u)/512u);
        }
      }
    }
  }
}
static void flip_finish_transition(uint32_t starts) {
  for(uint32_t i=0;i<12 && host_starts<starts+1u;++i) {
    app_card.n=0;
    if(host_active) advance_half(); else app_tick();
    app_tick(); /* The foreground runs repeatedly between DMA boundaries. */
  }
  CHECK(host_active && host_starts==starts+1u && host_led==2);
}
static void test_flip_transport(void) {
  for(uint32_t bits=16;bits<=24;bits+=8) {
    feature_disk("# test\nFlip\nQuantize\nShort",0,5000,bits);
    feature_mount();
    pulse();CHECK(!host_active && !host_starts); /* gate never starts transport */
    host_pins|=0x800u;app_tick();
    click_play();CHECK(host_active && host_starts==1);
    CHECK(host_audio[0]==0 && host_audio[1024]==sample_word(1,512));
    host_offset=123u*8u;
    uint32_t stops=host_stops,unchanged=host_audio[123u*2u];
    pulse();
    CHECK(host_starts==1 && host_stops==stops && host_audio[123u*2u]==unchanged);
    CHECK(host_audio[ONE_HALF_FRAMES*2u+1022u]==0);
    for(uint32_t i=ONE_HALF_FRAMES*2u+1024u;i<ONE_CACHE_FRAMES*2u;++i) CHECK(host_audio[i]==0);
    host_silent_stop=1;
    host_offset=ONE_HALF_FRAMES*8u+511u*8u;app_tick();
    CHECK(host_starts==1 && host_silent_stop);
    host_offset=ONE_HALF_FRAMES*8u+512u*8u;app_tick();
    CHECK(host_starts==2 && host_active && !host_silent_stop);
    for(uint32_t i=512;i<ONE_CACHE_FRAMES;++i)
      CHECK(host_audio[i*2u]==sample_word(1,(123u+5000u-i)%5000u));
    app_card.n=0; advance_half();
    host_offset=ONE_HALF_FRAMES*8u+77u*8u;
    uint32_t captured=(123u+10000u-ONE_HALF_FRAMES-77u)%5000u;
    pulse();flip_finish_transition(2);
    for(uint32_t i=512;i<ONE_CACHE_FRAMES;++i)
      CHECK(host_audio[i*2u]==sample_word(1,(captured+i)%5000u));
    /* An edge while SPI is priming coalesces without nested card commands. */
    app_card.n=0;host_offset=99u*8u;pulse();host_flip_edges=2;
    uint32_t frame=(captured+99u)%5000u;
    flip_finish_transition(3);
    CHECK(!host_flip_edges);
    CHECK(host_audio[1024]==sample_word(1,(frame+512u)%5000u));
    click_play();CHECK(!host_active && host_led==0);
    pulse();CHECK(!host_active);
    click_play();CHECK(host_active && host_audio[1024]==sample_word(1,512));
    browse_next();CHECK(!host_active);
    click_play();CHECK(host_active && host_audio[1024]==sample_word(2,512));
    /* A short file loops safely in both directions too. */
    feature_disk("# test\nFlip\nFree\nShort",0,37,bits);feature_mount();
    click_play();host_offset=5u*8u;pulse();flip_finish_transition(1);
    CHECK(host_audio[1024]==sample_word(1,(5u+37u-512u%37u)%37u));
    click_play();CHECK(!host_active);
    /* Capture the audible frame when an edge arrives inside a card read. */
    feature_disk("# test\nFlip\nQuantize\nShort",0,5000,bits);feature_mount();
    host_pins|=0x800u;click_play();app_card.n=0;advance_half();
    host_offset=ONE_HALF_FRAMES*8u+400u*8u;
    host_flip_inject=1;host_transfer_advance=8;
    app_tick();CHECK(!host_flip_inject);
    host_transfer_advance=0;
    uint32_t heard = bits==16 ? (host_flip_captured>>16)-4001u : ((host_flip_captured>>8)-100001u)/37u;
    flip_finish_transition(1);
    CHECK(host_audio[1024]==sample_word(1,(heard+5000u-512u)%5000u));
    /* A stationary fade DMA is a recoverable error, not a stuck red light. */
    host_pins|=0x800u;app_tick();pulse();
    app_card.time+=1001;app_tick();CHECK(!host_active && host_led==1);
    host_pins|=0x800u;app_tick();click_play();CHECK(host_active);
    /* Card failure during reverse priming stops and takes the recovery path. */
    app_card.n=0;host_pins|=0x800u;app_tick();pulse();
    app_card.token_bad=1;
    for(uint32_t i=0;i<4 && host_active;++i) advance_half();
    CHECK(!host_active);
    app_card.token_bad=0;app_card.n=0;app_card.time+=1001;app_tick();
    click_play();CHECK(host_active);
    click_play();CHECK(!host_active);
  }
  /* Preparation that loses its safe window must preserve queued raw audio. */
  feature_disk("# test\nFlip\nQuantize\nShort",0,5000,16);feature_mount();
  host_pins|=0x800u;click_play();host_offset=123u*8u;
  uint32_t stops=host_stops;
  host_commit_move=2;pulse();
  CHECK(host_active && host_starts==1 && host_stops==stops && host_led==2);
  host_offset=ONE_HALF_FRAMES*8u+64u*8u;app_tick();
  CHECK(host_active && host_starts==1 && host_stops==stops && host_led==2);
  uint32_t word=sample_word(1,ONE_CACHE_FRAMES);
  uint32_t faded=(uint32_t)((((int64_t)((int32_t)word/256))*32704)>>15)<<8;
  CHECK(host_audio[0]==faded); /* Original queued frame, faded exactly once. */
  host_offset=512u*8u;app_tick();
  CHECK(host_active && host_starts==2 && host_led==2);
  CHECK(host_audio[1024]==sample_word(1,(123u+5000u-512u)%5000u));
  click_play();CHECK(!host_active);
  /* A late gate simply waits for a fresh half instead of preparing too late. */
  feature_mount();click_play();host_offset=(ONE_HALF_FRAMES-64u)*8u;pulse();
  CHECK(host_active && host_starts==1 && host_led==2);
  host_offset=ONE_HALF_FRAMES*8u+32u*8u;app_tick();
  host_offset=512u*8u;app_tick();
  CHECK(host_active && host_starts==2 && host_led==2);
  click_play();CHECK(!host_active);
  /* Cross either physical half boundary after the poll/edge capture. */
  for(uint32_t initial=0;initial<2;++initial) {
    const uint32_t offsets[]={0,64,511};
    for(uint32_t k=0;k<3;++k) {
      feature_mount();click_play();
      if(initial) advance_half();
      host_offset=((initial+1u)*ONE_HALF_FRAMES-1u)*8u;
      host_pins|=0x800u;app_tick();
      host_offset_target=(initial^1u)*ONE_HALF_FRAMES*8u+offsets[k]*8u;
      host_offset_move=3;
      host_pins&=~0x800u;app_tick();
      CHECK(!host_offset_move && host_active && host_starts==1 && host_led==2);
      app_tick();
      host_offset=initial*ONE_HALF_FRAMES*8u+512u*8u;app_tick();
      CHECK(host_active && host_starts==2 && host_led==2);
      CHECK(host_audio[1024]==sample_word(1,(initial+1u)*ONE_HALF_FRAMES-1u-512u));
      click_play();CHECK(!host_active);
    }
  }
  /* Cross into the faded half between its ring poll and silence hand-off. */
  feature_mount();click_play();host_offset=123u*8u;pulse();
  host_offset=(ONE_HALF_FRAMES-1u)*8u;
  host_offset_target=ONE_HALF_FRAMES*8u+64u*8u;host_offset_move=2;
  app_tick();CHECK(!host_offset_move && host_active && host_starts==1);
  app_tick();
  host_offset=32u*8u;app_tick();
  CHECK(host_active && host_starts==2 && host_led==2);
  CHECK(host_audio[1024]==sample_word(1,(123u+5000u-512u)%5000u));
  click_play();CHECK(!host_active);
  /* Enter Flip from Trigger using the existing held-PLAY/FILE menu. */
  feature_disk("# test\nTrigger\nQuantize\nShort",0,5000,16);feature_mount();
  host_pins&=~0x200u;
  for(uint32_t i=0;i<3;++i) {++app_card.time;app_tick();}
  app_card.time+=400;app_tick();
  const uint32_t backwards[]={1,0,2,3};
  for(uint32_t i=0;i<4u*(PLAY_COUNT-PLAY_FLIP);++i) {
    host_pins=(host_pins&~0xa0u)|((backwards[i%4]&1u)<<5)|((backwards[i%4]&2u)<<6);encoder_settle();app_tick();
  }
  encoder_settle();app_tick();
  host_pins|=0x200u;
  for(uint32_t i=0;i<3;++i) {++app_card.time;app_tick();}
  click_play();CHECK(host_active);host_offset=100u*8u;pulse();flip_finish_transition(1);
  CHECK(host_audio[1024]==sample_word(1,(100u+5000u-512u)%5000u));
  click_play();CHECK(!host_active);
}
static void cv_hold_calibrate(uint32_t adc) {
  host_adc=adc;host_pins&=~0x200u;
  for(uint32_t i=0;i<3;++i) {++app_card.time;app_tick();}
  app_card.time+=401;app_tick();
  host_pins|=0x200u;
  for(uint32_t i=0;i<3;++i) {++app_card.time;app_tick();}
  CHECK(!host_active);
}
static void cv_value(uint32_t adc) {
  host_adc=adc;++app_card.time;app_tick();
}
static void test_cv_flip_transport(void) {
  for(uint32_t bits=16;bits<=24;bits+=8) {
    feature_disk("# test\nCV Flip\nSelect File\nLong",0,5000,bits);
    feature_mount();
    cv_hold_calibrate(400);
    click_play();CHECK(host_active && host_mode==CV_SELECT); /* native speed */
    CHECK(host_audio[1024]==sample_word(1,512)); /* CV Select never browses */
    uint32_t starts=host_starts;
    for(uint32_t adc=396;adc<=404;++adc) cv_value(adc);
    CHECK(host_starts==starts && host_active);
    pulse();app_tick();CHECK(host_starts==starts); /* GATE ignored */
    host_offset=123u*8u;cv_value(405);flip_finish_transition(starts);
    CHECK(host_audio[1024]==sample_word(1,(123u+5000u-512u)%5000u));
    starts=host_starts;
    for(uint32_t adc=404;adc>=396;--adc) cv_value(adc);
    CHECK(host_starts==starts);
    host_offset=100u*8u;cv_value(395);flip_finish_transition(starts);
    starts=host_starts;
    cv_value(400);advance_half();CHECK(host_starts==starts); /* zero holds forward */
    click_play();CHECK(!host_active);
    cv_value(450);click_play();CHECK(host_active);
    CHECK(host_audio[1024]==sample_word(1,4999u-512u)); /* negative starts from end */
    click_play();CHECK(!host_active);
    cv_hold_calibrate(600);click_play();CHECK(host_active);
    CHECK(host_audio[1024]==sample_word(1,512)); /* new zero resets forward */
    starts=host_starts;cv_value(450);advance_half();CHECK(host_starts==starts);
    click_play();CHECK(!host_active);
  }
}

static void test_tape_transport(void) {
  for(uint32_t bits=16;bits<=24;bits+=8) {
    feature_disk("# test\nTape\nQuantize\nShort",0,5000,bits);feature_mount();
    cv_hold_calibrate(400);click_play();CHECK(!host_active && host_led==2);
    cv_value(393);CHECK(!host_active); /* eight counts needed to leave rest */
    cv_value(350);CHECK(host_active && host_fixed==4000u);
    CHECK(host_audio[1024]==sample_word(1,512));
    uint32_t starts=host_starts;
    cv_value(300);CHECK(host_fixed==2000u && host_starts==starts);
    cv_value(0);CHECK(host_fixed==1334u && host_starts==starts); /* 1.5x cap */
    host_offset=123u*8u;cv_value(400);
    for(uint32_t i=0;i<12 && host_active;++i) {app_card.n=0;advance_half();app_tick();}
    CHECK(!host_active && host_led==2 && host_starts==starts);
    cv_value(405);CHECK(!host_active);
    cv_value(407);CHECK(!host_active);
    cv_value(450);CHECK(host_active && host_fixed==4000u);
    CHECK(host_audio[1024]==sample_word(1,(123u+5000u-512u)%5000u));
    starts=host_starts;host_offset=100u*8u;
    cv_value(350);flip_finish_transition(starts);CHECK(host_fixed==4000u);
    click_play();CHECK(!host_active);
    /* Negative from stopped starts at the end, and PLAY also stops a zero hold. */
    cv_value(450);click_play();CHECK(host_active);
    CHECK(host_audio[1024]==sample_word(1,4999u-512u));
    click_play();cv_value(400);click_play();CHECK(!host_active && host_led==2);
    click_play();CHECK(!host_active && host_led==0);
    cv_value(350);CHECK(!host_active);
  }
}

static void test_chop_transport(void) {
  const uint32_t lengths[]={0,1,37,5000,0x7fffffffu};
  for(uint32_t k=0;k<5;++k) for(uint32_t adc=0;adc<=1023;++adc) {
    uint32_t frames=lengths[k];
    uint32_t expected=frames ? (uint32_t)((uint64_t)(frames-1u)*(1023u-adc)/1023u) : 0;
    CHECK(cv_frame_offset(adc,frames)==expected);
  }
  CHECK(cv_frame_offset(9999,5000)==0);
  for(uint32_t bits=16;bits<=24;bits+=8) {
    feature_disk("# test\nChop\nSelect File\nLong",0,5000,bits);feature_mount();
    host_adc=1023;pulse();CHECK(host_active && host_mode==CV_SELECT);
    CHECK(host_audio[1024]==sample_word(1,512)); /* gate starts from beginning */
    host_pins|=0x800u;app_tick();
    uint32_t starts=host_starts;
    cv_value(0);CHECK(host_starts==starts && host_active); /* CV alone never seeks */
    pulse();host_adc=1023; /* target must remain the gate's sample, not current CV */
    flip_finish_transition(starts);
    CHECK(host_audio[1024]==sample_word(1,(4999u+512u)%5000u));
    starts=host_starts;
    host_adc=512;pulse(); /* another target during an existing transition: latest wins */
    host_adc=0;pulse();host_adc=1023;pulse();
    flip_finish_transition(starts);
    CHECK(host_audio[1024]==sample_word(1,512));
    /* The same target on consecutive gates lands on the same place each time,
     * also after playback has run on between them. */
    for(uint32_t repeat=0;repeat<4;++repeat) {
      uint32_t again=(uint32_t)((uint64_t)4999u*(1023u-300u)/1023u);
      for(uint32_t i=0;i<repeat*3u;++i) {app_card.n=0;advance_half();app_tick();app_tick();}
      starts=host_starts;host_adc=300;pulse();flip_finish_transition(starts);
      CHECK(host_audio[1024]==sample_word(1,(again+512u)%5000u));
    }
    click_play();CHECK(!host_active);
    host_adc=512;click_play();CHECK(host_active);
    uint32_t target=(uint32_t)((uint64_t)4999u*511u/1023u);
    CHECK(host_audio[1024]==sample_word(1,(target+512u)%5000u));
    click_play();CHECK(!host_active);
    /* FILE stops, rather than carrying a pending jump into the new sample. */
    host_adc=0;pulse();CHECK(host_active);browse_next();CHECK(!host_active);
  }
}

#endif

#ifdef ONE_COLOUR
static void colour_reference(uint32_t *out, uint32_t first, uint32_t frames,
                             uint32_t drive, uint32_t crush) {
  Colour state = {0, 0};
  for (uint32_t i = 0; i < frames; i++)
    out[2 * i] = out[2 * i + 1] = sample_word(1, first + i);
  colour_apply(&state, out, frames, drive, crush);
}
static void test_colour(void) {
  static uint32_t data[2 * 4096], expected[2 * 4096];
  Colour state = {0, 0};
  /* Off is exact bypass; silence stays silent at every setting. */
  for (uint32_t i = 0; i < 64; i++) data[2 * i] = data[2 * i + 1] = (i * 0x01234567u) & ~255u;
  memcpy(expected, data, 64 * 8);
  colour_apply(&state, data, 64, 0, 0);
  CHECK(!memcmp(expected, data, 64 * 8) && !state.count);
  for (uint32_t drive = 0; drive <= 300; drive += 25)
    for (uint32_t crush = 0; crush <= 300; crush += 25) {
      memset(data, 0, 64 * 8);
      state.hold = state.count = 0;
      colour_apply(&state, data, 64, drive, crush);
      for (uint32_t i = 0; i < 128; i++) CHECK(!data[i]);
    }
  /* Drive: odd symmetry within one step, monotonic and bounded. */
  for (uint32_t drive = 1; drive <= 255; drive += 127) {
    int32_t previous = -0x800000;
    for (int32_t x = -0x800000; x <= 0x7fffff; x += 0x3fff) {
      uint32_t pair[4] = {(uint32_t)x << 8, 0, (uint32_t)-x << 8, 0};
      state.hold = state.count = 0;
      colour_apply(&state, pair, x == -0x800000 ? 1 : 2, drive, 0);
      int32_t y = (int32_t)pair[0] >> 8, mirrored = (int32_t)pair[2] >> 8;
      CHECK(pair[1] == pair[0] && !(pair[0] & 255u));
      CHECK(y >= previous && y >= -0x800000 && y <= 0x7fffff);
      if (x != -0x800000) CHECK(y + mirrored >= -2 && y + mirrored <= 1);
      /* Clipping and level correction together never cut more than 14 dB. */
      if (x > 256) CHECK(y > 0 && (int64_t)y * 5 >= (int64_t)x - 8);
      previous = y;
    }
  }
  /* Full drive saturates a full-scale input, then applies the level trim;
   * the trim falls monotonically from just under unity to about 0.32. */
  uint32_t last_trim = 256;
  for (uint32_t drive = 1; drive <= 255; drive++) {
    uint32_t trim = 77u + 179u * 256u / (256u + drive * 30u);
    CHECK(trim <= last_trim && trim < 256u && trim >= 82u);
    last_trim = trim;
    data[0] = 0x7fffff00u; state.hold = state.count = 0;
    colour_apply(&state, data, 1, drive, 0);
    if (drive >= 32) CHECK(data[0] == ((0x7fffffu * trim) >> 8) << 8 && data[1] == data[0]);
  }
  CHECK(last_trim == 82u);
  /* Crush: held for the period, quantised to the step, state spans calls. */
  for (uint32_t crush = 1; crush <= 255; crush += 2) {
    uint32_t drop = 8u + (crush * 12u + 127u) / 255u, period = 1u + crush * 31u / 255u;
    for (uint32_t i = 0; i < 200; i++)
      data[2 * i] = data[2 * i + 1] = (uint32_t)((int32_t)(i * 7919u * 257u) - 0x400000) << 8;
    memcpy(expected, data, 200 * 8);
    state.hold = state.count = 0;
    colour_apply(&state, data, 77, 0, crush);
    colour_apply(&state, data + 154, 123, 0, crush);
    for (uint32_t i = 0; i < 200; i++) {
      int32_t in = (int32_t)expected[2 * (i - i % period)] >> 8, out = (int32_t)data[2 * i] >> 8;
      int32_t step = (int32_t)(1u << drop), error = out - in;
      CHECK(data[2 * i + 1] == data[2 * i] && !(out & (step - 1)));
      CHECK(error <= step / 2 && error >= -step / 2);
    }
  }
  CHECK(8u + (255u * 12u + 127u) / 255u == 20u && 1u + 255u * 31u / 255u == 32u);

  /* config.txt amounts and the Colour CV mode. */
  Fat f;
  Settings s;
  feature_disk("# card\nLoop\nColour\nDrive 5\ncrush  9\nignored", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.play == PLAY_LOOP && s.cv == CV_COLOUR &&
        s.drive == 141 && s.crush == 255 && !s.delay_ms);
  feature_disk("# card\nFade\nFree\nLong\nCRUSH 1\nDRIVE 0", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_FREE && s.delay_ms == 10 &&
        s.crush == 28 && !s.drive);
  feature_disk("# card\nFade\nFree\nShort\nDrive 12\nCrush\nDrive5", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && !s.drive && !s.crush);
  feature_disk("# card\nFade\nFree\nDrive 12", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == -2 && !s.drive && !s.crush);

  /* Fixed amounts colour the primed buffer and each prefetched half; the
   * sample-and-hold phase continues across the two calls. */
  feature_disk("# test\nLoop\nQuantize\nShort\nDrive 9\nCrush 4", 0, 5000, 24);
  feature_mount();
  host_adc = 512;
  click_play();
  CHECK(host_active && host_led == 2);
  colour_reference(expected, 0, ONE_CACHE_FRAMES + ONE_HALF_FRAMES, 255, 113);
  CHECK(expected[0] != sample_word(1, 0));
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  CHECK(host_active && !memcmp(host_audio, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES * 8u));

  /* Colour CV: native pitch, never reprogrammed while CV moves; the knob/CV
   * sweeps from clean (high ADC reading) to full drive and crush. */
  feature_disk("# test\nLoop\nColour", 0, 5000, 24);
  feature_mount();
  host_adc = 1023;
  click_play();
  CHECK(host_active && host_mode == CV_SELECT);
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    CHECK(host_audio[2 * i] == sample_word(1, i));
  host_pitch = 0xdeadu;
  host_adc = 0;
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick(); /* publishes the clean half prefetched before CV moved */
  CHECK(host_pitch == 0xdeadu && host_audio[0] == sample_word(1, ONE_CACHE_FRAMES));
  app_tick(); /* prefetches the next half at full amount */
  host_offset = 0;
  app_tick();
  colour_reference(expected, ONE_CACHE_FRAMES + ONE_HALF_FRAMES, ONE_HALF_FRAMES, 255, 255);
  CHECK(host_active && host_pitch == 0xdeadu &&
        !memcmp(host_audio + ONE_HALF_FRAMES * 2u, expected, ONE_HALF_FRAMES * 8u));

  /* Above 96 kHz output the stage is bypassed: 96 kHz file, +7 semitones. */
  feature_disk("# test\nLoop\nQuantize\nShort\nDrive 9\nCrush 9", 0, 5000, 24);
  put32(block(24) + 24, 96000);
  put32(block(24) + 28, 96000 * 3);
  feature_mount();
  host_adc = 0;
  click_play();
  CHECK(host_active && pitch_divider(0, 96000, CV_QUANTIZE) == 667);
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    CHECK(host_audio[2 * i] == sample_word(1, i));
  /* Native 96 kHz is still processed. */
  feature_mount();
  host_adc = 7 * 22;
  click_play();
  CHECK(host_active && pitch_divider(host_adc >> 2, 96000, CV_QUANTIZE) == 1000);
  CHECK(host_audio[0] != sample_word(1, 0));
}
static int32_t filter_peak(uint32_t cutoff, uint32_t resonance, uint32_t period) {
  static uint32_t wave_data[2 * 4096];
  Filter state = {0, 0, filter_coefficient(cutoff) << 8};
  for (uint32_t i = 0; i < 4096; i++)
    wave_data[2 * i] = wave_data[2 * i + 1] =
        (uint32_t)((i % period) * 2u < period ? 0x200000 : -0x200000) << 8;
  filter_apply(&state, wave_data, 4096, filter_coefficient(cutoff), resonance);
  int32_t peak = 0;
  for (uint32_t i = 2048; i < 4096; i++) {
    int32_t v = (int32_t)wave_data[2 * i] >> 8;
    CHECK(wave_data[2 * i + 1] == wave_data[2 * i]);
    if (v < 0) v = -v;
    if (v > peak) peak = v;
  }
  return peak;
}
static void test_filter(void) {
  static uint32_t data[2 * 4096], expected[2 * 4096];
  /* Coefficient map: monotonic, about fs/500 up to the stable limit. */
  uint32_t previous = 0;
  for (uint32_t cutoff = 1; cutoff <= 255; cutoff++) {
    uint32_t f = filter_coefficient(cutoff);
    CHECK(f >= previous && f >= 412u && f <= 32000u);
    previous = f;
  }
  CHECK(filter_coefficient(255) == 32000u && filter_coefficient(999) == 32000u);
  /* Silence in, silence out; state stays at rest. */
  Filter state = {0, 0, 0};
  memset(data, 0, sizeof(data));
  filter_apply(&state, data, 4096, 20000, 255);
  for (uint32_t i = 0; i < 8192; i++) CHECK(!data[i]);
  CHECK(!state.low && !state.band && state.f == 20000u << 8);
  filter_apply(&state, data, 0, 1234, 0);
  CHECK(state.f == 20000u << 8);
  /* Low-pass: unity at DC without resonance, and treble is removed when dark. */
  for (uint32_t cutoff = 1; cutoff <= 255; cutoff += 127) {
    state.low = state.band = 0; state.f = filter_coefficient(cutoff) << 8;
    for (uint32_t pass = 0; pass < 8; pass++) {
      for (uint32_t i = 0; i < 4096; i++) data[2 * i] = data[2 * i + 1] = 0x30000000u;
      filter_apply(&state, data, 4096, filter_coefficient(cutoff), 0);
    }
    int32_t settled = (int32_t)data[2 * 4095] >> 8;
    /* Truncation at the darkest setting leaves a DC error below -90 dBFS. */
    CHECK(settled > 0x300000 - 256 && settled < 0x300000 + 256);
  }
  CHECK(filter_peak(1, 0, 2) < 0x200000 / 1000);
  CHECK(filter_peak(1, 0, 8) < 0x200000 / 100);
  CHECK(filter_peak(255, 0, 64) > 0x200000 * 9 / 10);
  /* Resonance raises the response near cutoff. */
  CHECK(filter_peak(128, 255, 54) > 2 * filter_peak(128, 0, 54));
  /* Full-scale noise at every corner stays bounded with no overflow. */
  uint32_t noise = 12345;
  for (uint32_t cutoff = 1; cutoff <= 255; cutoff += 127)
    for (uint32_t resonance = 0; resonance <= 255; resonance += 255) {
      state.low = state.band = 0; state.f = filter_coefficient(cutoff) << 8;
      for (uint32_t pass = 0; pass < 4; pass++) {
        for (uint32_t i = 0; i < 4096; i++)
          data[2 * i] = data[2 * i + 1] = (random_next(&noise) & 1u) ? 0x7fffff00u : 0x80000000u;
        filter_apply(&state, data, 4096, filter_coefficient(cutoff), resonance);
        CHECK(state.low < 0x20000000 && state.low > -0x20000000);
        CHECK(state.band < 0x20000000 && state.band > -0x20000000);
      }
    }
  /* The coefficient ramps across a call and lands exactly on the target. */
  state.low = state.band = 0; state.f = 1000u << 8;
  for (uint32_t i = 0; i < 64; i++) data[2 * i] = data[2 * i + 1] = 0x10000000u;
  filter_apply(&state, data, 64, 9000, 0);
  CHECK(state.f == 9000u << 8);
  int32_t first = (int32_t)data[2] >> 8;
  state.low = state.band = 0; state.f = 9000u << 8;
  for (uint32_t i = 0; i < 64; i++) data[2 * i] = data[2 * i + 1] = 0x10000000u;
  filter_apply(&state, data, 64, 9000, 0);
  CHECK(first < (int32_t)data[2] >> 8);

  /* config.txt lines and the Filter CV mode. */
  Fat f;
  Settings s;
  feature_disk("# card\nLoop\nFilter\nFilter 4\nresonance 9\nDrive 2\nCrush 3\nShort", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_FILTER && s.filter == 113 &&
        s.resonance == 255 && s.drive == 56 && s.crush == 85);
  feature_disk("# card\nLoop\nQuantize\nResonance 5", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_QUANTIZE && !s.filter && s.resonance == 141);

  /* Fixed filter: primed buffer and prefetched half match one continuous pass. */
  feature_disk("# test\nLoop\nQuantize\nShort\nFilter 5\nResonance 7", 0, 5000, 24);
  feature_mount();
  host_adc = 512;
  click_play();
  CHECK(host_active && host_led == 2);
  Filter reference = {0, 0, filter_coefficient(141) << 8};
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES + ONE_HALF_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  filter_apply(&reference, expected, ONE_CACHE_FRAMES + ONE_HALF_FRAMES,
               filter_coefficient(141), 198);
  CHECK(expected[200] != sample_word(1, 100));
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  CHECK(host_active && !memcmp(host_audio, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES * 8u));

  /* Filter CV: native pitch that is never reprogrammed; the cutoff follows
   * the knob/CV with a ramp across the following block. */
  feature_disk("# test\nLoop\nFilter\nResonance 3", 0, 5000, 24);
  feature_mount();
  host_adc = 0;
  click_play();
  CHECK(host_active && host_mode == CV_SELECT);
  reference.low = reference.band = 0; reference.f = filter_coefficient(255) << 8;
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  filter_apply(&reference, expected, ONE_CACHE_FRAMES, filter_coefficient(255), 85);
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  host_pitch = 0xdeadu;
  host_adc = 1023;
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  app_tick();
  host_offset = 0;
  app_tick();
  CHECK(host_active && host_pitch == 0xdeadu);
  /* The half fetched after the move ends much darker than it would open. */
  for (uint32_t i = 0; i < ONE_HALF_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, ONE_CACHE_FRAMES + ONE_HALF_FRAMES + i);
  CHECK(memcmp(host_audio + ONE_HALF_FRAMES * 2u, expected, ONE_HALF_FRAMES * 8u));

  /* Rate limits: both stages at 48 kHz; only the filter between about 64 and
   * 96 kHz output; neither above 96 kHz. */
  feature_disk("# test\nLoop\nQuantize\nShort\nDrive 9\nFilter 9", 0, 5000, 24);
  put32(block(24) + 24, 96000);
  put32(block(24) + 28, 96000 * 3);
  feature_mount();
  host_adc = 0;
  click_play();
  CHECK(host_active);
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    CHECK(host_audio[2 * i] == sample_word(1, i));
  feature_mount();
  host_adc = 7 * 22;
  click_play();
  CHECK(host_active && pitch_divider(host_adc >> 2, 96000, CV_QUANTIZE) == 1000);
  reference.low = reference.band = 0; reference.f = filter_coefficient(255) << 8;
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  filter_apply(&reference, expected, ONE_CACHE_FRAMES, filter_coefficient(255), 0);
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  feature_mount();
  host_adc = 15 * 22;
  click_play();
  CHECK(host_active && pitch_divider(host_adc >> 2, 96000, CV_QUANTIZE) >= 1500);
  CHECK(memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
}
static int32_t shape_peak(uint32_t cutoff, uint32_t resonance, uint32_t period, uint32_t shape) {
  static uint32_t wave_data[2 * 4096];
  Filter state = {0, 0, filter_coefficient(cutoff) << 8};
  for (uint32_t i = 0; i < 4096; i++)
    wave_data[2 * i] = wave_data[2 * i + 1] =
        (uint32_t)((i % period) * 2u < period ? 0x200000 : -0x200000) << 8;
  filter_shape(&state, wave_data, 4096, filter_coefficient(cutoff), resonance, shape);
  int32_t peak = 0;
  for (uint32_t i = 2048; i < 4096; i++) {
    int32_t v = (int32_t)wave_data[2 * i] >> 8;
    CHECK(wave_data[2 * i + 1] == wave_data[2 * i]);
    if (v < 0) v = -v;
    if (v > peak) peak = v;
  }
  return peak;
}
static void test_filter_shapes(void) {
  static uint32_t data[2 * 4096], expected[2 * 4096];
  Filter state = {0, 0, 0};
  /* The low-pass shape is the original filter, sample for sample. */
  uint32_t noise = 777;
  for (uint32_t i = 0; i < 4096; i++)
    data[2 * i] = data[2 * i + 1] = expected[2 * i] = expected[2 * i + 1] =
        random_next(&noise) & ~255u;
  Filter other = {0, 0, 3000u << 8};
  state.f = 3000u << 8;
  filter_apply(&state, data, 4096, 9000, 200);
  filter_shape(&other, expected, 4096, 9000, 200, FILTER_LOW);
  CHECK(!memcmp(data, expected, sizeof(data)) && state.low == other.low && state.band == other.band);
  /* Silence stays silent; a steady level is removed by high-pass and band-pass. */
  for (uint32_t shape = FILTER_HIGH; shape <= FILTER_BAND; shape++) {
    state.low = state.band = 0; state.f = 0;
    memset(data, 0, sizeof(data));
    filter_shape(&state, data, 4096, 20000, 255, shape);
    for (uint32_t i = 0; i < 8192; i++) CHECK(!data[i]);
    for (uint32_t cutoff = 1; cutoff <= 255; cutoff += 127) {
      state.low = state.band = 0; state.f = filter_coefficient(cutoff) << 8;
      for (uint32_t pass = 0; pass < 8; pass++) {
        for (uint32_t i = 0; i < 4096; i++) data[2 * i] = data[2 * i + 1] = 0x30000000u;
        filter_shape(&state, data, 4096, filter_coefficient(cutoff), 0, shape);
      }
      int32_t settled = (int32_t)data[2 * 4095] >> 8;
      CHECK(settled > -256 && settled < 256);
    }
  }
  /* High-pass keeps treble when set low; the steady-level check above shows
   * that it removes the bottom. */
  CHECK(shape_peak(1, 0, 2, FILTER_HIGH) > 0x200000 * 9 / 10);
  /* Band-pass rejects both sides and passes the region around cutoff. */
  CHECK(shape_peak(1, 0, 2, FILTER_BAND) < 0x200000 / 50);
  CHECK(shape_peak(128, 0, 54, FILTER_BAND) > 0x200000 / 2);
  CHECK(shape_peak(128, 0, 54, FILTER_BAND) > 4 * shape_peak(128, 0, 2, FILTER_BAND));
  CHECK(shape_peak(128, 255, 54, FILTER_BAND) > 2 * shape_peak(128, 0, 54, FILTER_BAND));
  /* Full-scale noise stays bounded in every shape. */
  for (uint32_t shape = FILTER_HIGH; shape <= FILTER_BAND; shape++)
    for (uint32_t cutoff = 1; cutoff <= 255; cutoff += 127)
      for (uint32_t resonance = 0; resonance <= 255; resonance += 255) {
        state.low = state.band = 0; state.f = filter_coefficient(cutoff) << 8;
        for (uint32_t pass = 0; pass < 4; pass++) {
          for (uint32_t i = 0; i < 4096; i++)
            data[2 * i] = data[2 * i + 1] = (random_next(&noise) & 1u) ? 0x7fffff00u : 0x80000000u;
          filter_shape(&state, data, 4096, filter_coefficient(cutoff), resonance, shape);
          CHECK(state.low < 0x20000000 && state.low > -0x20000000);
          CHECK(state.band < 0x20000000 && state.band > -0x20000000);
          for (uint32_t i = 0; i < 4096; i++) CHECK(!(data[2 * i] & 255u));
        }
      }
  /* config.txt: the last cutoff line chooses the output. */
  Fat f;
  Settings s;
  feature_disk("# card\nLoop\nQuantize\nShort\nHighpass 4\nResonance 2", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.filter == 113 && s.shape == FILTER_HIGH && s.resonance == 56);
  feature_disk("# card\nLoop\nFilter\nFilter 9\nbandpass 3", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_FILTER && s.filter == 85 && s.shape == FILTER_BAND);
  feature_disk("# card\nLoop\nQuantize\nHighpass 3\nLowpass 6", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.filter == 170 && s.shape == FILTER_LOW);
  feature_disk("# card\nLoop\nQuantize\nFilter 5", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.filter == 141 && s.shape == FILTER_LOW);
  /* Playback: primed buffer and prefetched half match one continuous pass. */
  for (uint32_t shape = FILTER_HIGH; shape <= FILTER_BAND; shape++) {
    feature_disk(shape == FILTER_HIGH ? "# test\nLoop\nQuantize\nShort\nHighpass 5\nResonance 7"
                                      : "# test\nLoop\nQuantize\nShort\nBandpass 5\nResonance 7",
                 0, 5000, 24);
    feature_mount();
    host_adc = 512;
    click_play();
    CHECK(host_active && host_led == 2);
    Filter reference = {0, 0, filter_coefficient(141) << 8};
    for (uint32_t i = 0; i < ONE_CACHE_FRAMES + ONE_HALF_FRAMES; i++)
      expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
    filter_shape(&reference, expected, ONE_CACHE_FRAMES + ONE_HALF_FRAMES,
                 filter_coefficient(141), 198, shape);
    CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
    host_offset = ONE_HALF_FRAMES * 8u;
    app_tick();
    CHECK(host_active && !memcmp(host_audio, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES * 8u));
  }
  /* Filter CV sweeps whichever shape the config names. */
  feature_disk("# test\nLoop\nFilter\nHighpass 5", 0, 5000, 24);
  feature_mount();
  host_adc = 0;
  click_play();
  CHECK(host_active && host_mode == CV_SELECT);
  Filter swept = {0, 0, filter_coefficient(255) << 8};
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  filter_shape(&swept, expected, ONE_CACHE_FRAMES, filter_coefficient(255), 0, FILTER_HIGH);
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
}
#endif
#ifdef ONE_COLOUR
static void test_mod(void) {
  static uint32_t data[2 * 8192], expected[2 * 8192];
  /* Rate map: monotonic; ends land on the documented frequencies. */
  for (uint32_t ring = 0; ring < 2; ring++) {
    uint32_t previous = 0;
    for (uint32_t rate = 0; rate <= 1023; rate++) {
      uint32_t step = mod_increment(rate, ring, 48000);
      CHECK(step >= previous && (rate % 8u || step > previous));
      previous = step;
    }
    CHECK(mod_increment(9999, ring, 48000) == mod_increment(1023, ring, 48000));
  }
  CHECK(!mod_increment(100, 0, 0));
  /* Phase step for f Hz is f * 2^32 / sample rate, within 0.5%. */
#define MOD_NEAR(step, hz, fs) ((double)(step) > (hz) * 4294967296.0 / (fs) * 0.995 && \
                                (double)(step) < (hz) * 4294967296.0 / (fs) * 1.005)
  CHECK(MOD_NEAR(mod_increment(0, 0, 48000), 1.0, 48000));
  CHECK(MOD_NEAR(mod_increment(1023, 0, 48000), 16.0, 48000));
  CHECK(MOD_NEAR(mod_increment(0, 1, 48000), 16.0, 48000));
  CHECK(MOD_NEAR(mod_increment(1023, 1, 48000), 4096.0, 48000));
  /* The same control gives the same frequency at another sample rate. */
  CHECK(MOD_NEAR(mod_increment(1023, 1, 96000), 4096.0, 96000));
  CHECK(MOD_NEAR(mod_increment(1023, 0, 44100), 16.0, 44100));
  /* Silence stays silent in both modes at full depth. */
  Mod state = {MOD_START_PHASE};
  memset(data, 0, sizeof(data));
  mod_apply(&state, data, 8192, mod_increment(512, 1, 48000), 255, 1);
  mod_apply(&state, data, 8192, mod_increment(512, 0, 48000), 255, 0);
  for (uint32_t i = 0; i < 2 * 8192; i++) CHECK(!data[i]);

  /* Zero depth leaves the 24-bit sample unchanged. */
  for (uint32_t i = 0; i < 4096; i++)
    data[2 * i] = data[2 * i + 1] = (uint32_t)((int32_t)(i * 3001u % 16000000u) - 8000000) << 8;
  memcpy(expected, data, 4096 * 8);
  state.phase = MOD_START_PHASE;
  mod_apply(&state, data, 4096, mod_increment(800, 1, 48000), 0, 1);
  mod_apply(&state, data, 4096, mod_increment(800, 0, 48000), 0, 0);
  CHECK(!memcmp(data, expected, 4096 * 8));

  /* Tremolo on a steady level: starts at full level, never louder than the
   * input, dips to 1 - depth, and repeats at the chosen rate. */
  const int32_t level = 0x400000;
  const uint32_t period = 480; /* 100 Hz at 48 kHz */
  uint32_t step = (uint32_t)(4294967296.0 / period);
  for (uint32_t depth = 85; depth <= 255; depth += 85) {
    for (uint32_t i = 0; i < 8192; i++) data[2 * i] = data[2 * i + 1] = (uint32_t)level << 8;
    state.phase = MOD_START_PHASE;
    mod_apply(&state, data, 8192, step, depth, 0);
    int32_t smallest = level, largest = 0;
    for (uint32_t i = 0; i < 8192; i++) {
      int32_t v = (int32_t)data[2 * i] >> 8;
      CHECK(data[2 * i] == data[2 * i + 1] && v >= 0 && v <= level);
      if (v < smallest) smallest = v;
      if (v > largest) largest = v;
    }
    int32_t floor = level - (int32_t)((int64_t)level * depth / 255);
    CHECK(largest > level - level / 500 && smallest >= floor - level / 500 &&
          smallest <= floor + level / 500);
    CHECK(((int32_t)data[0] >> 8) > level - level / 100);
    for (uint32_t i = 0; i + period < 8192; i += 97) {
      int32_t a = (int32_t)data[2 * i] >> 8, b = (int32_t)data[2 * (i + period)] >> 8;
      CHECK(a - b < level / 200 && b - a < level / 200);
    }
  }

  /* Ring modulation at full depth on a steady level is a sine: symmetric
   * about zero, peaking at the input level, close to an ideal sine. */
  for (uint32_t i = 0; i < 8192; i++) data[2 * i] = data[2 * i + 1] = (uint32_t)level << 8;
  state.phase = MOD_START_PHASE;
  mod_apply(&state, data, 8192, step, 255, 1);
  int64_t sum = 0; int32_t peak = 0, trough = 0;
  for (uint32_t i = 0; i < 16 * period; i++) {
    int32_t v = (int32_t)data[2 * i] >> 8;
    sum += v;
    if (v > peak) peak = v;
    if (v < trough) trough = v;
    double ideal = level * cos(2.0 * 3.14159265358979 * (i + 1) / period);
    CHECK(v - ideal < level / 300.0 && ideal - v < level / 300.0);
  }
  CHECK(sum / (int64_t)(16 * period) < level / 1000 && sum / (int64_t)(16 * period) > -level / 1000);
  CHECK(peak > level - level / 500 && peak <= level && trough < -level + level / 500 && trough >= -level);
  /* Half depth keeps half the dry signal: the swing is 0 .. level. */
  for (uint32_t i = 0; i < 8192; i++) data[2 * i] = data[2 * i + 1] = (uint32_t)level << 8;
  state.phase = MOD_START_PHASE;
  mod_apply(&state, data, 8192, step, 128, 1);
  peak = 0; trough = level;
  for (uint32_t i = 0; i < 8192; i++) {
    int32_t v = (int32_t)data[2 * i] >> 8;
    if (v > peak) peak = v;
    if (v < trough) trough = v;
  }
  CHECK(peak > level - level / 100 && peak <= level && trough > -level / 100 && trough < level / 50);
  /* Chunked calls continue the oscillator exactly. */
  for (uint32_t i = 0; i < 4096; i++)
    data[2 * i] = data[2 * i + 1] = expected[2 * i] = expected[2 * i + 1] =
        (uint32_t)((int32_t)(i * 7919u % 12000000u) - 6000000) << 8;
  state.phase = MOD_START_PHASE;
  mod_apply(&state, expected, 4096, step, 200, 1);
  state.phase = MOD_START_PHASE;
  for (uint32_t done = 0; done < 4096; done += 64) mod_apply(&state, data + done * 2u, 64, step, 200, 1);
  CHECK(!memcmp(data, expected, 4096 * 8));

  /* config.txt lines and the Rate CV mode. */
  Fat f;
  Settings s;
  feature_disk("# card\nLoop\nRate\nRing 9\ntremolo 3\nRate 4\nDrive 2", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_RATE && s.ring == 255 && s.tremolo == 85 &&
        s.rate == 113 && s.drive == 56 && !s.filter);
  feature_disk("# card\nLoop\nQuantize\nTremolo 5", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_QUANTIZE && s.tremolo == 141 && !s.ring && !s.rate);

  /* Fixed tremolo after a fixed filter: the primed buffer and the prefetched
   * half match one continuous pass through both stages. */
  feature_disk("# test\nLoop\nQuantize\nShort\nFilter 5\nResonance 7\nTremolo 9\nRate 9", 0, 5000, 24);
  feature_mount();
  host_adc = 512;
  click_play();
  CHECK(host_active && host_led == 2);
  Filter reference = {0, 0, filter_coefficient(141) << 8};
  Mod wobble = {MOD_START_PHASE};
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES + ONE_HALF_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  filter_apply(&reference, expected, ONE_CACHE_FRAMES + ONE_HALF_FRAMES,
               filter_coefficient(141), 198);
  memcpy(data, expected, (ONE_CACHE_FRAMES + ONE_HALF_FRAMES) * 8u);
  mod_apply(&wobble, expected, ONE_CACHE_FRAMES + ONE_HALF_FRAMES,
            mod_increment(1023, 0, 48000), 255, 0);
  CHECK(memcmp(data, expected, ONE_CACHE_FRAMES * 8u));
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  CHECK(host_active && !memcmp(host_audio, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES * 8u));

  /* Rate CV: native pitch that is never reprogrammed; tremolo at full depth
   * by default, with the rate read from the knob/CV. */
  feature_disk("# test\nLoop\nRate", 0, 5000, 24);
  feature_mount();
  host_adc = 0;
  click_play();
  CHECK(host_active && host_mode == CV_SELECT);
  Mod swept = {MOD_START_PHASE};
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  mod_apply(&swept, expected, ONE_CACHE_FRAMES, mod_increment(1023, 0, 48000), 255, 0);
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  /* Two counts of knob/CV noise leave the rate where it was: the next half
   * continues the same oscillator at the same step. */
  for (uint32_t i = 0; i < ONE_HALF_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, ONE_CACHE_FRAMES + i);
  mod_apply(&swept, expected, ONE_HALF_FRAMES, mod_increment(1023, 0, 48000), 255, 0);
  host_adc = 2;
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  CHECK(host_active && !memcmp(host_audio, expected, ONE_HALF_FRAMES * 8u));
}
/* One effects block as the application runs it: 64-frame chunks with the
 * coefficient stepped towards the block's target. */
static void wah_block(Filter *state, uint32_t *data, uint32_t frames, uint32_t target,
                      uint32_t resonance, uint32_t shape) {
  uint32_t start = state->f >> 8;
  for (uint32_t done = 0; done < frames;) {
    uint32_t count = frames - done < 64u ? frames - done : 64u;
    done += count;
    int32_t span = (int32_t)target - (int32_t)start;
    uint32_t step = (uint32_t)((int32_t)start + span * (int32_t)done / (int32_t)frames);
    filter_shape(state, data + (done - count) * 2u, count, step, resonance, shape);
  }
}
static void test_wah(void) {
  static uint32_t expected[2 * (ONE_CACHE_FRAMES + ONE_HALF_FRAMES)];
  /* Sine: zero and rising at the start phase, peaks a quarter turn either side. */
  CHECK(mod_sine(WAH_START_PHASE) == 0);
  CHECK(mod_sine(MOD_START_PHASE) > 32700 && mod_sine(MOD_START_PHASE) <= 32768);
  CHECK(mod_sine(0x40000000u) < -32700 && mod_sine(0x40000000u) >= -32768);
  CHECK(mod_sine(WAH_START_PHASE + 0x01000000u) > 0);
  for (uint32_t phase = 0; phase < 0xff000000u; phase += 0x00800000u) {
    int32_t a = mod_sine(phase), b = mod_sine(phase + 0x80000000u);
    CHECK(a >= -32768 && a <= 32768 && a + b >= -2 && a + b <= 2);
  }
  /* Rate map: monotonic, 0.125 Hz to 8 Hz within 0.5% at each sample rate. */
  const uint32_t rates[] = {44100, 48000, 96000};
  for (uint32_t k = 0; k < 3; k++) {
    uint32_t previous = 0;
    for (uint32_t rate = 0; rate <= 1023; rate++) {
      uint32_t step = wah_increment(rate, rates[k]);
      CHECK(step >= previous);
      previous = step;
    }
    double low = wah_increment(0, rates[k]) * (double)rates[k] / 4294967296.0;
    double high = wah_increment(1023, rates[k]) * (double)rates[k] / 4294967296.0;
    CHECK(low > 0.125 * 0.995 && low < 0.125 * 1.005);
    CHECK(high > 8.0 * 0.995 && high < 8.0 * 1.005);
    CHECK(wah_increment(5000, rates[k]) == wah_increment(1023, rates[k]));
  }
  CHECK(!wah_increment(500, 0));
  /* Cutoff swing: centred, symmetric, scaled by depth and kept in range. */
  for (uint32_t centre = 1; centre <= 255; centre += 2) {
    CHECK(wah_cutoff(centre, 255, 0) == centre && wah_cutoff(centre, 0, 32767) == centre);
    uint32_t up = wah_cutoff(centre, 255, 32767), down = wah_cutoff(centre, 255, -32768);
    CHECK(up >= centre && up <= 255 && down <= centre && down >= 1);
    if (centre > 101 && centre < 155) CHECK(up - centre >= 99 && centre - down == 100);
    CHECK(wah_cutoff(centre, 128, 32767) <= up);
  }
  CHECK(wah_cutoff(128, 999, 32767) == wah_cutoff(128, 255, 32767));
  CHECK(wah_cutoff(999, 0, 0) == 255);
  /* config.txt. */
  Fat f;
  Settings s;
  feature_disk("# card\nLoop\nRate\nBandpass 5\nwah 9\nRate 3", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_RATE && s.wah == 255 && s.rate == 85 &&
        s.shape == FILTER_BAND && s.filter == 141);
  feature_disk("# card\nLoop\nQuantize", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && !s.wah);
  /* Playback: the cutoff starts at its centre and each block ramps to the
   * sweep's position at that block's end. */
  for (uint32_t pass = 0; pass < 3; pass++) {
    const char *config = pass == 0 ? "# test\nLoop\nQuantize\nShort\nFilter 5\nResonance 6\nWah 9\nRate 9"
                       : pass == 1 ? "# test\nLoop\nQuantize\nShort\nBandpass 5\nResonance 6\nWah 4\nRate 9"
                                   : "# test\nLoop\nQuantize\nShort\nWah 9\nRate 9";
    uint32_t centre = pass == 2 ? 128u : 141u, depth = pass == 1 ? 113u : 255u;
    uint32_t resonance = pass == 2 ? 0u : 170u, shape = pass == 1 ? FILTER_BAND : FILTER_LOW;
    feature_disk(config, 0, 5000, 24);
    feature_mount();
    host_adc = 512;
    click_play();
    CHECK(host_active && host_led == 2);
    Filter reference = {0, 0, filter_coefficient(centre) << 8};
    uint32_t phase = WAH_START_PHASE, step = wah_increment(1023, 48000);
    for (uint32_t i = 0; i < ONE_CACHE_FRAMES + ONE_HALF_FRAMES; i++)
      expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
    phase += step * ONE_CACHE_FRAMES;
    uint32_t first = filter_coefficient(wah_cutoff(centre, depth, mod_sine(phase)));
    CHECK(first != filter_coefficient(centre));
    wah_block(&reference, expected, ONE_CACHE_FRAMES, first, resonance, shape);
    phase += step * ONE_HALF_FRAMES;
    wah_block(&reference, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES,
              filter_coefficient(wah_cutoff(centre, depth, mod_sine(phase))), resonance, shape);
    CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
    host_offset = ONE_HALF_FRAMES * 8u;
    app_tick();
    CHECK(host_active && !memcmp(host_audio, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES * 8u));
  }
  /* Rate CV with a wah line sweeps the wah, and adds no tremolo by default. */
  feature_disk("# test\nLoop\nRate\nFilter 5\nWah 9", 0, 5000, 24);
  feature_mount();
  host_adc = 0;
  click_play();
  CHECK(host_active && host_mode == CV_SELECT);
  Filter swept = {0, 0, filter_coefficient(141) << 8};
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  wah_block(&swept, expected, ONE_CACHE_FRAMES,
            filter_coefficient(wah_cutoff(141, 255,
                mod_sine(WAH_START_PHASE + wah_increment(1023, 48000) * ONE_CACHE_FRAMES))), 0, FILTER_LOW);
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
}
static void phaser_block(Phaser *state, uint32_t *data, uint32_t frames, uint32_t target,
                         uint32_t intensity) {
  uint32_t start = state->a >> 8;
  for (uint32_t done = 0; done < frames;) {
    uint32_t count = frames - done < 64u ? frames - done : 64u;
    done += count;
    int32_t span = (int32_t)target - (int32_t)start;
    phaser_apply(state, data + (done - count) * 2u, count,
                 (uint32_t)((int32_t)start + span * (int32_t)done / (int32_t)frames), intensity);
  }
}
/* Peak of a steady sine through the phaser, as a fraction of its input peak. */
static double phaser_gain(uint32_t cutoff, uint32_t intensity, double turns_per_frame) {
  static uint32_t tone[2 * 8192];
  Phaser state = {{0, 0, 0, 0, 0}, phaser_coefficient(cutoff) << 8};
  for (uint32_t i = 0; i < 8192; i++)
    tone[2 * i] = tone[2 * i + 1] =
        (uint32_t)(int32_t)(2097152.0 * sin(6.283185307179586 * turns_per_frame * i)) << 8;
  phaser_apply(&state, tone, 8192, phaser_coefficient(cutoff), intensity);
  int32_t peak = 0;
  for (uint32_t i = 4096; i < 8192; i++) {
    int32_t v = (int32_t)tone[2 * i] >> 8;
    CHECK(tone[2 * i + 1] == tone[2 * i]);
    if (v < 0) v = -v;
    if (v > peak) peak = v;
  }
  return peak / 2097152.0;
}
static void phaser_capture(const char *config, uint32_t rate, uint32_t adc, uint32_t *out,
                           uint32_t least, uint32_t below) {
  feature_disk(config, 0, 5000, 24);
  if (rate != 48000) {
    put32(block(24) + 24, rate);
    put32(block(24) + 28, rate * 3);
  }
  feature_mount();
  host_adc = adc;
  click_play();
  uint32_t divider = pitch_divider(adc >> 2, rate, CV_QUANTIZE);
  CHECK(host_active && divider >= least && divider < below);
  memcpy(out, host_audio, ONE_CACHE_FRAMES * 8u);
}
static void test_phaser(void) {
  static uint32_t data[2 * 4096], expected[2 * (ONE_CACHE_FRAMES + ONE_HALF_FRAMES)];
  static uint32_t first[2 * ONE_CACHE_FRAMES], second[2 * ONE_CACHE_FRAMES];
  /* Coefficient: falls as the cutoff rises, always a stable fraction. */
  uint32_t previous = 32768;
  for (uint32_t cutoff = 1; cutoff <= 255; cutoff++) {
    uint32_t a = phaser_coefficient(cutoff);
    CHECK(a <= previous && a > 0 && a < 32768u);
    previous = a;
  }
  CHECK(phaser_coefficient(0) == phaser_coefficient(1));
  /* Silence stays silent at every setting. */
  Phaser state = {{0, 0, 0, 0, 0}, 0};
  for (uint32_t intensity = 0; intensity <= 300; intensity += 60) {
    memset(data, 0, sizeof(data));
    phaser_apply(&state, data, 4096, 20000, intensity);
    for (uint32_t i = 0; i < 8192; i++) CHECK(!data[i]);
    for (uint32_t i = 0; i < 5; i++) CHECK(!state.z[i]);
  }
  CHECK(state.a == 20000u << 8);
  phaser_apply(&state, data, 0, 1234, 255);
  CHECK(state.a == 20000u << 8);
  /* Zero intensity leaves the samples exactly as they were. */
  uint32_t noise = 4242;
  for (uint32_t i = 0; i < 2048; i++)
    data[2 * i] = data[2 * i + 1] = expected[2 * i] = expected[2 * i + 1] = random_next(&noise) & ~255u;
  phaser_apply(&state, data, 2048, 9000, 0);
  CHECK(!memcmp(data, expected, 2048 * 8u));
  /* Notches where each stage turns the phase 45 and 135 degrees; a peak
   * between them where the stages add up to a full turn. Depth follows
   * intensity. */
  for (uint32_t cutoff = 60; cutoff <= 180; cutoff += 60) {
    double a = phaser_coefficient(cutoff) / 32768.0, t = (1.0 - a) / (1.0 + a);
    double low = atan(t * 0.41421356) / 3.141592653589793;
    double high = atan(t * 2.41421356) / 3.141592653589793;
    double middle = atan(t) / 3.141592653589793;
    /* The one-sample delay in the feedback path moves the upper notch a
     * little at high settings, so it is checked more loosely. */
    CHECK(phaser_gain(cutoff, 255, low) < 0.05 && phaser_gain(cutoff, 255, high) < 0.1);
    double peak = phaser_gain(cutoff, 255, middle);
    CHECK(peak > 1.1 && peak < 1.25);
    double half = phaser_gain(cutoff, 128, low);
    CHECK(half > 0.3 && half < 0.8);
  }
  /* Full-scale noise at every corner stays bounded with no overflow. */
  for (uint32_t cutoff = 1; cutoff <= 255; cutoff += 127) {
    for (uint32_t i = 0; i < 5; i++) state.z[i] = 0;
    state.a = phaser_coefficient(cutoff) << 8;
    for (uint32_t pass = 0; pass < 4; pass++) {
      for (uint32_t i = 0; i < 4096; i++)
        data[2 * i] = data[2 * i + 1] = (random_next(&noise) & 1u) ? 0x7fffff00u : 0x80000000u;
      phaser_apply(&state, data, 4096, phaser_coefficient(cutoff), 255);
      for (uint32_t i = 0; i < 5; i++)
        CHECK(state.z[i] < 0x04000000 && state.z[i] > -0x04000000);
      for (uint32_t i = 0; i < 4096; i++) CHECK(!(data[2 * i] & 255u));
    }
  }
  /* Worst case for the feedback path: a full-scale square at the peak. */
  for (uint32_t i = 0; i < 5; i++) state.z[i] = 0;
  state.a = phaser_coefficient(120) << 8;
  for (uint32_t pass = 0; pass < 4; pass++) {
    for (uint32_t i = 0; i < 4096; i++)
      data[2 * i] = data[2 * i + 1] = (i / 20u) & 1u ? 0x7fffff00u : 0x80000000u;
    phaser_apply(&state, data, 4096, phaser_coefficient(120), 255);
    for (uint32_t i = 0; i < 5; i++)
      CHECK(state.z[i] < 0x04000000 && state.z[i] > -0x04000000);
  }
  /* config.txt. */
  Fat f;
  Settings s;
  feature_disk("# card\nLoop\nRate\nphaser 6\nRate 2", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_RATE && s.phaser == 170 && s.rate == 56);
  feature_disk("# card\nLoop\nQuantize", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && !s.phaser);
  /* Playback: the sweep starts at its centre and each block ramps to the
   * sweep's position at that block's end. */
  feature_disk("# test\nLoop\nQuantize\nShort\nPhaser 9\nRate 9", 0, 5000, 24);
  feature_mount();
  host_adc = 512;
  click_play();
  CHECK(host_active && host_led == 2);
  Phaser reference = {{0, 0, 0, 0, 0}, phaser_coefficient(PHASER_CENTRE) << 8};
  uint32_t phase = WAH_START_PHASE, step = wah_increment(1023, 48000);
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES + ONE_HALF_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  phase += step * ONE_CACHE_FRAMES;
  uint32_t target = phaser_coefficient(wah_cutoff(PHASER_CENTRE, PHASER_SWING, mod_sine(phase)));
  CHECK(target != phaser_coefficient(PHASER_CENTRE));
  phaser_block(&reference, expected, ONE_CACHE_FRAMES, target, 255);
  phase += step * ONE_HALF_FRAMES;
  phaser_block(&reference, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES,
               phaser_coefficient(wah_cutoff(PHASER_CENTRE, PHASER_SWING, mod_sine(phase))), 255);
  CHECK(expected[200] != sample_word(1, 100));
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  host_offset = ONE_HALF_FRAMES * 8u;
  app_tick();
  CHECK(host_active && !memcmp(host_audio, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES * 8u));
  /* Rate CV with a phaser line sweeps the phaser and adds no tremolo. */
  feature_disk("# test\nLoop\nRate\nPhaser 9", 0, 5000, 24);
  feature_mount();
  host_adc = 0;
  click_play();
  CHECK(host_active && host_mode == CV_SELECT);
  CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
  /* Stage limits. Up to 48 kHz output all four stages run. */
  const char *four = "# test\nLoop\nQuantize\nShort\nFilter 5\nPhaser 9\nTremolo 5\nDrive 9\nRate 9";
  const char *three = "# test\nLoop\nQuantize\nShort\nFilter 5\nPhaser 9\nTremolo 5\nRate 9";
  const char *two = "# test\nLoop\nQuantize\nShort\nFilter 5\nPhaser 9\nRate 9";
  phaser_capture(four, 48000, 512, first, 2000, 100000);
  phaser_capture(three, 48000, 512, second, 2000, 100000);
  CHECK(memcmp(first, second, sizeof(first)));
  /* Between 48 and about 64 kHz three run: colour gives way, tremolo stays. */
  phaser_capture(four, 96000, 15 * 22, first, 1500, 2000);
  phaser_capture(three, 96000, 15 * 22, second, 1500, 2000);
  CHECK(!memcmp(first, second, sizeof(first)));
  phaser_capture(two, 96000, 15 * 22, second, 1500, 2000);
  CHECK(memcmp(first, second, sizeof(first)));
  /* Up to 96 kHz one runs: the filter before the phaser, the phaser before
   * tremolo and colour. */
  phaser_capture(four, 96000, 7 * 22, first, 1000, 1500);
  phaser_capture("# test\nLoop\nQuantize\nShort\nFilter 5", 96000, 7 * 22, second, 1000, 1500);
  CHECK(!memcmp(first, second, sizeof(first)));
  phaser_capture("# test\nLoop\nQuantize\nShort\nPhaser 9\nTremolo 5\nDrive 9\nRate 9", 96000, 7 * 22,
                 first, 1000, 1500);
  phaser_capture("# test\nLoop\nQuantize\nShort\nPhaser 9\nRate 9", 96000, 7 * 22, second, 1000, 1500);
  CHECK(!memcmp(first, second, sizeof(first)));
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  CHECK(memcmp(first, expected, sizeof(first)));
  /* Above 96 kHz nothing runs. */
  phaser_capture(four, 96000, 0, first, 0, 1000);
  CHECK(!memcmp(first, expected, sizeof(first)));
}
static void flanger_block(Flanger *state, uint32_t *data, uint32_t frames, uint32_t target,
                          uint32_t intensity) {
  uint32_t start = state->delay;
  for (uint32_t done = 0; done < frames;) {
    uint32_t count = frames - done < 64u ? frames - done : 64u;
    done += count;
    int32_t span = (int32_t)target - (int32_t)start;
    flanger_apply(state, data + (done - count) * 2u, count,
                  (uint32_t)((int32_t)start + (span / 256) * (int32_t)done / (int32_t)frames * 256),
                  intensity);
  }
}
static Flanger flanger_state;
/* Peak of a steady sine through the flanger at a fixed delay. */
static double flanger_gain(uint32_t delay, uint32_t intensity, double turns_per_frame) {
  static uint32_t tone[2 * 8192];
  memset(&flanger_state, 0, sizeof(flanger_state));
  flanger_state.delay = delay << 16;
  for (uint32_t i = 0; i < 8192; i++)
    tone[2 * i] = tone[2 * i + 1] =
        (uint32_t)(int32_t)(2097152.0 * sin(6.283185307179586 * turns_per_frame * i)) << 8;
  flanger_apply(&flanger_state, tone, 8192, delay << 16, intensity);
  int32_t peak = 0;
  for (uint32_t i = 4096; i < 8192; i++) {
    int32_t v = (int32_t)tone[2 * i] >> 8;
    CHECK(tone[2 * i + 1] == tone[2 * i]);
    if (v < 0) v = -v;
    if (v > peak) peak = v;
  }
  return peak / 2097152.0;
}
static void test_flanger(void) {
  static uint32_t data[2 * 4096], expected[2 * (ONE_CACHE_FRAMES + ONE_HALF_FRAMES)];
  static uint32_t first[2 * ONE_CACHE_FRAMES], second[2 * ONE_CACHE_FRAMES];
  Flanger *state = &flanger_state;
  /* Delay map: 32 to 384 frames, rising with the sine, centre in between. */
  CHECK(flanger_delay(-32768) == 32u << 16 && flanger_delay(32767) >= 383u << 16 &&
        flanger_delay(32767) <= 384u << 16);
  CHECK(flanger_delay(0) == 112u << 16);
  uint32_t previous = 0;
  for (int32_t sine = -32768; sine <= 32767; sine += 257) {
    uint32_t delay = flanger_delay(sine);
    CHECK(delay >= previous && delay < (FLANGER_FRAMES - 2u) << 16);
    previous = delay;
  }
  /* Silence stays silent at every setting. */
  for (uint32_t intensity = 0; intensity <= 300; intensity += 60) {
    memset(state, 0, sizeof(*state));
    memset(data, 0, sizeof(data));
    flanger_apply(state, data, 4096, 200u << 16, intensity);
    for (uint32_t i = 0; i < 8192; i++) CHECK(!data[i]);
    for (uint32_t i = 0; i < FLANGER_FRAMES; i++) CHECK(!state->line[i]);
    CHECK(state->delay == 200u << 16);
  }
  flanger_apply(state, data, 0, 300u << 16, 255);
  CHECK(state->delay == 200u << 16);
  /* Out-of-range targets are kept inside the line. */
  flanger_apply(state, data, 64, 0xffffffffu, 255);
  CHECK(state->delay == (FLANGER_FRAMES - 2u) << 16);
  flanger_apply(state, data, 64, 0, 255);
  CHECK(state->delay == 1u << 16);
  /* Zero intensity leaves the samples exactly as they were. */
  uint32_t noise = 9191;
  memset(state, 0, sizeof(*state));
  for (uint32_t i = 0; i < 2048; i++)
    data[2 * i] = data[2 * i + 1] = expected[2 * i] = expected[2 * i + 1] = random_next(&noise) & ~255u;
  flanger_apply(state, data, 2048, 100u << 16, 0);
  CHECK(!memcmp(data, expected, 2048 * 8u));
  /* Comb response at a fixed delay: notches at odd half-cycles of the delay,
   * peaks at whole cycles; depth follows intensity. */
  for (uint32_t delay = 40; delay <= 360; delay += 160) {
    CHECK(flanger_gain(delay, 255, 0.5 / delay) < 0.05);
    CHECK(flanger_gain(delay, 255, 1.5 / delay) < 0.05);
    double peak = flanger_gain(delay, 255, 1.0 / delay);
    CHECK(peak > 1.1 && peak < 1.25);
    double half = flanger_gain(delay, 128, 0.5 / delay);
    CHECK(half > 0.3 && half < 0.8);
  }
  /* Full-scale noise and a full-scale square at the comb's peak stay bounded,
   * and the line then decays to exact silence. */
  memset(state, 0, sizeof(*state));
  for (uint32_t pass = 0; pass < 6; pass++) {
    for (uint32_t i = 0; i < 4096; i++)
      data[2 * i] = data[2 * i + 1] =
          pass < 3 ? ((random_next(&noise) & 1u) ? 0x7fffff00u : 0x80000000u)
                   : ((i / 50u) & 1u ? 0x7fffff00u : 0x80000000u);
    flanger_apply(state, data, 4096, 100u << 16, 255);
    for (uint32_t i = 0; i < 4096; i++) CHECK(!(data[2 * i] & 255u) && data[2 * i + 1] == data[2 * i]);
  }
  for (uint32_t pass = 0; pass < 8; pass++) {
    memset(data, 0, sizeof(data));
    flanger_apply(state, data, 4096, 100u << 16, 255);
  }
  for (uint32_t i = 0; i < 8192; i++) CHECK(!data[i]);
  for (uint32_t i = 0; i < FLANGER_FRAMES; i++) CHECK(!state->line[i]);
  /* A moving delay reads between stored samples without leaving the line. */
  memset(state, 0, sizeof(*state));
  state->delay = 32u << 16;
  for (uint32_t i = 0; i < 4096; i++)
    data[2 * i] = data[2 * i + 1] = (uint32_t)((int32_t)(i % 97u) * 40000 - 1900000) << 8;
  flanger_apply(state, data, 4096, 384u << 16, 255);
  CHECK(state->delay == 384u << 16 && state->at == (4096u & (FLANGER_FRAMES - 1u)));
  /* config.txt. */
  Fat f;
  Settings s;
  feature_disk("# card\nLoop\nRate\nflanger 6\nRate 2", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && s.cv == CV_RATE && s.flanger == 170 && s.rate == 56 && !s.phaser);
  feature_disk("# card\nLoop\nQuantize", 0, 37, 16);
  CHECK(!fat_mount(&f, disk, 0));
  CHECK(settings_load(&f, &s) == 1 && !s.flanger);
  /* Playback: the sweep starts at its centre and each block ramps to the
   * sweep's position at that block's end. Twice, so a restart is shown to
   * begin from a cleared line. */
  for (uint32_t pass = 0; pass < 2; pass++) {
    feature_disk("# test\nLoop\nQuantize\nShort\nFlanger 9\nRate 9", 0, 5000, 24);
    feature_mount();
    host_adc = 512;
    click_play();
    CHECK(host_active && host_led == 2);
    memset(state, 0, sizeof(*state));
    state->delay = flanger_delay(0);
    uint32_t phase = WAH_START_PHASE, step = wah_increment(1023, 48000);
    for (uint32_t i = 0; i < ONE_CACHE_FRAMES + ONE_HALF_FRAMES; i++)
      expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
    phase += step * ONE_CACHE_FRAMES;
    CHECK(flanger_delay(mod_sine(phase)) != flanger_delay(0));
    flanger_block(state, expected, ONE_CACHE_FRAMES, flanger_delay(mod_sine(phase)), 255);
    phase += step * ONE_HALF_FRAMES;
    flanger_block(state, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES,
                  flanger_delay(mod_sine(phase)), 255);
    CHECK(expected[400] != sample_word(1, 200));
    CHECK(!memcmp(host_audio, expected, ONE_CACHE_FRAMES * 8u));
    host_offset = ONE_HALF_FRAMES * 8u;
    app_tick();
    CHECK(host_active && !memcmp(host_audio, expected + ONE_CACHE_FRAMES * 2u, ONE_HALF_FRAMES * 8u));
  }
  /* The flanger takes the phaser's place when both lines are present. */
  phaser_capture("# test\nLoop\nQuantize\nShort\nPhaser 9\nFlanger 9\nRate 9", 48000, 512, first, 2000, 100000);
  phaser_capture("# test\nLoop\nQuantize\nShort\nFlanger 9\nRate 9", 48000, 512, second, 2000, 100000);
  CHECK(!memcmp(first, second, sizeof(first)));
  phaser_capture("# test\nLoop\nQuantize\nShort\nPhaser 9\nRate 9", 48000, 512, second, 2000, 100000);
  CHECK(memcmp(first, second, sizeof(first)));
  /* Rate CV with a flanger line adds no tremolo. */
  phaser_capture("# test\nLoop\nRate\nFlanger 9", 48000, 0, second, 0, 100000);
  phaser_capture("# test\nLoop\nQuantize\nShort\nFlanger 9\nRate 9", 48000, 512, first, 2000, 100000);
  CHECK(!memcmp(first, second, sizeof(first)));
  /* It shares the phaser's place in the stage limits: behind the filter in
   * the one-stage band, ahead of tremolo and colour. */
  phaser_capture("# test\nLoop\nQuantize\nShort\nFilter 5\nFlanger 9\nTremolo 5\nDrive 9\nRate 9", 96000, 7 * 22,
                 first, 1000, 1500);
  phaser_capture("# test\nLoop\nQuantize\nShort\nFilter 5", 96000, 7 * 22, second, 1000, 1500);
  CHECK(!memcmp(first, second, sizeof(first)));
  phaser_capture("# test\nLoop\nQuantize\nShort\nFlanger 9\nTremolo 5\nDrive 9\nRate 9", 96000, 7 * 22,
                 first, 1000, 1500);
  phaser_capture("# test\nLoop\nQuantize\nShort\nFlanger 9\nRate 9", 96000, 7 * 22, second, 1000, 1500);
  CHECK(!memcmp(first, second, sizeof(first)));
  for (uint32_t i = 0; i < ONE_CACHE_FRAMES; i++)
    expected[2 * i] = expected[2 * i + 1] = sample_word(1, i);
  CHECK(memcmp(first, expected, sizeof(first)));
  phaser_capture("# test\nLoop\nQuantize\nShort\nFlanger 9\nRate 9", 96000, 0, first, 0, 1000);
  CHECK(!memcmp(first, expected, sizeof(first)));
}
#endif
#ifdef ONE_TOLERANT_ENCODER
static int32_t encoder_feed(Encoder *e, const uint8_t *states, uint32_t count) {
  int32_t total = 0;
  for (uint32_t i = 0; i < count; ++i) total += encoder_step(e, states[i]);
  return total;
}
static void test_encoder(void) {
  static const uint8_t up[4] = {2, 0, 1, 3}, down[4] = {1, 0, 2, 3};
  Encoder e;
  encoder_init(&e, 3);
  /* Clean clicks, each reported exactly once and only at the detent. */
  for (uint32_t i = 0; i < 3; ++i) CHECK(encoder_step(&e, up[i]) == 0);
  CHECK(encoder_step(&e, 3) == 1);
  for (uint32_t i = 0; i < 3; ++i) CHECK(encoder_step(&e, down[i]) == 0);
  CHECK(encoder_step(&e, 3) == -1);
  CHECK(e.steps == 2 && !e.recovered && !e.guessed);
  /* Any one intermediate state unseen, either direction: still one step. */
  for (uint32_t direction = 0; direction < 2; ++direction)
    for (uint32_t missing = 0; missing < 3; ++missing) {
      const uint8_t *full = direction ? down : up;
      uint8_t seen[3]; uint32_t n = 0;
      for (uint32_t i = 0; i < 4; ++i) if (i != missing) seen[n++] = full[i];
      encoder_init(&e, 3);
      CHECK(encoder_feed(&e, seen, 3) == (direction ? -1 : 1));
      CHECK(e.steps == 1 && e.recovered == 1 && !e.guessed);
      /* The decoder is back at rest: the next clean click counts once. */
      CHECK(encoder_feed(&e, full, 4) == (direction ? -1 : 1));
    }
  /* Bounce off the detent and partial turns that return report nothing. */
  static const uint8_t bounce[] = {2,3,2,3,1,3,1,3}, half[] = {2,0,2,3}, most[] = {2,0,1,0,2,3};
  encoder_init(&e, 3);
  CHECK(encoder_feed(&e, bounce, sizeof(bounce)) == 0);
  CHECK(encoder_feed(&e, half, sizeof(half)) == 0);
  CHECK(encoder_feed(&e, most, sizeof(most)) == 0);
  CHECK(!e.steps && !e.recovered && !e.guessed);
  /* Bounce inside a click does not add or lose steps. */
  static const uint8_t chatter[] = {2,3,2,0,2,0,1,0,1,3,1,3};
  CHECK(encoder_feed(&e, chatter, sizeof(chatter)) == 1);
  /* Only the far state seen: a click in the direction of the previous step. */
  static const uint8_t blink[] = {0, 3};
  CHECK(e.previous == 1 && encoder_feed(&e, blink, 2) == 1 && e.guessed == 1);
  CHECK(encoder_feed(&e, down, 4) == -1 && encoder_feed(&e, blink, 2) == -1);
  CHECK(e.guessed == 2);
  /* A wiggle off the detent whose return went unseen, then a real click the
   * other way: reported in the direction it arrived. */
  static const uint8_t wiggle_down[] = {2, 1, 0, 2, 3}, wiggle_up[] = {1, 2, 0, 1, 3};
  CHECK(encoder_feed(&e, wiggle_up, 5) == 1 && encoder_feed(&e, wiggle_down, 5) == -1);
  CHECK(e.guessed == 4);
  /* One line bouncing alone, however long, still reports nothing. */
  static const uint8_t lost[] = {1, 3, 1, 3, 2, 3};
  CHECK(encoder_feed(&e, lost, sizeof(lost)) == 0 && e.guessed == 4);
  CHECK(encoder_feed(&e, up, 4) == 1 && encoder_feed(&e, down, 4) == -1);
  /* Random walks with bounce: every poll seen, net steps match the detents
   * crossed; with one state hidden per click the count is unchanged. */
  uint32_t seed = 12345;
  for (uint32_t hide = 0; hide < 2; ++hide) {
    encoder_init(&e, 3);
    int32_t expected = 0, got = 0;
    for (uint32_t click = 0; click < 2000; ++click) {
      seed = seed * 1664525u + 1013904223u;
      uint32_t direction = (seed >> 16) & 1u, hidden = hide ? (seed >> 20) % 3u : 9u;
      const uint8_t *full = direction ? down : up;
      for (uint32_t i = 0; i < 4; ++i) {
        if (i == hidden) continue;
        got += encoder_step(&e, full[i]);
        /* Chatter back to the previous visible state, except across a gap. */
        seed = seed * 1664525u + 1013904223u;
        if (!hide && (seed >> 28) < 4u) {
          got += encoder_step(&e, i ? full[i - 1] : 3);
          got += encoder_step(&e, full[i]);
        }
      }
      expected += direction ? -1 : 1;
    }
    CHECK(got == expected);
    CHECK(e.steps == 2000 && e.recovered == (hide ? 2000u : 0u) && !e.guessed);
  }
}
/* Timed raw pin trace in microseconds, polled every 100 us through the
 * contact filter and decoder. Returns net steps; then rests 20 ms. */
typedef struct { uint32_t us; uint8_t ab; } Edge;
static int32_t encoder_trace(Encoder *e, const Edge *edges, uint32_t count, uint32_t *clock) {
  int32_t total = 0; uint32_t ab = e->raw;
  for (uint32_t i = 0; i <= count; ++i) {
    uint32_t wait = i < count ? edges[i].us : 20000u;
    for (uint32_t t = 0; t < wait; t += 100u) {
      total += encoder_step(e, encoder_filter(e, ab, *clock * 24u));
      *clock += 100u;
    }
    if (i < count) ab = edges[i].ab;
  }
  return total;
}
static void test_encoder_filter(void) {
  Encoder e; uint32_t clock = 5000;
  encoder_init(&e, 3);
  /* At rest: low is immediate; high needs the full hold of continuous high. */
  const uint32_t ms = ENCODER_TICKS_PER_MS, hold = ENCODER_HOLD_MAX;
  CHECK(encoder_filter(&e, 2, 10 * ms) == 2 && encoder_filter(&e, 3, 11 * ms) == 2);
  CHECK(encoder_filter(&e, 3, 11 * ms + hold - 1u) == 2 && encoder_filter(&e, 3, 11 * ms + hold) == 3);
  CHECK(encoder_filter(&e, 1, 300 * ms) == 1 && encoder_filter(&e, 3, 301 * ms) == 1);
  CHECK(encoder_filter(&e, 1, 303 * ms) == 1 && e.bridged_max == 2 * ms);
  CHECK(encoder_filter(&e, 3, 310 * ms) == 1 && encoder_filter(&e, 3, 310 * ms + hold - 1u) == 1 &&
        encoder_filter(&e, 3, 310 * ms + hold) == 3 && e.hold_min == hold);
  /* The build-40 hardware miss: A chatters through its low phase and reads
   * high just as B rises, so the raw pins show a false detent. */
  static const Edge miss[] = {{3000,1},{2192,0},{304,1},{2192,0},{64,1},{544,0},{128,1},
                              {288,3},{144,2},{2784,3}};
  encoder_init(&e, 3);
  CHECK(encoder_trace(&e, miss, 10, &clock) == -1 && e.steps == 1);
  /* The following logged click, with heavy chatter on A while closed. */
  static const Edge noisy[] = {{9000,1},{100,3},{100,1},{10960,0},{3648,1},{288,0},{64,1},{144,0},
      {100,1},{100,0},{528,1},{1104,0},{128,1},{544,0},{160,1},{100,0},{64,1},{100,0},
      {100,1},{192,0},{100,1},{100,0},{2528,2},{4736,3}};
  CHECK(encoder_trace(&e, noisy, 24, &clock) == -1 && e.steps == 2);
  CHECK(e.bridged_max >= 50400u && e.bridged_max <= 55200u); /* 2.19 ms open gap */
  /* Clean clicks at 20 ms each, both ways, and a detent wiggle. */
  static const Edge up[] = {{5000,2},{5000,0},{5000,1},{5000,3}}, down[] = {{5000,1},{5000,0},{5000,2},{5000,3}};
  static const Edge wiggle[] = {{5000,2},{300,3},{200,2},{400,3}};
  for (uint32_t i = 0; i < 5; ++i) CHECK(encoder_trace(&e, up, 4, &clock) == 1);
  for (uint32_t i = 0; i < 5; ++i) CHECK(encoder_trace(&e, down, 4, &clock) == -1);
  CHECK(encoder_trace(&e, wiggle, 4, &clock) == 0 && e.steps == 12);
  /* Continuous spins with make-chatter on each closure: the first 40% of
   * every low phase flickers open every 200 us. A spin that eases up from
   * 30 ms per click loses nothing down to 8 ms per click. */
  static const uint32_t speeds[] = {20000, 12000, 8000};
  for (uint32_t direction = 0; direction < 2; ++direction)
    for (uint32_t k = 0; k < 3; ++k) {
      encoder_init(&e, 3);
      int32_t total = 0; uint32_t phase = 0, clicks = 0;
      for (uint32_t click = 0; click < 60; ++click) {
        uint32_t period = click < 3 ? 30000u - click * (30000u - speeds[k]) / 3u : speeds[k];
        uint32_t quarter = period / 4u;
        for (uint32_t t = 0; t < period; t += 100u, clock += 100u, phase += 100u) {
          /* Leading line low for quarters 0-1, trailing line for quarters 1-2. */
          uint32_t lead = t < 2u * quarter, trail = t >= quarter && t < 3u * quarter;
          uint32_t into_lead = t, into_trail = t - quarter;
          if (lead && into_lead < quarter * 4u / 5u && (into_lead / 200u) % 2u) lead = 0;
          if (trail && into_trail < quarter * 4u / 5u && (into_trail / 200u) % 2u) trail = 0;
          uint32_t a = direction ? trail : lead, b = direction ? lead : trail;
          total += encoder_step(&e, encoder_filter(&e, (a ? 0u : 1u) | (b ? 0u : 2u), clock * 24u));
        }
        clicks++;
      }
      for (uint32_t t = 0; t < 20000u; t += 100u, clock += 100u)
        total += encoder_step(&e, encoder_filter(&e, 3, clock * 24u));
      CHECK(total == (direction ? -(int32_t)clicks : (int32_t)clicks));
      CHECK(e.hold_min < ENCODER_HOLD_MAX || speeds[k] >= 18000u);
      (void)phase;
    }
}
#endif
#ifdef ONE_POLL_DECODE
static uint32_t decode_polls;
static void count_decode_poll(void) { decode_polls++; }
static void test_polled_decode(void) {
  static uint32_t plain[ONE_CACHE_FRAMES * 2u + 2u], polled[ONE_CACHE_FRAMES * 2u + 2u];
  static uint8_t scratch[ONE_CACHE_FRAMES * 3u];
  void (*saved)(void) = wave_poll;
  const uint32_t lengths[] = {1, 127, 128, 129, 1025, 5000};
  for (uint32_t bits = 16; bits <= 24; bits += 8)
    for (uint32_t k = 0; k < 6; ++k) {
      uint32_t length = lengths[k];
      feature_disk("# test\nFlip\nQuantize\nShort", 0, length, bits);
      Fat f; File file; Entry e; Wave w; FileIndex index;
      CHECK(!fat_mount(&f, disk, 0));
      CHECK(fat_find(&f, "A.wav", &e) == 1 && !file_open(&file, &f, &e));
      CHECK(!wave_open(&file, &w) && !file_index_build(&file, &index));
      /* Forward one-shot read: same samples, padding and guards with polls. */
      for (uint32_t pass = 0; pass < 2; ++pass) {
        uint32_t *out = pass ? polled : plain, got;
        wave_poll = pass ? count_decode_poll : 0;
        decode_polls = 0;
        out[0] = 0x12345678; out[ONE_CACHE_FRAMES * 2u + 1u] = 0x87654321;
        CHECK(!wave_seek_frame(&file, &w, 0));
        CHECK(!wave_read(&file, &w, out + 1, ONE_CACHE_FRAMES, &got, scratch, sizeof(scratch)));
        uint32_t expected = length < ONE_CACHE_FRAMES ? length : ONE_CACHE_FRAMES;
        CHECK(got == expected);
        if (pass) CHECK(decode_polls == (expected - 1u) / 128u);
      }
      CHECK(!memcmp(plain, polled, sizeof(plain)));
      for (uint32_t i = 0; i < length && i < ONE_CACHE_FRAMES; ++i)
        CHECK(polled[1 + i * 2u] == sample_word(1, i) && polled[2 + i * 2u] == sample_word(1, i));
      /* Both directions with wraps: identical to the unpolled result. */
      for (uint32_t reverse = 0; reverse < 2; ++reverse) {
        for (uint32_t pass = 0; pass < 2; ++pass) {
          uint32_t *out = pass ? polled : plain, cursor = length - 1u;
          wave_poll = pass ? count_decode_poll : 0;
          decode_polls = 0;
          out[0] = 0x12345678; out[ONE_CACHE_FRAMES * 2u + 1u] = 0x87654321;
          CHECK(!wave_read_direction(&file, &w, &index, &cursor, reverse, out + 1,
                                     ONE_CACHE_FRAMES, scratch, sizeof(scratch)));
          CHECK(cursor == wave_frame_offset(length - 1u, ONE_CACHE_FRAMES, length, reverse));
          if (pass && length >= ONE_CACHE_FRAMES) CHECK(decode_polls >= 8u);
        }
        CHECK(!memcmp(plain, polled, sizeof(plain)));
        for (uint32_t i = 0; i < ONE_CACHE_FRAMES; ++i) {
          uint32_t start = length - 1u;
          uint32_t frame = reverse ? (start + length - (i % length)) % length : (start + i) % length;
          CHECK(polled[1 + i * 2u] == sample_word(1, frame));
        }
      }
    }
  wave_poll = saved;
}
#endif
int main(void) {
  test_fat();
  test_wave();
  test_sd();
  test_bulk_sd_and_audio_copy();
  test_aligned_and_unaligned_memcpy();
  test_ring();
  test_advertisement();
  test_application();
  test_settings_playlist();
  test_long_playlist_name();
  test_policy();
  test_modes();
  test_sd_write();
  test_ui();
  test_recall();
  test_state_allocation();
  test_build_info();
#ifdef ONE_PROFILE
  test_profile_file();
#endif
#ifdef ONE_CV_DIAG
  test_cv_file();
#endif
  test_front_panel_and_remember();
  test_playback_error_recovery_and_read_ahead();
  test_fade_gate_read_ahead();
  test_click_overlapping_file_browse();
  test_build_info_write_failure_does_not_block_playback();
#ifdef ONE_FLIP
  test_reverse_index();
  test_reverse_wave();
  test_flip_transport();
  test_cv_flip_transport();
  test_tape_transport();
  test_chop_transport();
#endif
#ifdef ONE_COLOUR
  test_colour();
  test_filter();
  test_filter_shapes();
  test_mod();
  test_wah();
  test_phaser();
  test_flanger();
#endif
#ifdef ONE_POLL_DECODE
  test_polled_decode();
#endif
#ifdef ONE_TOLERANT_ENCODER
  test_encoder();
  test_encoder_filter();
#endif
  printf("standalone core: %u checks passed\n", checks);
  return 0;
}
