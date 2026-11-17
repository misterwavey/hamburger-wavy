/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef ONE_CV_DIAG
#include "build.h"
#include "platform.h"
#include "cv_capture.h"
enum {READY,SETTLE,CAPTURE,WRITING,SAVED,ERROR,FULL};
static struct {
  Sd card;Fat fs;CvRow rows[CV_ROWS];uint8_t report[1024];
  uint32_t count,phase,phase_time,sample_time,button,time,debounce,armed,boot_held;
} d;
void app_background(void) { board_watchdog(); }
static void reset_button(void) {
  d.button=(board_pins()&0x200u)==0;d.boot_held=d.button;
  d.time=board_millis();d.debounce=d.armed=0;
}
static int mount(void) {
  if(sd_init(&d.card,&board_sd_io) || fat_mount(&d.fs,sd_read,&d.card)) return -1;
  d.fs.write=sd_write;(void)fat_build_info(&d.fs,ONE_BUILD_NUMBER);return 0;
}
static void save(void) {
  d.phase=WRITING;board_led(1);
  if(cv_format_report(d.report,d.rows,d.count,ONE_BUILD_NUMBER) ||
     fat_cv_write(&d.fs,d.report)) d.phase=ERROR;
  else {d.phase=SAVED;d.phase_time=board_millis();}
  reset_button();
}
void cv_diag_init(void) {
  memset(&d,0,sizeof(d));board_audio_stop();
  d.phase=mount() ? ERROR : READY;reset_button();
  board_led(d.phase==READY ? 0 : 1);
}
void cv_diag_tick(void) {
  board_watchdog();uint32_t now=board_millis(),click=0;
  uint32_t down=(board_pins()&0x200u)==0;
  if(now!=d.time) {
    d.time=now;
    if(down==d.button) d.debounce=0;
    else if(++d.debounce>=3) {
      d.button=down;d.debounce=0;
      if(down) d.armed=d.phase==READY || d.phase==ERROR;
      else {
        if(d.boot_held) d.boot_held=0;
        else click=d.armed;
        d.armed=0;
      }
    }
  }
  if(click && d.phase==ERROR) {
    if(!mount()) {
      if(d.count) save();else {d.phase=READY;reset_button();}
    }
  } else if(click && d.phase==READY) {
    if(d.count==CV_ROWS) d.phase=FULL;
    else {d.phase=SETTLE;d.phase_time=now;cv_stats_init(d.rows+d.count);}
  }
  if(d.phase==SETTLE && (uint32_t)(now-d.phase_time)>=CV_SETTLE_MS) {
    d.phase=CAPTURE;d.phase_time=now;d.sample_time=now-1u;
  }
  if(d.phase==CAPTURE) {
    uint32_t elapsed=now-d.phase_time;
    CvRow *r=d.rows+d.count;
    if(elapsed>=CV_WINDOW_MS) {
      r->elapsed=elapsed;r->missed=CV_WINDOW_MS-r->n;
      ++d.count;save();
    } else if(now!=d.sample_time) {
      d.sample_time=now;
      if(cv_stats_add(r,board_adc())) d.phase=ERROR;
    }
  }
  now=board_millis();
  if(d.phase==SAVED && (uint32_t)(now-d.phase_time)>=250u) d.phase=READY;
  if(d.phase==ERROR) board_led((now/250u)&1u ? 1u : 2u);
  else if(d.phase==SETTLE) board_led((now/100u)&1u ? 2u : 0u);
  else if(d.phase==CAPTURE || d.phase==FULL) board_led(2);
  else if(d.phase==SAVED || d.phase==WRITING) board_led(1);
  else board_led(0);
}
void standalone_main(void) {
  board_init();advertise_build(ONE_BUILD_NUMBER);cv_diag_init();
  for(;;) cv_diag_tick();
}
#endif
