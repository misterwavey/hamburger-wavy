/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef ONE_CV_DIAG
#include "cv_capture.h"
void cv_stats_init(CvRow *r) {
  memset(r,0,sizeof(*r)); r->minimum=1023;
}
int cv_stats_add(CvRow *r,uint32_t value) {
  if(value>1023 || r->n>=CV_WINDOW_MS) return -1;
  if(value<r->minimum) r->minimum=value;
  if(value>r->maximum) r->maximum=value;
  ++r->n;r->sum+=value;
  uint32_t old=r->ss_low;r->ss_low+=value*value;r->ss_high+=r->ss_low<old;
  r->at0+=value==0;r->at1023+=value==1023;
  return 0;
}
static void append(uint8_t *out,uint32_t *at,const char *s) {
  while(*s && *at<1009u) out[(*at)++]=(uint8_t)*s++;
}
static void number(uint8_t *out,uint32_t *at,uint32_t n) {
  char digits[10];uint32_t used=0;
  do {digits[used++]=(char)('0'+n%10u);n/=10u;} while(n);
  while(used && *at<1009u) out[(*at)++]=(uint8_t)digits[--used];
}
static void field(uint8_t *out,uint32_t *at,const char *name,uint32_t value) {
  append(out,at,name);number(out,at,value);append(out,at,"\n");
}
int cv_format_report(uint8_t out[1024],const CvRow *rows,uint32_t count,uint32_t build) {
  if(!count || count>CV_ROWS) return -1;
  memset(out,' ',1024);uint32_t at=0;
  append(out,&at,CV_REPORT_MAGIC);field(out,&at,"build=",build);
  field(out,&at,"adc_bits=",10);field(out,&at,"settle_ms=",CV_SETTLE_MS);
  field(out,&at,"window_ms=",CV_WINDOW_MS);field(out,&at,"poll_ms=",1);
  field(out,&at,"rows=",count);
  append(out,&at,"row,n,min,max,sum,ss_lo,ss_hi,at0,at1023,ms,missed\n");
  for(uint32_t i=0;i<count;++i) {
    const CvRow *r=rows+i;
    uint32_t fields[]={r->n,r->minimum,r->maximum,r->sum,r->ss_low,r->ss_high,
                       r->at0,r->at1023,r->elapsed,r->missed};
    number(out,&at,i+1);
    for(uint32_t j=0;j<10;++j) {append(out,&at,",");number(out,&at,fields[j]);}
    append(out,&at,"\n");
  }
  if(at>=1009u) return -1;
  uint32_t crc=0xffffffffu;
  for(uint32_t i=0;i<1009;++i) {
    crc^=out[i];
    for(uint32_t bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
  }
  crc=~crc;
  memcpy(out+1009,"crc32=",6);
  for(uint32_t i=0;i<8;++i) out[1015+i]=(uint8_t)"0123456789abcdef"[(crc>>(28u-i*4u))&15u];
  out[1023]='\n';return 0;
}
#endif
