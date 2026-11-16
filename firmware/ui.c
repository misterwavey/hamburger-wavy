/* SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
void ui_init(Ui *u, uint32_t down, uint32_t adc, uint32_t now,
             const Settings *s) {
  memset(u, 0, sizeof(*u));
  u->button = u->boot_held = down;
  u->anchor = adc;
  u->time = now;
  u->play = s->play;
  u->cv = s->cv;
}
static uint32_t enter(Ui *u, const Settings *s, uint32_t now) {
  if (u->menu)
    return 0;
  u->menu = 1;
  u->led_time = now;
  u->kind = 0;
  u->play = s->play;
  u->cv = s->cv;
  return UI_ENTER;
}
uint32_t ui_poll(Ui *u, uint32_t down, int32_t step, uint32_t adc, uint32_t now,
                 const Settings *s) {
  uint32_t event = 0;
  if (now != u->time) {
    u->time = now;
    if (down == u->button)
      u->count = 0;
    else if (++u->count >= 3) {
      u->button = down;
      u->count = 0;
      if (down) {
        u->press_time = now;
        u->file_edit = 0;
        u->anchor = adc;
      } else {
        if (u->boot_held)
          u->boot_held = 0;
        else if (u->menu) {
          u->menu = 0;
          event |= UI_COMMIT;
        } else
          event |= UI_CLICK;
      }
    }
  }
  if (!u->button || u->boot_held)
    return event;
  /* A short click overlapping FILE motion is still a playback click. */
  if ((uint32_t)(now - u->press_time) < 400u)
    return event;
  event |= enter(u, s, now);
  if (step) {
    /* A FILE gesture wins over earlier pitch/LFO movement in this hold. */
    if (!u->file_edit)
      u->cv = s->cv;
    int32_t next = (int32_t)u->play + step;
    while (next < 0)
      next += 8;
    u->play = (PlayMode)(next % 8);
    u->kind = 0;
    u->file_edit = 1;
    u->led_time = now;
  }
  uint32_t distance = adc > u->anchor ? adc - u->anchor : u->anchor - adc;
  if (!u->file_edit && distance >= 64u) {
    CvMode previous = u->cv;
    uint32_t previous_kind = u->kind;
    event |= enter(u, s, now);
    if (adc < 256u)
      u->cv = CV_FREE;
    else if (adc >= 384u && adc <= 640u)
      u->cv = CV_QUANTIZE;
    else if (adc >= 768u)
      u->cv = CV_SELECT;
    u->kind = 1;
    if (u->cv != previous || previous_kind != 1u)
      u->led_time = now;
    u->anchor = adc;
  }
  return event;
}
uint32_t ui_led(const Ui *u, uint32_t now) {
  uint32_t number = u->kind ? (uint32_t)u->cv + 1u : (uint32_t)u->play + 1u;
  uint32_t elapsed = now - u->led_time;
  /* Let the encoder settle; never display the tail of an old count. */
  if (elapsed < 350u)
    return 0;
  uint32_t phase = (elapsed - 350u) % (number * 400u + 1000u);
  if (phase >= number * 400u || phase % 400u >= 200u)
    return 0;
  return u->kind ? 1u : 2u;
}
