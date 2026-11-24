/* shruthi_core.c -- C port of the Shruthi-1 voice. See shruthi_core.h.
 *
 * Original code: Copyright 2009 Emilie Gillet (pichenettes/shruthi-1),
 * GPL-3.0-or-later. This port keeps that license.
 *
 * avrlib is not available here, so the small fixed-point helpers below are
 * re-implemented from their documented behaviour (U8Mix, U8U8MulShift8, ...).
 */
#include "shruthi_core.h"
#include <string.h>
#include "shruthi_tables.h"

/* ------------------------------------------------------------------------ */
/* avrlib stand-ins                                                         */
/* ------------------------------------------------------------------------ */
/* avrlib/random.h: 16-bit Galois LFSR, x^16 + x^14 + x^13 + x^11, initial state 0x21 */
static uint16_t g_rng = 0x21;
static void rng_update(void) { g_rng = (uint16_t)((g_rng >> 1) ^ (-(g_rng & 1) & 0xb400)); }
static uint8_t rng_state_msb(void) { return (uint8_t)(g_rng >> 8); }
static uint8_t rng_byte(void) { rng_update(); return rng_state_msb(); }
void sh_random_seed(uint16_t s) { g_rng = s; }

static inline uint8_t U8Mix(uint8_t a, uint8_t b, uint8_t bal) {
  return (uint8_t)((a * (255 - bal) + b * bal) >> 8);
}
static inline uint8_t U8Mix2(uint8_t a, uint8_t b, uint8_t ga, uint8_t gb) {
  return (uint8_t)((a * ga + b * gb) >> 8);
}
static inline uint16_t U8MixU16(uint8_t a, uint8_t b, uint8_t bal) {
  return (uint16_t)(a * (255 - bal) + b * bal);
}
static inline uint8_t U8U8MulShift8(uint8_t a, uint8_t b) { return (uint8_t)((a * b) >> 8); }
static inline uint16_t U8U8Mul(uint8_t a, uint8_t b) { return (uint16_t)(a * b); }
static inline int16_t S8U8Mul(int8_t a, uint8_t b) { return (int16_t)(a * b); }
static inline int16_t S8S8Mul(int8_t a, int8_t b) { return (int16_t)(a * b); }
static inline int8_t S8U8MulShift8(int8_t a, uint8_t b) { return (int8_t)((a * b) >> 8); }
static inline int8_t S8S8MulShift8(int8_t a, int8_t b) { return (int8_t)((a * b) >> 8); }
static inline uint8_t U8Swap4(uint8_t v) { return (uint8_t)((v << 4) | (v >> 4)); }
static inline uint8_t U8AddClip(uint8_t a, uint8_t b, uint8_t m) {
  unsigned s = (unsigned)a + b; return (uint8_t)(s > m ? m : s);
}
static inline int16_t S16ClipU14(int16_t x) { return x < 0 ? 0 : (x > 16383 ? 16383 : x); }
static inline int Clip(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

static inline uint8_t interp(const uint8_t *t, uint16_t phase) {
  return U8Mix(t[phase >> 8], t[(phase >> 8) + 1], (uint8_t)(phase & 0xff));
}
static inline uint8_t interp2(const uint8_t *a, const uint8_t *b, uint16_t phase,
                              uint8_t ga, uint8_t gb) {
  return U8Mix2(interp(a, phase), interp(b, phase), ga, gb);
}
static inline uint8_t read_sample(const uint8_t *t, uint16_t phase) { return t[phase >> 8]; }

/* resources.cc's waveform_table[] (ids 0/1 formant, 24+ non-wave data omitted) */
static const uint8_t *wf(unsigned id) {
  switch (id) {
    case 2: case 9: case 16: case 23: return wav_res_sine;
    case 3: return wav_res_bandlimited_square_0;
    case 4: return wav_res_bandlimited_square_1;
    case 5: return wav_res_bandlimited_square_2;
    case 6: return wav_res_bandlimited_square_3;
    case 7: return wav_res_bandlimited_square_4;
    case 8: return wav_res_bandlimited_square_5;
    case 10: return wav_res_bandlimited_saw_0;
    case 11: return wav_res_bandlimited_saw_1;
    case 12: return wav_res_bandlimited_saw_2;
    case 13: return wav_res_bandlimited_saw_3;
    case 14: return wav_res_bandlimited_saw_4;
    case 15: return wav_res_bandlimited_saw_5;
    case 17: case 18: case 19: return wav_res_bandlimited_triangle_0;
    case 20: return wav_res_bandlimited_triangle_3;
    case 21: return wav_res_bandlimited_triangle_4;
    case 22: return wav_res_bandlimited_triangle_5;
    default: return wav_res_sine;
  }
}
enum { ID_SQUARE_0 = 3, ID_SAW_0 = 10, ID_SAW_1 = 11, ID_TRI_0 = 17 };

/* ------------------------------------------------------------------------ */
/* Oscillator                                                               */
/* ------------------------------------------------------------------------ */
#define MASK24 0xFFFFFFu

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

void sh_osc_reset(sh_osc *o) { wr16(o->d + 3, (uint16_t)(rng_byte() + 1)); }

typedef struct {
  uint32_t phase;      /* 24 bit */
  uint32_t inc;
  uint8_t carry;
} phase_t;

static inline void phase_add(phase_t *p) {
  uint32_t n = p->phase + p->inc;
  p->carry = (uint8_t)((n >> 24) & 1);
  p->phase = n & MASK24;
}
static inline uint16_t pint(const phase_t *p) { return (uint16_t)(p->phase >> 8); }

static void render_silence(sh_osc *o, uint8_t *buf) { (void)o; memset(buf, 128, o->blk); }

static void render_pwm(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint8_t balance_index = U8Swap4((uint8_t)(o->note - 12));
  uint8_t gain_2 = balance_index & 0xf0, gain_1 = (uint8_t)~gain_2;
  uint8_t wave_index = balance_index & 0xf;
  const uint8_t *wave_1 = wf(ID_SAW_1 + wave_index);
  wave_index = U8AddClip(wave_index, 1, 5);
  const uint8_t *wave_2 = wf(ID_SAW_1 + wave_index);
  uint16_t shift = (uint16_t)((uint16_t)(o->parameter + 128) << 8);
  uint8_t scale = (uint8_t)(192 - (o->parameter >> 1));
  if (o->note > 64) {
    scale = U8Mix(scale, 102, (uint8_t)((o->note - 64) << 2));
    scale = U8Mix(scale, 102, (uint8_t)((o->note - 64) << 2));
  }
  phase_t p = { o->phase, (o->inc << 1) & MASK24, 0 };
  o->inc = p.inc;
  int size = o->blk;
  while (size--) {
    phase_add(&p);
    *sync_out++ = p.carry;
    *sync_out++ = 0;
    if (sync_in[0] || sync_in[1]) p.phase = 0;
    sync_in += 2;
    uint8_t a = interp2(wave_1, wave_2, pint(&p), gain_1, gain_2);
    a = U8U8MulShift8(a, scale);
    uint8_t b = interp2(wave_1, wave_2, (uint16_t)(pint(&p) + shift), gain_1, gain_2);
    b = U8U8MulShift8(b, scale);
    a = (uint8_t)(a - b + 128);
    *buf++ = a;
    *buf++ = a;
    size--;
  }
  o->phase = p.phase;
}

static void render_simple_wavetable(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint8_t balance_index = U8Swap4((uint8_t)(o->note - 12));
  uint8_t gain_2 = balance_index & 0xf0, gain_1 = (uint8_t)~gain_2;
  uint8_t wave_index = balance_index & 0xf;
  unsigned base = o->shape == SH_WAVE_SAW ? ID_SAW_0
                : (o->shape == SH_WAVE_SQUARE ? ID_SQUARE_0 : ID_TRI_0);
  const uint8_t *wave_1 = wf(base + wave_index);
  wave_index = U8AddClip(wave_index, 1, 6);
  const uint8_t *wave_2 = wf(base + wave_index);
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    uint8_t sample = interp2(wave_1, wave_2, pint(&p), gain_1, gain_2);
    if (sample < o->parameter) {
      if (o->shape == SH_WAVE_SAW) sample = (uint8_t)(sample + (o->parameter >> 1));
      else sample = o->parameter;
    }
    *buf++ = sample;
  }
  o->phase = p.phase;
}

