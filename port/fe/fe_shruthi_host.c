/* Host test for ENG_SHRUTHI, built the way Felucca-Engines' tests/fe_host.c builds its engines: one translation
 * unit with the real felucca_engine.h and dsp.c, and a minimal track / voice / envelope.
 *   cc -O2 -std=c99 -I<Felucca-Engines>/src -I<fe_tables dir> -I<port dir> -o fe_shruthi_host fe_shruthi_host.c
 * Checks: pitch, every preset, every WAVE1 x OP combination, determinism, release to silence, no overflow.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVOICE 8
#define NPART 4
#include "fe_tables.h"
#include "felucca_engine.h"

enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track { int16_t p[P_COUNT]; voice_t v[NVOICE]; } track_t;
static track_t trk[NPART];

#include "dsp.c"
#include "eng_shruthi.c"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); ++failures; } } while (0)

static uint32_t vage;
static void voice_on(track_t *t, const engine_t *e, voice_t *v, uint32_t note, int32_t fine)
{
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s[8];
    uint32_t k;
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note; v->vel = 100; v->gate = 1; v->active = 1; v->stage = 1; v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->fine = fine;
    if (!sounding) v->env = v->env_out = 0;
    e->note_on(t, v);
    if (sounding) {
        v->ph[0] = ph0; v->ph[1] = ph1; v->ph[2] = ph2;
        for (k = 0; k < 8u; k++)
            if (k != 2u && k != 3u && (e->keep >> k & 1u)) v->s[k] = s[k];
    }
}
static int32_t env_tick(voice_t *v)   /* the same plain envelope as fe_host.c */
{
    if (!v->active) return 0;
    switch (v->stage) {
    case 1: v->env += 1 << 22; if (v->env >= 1 << 24) { v->env = 1 << 24; v->stage = 2; } break;
    case 2: v->env -= (v->env - (3 << 22)) >> 3; break;
    case 3: v->env -= v->env >> 4;
            if (v->env < (1 << 24) / 1000) { v->env = 0; v->stage = 0; v->active = 0; } break;
    default: break;
    }
    return v->env >> 9;
}

/* Renders nblk blocks; voices: list of (block, note) starts; release at rel_blk. Returns peak, fills pcm (CTL*nblk). */
typedef struct { uint32_t blk; uint32_t note; int32_t fine; uint32_t voice; } start_t;
static int32_t render(const engine_t *e, const int16_t *ep, const start_t *st, uint32_t nst, uint32_t nblk,
                      uint32_t rel_blk, int32_t sweep, int32_t *pcm, uint64_t *hash)
{
    track_t *t = &trk[0];
    int32_t last[NVOICE] = {0}, peak = 0;
    uint32_t b, i, k, j;
    memset(t, 0, sizeof *t); memset(shr_v, 0, sizeof shr_v); vage = 0;
    sh_random_seed(0x1234);                    /* avrlib's Random is one global generator: reproducible renders */
    for (k = 0; k < 8; k++) t->p[P_E0 + k] = ep[k];
    for (b = 0; b < nblk; b++) {
        int32_t out[CTL];
        for (j = 0; j < nst; j++) if (st[j].blk == b) voice_on(t, e, &t->v[st[j].voice], st[j].note, st[j].fine);
        if (b == rel_blk) for (k = 0; k < NVOICE; k++) if (t->v[k].active) { t->v[k].gate = 0; t->v[k].stage = 3; }
        memset(out, 0, sizeof out);
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &t->v[k]; vmod_t m; int32_t a; uint32_t base;
            if (!v->active) continue;
            a = env_tick(v);
            if (!v->active) continue;
            base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
            memset(&m, 0, sizeof m);
            m.pitch16 = v->pitch16; m.inc = base + (uint32_t)((int32_t)(base >> 12) * v->fine); m.fine = v->fine;
            m.amp0 = last[k]; m.amp1 = a; m.envq15 = a;
            m.cutoff = sweep ? ((int32_t)(b % 96u) - 48) * 128 : 0;
            m.shape = (64 << 8) + (sweep ? ((int32_t)(b % 64u) - 32) * 256 : 0);
            last[k] = a;
            e->render(t, v, out, CTL, &m);
        }
        for (i = 0; i < CTL; i++) {
            int32_t x = out[i], ax = x < 0 ? -x : x;
            if (ax > peak) peak = ax;
            if (pcm) pcm[b * CTL + i] = x;
            if (hash) { uint32_t u = (uint32_t)x, q; for (q = 0; q < 4; q++) { *hash ^= (u >> (8 * q)) & 0xFF; *hash *= 1099511628211ull; } }
        }
    }
    return peak;
}

static double measure_hz(const int32_t *x, uint32_t n)
{
    double mean = 0, first = -1, last = -1, prev; uint32_t i, count = 0;
    for (i = 0; i < n; i++) mean += x[i];
    mean /= n; prev = x[0] - mean;
    for (i = 1; i < n; i++) {
        double cur = x[i] - mean;
        if (prev < 0 && cur >= 0) { double pos = (i - 1) + (-prev) / (cur - prev); if (first < 0) first = pos; last = pos; count++; }
        prev = cur;
    }
    return count < 3 ? 0 : (count - 1) * (double)FS / (last - first);
}

