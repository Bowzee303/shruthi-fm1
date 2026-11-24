// Bit-exact comparison: original shruthi::Oscillator (oscillator.cc, unmodified) vs the C port.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
extern "C" {
#include "shruthi_core.h"
void sh_random_seed(uint16_t);
void ref_seed(uint16_t); void* ref_new(void); void ref_free(void*); void ref_reset(void*);
void ref_render(void*, uint8_t, uint8_t, uint32_t, uint8_t, uint8_t, uint8_t*, uint8_t*, uint8_t*);
extern const uint16_t dummy_unused;
}
#include "shruthi_tables.h"

static uint32_t increment_for_note(int note) {
  int pitch = note * 128; int ref = pitch - 116 * 128; int shifts = 0;
  while (ref < 0) { ref += 12 * 128; ++shifts; }
  uint32_t i = ((unsigned)ref) >> 4, f = (ref << 4) & 0xff;
  uint16_t a = lut_res_oscillator_increments[i], b = lut_res_oscillator_increments[i + 1];
  uint32_t inc = ((uint32_t)(a + (uint16_t)(((uint32_t)(uint16_t)(b - a) * f) >> 8))) << 8;
  while (shifts--) inc >>= 1;
  return inc;
}

int main() {
  const char* names[] = {"none","saw","square","triangle","cz_saw","cz_reso","cz_tri","cz_pulse","cz_sync","quad_saw","fm",
    "wt1","wt2","wt3","wt4","wt5","wt6","wt7","wt8","user","8bit","crushed","dirty_pwm","noise","vowel","wt9"};
  const int shapes[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,30,34};
  const uint8_t params[] = {0, 1, 37, 64, 100, 127};
  const int notes[] = {12, 24, 48, 60, 69, 84, 96, 110, 127};
  const uint8_t secs[] = {0, 20, 30, 40, 60};
  long cases = 0, bad_cases = 0, known_quirk = 0; int printed = 0;
  for (unsigned si = 0; si < sizeof shapes / sizeof *shapes; ++si) {
    int shape = shapes[si]; long shape_bad = 0, shape_cases = 0;
    for (unsigned pi = 0; pi < sizeof params; ++pi)
    for (unsigned ni = 0; ni < sizeof notes / sizeof *notes; ++ni)
    for (unsigned ci = 0; ci < sizeof secs; ++ci)
    for (int sync_mode = 0; sync_mode < 2; ++sync_mode) {
      sh_random_seed(0x1234); ref_seed(0x1234);
      sh_osc mine; memset(&mine, 0, sizeof mine);
      static uint8_t user_wt[129 * 8 + 1]; memcpy(user_wt, wav_res_waves, 129 * 8); mine.user_wt = user_wt;
      void* ref = ref_new();
      sh_osc_reset(&mine); ref_reset(ref);   /* all shapes: the union overlap is now modelled */
      uint32_t inc = increment_for_note(notes[ni]);
      int ok = 1;
      for (int blk = 0; blk < 12 && ok; ++blk) {
        uint8_t sin[SH_BLOCK], so_m[SH_BLOCK], so_r[SH_BLOCK], bm[SH_BLOCK], br[SH_BLOCK];
        for (int i = 0; i < SH_BLOCK; ++i) sin[i] = sync_mode && ((i * 7 + blk * 3) % 17 == 0);
        memset(so_m, 0xAA, sizeof so_m); memset(so_r, 0xAA, sizeof so_r);
        memset(bm, 0x55, sizeof bm); memset(br, 0x55, sizeof br);
        uint8_t midi_note = (uint8_t)notes[ni];

        mine.parameter = params[pi]; mine.secondary_parameter = secs[ci];
        sh_osc_render(&mine, (uint8_t)shape, midi_note, inc, sin, so_m, bm);
        ref_render(ref, (uint8_t)shape, midi_note, inc, params[pi], secs[ci], sin, so_r, br);
        if (memcmp(bm, br, SH_BLOCK) || memcmp(so_m, so_r, SH_BLOCK)) {
          ok = 0;
          if (printed < 6 && !(shape == 3 && notes[ni] >= 124)) {
            ++printed;
            printf("MISMATCH %s shape=%d param=%d note=%d sec=%d sync=%d block=%d\n", names[shape > 25 ? 25 : shape], shape, params[pi], notes[ni], secs[ci], sync_mode, blk);
            for (int i = 0; i < SH_BLOCK; ++i) if (bm[i] != br[i]) { printf("  first diff at sample %d: mine=%d ref=%d\n", i, bm[i], br[i]); break; }
          }
        }
      }
      ref_free(ref);
      if (!ok && shape == 3 && notes[ni] >= 124) { ++known_quirk; continue; }  /* original reads past its table */
      ++cases; ++shape_cases; if (!ok) { ++bad_cases; ++shape_bad; }
    }
    printf("%-10s shape %2d: %ld/%ld cases bit-exact\n", names[shape > 25 ? 25 : shape], shape, shape_cases - shape_bad, shape_cases);
  }
  printf("\nTOTAL: %ld/%ld cases bit-exact (+ %ld known-quirk cases: triangle at MIDI note >= 124, original reads past its table)\n", cases - bad_cases, cases, known_quirk);
  return bad_cases != 0;
}
