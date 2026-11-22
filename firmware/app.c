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
#ifdef ONE_FLIP
/* Share existing scratch storage explicitly: fade work must not damage the
 * decoded staging half if a preparation attempt has to be deferred. */
static union {
  uint8_t bytes[ONE_CACHE_FRAMES * 3];
  uint32_t fade[ONE_CACHE_FRAMES * 3 / 4];
} raw_store __attribute__((aligned(16)));
#define raw raw_store.bytes
#else
static uint8_t raw[ONE_CACHE_FRAMES * 3];
#endif
static uint32_t mounted, ready, file_count, index, base_index, cached_bytes,
    trigger_pending, trigger_time, last_gate;
static uint32_t retry_time, last_pitch;
#ifdef ONE_TOLERANT_ENCODER
static Encoder encoder;
static uint32_t deferred_click;
#else
static uint32_t encoder_state;
#endif
static uint32_t error_state, dma_last_offset, dma_last_time, random_state;
static uint32_t releasing, release_stop_block, restart_after_release,
    stream_frame, staging_ready;
static int32_t selection_delta;

#ifdef ONE_FLIP
static FileIndex flip_index;
static uint32_t cv_zero, cv_reverse, cv_poll_time, cv_speed, tape_paused, tape_divider, chop_adc;
static int flip_mode(void) {
  return settings.play == PLAY_FLIP || settings.play == PLAY_CV_FLIP || settings.play == PLAY_TAPE || settings.play == PLAY_CHOP;
}
static uint32_t flip_playing, flip_restarting, flip_fading, flip_pending, flip_reverse,
    flip_requested, flip_cursor, flip_frame, flip_half[2], flip_staging_frame, flip_fade_half;
static void flip_cancel(void) {
  flip_playing = flip_restarting = flip_fading = flip_pending = tape_paused = 0;
}
static void flip_capture(void) {
  if (ring.running && !flip_fading) {
    uint32_t frame = board_audio_offset() / 8u;
    flip_frame = wave_frame_offset(flip_half[frame / ONE_HALF_FRAMES],
        frame % ONE_HALF_FRAMES, wave.bytes / (wave.bits / 8u), flip_reverse);
  }
}
static void flip_edge(void) {
  flip_capture();
  if (ring.running || flip_restarting || flip_fading) {
    flip_requested ^= 1u;
    flip_pending = 1;
  }
}
/* Below five ADC counts enters rest; eight leaves it. About 100 counts per volt
 * on the measured module, with no claim of a calibrated volts/speed scale. */
