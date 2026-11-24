# Shruthi-1 -> M-VAVE FM-1 port: what exists, what is proven, what is not

License: Shruthi-1's firmware is GPL-3.0-or-later (Emilie Gillet). Everything derived from it here is the same.
Felucca is GPL-3.0-only, which is compatible. Felucca-Engines (the interface this engine follows) is MIT.
Unaffiliated with M-VAVE, Mutable Instruments or Hügelton Instruments.

## Contents

| Path | What |
|---|---|
| `shruthi_core.c/.h`, `shruthi_tables.h` | Plain-C port of the Shruthi voice (oscillators, sub osc, transient generator, envelopes, LFOs, 12-row mod matrix, CV operators, mixing operators, patch loader). `gen_tables.py` makes the tables from `resources.cc`. |
| `fe/eng_shruthi.c` | `ENG_SHRUTHI`: a Felucca engine (integer-only) built on that core. 12 presets. |
| `fe/fe_shruthi_host.c` | Host test that builds the engine against Felucca-Engines' **real** `felucca_engine.h` + `dsp.c`. |
| `test_core.c`, `test_exact*.cc`, `test_voice.cc`, `ref/`, `build_ref.sh` | Tests, including comparison against the ORIGINAL Shruthi source (see `ref/README.md`). |
| `out/`, `fe/out/` | WAVs: 8 factory patches through the C core (float filter), 12 engine presets through the Felucca engine. |

Run everything: `make all` (needs the three source trees; see the top of `Makefile` for paths).

## What is proven (all re-run from a clean build by `make all`)

- **Oscillators bit-exact** vs the original `oscillator.cc` + the real avrlib: 14,520/14,520 cases (all 24 shapes,
  6 parameter values, 9 notes, 5 secondary values, sync on/off, 12 blocks each). 60 more cases (triangle at MIDI
  note >= 124) differ because the original reads past the end of a table there; the port reads a sine slot.
- **Envelope and LFO bit-exact**: ~1.08 M and ~1.55 M renders over random call scripts, zero divergences.
- **Whole voice bit-exact**: the original `voice.cc` + `part.cc` run on a PC (AVR hardware stubbed) against the port:
  all 128 factory patches x 4 notes x 400 blocks (note-off included) = 512/512 runs identical in audio bytes and in
  the cutoff / resonance / VCA control values.
- **Felucca engine**: compiles clean with -Wall -Wextra against the real engine interface; pitch within 0.5 cent;
  all 12 presets play, release to silence, are deterministic; all 24 WAVE x 7 OP combinations sound and stay in
  range; passes under AddressSanitizer + UBSan; the engine translation unit contains no `float` / `double`.

### Limits of that proof (read these)
- The whole-voice comparison runs with the original's **sequencer clock off**, so tempo-synced LFOs (rate < 16),
  the step-sequencer LFO, the arpeggiator and the sequencer modulation sources are NOT covered. They are not ported.
- To build the original on a PC I made host-only changes (listed in `ref/README.md`): AVR assembly switched off,
  pointer reads for 64-bit, a missing `return` added to `TransientGenerator::Trigger()` (UB with modern compilers),
  `-fpack-struct=1` to mimic the AVR layout, hardware headers stubbed, state reset by running each case in a fresh
  process. None changes DSP behaviour, but it is a modified reference, not the AVR binary.
- "Bit-exact" is about the **8-bit samples before the filter**. Nothing proves the *sound* of the filter.

## What is NOT done / known differences

1. **The filter is a stand-in** and the largest audible difference from a real Shruthi, which uses an analog filter
   board. In the core (PC) it is a float ladder-style filter; in the Felucca engine it is two cascaded Felucca
   trapezoidal SVFs (one resonant), 24 dB/oct. Cutoff scaling, resonance feel and the key-tracking amount
   (~1 octave/octave, `* 19` in `eng_shruthi.c`) are my estimates. **I have not listened to any of it**; tune by ear.
2. **The Felucca engine is a subset** by design (8 parameter slots, and Felucca supplies envelopes/LFOs/matrix):
   WAVE1, WAVE2, PARM (shared by both oscillators; the host SHAPE modulation moves them in opposite
   directions), INTV (osc 2 interval; FM ratio for FM), MIX, OP, CUT, RES. Shruthi's sub oscillator, noise mix and
   transient generator are not exposed (no slot). The user wavetable slot plays the first 8 factory waves.
3. **Not integrated into Felucca's firmware and never built with the JieLi toolchain or run on an FM-1.** The host
   test proves the engine against the interface, not the firmware build. Footprint (from the host build): ~34 KB of
   tables in flash, ~3 KB RAM (`shr_v[NPART][NVOICE]`, 96 bytes per voice).
4. Tempo-synced / sequencer features of Shruthi (see above) are not ported.

## Integration into Felucca (steps; UNVERIFIED against the firmware build)

From reading `firmware/src/engines.c` and `core.h`. The store says "append, never reorder":
1. Copy `shruthi_core.c/.h`, `shruthi_tables.h`, `fe/eng_shruthi.c` into `firmware/src/` (or a subfolder; the
   `#include "shruthi_core.c"` inside `eng_shruthi.c` is relative).
2. `engines.c`: `#include "eng_shruthi.c"` after `dsp.c` and the other engines; append `&ENG_SHRUTHI` to `ENGINES[]`
   as the next index (15 today), and add that index to `ENGINE_ORDER[]` (and `NENG_SHOWN`).
3. `core.h`: bump `NENGINES`, add `ENGI_SHRUTHI`.
4. Check anything sized by engine count: `FAV_ENGINES` (favorites), user presets (`upreset.c`), the editor protocol's
   engine list, and `ui_layer.c` `LY_ENG_KEYS` (the EDIT layer's keys are limited).
5. Make sure `eng_shruthi.c`'s `P_E0..P_E7`, `P_ATK`, ... exist as in `fe_host.c` (they do in Felucca).
6. Then build with Felucca's own toolchain and test on the device. Expect to fix compile/size issues there.

## Next, in order
1. Build and flash; report compile errors / size / glitches.
2. Listen; tune the filter (cutoff curve, resonance, tracking) and the preset levels.
3. Decide whether to give up a slot for SUB / noise, or port the sequencer-synced LFOs.