static void render_interpolated_wavetable(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint8_t index = o->shape >= SH_WAVE_WAVETABLE_9 ? (uint8_t)(o->shape - SH_WAVE_WAVETABLE_9 + 8)
                                                  : (uint8_t)(o->shape - SH_WAVE_WAVETABLE_1);
  const uint8_t *def = wav_res_wavetables + U8U8Mul(index, 18);
  uint8_t num_steps = def[0];
  uint16_t pointer = U8U8Mul((uint8_t)(o->parameter << 1), num_steps);
  uint16_t wi1 = def[1 + (pointer >> 8)];
  uint16_t wi2 = def[2 + (pointer >> 8)];
  uint8_t gain = pointer & 0xff;
  const uint8_t *w1 = wav_res_waves + U8U8Mul((uint8_t)wi1, 129) + 0;
  const uint8_t *w2 = wav_res_waves + U8U8Mul((uint8_t)wi2, 129) + 0;
  /* U8U8Mul is only 8x8; wave indices can exceed 255 only if the table did. */
  w1 = wav_res_waves + (unsigned)wi1 * 129u;
  w2 = wav_res_waves + (unsigned)wi2 * 129u;
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    *buf++ = interp2(w1, w2, (uint16_t)(pint(&p) >> 1), (uint8_t)~gain, gain);
  }
  o->phase = p.phase;
}

static void render_sweeping_user(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint8_t bi = U8Swap4(o->parameter);
  uint8_t wave_index = bi & 0xf, gain_2 = bi & 0xf0, gain_1 = (uint8_t)~gain_2;
  uint16_t offset = (uint16_t)((wave_index << 7) + wave_index);
  const uint8_t *w1 = o->user_wt + offset, *w2 = w1;
  if (offset < 8 * 129 - 129) w2 += 129;
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    *buf++ = U8Mix2(interp(w1, (uint16_t)(pint(&p) >> 1)), interp(w2, (uint16_t)(pint(&p) >> 1)), gain_1, gain_2);
  }
  o->phase = p.phase;
}

static void render_cz_saw(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    uint8_t phi = (uint8_t)(pint(&p) >> 8);
    uint8_t clipped = phi < 0x20 ? (uint8_t)(phi << 3) : 0xff;
    *buf++ = read_sample(wav_res_sine, U8MixU16(phi, clipped, (uint8_t)(o->parameter << 1)));
  }
  o->phase = p.phase;
}

static void render_cz_pulse_reso(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint16_t pi_int = (uint16_t)(o->inc >> 8);
  uint16_t increment = (uint16_t)((pi_int + (((uint32_t)pi_int * o->parameter) >> 3)) << 1);
  uint16_t phase_2 = rd16(o->d);
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    if (p.carry) phase_2 = 32768;
    phase_2 = (uint16_t)(phase_2 + increment);
    uint8_t result = read_sample(wav_res_sine, phase_2);
    result >>= 1;
    result = (uint8_t)(result + 128);
    uint16_t pi = pint(&p);
    if (pi < 0x4000) *buf = result;
    else if (pi < 0x8000) *buf = U8U8MulShift8(result, (uint8_t)(~(int)(pi - 0x4000) >> 6));
    else *buf = 0;
    ++buf;
  }
  o->phase = p.phase;
  wr16(o->d, phase_2);
}

static void render_cz_reso(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint16_t pi_int = (uint16_t)(o->inc >> 8);
  uint16_t increment = (uint16_t)(pi_int + (((uint32_t)pi_int * o->parameter) >> 3));
  uint16_t phase_2 = rd16(o->d);
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    if (p.carry) phase_2 = 0;
    phase_2 = (uint16_t)(phase_2 + increment);
    uint8_t carrier = interp(wav_res_sine, phase_2);
    if (o->shape == SH_WAVE_CZ_SYNC) {
      *buf++ = pint(&p) < 0x8000 ? carrier : 128;
    } else {
      uint8_t window;
      if (o->shape == SH_WAVE_CZ_RESO) window = (uint8_t)~(pint(&p) >> 8);
      else window = (pint(&p) & 0x8000) ? (uint8_t)~(uint8_t)(pint(&p) >> 7) : (uint8_t)(pint(&p) >> 7);
      *buf++ = U8U8MulShift8(carrier, window);
    }
  }
  o->phase = p.phase;
  wr16(o->d, phase_2);
}

static void render_fm(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint8_t offset = o->secondary_parameter;
  if (offset < 12) offset = 0;
  else if (offset > 36) offset = 24;
  else offset = (uint8_t)(offset - 12);
  uint16_t multiplier = lut_res_fm_frequency_ratios[offset];
  uint16_t pi_int = (uint16_t)(o->inc >> 8);
  uint16_t increment = (uint16_t)(((int32_t)pi_int * multiplier) >> 8);
  uint8_t parameter = (uint8_t)(o->parameter << 1);
  o->phase &= ~0xffu;                         /* phase_.fractional = 0 */
  uint16_t phase_2 = rd16(o->d);
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    phase_2 = (uint16_t)(phase_2 + increment);
    uint8_t modulator = interp(wav_res_sine, phase_2);
    uint16_t modulation = (uint16_t)(modulator * parameter);
    *buf++ = interp(wav_res_sine, (uint16_t)(pint(&p) + modulation));
  }
  o->phase = p.phase;
  wr16(o->d, phase_2);
}

static void render_8bit_land(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    uint8_t x = o->parameter;
    *buf++ = (uint8_t)((((pint(&p) >> 8) ^ (x << 1)) & (uint8_t)(~x)) + (x >> 1));
  }
  o->phase = p.phase;
}

static void render_crushed_sine(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint8_t decimate = o->d[0], held = o->d[1];
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    ++decimate;
    if (o->parameter <= 63) {
      if (decimate >= o->parameter + 1) { decimate = 0; held = interp(wav_res_sine, pint(&p)); }
    } else {
      if (decimate >= 128 - o->parameter) { decimate = 0; held = interp(wav_res_bandlimited_triangle_0, pint(&p)); }
    }
    *buf++ = held;
  }
  o->phase = p.phase;
  o->d[0] = decimate; o->d[1] = held;
}

/* Vowel: formant synthesis. Control-rate update every 4th block; the amplitude loop runs 4 times over a
 * 3-element array in the original, so the 4th value lands in noise_modulation (d[15]) -- kept on purpose. */