static void tape_sample(uint32_t adc) {
  int32_t delta=(int32_t)cv_zero-(int32_t)adc;
  uint32_t magnitude=(uint32_t)(delta<0 ? -delta : delta);
  if(magnitude<=4u || (!cv_speed && magnitude<8u)) cv_speed=0;
  else {cv_speed=magnitude>150u ? 150u : magnitude;cv_reverse=delta<0;}
}
static void cv_transport_poll(uint32_t adc) {
  if(settings.play==PLAY_TAPE) {
    tape_sample(adc);
    if(flip_playing && ((cv_speed && (tape_paused || cv_reverse!=flip_requested)) ||
        (!cv_speed && !tape_paused && !flip_pending))) {
      flip_capture();flip_requested=cv_reverse;flip_pending=1;
    }
  } else {
    int32_t delta=(int32_t)adc-(int32_t)cv_zero;
    if(delta>=5) cv_reverse=1;
    else if(delta<=-5) cv_reverse=0;
    if(flip_playing && cv_reverse!=flip_requested) flip_edge();
  }
}
/* Keep DMA/SPI running until the scheduled fade has actually reached zero. */
__attribute__((noinline))
static void flip_ramp(uint32_t *data, uint32_t down) {
  for (uint32_t i = 0; i < 512u; ++i) {
    uint32_t gain = (down ? 511u - i : i) * 64u;
    int64_t value = ((int64_t)((int32_t)data[i * 2u] / 256) * gain) >> 15;
    data[i * 2u] = data[i * 2u + 1u] = (uint32_t)value << 8;
  }
}
static int flip_fade_out(void) {
  uint32_t position=board_audio_offset();
  /* DMA can cross after app_background's poll. Wait for its boundary
   * bookkeeping before assigning a freshly inactive half. */
  if (position/(ONE_HALF_FRAMES*8u)!=ring.half) return 0;
  uint32_t remaining=ONE_HALF_FRAMES-(position/8u)%ONE_HALF_FRAMES;
  /* Reserve preparation time as well as the existing publication guard. */
  if (remaining<ONE_HALF_FRAMES/2u) return 0;
  uint32_t half=1u-position/(ONE_HALF_FRAMES*8u);
  uint32_t pending=ring.pending&(1u<<half);
  if (!board_audio_commit_safe(half) || (pending && !staging_ready)) return 0;
  uint32_t *fade=raw_store.fade;
  audio_copy(fade,pending ? staging : audio+half*ONE_HALF_FRAMES*2u,1024);
  PROFILE_BEGIN(flip_fade);
  flip_ramp(fade,1);
  PROFILE_END(PROF_ENVELOPE, flip_fade);
  /* No live or queued audio has changed yet. Missing this window is safe. */
  if (!board_audio_commit_safe(half)) return 0;
  audio_copy(audio+half*ONE_HALF_FRAMES*2u,fade,1024);
  for(uint32_t i=1024;i<ONE_HALF_FRAMES*2u;++i) audio[half*ONE_HALF_FRAMES*2u+i]=0;
  if (pending) flip_half[half]=flip_staging_frame;
  board_audio_publish();
  if (pending && ring_commit(&ring,half)) return -1;
  staging_ready=0;flip_fade_half=half;flip_fading=1;
  return 1;
}
static int flip_fill(uint32_t frame, uint32_t reverse) {
  flip_reverse = reverse; flip_cursor = frame;
  flip_half[0] = frame;
  flip_half[1] = wave_frame_offset(frame, ONE_HALF_FRAMES,
                                  wave.bytes / (wave.bits / 8u), reverse);
  return wave_read_direction(&file, &wave, &flip_index, &flip_cursor, reverse,
                             audio, ONE_CACHE_FRAMES, raw, sizeof(raw));
}
#endif

#ifdef ONE_COLOUR
/* Output frames/s are about 96e6 / divider. Effects run at or below 96 kHz
 * output; faster settings keep the unprocessed, measured streaming margin.
 * Above about 64 kHz only one stage runs: the filter takes priority. */
#define EFFECT_MIN_DIVIDER 1000u
#define EFFECT_BOTH_DIVIDER 1500u
#define EFFECT_ALL_DIVIDER 2000u
#define EFFECT_CHUNK 64u
static Colour colour;
static Filter filter;
static Mod mod;
static Phaser phaser;
static Flanger flanger;
static uint32_t flanger_live; /* the delay line holds something */
static void flanger_clear(void) {
  for (uint32_t i = 0; i < FLANGER_FRAMES; ++i) flanger.line[i] = 0;
  flanger.at = 0;
  flanger_live = 0;
}
static int32_t rate_cv; /* smoothed knob/CV reading, ADC counts in Q4 */
static uint32_t effect_divider;
static int cv_effect(void) {
  return settings.cv == CV_COLOUR || settings.cv == CV_FILTER || settings.cv == CV_RATE;
}
/* CV Flip, Tape and Chop already own the CV input: fixed amounts there. */
static int cv_effect_live(void) {
  return settings.play != PLAY_CV_FLIP && settings.play != PLAY_TAPE &&
         settings.play != PLAY_CHOP;
}
static uint32_t wah_phase;
static uint32_t filter_target(void) {
  uint32_t cutoff = settings.filter;
  if (settings.cv == CV_FILTER && cv_effect_live()) {
    cutoff = 255u - (board_adc() >> 2);
    if (!cutoff) cutoff = 1;
  }
  /* Wah sweeps around the set cutoff, or the middle if none is set. */
  if (settings.wah)
    cutoff = wah_cutoff(cutoff ? cutoff : 128u, settings.wah, mod_sine(wah_phase));
  return cutoff && effect_divider >= EFFECT_MIN_DIVIDER ?
         filter_coefficient(cutoff) : 0;
}
static void effects_reset(void) {
  colour.count = 0;
  filter.low = filter.band = 0;
  wah_phase = WAH_START_PHASE;
  filter.f = filter_target() << 8;
  for (uint32_t i = 0; i < 5u; ++i) phaser.z[i] = 0;
  phaser.a = phaser_coefficient(PHASER_CENTRE) << 8;
  if (flanger_live) flanger_clear();
  flanger.delay = flanger_delay(0);
  mod.phase = MOD_START_PHASE;
  rate_cv = (int32_t)board_adc() << 4;
}
/* Process in short chunks so controls and DMA bookkeeping keep being polled:
 * the FILE encoder loses steps when a quadrature state falls between polls. */
