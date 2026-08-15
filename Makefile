## Revs — host (macOS/Linux) build system.
##
## The host build is NOT a visual reference for this port — see
## src/platform/host/PlatformHost.h for why.  What it is for:
##
##   make validate            the byte-exact native-twin differential (the real product
##                            of this build)
##   make validate FN=<sub>   only tests whose name contains <sub>  — use this; a full
##                            suite run gets slow fast
##   make                     the headless host runner (build/revs)
##   make gen                 regenerate the transliterated C from the Ghidra listing
##   make image               rebuild disasm/revs_mem.bin from revs.ssd (TRACK=SILVER etc.)
##   make runtime             replay the engine's startup relocation -> revs_runtime.bin
##                            ⭐ THIS, not revs_mem.bin, is what to disassemble
##                            (also runs `make dashcode` afterwards)
##   make dashcode            replay the SECOND unpack (copy_dash_data, $18EA) — the
##                            $7B00-$7FFF overlay Ghidra cannot see -> disasm/dashcode.txt
##   make sweep             the entry-point sweep report (docs/entrypoint-sweep.md)
##   make endian-lint         fail if anything aliases mem[] as uint16_t*/uint32_t*
##
## Visual ground truth: jsbeeb / b2 on the real disc (docs/bbc-reference-loop.md).
## Performance ground truth: FS-UAE + gdb on the Amiga build (docs/headless-fsuae.md).

CC  := clang
CXX := clang++

# Build mode: debug (default) or release.
#   make            -> debug (-O0 -g, full lldb variable support)
#   make RELEASE=1  -> release (-O2 -g)
ifdef RELEASE
  OPT := -O2
else
  OPT := -O0
endif

CFLAGS   := -std=c11   -g $(OPT) -Wall -Wno-unused-label -fsigned-char \
            -Isrc -Isrc/cpu -Isrc/platform -Isrc/gen
CXXFLAGS := -std=c++11 -g $(OPT) -Wall -Wno-reorder -fsigned-char \
            -Isrc -Isrc/cpu -Isrc/platform -Isrc/gen

# `make STRAIGHT_TO_RACE=1` — ⚠ `make clean` when you toggle it; this Makefile tracks the
# define no more than the Amiga one does, so a partial rebuild links objects compiled the
# other way and the flag silently does nothing.
#
# answer the front end's ONE practice-mode question the instant
# it is asked and press the starter, instead of the timed 8-menu walk.  Same flag and same
# script as the Amiga build (amiga/Makefile has the rationale); here it is the fast loop for
# checking that the answer lands, since the host reaches the driving loop in a second.
ifdef STRAIGHT_TO_RACE
CFLAGS   += -DREVS_STRAIGHT_TO_RACE
CXXFLAGS += -DREVS_STRAIGHT_TO_RACE
endif

# `make STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` — hold the throttle past the end of the script, so
# the host runs a MOVING car.  ⚠ Without it the script hands the keyboard back and the host
# sits parked in gear, which is a DIFFERENT SCENE from an Amiga FPSCOUNT/PROBES build (those
# hold automatically).  Comparing the two then compares a moving target against a parked host
# and attributes the difference to the backend.  Measured: that is exactly what happened while
# chasing the horizon stripes.
ifdef HOLD_THROTTLE
CFLAGS   += -DREVS_HOLD_THROTTLE
CXXFLAGS += -DREVS_HOLD_THROTTLE
endif

# C sources: the 6502 CPU model + the generated transliteration + native twins.
# The generated files do not exist until `make gen`; wildcard so a fresh clone builds.
C_SRCS := \
    src/cpu/cpu.c \
    src/platform/sound.c \
    $(wildcard src/gen/revs_gen.c) \
    $(wildcard src/gen/revs_manual.c) \
    $(wildcard src/gen/revs_native.c)

CXX_SRCS := \
    src/platform/Platform.cpp \
    src/platform/mos.cpp \
    src/platform/probe.cpp \
    src/platform/bbc_hw.cpp \
    src/platform/teletext.cpp \
    src/platform/autorun.cpp \
    src/platform/platform_cbridge.cpp \
    src/platform/host/PlatformHost.cpp \
    src/main.cpp

C_OBJS   := $(C_SRCS:.c=.o)
CXX_OBJS := $(CXX_SRCS:.cpp=.o)
OBJS     := $(C_OBJS) $(CXX_OBJS)
TARGET   := build/revs

.PHONY: all clean gen validate image runtime dashcode sweep endian-lint refloop refloop-keys \
        mode7 mode7-fixture font sound sound-fixture sound-fixture-race

