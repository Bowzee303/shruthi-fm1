// Drives the ORIGINAL shruthi::Part / Voice (voice.cc, part.cc, unmodified; AVR hardware headers
// stubbed) so its audio output can be compared with the C port.
#define private public
#define protected public
// voice.cc is #included (not linked) so the file-scope oscillators and header-defined statics are reachable
#include "shruthi/voice.cc"
#include "shruthi/part.h"
#include "shruthi/audio_out.h"
#include "shruthi/midi_dispatcher.h"
HostAudioOut audio_out;
namespace shruthi { MidiDispatcher midi_dispatcher; }
using namespace shruthi;
extern "C" {
// The original keeps all voice state in statics that survive Part::Init(); a test that loads many
// patches in one process has to clear them so every run starts from the power-on state.
static void refv_reset_statics() {
  memset((void*)&osc_1, 0, sizeof osc_1); memset((void*)&osc_2, 0, sizeof osc_2);
  memset((void*)&sub_osc, 0, sizeof sub_osc);
  memset(Voice::buffer_, 0, sizeof Voice::buffer_); memset(Voice::osc2_buffer_, 0, sizeof Voice::osc2_buffer_);
  memset(Voice::sync_state_, 0, sizeof Voice::sync_state_); memset(Voice::dummy_sync_state_, 0, sizeof Voice::dummy_sync_state_);
  memset(Voice::dst_, 0, sizeof Voice::dst_); memset(Voice::modulation_destinations_, 0, sizeof Voice::modulation_destinations_);
  memset(Voice::modulation_sources_, 0, sizeof Voice::modulation_sources_);
  memset(Voice::disable_envelope_auto_retriggering_, 0, sizeof Voice::disable_envelope_auto_retriggering_);
  Voice::gate_ = 0; Voice::pitch_increment_ = Voice::pitch_target_ = Voice::pitch_value_ = Voice::pitch_bass_note_ = 0;
  Voice::trigger_count_ = 0; Voice::volume_ = 0;
  memset((void*)Part::lfo_, 0, sizeof(Part::lfo_)); memset(Part::previous_lfo_fm_, 0, sizeof Part::previous_lfo_fm_);
  memset(Part::lfo_step_, 0, sizeof Part::lfo_step_); memset(Part::lfo_limit_, 0, sizeof Part::lfo_limit_);
  memset(Part::lfo_increment_, 0, sizeof Part::lfo_increment_);
  transient_generator.counter_ = 0; transient_generator.gain_ = 0; transient_generator.rng_state_ = 0; transient_generator.decimate_ = 0;
}
void refv_init(const uint8_t* raw92) {
  memset((void*)&audio_out, 0, sizeof audio_out);
  refv_reset_statics();
  part.Init();
  part.mutable_sequencer_settings()->seq_tempo = 0;   /* external clock: no sequencer ticks, so tempo-synced LFOs stay frozen (not ported) */
  memcpy(part.mutable_patch()->saved_data(), raw92, 92);
  part.mutable_patch()->Update();
  Part::Touch(false);
  /* Lfo::Update() keeps the previous increment when given 0; with no sequencer clock a tempo-synced LFO
   * (rate < 16) would keep the default patch's speed. Start it frozen, like the port. */
  for (int i = 0; i < 2; ++i) if (Part::patch_.lfo[i].rate < 16) Part::lfo_[i].phase_increment_ = 0;
}
void refv_note_on(uint8_t note, uint8_t vel) { Voice::NoteOn((uint16_t)note << 7, vel, 0, true); }
void refv_note_off(void) { Voice::NoteOff(); }
// one 40-sample block through the original Part::ProcessBlock
void refv_block(uint8_t* out, uint8_t* cutoff, uint8_t* res, uint8_t* vca) {
  audio_out.n = 0;
  part.ProcessBlock();
  memcpy(out, audio_out.buf, kAudioBlockSize);
  *cutoff = Voice::cutoff(); *res = Voice::resonance(); *vca = Voice::vca();
}
}
extern "C" {
uint8_t refv_src(uint8_t i) { return Voice::modulation_source(i); }
uint8_t refv_dst(uint8_t i) { return Voice::modulation_destination(i); }
}
extern "C" {
uint16_t refv_lfo_inc(uint8_t i) { return Part::lfo_[i].phase_increment_; }
uint16_t refv_lfo_phase(uint8_t i) { return Part::lfo_[i].phase_; }
uint16_t refv_lfo_int(uint8_t i) { return Part::lfo_[i].intensity_; }
int refv_tempo(void) { return Part::sequencer_settings_.seq_tempo; }
}
extern "C" { int refv_lfo_cycle(uint8_t i) { return Part::lfo_[i].cycle_complete_; } int refv_lfo_mode(uint8_t i) { return Part::lfo_[i].retrigger_mode_; } }