static void effects_block(uint32_t *data, uint32_t frames) {
  uint32_t drive = settings.drive, crush = settings.crush;
  if (settings.cv == CV_COLOUR) {
    if (!drive && !crush) drive = crush = 255u;
    if (cv_effect_live()) {
      uint32_t amount = 255u - (board_adc() >> 2);
      drive = drive * amount / 255u;
      crush = crush * amount / 255u;
    }
  }
  /* Tremolo or ring modulation: Ring wins if both are set. */
  uint32_t ring = settings.ring, depth = ring ? ring : settings.tremolo;
  uint32_t rate = settings.rate * 1023u / 255u;
  if (settings.cv == CV_RATE) {
    if (!depth && !settings.wah && !settings.phaser && !settings.flanger) depth = 255u;
    if (cv_effect_live()) {
      /* Hold still inside two ADC counts; otherwise close a quarter of the
       * distance each block, so a chosen rate does not wander with noise. */
      int32_t error = ((int32_t)board_adc() << 4) - rate_cv;
      if (error > 32 || error < -32) rate_cv += error / 4;
      rate = 1023u - (uint32_t)(rate_cv >> 4);
    }
  }
  /* The wah position at the end of this block is the filter's next target. */
  if (settings.wah || settings.phaser || settings.flanger) wah_phase += wah_increment(rate, wave.rate) * frames;
  uint32_t target = filter_target(), start = filter.f >> 8;
  /* Stages allowed by output rate: all four up to 48 kHz, three up to about
   * 64 kHz, one up to 96 kHz. Filter, phaser, modulation, colour in that
   * order of priority. */
  uint32_t budget = effect_divider >= EFFECT_ALL_DIVIDER ? 4u :
                    effect_divider >= EFFECT_BOTH_DIVIDER ? 3u :
                    effect_divider >= EFFECT_MIN_DIVIDER ? 1u : 0u;
  if (target) --budget; /* target is already zero when no stage may run */
  /* The flanger takes the phaser's place if both are set. */
  uint32_t swirl = (settings.phaser || settings.flanger) && budget;
  if (swirl) --budget;
  uint32_t wobble = depth && budget;
  if (wobble) --budget;
  uint32_t tone = (drive || crush) && budget;
  uint32_t sweep = phaser_coefficient(wah_cutoff(PHASER_CENTRE, PHASER_SWING, mod_sine(wah_phase)));
  uint32_t sweep_start = phaser.a >> 8;
  uint32_t jet = flanger_delay(mod_sine(wah_phase)), jet_start = flanger.delay;
  if (!swirl) {
    for (uint32_t i = 0; i < 5u; ++i) phaser.z[i] = 0;
    phaser.a = sweep << 8;
    if (flanger_live) flanger_clear();
    flanger.delay = jet;
  }
  uint32_t increment = wobble ? mod_increment(rate, ring, wave.rate) : 0;
  if (!target) filter.low = filter.band = 0, filter.f = 0;
  else if (!start) start = target, filter.f = target << 8;
  if (!tone && !target && !wobble && !swirl) return;
#ifdef ONE_PROFILE
  uint32_t tone_ticks = 0, filter_ticks = 0, mod_ticks = 0, swirl_ticks = 0, profiled = profile_running();
#endif
  for (uint32_t done = 0; done < frames;) {
    uint32_t count = frames - done < EFFECT_CHUNK ? frames - done : EFFECT_CHUNK;
#ifdef ONE_PROFILE
    uint32_t mark = board_ticks();
#endif
    if (tone) colour_apply(&colour, data + done * 2u, count, drive, crush);
#ifdef ONE_PROFILE
    uint32_t middle = board_ticks();
    tone_ticks += middle - mark;
#endif
    done += count;
    if (target) {
      /* Ramp the coefficient across the whole block, not within one chunk. */
      int32_t span = (int32_t)target - (int32_t)start;
      uint32_t step = (uint32_t)((int32_t)start + span * (int32_t)done / (int32_t)frames);
      filter_shape(&filter, data + (done - count) * 2u, count, step,
                   settings.resonance, settings.shape);
    }
#ifdef ONE_PROFILE
    uint32_t late = board_ticks();
    filter_ticks += late - middle;
#endif
    if (swirl && settings.flanger) {
      int32_t span = (int32_t)jet - (int32_t)jet_start;
      flanger_live = 1;
      flanger_apply(&flanger, data + (done - count) * 2u, count,
                    /* Q8 steps keep the product inside 32 bits. */
                    (uint32_t)((int32_t)jet_start +
                               (span / 256) * (int32_t)done / (int32_t)frames * 256),
                    settings.flanger);
    } else if (swirl) {
      int32_t span = (int32_t)sweep - (int32_t)sweep_start;
      phaser_apply(&phaser, data + (done - count) * 2u, count,
                   (uint32_t)((int32_t)sweep_start + span * (int32_t)done / (int32_t)frames),
                   settings.phaser);
    }
#ifdef ONE_PROFILE
    uint32_t later = board_ticks();
    swirl_ticks += later - late;
#endif
    if (wobble) mod_apply(&mod, data + (done - count) * 2u, count, increment, depth, ring);
#ifdef ONE_PROFILE
    mod_ticks += board_ticks() - later;
#endif
    if (done < frames) app_background();
  }
#ifdef ONE_PROFILE
  if (profiled && tone) profile_sample(PROF_COLOUR, tone_ticks);
  if (profiled && target) profile_sample(PROF_FILTER, filter_ticks);
  if (profiled && wobble) profile_sample(PROF_MOD, mod_ticks);
  if (profiled && swirl) profile_sample(settings.flanger ? PROF_FLANGER : PROF_PHASER, swirl_ticks);
#endif
}
#endif
#ifdef ONE_POLL_DECODE
/* Fade envelope in short chunks with a control poll between them. Each frame
 * still gets the envelope and then the end-of-file tail, as before. */
