/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"

/* Bounded line reader. Accept LF/CRLF and an unterminated final line. */
static int line(File *f, char *out, uint32_t capacity) {
  uint32_t used = 0, overflow = 0;
  out[0] = 0;
  if (f->pos == f->size)
    return 0;
  while (f->pos < f->size) {
    uint8_t c;
    uint32_t got;
    if (file_read(f, &c, 1, &got) || got != 1)
      return -1;
    if (c == '\n')
      break;
    if (c == '\r')
      continue;
    if (!c)
      return -2;
    if (used + 1 < capacity)
      out[used++] = (char)c;
    else
      overflow = 1;
  }
  while (used && (out[used - 1] == ' ' || out[used - 1] == '\t'))
    used--;
  out[used] = 0;
  return overflow ? -2 : 1;
}
static int equal(const char *a, const char *b) {
  while (*a == ' ' || *a == '\t')
    a++;
  while (*a && *b) {
    char c = *a++;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c != *b++)
      return 0;
  }
  return !*a && !*b;
}
#ifdef ONE_COLOUR
/* "KEY n" with a single digit; returns 1 and scales 0..9 to 0..255. */
static int amount(const char *text, const char *key, uint32_t *value) {
  while (*text == ' ' || *text == '\t')
    text++;
  while (*key) {
    char c = *text++;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c != *key++)
      return 0;
  }
  if (*text != ' ' && *text != '\t')
    return 0;
  while (*text == ' ' || *text == '\t')
    text++;
  if (*text < '0' || *text > '9' || text[1])
    return 0;
  *value = (uint32_t)(*text - '0') * 255u / 9u;
  return 1;
}
#endif
#ifdef ONE_COLOUR
/* "PLAYLIST name" or "PLAYLIST name.txt": one to eight letters, digits or ~.
 * Stores "NAME.TXT". Anything else is not a playlist line. */
