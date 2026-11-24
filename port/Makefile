# Host build + tests for the Shruthi C core.
#   make test    : pitch accuracy, filter stability, every factory patch plays; writes WAVs to out/
#   make exact   : bit-exact oscillator / envelope / LFO comparison against the ORIGINAL sources
#   make voice   : whole-voice comparison (all factory patches x 4 notes x 400 blocks) against the original
#   make all     : test + exact + voice
# The comparisons need the shruthi-1 source (and avril = its avrlib) -- see build_ref.sh and ref/README.md:
#   make exact SHRUTHI=/path/to/shruthi-1-master AVRIL=/path/to/avril-master
SHRUTHI ?= ../src/shruthi-1-master
AVRIL   ?= ../src/avril-master
# Felucca-Engines checkout (felucca_engine.h, dsp.c, tools/)
ENGINES ?= ../src/Felucca-Engines-main
FACTORY := $(SHRUTHI)/shruthi/data/factory_data/factory_data.txt
CORE    := shruthi_core.c shruthi_core.h shruthi_tables.h
REFOBJ  := ref/ref_voice_harness.o ref/part.o ref/oscillator.o ref/resources.o ref/random.o ref/clock.o \
           ref/note_stack.o ref/parameter.o ref/patch.o ref/system_settings.o ref/voice_allocator.o \
           ref/sequencer_settings.o ref/avstring.o

all: test exact voice felucca

test: $(CORE) test_core.c
	mkdir -p out
	gcc -std=gnu99 -O2 -Wall -Wextra shruthi_core.c test_core.c -lm -o test_core
	./test_core $(FACTORY) out

.PHONY: refbuild all test exact voice felucca tables clean
refbuild: $(CORE)
	AVRIL=$(AVRIL) ./build_ref.sh $(SHRUTHI)

exact: refbuild test_exact.cc test_exact2.cc
	gcc -std=gnu99 -O1 -c shruthi_core.c -o shruthi_core.o
	g++ -std=gnu++11 -O1 -w -I. -Iref -c test_exact.cc -o test_exact.o
	g++ test_exact.o shruthi_core.o ref/ref_harness.o ref/oscillator.o ref/resources.o ref/random.o -lm -o test_exact
	./test_exact
	g++ -std=gnu++11 -O1 -w -I. -Iref -c test_exact2.cc -o test_exact2.o
	g++ test_exact2.o shruthi_core.o ref/ref_harness.o ref/oscillator.o ref/resources.o ref/random.o -lm -o test_exact2
	./test_exact2

voice: refbuild test_voice.cc
	gcc -std=gnu99 -O1 -c shruthi_core.c -o shruthi_core.o
	g++ -std=gnu++11 -O1 -w -I. -Iref -c test_voice.cc -o test_voice.o
	g++ test_voice.o shruthi_core.o ref/ref_harness.o $(REFOBJ) -lm -o test_voice
	./test_voice $(FACTORY) 2>/dev/null

# the Felucca engine, built against the REAL felucca_engine.h / dsp.c: pitch, presets, every wave x operator,
# determinism, AddressSanitizer + UBSan, and a check that the engine contains no floating point
felucca:
	python3 $(ENGINES)/tools/gen_engine_tables.py fe/fe_tables.h
	mkdir -p fe/out
	cc -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -I$(ENGINES)/src -Ife -I. -o fe/fe_shruthi_host fe/fe_shruthi_host.c -lm
	./fe/fe_shruthi_host fe/out
	cc -O1 -g -std=c99 -fsanitize=address,undefined -fno-sanitize-recover=all -Wno-unused-function -Wno-unused-parameter -I$(ENGINES)/src -Ife -I. -o fe/fe_san fe/fe_shruthi_host.c -lm
	./fe/fe_san | tail -1
	cc -E -P -std=c99 -I$(ENGINES)/src -Ife -I. fe/engine_only.c | awk '/ENGINE_BEGIN|sh_osc;/{f=1} f' > fe/engine_pp.txt
	@echo "float/double tokens in the engine translation unit: $$(grep -cwE 'float|double' fe/engine_pp.txt)"

# regenerate the tables header from the Shruthi source
tables:
	python3 gen_tables.py $(SHRUTHI)/shruthi/resources.cc shruthi_tables.h

clean:
	rm -f test_core test_exact test_exact2 test_voice *.o ref/*.o fe/fe_shruthi_host fe/fe_san fe/engine_pp.txt
