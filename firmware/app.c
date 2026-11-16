/*
 * Encoder table and state transition traversal:
 * Copyright 2011 Ben Buxton. Licenced under the GNU GPL Version 3.
 * Adapted for this PIC32 application by Hamburger Wavy contributors,
 * 2026-11-16: direct GPIO polling, table representation and event handling.
 * Upstream: https://github.com/buxtronix/arduino/blob/master/libraries/Rotary/Rotary.cpp
 * See NOTICE and LICENSE for attribution and terms; no warranty.
 */
/* SPDX-License-Identifier: GPL-3.0-only */
#include "build.h"
#include "platform.h"
#include "profile.h"
static Sd card;
static Fat fs;
static Entry entries[ONE_MAX_FILES];
static File file;
static Wave wave;
static Ring ring;
static Settings settings;
static Envelope envelope;
static Recall recall;
static Ui ui;
static uint32_t ui_events, trigger_manual, restart_manual, manual_gate;
static uint32_t cache[ONE_CACHE_FRAMES * 2] __attribute__((aligned(16)));
static uint32_t audio[ONE_CACHE_FRAMES * 2] __attribute__((aligned(16)));
static uint32_t staging[ONE_HALF_FRAMES * 2];
static uint8_t raw[ONE_CACHE_FRAMES * 3];
static uint32_t mounted, ready, file_count, index, base_index, cached_bytes,
    trigger_pending, trigger_time, last_gate;
static uint32_t encoder_state, retry_time, last_pitch;
static uint32_t error_state, dma_last_offset, dma_last_time, random_state;
static uint32_t releasing, release_stop_block, restart_after_release,
    stream_frame, staging_ready;
static int32_t selection_delta;

static int looping(void) {
  return settings.play == PLAY_LOOP || settings.play == PLAY_COMBO ||
         settings.play == PLAY_RANDOM_LOOP;
}
static int fading(void) {
  return settings.play == PLAY_GATE || settings.play == PLAY_FADE;
}
static void queue_trigger(uint32_t manual) {
  if (!trigger_pending)
    trigger_time = board_millis();
  trigger_pending = 1;
  trigger_manual |= manual;
}
static void stop(void) {
  PROFILE(profile_stop(board_millis()));
  ring.running = 0;
  staging_ready = 0;
  board_audio_stop();
}
static void playback_error(void) {
  stop();
  releasing = release_stop_block = restart_after_release = restart_manual = 0;
  error_state = 1;
  board_led(1);
}
void app_background(void) {
  board_watchdog();
  PROFILE(profile_poll());
  if (ring.running) {
    uint32_t offset = board_audio_offset(), now = board_millis();
    if (offset != dma_last_offset) {
      dma_last_offset = offset;
      dma_last_time = now;
    } else if ((uint32_t)(now - dma_last_time) >= 1000u) {
      PROFILE(profile_error(PROF_DMA_ERROR));
      playback_error();
      error_state = 2;
    }
    if (ring_boundary(&ring, offset / (ONE_HALF_FRAMES * 8u)) < 0) {
      PROFILE(profile_error(PROF_RING_ERROR));
      playback_error();
    }
  }
  uint32_t pins = board_pins(), gate = (pins & 0x800u) == 0;
  if (gate && !last_gate && (pins & 0x200u) && !ui.button && !ui.menu &&
      !ui_events)
    queue_trigger(0);
  last_gate = gate;
  static const uint8_t transitions[7][4] = {
      {0, 2, 4, 0}, {3, 0, 1, 16}, {3, 2, 0, 0}, {3, 2, 1, 0},
      {6, 0, 4, 0}, {6, 5, 0, 32}, {6, 5, 4, 0}};
  uint32_t ab = ((pins >> 5) & 1u) | ((pins >> 6) & 2u);
  encoder_state = transitions[encoder_state & 15u][ab];
  int32_t step = (encoder_state & 32u) ? 1 : ((encoder_state & 16u) ? -1 : 0);
  uint32_t down = (pins & 0x200u) == 0;
  ui_events |= ui_poll(&ui, down, step, board_adc(), board_millis(), &settings);
  if (!ui.menu && !ui.boot_held &&
      (!ui.button || (uint32_t)(board_millis() - ui.press_time) < 400u) &&
      !(ui_events & (UI_ENTER | UI_COMMIT))) {
    if (step > 0 && selection_delta < 256)
      selection_delta++;
    if (step < 0 && selection_delta > -256)
      selection_delta--;
  }
}

