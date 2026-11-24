#!/usr/bin/env python3
"""Add ENG_SHRUTHI to a Felucca source tree (hugelton/Felucca), behind FELUCCA_SHRUTHI (default 0).

usage: apply_shruthi.py <felucca-dir> <this-repo-dir>

Every edit must match exactly once, otherwise this stops with an error (Felucca changed: look at the message).
Follows how the ADPCM engine (the newest) is wired. ENG_SHRUTHI takes engine number 13 + SLICE + ADPCM (appended,
nothing is renumbered). NOTE: with FELUCCA_ADPCM=0 that is 14, the number ADPCM uses when it is built, so do not
mix user presets / projects between an ADPCM build and a SHRUTHI build without ADPCM.
GPL: the Shruthi code is GPL-3.0-or-later; a firmware built with it must be distributed under GPL terms.
"""
import pathlib, shutil, sys

fw = pathlib.Path(sys.argv[1])
repo = pathlib.Path(sys.argv[2])
src = fw / "firmware" / "src"


def edit(path, old, new, count=1):
    p = fw / path
    s = p.read_text()
    n = s.count(old)
    if n != count:
        sys.exit(f"apply_shruthi: {path}: expected {count} match(es), found {n} for:\n{old}")
    p.write_text(s.replace(old, new))


# --- the engine's files, next to the other engines (felucca.c is one translation unit; build.py compiles only it)
for f in ("port/shruthi_core.c", "port/shruthi_core.h", "port/shruthi_tables.h", "port/fe/eng_shruthi.c"):
    shutil.copy(repo / f, src / pathlib.Path(f).name)
# the firmware has no <string.h>: its own memset / memcpy are in bsp/chip/libc.c (-fno-builtin)
decl = ("#ifdef SH_FIRMWARE\nvoid *memset(void *, int, unsigned);\nvoid *memcpy(void *, const void *, unsigned);\n"
        "#else\n#include <string.h>\n#endif")
edit("firmware/src/shruthi_core.c", "#include <string.h>", decl)
edit("firmware/src/eng_shruthi.c", "#define SH_NO_FLOAT 1", "#define SH_NO_FLOAT 1\n#define SH_FIRMWARE 1")

# --- core.h: the flag and the engine count
edit("firmware/src/core.h", "#if FELUCCA_ADPCM && !FELUCCA_SLICE",
     "#ifndef FELUCCA_SHRUTHI\n#define FELUCCA_SHRUTHI 0        /* the SHRUTHI engine (eng_shruthi.c), appended after ADPCM; build.py: FELUCCA_SHRUTHI=1 */\n#endif\n"
     "#if FELUCCA_SHRUTHI && !FELUCCA_SLICE\n#error \"FELUCCA_SHRUTHI needs FELUCCA_SLICE\"\n#endif\n"
     "#if FELUCCA_ADPCM && !FELUCCA_SLICE")
edit("firmware/src/core.h", "#define NENGINES (13 + FELUCCA_SLICE + FELUCCA_ADPCM)",
     "#define NENGINES (13 + FELUCCA_SLICE + FELUCCA_ADPCM + FELUCCA_SHRUTHI)")
edit("firmware/src/core.h", "#define ENGI_ADPCM 14u",
     "#define ENGI_ADPCM 14u\n#define ENGI_SHRUTHI (13u + FELUCCA_SLICE + FELUCCA_ADPCM)")

# --- engines.c: include, table, display order
edit("firmware/src/engines.c",
     "#endif\n\n/* the editor protocol, user presets and projects store these indices: append, never reorder */",
     "#endif\n#if FELUCCA_SHRUTHI\n#include \"eng_shruthi.c\"        /* SHRUTHI: Shruthi-1's oscillators (shruthi_core.c, GPL-3.0+) */\n#endif\n\n"
     "/* the editor protocol, user presets and projects store these indices: append, never reorder */")
