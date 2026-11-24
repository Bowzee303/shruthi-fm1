/* Host tests for shruthi_core: pitch accuracy, sanity of every factory patch,
 * and WAV rendering of a few patches for listening / spectral checks. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shruthi_core.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); ++failures; } } while (0)

static sh_patch blank_patch(uint8_t shape) {
  sh_patch p; memset(&p, 0, sizeof p);
  p.osc[0].shape = shape; p.osc[1].shape = SH_WAVE_NONE;
  p.filter_cutoff = 255; p.env[0].sustain = 127; p.env[1].sustain = 127;
  strcpy(p.name, "TEST");
  return p;
}

/* frequency from upward zero crossings of the DC-removed raw block output */
static double measure_hz(sh_voice *v, int blocks) {
  double mean = 0; int total = blocks * SH_BLOCK;
  uint8_t *buf = malloc(total);
  for (int b = 0; b < blocks; ++b) sh_voice_process_block(v, buf + b * SH_BLOCK);
  for (int i = 0; i < total; ++i) mean += buf[i];
  mean /= total;
  double first = -1, last = -1; int count = 0; double prev = buf[0] - mean;
  for (int i = 1; i < total; ++i) {
    double cur = buf[i] - mean;
    if (prev < 0 && cur >= 0) {
      double frac = -prev / (cur - prev);
      double pos = i - 1 + frac;
      if (first < 0) first = pos;
      last = pos; ++count;
    }
    prev = cur;
  }
  free(buf);
  if (count < 3) return 0;
  return (count - 1) * SH_SAMPLE_RATE / (last - first);
}

static void test_pitch(void) {
  const struct { uint8_t shape; const char *name; } shapes[] = {
    { SH_WAVE_SAW, "saw" }, { SH_WAVE_TRIANGLE, "triangle" }, { SH_WAVE_SQUARE, "square" },
    { SH_WAVE_WAVETABLE_1, "wavetable1" },
  };
  for (unsigned s = 0; s < sizeof shapes / sizeof *shapes; ++s) {
    double worst = 0;
    for (int note = 36; note <= 96; note += 6) {
      sh_patch p = blank_patch(shapes[s].shape);
      sh_voice v; sh_voice_init(&v, &p); sh_voice_warm_up(&v, 8);
      sh_voice_note_on(&v, (uint8_t)note, 100, 0, 1);
      for (int i = 0; i < 5; ++i) { uint8_t tmp[SH_BLOCK]; sh_voice_process_block(&v, tmp); }
      double hz = measure_hz(&v, 400);
      double want = 440.0 * pow(2.0, (note - 69) / 12.0);
      double cents = hz > 0 ? 1200.0 * log2(hz / want) : 9999;
      if (fabs(cents) > worst) worst = fabs(cents);
      CHECK(fabs(cents) < 15.0, "%s note %d: measured %.2f Hz, want %.2f Hz (%.1f cents)",
            shapes[s].name, note, hz, want, cents);
    }
    printf("pitch %-11s worst error %.2f cents\n", shapes[s].name, worst);
  }
}

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

typedef struct { char name[16]; uint8_t raw[SH_PATCH_BYTES]; } factory_patch;

static int load_factory(const char *path, factory_patch *out, int max) {
  FILE *f = fopen(path, "r"); if (!f) return 0;
  char line[1024]; int n = 0;
  while (fgets(line, sizeof line, f) && n < max) {
    if (strncmp(line, "patch\t", 6)) continue;
    char *name = line + 6, *tab = strchr(name, '\t'); if (!tab) continue;
    *tab = 0; char *hex = tab + 1;
    if (strlen(hex) < SH_PATCH_BYTES * 2) continue;
    strncpy(out[n].name, name, 15); out[n].name[15] = 0;
    for (int i = 0; i < SH_PATCH_BYTES; ++i) out[n].raw[i] = (uint8_t)(hexval(hex[2 * i]) * 16 + hexval(hex[2 * i + 1]));
    ++n;
  }
  fclose(f); return n;
}

typedef struct { sh_voice *v; sh_filter f; int blocks; } ctx_t;
static void refill(void *c, float *block) {
  ctx_t *x = c; uint8_t raw[SH_BLOCK]; float in[SH_BLOCK];
  sh_voice_process_block(x->v, raw);
  for (int i = 0; i < SH_BLOCK; ++i) in[i] = ((int)raw[i] - 128) / 128.f;
  sh_filter_process(&x->f, x->v->out_cutoff, x->v->out_resonance, in, block, SH_BLOCK);
  float g = x->v->out_vca / 255.f;
  for (int i = 0; i < SH_BLOCK; ++i) block[i] *= g;
  ++x->blocks;
}