static void update_pitch(void) {
  uint32_t pitch = board_adc() >> 2;
  board_pitch_mode(pitch, wave.rate, settings.cv);
  last_pitch = pitch;
}
static int select_file(uint32_t which) {
  stop();
  releasing = release_stop_block = restart_after_release = restart_manual = 0;
  ready = error_state = 0;
  index = which;
  if (file_open(&file, &fs, &entries[index]) || wave_open(&file, &wave) ||
      !wave.bytes)
    goto fail;
  uint32_t got;
  if (wave_read(&file, &wave, cache, ONE_CACHE_FRAMES, &got, raw, sizeof(raw)))
    goto fail;
  cached_bytes = got * (wave.bits / 8);
  update_pitch();
  ready = 1;
  board_led(0);
  return 0;
fail:
  if (!card.ready)
    mounted = 0;
  return -1;
}
static void begin_release(uint32_t restart) {
  if (!releasing) {
    releasing = 1;
    release_stop_block = 0;
    envelope.target = 0;
  }
  restart_after_release = restart;
}
static void trigger(void) {
  uint32_t manual = trigger_manual;
  trigger_pending = trigger_manual = 0;
  if (!ready) {
    mounted = 0;
    retry_time = board_millis() - 1000u;
    return;
  }
  /* Reopen and re-prime after an error, just as FILE browsing does. */
  if (error_state && select_file(index)) {
    playback_error();
    return;
  }
  if (fading() && ring.running) {
    restart_manual |= manual;
    begin_release(1);
    return;
  }
  uint32_t seek_random = settings.play == PLAY_RANDOM_LOOP && ring.running;
  random_state ^= board_millis() ^ (board_adc() << 16);
  uint32_t next = index;
  if (settings.cv == CV_SELECT)
    next = (base_index + cv_file_offset(board_adc(), file_count)) % file_count;
  else if (settings.play == PLAY_RANDOM_SEQUENCE)
    next = random_below(&random_state, file_count);
  else if (settings.play == PLAY_SEQUENCE || settings.play == PLAY_COMBO)
    next = (index + 1u) % file_count;
  if (next != index && select_file(next)) {
    playback_error();
    return;
  }
  if (settings.cv != CV_SELECT)
    base_index = index;
  stop();
  releasing = release_stop_block = restart_after_release = restart_manual = 0;
  error_state = 0;
  if (looping()) {
    uint32_t frame =
        seek_random ? random_below(&random_state, wave.bytes / (wave.bits / 8))
                    : 0;
    if (wave_seek_frame(&file, &wave, frame) ||
        wave_read_loop(&file, &wave, audio, ONE_CACHE_FRAMES, raw,
                       sizeof(raw))) {
      playback_error();
      if (!card.ready)
        mounted = 0;
      return;
    }
  } else {
    if (file_seek(&file, wave.offset + cached_bytes)) {
      playback_error();
      return;
    }
    wave.remaining = wave.bytes - cached_bytes;
    audio_copy(audio, cache, ONE_CACHE_FRAMES * 2u);
  }
  envelope.gain = fading() ? 0 : 32768;
  envelope.target = 32768;
  if (fading()) {
    envelope_apply(&envelope, audio, ONE_CACHE_FRAMES);
    envelope_tail(audio, ONE_CACHE_FRAMES, 0, wave.bytes / (wave.bits / 8));
  }
  stream_frame = ONE_CACHE_FRAMES;
  if (manual)
    (void)recall_save(&recall, &fs, &entries[index], index);
  if (ui.menu || (ui_events & (UI_ENTER | UI_COMMIT)))
    return;
  ring_start(&ring);
  board_audio_start(audio);
  PROFILE(profile_start(wave.rate, wave.bits, settings.play, settings.cv,
                        pitch_divider(last_pitch, wave.rate, settings.cv)));
  dma_last_offset = board_audio_offset();
  dma_last_time = board_millis();
  board_led(2);
}
static void mount_card(void) {
  stop();
  ready = mounted = 0;
  file_count = selection_delta = 0;
  releasing = release_stop_block = restart_after_release = restart_manual = 0;
  if (sd_init(&card, &board_sd_io) || fat_mount(&fs, sd_read, &card)) {
    error_state = 1;
    return;
  }
  fs.write = sd_write;
  (void)fat_build_info(&fs, ONE_BUILD_NUMBER);
  int config = settings_load(&fs, &settings);
  if (config == -1) {
    error_state = 1;
    return;
  }
  /* A malformed text file leaves all settings at their safe defaults. */
  int n = playlist_load(&fs, entries, ONE_MAX_FILES);
  if (n == -2)
    n = fat_list(&fs, entries, ONE_MAX_FILES);
  if (n <= 0) {
    error_state = 1;
    return;
  }
  file_count = (uint32_t)n;
  mounted = 1;
  uint32_t remembered = 0;
  int restored = recall_open(&recall, &fs, entries, file_count, &remembered);
  if (restored <= 0 && recall.saved_index == 0xffffffffu) {
    /* Read-only import of the stock status.bin's first-byte file index. */
    Entry legacy;
    File old;
    uint8_t value;
    uint32_t got;
    if (fat_find(&fs, "STATUS.BIN", &legacy) == 1 && legacy.size >= 1 &&
        legacy.size <= 2 && !file_open(&old, &fs, &legacy) &&
        !file_read(&old, &value, 1, &got) && got == 1 && value < file_count)
      remembered = value;
  }
  for (uint32_t attempt = 0; attempt <= file_count; attempt++) {
    uint32_t i = attempt ? attempt - 1u : remembered;
    if (!select_file(i)) {
      base_index = i;
      return;
    }
    if (!card.ready)
      break;
  }
  error_state = 1;
}
void app_init(void) {
  PROFILE(profile_init());
  memset(&ring, 0, sizeof(ring));
  settings_default(&settings);
  ready = mounted = trigger_pending = 0;
  error_state = selection_delta = 0;
  releasing = release_stop_block = restart_after_release = restart_manual = 0;
  encoder_state = 0;
  ui_events = trigger_manual = restart_manual = manual_gate = 0;
  memset(&recall, 0, sizeof(recall));
  recall.saved_index = 0xffffffffu;
  ui_init(&ui, (board_pins() & 0x200u) == 0, board_adc(), board_millis(),
          &settings);
  last_gate = (board_pins() & 0x800u) == 0;
  retry_time = board_millis() - 1000u;
  random_state = board_millis() ^ (board_adc() << 16) ^ 0x6d2b79f5u;
}
void app_tick(void) {
  app_background();
  uint32_t now = board_millis();
  if (!mounted) {
    board_led((now >> 8) & 1u);
    if (trigger_pending || now - retry_time >= 1000) {
      retry_time = now;
      mount_card();
    }
    return;
  }
  uint32_t events = ui_events;
  ui_events = 0;
  if (events & (UI_ENTER | UI_COMMIT)) {
    stop();
    trigger_pending = trigger_manual = manual_gate = 0;
    releasing = release_stop_block = restart_after_release = restart_manual = 0;
    selection_delta = 0;
  }
  if (events & UI_COMMIT) {
    settings.play = ui.play;
    settings.cv = ui.cv;
    if (ready)
      update_pitch();
    board_led(0);
  }
  if (ui.menu) {
    board_led(ui_led(&ui, now));
    return;
  }
  if (selection_delta) {
    manual_gate = 0;
    int32_t delta = selection_delta;
    selection_delta = 0;
    int32_t next = (int32_t)base_index + delta;
    while (next < 0)
      next += (int32_t)file_count;
    while (next >= (int32_t)file_count)
      next -= (int32_t)file_count;
    base_index = (uint32_t)next;
    if (select_file(base_index))
      error_state = 1;
  }
  if (events & UI_CLICK) {
    if (settings.play == PLAY_GATE && manual_gate) {
      manual_gate = 0;
      if (ring.running)
        begin_release(0);
    } else {
      if (settings.play == PLAY_GATE)
        manual_gate = 1;
      queue_trigger(1);
    }
  }
  uint32_t end_blocks =
      ready ? (wave.bytes / (wave.bits / 8) + ONE_HALF_FRAMES - 1) /
                  ONE_HALF_FRAMES
            : 0;
  if (ring.running &&
      ((release_stop_block && ring.blocks >= release_stop_block) ||
       (!looping() && ring.blocks >= end_blocks))) {
    uint32_t restart = restart_after_release, manual = restart_manual,
             gate_latched = manual_gate;
    restart_manual = manual_gate = 0;
    stop();
    releasing = release_stop_block = restart_after_release = restart_manual = 0;
    board_led(0);
    if (restart) {
      manual_gate = gate_latched;
      queue_trigger(manual);
    }
  }
  if (trigger_pending &&
      (settings.cv != CV_SELECT ||
       (uint32_t)(board_millis() - trigger_time) >= settings.delay_ms))
    trigger();
  if (settings.play == PLAY_GATE && ring.running && !last_gate &&
      !manual_gate && !releasing)
    begin_release(0);
  if (ready && settings.cv != CV_SELECT) {
    uint32_t pitch = board_adc() >> 2;
    uint32_t delta =
        pitch > last_pitch ? pitch - last_pitch : last_pitch - pitch;
    if (delta >= (settings.cv == CV_QUANTIZE ? 4u : 1u))
      update_pitch();
  }
  /* Read raw decoded audio ahead in every mode. Apply envelopes only when
   * publishing, so a release/retrigger can still affect prefetched data. */
  if (ring.running) {
    uint32_t half = (ring.pending & 1u) ? 0 : 1, got = ONE_HALF_FRAMES;
    if (!staging_ready) {
      PROFILE_BEGIN(read_ahead);
      int failed = looping() ? wave_read_loop(&file, &wave, staging,
                                              ONE_HALF_FRAMES, raw, sizeof(raw))
                             : wave_read(&file, &wave, staging, ONE_HALF_FRAMES,
                                         &got, raw, sizeof(raw));
      PROFILE_END(PROF_PREFETCH, read_ahead);
      if (failed) {
        PROFILE(profile_error(PROF_READ_ERROR));
        stop();
        mounted = 0;
        error_state = 1;
        retry_time = now;
        return;
      }
      staging_ready = 1;
    }
    app_background();
    /* Let the next tick process all new requests through the same CV delay. */
    if (selection_delta || !ring.running || !ring.pending)
      return;
    half = (ring.pending & 1u) ? 0 : 1;
    PROFILE_BEGIN(publish);
    PROFILE(profile_margin(half));
    if (!board_audio_commit_safe(half)) {
      PROFILE(profile_error(PROF_LATE_ERROR));
      playback_error();
      return;
    }
    if (fading()) {
      PROFILE_BEGIN(render);
      envelope_apply(&envelope, staging, ONE_HALF_FRAMES);
      envelope_tail(staging, ONE_HALF_FRAMES, stream_frame,
                    wave.bytes / (wave.bits / 8));
      PROFILE_END(PROF_ENVELOPE, render);
      /* Recheck after envelope work before touching the inactive DMA half. */
      if (!board_audio_commit_safe(half)) {
        PROFILE(profile_error(PROF_LATE_ERROR));
        playback_error();
        return;
      }
    }
    stream_frame += ONE_HALF_FRAMES;
    PROFILE_BEGIN(copy);
    audio_copy(audio + half * ONE_HALF_FRAMES * 2, staging, ONE_HALF_FRAMES * 2u);
    board_audio_publish();
    PROFILE_END(PROF_COPY, copy);
    PROFILE(profile_margin(half));
    PROFILE_PUBLISH(publish);
    if (ring_commit(&ring, half)) {
      PROFILE(profile_error(PROF_RING_ERROR));
      playback_error();
      return;
    }
    staging_ready = 0;
    if (releasing && !envelope.gain && !release_stop_block)
      release_stop_block = ring.blocks + 2;
  }
  if (error_state)
    board_led(error_state == 2 ? ((board_millis() / 250u) & 1u) : 1u);
  PROFILE(profile_idle(&fs, board_millis(),
      !ring.running && !trigger_pending && !ui.menu && !ui.button &&
      !ui_events && !selection_delta && (board_pins() & 0x200u)));
}
void standalone_main(void) {
  board_init();
  advertise_build(ONE_BUILD_NUMBER);
  app_init();
  for (;;)
    app_tick();
}
