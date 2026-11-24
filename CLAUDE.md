# CLAUDE.md — context for continuing this project

Written 2026-11-24 from the conversation history. Read `PROJECT_STATUS.md` first for the current state. Treat
anything here marked UNVERIFIED as a lead, not a fact. I (the previous Claude) had no access to the owner's
GitHub account or to their Windows PC, so I cannot tell you what is in the repository.

## 1. The owner and how they want to work

- GitHub user **Bowzee303**; repository **https://github.com/Bowzee303/shruthi-fm1** (created from the GitHub
  Android app; a screenshot of the "New repository" screen is in `uploads_misc/`). Android phone, and a
  **Windows PC** that may hold newer files than the repo. Messages are often dictated, so names come out misspelled
  ("Shruti", "Feluca", "Averlib") — the intended names are Shruthi, Felucca, avrlib/avril.
- **Goal: manage future FM-1 firmware builds entirely from the phone** (GitHub Actions, manual trigger, downloadable
  `.fwsc`). The owner does not want to manually upload ZIP files.
- Rules the owner stated explicitly (follow them):
  - Do **not** claim a successful firmware build unless the actual FM-1 firmware compilation succeeds.
  - If blocked, **stop and explain exactly what is preventing progress**.
  - **Do not overwrite existing work without checking it** (their latest files may be only on the PC; do not assume
    the repo has them). Compare before changing.
  - "Tell me when you are 100 percent complete": I answered that I cannot honestly promise this and reported
    verified vs unverified instead. Keep that honesty standard: never say "complete/works" for anything not
    actually run. Report negative results plainly.
  - For the migration/backup request: backup only, no new features or refactoring.
- The owner is happy with plain, direct explanations and wants exact links/steps (they could not find a release
  asset on GitHub and asked for exact URLs).

## 2. The project in one paragraph

Port the Shruthi-1 sound to the M-VAVE FM-1 (pocket 6-op FM synth, JieLi AC79 SoC). Result so far: a verified C port
of the Shruthi voice (`port/shruthi_core.c`) and a Felucca engine (`port/fe/eng_shruthi.c`), both tested on a PC.
**Not done:** adding the engine to the Felucca firmware, building the real FM-1 firmware, a GitHub Actions
workflow, hardware tests, listening/tuning.

## 3. Key technical decisions and why

1. **Host = Felucca, not fm1_open.** fm1_open's DSP is a sealed binary (`libfm1_engine.a`; `sdk/synth.json` has
   `sealed_audio_api: true`), so engines cannot be added there. Felucca (hugelton/Felucca, built on the same JieLi
   SDK) has an engine table (`ENGINES[]` in `firmware/src/engines.c`) and its own DSP.
2. **The Felucca engine is the sound source only.** Felucca already supplies envelopes, LFOs, mod matrix, sequencer,
   arpeggiator, so the engine = 2 Shruthi oscillators + interval + mix + operator + a filter. Felucca engines carry
   their own filter (there is no host filter). Eight edit slots forced trade-offs: WAVE1, WAVE2, PARM (shared;
   the host SHAPE modulation pushes the two oscillators in opposite directions = detuned-PWM chorus), INTV (osc 2
   interval; FM ratio when the shape is FM), MIX, OP, CUT, RES. Dropped: sub oscillator, noise mix, transient generator.
3. **Integer only in the engine** (Felucca rule). `shruthi_core.c` has float code (a PC filter, a resampler) that is
   compiled out with `-DSH_NO_FLOAT`; `fe/eng_shruthi.c` defines it before including the core. The check "float/double
   tokens in the engine translation unit: 0" is part of `make felucca`.
4. **Oscillators are rate-agnostic phase accumulators.** At Felucca's 44.1 kHz the 24-bit increment is simply
   `m->inc >> 8` (I first wrongly scaled by 39215.686/44100; corrected). Only the vowel formants (tuned per sample
   at 39215.686 Hz) need a factor, set via `sh_osc.fs_q16` (58277 at 44.1 kHz).