static int playlist_line(const char *text, Settings *s) {
  static const char key[] = "PLAYLIST";
  while (*text == ' ' || *text == '\t')
    text++;
  for (uint32_t i = 0; key[i]; i++) {
    char c = *text++;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c != key[i])
      return 0;
  }
  if (*text != ' ' && *text != '\t')
    return 0;
  while (*text == ' ' || *text == '\t')
    text++;
  char name[13];
  uint32_t n = 0;
  for (;;) {
    char c = text[n];
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '~'))
      break;
    if (n == 8)
      return 0;
    name[n++] = c;
  }
  if (!n || (text[n] && !equal(text + n, ".TXT")))
    return 0;
  name[n++] = '.';
  name[n++] = 'T';
  name[n++] = 'X';
  name[n++] = 'T';
  name[n] = 0;
  for (uint32_t i = 0; i <= n; i++)
    s->playlist[i] = name[i];
  return 1;
}
#endif
static int effect_line(const char *text, Settings *s) {
#ifdef ONE_COLOUR
  if (playlist_line(text, s))
    return 1;
  /* The last cutoff line sets both the cutoff and the filter's output. */
  if (amount(text, "FILTER", &s->filter) || amount(text, "LOWPASS", &s->filter)) {
    s->shape = FILTER_LOW;
    return 1;
  }
  if (amount(text, "HIGHPASS", &s->filter)) {
    s->shape = FILTER_HIGH;
    return 1;
  }
  if (amount(text, "BANDPASS", &s->filter)) {
    s->shape = FILTER_BAND;
    return 1;
  }
  return amount(text, "DRIVE", &s->drive) || amount(text, "CRUSH", &s->crush) ||
         amount(text, "RESONANCE", &s->resonance) ||
         amount(text, "TREMOLO", &s->tremolo) || amount(text, "RING", &s->ring) ||
         amount(text, "RATE", &s->rate) || amount(text, "WAH", &s->wah) ||
         amount(text, "PHASER", &s->phaser) || amount(text, "FLANGER", &s->flanger);
#else
  (void)text;
  (void)s;
  return 0;
#endif
}
void settings_default(Settings *s) {
  s->play = PLAY_TRIGGER;
  s->cv = CV_QUANTIZE; /* Match the stock boot default without config.txt. */
  s->delay_ms = 0;
#ifdef ONE_COLOUR
  s->drive = s->crush = s->filter = s->resonance = s->shape = 0;
  s->tremolo = s->ring = s->rate = s->wah = s->phaser = s->flanger = 0;
  s->playlist[0] = 0;
#endif
}
int settings_load(Fat *fs, Settings *out) {
  Entry e;
  File f;
  Settings s;
  settings_default(out);
  settings_default(&s);
  int found = fat_find(fs, "CONFIG.TXT", &e);
  if (found <= 0)
    return found;
  if (e.size > 4096)
    return -2;
  if (file_open(&f, fs, &e))
    return -1;
  char text[64];
  int n = line(&f, text, sizeof(text));
  if (n < 0)
    return n;
  const uint8_t *p = (const uint8_t *)text;
  if (p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf)
    p += 3;
  if (n != 1 || *p != '#')
    return -2;
  n = line(&f, text, sizeof(text));
  if (n < 0)
    return n;
  if (n != 1)
    return -2;
  if (equal(text, "TRIGGER"))
    s.play = PLAY_TRIGGER;
  else if (equal(text, "GATE"))
    s.play = PLAY_GATE;
  else if (equal(text, "FADE"))
    s.play = PLAY_FADE;
  else if (equal(text, "LOOP") || equal(text, "LOOPING"))
    s.play = PLAY_LOOP;
  else if (equal(text, "SEQUENCE"))
    s.play = PLAY_SEQUENCE;
  else if (equal(text, "COMBO"))
    s.play = PLAY_COMBO;
  else if (equal(text, "RANDOM SEQUENCE") || equal(text, "RANDOM") ||
           equal(text, "TG SEQUENCE") /* Legacy card alias. */)
    s.play = PLAY_RANDOM_SEQUENCE;
  else if (equal(text, "TG LOOP") || equal(text, "RANDOM LOOP"))
    s.play = PLAY_RANDOM_LOOP;
#ifdef ONE_FLIP
  else if (equal(text, "FLIP"))
    s.play = PLAY_FLIP;
  else if (equal(text, "CV FLIP"))
    s.play = PLAY_CV_FLIP;
  else if (equal(text, "TAPE"))
    s.play = PLAY_TAPE;
  else if (equal(text, "CHOP"))
    s.play = PLAY_CHOP;
#endif
  else
    return -2;
  n = line(&f, text, sizeof(text));
  if (n < 0)
    return n;
  if (n != 1)
    return -2;
  if (equal(text, "FREE"))
    s.cv = CV_FREE;
  else if (equal(text, "QUANTIZE") || equal(text, "QUANTIZED"))
    s.cv = CV_QUANTIZE;
  else if (equal(text, "SELECT FILE"))
    s.cv = CV_SELECT;
#ifdef ONE_COLOUR
  else if (equal(text, "COLOUR") || equal(text, "COLOR"))
    s.cv = CV_COLOUR;
  else if (equal(text, "FILTER"))
    s.cv = CV_FILTER;
  else if (equal(text, "RATE"))
    s.cv = CV_RATE;
#endif
  else
    return -2;
  n = line(&f, text, sizeof(text));
  if (n < 0)
    return n;
  if (n && text[0] && text[0] != '#') {
    if (effect_line(text, &s)) {
    } else if (equal(text, "SHORT"))
      s.delay_ms = 0;
    else if (equal(text, "MEDIUM"))
      s.delay_ms = 2;
    else if (equal(text, "LONG"))
      s.delay_ms = 10;
    else
      return -2;
  }
#ifdef ONE_COLOUR
  /* Later lines: effect amounts and a playlist name are recognised; anything
   * else stays ignored. */
  for (unsigned extra = 0; extra < 12 && n == 1; extra++) {
    n = line(&f, text, sizeof(text));
    if (n < 0)
      return n;
    if (n == 1)
      (void)effect_line(text, &s);
  }
#endif
  *out = s;
  return 1;
}
#ifdef ONE_COLOUR
#define PLAYLIST_FILE name
static int playlist_file(Fat *fs, const char *name, Entry *entries, uint32_t max) {
#else
#define PLAYLIST_FILE "PLAYLIST.TXT"
int playlist_load(Fat *fs, Entry *entries, uint32_t max) {
#endif
  Entry e;
  File f;
  int found = fat_find(fs, PLAYLIST_FILE, &e);
  if (found < 0)
    return -1;
  if (!found)
    return -2;
  if (e.size > 16384 || file_open(&f, fs, &e))
    return -1;
  uint32_t count = 0;
#ifdef ONE_LONG_NAMES
  char text[64];
#else
  char text[16];
#endif
  while (count < max) {
    int n = line(&f, text, sizeof(text));
    if (n == -1)
      return -1;
    if (n <= 0)
      break;
    uint32_t len = 0;
    while (text[len])
      len++;
#ifdef ONE_LONG_NAMES
    /* Any plain-ASCII name up to 63 characters; the card decides if it exists. */
    if (len < 5 || text[len - 4] != '.' || !equal(text + len - 3, "WAV"))
      break;
    for (uint32_t i = 0; i < len - 4; i++)
      if (text[i] < 32 || text[i] > 126)
        return (int)count;
#else
    if (len < 5 || len > 13 || text[len - 4] != '.' ||
        !equal(text + len - 3, "WAV"))
      break;
    for (uint32_t i = 0; i < len - 4; i++) {
      char c = text[i];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '~'))
        return (int)count;
    }
#endif
    found = fat_find(fs, text, &e);
    if (found < 0)
      return -1;
    if (!found)
      break;
    entries[count++] = e;
  }
  return (int)count;
}
#ifdef ONE_COLOUR
int playlist_load(Fat *fs, Entry *entries, uint32_t max) {
  return playlist_file(fs, "PLAYLIST.TXT", entries, max);
}
int playlist_load_named(Fat *fs, const char *name, Entry *entries, uint32_t max) {
  if (name[0]) {
    int n = playlist_file(fs, name, entries, max);
    if (n != -2)
      return n;
  }
  return playlist_file(fs, "PLAYLIST.TXT", entries, max);
}
#endif
