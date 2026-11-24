/* SPDX-License-Identifier: GPL-3.0-or-later
 * eng_shruthi.c -- a Felucca engine built from the Shruthi-1 sound source.
 *
 * The oscillators, wavetables, the oscillator-mixing operators and the vowel / CZ / FM shapes are the
 * Shruthi-1 code (Copyright 2009 Emilie Gillet, pichenettes/shruthi-1, GPL-3.0-or-later) via shruthi_core.c,
 * which a host test shows bit-identical to the original (see PORTING_NOTES.md). Because that code is GPL, a
 * firmware that includes this file is GPL too. The engine glue below follows Felucca-Engines' interface
 * (MIT, Leo Kuroshita / Hügelton Instruments).
 *
 * What is and is not here:
 *   - here: two Shruthi oscillators (23 shapes), an interval, the operator (sum / sync / ring / xor / fuzz /
 *     fold / bits), and a 4-pole low-pass with resonance that stands in for Shruthi's analog filter board
 *   - Felucca provides what Shruthi's own firmware did around the oscillators: the envelopes, LFOs, the
 *     modulation matrix, the sequencer and the arpeggiator (host side, via vmod_t and the preset's ENV amounts)
 *   - not here: Shruthi's sub oscillator, noise mix and transient generator (no free parameter slot), the
 *     step-sequencer / tempo-synced LFOs (the host has its own)
 *
 * Include after dsp.c, in the single translation unit Felucca-Engines describes. Integer only: shruthi_core.c
 * is built with SH_NO_FLOAT here, so none of its floating-point code is compiled in.
 * The oscillators are rate-agnostic phase accumulators, so they run directly at FS in 32-sample blocks: the
 * 24-bit increment is simply Felucca's 32-bit one >> 8. Only the vowel formants (tuned per sample at Shruthi's
 * 39.2 kHz) need a rate factor, set at note-on.
 */
#ifndef ENG_SHRUTHI_C
#define ENG_SHRUTHI_C

#define SH_NO_FLOAT 1
#include "shruthi_core.c"

/* The vowel formant increments were designed per sample at 39215.686 Hz; at FS they scale by 39215.686 / FS (Q16). */
#define SHR_VOWEL_Q16 ((uint32_t)((39215686ull * 65536ull) / ((uint64_t)FS * 1000ull)))

/* ---------------------------------------------------------------- parameters */
static const char *const N_SHR_WAVE[] = {
    "OFF", "SAW", "SQR", "TRI", "CZSAW", "CZRES", "CZTRI", "CZPLS", "CZSYN", "QSAW", "FM",
    "WT1", "WT2", "WT3", "WT4", "WT5", "WT6", "WT7", "WT8", "8BIT", "CRSH", "DPWM", "NOIS", "VOWL"
};
static const uint8_t SHR_SHAPE[24] = {          /* names above -> Shruthi shape ids */
    SH_WAVE_NONE, SH_WAVE_SAW, SH_WAVE_SQUARE, SH_WAVE_TRIANGLE, SH_WAVE_CZ_SAW, SH_WAVE_CZ_RESO,
    SH_WAVE_CZ_TRIANGLE, SH_WAVE_CZ_PULSE, SH_WAVE_CZ_SYNC, SH_WAVE_QUAD_SAW_PAD, SH_WAVE_FM,
    SH_WAVE_WAVETABLE_1, SH_WAVE_WAVETABLE_2, SH_WAVE_WAVETABLE_3, SH_WAVE_WAVETABLE_4, SH_WAVE_WAVETABLE_5,
    SH_WAVE_WAVETABLE_6, SH_WAVE_WAVETABLE_7, SH_WAVE_WAVETABLE_8, SH_WAVE_8BITLAND, SH_WAVE_CRUSHED_SINE,
    SH_WAVE_DIRTY_PWM, SH_WAVE_FILTERED_NOISE, SH_WAVE_VOWEL
};
static const char *const N_SHR_OP[] = {"SUM", "SYNC", "RING", "XOR", "FUZZ", "FOLD", "BITS"};
static const uint8_t SHR_OPID[7] = {
    SH_OP_SUM, SH_OP_SYNC, SH_OP_RING_MOD, SH_OP_XOR, SH_OP_FUZZ, SH_OP_FOLD, SH_OP_BITS
};