#define ENVELOPE_CHUNK 64u
static void envelope_block(uint32_t *data, uint32_t frames, uint32_t start, uint32_t tail) {
  uint32_t total = wave.bytes / (wave.bits / 8);
  for (uint32_t done = 0; done < frames;) {
    uint32_t n = frames - done < ENVELOPE_CHUNK ? frames - done : ENVELOPE_CHUNK;
    envelope_apply(&envelope, data + done * 2u, n);
    if (tail) envelope_tail(data + done * 2u, n, start + done, total);
    done += n;
    if (done < frames) app_background();
  }
}
#endif
static int looping(void) {
  return settings.play == PLAY_LOOP || settings.play == PLAY_COMBO ||
         settings.play == PLAY_RANDOM_LOOP
#ifdef ONE_FLIP
         || flip_mode()
#endif
         ;
}
static int fading(void) {
  return settings.play == PLAY_GATE || settings.play == PLAY_FADE
#ifdef ONE_FLIP
         || flip_mode()
#endif
         ;
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
#ifdef ONE_FLIP
  flip_cancel();
#endif
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
      !ui_events) {
#ifdef ONE_FLIP
    if(settings.play==PLAY_CHOP) {
      chop_adc=board_adc();
#ifdef ONE_TOLERANT_ENCODER
      PROFILE(profile_chop(chop_adc, ready && flip_playing));
#endif
      if(ready && flip_playing) {
        flip_frame=cv_frame_offset(chop_adc,wave.bytes/(wave.bits/8u));
        flip_requested=0;flip_pending=1;
      } else queue_trigger(0);
    }
    else if (settings.play == PLAY_CV_FLIP || settings.play == PLAY_TAPE) { /* CV owns transport. */ }
    else if (settings.play == PLAY_FLIP) flip_edge();
    else
#endif
      queue_trigger(0);
  }
