/* SPDX-License-Identifier: GPL-3.0-only */
#include "../firmware/platform.h"
#include "../firmware/profile.h"
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
      crc_bad, fail, disk_mode, background, crc_on;
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
    enqueue(m, (crc & 255) ^ (m->crc_bad ? 1 : 0));
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
#ifdef ONE_PROFILE
static uint32_t profile_mock_ticks;
uint32_t board_ticks(void) { return app_card.time * 24000u + ++profile_mock_ticks; }
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
static void browse_next(void) {
  static const uint32_t sequence[4] = {2, 0, 1, 3};
  for (uint32_t i = 0; i < 4; i++) {
    host_pins = (host_pins & ~0xa0u) | ((sequence[i] & 1u) << 5) |
                ((sequence[i] & 2u) << 6);
    app_tick();
  }
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
  Settings s = {PLAY_TRIGGER, CV_FREE, 0};
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
  for(uint32_t i=0;i<4;++i) {
    host_pins=(host_pins&~0xa0u)|((backwards[i]&1u)<<5)|((backwards[i]&2u)<<6);app_tick();
  }
  host_pins|=0x200u;
  for(uint32_t i=0;i<3;++i) {++app_card.time;app_tick();}
  click_play();CHECK(host_active);host_offset=100u*8u;pulse();flip_finish_transition(1);
  CHECK(host_audio[1024]==sample_word(1,(100u+5000u-512u)%5000u));
  click_play();CHECK(!host_active);
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
  test_front_panel_and_remember();
  test_playback_error_recovery_and_read_ahead();
  test_fade_gate_read_ahead();
  test_click_overlapping_file_browse();
  test_build_info_write_failure_does_not_block_playback();
#ifdef ONE_FLIP
  test_reverse_index();
  test_reverse_wave();
  test_flip_transport();
#endif
  printf("standalone core: %u checks passed\n", checks);
  return 0;
}
