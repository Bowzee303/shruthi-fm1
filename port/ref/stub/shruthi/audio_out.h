// Host stub: captures what Voice::ProcessBlock sends to the DAC.
#pragma once
#include <stdint.h>
struct HostAudioOut {
  uint8_t buf[4096]; int n;
  void Overwrite(uint8_t v) { if (n < 4096) buf[n++] = v; }
};
extern HostAudioOut audio_out;
