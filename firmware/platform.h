/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ONE_PLATFORM_H
#define ONE_PLATFORM_H
#include "core.h"
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
void board_init(void);
#ifdef ONE_EXTENDED_APP
void extension_startup(void);
#endif
#ifdef ONE_FLASH_PROBE
void flash_probe_startup(void);
#endif
void board_watchdog(void);
void board_audio_publish(void);
uint32_t board_millis(void);
#ifdef ONE_TOLERANT_ENCODER
uint32_t board_ticks(void); /* free-running, 24 per microsecond */
#endif
uint32_t board_pins(void);
uint32_t board_adc(void);
void board_pitch(uint32_t, uint32_t);
void board_pitch_mode(uint32_t, uint32_t, CvMode);
#ifdef ONE_FLIP
void board_pitch_fixed(uint32_t);
#endif
void board_led(uint32_t);
void board_audio_stop(void);
void board_audio_start(const uint32_t *);
uint32_t board_audio_offset(void);
int board_audio_commit_safe(uint32_t);
extern const SdIo board_sd_io;
void app_background(void);
void app_tick(void);
void app_init(void);
void advertise_build(uint32_t);
void standalone_main(void) __attribute__((noreturn));
void standalone_fault(void) __attribute__((noreturn));
#endif