/* P_E0 WAVE1  P_E1 WAVE2  P_E2 PARM  P_E3 INTV  P_E4 MIX  P_E5 OP  P_E6 CUT  P_E7 RES
 * PARM is the shape parameter (PWM width, CZ / FM index, wavetable position, vowel, ...) of both oscillators;
 * the host's SHAPE modulation pushes them in opposite directions, which makes a detuned-PWM chorus. */

#define SHR_BLK 32                              /* = CTL */
#if SHR_BLK != CTL
#error "eng_shruthi.c renders CTL-sample blocks of 32"
#endif

typedef struct { sh_osc o[2]; } shr_voice_t;
static shr_voice_t shr_v[NPART][NVOICE];        /* the two oscillators' state, per part and voice (like WHEEL) */

static void shr_note_on(struct track *tt, voice_t *v)
{
    track_t *t = (track_t *)tt;
    shr_voice_t *sv = &shr_v[t - trk][v - t->v];
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        sh_osc_reset(&sv->o[k]);                /* Oscillator::Reset(): a new noise / vowel start value */
        sv->o[k].fs_q16 = (uint16_t)SHR_VOWEL_Q16;
        sv->o[k].user_wt = wav_res_waves;       /* the user wavetable slot is the first 8 factory waves */
    }
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;  /* filter */
}

static inline uint32_t shr_inc24(uint32_t inc)        /* Felucca's 32-bit phase increment -> Shruthi's 24-bit one */
{
    return inc >> 8;
}

