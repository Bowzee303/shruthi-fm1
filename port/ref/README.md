# Reference build of the ORIGINAL Shruthi firmware, on a PC

`build_ref.sh` compiles the unmodified Shruthi sources (`oscillator.cc`, `voice.cc` via `ref_voice_harness.cc`,
`part.cc`, `patch.cc`, ...) together with the real avrlib ("avril") so the C port can be compared byte for byte.
Only host-adaptation changes were needed; none alters DSP behaviour:

| Where | Change | Why |
|---|---|---|
| avrlib `op.h` (copy) | AVR-assembly switch removed; typo fixed in unused `U24Sub` | assembly cannot build on x86; C fallbacks are used |
| avrlib `resources_manager.h` (copy) | lookup-table pointers read directly | 16-bit `pgm_read_word` truncates 64-bit pointers |
| `stub/shruthi/transient_generator.h` | adds `return 0;` to `Trigger()` | original has no return (UB with modern compilers; value unused) |
| `stub/shruthi/{audio_out,storage,midi_dispatcher}.h`, `avr/*.h` | hardware stubs | DAC/EEPROM/MIDI are AVR-only |
| compile flag `-fpack-struct=1` | no struct padding | AVR has none; the oscillator state union overlaps bytes |
| `ref_voice_harness.cc` | sequencer clock off (`seq_tempo=0`); synced LFOs start frozen; statics cleared | tempo-synced LFOs and the sequencer are not ported |

Each (patch, note) comparison runs in a fresh process because the original keeps its state in globals.