static void render_vowel(sh_osc *o, uint8_t *buf) {
  uint8_t *d = o->d;
  d[16] = (uint8_t)((d[16] + 1) & 3);
  if (!d[16]) {
    uint8_t offset_1 = (uint8_t)(o->parameter >> 4);
    offset_1 = (uint8_t)U8U8Mul(offset_1, 7);
    uint8_t offset_2 = (uint8_t)(offset_1 + 7);
    uint8_t balance = o->parameter & 15;
    for (uint8_t i = 0; i < 3; ++i) {
      uint16_t inc = (uint16_t)(wav_res_vowel_data[(uint8_t)(offset_1 + i)] * (15 - balance) +
                                wav_res_vowel_data[(uint8_t)(offset_2 + i)] * balance);
      inc = (uint16_t)(inc << 3);
      if (o->fs_q16) inc = (uint16_t)(((uint32_t)inc * o->fs_q16) >> 16);   /* engine runs at another rate */
      wr16(d + 2 * i, inc);
    }
    for (uint8_t i = 0; i < 4; ++i) {
      uint8_t a = wav_res_vowel_data[(uint8_t)(offset_1 + 3 + i)];
      uint8_t b = wav_res_vowel_data[(uint8_t)(offset_2 + 3 + i)];
      d[12 + i] = (uint8_t)((a * (15 - balance) + b * balance) >> 4);
    }
  }
  uint16_t phase_int = (uint16_t)(o->phase >> 8);
  uint16_t inc_int = (uint16_t)(o->inc >> 8);
  int size = o->blk;
  while (size--) {
    int8_t result = 0;
    for (uint8_t i = 0; i < 3; ++i) {
      uint16_t fp = (uint16_t)(rd16(d + 6 + 2 * i) + rd16(d + 2 * i));
      wr16(d + 6 + 2 * i, fp);
      const uint8_t *t = i == 2 ? wav_res_formant_square : wav_res_formant_sine;
      result = (int8_t)(result + t[((fp >> 8) & 0xf0) | d[12 + i]]);
    }
    result = S8U8MulShift8(result, (uint8_t)~(phase_int >> 8));
    phase_int = (uint16_t)(phase_int + inc_int);
    int16_t phase_noise = (int16_t)((int8_t)rng_state_msb() * (int8_t)d[15]);
    if ((phase_int + phase_noise) < inc_int) { wr16(d + 6, 0); wr16(d + 8, 0); wr16(d + 10, 0); }
    uint8_t x = (uint8_t)(Clip(4 * result, -128, 127) + 128);
    *buf++ = x;
    *buf++ = x;
    size--;
  }
  o->phase = ((uint32_t)phase_int << 8) | (o->phase & 0xff);
}

static void render_dirty_pwm(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    *buf++ = (pint(&p) >> 8) < 127 + o->parameter ? 0 : 255;
  }
  o->phase = p.phase;
}

static void render_quad_saw_pad(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  uint16_t pi_int = (uint16_t)(o->inc >> 8);
  uint16_t phase_spread = (uint16_t)(((uint32_t)pi_int * o->parameter) >> 13);
  ++phase_spread;
  uint16_t phase_increment = pi_int, increments[3];
  for (int i = 0; i < 3; ++i) { phase_increment = (uint16_t)(phase_increment + phase_spread); increments[i] = phase_increment; }
  uint16_t q0 = rd16(o->d), q1 = rd16(o->d + 2), q2 = rd16(o->d + 4);
  phase_t p = { o->phase, o->inc, 0 };
  int size = o->blk;
  while (size--) {
    if (*sync_in++) p.phase = 0;
    phase_add(&p);
    *sync_out++ = p.carry;
    q0 = (uint16_t)(q0 + increments[0]);
    q1 = (uint16_t)(q1 + increments[1]);
    q2 = (uint16_t)(q2 + increments[2]);
    uint8_t value = (uint8_t)(pint(&p) >> 10);
    value = (uint8_t)(value + (q0 >> 10));
    value = (uint8_t)(value + (q1 >> 10));
    value = (uint8_t)(value + (q2 >> 10));
    *buf++ = value;
  }
  o->phase = p.phase;
  wr16(o->d, q0); wr16(o->d + 2, q1); wr16(o->d + 4, q2);
}

static void render_filtered_noise(sh_osc *o, const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buf) {
  (void)sync_out;  /* like the original: this oscillator writes no sync output */
  uint8_t lp = o->d[0];
  uint16_t rng_state = rd16(o->d + 1);
  uint16_t rng_reset_value = rd16(o->d + 3);
  if (rng_state == 0) ++rng_state;
  uint8_t coef = (uint8_t)(o->parameter << 2);
  if (coef <= 4) coef = 4;
  int size = o->blk;
  while (size--) {
    if (*sync_in++) rng_state = rng_reset_value;
    rng_state = (uint16_t)((rng_state >> 1) ^ (-(rng_state & 1) & 0xb400));
    uint8_t noise_sample = (uint8_t)(rng_state >> 8);
    lp = U8Mix(lp, noise_sample, coef);
    if (o->parameter >= 64) *buf++ = (uint8_t)(noise_sample - lp - 128);
    else *buf++ = lp;
  }
  o->d[0] = lp;
  wr16(o->d + 1, rng_state);
}

void sh_osc_render(sh_osc *o, uint8_t shape, uint8_t note, uint32_t increment,
                   const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buffer) {
  sh_osc_render_n(o, shape, note, increment, sync_in, sync_out, buffer, SH_BLOCK);
}

