// Host stub of the MIDI dispatcher: no output, channel 0.
#pragma once
#include <stdint.h>
#include "shruthi/part.h"
namespace shruthi {
struct MidiDispatcher {
  void OnClock() {} void OnStart() {} void OnStop() {}
  void OnEdit(uint8_t, uint8_t, uint8_t) {}
  uint8_t channel() { return 0; }
  void OnInternalNoteOn(uint8_t, uint8_t) {} void OnInternalNoteOff(uint8_t) {}
  void ForwardNoteOn(uint8_t, uint8_t, uint8_t) {} void ForwardNoteOff(uint8_t, uint8_t) {}
};
extern MidiDispatcher midi_dispatcher;
}
