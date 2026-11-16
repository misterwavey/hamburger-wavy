/* SPDX-License-Identifier: GPL-3.0-only */
#include "../firmware/platform.h"
#include "../firmware/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t ticks, offset, writes, reject;
static uint8_t saved[1025];
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"profile test line %d\n",__LINE__); exit(1); } } while (0)
uint32_t board_ticks(void) { return ticks++; }
uint32_t board_audio_offset(void) { return offset; }
void board_stack_info(uint32_t out[3]) {
  out[0]=33488; out[1]=2048; out[2]=29992;
}
int fat_profile_write(Fat *fs, const uint8_t data[1024]) {
  (void)fs;
  CHECK(!profile_running());
  ++writes;
  if (reject) return -1;
  memcpy(saved,data,1024);
  return 0;
}
int main(int argc, char **argv) {
  CHECK(argc==2);
  Fat fs = {0};
  ticks=0xffffff00u;
  profile_init();
  profile_start(96000,24,PLAY_FADE,CV_QUANTIZE,1000);
  ticks+=300;
  profile_poll(); /* Unsigned interval crosses the hardware timer wrap. */
  profile_sample(PROF_SD,9600);
  profile_sample(PROF_SD,48000);
  profile_sample(PROF_PREFETCH,60000);
  profile_sample(PROF_DECODE,2400);
  profile_sample(PROF_ENVELOPE,1200);
  profile_sample(PROF_COPY,240);
  profile_pitch(2000);
  offset=4096+512;
  profile_margin(0); /* 448 frames remain in the active half. */
  profile_publish(1440);
  /* More remaining frames can still mean less time at a faster pitch. */
  profile_pitch(1000); offset=4096+256; profile_margin(0);
  profile_error(PROF_LATE_ERROR);
  ticks+=100000;
  profile_stop(1000);
  profile_idle(&fs,1249,1); CHECK(writes==0);
  profile_idle(&fs,1250,0); CHECK(writes==0);
  profile_idle(&fs,1250,1); CHECK(writes==1);
  profile_idle(&fs,2000,1); CHECK(writes==1);
  CHECK(strstr((char *)saved,"plays=1\n")!=NULL);
  CHECK(strstr((char *)saved,"min_slack_frames=480\n")!=NULL);
  CHECK(strstr((char *)saved,"wall_hi=0\n")!=NULL);
  CHECK(strstr((char *)saved,"refill_work,1,61440,0,61440,61440\n")!=NULL);
  /* Failed report is attempted only once per stop, retried after another run. */
  profile_start(48000,16,PLAY_TRIGGER,CV_FREE,2000);
  profile_stop(2000);
  reject=1; profile_idle(&fs,2250,1); CHECK(writes==2);
  profile_idle(&fs,9000,1); CHECK(writes==2);
  profile_start(48000,16,PLAY_TRIGGER,CV_FREE,2000);
  profile_sample(PROF_PREFETCH,2400);
  offset=0; profile_margin(0); profile_publish(240);
  profile_stop(9000);
  reject=0; profile_idle(&fs,9250,1); CHECK(writes==3);
  CHECK(strstr((char *)saved,"plays=3\n")!=NULL);
  CHECK(strstr((char *)saved,"report_failures=1\n")!=NULL);
  CHECK(strstr((char *)saved,"min_slack_frames=0\n")!=NULL);
  FILE *out=fopen(argv[1],"wb"); CHECK(out!=NULL);
  CHECK(fwrite(saved,1,1024,out)==1024);
  CHECK(!fclose(out));
  /* Exercise carry in the 64-bit totals, separately from the saved fixture. */
  profile_init(); profile_start(48000,16,PLAY_TRIGGER,CV_FREE,2000);
  ticks += 0xfffffff0u; profile_poll();
  ticks += 100; profile_poll();
  profile_sample(PROF_SD,0xfffffff0u); profile_sample(PROF_SD,0xfffffff0u);
  profile_stop(10000); profile_idle(&fs,10250,1);
  CHECK(strstr((char *)saved,"wall_hi=1\n")!=NULL);
  CHECK(strstr((char *)saved,"sd,2,4294967264,1,4294967280,4294967280\n")!=NULL);
  puts("profile unit checks passed");
  return 0;
}
