/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ONE_CORE_H
#define ONE_CORE_H
#include <stddef.h>
#include <stdint.h>
#define ONE_MAX_FILES 256u
#define ONE_HALF_FRAMES 1024u
#define ONE_CACHE_FRAMES (ONE_HALF_FRAMES * 2u)
typedef int (*BlockRead)(void *, uint32_t, uint8_t *);
typedef int (*BlockWrite)(void *, uint32_t, const uint8_t *);
typedef struct {
  void *ctx;
  int (*xfer)(void *, uint8_t);
  void (*select)(void *, int);
  void (*speed)(void *, int);
  uint32_t (*millis)(void *);
  /* Optional bulk receive; returns CRC16 with seed zero on success. */
  int (*read_bytes)(void *, uint8_t *, uint32_t, uint16_t *);
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
#ifdef ONE_CV_DIAG
int fat_cv_write(Fat *, const uint8_t[1024]);
#endif
#ifdef ONE_PROFILE
int fat_profile_write(Fat *, const uint8_t[1024]);
#endif
typedef struct {
  uint32_t first, size, stride, count, contiguous;
  uint32_t cluster[32], boundary_lba[2], boundary_slot;
  uint8_t *boundary_owner;
} FileIndex;
int file_index_build(File *, FileIndex *);
int file_seek_indexed(File *, const FileIndex *, uint32_t);
int file_read_window(File *, FileIndex *, void *, uint32_t, uint8_t *);
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
uint32_t wave_frame_offset(uint32_t, uint32_t, uint32_t, uint32_t);
int wave_read_direction(File *, Wave *, FileIndex *, uint32_t *, uint32_t,
                        uint32_t *, uint32_t, uint8_t *, uint32_t);
int pcm_decode_reverse(const uint8_t *, uint32_t, uint32_t *, uint32_t);
#ifdef ONE_POLL_DECODE
/* Optional hook called between decode chunks while reading; may be null. */
extern void (*wave_poll)(void);
#endif

#ifdef ONE_TOLERANT_ENCODER
/* Quadrature decoder that survives unseen states within a detent.
 * ab: bit 0 = A, bit 1 = B; 3 is the detent. Returns +1, -1 or 0. */
typedef struct {
  uint8_t last, skipped, jumped;
  int8_t acc, dir, previous;
  uint32_t steps, recovered, guessed; /* diagnostics */
  uint8_t filtered, timing, raw;
  uint32_t since[2], raw_changes, bridged_max; /* bridged_max in ticks */
  uint32_t polled, fall_age[2], period, hold_min; /* ticks */
} Encoder;
#define ENCODER_RELEASE_MS 3u /* longest hold, used at rest and slow turns */
#define ENCODER_TICKS_PER_MS 24000u
#define ENCODER_HOLD_MAX (ENCODER_RELEASE_MS * ENCODER_TICKS_PER_MS)
#define ENCODER_HOLD_MIN (ENCODER_TICKS_PER_MS / 2u)
#define ENCODER_AGE_MAX (200u * ENCODER_TICKS_PER_MS)
void encoder_init(Encoder *, uint32_t ab);
/* Contact filter: a line reads low at once, and high again only after it has
 * stayed high for the hold. The hold is an eighth of the time between a
 * line's successive closures, between 0.5 ms and ENCODER_RELEASE_MS, so it
 * stays shorter than a quarter cycle during fast spins. `now` is a free-
 * running 24 MHz tick count. Returns the filtered ab. */
uint32_t encoder_filter(Encoder *, uint32_t ab, uint32_t now);
int32_t encoder_step(Encoder *, uint32_t ab);
#endif

typedef enum {
  PLAY_TRIGGER,
  PLAY_GATE,
  PLAY_FADE,
  PLAY_LOOP,
  PLAY_SEQUENCE,
  PLAY_COMBO,
  PLAY_RANDOM_SEQUENCE,
  PLAY_RANDOM_LOOP,
  PLAY_FLIP,
#ifdef ONE_FLIP
  PLAY_CV_FLIP,
  PLAY_TAPE,
  PLAY_CHOP,
  PLAY_COUNT
#else
  PLAY_COUNT = PLAY_FLIP
#endif
} PlayMode;
#if defined(ONE_COLOUR) && !defined(ONE_FLIP)
#error "Colour is part of the extended application"
#endif
typedef enum {
  CV_FREE,
  CV_QUANTIZE,
  CV_SELECT,
#ifdef ONE_COLOUR
  CV_COLOUR, /* config.txt only: native pitch, knob/CV sets colour amount */
  CV_FILTER, /* config.txt only: native pitch, knob/CV sets filter cutoff */
  CV_RATE,   /* config.txt only: native pitch, knob/CV sets tremolo/ring rate */
#endif
} CvMode;
typedef struct {
  PlayMode play;
  CvMode cv;
  uint32_t delay_ms;
#ifdef ONE_COLOUR
  uint32_t drive, crush; /* 0 off .. 255 */
  uint32_t filter, resonance; /* cutoff 0 off, 1 dark .. 255 open; 0 .. 255 */
  uint32_t shape; /* FILTER_LOW, FILTER_HIGH or FILTER_BAND */
  uint32_t tremolo, ring, rate; /* depths 0 off .. 255; rate 0 .. 255 */
  uint32_t wah; /* filter cutoff sweep depth, 0 off .. 255 */
  uint32_t phaser; /* intensity, 0 off .. 255 */
  uint32_t flanger; /* intensity, 0 off .. 255; takes the phaser's place */
  char playlist[13]; /* "NAME.TXT" from a Playlist line, or empty for PLAYLIST.TXT */
#endif
} Settings;
void settings_default(Settings *);
int settings_load(Fat *,
                  Settings *); /* 1 loaded, 0 absent, -1 I/O, -2 invalid */
int playlist_load(Fat *, Entry *, uint32_t); /* -2 absent, -1 I/O, else count */
#ifdef ONE_COLOUR
/* The named list if it exists, otherwise PLAYLIST.TXT. */
int playlist_load_named(Fat *, const char *name, Entry *, uint32_t);
#endif
uint32_t pitch_divider(uint32_t, uint32_t, CvMode);
uint32_t cv_file_offset(uint32_t, uint32_t);
#ifdef ONE_FLIP
uint32_t cv_frame_offset(uint32_t, uint32_t);
#endif
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
#ifdef ONE_COLOUR
typedef struct {
  uint32_t hold, count;
} Colour;
void colour_apply(Colour *, uint32_t *, uint32_t, uint32_t, uint32_t);
typedef struct {
  int32_t low, band;
  uint32_t f; /* Q23 running coefficient; Q15 targets */
} Filter;
uint32_t filter_coefficient(uint32_t);
void filter_apply(Filter *, uint32_t *, uint32_t, uint32_t, uint32_t);
/* The same filter with a choice of output: low-pass, high-pass or band-pass. */
enum { FILTER_LOW, FILTER_HIGH, FILTER_BAND };
void filter_shape(Filter *, uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t);
/* Tremolo / ring modulation: one sine oscillator multiplying the signal. */
typedef struct {
  uint32_t phase; /* full circle is 2^32 */
} Mod;
#define MOD_START_PHASE 0xc0000000u /* oscillator at its positive peak */
uint32_t mod_increment(uint32_t rate, uint32_t ring, uint32_t sample_rate);
void mod_apply(Mod *, uint32_t *, uint32_t frames, uint32_t increment,
               uint32_t depth, uint32_t ring);
/* Wah: a slow sine moves the filter cutoff around its set value. */
#define WAH_START_PHASE 0x80000000u /* sine at zero and rising: cutoff at its centre */
int32_t mod_sine(uint32_t phase); /* Q15, -32768 .. 32767 */
uint32_t wah_increment(uint32_t rate /* 0..1023 */, uint32_t sample_rate);
uint32_t wah_cutoff(uint32_t centre, uint32_t depth, int32_t sine);
/* Phaser: four all-pass stages with feedback, mixed with the dry signal. */
typedef struct {
  int32_t z[5]; /* previous input, then each stage's previous output */
  uint32_t a;   /* Q23 running coefficient; Q15 targets */
} Phaser;
#define PHASER_CENTRE 120u /* cutoff-control units, as for the filter */
#define PHASER_SWING 204u  /* wah_cutoff depth: 80 units each way */
uint32_t phaser_coefficient(uint32_t cutoff);
void phaser_apply(Phaser *, uint32_t *, uint32_t frames, uint32_t target,
                  uint32_t intensity);
/* Flanger: a short swept delay with feedback, mixed with the dry signal.
 * The delayed signal is kept at 16 bits. */
#define FLANGER_FRAMES 512u
typedef struct {
  int16_t line[FLANGER_FRAMES];
  uint32_t at;    /* next write position */
  uint32_t delay; /* running delay in frames, Q16 */
} Flanger;
uint32_t flanger_delay(int32_t sine); /* Q16 frames, 32 to 384 */
void flanger_apply(Flanger *, uint32_t *, uint32_t frames, uint32_t target,
                   uint32_t intensity);
#endif
typedef struct {
  uint32_t half, pending, blocks, underruns, running;
} Ring;
void ring_start(Ring *);
int ring_boundary(Ring *, uint32_t);
int ring_commit(Ring *, uint32_t);
void *memcpy(void *, const void *, size_t);
void audio_copy(uint32_t *, const uint32_t *, uint32_t);
void *memset(void *, int, size_t);
#endif
