/* shruthi_core.h -- portable C port of the Shruthi-1 voice (oscillators,
 * envelopes, LFOs, modulation matrix) plus a digital stand-in for the
 * analog filter board.
 *
 * Ported from Shruthi-1 by Emilie Gillet (pichenettes/shruthi-1).
 * Copyright 2009 Emilie Gillet; this port is distributed under the same
 * terms: GNU General Public License v3 or later.
 *
 * Differences from the original (see PORTING_NOTES.md):
 *  - plain C99, no avrlib / AVR assembly / PROGMEM
 *  - runs the oscillators at the original 39215.686 Hz, 40 samples per block;
 *    the host adapter resamples to the FM-1 output rate
 *  - the analog filter is replaced by sh_filter_* (floating point)
 *  - NOT ported yet: vowel oscillator, sequencer/arpeggiator, step-sequencer
 *    LFO, tempo-synced LFO rates (< 16), polyphonic voice allocation
 */
#ifndef SHRUTHI_CORE_H
#define SHRUTHI_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SH_BLOCK 40
#define SH_SAMPLE_RATE 39215.686f /* 20 MHz / 510 */
#define SH_PATCH_BYTES 92
#define SH_MOD_MATRIX_SIZE 12

/* ---- enums copied from shruthi/patch.h ---- */
enum {
  SH_WAVE_NONE, SH_WAVE_SAW, SH_WAVE_SQUARE, SH_WAVE_TRIANGLE, SH_WAVE_CZ_SAW,
  SH_WAVE_CZ_RESO, SH_WAVE_CZ_TRIANGLE, SH_WAVE_CZ_PULSE, SH_WAVE_CZ_SYNC,
  SH_WAVE_QUAD_SAW_PAD, SH_WAVE_FM, SH_WAVE_WAVETABLE_1, SH_WAVE_WAVETABLE_2,
  SH_WAVE_WAVETABLE_3, SH_WAVE_WAVETABLE_4, SH_WAVE_WAVETABLE_5,
  SH_WAVE_WAVETABLE_6, SH_WAVE_WAVETABLE_7, SH_WAVE_WAVETABLE_8,
  SH_WAVE_WAVETABLE_USER, SH_WAVE_8BITLAND, SH_WAVE_CRUSHED_SINE,
  SH_WAVE_DIRTY_PWM, SH_WAVE_FILTERED_NOISE, SH_WAVE_VOWEL,
  SH_WAVE_WAVETABLE_9, SH_WAVE_WAVETABLE_18 = SH_WAVE_WAVETABLE_9 - 1 + 10,
  SH_WAVE_LAST
};
enum { /* sub oscillator shapes */
  SH_SUB_SQUARE_1, SH_SUB_TRIANGLE_1, SH_SUB_PULSE_1, SH_SUB_SQUARE_2,
  SH_SUB_TRIANGLE_2, SH_SUB_PULSE_2, SH_SUB_CLICK, SH_SUB_GLITCH, SH_SUB_BLOW,
  SH_SUB_METALLIC, SH_SUB_POP, SH_SUB_LAST
};
enum { SH_LFO_TRIANGLE, SH_LFO_SQUARE, SH_LFO_S_H, SH_LFO_RAMP,
       SH_LFO_STEP_SEQUENCER, SH_LFO_WAVE_1 };
enum { SH_LFO_MODE_FREE, SH_LFO_MODE_SLAVE, SH_LFO_MODE_MASTER,
       SH_LFO_MODE_ONE_SHOT };