#ifdef ONE_FLIP
  uint32_t cv_now=board_millis();
  if ((settings.play==PLAY_CV_FLIP || settings.play==PLAY_TAPE) && cv_now!=cv_poll_time &&
      (pins&0x200u) && !ui.button && !ui.menu && !ui_events) {
    cv_poll_time=cv_now;
    cv_transport_poll(board_adc());
  }
#endif
  last_gate = gate;
  uint32_t ab = ((pins >> 5) & 1u) | ((pins >> 6) & 2u);
#ifdef ONE_TOLERANT_ENCODER
  ab = encoder_filter(&encoder, ab, board_ticks());
  int32_t step = encoder_step(&encoder, ab);
  PROFILE(profile_encoder(encoder.steps, encoder.recovered, encoder.guessed));
  PROFILE(profile_debounce(encoder.raw_changes, encoder.bridged_max / 24u, encoder.hold_min / 24u));
#ifdef ONE_PROFILE
  static uint32_t logged_moved; /* zero-initialised RAM only: 0 is the detent */
  if ((ab ^ 3u) != logged_moved) {
    logged_moved = ab ^ 3u;
    profile_encoder_edge(ab, ring.running, step != 0);
  }
#endif
#else
  static const uint8_t transitions[7][4] = {
      {0, 2, 4, 0}, {3, 0, 1, 16}, {3, 2, 0, 0}, {3, 2, 1, 0},
      {6, 0, 4, 0}, {6, 5, 0, 32}, {6, 5, 4, 0}};
  encoder_state = transitions[encoder_state & 15u][ab];
  int32_t step = (encoder_state & 32u) ? 1 : ((encoder_state & 16u) ? -1 : 0);
#endif
  uint32_t down = (pins & 0x200u) == 0;
  ui_events |= ui_poll(&ui, down, step, board_adc(), board_millis(), &settings);
  if (!ui.menu && !ui.boot_held &&
      (!ui.button || (uint32_t)(board_millis() - ui.press_time) < 400u) &&
      !(ui_events & (UI_ENTER | UI_COMMIT))) {
    if (step > 0 && selection_delta < 256)
      selection_delta++;
    if (step < 0 && selection_delta > -256)
      selection_delta--;
#ifdef ONE_TOLERANT_ENCODER
    if (step) PROFILE(profile_count(0));
#endif
  }
}

