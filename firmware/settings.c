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
void settings_default(Settings *s) {
  s->play = PLAY_TRIGGER;
  s->cv = CV_QUANTIZE; /* Match the stock boot default without config.txt. */
  s->delay_ms = 0;
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
  else
    return -2;
  n = line(&f, text, sizeof(text));
  if (n < 0)
    return n;
  if (n && text[0] && text[0] != '#') {
    if (equal(text, "SHORT"))
      s.delay_ms = 0;
    else if (equal(text, "MEDIUM"))
      s.delay_ms = 2;
    else if (equal(text, "LONG"))
      s.delay_ms = 10;
    else
      return -2;
  }
  *out = s;
  return 1;
}
int playlist_load(Fat *fs, Entry *entries, uint32_t max) {
  Entry e;
  File f;
  int found = fat_find(fs, "PLAYLIST.TXT", &e);
  if (found < 0)
    return -1;
  if (!found)
    return -2;
  if (e.size > 16384 || file_open(&f, fs, &e))
    return -1;
  uint32_t count = 0;
  char text[16];
  while (count < max) {
    int n = line(&f, text, sizeof(text));
    if (n == -1)
      return -1;
    if (n <= 0)
      break;
    uint32_t len = 0;
    while (text[len])
      len++;
    if (len < 5 || len > 13 || text[len - 4] != '.' ||
        !equal(text + len - 3, "WAV"))
      break;
    for (uint32_t i = 0; i < len - 4; i++) {
      char c = text[i];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '~'))
        return (int)count;
    }
    found = fat_find(fs, text, &e);
    if (found < 0)
      return -1;
    if (!found)
      break;
    entries[count++] = e;
  }
  return (int)count;
}