static __attribute__((noinline)) void shr_render(struct track *tt, voice_t *v, int32_t *out, uint32_t n,
                                                 const vmod_t *m)
{
    track_t *t = (track_t *)tt;
    const int16_t *p = t->p;
    shr_voice_t *sv = &shr_v[t - trk][v - t->v];
    uint8_t b1[SHR_BLK], b2[SHR_BLK], sync1[SHR_BLK], sync2[SHR_BLK], nosync[SHR_BLK];
    uint8_t shape1 = SHR_SHAPE[clamp(p[P_E0], 0, 23)], shape2 = SHR_SHAPE[clamp(p[P_E1], 0, 23)];
    int32_t off = (m->shape - (64 << 8)) >> 8;                       /* +-64 */
    uint8_t prm1 = (uint8_t)clamp(p[P_E2] + off, 0, 127), prm2 = (uint8_t)clamp(p[P_E2] - off, 0, 127);
    int32_t intv = clamp(p[P_E3], -24, 24);
    uint32_t i, inc1, inc2;
    int32_t note1 = clamp(m->pitch16 >> 4, 12, 127), note2;
    uint8_t g2 = (uint8_t)clamp(p[P_E4] * 2, 0, 254), g1 = (uint8_t)~g2;
    uint8_t op = SHR_OPID[clamp(p[P_E5], 0, 6)];
    tsvf_t fa, fb;
    int32_t cut = (p[P_E6] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * 19;   /* ~1 octave per octave */
    int32_t ic1a = v->s[0], ic2a = v->s[1], ic1b = v->s[2], ic2b = v->s[3];

    (void)n;                                                        /* blocks are CTL = SHR_BLK long */
    memset(nosync, 0, sizeof nosync);
    /* oscillator 2 sits INTV semitones above, a few cents detuned (Shruthi's osc 2 detune); FM uses INTV as
     * its frequency ratio instead of a pitch offset, as the original does */
    inc1 = shr_inc24(m->inc);
    if (shape2 == SH_WAVE_FM) {
        inc2 = inc1; note2 = note1;
    } else {
        inc2 = shr_inc24(cents_inc(m->pitch16 + intv * 16, 6, m->fine));
        note2 = clamp(note1 + intv, 12, 127);
    }
    sv->o[0].parameter = prm1; sv->o[0].secondary_parameter = 24;
    sv->o[1].parameter = prm2; sv->o[1].secondary_parameter = (uint8_t)(intv + 24);
    sh_osc_render_n(&sv->o[0], shape1, (uint8_t)note1, inc1, nosync, sync1, b1, SHR_BLK);
    sh_osc_render_n(&sv->o[1], shape2, (uint8_t)note2, inc2, op == SH_OP_SYNC ? sync1 : nosync, sync2, b2, SHR_BLK);
    sh_mix_oscillators(b1, b2, op, g1, g2, SHR_BLK);

    tsvf_coef(&fa, cut, 0);                                         /* two 12 dB poles: the second carries the */
    tsvf_coef(&fb, cut, p[P_E7]);                                   /* resonance -> a resonant 24 dB/oct low-pass */
    for (i = 0; i < SHR_BLK; i++) {
        int32_t s = ((int32_t)b1[i] - 128) * 256;                   /* Q15, full scale */
        int32_t y = tsvf_lp(&fa, s >> 1, &ic1a, &ic2a);
        int32_t a;
        y = tsvf_lp(&fb, y, &ic1b, &ic2b);
        a = y < 0 ? -y : y;
        if (a > 16000) {                                            /* soft knee: only resonance peaks saturate */
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        out[i] += voice_amp(y * 2, m, i);
    }
    v->s[0] = ic1a; v->s[1] = ic2a; v->s[2] = ic1b; v->s[3] = ic2b;
}

/* ------------------------------------------------------------------- presets */
static const preset_t SHRUTHI_PRESETS[] = {
    /* WAVE1 WAVE2 PARM INTV MIX OP CUT RES */
    {"SAW PAIR",  {1, 1,   0,   0, 64, 0,  80, 30}, {4, 70, 100, 50},  30, 1, FX(0, 10, 40, 30), PAT(4)},
    {"PWM PAD",   {2, 2,  50,   0, 64, 0,  70, 10}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SYNC LEAD", {1, 1,   0,   7, 64, 1,  95, 25}, {4, 60, 100, 45},  20, 1, FX(10, 10, 40, 30), PAT(4)},
    {"FM BELL",   {10, 10, 60,  7, 64, 2, 110,  0}, {0, 90, 0, 70},     0, 0, FX(0, 30, 30, 50), PAT(6)},
    {"XOR BASS",  {2, 2,  30, -12, 80, 3,  60, 40}, {0, 55, 60, 30},   40, 1, FX(20, 0, 10, 10), PAT(2)},
    {"FUZZ BASS", {1, 3,   0, -12, 64, 4,  55, 50}, {0, 60, 40, 30},   45, 1, FX(25, 0, 10, 10), PAT(1)},
    {"CZ RESO",   {5, 4,  70,   0, 50, 0, 100, 20}, {2, 70, 80, 40},   25, 0, FX(0, 20, 30, 30), PAT(3)},
    {"WAVETABLE", {13, 15, 64,  0, 64, 0,  90, 15}, {70, 90, 110, 90}, 10, 0, FX(0, 50, 20, 60), PAT(5)},
    {"VOWEL",     {23, 1,  40,  0, 20, 0, 110,  0}, {30, 80, 100, 60},  0, 0, FX(0, 20, 20, 50), PAT(6)},
    {"QUAD PAD",  {9, 9,  50,   0, 64, 0,  75, 10}, {90, 90, 110, 95},  5, 0, FX(0, 60, 20, 70), PAT(5)},
    {"BIT FOLD",  {19, 1, 70,   0, 64, 5, 100, 30}, {0, 70, 80, 30},   15, 1, FX(10, 0, 20, 20), PAT(2)},
    {"NOISE HAT", {22, 22, 100, 0, 64, 0, 120,  0}, {0, 30, 0, 20},     0, 0, FX(0, 0, 20, 20), PAT(8)},
};

static const engine_t ENG_SHRUTHI = {
    .name = "SHRUTHI",
    .page_title = {"OSC", "MIX"},
    .edit = {
        {"WAVE1", F_ENUM, 0, 23, 1, N_SHR_WAVE, 0},
        {"WAVE2", F_ENUM, 0, 23, 1, N_SHR_WAVE, 0},
        {"PARM", F_PCT, 0, 127, 40, 0, 0},
        {"INTV", F_SEMI, -24, 24, 0, 0, 0},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"OP", F_ENUM, 0, 6, 0, N_SHR_OP, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = SHRUTHI_PRESETS,
    .npresets = NELEM(SHRUTHI_PRESETS),
    .note_on = shr_note_on,
    .render = shr_render,
    .knob = {P_E6, P_E7, P_E2, P_E4},
    .keep = 0x0F,                /* the two filter stages */
};

#endif /* ENG_SHRUTHI_C */
