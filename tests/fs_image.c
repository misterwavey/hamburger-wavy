/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise state allocation on a regular disk image checked by the OS fsck. */
#include "../firmware/core.h"
#include <stdio.h>
#include <string.h>
static int read_block(void *ctx, uint32_t lba, uint8_t *p) {
  FILE *f = ctx;
  return fseek(f,(long)lba*512,SEEK_SET) || fread(p,1,512,f)!=512 ? -1 : 0;
}
static int write_block(void *ctx, uint32_t lba, const uint8_t *p) {
  FILE *f = ctx;
  return fseek(f,(long)lba*512,SEEK_SET) || fwrite(p,1,512,f)!=512 || fflush(f) ? -1 : 0;
}
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  FILE *f = fopen(argv[1],"r+b");
  if (!f) return 2;
  Fat fs; Recall state; Entry entries[4]; uint32_t index=99;
  if (fat_mount(&fs,read_block,f)) return 3;
  fs.write=write_block;
  if (fat_build_info(&fs,15)!=1 || fat_build_info(&fs,15)!=0 || fat_build_info(&fs,16)!=1) return 9;
  if (fat_list(&fs,entries,4)!=1 || recall_open(&state,&fs,entries,1,&index)!=0 || !state.available) return 4;
  if (recall_save(&state,&fs,entries,0)) return 5;
  uint8_t report[1024];
  memset(report,' ',sizeof(report));
  memcpy(report,"Hamburger Wavy profile 1\n",sizeof("Hamburger Wavy profile 1\n")-1);
  if (fat_profile_write(&fs,report)) return 11;
  if (fat_mount(&fs,read_block,f)) return 6;
  fs.write=write_block;
  if (recall_open(&state,&fs,entries,1,&index)!=1 || index!=0) return 7;
  if (fat_build_info(&fs,16)!=0) return 10;
  Entry entry; File file; uint32_t got; uint8_t readback[1024];
  if (fat_find(&fs,"ONEPROF.TXT",&entry)!=1 || entry.size!=1024 ||
      file_open(&file,&fs,&entry) || file_read(&file,readback,1024,&got) ||
      got!=1024 || memcmp(readback,report,1024)) return 12;
  return fclose(f) ? 8 : 0;
}