void sh_osc_render_n(sh_osc *o, uint8_t shape, uint8_t note, uint32_t increment,
                     const uint8_t *sync_in, uint8_t *sync_out, uint8_t *buffer, int n) {
  o->blk = (uint8_t)n;
  o->shape = shape; o->note = note; o->inc = increment & MASK24;
  if (shape == SH_WAVE_SQUARE) {
    if (o->parameter == 0) render_simple_wavetable(o, sync_in, sync_out, buffer);
    else render_pwm(o, sync_in, sync_out, buffer);
    return;
  }
  uint8_t index = shape > SH_WAVE_VOWEL ? SH_WAVE_WAVETABLE_1 : shape;
  switch (index) {
    case SH_WAVE_NONE: render_silence(o, buffer); break;
    case SH_WAVE_SAW: case SH_WAVE_TRIANGLE: render_simple_wavetable(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_CZ_SAW: render_cz_saw(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_CZ_RESO: case SH_WAVE_CZ_TRIANGLE: case SH_WAVE_CZ_SYNC:
      render_cz_reso(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_CZ_PULSE: render_cz_pulse_reso(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_QUAD_SAW_PAD: render_quad_saw_pad(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_FM: render_fm(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_WAVETABLE_USER: render_sweeping_user(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_8BITLAND: render_8bit_land(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_CRUSHED_SINE: render_crushed_sine(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_DIRTY_PWM: render_dirty_pwm(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_FILTERED_NOISE: render_filtered_noise(o, sync_in, sync_out, buffer); break;
    case SH_WAVE_VOWEL: render_vowel(o, buffer); break;
    default: /* WAVETABLE_1..8 */ render_interpolated_wavetable(o, sync_in, sync_out, buffer); break;
  }
}

/* ------------------------------------------------------------------------ */
/* Envelope                                                                 */
/* ------------------------------------------------------------------------ */
enum { ATTACK, DECAY, SUSTAIN, RELEASE, DEAD };

void sh_env_init(sh_env *e) {
  memset(e, 0, sizeof(*e));
  e->stage_target[ATTACK] = 255;
  e->stage_target[RELEASE] = 0;
  e->stage_target[DEAD] = 0;
  e->stage_phase_increment[SUSTAIN] = 0;
  e->stage_phase_increment[DEAD] = 0;
}
void sh_env_trigger(sh_env *e, uint8_t stage) {
  if (stage == DEAD) e->value = 0;
  e->a = (uint8_t)(e->value >> 8);
  e->b = e->stage_target[stage];
  e->stage = stage;
  e->phase = 0;
  e->phase_increment = e->stage_phase_increment[stage];
}
void sh_env_update(sh_env *e, uint8_t a, uint8_t d, uint8_t s, uint8_t r) {
  e->stage_phase_increment[ATTACK] = lut_res_env_portamento_increments[a & 127];
  e->stage_phase_increment[DECAY] = lut_res_env_portamento_increments[d & 127];
  e->stage_phase_increment[RELEASE] = lut_res_env_portamento_increments[r & 127];
  e->stage_target[DECAY] = (uint8_t)(s << 1);
  e->stage_target[SUSTAIN] = e->stage_target[DECAY];
}
uint8_t sh_env_render(sh_env *e) {
  e->phase = (uint16_t)(e->phase + e->phase_increment);
  if (e->phase < e->phase_increment) {
    e->value = U8MixU16(e->a, e->b, 255);
    sh_env_trigger(e, (uint8_t)(e->stage + 1));
  }
  if (e->phase_increment) {
    uint8_t step = interp(wav_res_env_expo, e->phase);
    e->value = U8MixU16(e->a, e->b, step);
  }
  return e->stage == SUSTAIN ? e->stage_target[DECAY] : (uint8_t)(e->value >> 8);
}

/* ------------------------------------------------------------------------ */
/* LFO (shared-phase / sequencer-synced features not ported)                */
/* ------------------------------------------------------------------------ */
void sh_lfo_reset_phase(sh_lfo *l) { l->phase = 0; l->cycle_complete = 1; l->running = 1; }
void sh_lfo_trigger(sh_lfo *l) { if (l->retrigger_mode) sh_lfo_reset_phase(l); l->intensity = 0; }
void sh_lfo_reset(sh_lfo *l) { sh_lfo_reset_phase(l); sh_lfo_trigger(l); }
void sh_lfo_update(sh_lfo *l, uint8_t shape, uint16_t inc, uint8_t attack, uint8_t mode) {
  l->shape = shape;
  if (inc) l->phase_increment = inc;
  l->intensity_increment = (uint16_t)(lut_res_env_portamento_increments[attack & 127] >> 1);
  l->retrigger_mode = mode;
}
uint8_t sh_lfo_render(sh_lfo *l) {
  uint8_t value;
  uint16_t i = l->intensity;
  if ((uint8_t)(i >> 8) != 0x3f) {
    i = (uint16_t)(i + l->intensity_increment);
    if ((uint8_t)(i >> 8) >= 0x40) i = 16383;
    l->intensity = i;
  }
  l->cycle_complete = l->phase < l->phase_increment;
  switch (l->shape) {
    case SH_LFO_RAMP: value = (uint8_t)(l->phase >> 8); break;
    case SH_LFO_S_H:
      if (l->cycle_complete) l->value = rng_byte();
      value = l->value; break;
    case SH_LFO_TRIANGLE:
      value = (l->phase & 0x8000) ? (uint8_t)(l->phase >> 7) : (uint8_t)~(uint8_t)(l->phase >> 7); break;
    case SH_LFO_SQUARE: value = (l->phase & 0x8000) ? 255 : 0; break;
    case SH_LFO_STEP_SEQUENCER: value = 0; break; /* needs the sequencer */
    default: {
      uint8_t off = (uint8_t)(l->shape - SH_LFO_WAVE_1);
      if (off == 0) off = 3; else off = (uint8_t)(off + 16);
      value = wav_res_waves[(unsigned)off * 129u + (l->phase >> 9)];
    } break;
  }
  l->phase = (uint16_t)(l->phase + l->phase_increment);
  if (l->retrigger_mode == SH_LFO_MODE_ONE_SHOT) {
    if (l->running) {
      if (l->phase < l->phase_increment) {
        l->running = 0;
        if (l->shape == SH_LFO_RAMP || l->shape == SH_LFO_TRIANGLE || l->shape == SH_LFO_SQUARE) value = 255;
        else if (l->shape == SH_LFO_STEP_SEQUENCER) value = 0;
        l->one_shot_value = value;
      }
    } else value = l->one_shot_value;
    value = l->shape == SH_LFO_STEP_SEQUENCER ? (uint8_t)(128 + (value >> 1)) : (uint8_t)(255 - (value >> 1));
  }
  return (uint8_t)(S8U8MulShift8((int8_t)(uint8_t)(value - 128), (uint8_t)(l->intensity >> 6)) + 128);
}

/* ------------------------------------------------------------------------ */
/* Patch                                                                    */
/* ------------------------------------------------------------------------ */
int sh_patch_load(sh_patch *p, const uint8_t raw[SH_PATCH_BYTES]) {
  memset(p, 0, sizeof(*p));
  for (int i = 8; i < 24; ++i) if (raw[i] >= 128) return 0;
  if (raw[91] != '!' && raw[91] != '%') return 0;
  const uint8_t *r = raw;
  for (int i = 0; i < 2; ++i) {
    p->osc[i].shape = r[i * 4]; p->osc[i].parameter = r[i * 4 + 1];
    p->osc[i].range = (int8_t)r[i * 4 + 2]; p->osc[i].option = r[i * 4 + 3];
  }
  p->mix_balance = r[8]; p->mix_sub_osc = r[9]; p->mix_noise = r[10]; p->mix_sub_osc_shape = r[11];
  p->filter_cutoff = r[12]; p->filter_resonance = r[13];
  p->filter_env = (int8_t)r[14]; p->filter_lfo = (int8_t)r[15];
  for (int i = 0; i < 2; ++i) {
    p->env[i].attack = r[16 + i * 4]; p->env[i].decay = r[17 + i * 4];
    p->env[i].sustain = r[18 + i * 4]; p->env[i].release = r[19 + i * 4];
    p->lfo[i].waveform = r[24 + i * 4]; p->lfo[i].rate = r[25 + i * 4];
    p->lfo[i].attack = r[26 + i * 4]; p->lfo[i].retrigger_mode = r[27 + i * 4];
  }
  for (int i = 0; i < SH_MOD_MATRIX_SIZE; ++i) {
    p->mod[i].source = r[32 + i * 3]; p->mod[i].destination = r[33 + i * 3];
    p->mod[i].amount = (int8_t)r[34 + i * 3];
  }
  memcpy(p->name, r + 68, 8); p->name[8] = 0;
  p->portamento = r[76] & 0x3f; p->legato = (r[76] & 0x40) ? 1 : 0;
  const uint8_t *od = r + 87;   /* op_data_ */
  p->filter_2_mode = r[86] & 0xf; p->filter_1_mode = r[86] >> 4;
  for (int i = 0; i < 2; ++i) {
    uint8_t d0 = od[i * 2], d1 = od[i * 2 + 1];
    p->ops[i].operands[0] = d0 & 0x3f;
    p->ops[i].operands[1] = (uint8_t)(d1 >> 3);
    p->ops[i].op = d1 & 0x7;
    if (d0 & 0x80) p->ops[i].op |= 0x8;
    if (d0 & 0x40) p->ops[i].operands[1] |= 0x20;
  }
  return 1;
}

/* ------------------------------------------------------------------------ */
/* Voice                                                                    */
/* ------------------------------------------------------------------------ */
static const int16_t kHighestNote = 128 * 128, kOctave = 12 * 128, kPitchTableStart = 116 * 128;
static const uint8_t eight_step_sequence[8] = { 4, 1, 8, 8, 2, 4, 8, 2 };
static const uint8_t four_step_sequence[4] = { 4, 1, 8, 2 };

/* NOTE: like the original, nothing here primes the envelope rates: they are computed inside the first
 * process_block. Call sh_voice_warm_up() (or run a few blocks) before the first note-on. */
static void update_modulation_rates(sh_voice *v);
void sh_voice_set_patch(sh_voice *v, const sh_patch *p) { v->patch = *p; update_modulation_rates(v); }
void sh_voice_warm_up(sh_voice *v, int blocks) { uint8_t scratch[SH_BLOCK]; while (blocks-- > 0) sh_voice_process_block(v, scratch); }

void sh_voice_init(sh_voice *v, const sh_patch *p) {
  memset(v, 0, sizeof(*v));
  v->patch = *p;
  v->pitch_value = 60 << 7;
  for (int i = 0; i < 2; ++i) { sh_env_init(&v->env[i]); v->osc[i].user_wt = v->user_wavetable; }
  memcpy(v->user_wavetable, wav_res_waves, 8 * 129);
  v->src[SH_SRC_VALUE_4] = 4; v->src[SH_SRC_VALUE_8] = 8;
  v->src[SH_SRC_VALUE_16] = 16; v->src[SH_SRC_VALUE_32] = 32;
  v->src[SH_SRC_PITCH_BEND] = 128; v->src[SH_SRC_WHEEL] = 0;
  v->src[SH_SRC_OFFSET] = 255; v->volume = 255;
  sh_voice_note_off(v);
  for (int i = 0; i < 2; ++i) sh_lfo_reset(&v->lfo[i]);
  update_modulation_rates(v);
}

static void trigger_all_env(sh_voice *v, uint8_t stage) { for (int i = 0; i < 2; ++i) sh_env_trigger(&v->env[i], stage); }

void sh_voice_note_on(sh_voice *v, uint8_t midi_note, uint8_t velocity, uint8_t portamento, int trigger) {
  uint16_t pitch = (uint16_t)(midi_note << 7);
  if (pitch == 0) return;
  v->pitch_target = (int16_t)pitch;
  if (trigger || v->pitch_value == 0) {
    for (int i = 0; i < 2; ++i) if (!v->disable_env_retrigger[i]) sh_env_trigger(&v->env[i], ATTACK);
    ++v->trigger_count;
    v->gate = 255;
    for (int i = 0; i < 2; ++i) sh_lfo_trigger(&v->lfo[i]);
    v->tg_counter = 255;
    v->src[SH_SRC_VELOCITY] = (uint8_t)(velocity << 1);
    v->src[SH_SRC_RANDOM] = rng_state_msb();
    sh_osc_reset(&v->osc[0]); sh_osc_reset(&v->osc[1]);
  }
  if (portamento) {
    int16_t delta = (int16_t)(v->pitch_target - v->pitch_value);
    int32_t increment = lut_res_env_portamento_increments[portamento & 127];
    v->pitch_increment = (int16_t)((delta * increment) >> 16);
    if (v->pitch_increment == 0) v->pitch_increment = delta < 0 ? -1 : 1;
  } else {
    v->pitch_value = v->pitch_target;
    v->pitch_increment = 1;
  }
}

void sh_voice_note_off(sh_voice *v) { v->gate = 0; trigger_all_env(v, RELEASE); }
void sh_voice_pitch_bend(sh_voice *v, uint16_t value14) { v->src[SH_SRC_PITCH_BEND] = (uint8_t)(value14 >> 6); }
void sh_voice_control_change(sh_voice *v, uint8_t cc, uint8_t value) {
  value = (uint8_t)(value << 1);
  switch (cc) {
    case 1: v->src[SH_SRC_WHEEL] = value; break;     /* mod wheel */
    case 7: v->volume = value; break;
    default: break;
  }
}

static void load_sources(sh_voice *v) {
  sh_patch *pt = &v->patch;
  v->src[SH_SRC_ENV_1] = sh_env_render(&v->env[0]);
  v->src[SH_SRC_ENV_2] = sh_env_render(&v->env[1]);
  v->src[SH_SRC_NOTE] = (uint8_t)(v->pitch_value >> 6);
  v->src[SH_SRC_GATE] = v->gate;
  v->src[SH_SRC_AUDIO] = v->buffer[0];
  for (int i = 0; i < 2; ++i) {
    if (!pt->ops[i].op) continue;
    uint8_t x = v->src[pt->ops[i].operands[0] % SH_SRC_LAST];
    uint8_t y = v->src[pt->ops[i].operands[1] % SH_SRC_LAST];
    uint8_t op = pt->ops[i].op;
    if (op <= 7) {
      uint8_t ops[8];
      if (x > y) { ops[3] = x; ops[6] = 255; ops[4] = y; ops[7] = 0; }
      else       { ops[3] = y; ops[6] = 0;   ops[4] = x; ops[7] = 255; }
      ops[1] = (uint8_t)((x >> 1) + (y >> 1));
      ops[2] = U8U8MulShift8(x, y);
      ops[5] = x ^ y;
      ops[0] = 0;
      v->src[SH_SRC_OP_1 + i] = ops[op];
    } else if (op == 8) {
      uint8_t mask = 0;
      while (y >>= 1) { mask >>= 1; mask |= 0x80; }
      v->src[SH_SRC_OP_1 + i] = x & mask;
    } else if (op == 9) {
      y >>= 2; ++y;
      uint16_t val = U8U8Mul((uint8_t)(256 - y), v->src[SH_SRC_OP_1 + i]);
      val = (uint16_t)(val + U8U8Mul(y, x));
      v->src[SH_SRC_OP_1 + i] = (uint8_t)(val >> 8);
    }
  }
  v->dstv[SH_DST_VCA] = v->volume;
  v->dst[SH_DST_FILTER_CUTOFF] = (int16_t)U8U8Mul(pt->filter_cutoff, 128);
  v->dst[SH_DST_PWM_1] = (int16_t)U8U8Mul(pt->osc[0].parameter, 128);
  v->dst[SH_DST_PWM_2] = (int16_t)U8U8Mul(pt->osc[1].parameter, 128);
  v->dst[SH_DST_VCO_1_2_COARSE] = v->dst[SH_DST_VCO_1_2_FINE] = 8192;
  v->dst[SH_DST_VCO_1] = v->dst[SH_DST_VCO_2] = 8192;
  v->dst[SH_DST_LFO_1] = v->dst[SH_DST_LFO_2] = 8192;
  v->dst[SH_DST_TRIGGER_ENV_1] = 0; v->dst[SH_DST_TRIGGER_ENV_2] = 0;
  v->dst[SH_DST_FILTER_RESONANCE] = (int16_t)(pt->filter_resonance << 8);
  v->dst[SH_DST_MIX_BALANCE] = (int16_t)(pt->mix_balance << 8);
  v->dst[SH_DST_MIX_NOISE] = (int16_t)(pt->mix_noise << 8);
  v->dst[SH_DST_MIX_SUB_OSC] = (int16_t)(pt->mix_sub_osc << 8);
  v->dst[SH_DST_ATTACK] = 8192;
  int16_t *ep = &v->dst[SH_DST_ATTACK_1];
  for (int i = 0; i < 2; ++i) {
    *ep++ = (int16_t)U8U8Mul(pt->env[i].attack, 128);
    *ep++ = (int16_t)U8U8Mul(pt->env[i].decay, 128);
    *ep++ = (int16_t)U8U8Mul(pt->env[i].sustain, 128);
    *ep++ = (int16_t)U8U8Mul(pt->env[i].release, 128);
  }
  v->dst[SH_DST_CV_1] = 0; v->dst[SH_DST_CV_2] = 0;
}

static void process_modulation_matrix(sh_voice *v) {
  for (int i = 0; i < 2; ++i) v->disable_env_retrigger[i] = 0;
  for (int i = 0; i < SH_MOD_MATRIX_SIZE; ++i) {
    int8_t amount = v->patch.mod[i].amount;
    if (!amount) continue;
    if (i == SH_MOD_MATRIX_SIZE - 1) amount = S8U8MulShift8(amount, v->src[SH_SRC_WHEEL]);
    uint8_t source = v->patch.mod[i].source % SH_SRC_LAST;
    uint8_t destination = v->patch.mod[i].destination % SH_DST_LAST;
    if (destination >= SH_DST_TRIGGER_ENV_1 && destination <= SH_DST_TRIGGER_ENV_2)
      v->disable_env_retrigger[destination - SH_DST_TRIGGER_ENV_1] = 1;
    uint8_t source_value = v->src[source];
    if (destination != SH_DST_VCA) {
      int16_t modulation = v->dst[destination];
      if (source <= SH_SRC_LFO_2 || source == SH_SRC_PITCH_BEND || source == SH_SRC_NOTE || source == SH_SRC_AUDIO)
        modulation = (int16_t)(modulation + S8S8Mul(amount, (int8_t)(uint8_t)(source_value + 128)));
      else
        modulation = (int16_t)(modulation + S8U8Mul(amount, source_value));
      v->dst[destination] = S16ClipU14(modulation);
    } else {
      if (amount < 0) { amount = (int8_t)-amount; source_value = (uint8_t)(255 - source_value); }
      if (amount != 63) source_value = U8Mix(255, source_value, (uint8_t)(amount << 2));
      v->dstv[SH_DST_VCA] = U8U8MulShift8(v->dstv[SH_DST_VCA], source_value);
    }
  }
}

static uint16_t lfo_phase_increment(const sh_voice *v, int i) {
  if (v->patch.lfo[i].rate < 16) return 0;      /* sequencer-synced: not ported */
  int rate = v->patch.lfo[i].rate + v->dstv[SH_DST_LFO_1 + i] - 32;
  rate = Clip(rate, 0, 127);
  return lut_res_lfo_increments[rate];
}

/* Part::UpdateLfoRate: the original only recomputes the LFO increment when the patch changes or the
 * LFO-FM destination value changes (previous_lfo_fm_), not every block. */
static void update_lfo_rate(sh_voice *v, int i) {
  sh_patch *pt = &v->patch;
  sh_lfo_update(&v->lfo[i], pt->lfo[i].waveform, lfo_phase_increment(v, i), pt->lfo[i].attack, pt->lfo[i].retrigger_mode);
}
static void update_modulation_rates(sh_voice *v) { update_lfo_rate(v, 0); update_lfo_rate(v, 1); }

static void update_destinations(sh_voice *v) {
  sh_patch *pt = &v->patch;
  uint16_t cutoff = (uint16_t)v->dst[SH_DST_FILTER_CUTOFF];
  if (pt->osc[0].option != SH_OP_DUO)
    cutoff = (uint16_t)S16ClipU14((int16_t)(cutoff + v->pitch_value - 8192));
  cutoff = (uint16_t)S16ClipU14((int16_t)(cutoff + S8U8Mul(pt->filter_env, v->src[SH_SRC_ENV_1])));
  cutoff = (uint16_t)S16ClipU14((int16_t)(cutoff + S8S8Mul(pt->filter_lfo, (int8_t)(uint8_t)(v->src[SH_SRC_LFO_2] + 128))));
  v->dstv[SH_DST_FILTER_CUTOFF] = (uint8_t)(cutoff >> 6);
  v->dstv[SH_DST_FILTER_RESONANCE] = (uint8_t)((uint16_t)v->dst[SH_DST_FILTER_RESONANCE] >> 6);
  v->dstv[SH_DST_CV_1] = (uint8_t)((uint16_t)v->dst[SH_DST_CV_1] >> 6);
  v->dstv[SH_DST_CV_2] = (uint8_t)((uint16_t)v->dst[SH_DST_CV_2] >> 6);
  v->dstv[SH_DST_LFO_1] = (uint8_t)(v->dst[SH_DST_LFO_1] >> 8);
  v->dstv[SH_DST_LFO_2] = (uint8_t)(v->dst[SH_DST_LFO_2] >> 8);
  for (int e = 0; e < 2; ++e) {
    int d = SH_DST_TRIGGER_ENV_1 + e;
    if (v->dst[d] > 6000) {
      if (!v->dstv[d]) sh_env_trigger(&v->env[e], ATTACK);
      v->dstv[d] = 255;
    } else v->dstv[d] = 0;
  }
  v->osc[0].parameter = (uint8_t)((uint16_t)v->dst[SH_DST_PWM_1] >> 7);
  v->osc[0].secondary_parameter = (uint8_t)(pt->osc[0].range + 24);
  v->osc[1].parameter = (uint8_t)((uint16_t)v->dst[SH_DST_PWM_2] >> 7);
  v->osc[1].secondary_parameter = (uint8_t)(pt->osc[1].range + 24);
  int8_t attack_mod = (int8_t)((((uint16_t)v->dst[SH_DST_ATTACK] >> 7) - 64) << 1);
  int16_t *ep = &v->dst[SH_DST_ATTACK_1];
  for (int i = 0; i < 2; ++i) {
    sh_env_update(&v->env[i], (uint8_t)Clip(((uint16_t)ep[0] >> 7) - attack_mod, 0, 127),
               (uint8_t)((uint16_t)ep[1] >> 7), (uint8_t)((uint16_t)ep[2] >> 7), (uint8_t)((uint16_t)ep[3] >> 7));
    ep += 4;
  }
}

static void render_oscillators(sh_voice *v) {
  sh_patch *pt = &v->patch;
  int16_t base_pitch = (int16_t)(v->pitch_value + v->pitch_increment);
  if ((v->pitch_increment > 0) ^ (base_pitch < v->pitch_target)) {
    base_pitch = v->pitch_target;
    v->pitch_increment = 0;
  }
  v->pitch_value = base_pitch;
  base_pitch = (int16_t)(base_pitch + ((v->dst[SH_DST_VCO_1_2_COARSE] - 8192) >> 4));
  base_pitch = (int16_t)(base_pitch + ((v->dst[SH_DST_VCO_1_2_FINE] - 8192) >> 7));
  base_pitch = (int16_t)(base_pitch + v->master_tuning);
  for (int i = 0; i < 2; ++i) {
    int16_t pitch = base_pitch;
    if (pt->osc[0].option == SH_OP_DUO && i == 0 && v->pitch_bass_note) {
      pitch = (int16_t)(pitch - v->pitch_value + v->pitch_bass_note);
    }
    int8_t range = 0;
    if (pt->osc[i].shape != SH_WAVE_FM) range = (int8_t)(range + pt->osc[i].range);
    range = (int8_t)(range + v->octave * 12);
    pitch = (int16_t)(pitch + S8U8Mul(range, 128));
    if (i == 1) pitch = (int16_t)(pitch + pt->osc[1].option);
    pitch = (int16_t)(pitch + ((v->dst[SH_DST_VCO_1 + i] - 8192) >> 2));
    while (pitch >= kHighestNote) pitch = (int16_t)(pitch - kOctave);
    int16_t ref_pitch = (int16_t)(pitch - kPitchTableStart);
    uint8_t num_shifts = 0;
    while (ref_pitch < 0) { ref_pitch = (int16_t)(ref_pitch + kOctave); ++num_shifts; }
    uint16_t idx = (uint16_t)((uint16_t)ref_pitch >> 4);
    uint8_t frac = (uint8_t)(ref_pitch << 4);
    uint16_t inc16 = lut_res_oscillator_increments[idx];
    uint16_t inc16n = lut_res_oscillator_increments[idx + 1];
    uint16_t integral = (uint16_t)(inc16 + (uint16_t)(((uint32_t)(uint16_t)(inc16n - inc16) * frac) >> 8));
    uint32_t increment = (uint32_t)integral << 8;
    while (num_shifts--) increment >>= 1;
    int8_t midi_note = (int8_t)((uint16_t)pitch >> 7);
    if (midi_note < 12) midi_note = 12;
    if (i == 0) {
      v->sub_inc = increment >> 1;
      sh_osc_render(&v->osc[0], pt->osc[0].shape, (uint8_t)midi_note, increment, v->no_sync, v->sync_state, v->buffer);
    } else {
      uint8_t shape = pt->osc[1].shape;
      if (pt->osc[0].option == SH_OP_DUO && !v->pitch_bass_note) shape = 0;
      sh_osc_render(&v->osc[1], shape, (uint8_t)midi_note, increment,
                    pt->osc[0].option == SH_OP_SYNC ? v->sync_state : v->no_sync, v->dummy_sync, v->osc2_buffer);
    }
  }
}

static void sub_render(sh_voice *v, uint8_t shape, uint8_t *buffer, uint8_t amount) {
  uint32_t increment = v->sub_inc;
  if (shape >= 3) { increment >>= 1; shape = (uint8_t)(shape - 3); }
  uint8_t pulse_width = shape == 0 ? 0x80 : 0x40;
  uint8_t mix_gain = (uint8_t)~amount;
  for (int n = 0; n < SH_BLOCK; ++n) {
    v->sub_phase = (v->sub_phase + increment) & MASK24;
    uint16_t pi = (uint16_t)(v->sub_phase >> 8);
    uint8_t val;
    if (shape != 1) val = (uint8_t)(pi >> 8) < pulse_width ? 0 : 255;
    else { uint8_t tri = (uint8_t)(pi >> 7); val = (pi & 0x8000) ? tri : (uint8_t)~tri; }
    buffer[n] = U8Mix2(buffer[n], val, mix_gain, amount);
  }
}

static uint8_t tg_fn(sh_voice *v, int which) {
  switch (which) {
    case 0: v->tg_gain = v->tg_counter; --v->tg_counter; return v->tg_counter < 32 ? 255 : 0;
    case 1: v->tg_gain = v->tg_counter; --v->tg_counter;
            v->tg_rng = (uint8_t)(v->tg_rng * 73 + v->tg_counter); return v->tg_rng;
    case 2: v->tg_decimate = (uint8_t)(v->tg_decimate + 2);
            if (v->tg_decimate >= 16) {
              v->tg_decimate = (uint8_t)(v->tg_decimate - 17);
              v->tg_rng = (uint8_t)(v->tg_rng * 73 + v->tg_counter);
              if (v->tg_decimate == 0) {
                --v->tg_counter;
                v->tg_gain = (v->tg_counter & 0x80) ? (uint8_t)~v->tg_counter : v->tg_counter;
              }
            }
            return v->tg_rng;
    case 3: --v->tg_counter; v->tg_gain = v->tg_counter >= 64 ? 255 : (uint8_t)(v->tg_counter << 2);
            return (uint8_t)(v->tg_counter * 57);
    default: --v->tg_counter; v->tg_gain = v->tg_counter > 0 ? 255 : 0; return 0;
  }
}
static void transient_render(sh_voice *v, uint8_t shape, uint8_t *buffer, uint8_t amount) {
  if (shape < SH_SUB_CLICK) return;
  if (shape > SH_SUB_POP) shape = SH_SUB_POP;
  int size = SH_BLOCK;
  while (v->tg_counter && size--) {
    uint8_t value = tg_fn(v, shape - SH_SUB_CLICK);
    uint8_t amp = U8U8MulShift8(v->tg_gain, amount);
    *buffer = U8Mix(*buffer, value, amp);
    ++buffer;
  }
}

/* The oscillator mixing modes of Voice::ProcessBlock (b = osc 1 in/out, b2 = osc 2). */
void sh_mix_oscillators(uint8_t *b, const uint8_t *b2, uint8_t op, uint8_t osc_1_gain, uint8_t osc_2_gain, int n) {
  switch (op) {
    case SH_OP_RING_MOD:
      for (int i = 0; i < n; ++i) {
        uint8_t rm = (uint8_t)(S8S8MulShift8((int8_t)(uint8_t)(b[i] + 128), (int8_t)(uint8_t)(b2[i] + 128)) + 128);
        b[i] = U8Mix2(b[i], rm, osc_1_gain, osc_2_gain);
      } break;
    case SH_OP_FUZZ:
      for (int i = 0; i < n; ++i) {
        b[i] >>= 1; b[i] = (uint8_t)(b[i] + (b2[i] >> 1));
        b[i] = U8Mix2(b[i], wav_res_distortion[b[i]], osc_1_gain, osc_2_gain);
      } break;
    case SH_OP_XOR:
      for (int i = 0; i < n; ++i) { b[i] ^= b2[i]; b[i] ^= osc_2_gain; } break;
    case SH_OP_FOLD:
      for (int i = 0; i < n; ++i) {
        b[i] >>= 1; b[i] = (uint8_t)(b[i] + (b2[i] >> 1));
        b[i] = U8Mix2(b[i], (uint8_t)(b[i] + 128), osc_1_gain, osc_2_gain);
      } break;
    case SH_OP_BITS: {
      uint8_t g = (uint8_t)(osc_2_gain >> 5);
      g = (uint8_t)(255 - ((1 << g) - 1));
      for (int i = 0; i < n; ++i) { b[i] >>= 1; b[i] = (uint8_t)(b[i] + (b2[i] >> 1)); b[i] &= g; }
    } break;
    default:
      for (int i = 0; i < n; ++i) b[i] = U8Mix2(b[i], b2[i], osc_1_gain, osc_2_gain);
      break;
  }
}

void sh_voice_process_block(sh_voice *v, uint8_t out[SH_BLOCK]) {
  sh_patch *pt = &v->patch;
  /* Part::ProcessBlock: LFOs first */
  for (int i = 0; i < 2; ++i) v->src[SH_SRC_LFO_1 + i] = sh_lfo_render(&v->lfo[i]);
  v->src[SH_SRC_NOISE] = rng_state_msb();
  for (int i = 0; i < 2; ++i)
    if (v->prev_lfo_fm[i] != v->dstv[SH_DST_LFO_1 + i]) {
      v->prev_lfo_fm[i] = v->dstv[SH_DST_LFO_1 + i];
      update_lfo_rate(v, i);
    }
  for (int j = 0; j < 2; ++j)
    if (v->lfo[j].cycle_complete && pt->lfo[j].retrigger_mode == SH_LFO_MODE_MASTER)
      sh_env_trigger(&v->env[j], ATTACK);

  load_sources(v);
  process_modulation_matrix(v);
  update_destinations(v);
  render_oscillators(v);

  v->out_cutoff = v->dstv[SH_DST_FILTER_CUTOFF];
  v->out_resonance = v->dstv[SH_DST_FILTER_RESONANCE];
  v->out_vca = v->dstv[SH_DST_VCA];
  if (v->out_vca == 0) { memset(out, 128, SH_BLOCK); return; }

  uint8_t op = pt->osc[0].option;
  uint8_t mask = 0xff;
  if (op == SH_OP_PING_PONG_2) mask = (v->trigger_count & 1) ? 0x0e : 0x0d;
  else if (op == SH_OP_PING_PONG_4) mask = four_step_sequence[v->trigger_count & 3];
  else if (op == SH_OP_PING_PONG_8) mask = eight_step_sequence[v->trigger_count & 7];
  else if (op == SH_OP_PING_PONG_SEQ) mask = (uint8_t)(v->src[SH_SRC_SEQ] >> 4);

  uint8_t osc_2_gain = (uint8_t)((uint16_t)v->dst[SH_DST_MIX_BALANCE] >> 6);
  uint8_t osc_1_gain = (uint8_t)~osc_2_gain;
  if (mask != 0xff && (mask & 3) != 3) {
    osc_2_gain = (mask & 1) ? 255 : 0;
    osc_1_gain = (mask & 2) ? 255 : 0;
  }
  uint8_t *b = v->buffer, *b2 = v->osc2_buffer;
  if (!osc_1_gain && !osc_2_gain) {
    memset(b, 128, SH_BLOCK);
  } else {
    sh_mix_oscillators(b, b2, op, osc_1_gain, osc_2_gain, SH_BLOCK);
  }
  uint8_t decimate = 1;
  if (op == SH_OP_CRUSH_4) decimate = 4; else if (op == SH_OP_CRUSH_8) decimate = 8;

  uint8_t sub_gain = (uint8_t)((uint16_t)v->dst[SH_DST_MIX_SUB_OSC] >> 7);
  if (mask != 0xff) {
    sub_gain = (mask & 4) ? sub_gain : 0;
    if ((mask & 3) == 0) sub_gain <<= 1;
  }
  if (pt->mix_sub_osc_shape < SH_SUB_CLICK) sub_render(v, pt->mix_sub_osc_shape, b, sub_gain);
  else { if (sub_gain < 128) sub_gain <<= 1; transient_render(v, pt->mix_sub_osc_shape, b, sub_gain); }

  if (decimate > 1) {
    uint8_t *q = b;
    for (int i = 0; i < SH_BLOCK; i += decimate) {
      uint8_t val = *q++;
      for (int j = 1; j < decimate; ++j) *q++ = val;
    }
  }
  uint8_t noise = rng_byte();
  uint8_t noise_gain = (uint8_t)(v->dst[SH_DST_MIX_NOISE] >> 8);
  if (mask != 0xff) {
    noise_gain = (mask & 8) ? noise_gain : 0;
    if (mask == 8) noise_gain <<= 1;
  }
  uint8_t mix_gain = (uint8_t)~noise_gain;
  for (int i = 0; i < SH_BLOCK; ++i) {
    noise = (uint8_t)(noise * 73 + 1);
    out[i] = U8Mix2(b[i], noise, mix_gain, noise_gain);
  }
}

/* ------------------------------------------------------------------------ */
/* Digital filter replacing the analog board: 4-pole ladder-style LP.        */
/* Cutoff CV 0..255 -> ~20 Hz .. ~12 kHz, exponential. Resonance 0..255 ->  */
/* feedback 0..~4.1 (self-oscillation around 4). These are tuned by ear     */
/* targets, not measured from hardware; see PORTING_NOTES.md.                */
/* ------------------------------------------------------------------------ */
#ifndef SH_NO_FLOAT
void sh_filter_init(sh_filter *f) { memset(f, 0, sizeof(*f)); }

static inline float fast_tanh(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

void sh_filter_process(sh_filter *f, uint8_t cutoff, uint8_t resonance,
                       const float *in, float *out, int n) {
  /* frequency in Hz */
  float exponent = (float)cutoff * (9.2f / 255.f);        /* ~ 10 octaves */
  float hz = 20.f * (float)__builtin_expf(exponent * 0.693147f);
  if (hz > 0.45f * SH_SAMPLE_RATE) hz = 0.45f * SH_SAMPLE_RATE;
  /* one-pole TPT coefficient g = tan(pi f / fs), small-angle safe rational */
  float w = 3.14159265f * hz / SH_SAMPLE_RATE;
  float g = w * (1.f + w * w * (0.3333333f + w * w * 0.1333333f));
  if (g > 2.f) g = 2.f;
  float G = g / (1.f + g);
  float k = (float)resonance * (4.1f / 255.f);
  for (int i = 0; i < n; ++i) {
    /* feedback from the last stage, delayed by one sample (cheap, stable
     * enough with the soft clip; 2x oversampling would reduce the error) */
    float u = in[i] - k * f->y;
    u = fast_tanh(u);
    float x = u;
    for (int s = 0; s < 4; ++s) {
      float v = (x - f->s[s]) * G;
      float lp = v + f->s[s];
      f->s[s] = lp + v;
      x = lp;
    }
    f->y = x;
    out[i] = x;
  }
}

/* ------------------------------------------------------------------------ */
/* Resampler: 4-point Catmull-Rom from 39.2 kHz to the output rate           */
/* ------------------------------------------------------------------------ */
void sh_resampler_init(sh_resampler *r, float out_rate) {
  memset(r, 0, sizeof(*r));
  r->step = SH_SAMPLE_RATE / out_rate;
  r->idx = SH_BLOCK;      /* empty: force a refill */
  r->frac = 1.f;
}

int sh_resampler_run(sh_resampler *r, float *out, int n,
                     void (*refill)(void *ctx, float *block), void *ctx) {
  for (int i = 0; i < n; ++i) {
    while (r->frac >= 1.f) {
      r->frac -= 1.f;
      if (r->idx >= SH_BLOCK) { refill(ctx, r->blk); r->idx = 0; }
      r->w[0] = r->w[1]; r->w[1] = r->w[2]; r->w[2] = r->w[3];
      r->w[3] = r->blk[r->idx++];
    }
    float t = r->frac, a = r->w[0], b = r->w[1], c = r->w[2], d = r->w[3];
    out[i] = b + 0.5f * t * (c - a + t * (2.f * a - 5.f * b + 4.f * c - d + t * (3.f * (b - c) + d - a)));
    r->frac += r->step;
  }
  return n;
}
#endif /* SH_NO_FLOAT */