static CvMode pitch_mode(void) {
#ifdef ONE_FLIP
  if (settings.play==PLAY_CV_FLIP || settings.play==PLAY_TAPE || settings.play==PLAY_CHOP) return CV_SELECT;
#endif
#ifdef ONE_COLOUR
  if (cv_effect()) return CV_SELECT;
#endif
  return settings.cv;
}
#ifdef ONE_PROFILE
static uint32_t effective_divider(void) {
#ifdef ONE_FLIP
  if(settings.play==PLAY_TAPE) return tape_divider;
#endif
  return pitch_divider(last_pitch,wave.rate,pitch_mode());
}
#endif
static void update_pitch(void) {
  uint32_t pitch = board_adc() >> 2;
#ifdef ONE_FLIP
  if(settings.play==PLAY_TAPE) {
    uint32_t speed=cv_speed ? cv_speed : 100u;
    tape_divider=(pitch_divider(0,wave.rate,CV_SELECT)*100u+speed-1u)/speed;
    board_pitch_fixed(tape_divider);
  } else
#endif
  board_pitch_mode(pitch, wave.rate, pitch_mode());
  last_pitch = pitch;
#ifdef ONE_COLOUR
  effect_divider = settings.play == PLAY_TAPE ? tape_divider :
      pitch_divider(pitch, wave.rate, pitch_mode());
#endif
}
static int select_file(uint32_t which) {
#ifdef ONE_FLIP
  flip_cancel();
  memset(&flip_index, 0, sizeof(flip_index));
#endif
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
#ifdef ONE_FLIP
  if (flip_mode() && file_index_build(&file, &flip_index)) goto fail;
#endif
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
#ifdef ONE_FLIP
static void arm_audio(uint32_t manual) {
#ifdef ONE_COLOUR
  /* Process the primed buffer at the settings about to play, before fades. */
  update_pitch();
  effects_reset();
  effects_block(audio, ONE_CACHE_FRAMES);
#endif
  envelope.gain = fading() ? 0 : 32768;
  envelope.target = 32768;
  if (flip_mode()) {
    flip_ramp(audio,0); envelope.gain=32768;
  } else if (fading()) {
#ifdef ONE_POLL_DECODE
    envelope_block(audio, ONE_CACHE_FRAMES, 0, 1);
#else
    envelope_apply(&envelope, audio, ONE_CACHE_FRAMES);
    envelope_tail(audio, ONE_CACHE_FRAMES, 0, wave.bytes / (wave.bits / 8));
#endif
  }
  stream_frame = ONE_CACHE_FRAMES;
  if (manual)
    (void)recall_save(&recall, &fs, &entries[index], index);
  if (ui.menu || (ui_events & (UI_ENTER | UI_COMMIT)))
    return;
  update_pitch();
  ring_start(&ring);
  board_audio_start(audio);
  PROFILE(profile_start(wave.rate, wave.bits, settings.play, settings.cv,
                        effective_divider()));
  dma_last_offset = board_audio_offset();
  dma_last_time = board_millis();
  board_led(2);
}
static void flip_resume(void) {
  if(settings.play==PLAY_TAPE && !cv_speed) {
    tape_paused=1;flip_pending=0;board_led(2);return;
  }
  tape_paused=0;
  uint32_t frame = flip_frame, reverse = flip_requested;
  flip_pending = 0; flip_restarting = 1;
  int failed = flip_fill(frame, reverse);
  flip_restarting = 0;
  if (failed) {
    PROFILE(profile_error(PROF_READ_ERROR));
#ifdef ONE_TOLERANT_ENCODER
    PROFILE(profile_read_fail(2, card.ready, frame, reverse));
#endif
    playback_error();
    if (!card.ready) mounted = 0;
    return;
  }
  /* Coalesce edges during refill without starting the obsolete direction. */
  if (flip_pending || selection_delta || ui.menu || ui_events) return;
  arm_audio(0);
}
#endif
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
  if (settings.cv == CV_SELECT
#ifdef ONE_FLIP
      && settings.play != PLAY_CV_FLIP && settings.play != PLAY_TAPE && settings.play != PLAY_CHOP
#endif
     )
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
#ifdef ONE_FLIP
  if (flip_mode()) {
    flip_playing = flip_restarting = 1;
    flip_pending=0;
    if(settings.play==PLAY_TAPE) tape_sample(board_adc());
    flip_requested=(settings.play==PLAY_CV_FLIP || settings.play==PLAY_TAPE) ? cv_reverse : 0;
    flip_frame=flip_requested ? wave.bytes/(wave.bits/8u)-1u : 0;
    if(settings.play==PLAY_CHOP)
      flip_frame=cv_frame_offset(manual ? board_adc() : chop_adc,wave.bytes/(wave.bits/8u));
    if(settings.play==PLAY_TAPE && !cv_speed) {
      flip_restarting=0;tape_paused=1;board_led(2);return;
    }
    int failed = flip_fill(flip_frame, flip_requested);
    flip_restarting = 0;
    if (failed) {
      PROFILE(profile_error(PROF_READ_ERROR));
#ifdef ONE_TOLERANT_ENCODER
      PROFILE(profile_read_fail(3, card.ready, flip_frame, flip_requested));
#endif
      playback_error(); if (!card.ready) mounted = 0; return;
    }
    if (flip_pending || selection_delta || ui.menu || ui_events) return;
  } else
#endif
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
#ifdef ONE_FLIP
  arm_audio(manual);
#else
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
                        effective_divider()));
  dma_last_offset = board_audio_offset();
  dma_last_time = board_millis();
  board_led(2);
#endif
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
#ifdef ONE_FLIP
  flip_cancel();
  cv_zero=327;cv_reverse=cv_speed=tape_paused=tape_divider=chop_adc=0;cv_poll_time=board_millis()-1u;
  memset(&flip_index, 0, sizeof(flip_index));
#endif
  PROFILE(profile_init());
#ifdef ONE_POLL_DECODE
  wave_poll = app_background;
#endif
  memset(&ring, 0, sizeof(ring));
  settings_default(&settings);
  ready = mounted = trigger_pending = 0;
  error_state = selection_delta = 0;
  releasing = release_stop_block = restart_after_release = restart_manual = 0;