5. **Filter in the engine** = two cascaded Felucca trapezoidal SVFs (`tsvf_*` from `dsp.c`), the second resonant,
   24 dB/oct, key tracking ~1 octave/octave (`* 19` per 1/16 semitone). Cutoff 0..127 maps 30 Hz..16 kHz
   (about 1.17 steps per semitone). These numbers are estimates; **nobody has listened to it**.
6. **Faithfulness first.** The port keeps AVR quirks on purpose: the oscillator state is a byte image of the original
   union (`sh_osc.d[20]`, AVR layout, no padding), including the vowel loop writing a 4th amplitude into
   `noise_modulation`. A note-on `sh_osc_reset` perturbs quad-saw phases because of that overlap. Do not "clean up".
7. **API matches the original:** `sh_voice_note_on(v, note, velocity, portamento, trigger)` takes portamento as an
   argument (the original takes it from system settings, not the patch). Envelope rates are only computed inside
   `process_block`, so call `sh_voice_warm_up(v, 8)` (or run blocks) before the first note-on, as the real firmware's
   continuously running audio loop does. (I removed an earlier "priming" hack because it differed from the original.)
8. **LFO rate scheduling** copies `Part::ProcessBlock`: the LFO increment is recomputed only when the patch changes
   or the LFO-FM destination changes (`prev_lfo_fm`), not every block.

## 4. What was verified, and how (details in `port/PORTING_NOTES.md`)

- Oscillators: bit-exact vs the ORIGINAL `oscillator.cc` + the real avrlib: 14,520/14,520 cases (+60 known-quirk
  triangle cases at MIDI note >= 124 where the original reads past a table).
- Envelope/LFO: ~1.08 M / ~1.55 M random-script renders, 0 divergences.
- Whole voice: original `voice.cc` + `part.cc` run on a PC against the port, all 128 factory patches x 4 notes x
  400 blocks: 512/512 runs identical (audio bytes and cutoff/resonance/VCA CVs).