static void write_wav(const char *path, const int32_t *x, uint32_t n)
{
    FILE *f = fopen(path, "wb"); uint32_t i; uint32_t data = n * 2, riff = 36 + data, fmt = 16, sr = FS, br = FS * 2;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    if (!f) return;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (i = 0; i < n; i++) { int32_t v = x[i]; int16_t q; if (v > 32767) v = 32767; if (v < -32768) v = -32768; q = (int16_t)v; fwrite(&q, 2, 1, f); }
    fclose(f);
}

int main(int argc, char **argv)
{
    const engine_t *e = &ENG_SHRUTHI;
    static int32_t pcm[CTL * 600];
    uint32_t j, w, o, note;
    const char *outdir = argc > 1 ? argv[1] : 0;
    printf("FS=%d CTL=%d  vowel scale Q16=%u\n", FS, CTL, (unsigned)SHR_VOWEL_Q16);

    /* 1. pitch: osc 1 only, filter wide open, three waveforms, several notes */
    {
        static const struct { int w; const char *n; } W[] = {{1, "saw"}, {2, "square"}, {3, "triangle"}};
        for (w = 0; w < 3; w++) {
            double worst = 0;
            for (note = 36; note <= 96; note += 6) {
                int16_t ep[8] = {W[w].w, 0, 0, 0, 0, 0, 127, 0};          /* MIX 0 = osc 1 only */
                start_t st = {0, note, 0, 0};
                double hz, want = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0), cents;
                render(e, ep, &st, 1, 120, 9999, 0, pcm, 0);
                hz = measure_hz(pcm + 40 * CTL, 60 * CTL);
                cents = hz > 0 ? 1200.0 * log2(hz / want) : 9999;
                if (fabs(cents) > worst) worst = fabs(cents);
                CHECK(fabs(cents) < 8.0, "%s note %u: %.2f Hz, want %.2f (%.1f cents)", W[w].n, note, hz, want, cents);
            }
            printf("pitch %-9s worst error %.2f cents\n", W[w].n, worst);
        }
    }

    /* 2. every preset: a short phrase, finite, bounded, silent after the release, deterministic */
    {
        double worst_peak = 0;
        for (j = 0; j < e->npresets; j++) {
            int16_t ep[8]; uint32_t k, n_blk = 400, i; int32_t peak, tail = 0; uint64_t h1 = 14695981039346656037ull, h2 = h1;
            start_t st[3] = {{0, 43, 0, 0}, {30, 62, 23, 1}, {100, 50, -17, 2}};
            for (k = 0; k < 8; k++) ep[k] = e->presets[j].e[k];
            peak = render(e, ep, st, 3, n_blk, 200, 1, pcm, &h1);
            for (i = (n_blk - 20) * CTL; i < n_blk * CTL; i++) { int32_t a = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (a > tail) tail = a; }
            render(e, ep, st, 3, n_blk, 200, 1, 0, &h2);
            CHECK(peak > 200, "preset %s is silent (peak %d)", e->presets[j].name, peak);
            CHECK(peak < 60000, "preset %s is too hot (peak %d)", e->presets[j].name, peak);
            CHECK(tail < 50, "preset %s does not release to silence (tail %d)", e->presets[j].name, tail);
            CHECK(h1 == h2, "preset %s is not deterministic", e->presets[j].name);
            if ((double)peak > worst_peak) worst_peak = peak;
            printf("  %-10s peak %6d  tail %d\n", e->presets[j].name, peak, tail);
            if (outdir) { char path[512]; char nm[16]; uint32_t q = 0, r;
                for (r = 0; e->presets[j].name[r] && q < 14; r++) nm[q++] = e->presets[j].name[r] == ' ' ? '_' : e->presets[j].name[r];
                nm[q] = 0; snprintf(path, sizeof path, "%s/%02u_%s.wav", outdir, j, nm); write_wav(path, pcm, n_blk * CTL); }
        }
        printf("%u presets, worst peak %.0f\n", e->npresets, worst_peak);
    }

    /* 3. every WAVE1 x every OP at the defaults: finite, bounded; only OFF may be silent */
    {
        uint32_t silent = 0, hot = 0, cases = 0;
        for (w = 0; w < 24; w++) for (o = 0; o < 7; o++) {
            int16_t ep[8] = {(int16_t)w, 1, 64, 0, 64, (int16_t)o, 100, 30};
            start_t st = {0, 48, 0, 0};
            int32_t peak = render(e, ep, &st, 1, 100, 9999, 1, pcm, 0);
            ++cases;
            if (peak < 50 && w != 0) { ++silent; printf("  silent: wave %u op %u\n", w, o); }
            if (peak > 60000) { ++hot; printf("  hot: wave %u op %u peak %d\n", w, o, peak); }
        }
        printf("%u wave x op combinations: %u silent, %u hot\n", cases, silent, hot);
        CHECK(silent == 0, "%u combinations are silent", silent);
        CHECK(hot == 0, "%u combinations are too hot", hot);
    }
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
    return failures != 0;
}
