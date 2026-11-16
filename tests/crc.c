/* SPDX-License-Identifier: GPL-3.0-only */
/* Test the actual private CRC update against the bitwise polynomial recurrence. */
#include "../firmware/sd.c"
#include <stdio.h>
static uint16_t reference(uint16_t crc, uint8_t byte) {
  uint32_t value=(uint32_t)crc ^ ((uint32_t)byte << 8);
  for (uint32_t i=0; i<8; ++i)
    value=((value<<1) ^ ((value&0x8000u)?0x1021u:0u))&0xffffu;
  return (uint16_t)value;
}
int main(void) {
  for (uint32_t state=0; state<65536; ++state)
    for (uint32_t byte=0; byte<256; ++byte)
      if (crc16_byte((uint16_t)state,(uint8_t)byte)!=reference((uint16_t)state,(uint8_t)byte)) {
        fprintf(stderr,"CRC mismatch: %u %u\n",state,byte); return 1;
      }
  puts("16777216 CRC state/byte combinations passed");
  return 0;
}