#ifdef ONE_TOLERANT_ENCODER
  encoder_init(&encoder, 3);
  deferred_click = 0;
#else
  encoder_state = 0;
#endif
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
#ifdef ONE_TOLERANT_ENCODER
  /* The contact filter reports a detent a few milliseconds late. Hold a
   * click that lands inside that window so the browse is still seen first. */
  if (events & (UI_ENTER | UI_COMMIT)) deferred_click = 0;
  if (deferred_click && !encoder.timing) { events |= UI_CLICK; deferred_click = 0; }
  if ((events & UI_CLICK) && encoder.timing) { events &= ~(uint32_t)UI_CLICK; deferred_click = 1; }
#endif
  if (events & (UI_ENTER | UI_COMMIT)) {
#ifdef ONE_FLIP
    flip_cancel();
#endif
    stop();
    trigger_pending = trigger_manual = manual_gate = 0;
    releasing = release_stop_block = restart_after_release = restart_manual = 0;
    selection_delta = 0;
  }
  if (events & UI_COMMIT) {
#ifdef ONE_FLIP
    if ((settings.play==PLAY_CV_FLIP || settings.play==PLAY_TAPE) && ui.play==settings.play && !ui.file_edit && ui.kind==0) {
      cv_zero=board_adc();cv_reverse=cv_speed=0;
    }
#endif
    settings.play = ui.play;
    settings.cv = ui.cv;
#ifdef ONE_FLIP
    if (ready && flip_mode() && select_file(index)) error_state = 1;
#endif
    if (ready)
      update_pitch();
    board_led(0);
  }
  if (ui.menu) {
    board_led(ui_led(&ui, now));
    return;
  }
  if (selection_delta) {
#ifdef ONE_TOLERANT_ENCODER
    PROFILE(profile_count(1));
#endif
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
#ifdef ONE_FLIP
    if (flip_mode() && (flip_playing || trigger_pending)) {
      flip_cancel(); stop(); trigger_pending = trigger_manual = 0;
      releasing = release_stop_block = restart_after_release = restart_manual = 0;
      board_led(0);
    } else
#endif
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
#ifdef ONE_FLIP
  if(settings.play==PLAY_TAPE && ring.running && !flip_fading && cv_speed) {
    uint32_t div=(pitch_divider(0,wave.rate,CV_SELECT)*100u+cv_speed-1u)/cv_speed;
    if(div!=tape_divider) update_pitch();
  }
  if (flip_mode() && flip_playing && flip_pending) {
    if (flip_fading) {
      uint32_t position=board_audio_offset(),half=position/(ONE_HALF_FRAMES*8u);
      if ((half==flip_fade_half && position%(ONE_HALF_FRAMES*8u)>=512u*8u) ||
          (flip_fading==2 && half!=flip_fade_half)) {
        stop();flip_fading=0;flip_resume();
      } else if (half==flip_fade_half && flip_fading==1 && half==ring.half) {
        uint32_t other=half^1u;
        if (!board_audio_commit_safe(other)) { PROFILE(profile_error(PROF_LATE_ERROR));playback_error();return; }
        for(uint32_t i=0;i<ONE_HALF_FRAMES*2u;++i) audio[other*ONE_HALF_FRAMES*2u+i]=0;
        board_audio_publish();
        if ((ring.pending&(1u<<other)) && ring_commit(&ring,other)) { PROFILE(profile_error(PROF_RING_ERROR));playback_error();return; }
        flip_fading=2;
      }
    } else if (ring.running) {
      if (flip_fade_out()<0) {
        PROFILE(profile_error(PROF_LATE_ERROR));playback_error();return;
      }
    } else flip_resume();
  }
#endif
  if (trigger_pending &&
      (settings.cv != CV_SELECT ||
#ifdef ONE_FLIP
       settings.play==PLAY_CV_FLIP || settings.play==PLAY_TAPE || settings.play==PLAY_CHOP ||
#endif
       (uint32_t)(board_millis() - trigger_time) >= settings.delay_ms))
    trigger();
  if (settings.play == PLAY_GATE && ring.running && !last_gate &&
      !manual_gate && !releasing)
    begin_release(0);
  if (ready && settings.cv != CV_SELECT
#ifdef ONE_FLIP
      && settings.play != PLAY_CV_FLIP && settings.play != PLAY_TAPE && settings.play != PLAY_CHOP
#endif
#ifdef ONE_COLOUR
      && !cv_effect() /* fixed pitch: never reprogram the clock */
#endif
     ) {
    uint32_t pitch = board_adc() >> 2;
    uint32_t delta =
        pitch > last_pitch ? pitch - last_pitch : last_pitch - pitch;
    if (delta >= (settings.cv == CV_QUANTIZE ? 4u : 1u))
      update_pitch();
  }
  /* Read raw decoded audio ahead in every mode. Apply envelopes only when
   * publishing, so a release/retrigger can still affect prefetched data. */
  if (ring.running
#ifdef ONE_FLIP
      && !flip_fading
#endif
      ) {
    uint32_t half = (ring.pending & 1u) ? 0 : 1, got = ONE_HALF_FRAMES;
    if (!staging_ready) {
      PROFILE_BEGIN(read_ahead);
#ifdef ONE_FLIP
      flip_staging_frame = flip_cursor;
      int failed = flip_mode() ?
          wave_read_direction(&file, &wave, &flip_index, &flip_cursor, flip_reverse,
                              staging, ONE_HALF_FRAMES, raw, sizeof(raw)) :
          looping() ? wave_read_loop
#else
      int failed = looping() ? wave_read_loop
#endif
                              (&file, &wave, staging,
                                              ONE_HALF_FRAMES, raw, sizeof(raw))
                             : wave_read(&file, &wave, staging, ONE_HALF_FRAMES,
                                         &got, raw, sizeof(raw));
#ifdef ONE_COLOUR
      /* Before staging becomes visible: envelopes and Flip fades see it. */
      if (!failed) effects_block(staging, ONE_HALF_FRAMES);
#endif
      PROFILE_END(PROF_PREFETCH, read_ahead);
      if (failed) {
        PROFILE(profile_error(PROF_READ_ERROR));
#ifdef ONE_TOLERANT_ENCODER
        PROFILE(profile_read_fail(1, card.ready, flip_mode() ? flip_staging_frame : stream_frame,
                                  flip_mode() ? flip_reverse : 0));
#endif
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
    if (fading()
#ifdef ONE_FLIP
        && (!flip_mode() || envelope.gain != 32768u || envelope.target != 32768u)
#endif
#ifdef ONE_POLL_DECODE
        /* At steady full level the envelope only rewrites the samples. Skip
         * it unless a ramp is in progress or this half reaches the 256-frame
         * end-of-file tail. */
        && (envelope.gain != 32768u || envelope.target != 32768u ||
            (!flip_mode() && stream_frame + ONE_HALF_FRAMES + 256u >
                                 wave.bytes / (wave.bits / 8)))
#endif
        ) {
      PROFILE_BEGIN(render);
#ifdef ONE_POLL_DECODE
      envelope_block(staging, ONE_HALF_FRAMES, stream_frame,
#ifdef ONE_FLIP
                     !flip_mode()
#else
                     1
#endif
                     );
#else
      envelope_apply(&envelope, staging, ONE_HALF_FRAMES);
#ifdef ONE_FLIP
      if (!flip_mode())
#endif
      envelope_tail(staging, ONE_HALF_FRAMES, stream_frame,
                    wave.bytes / (wave.bits / 8));
#endif
      PROFILE_END(PROF_ENVELOPE, render);
#ifdef ONE_POLL_DECODE
      /* A poll inside the envelope can stop playback on a streaming error. */
      if (!ring.running) return;
#endif
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
#ifdef ONE_FLIP
    if (flip_mode()) flip_half[half] = flip_staging_frame;
#endif
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
      !ui_events && !selection_delta && (board_pins() & 0x200u)
#ifdef ONE_FLIP
      && !flip_playing && !flip_pending && !flip_restarting && !flip_fading
#endif
      ));
}
void standalone_main(void) {
  board_init();
#ifdef ONE_EXTENDED_APP
  extension_startup();
#endif
#ifdef ONE_FLASH_PROBE
  flash_probe_startup();
#endif
  advertise_build(ONE_BUILD_NUMBER);
  app_init();
  for (;;)
    app_tick();
}
