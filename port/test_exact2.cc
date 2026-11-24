// Bit-exact comparison of the ORIGINAL shruthi::Envelope and shruthi::Lfo against the C port,
// driven by long pseudo-random scripts of API calls (Trigger/Update/Render/Reset).
#include <stdio.h>
#include <string.h>
#include <stdint.h>
extern "C" {
#include "shruthi_core.h"
void sh_random_seed(uint16_t); void ref_seed(uint16_t);
void* ref_env_new(void); void ref_env_free(void*); void ref_env_trigger(void*, uint8_t);
void ref_env_update(void*, uint8_t, uint8_t, uint8_t, uint8_t); uint8_t ref_env_render(void*);
uint8_t ref_env_stage(void*); uint16_t ref_env_value(void*);
void* ref_lfo_new(void); void ref_lfo_free(void*); void ref_lfo_update(void*, uint8_t, uint16_t, uint8_t, uint8_t);
void ref_lfo_reset(void*); void ref_lfo_trigger(void*); uint8_t ref_lfo_render(void*); int ref_lfo_triggered(void*);
}
static uint32_t s = 1;
static uint32_t rnd() { s = s * 1664525u + 1013904223u; return s >> 8; }

int main() {
  long env_ops = 0, env_bad = 0, lfo_ops = 0, lfo_bad = 0;
  for (int seed = 1; seed <= 300; ++seed) {
    s = seed * 7919u;
    void* r = ref_env_new(); sh_env m; sh_env_init(&m); memset(&m, 0, sizeof m); sh_env_init(&m);
    for (int i = 0; i < 4000; ++i) {
      uint32_t op = rnd() % 100;
      if (op < 4) { uint8_t st = rnd() % 4; ref_env_trigger(r, st); sh_env_trigger(&m, st); }
      else if (op < 10) { uint8_t a = rnd() & 127, d = rnd() & 127, su = rnd() & 127, re = rnd() & 127;
                          ref_env_update(r, a, d, su, re); sh_env_update(&m, a, d, su, re); }
      else {
        uint8_t x = ref_env_render(r), y = sh_env_render(&m); ++env_ops;
        if (x != y || ref_env_stage(r) != m.stage || ref_env_value(r) != m.value) {
          if (++env_bad <= 3) printf("ENV MISMATCH seed %d step %d: ref=%d mine=%d\n", seed, i, x, y);
          break;
        }
      }
    }
    ref_env_free(r);
    sh_random_seed(0x4321); ref_seed(0x4321);
    void* rl = ref_lfo_new(); sh_lfo l; memset(&l, 0, sizeof l);
    for (int i = 0; i < 6000; ++i) {
      uint32_t op = rnd() % 100;
      if (op < 3) { ref_lfo_reset(rl); sh_lfo_reset(&l); }
      else if (op < 7) { ref_lfo_trigger(rl); sh_lfo_trigger(&l); }
      else if (op < 14) {
        uint8_t shape = rnd() % 21, mode = rnd() % 4, attack = rnd() & 127; uint16_t inc = (rnd() % 3) ? (uint16_t)(rnd() % 1500) : 0;
        ref_lfo_update(rl, shape, inc, attack, mode); sh_lfo_update(&l, shape, inc, attack, mode);
      } else {
        uint8_t x = ref_lfo_render(rl), y = sh_lfo_render(&l); ++lfo_ops;
        if (x != y || ref_lfo_triggered(rl) != l.cycle_complete) {
          if (++lfo_bad <= 3) printf("LFO MISMATCH seed %d step %d shape %d: ref=%d mine=%d\n", seed, i, l.shape, x, y);
          break;
        }
      }
    }
    ref_lfo_free(rl);
  }
  printf("Envelope: %ld renders compared, %ld script(s) diverged\n", env_ops, env_bad);
  printf("LFO:      %ld renders compared, %ld script(s) diverged\n", lfo_ops, lfo_bad);
  return env_bad || lfo_bad;
}