enum {
  SH_OP_SUM, SH_OP_SYNC, SH_OP_RING_MOD, SH_OP_XOR, SH_OP_FUZZ, SH_OP_CRUSH_4,
  SH_OP_CRUSH_8, SH_OP_FOLD, SH_OP_BITS, SH_OP_DUO, SH_OP_PING_PONG_2,
  SH_OP_PING_PONG_4, SH_OP_PING_PONG_8, SH_OP_PING_PONG_SEQ
};
enum {
  SH_SRC_LFO_1, SH_SRC_LFO_2, SH_SRC_SEQ, SH_SRC_SEQ_1, SH_SRC_SEQ_2,
  SH_SRC_STEP, SH_SRC_WHEEL, SH_SRC_AFTERTOUCH, SH_SRC_PITCH_BEND,
  SH_SRC_OFFSET, SH_SRC_CV_1, SH_SRC_CV_2, SH_SRC_CV_3, SH_SRC_CV_4,
  SH_SRC_CC_A, SH_SRC_CC_B, SH_SRC_CC_C, SH_SRC_CC_D, SH_SRC_NOISE,
  SH_SRC_ENV_1, SH_SRC_ENV_2, SH_SRC_VELOCITY, SH_SRC_RANDOM, SH_SRC_NOTE,
  SH_SRC_GATE, SH_SRC_AUDIO, SH_SRC_OP_1, SH_SRC_OP_2, SH_SRC_VALUE_4,
  SH_SRC_VALUE_8, SH_SRC_VALUE_16, SH_SRC_VALUE_32, SH_SRC_LAST
};
enum {
  SH_DST_FILTER_CUTOFF, SH_DST_VCA, SH_DST_PWM_1, SH_DST_PWM_2, SH_DST_VCO_1,
  SH_DST_VCO_2, SH_DST_VCO_1_2_COARSE, SH_DST_VCO_1_2_FINE, SH_DST_MIX_BALANCE,
  SH_DST_MIX_NOISE, SH_DST_MIX_SUB_OSC, SH_DST_FILTER_RESONANCE, SH_DST_CV_1,
  SH_DST_CV_2, SH_DST_ATTACK, SH_DST_LFO_1, SH_DST_LFO_2, SH_DST_TRIGGER_ENV_1,
  SH_DST_TRIGGER_ENV_2, SH_DST_ATTACK_1, SH_DST_DECAY_1, SH_DST_SUSTAIN_1,
  SH_DST_RELEASE_1, SH_DST_ATTACK_2, SH_DST_DECAY_2, SH_DST_SUSTAIN_2,
  SH_DST_RELEASE_2, SH_DST_LAST
};

/* ---- patch: byte layout identical to shruthi::Patch's saved data ---- */
typedef struct { uint8_t shape, parameter; int8_t range; uint8_t option; } sh_osc_settings;
typedef struct { uint8_t attack, decay, sustain, release; } sh_env_settings;
typedef struct { uint8_t waveform, rate, attack, retrigger_mode; } sh_lfo_settings;
typedef struct { uint8_t source, destination; int8_t amount; } sh_modulation;
typedef struct { uint8_t operands[2]; uint8_t op; } sh_operation;

typedef struct {
  sh_osc_settings osc[2];
  uint8_t mix_balance, mix_sub_osc, mix_noise, mix_sub_osc_shape;
  uint8_t filter_cutoff, filter_resonance;
  int8_t filter_env, filter_lfo;
  sh_env_settings env[2];
  sh_lfo_settings lfo[2];
  sh_modulation mod[SH_MOD_MATRIX_SIZE];
  char name[9];                 /* NUL terminated copy of the 8 byte name */
  uint8_t portamento, legato;   /* from extra_data_[0] */
  sh_operation ops[2];          /* decoded from op_data_ */
  uint8_t filter_1_mode, filter_2_mode;
} sh_patch;

/* Decode a 92-byte saved patch. Returns 1 on success, 0 if the buffer fails
 * the same sanity checks as Patch::CheckBuffer. */
int sh_patch_load(sh_patch *p, const uint8_t raw[SH_PATCH_BYTES]);

/* ---- internal building blocks (exposed for the bit-exact test) ---- */
typedef struct {
  uint32_t phase, inc;            /* 24 bit: 16.8 fixed point */
  uint8_t shape, parameter, secondary_parameter, note;
  /* Byte-for-byte image of the original's OscillatorState union (AVR layout, no padding):
   *   secondary_phase u16@0 | noise: lp u8@0, rng_state u16@1, rng_reset_value u16@3 |
   *   quad saw: phase[3] u16@0,2,4 | crushed sine: decimate u8@0, state u8@1 |
   *   vowel: formant_increment[3] u16@0, formant_phase[3] u16@6, amplitude[3] u8@12, noise_mod u8@15, update u8@16.
   * The overlap is deliberate: a note-on writes rng_reset_value, which perturbs the quad-saw phases. */
  uint8_t d[20];
  uint8_t blk;                  /* samples per render call (set by sh_osc_render*) */
  uint16_t fs_q16;              /* vowel formant scale for hosts not running at 39.2 kHz, Q16; 0 = none */
  const uint8_t *user_wt;       /* 8 * 129 + 1 bytes, for WAVETABLE_USER */
} sh_osc;

void sh_osc_reset(sh_osc *o);
/* Renders SH_BLOCK samples. sync_in / sync_out are SH_BLOCK byte arrays. */
void sh_osc_render(sh_osc *o, uint8_t shape, uint8_t note, uint32_t increment,
                   const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buffer);

/* Variable block length (n must be even; 32 for Felucca). Same state, same results as sh_osc_render. */
void sh_osc_render_n(sh_osc *o, uint8_t shape, uint8_t note, uint32_t increment,
                     const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buffer, int n);