all: $(TARGET)

$(TARGET): $(OBJS) | build
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Native-twin validation harness.  Links the full object graph minus main.o (for the
# symbol environment) plus the harness with its own main().
VALIDATE_OBJS := $(filter-out src/main.o,$(OBJS)) tools/validate_native.o
validate: $(VALIDATE_OBJS) | build
	$(CXX) $(CXXFLAGS) -o build/validate_native $(VALIDATE_OBJS)
	./build/validate_native $(FN)

# ⭐ MODE 7 validation — the port's VDU driver + SAA5050 against a REAL BBC, byte for byte.
#   make mode7-fixture      re-record the fixture off jsbeeb (needs volta/node; see the probe)
#   make mode7              replay it through the port's driver and diff every page
#   make mode7 PPM=tmp/m7   ...and write both decoded pages as PPMs to look at
# The fixture is an ordered log of the engine's VDU bytes AND its direct screen pokes, because
# the real page is built by two writers; tools/validate_mode7.c has the full rationale.
MODE7_OBJS := src/cpu/cpu.o src/platform/teletext.o tools/validate_mode7.o
mode7: $(MODE7_OBJS) | build
	$(CC) $(CFLAGS) -o build/validate_mode7 $(MODE7_OBJS)
	./build/validate_mode7 $(if $(PPM),--ppm=$(PPM),)

mode7-fixture:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_mode7.mjs \
	    --dump=../../tmp/mode7

# ⭐ SOUND validation — the port's MOS sound scheduler against a REAL BBC, tick for tick.
#   make sound                     replay both fixtures through src/platform/sound.c
#   make sound VERBOSE=1           ...and print the first mismatching ticks in full
#   make sound FIX=<file>          just one fixture
#   make sound-fixture             re-record the MOS sweeps (pitch/amplitude/envelope/flush)
#   make sound-fixture-race        re-record REVS'S OWN sound out of a real driving race
# Revs never addresses the SN76489 — it issues OSWORD 7/8 — so what is under test is the OS's
# scheduler, and none of it can be recovered from the game binary.  src/platform/sound.h has the
# model and where each number was measured.
SOUND_OBJS := src/platform/sound.o tools/validate_sound.o
sound: $(SOUND_OBJS) | build
	$(CC) $(CFLAGS) -o build/validate_sound $(SOUND_OBJS)
	./build/validate_sound $(if $(VERBOSE),--verbose,) $(FIX)

sound-fixture:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_sound.mjs \
	    --dump=../../tmp/soundref

# ⚠ --drive is not optional here: the engine sound IS the rev count, so a parked car records
# silence and the fixture would pass on nothing.
sound-fixture-race:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
	    --frames=$(FRAMES) --track=$(TRACK) --wing=$(WING) --drive \
	    --sound=tmp/soundref/revs_race.json

# Regenerate the MODE 7 character generator.  Sources the SAA5050 glyph shapes from jsbeeb's
# teletext data and GENERATES the two mosaic sets; see the generator's header for provenance and
# for why the cell is 8x10.  Checked in, so this is only needed when the layout changes.
font:
	python3 tools/gen_teletext_font.py

build:
	mkdir -p build

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Regenerate the transliterated C from the Ghidra listing.
# Requires disasm/listing.txt to be current (see docs/toolchain.md).
#   make gen              ingest disasm/dashcode.txt too (the $7B00-$7FFF overlay)
#   make gen DASHCODE=0   leave it out; the four $7Bxx call sites keep platform_brk() traps
gen:
	REVS_DASHCODE=$(if $(DASHCODE),$(DASHCODE),1) python3 tools/transpile.py

# Rebuild the post-load memory image from the disc.
#   make image              -> the default circuit (SILVER)
#   make image TRACK=BRANDS -> another (SILVER BRANDS DONING NURBURG OULTON SNETTER)
# ⚠ The image is the state BEFORE the track file patches the engine, and it is unconfirmed
# against a real machine — see tools/ssd_load.py and docs/bbc-reference-loop.md.
image:
	python3 tools/ssd_load.py revs.ssd disasm $(TRACK)

# ENDIANNESS LINT (postmortem §3.1).  mem[] is little-endian (6502); the Amiga is
# big-endian and this host is little-endian, so a uint16_t*/uint32_t* alias of mem[]
# reads correct here and byte-swapped on the target — `make validate` stays green while
# the Amiga renders garbage.  This grep is the cheap structural guard.  The ONE legitimate
# exception is a uniform-byte broadcast store; mark such a line with the comment
# `ENDIAN-OK:` and it is allowed through.
endian-lint:
	@hits=$$(grep -rnE '\((u?int(16|32)_t) *\*\) *(\(void\*\))? *(&? *mem|M\b)' \
	          src/ tools/ 2>/dev/null | grep -v 'ENDIAN-OK:' || true); \
	if [ -n "$$hits" ]; then \
	  echo "endian-lint: mem[] aliased as a wide pointer (see docs/m68k-optimisation.md):"; \
	  echo "$$hits"; exit 1; \
	else echo "endian-lint: clean"; fi

clean:
	rm -f $(OBJS) $(TARGET) tools/validate_native.o build/validate_native \
	      tools/validate_mode7.o build/validate_mode7 \
	      tools/validate_sound.o build/validate_sound

# ⭐ Replay the engine's own startup unpack -> disasm/revs_runtime.bin.
# REVS2 relocates itself before running, so revs_mem.bin is NOT the layout the engine
# executes.  THIS is the image to disassemble.  Full mechanism: tools/relocate.py.
#   make runtime                                     from disasm/revs_mem.bin
#   make runtime VERIFY="tmp/dump_SILVER_before.bin tmp/dump_SILVER_after.bin"
#                                                    cross-check against a real BBC
runtime:
	python3 tools/relocate.py $(if $(VERIFY),--verify $(VERIFY),)
	@$(MAKE) --no-print-directory dashcode   # ⚠ AFTER: dashcode reads revs_runtime.bin

# ⭐⭐ Replay the SECOND unpack -> disasm/dashcode.txt (and optionally the bytes).
# copy_dash_data ($18EA) assembles $7B00-$7FFF — 1280 bytes of live code, incl. the wing
# mirrors, three of which the main loop calls at 50 Hz — out of the tails of 41 blocks at
# $3000, and stows it back before returning to MODE 7.  So the page is $00 in
# revs_runtime.bin and Ghidra has nothing to disassemble there.
#
# ⚠ Deliberately NOT folded into revs_runtime.bin: the same copy also drops the dashboard
# bitmap over the track data at $70DB-$7813, which is faithful to a running machine and
# useless as a disassembly input.  One image, one meaning.  The transpiler reads the overlay
# listing alongside listing.txt (tools/transpile.py DASHCODE).
# docs/static-map.md §Open items 6.
dashcode:
	python3 tools/dashdata.py --listing disasm/dashcode.txt \
	                          --code-only disasm/revs_dashcode.bin | tail -n 12

# ⭐⭐ THE BBC DRIVING REFERENCE LOOP — ground truth from a real BBC, in a real race.
# Boots revs.ssd under jsbeeb, answers the front end (incl. the wing-settings prompt that
# blocked this for two days), starts the engine, engages first gear and drives, then dumps
# BOTH the BBC frame buffer and what the real 6845 + Video ULA actually displayed.
#
#   make refloop                                  200 frames of Silverstone practice
#   make refloop FRAMES=400 TRACK=1 WING=30       Brands Hatch, more downforce
#
# ⚠ jsbeeb resolves its ROMs against cwd, so it MUST be run from inside tools/jsbeeb, and
# output paths inside the script are relative to the SCRIPT — "../tmp" is this repo's tmp;
# "../../tmp" silently writes into ~/Documents/tmp and the run still looks successful.
# ⚠ Needs Node >= 24.15 (jsbeeb's own engines field); this machine's default Volta node is
# older, hence `volta run`.  Full write-up: docs/bbc-reference-loop.md.
FRAMES ?= 200
WING   ?= 20
TRACK  ?= 5
refloop:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
	    --frames=$(FRAMES) --track=$(TRACK) --wing=$(WING) --drive --dump=tmp/bbcref

# Is key injection working at all?  Verified at the BASIC prompt, where success is VISIBLE —
# never through the game, where a silent no-op and a rejected value look identical.
refloop-keys:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_return.mjs

# The entry-point sweep report (docs/entrypoint-sweep.md, docs/static-map.md).
#   make sweep                       -> disasm/sweep.txt
#   make sweep TRACE=tmp/trace_SILVER.bin  also cross-check against a real execution trace
sweep:
	python3 tools/sweep_entrypoints.py $(if $(TRACE),--trace $(TRACE),) > disasm/sweep.txt
	@echo "wrote disasm/sweep.txt"
	@sed -n '1,5p' disasm/sweep.txt
