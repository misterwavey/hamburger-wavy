/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ONE_CORE_H
#define ONE_CORE_H
#include <stddef.h>
#include <stdint.h>
#define ONE_MAX_FILES 256u
#define ONE_HALF_FRAMES 512u
#define ONE_CACHE_FRAMES (ONE_HALF_FRAMES * 2u)
typedef int (*BlockRead)(void *, uint32_t, uint8_t *);
typedef int (*BlockWrite)(void *, uint32_t, const uint8_t *);
typedef struct {
  void *ctx;
  int (*xfer)(void *, uint8_t);
  void (*select)(void *, int);
  void (*speed)(void *, int);
  uint32_t (*millis)(void *);
} SdIo;
typedef struct {
  const SdIo *io;
  uint32_t high_capacity, ready;
} Sd;
int sd_init(Sd *, const SdIo *);
int sd_read(void *, uint32_t, uint8_t *);
int sd_write(void *, uint32_t, const uint8_t *);
typedef struct {
  BlockRead read;
  BlockWrite write;
  void *ctx;
  uint32_t start, total, fat, data, root, root_entries, fat_sectors, clusters,
      spc, type;
  uint32_t fat_base, fat_copies, mirrored, reserved, fsinfo, backup_boot;
  uint32_t cache_lba, fat_lba;
  uint8_t cache[512], fat_cache[512];
} Fat;
typedef struct {
  uint32_t cluster, size, attr;
  char name[14];
} Entry;
typedef struct {
  Fat *fs;
  uint32_t first, cluster, cluster_index, size, pos;
} File;
int fat_mount(Fat *, BlockRead, void *);
int fat_list(Fat *, Entry *, uint32_t);
int fat_find(Fat *, const char *, Entry *); /* 1 found, 0 absent, -1 I/O */
int fat_state_area(Fat *, uint32_t[2]);
int fat_build_info(Fat *, uint32_t);
int file_open(File *, Fat *, const Entry *);
int file_seek(File *, uint32_t);
int file_read(File *, void *, uint32_t, uint32_t *);
typedef struct {
  uint32_t offset, bytes, rate, bits, remaining;
} Wave;
int wave_open(File *, Wave *);
int wave_read(File *, Wave *, uint32_t *, uint32_t, uint32_t *, uint8_t *,
              uint32_t);
int wave_seek_frame(File *, Wave *, uint32_t);
int wave_read_loop(File *, Wave *, uint32_t *, uint32_t, uint8_t *, uint32_t);

typedef enum {
  PLAY_TRIGGER,
  PLAY_GATE,
  PLAY_FADE,
  PLAY_LOOP,
  PLAY_SEQUENCE,
  PLAY_COMBO,
  PLAY_RANDOM_SEQUENCE,
  PLAY_RANDOM_LOOP
} PlayMode;
typedef enum { CV_FREE, CV_QUANTIZE, CV_SELECT } CvMode;
typedef struct {
  PlayMode play;
  CvMode cv;
  uint32_t delay_ms;
} Settings;
void settings_default(Settings *);
int settings_load(Fat *,
                  Settings *); /* 1 loaded, 0 absent, -1 I/O, -2 invalid */
int playlist_load(Fat *, Entry *, uint32_t); /* -2 absent, -1 I/O, else count */
uint32_t pitch_divider(uint32_t, uint32_t, CvMode);
uint32_t cv_file_offset(uint32_t, uint32_t);
typedef struct {
  uint32_t lba[2], available, sequence, slot, saved_index, size;
  char name[14];
} Recall;
int recall_open(Recall *, Fat *, const Entry *, uint32_t, uint32_t *);
int recall_save(Recall *, Fat *, const Entry *, uint32_t);

typedef struct {
  uint32_t button, count, time, press_time, menu, kind, anchor, boot_held, led_time, file_edit;
  PlayMode play;
  CvMode cv;
} Ui;
#define UI_CLICK 1u
#define UI_ENTER 2u
#define UI_COMMIT 4u
void ui_init(Ui *, uint32_t, uint32_t, uint32_t, const Settings *);
uint32_t ui_poll(Ui *, uint32_t, int32_t, uint32_t, uint32_t, const Settings *);
uint32_t ui_led(const Ui *, uint32_t);
uint32_t random_next(uint32_t *);
uint32_t random_below(uint32_t *, uint32_t);
typedef struct {
  uint32_t gain, target;
} Envelope;
void envelope_apply(Envelope *, uint32_t *, uint32_t);
void envelope_tail(uint32_t *, uint32_t, uint32_t, uint32_t);
int pcm_decode(const uint8_t *, uint32_t, uint32_t *, uint32_t);
typedef struct {
  uint32_t half, pending, blocks, underruns, running;
} Ring;
void ring_start(Ring *);
int ring_boundary(Ring *, uint32_t);
int ring_commit(Ring *, uint32_t);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
#endif