- Felucca engine: built against the real `felucca_engine.h` + `dsp.c` (Felucca-Engines' own `tests/fe_host.c` pattern):
  pitch <= 0.5 cent, 12 presets play/release/deterministic, 168 wave x op combos, ASan+UBSan clean, float-free.
- NOT verified: anything on the FM-1 or with the JieLi toolchain; the sound of the filter; Felucca firmware integration.
- Limits of the whole-voice test: sequencer clock disabled in the reference (tempo-synced LFOs, step-sequencer LFO,
  arpeggiator, sequencer sources are NOT ported/covered); the reference needed host-only edits (`port/ref/README.md`).

## 5. Facts about the upstream projects that were learned the hard way

- **avrlib is the repo `pichenettes/avril`**, referenced by Shruthi's `.gitmodules` with a dead `git://` URL. GitHub
  "Download ZIP" leaves submodules **empty** (Shruthi's `avrlib/`, Felucca's `firmware/engines/` and `firmware/bsp/`).
  Felucca's submodules are separate repos: `hugelton/Felucca-Engines` (MIT) and `hugelton/felucca-bsp` (MPL-2.0).
  `git clone --recurse-submodules` (or `git submodule update --init`) fetches them. Felucca releases also advertise a
  `felucca-X.Y.Z-source.tar.gz` (the owner could not find it; I never confirmed it exists).
- Felucca build (`Felucca-main/BUILDING.md`): `./build.sh` produces `build/felucca.bin`, `build/loader/ota.bin`,
  `build/felucca.fwsc`. Needs Linux x86-64 (Docker amd64 on macOS), `tools/get_toolchain.sh` (JieLi clang 4.0.1 pi32v2 into
  `~/.jieli/toolchain`), JieLi AC79 SDK cloned from gitee (`--branch AC79NN_SDK_V1.2.1_2023-12-13`, three files used),
  Python Pillow + fonttools. Env overrides `JIELI_TOOLCHAIN`, `AC79_SDK`. Release builds: `./build.sh --release X.Y.Z`.
  `tests/run_tests.sh` needs `AC79_SDK`. Whether GitHub's runners can reach JieLi's package server and gitee is UNTESTED.
- Install: web installer (Chrome/Edge, Web MIDI over USB): https://hugelton.github.io/Felucca/ ; own builds need a local
  copy of the site served from localhost/HTTPS (`web/make_site.py`); CLI `tools/fm1_install.py` (`mido`, `python-rtmidi`).
  iOS browsers have no Web MIDI; Android Chrome + USB-OTG is UNVERIFIED. Failed installs may need FM-1-transporter
  (https://github.com/kurogedelic/FM-1-transporter) — a computer, probably. The official installer offers a way back to V15/V16.
- Felucca engine contract (from `Felucca-Engines/src/felucca_engine.h`): `engine_t` with 8 `edit[]` slots, `presets`,
  `note_on`, `render(track, voice, out[], n, vmod_t*)`, `knob[4]`, `keep` mask; `FS=44100`, `CTL=32`; voice state is
  `ph[3]` + `s[8]`, engines needing more keep file-level statics (like WHEEL; mine: `shr_v[NPART][NVOICE]`).
  `tests/fe_host.c` there is a complete single-TU host; `tools/gen_engine_tables.py` makes `fe_tables.h`.
- Integration touch points in Felucca (from reading, UNVERIFIED by a build): `engines.c` (`#include`, `ENGINES[]`,
  `ENGINE_ORDER`, `NENG_SHOWN`), `core.h` (`NENGINES`, `ENGI_*`), `FAV_ENGINES`, user presets (`upreset.c`), the editor
  protocol, `ui_layer.c` `LY_ENG_KEYS`. Felucca's rule: append engines, never reorder.

## 6. Working environment of the previous Claude (limits you may or may not share)

- Sandbox: Linux, `bash`, **no network** (github.com does not resolve), no gdb, no JieLi toolchain, no git credentials.
  Each tool call is limited to 300 s. Web fetch can only open URLs already seen in the conversation and rate-limits.
  No GitHub connector was available (searched the connector registry; nothing relevant).
- **I could not read or write the owner's GitHub repository**, so its contents, branches, history, and whether a
  workflow exists are UNKNOWN. A new Claude with a GitHub connector should do step 1 of `PROJECT_STATUS.md` first.
- Chat history cannot be exported by me (`/mnt/transcripts` was empty); this file and `PROJECT_STATUS.md` are the record.
  Claude memory was disabled on this account.

## 7. Practical gotchas (cost me time; avoid them)

- After editing `shruthi_core.h`, **rebuild every object** (`make clean` first). Stale `.o` files with an old
  `sh_voice`/`sh_osc` layout produced zeros/garbage twice.
- Don't name a make target like an existing directory (`ref`): make skips it. Target is `refbuild`.
- Don't put a trailing `# comment` after a make variable assignment (it keeps the spaces).
- The original Shruthi keeps all voice state in globals; the whole-voice test therefore runs every (patch, note)
  in a **forked process**. Don't "optimize" that away.
- `TransientGenerator::Trigger()` in the original has no `return` — UB with modern compilers (hang/illegal instruction);
  `port/ref/stub/shruthi/transient_generator.h` is the one-line fix for the PC reference only.
- `-fpack-struct=1` is required for the reference build (AVR has no padding; the oscillator state is a union).
- `Lfo::Update` ignores a zero increment (keeps the previous one) — the harness zeroes synced LFOs explicitly.
- Shruthi factory patches are the 92-byte hex blobs in `shruthi/data/factory_data/factory_data.txt` (lines starting
  `patch<TAB>name<TAB>hex`).
- `zip -r` follows symlinks: exclude `port/ref/shruthi` and its `/*` when packaging.

## 8. How to proceed (summary; see PROJECT_STATUS.md for the ordered list)

1. Get access to the repo (connector, public temporarily, or `git log` + `git ls-files` output) and ask the owner which
   files on the Windows PC are newer; compare before changing anything.
2. Integrate `ENG_SHRUTHI` into the Felucca source; write a GitHub Actions workflow (`workflow_dispatch`, ubuntu,
   toolchain + SDK setup, `./build.sh`, upload `build/felucca.fwsc`); iterate on the real compiler errors.
3. Only claim a firmware build after the real JieLi compile + package step succeeds in CI.