/* The oscillator mixing modes (sum, ring, xor, fuzz, fold, bits). b: osc 1 in/out; b2: osc 2. */
void sh_mix_oscillators(uint8_t *b, const uint8_t *b2, uint8_t op, uint8_t osc_1_gain, uint8_t osc_2_gain, int n);

typedef struct {
  uint16_t stage_phase_increment[5];
  uint8_t stage_target[5];
  uint8_t stage, a, b;
  uint16_t phase_increment, phase, value;
} sh_env;

typedef struct {
  uint16_t phase_increment, intensity_increment, intensity, phase;
  uint8_t shape, retrigger_mode, value, one_shot_value;
  uint8_t cycle_complete, running;
} sh_lfo;

void sh_env_init(sh_env *e);
void sh_env_trigger(sh_env *e, uint8_t stage);
void sh_env_update(sh_env *e, uint8_t a, uint8_t d, uint8_t s, uint8_t r);
uint8_t sh_env_render(sh_env *e);
void sh_lfo_reset(sh_lfo *l);
void sh_lfo_trigger(sh_lfo *l);
void sh_lfo_update(sh_lfo *l, uint8_t shape, uint16_t inc, uint8_t attack, uint8_t mode);
uint8_t sh_lfo_render(sh_lfo *l);

/* avrlib's Random is a single global 16-bit generator; seed it for reproducible renders (default 0x21) */
void sh_random_seed(uint16_t seed);

/* ---- the voice ---- */
typedef struct {
  sh_patch patch;
  sh_osc osc[2];
  sh_env env[2];
  sh_lfo lfo[2];
  uint8_t disable_env_retrigger[2];
  uint8_t gate;
  int16_t pitch_increment, pitch_target, pitch_value, pitch_bass_note;
  uint8_t src[SH_SRC_LAST];
  uint8_t dstv[SH_DST_LAST];      /* scaled destinations, like the original */
  int16_t dst[SH_DST_LAST];
  uint8_t buffer[SH_BLOCK], osc2_buffer[SH_BLOCK];
  uint8_t sync_state[SH_BLOCK], no_sync[SH_BLOCK], dummy_sync[SH_BLOCK];
  uint8_t trigger_count, volume;
  uint8_t prev_lfo_fm[2];
  uint8_t tg_rng, tg_decimate, tg_gain, tg_counter; /* transient generator */
  uint32_t sub_phase, sub_inc;
  uint8_t user_wavetable[129 * 8 + 1];
  int16_t master_tuning; int8_t octave;
  /* outputs of the last block */
  uint8_t out_cutoff, out_resonance, out_vca;
} sh_voice;

void sh_voice_init(sh_voice *v, const sh_patch *p);
void sh_voice_set_patch(sh_voice *v, const sh_patch *p);
/* Runs blocks and discards them. The firmware always runs blocks continuously; envelope rates are only
 * computed inside process_block, so a note-on before the first block would stall the envelope. */
void sh_voice_warm_up(sh_voice *v, int blocks);
/* portamento: 0 = off, 1..127 = glide rate (the original takes it from system settings, not the patch) */
void sh_voice_note_on(sh_voice *v, uint8_t midi_note, uint8_t velocity, uint8_t portamento, int trigger);
void sh_voice_note_off(sh_voice *v);
void sh_voice_pitch_bend(sh_voice *v, uint16_t value14);
void sh_voice_control_change(sh_voice *v, uint8_t cc, uint8_t value);
/* Renders one block of unsigned 8-bit audio (128 = silence), updates
 * out_cutoff / out_resonance / out_vca for the digital filter + VCA. */
void sh_voice_process_block(sh_voice *v, uint8_t out[SH_BLOCK]);

#ifndef SH_NO_FLOAT   /* floating-point helpers for the PC / float-capable hosts; the Felucca engine omits them */
/* ---- digital filter standing in for the analog board (4-pole LP) ---- */
typedef struct { float s[4]; float y; } sh_filter;
void sh_filter_init(sh_filter *f);
/* cutoff, resonance: 0..255 as produced by the voice; in/out: -1..+1 */
void sh_filter_process(sh_filter *f, uint8_t cutoff, uint8_t resonance,
                       const float *in, float *out, int n);

/* ---- linear-interpolating resampler from 39.2 kHz to an output rate ---- */
typedef struct { float step, frac, w[4], blk[SH_BLOCK]; int idx; } sh_resampler;
void sh_resampler_init(sh_resampler *r, float out_rate);
/* Pulls output samples; calls refill(ctx, block) for each new SH_BLOCK input
 * block. Returns n. */
int sh_resampler_run(sh_resampler *r, float *out, int n,
                     void (*refill)(void *ctx, float *block), void *ctx);
#endif /* SH_NO_FLOAT */

#ifdef __cplusplus
}
#endif
#endif
