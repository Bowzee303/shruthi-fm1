// Wraps the ORIGINAL shruthi::Oscillator (unmodified oscillator.cc) for the exactness test.
#include "shruthi/oscillator.h"
#include "avrlib/random.h"
using namespace shruthi;
extern "C" {
void ref_seed(uint16_t s) { avrlib::Random::Seed(s); }
void* ref_new(void) {
  Oscillator* o = new Oscillator(); memset((void*)o, 0, sizeof(Oscillator));
  memcpy(user_wavetable, wav_res_waves, kUserWavetableSize);
  return o;
}
void ref_free(void* o) { delete (Oscillator*)o; }
void ref_reset(void* o) { ((Oscillator*)o)->Reset(); }
void ref_render(void* o, uint8_t shape, uint8_t note, uint32_t inc24, uint8_t param, uint8_t sec,
                uint8_t* sync_in, uint8_t* sync_out, uint8_t* buf) {
  Oscillator* osc = (Oscillator*)o;
  osc->set_parameter(param); osc->set_secondary_parameter(sec);
  uint24_t inc; inc.integral = (inc24 >> 8) & 0xffff; inc.fractional = inc24 & 0xff;
  osc->Render(shape, note, inc, sync_in, sync_out, buf);
}
}

// ---- original Envelope and Lfo (header-only in the Shruthi source) ----
#include "shruthi/envelope.h"
#include "shruthi/lfo.h"
extern "C" {
void* ref_env_new(void) { Envelope* e = new Envelope(); memset((void*)e, 0, sizeof(Envelope)); e->Init(); return e; }
void ref_env_free(void* e) { delete (Envelope*)e; }
void ref_env_trigger(void* e, uint8_t st) { ((Envelope*)e)->Trigger(st); }
void ref_env_update(void* e, uint8_t a, uint8_t d, uint8_t s, uint8_t r) { ((Envelope*)e)->Update(a, d, s, r); }
uint8_t ref_env_render(void* e) { return ((Envelope*)e)->Render(); }
uint8_t ref_env_stage(void* e) { return ((Envelope*)e)->stage(); }
uint16_t ref_env_value(void* e) { return ((Envelope*)e)->value(); }

static SequencerSettings g_seq;
void* ref_lfo_new(void) { Lfo* l = new Lfo(); memset((void*)l, 0, sizeof(Lfo)); memset((void*)&g_seq, 0, sizeof(g_seq)); return l; }
void ref_lfo_free(void* l) { delete (Lfo*)l; }
void ref_lfo_update(void* l, uint8_t shape, uint16_t inc, uint8_t attack, uint8_t mode) { ((Lfo*)l)->Update(shape, inc, attack, mode); }
void ref_lfo_reset(void* l) { ((Lfo*)l)->Reset(); }
void ref_lfo_trigger(void* l) { ((Lfo*)l)->Trigger(); }
uint8_t ref_lfo_render(void* l) { return ((Lfo*)l)->Render(g_seq); }
int ref_lfo_triggered(void* l) { return ((Lfo*)l)->triggered(); }
}