edit("firmware/src/engines.c",
     "    &ENG_ADPCM,                  /* 14 (ENGI_ADPCM; FELUCCA_ADPCM=0 builds without it) */\n#endif\n};",
     "    &ENG_ADPCM,                  /* 14 (ENGI_ADPCM; FELUCCA_ADPCM=0 builds without it) */\n#endif\n"
     "#if FELUCCA_SHRUTHI\n    &ENG_SHRUTHI,                /* ENGI_SHRUTHI: 15, or 14 without ADPCM */\n#endif\n};")
edit("firmware/src/engines.c",
     "    14,                          /* ADPCM (1.8): last, so the EDIT layer's white keys keep the 13 above (ui_layer.c LY_ENG_KEYS) */\n#endif\n};",
     "    14,                          /* ADPCM (1.8): last, so the EDIT layer's white keys keep the 13 above (ui_layer.c LY_ENG_KEYS) */\n#endif\n"
     "#if FELUCCA_SHRUTHI\n    13 + FELUCCA_SLICE + FELUCCA_ADPCM,   /* SHRUTHI: after ADPCM, picked with PRESETS' ENG knob */\n#endif\n};")

# --- names, MIDI CC74/CC71 (CUT = E7, RES = E8), CPU cost estimate, EDIT-layer keys, categories, icon, credits
edit("firmware/src/params.c", "\"ADPCM\",\n#endif\n};", "\"ADPCM\",\n#endif\n#if FELUCCA_SHRUTHI\n                                             \"SHRUTHI\",\n#endif\n};")
edit("firmware/src/midi_control.c", "0x00,                                             /* ADPCM - (no filter) */\n#endif",
     "0x00,                                             /* ADPCM - (no filter) */\n#endif\n#if FELUCCA_SHRUTHI\n    0x78,                                             /* SHRUTHI CUT RES */\n#endif")
edit("firmware/src/fx.c", "168,      /* ADPCM (ORGAN, 1693 instructions / sample at 8 notes) */\n#endif",
     "168,      /* ADPCM (ORGAN, 1693 instructions / sample at 8 notes) */\n#endif\n#if FELUCCA_SHRUTHI\n                                             190,      /* SHRUTHI (an estimate, not measured) */\n#endif")
edit("firmware/src/ui_layer.c", "(NENG_SHOWN - FELUCCA_ADPCM))", "(NENG_SHOWN - FELUCCA_ADPCM - FELUCCA_SHRUTHI))")
edit("firmware/src/category.c", "\"LLBBKKPD\",                  /* 14 ADPCM:",
     "\"LLBBKKPD\",                  /* 14 ADPCM (SHRUTHI's, 14 or 15, below):")
edit("firmware/src/category.c", "                                  * NOISE HAT */\n};",
     "                                  * NOISE HAT */\n#if FELUCCA_SHRUTHI && FELUCCA_ADPCM\n    \"LPLUBBLPLPLD\",              /* 15 SHRUTHI */\n#endif\n};")
edit("firmware/src/icons.c", "        {\"ADPCM\", ICON_SAMPLE},", "        {\"ADPCM\", ICON_SAMPLE},\n        {\"SHRUTHI\", ICON_WAVE},")
edit("firmware/src/ui_menu.c", "#if FELUCCA_ADPCM\n    {\"ADPCM DSP\", MC_TITLE},",
     "#if FELUCCA_SHRUTHI\n    {\"SHRUTHI-1\", MC_TITLE},\n    {\"Emilie Gillet / GPL-3.0+\", MC_TEXT},\n    {\"\", MC_TEXT},\n#endif\n#if FELUCCA_ADPCM\n    {\"ADPCM DSP\", MC_TITLE},")
edit("tools/build.py", "\"FELUCCA_CPU2\", \"FELUCCA_ADPCM\"):", "\"FELUCCA_CPU2\", \"FELUCCA_ADPCM\", \"FELUCCA_SHRUTHI\"):")
print("apply_shruthi: ok")