static void write_wav(const char *path, const float *x, int n, int rate) {
  FILE *f = fopen(path, "wb"); if (!f) return;
  uint32_t data = (uint32_t)n * 2, riff = 36 + data, fmt = 16, sr = rate, br = rate * 2;
  uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
  fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (int i = 0; i < n; ++i) {
    float s = x[i] * 0.9f; if (s > 1) s = 1; if (s < -1) s = -1;
    int16_t q = (int16_t)lrintf(s * 32767.f); fwrite(&q, 2, 1, f);
  }
  fclose(f);
}

static void test_factory(const char *path, const char *outdir) {
  static factory_patch fp[128];
  int n = load_factory(path, fp, 128);
  CHECK(n >= 32, "factory patches parsed: %d", n);
  printf("factory patches: %d\n", n);
  const int rate = 44100, seconds = 2, total = rate * seconds;
  float *out = malloc(sizeof(float) * total);
  int silent = 0, bad = 0, clipped = 0, loaded = 0;
  for (int k = 0; k < n; ++k) {
    sh_patch p;
    if (!sh_patch_load(&p, fp[k].raw)) continue;
    ++loaded;
    sh_voice v; sh_voice_init(&v, &p); sh_voice_warm_up(&v, 8);
    ctx_t c; c.v = &v; c.blocks = 0; sh_filter_init(&c.f);
    sh_resampler rs; sh_resampler_init(&rs, (float)rate);
    sh_voice_note_on(&v, 57, 100, 0, 1);                 /* A3 */
    int half = total / 2;
    sh_resampler_run(&rs, out, half, refill, &c);
    sh_voice_note_off(&v);
    sh_resampler_run(&rs, out + half, total - half, refill, &c);
    double peak = 0, rms = 0; int nan = 0;
    for (int i = 0; i < total; ++i) {
      if (!isfinite(out[i])) { nan = 1; break; }
      double a = fabs(out[i]); if (a > peak) peak = a; rms += out[i] * out[i];
    }
    rms = sqrt(rms / total);
    if (nan) { ++bad; printf("  %-9s NaN/Inf!\n", fp[k].name); }
    else if (peak < 1e-4) { ++silent; printf("  %-9s silent (peak %.5f)\n", fp[k].name, peak); }
    if (peak > 1.5) { ++clipped; printf("  %-9s hot: peak %.2f\n", fp[k].name, peak); }
    if (outdir && k < 8) {
      char path2[512]; char nm[16]; int j = 0;
      for (int i = 0; fp[k].name[i] && j < 14; ++i) { char ch = fp[k].name[i]; nm[j++] = (ch == ' ' || ch == '?' || ch == '/') ? '_' : ch; }
      nm[j] = 0;
      snprintf(path2, sizeof path2, "%s/%02d_%s.wav", outdir, k, nm);
      write_wav(path2, out, total, rate);
    }
  }
  printf("loaded %d/%d, NaN %d, silent %d, hot %d\n", loaded, n, bad, silent, clipped);
  CHECK(bad == 0, "patches produced NaN/Inf");
  CHECK(loaded == n, "all factory patches should pass CheckBuffer");
  CHECK(silent <= n / 8, "too many silent patches: %d of %d", silent, n);
  free(out);
}

static void test_filter_stability(void) {
  /* every cutoff x resonance pair, full-scale noise + DC steps: output must stay finite and bounded */
  double worst = 0; int bad = 0;
  for (int c = 0; c < 256; c += 5)
    for (int r = 0; r < 256; r += 5) {
      sh_filter f; sh_filter_init(&f);
      float in[SH_BLOCK], out[SH_BLOCK]; uint32_t s = 12345u + c * 977u + r;
      for (int blk = 0; blk < 200; ++blk) {
        for (int i = 0; i < SH_BLOCK; ++i) { s = s * 1664525u + 1013904223u; in[i] = (blk % 50 < 25) ? ((s >> 8) / 8388608.f - 1.f) : 1.f; }
        sh_filter_process(&f, (uint8_t)c, (uint8_t)r, in, out, SH_BLOCK);
        for (int i = 0; i < SH_BLOCK; ++i) {
          if (!isfinite(out[i])) { ++bad; goto next; }
          if (fabs(out[i]) > worst) worst = fabs(out[i]);
        }
      }
      next:;
    }
  printf("filter stability: worst |y| = %.2f over a 52x52 cutoff/resonance grid\n", worst);
  CHECK(bad == 0, "filter produced NaN/Inf in %d settings", bad);
  CHECK(worst < 8.0, "filter output too large: %.2f", worst);
}

int main(int argc, char **argv) {
  test_pitch();
  test_filter_stability();
  if (argc > 1) test_factory(argv[1], argc > 2 ? argv[2] : NULL);
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures != 0;
}
