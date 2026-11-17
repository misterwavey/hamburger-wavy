/* SPDX-License-Identifier: GPL-3.0-only */
#include "../firmware/cv_capture.h"
#include "../firmware/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t now,pins=0xaa0,adc=512,led,writes,fail_write,fail_mount,build,stops;
static uint8_t saved[1024];
#define CHECK(x) do {if(!(x)){fprintf(stderr,"CV test line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
const SdIo board_sd_io={0};
void board_init(void) {}
void board_watchdog(void) {}
uint32_t board_millis(void) {return now;}
uint32_t board_pins(void) {return pins;}
uint32_t board_adc(void) {return adc;}
void board_led(uint32_t value) {led=value;}
void board_audio_stop(void) {++stops;}
void advertise_build(uint32_t n) {(void)n;}
int sd_init(Sd *s,const SdIo *io) {(void)s;(void)io;return fail_mount ? -1 : 0;}
int sd_read(void *ctx,uint32_t lba,uint8_t *p) {(void)ctx;(void)lba;(void)p;return -1;}
int sd_write(void *ctx,uint32_t lba,const uint8_t *p) {(void)ctx;(void)lba;(void)p;return -1;}
int fat_mount(Fat *f,BlockRead r,void *ctx) {(void)f;(void)r;(void)ctx;return 0;}
int fat_build_info(Fat *f,uint32_t n) {(void)f;build=n;return 1;}
int fat_cv_write(Fat *f,const uint8_t data[1024]) {
  (void)f;CHECK(led==1);++writes;now+=37;
  if(fail_write)return -1;
  memcpy(saved,data,1024);return 0;
}
static void tick(uint32_t count) {while(count--){++now;cv_diag_tick();}}
static void click(void) {pins&=~0x200u;tick(3);pins|=0x200u;tick(3);}
static uint32_t start(void) {click();tick(500);return now;}
static void finish(uint32_t start_time) {while((uint32_t)(now-start_time)<5000u)tick(1);}
static void capture(uint32_t value) {adc=value;uint32_t t=start();finish(t);CHECK(led==1);tick(250);CHECK(led==0);}
static void init(void) {pins=0xaa0;writes=fail_write=fail_mount=0;cv_diag_init();CHECK(build==29 && led==0);}
int main(int argc,char **argv) {
  CHECK(argc==2);CvRow row;
  cv_stats_init(&row);
  for(uint32_t i=0;i<5000;++i)CHECK(!cv_stats_add(&row,1023));
  CHECK(row.ss_high==1 && row.ss_low==937677704u && row.sum==5115000);
  CHECK(cv_stats_add(&row,0)<0 && row.n==5000);
  cv_stats_init(&row);CHECK(cv_stats_add(&row,1024)<0 && row.n==0);
  now=100;init();CHECK(stops);
  uint32_t t=start();for(uint32_t i=0;i<100;++i)cv_diag_tick();
  finish(t);CHECK(writes==1 && led==1);tick(249);CHECK(led==1);tick(1);CHECK(led==0);
  CHECK(strstr((char *)saved,"rows=1\n") && strstr((char *)saved,"1,5000,512,512,2560000,"));
  capture(0);capture(1023);
  /* A click during capture is ignored, with no hidden extra row. */
  adc=123;t=start();pins&=~0x200u;tick(3);pins|=0x200u;tick(3);finish(t);tick(250);
  CHECK(writes==4 && strstr((char *)saved,"rows=4\n"));
  /* A skipped time interval is visible instead of fabricated duplicate samples. */
  t=start();now+=5000;cv_diag_tick();tick(250);
  CHECK(strstr((char *)saved,"5,1,123,123,123,15129,0,0,0,5000,4999\n"));
  /* Failed writes retain the same row set for a PLAY retry. */
  fail_write=1;adc=256;t=start();finish(t);CHECK(writes==6);
  fail_write=0;click();CHECK(writes==7 && led==1 && strstr((char *)saved,"rows=6\n"));tick(250);
  /* Button held through a busy period must not start a new capture on release. */
  t=start();pins&=~0x200u;tick(3);finish(t);tick(250);pins|=0x200u;tick(3);tick(6000);
  CHECK(writes==8 && strstr((char *)saved,"rows=7\n"));
  now=0xfffffd00u;init();capture(33);CHECK(strstr((char *)saved,",5000,0\n"));
  /* Startup-held PLAY release is ignored, including after updater use. */
  pins&=~0x200u;writes=0;cv_diag_init();pins|=0x200u;tick(3);tick(6000);CHECK(!writes);
  /* Recover mount failure without emitting an empty report. */
  fail_mount=1;cv_diag_init();click();CHECK(!writes);fail_mount=0;click();CHECK(!writes && led==0);
  /* Maximum real report: every capture at the upper rail, including 64-bit carry. */
  init();for(uint32_t i=0;i<12;++i)capture(1023);
  CHECK(writes==12 && strstr((char *)saved,"rows=12\n"));click();tick(6000);CHECK(writes==12 && led==2);
  FILE *file=fopen(argv[1],"wb");CHECK(file);CHECK(fwrite(saved,1,1024,file)==1024);CHECK(!fclose(file));
  uint8_t output[1025];output[1024]=0xa5;
  CHECK(cv_format_report(output,&row,0,29)<0);
  CvRow too_large[12];memset(too_large,255,sizeof(too_large));
  CHECK(cv_format_report(output,too_large,12,29)<0 && output[1024]==0xa5);
  puts("CV capture/controller checks passed");return 0;
}
