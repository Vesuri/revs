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

# ⭐ REVS_HW_TRACE — record every BBC hardware write so `make validate` can DIFF them.
# mem[] is only half of what a 6502 routine produces; for a display or timer routine the
# other half is all of it.  Twin #1 (irq1v_band_schedule) survived two deliberate defects with a
# byte-identical mem[], which is what put this here.  Host-only and unconditional: this
# build exists for the differential, not for speed (src/platform/host/PlatformHost.h).
# The Amiga build never defines it and the hook compiles to nothing there.
CFLAGS   := -std=c11   -g $(OPT) -Wall -Wno-unused-label -fsigned-char -DREVS_HW_TRACE \
            -Isrc -Isrc/cpu -Isrc/platform -Isrc/gen
CXXFLAGS := -std=c++11 -g $(OPT) -Wall -Wno-reorder -fsigned-char -DREVS_HW_TRACE \
            -Isrc -Isrc/cpu -Isrc/platform -Isrc/gen

# ⭐ `make BANDSKIP=0` — the control for the raster-band record reuse (src/platform/bbc_hw.cpp
# §fireIrq1vField).  Present on the host too, and not as a convenience: `make determinism` is the
# oracle for that change, so the A/B has to be runnable on the build that owns the differential.
# ⚠ `make clean` when you toggle it — this Makefile tracks the define no better than the flags below.
# ⚠⚠ THE CONTROL DIFFERS FROM THE DEFAULT BY EXACTLY 2 BYTES, and they are not a defect: the real
# cycle pushes P and X on the 6502 stack five times a field and pulls them straight back, so it
# leaves scratch at mem[$01F7]/mem[$01F8] — BELOW the entry S of $F8, i.e. dead.  The fast path
# pushes nothing.  Everything else at frame 300 is identical, frame buffer included, and the band
# record matches byte for byte.  So `make determinism BANDSKIP=0` will report those two bytes
# against a reference recorded from the default build; that is the expected result, not a
# divergence, and it is written down here rather than papered over in the differential.
ifeq ($(BANDSKIP),0)
CFLAGS   += -DREVS_NO_BANDSKIP
CXXFLAGS += -DREVS_NO_BANDSKIP
endif

# `make BANDCHECK=1` — the oracle for the reuse (see amiga/Makefile for the full argument).
# ⚠ On the host it is weak by construction: ~36 fields per run and a horizon that never moves.
# It lives here so the sabotage can be developed quickly; the verdict comes off the target.
ifeq ($(BANDCHECK),1)
CFLAGS   += -DREVS_BAND_CHECK
CXXFLAGS += -DREVS_BAND_CHECK
endif

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

# `make HOOK_PROFILE=1 TRACK=n STRAIGHT_TO_RACE=1` — per-ENTRY call counts for the circuit's own
# hook bodies, printed beside the screen dump.  The order a hook-twinning campaign should work in
# is not derivable from the listing: the same $53xx-$5Axx address is DIFFERENT CODE on every
# circuit, and several entries are only reached from an arm the render path takes on some frames.
# Host only, and `make clean` when you toggle it.
ifdef HOOK_PROFILE
CFLAGS   += -DREVS_HOOK_PROFILE
CXXFLAGS += -DREVS_HOOK_PROFILE
endif

# `make COMPETITION=1` — the COMPETITION branch instead of practice, so the session has a FIELD
# of other cars in it and competitor-car rendering finally has a stimulus (amiga/Makefile has the
# full rationale; src/platform/autorun.cpp has the menu chain).  Same `make clean` caveat.
ifdef COMPETITION
CFLAGS   += -DREVS_COMPETITION
CXXFLAGS += -DREVS_COMPETITION
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

# ⭐ ...and `REVS_HOLD_STEER=l|r` at RUN time holds a STEERING key beside the throttle, which is
# the only way to exercise the steering chain on a host build (the host has no keyboard, and the
# mouse axis is the Amiga's).  It is how "the wheel does not turn" was reproduced off-target in
# one run.  ⚠ Read it as TWO runs with opposite keys and check the sign bits differ: one run
# showing a moving wheel cannot tell steering from the slip-cancelling self-drive demand.
# On the Amiga the same knob is COMPILE-time (`amiga/make HOLD_STEER=l`) — no environment there.

# ⭐ `make STACK_TRAP=1` — arm the 6502 stack-watermark backtrace (host only; cpu.c has the
# mechanism).  Then run with a hex threshold: `REVS_STACK_TRAP=f0 ./build/revs`, and the first
# push that takes S below $F0 prints ONE backtrace naming the 6502 routines that leaked it.
# S is a correctness invariant in Revs, not bookkeeping — page 1's bottom is the per-car arrays
# (cpu.h) — so "how far it fell" is far less useful than "where".
# ⭐ `make TRACK=n` — boot circuit n instead of Silverstone (0=Silverstone, 1=Brands, 2=Donington,
# 3=Oulton, 4=Snetterton, 5=Nurburgring if that disc is present).  On the host $REVS_TRACK
# overrides it at run time too.  ⚠ An expansion circuit is REFUSED until its SMC sites exist and
# the build falls back to Silverstone, loudly — src/platform/track.h has the contract.
ifdef TRACK
CFLAGS   += -DREVS_TRACK_DEFAULT=$(TRACK)
CXXFLAGS += -DREVS_TRACK_DEFAULT=$(TRACK)
endif

# ⭐ `make INK_WATCH=1` — name the C routine that wrote a given frame-buffer byte.  The plotters
# reach the screen through the indirect modes, hence through bus_write, so that is the choke
# point; src/platform/bbc_hw.cpp §THE INK WATCH lists the REVS_INK_* run-time knobs.  Opt-in
# because bus_write is the hottest function in the build.
ifdef INK_WATCH
CFLAGS   += -DREVS_INK_WATCH -g -fno-omit-frame-pointer
CXXFLAGS += -DREVS_INK_WATCH -g -fno-omit-frame-pointer
endif

# ⭐⭐ `make TRANS_TRAP=1` — record every entry into a body that is still a 6502 TRANSLITERATION
# (validation oracles excepted: running those is the harness's job).  `make transtrap` below is
# the gate; the macro compiles to nothing without this flag, so the shipping build pays nothing.
ifdef TRANS_TRAP
CFLAGS   += -DREVS_TRANS_TRAP
CXXFLAGS += -DREVS_TRANS_TRAP
endif

ifdef STACK_TRAP
CFLAGS   += -DREVS_STACK_TRAP -g -fno-omit-frame-pointer
CXXFLAGS += -DREVS_STACK_TRAP -g -fno-omit-frame-pointer
endif

# ⭐ `make SHAPE=1` — the render path's INPUT-DISTRIBUTION counters (src/platform/shape.h),
# read with REVS_SHAPE_WATCH=N.  Phase 6 item 0 step 2: the numbers that size the dirty-flag
# and hardware-sprite items instead of assuming them.  The counters read only mem[], so the
# host measures the same shape the target would, for free.  ⚠ `make clean` when toggling —
# this Makefile tracks no build flags (docs/headless-fsuae.md).
ifdef SHAPE
CFLAGS   += -DREVS_SHAPE
CXXFLAGS += -DREVS_SHAPE
endif

# ⭐ `make GEOSPLIT=1` — build_track_geometry's per-frame call tallies (points, transforms,
# divides), platform-independent so the host counts them for free (src/platform/probe.h §GEOSPLIT).
# The beam TIME split is Amiga-only; here it is the COUNTS half.
ifdef GEOSPLIT
CFLAGS   += -DREVS_GEOSPLIT
CXXFLAGS += -DREVS_GEOSPLIT
endif

# ⭐ `make ROADSPLIT=1` — draw_road's per-frame leaf tallies (spans, DDA scan lines, columns,
# fill lines, mark points), platform-independent so the host counts them for free
# (src/platform/probe.h §ROADSPLIT).  The beam TIME split is Amiga-only; here it is the COUNTS half.
ifdef ROADSPLIT
CFLAGS   += -DREVS_ROADSPLIT
CXXFLAGS += -DREVS_ROADSPLIT
endif

# C sources: the 6502 CPU model + the generated transliteration + native twins.
# The generated files do not exist until `make gen`; wildcard so a fresh clone builds.
C_SRCS := \
    src/cpu/cpu.c \
    src/platform/sound.c \
    src/platform/track.c \
    src/platform/trans_trap.c \
    $(wildcard src/gen/revs_tracks.c) \
    $(wildcard src/gen/revs_gen.c) \
    $(wildcard src/gen/revs_track_hooks.c) \
    $(wildcard src/gen/revs_manual.c) \
    $(wildcard src/gen/revs_native.c) \
    $(wildcard src/gen/revs_native_seam.c)

CXX_SRCS := \
    src/platform/Platform.cpp \
    src/platform/mos.cpp \
    src/platform/probe.cpp \
    src/platform/shape.cpp \
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
        mode7 mode7-fixture font mos-font refloop-charset refloop-comp track-patch \
        tracks tracks-gen track-fixtures track-smc track-smc-check track-run viewdiff \
        trackmenu trackmenu-fixture titlescreen \
        sound sound-fixture sound-fixture-race determinism determinism-record fbwrites \
        determinism-drive determinism-drive-record \
        determinism-crash determinism-crash-record

all: $(TARGET)

$(TARGET): $(OBJS) | build
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# ⭐⭐ THE WHOLE-CORPUS DIFFERENTIAL.  `make validate` compares one twin against an oracle
# the SAME transpiler generated, so it is blind by construction to a codegen change that
# hits both — and the flag-liveness pass (tools/transpile.py FLAG_SUPPRESS) is exactly such
# a change: it rewrites 2783 instructions across every routine in the image.
#
# So: drive the real engine into a race for N frames with the clock pinned
# (REVS_FIXED_RNG=1) and dump all 64 KB.  A recorded reference image and a fresh run must be
# byte-identical.  Any wrongly-dropped flag diverges the simulation and shows up here.
#
#   make determinism-record   # after a change you have already proven correct
#   make determinism          # the check
#
# ⚠ The reference is git-ignored and machine-local by design: it is a witness that THIS tree
# still computes what it computed, not a fixture with independent authority.  Ground truth
# for behaviour is still the BBC (`make refloop`).
DET_FRAME  ?= 300
DET_REF    := tmp/determinism/ref.mem
DET_RUN    := tmp/determinism/run

determinism-record: $(TARGET)
	@mkdir -p tmp/determinism
	@rm -f $(DET_RUN)*
	REVS_FIXED_RNG=1 REVS_SCREEN_DUMP=$(DET_RUN) REVS_SCREEN_FRAME=$(DET_FRAME) \
	  REVS_MEM_DUMP=1 REVS_QUIT_AFTER_DUMP=1 ./$(TARGET) >/dev/null 2>&1
	@cp $(DET_RUN).mem.$(DET_FRAME) $(DET_REF)
	@echo "determinism: recorded frame $(DET_FRAME) -> $(DET_REF)"

determinism: $(TARGET)
	@test -f $(DET_REF) || { echo "no reference — run 'make determinism-record' first"; exit 1; }
	@mkdir -p tmp/determinism
	@rm -f $(DET_RUN)*
	REVS_FIXED_RNG=1 REVS_SCREEN_DUMP=$(DET_RUN) REVS_SCREEN_FRAME=$(DET_FRAME) \
	  REVS_MEM_DUMP=1 REVS_QUIT_AFTER_DUMP=1 ./$(TARGET) >/dev/null 2>&1
	@python3 tools/det_compare.py $(DET_REF) $(DET_RUN).mem.$(DET_FRAME) \
	  && echo "determinism: 64K byte-identical (stack scratch aside) at frame $(DET_FRAME) — PASS" \
	  || { echo "determinism: FAIL — the engine's state diverged"; exit 1; }

# ⭐⭐ …AND A SECOND TRAJECTORY, BECAUSE ONE IS NOT ENOUGH.  The default run above never puts
# the car under power — `road_speed` reads 0 at frame 300 and at frame 1500 — so it drives the
# simulation through its idle path only.  This one boots straight into the race and holds the
# throttle, i.e. a MOVING car on the circuit.  MEASURED, not assumed: dropping the fourth
# `engine_sound_update` call from race_main_loop's tail is byte-identical in the parked run at
# BOTH depths and FAILS here at frame 300 (2026-08-17).  Run both after a change to any driver.
#
# ⚠ It cleans, builds, runs, then rebuilds the default configuration, and that is deliberate:
# this Makefile tracks no build flag, so anything cheaper would eventually compare a DRIVE
# reference against a stale default binary — the exact failure this project has hit twice.
# Two full builds per invocation is the price of not inventing that trap again.
DET_DRIVE_REF   := tmp/determinism/ref_drive.mem
DET_DRIVE_RUN   := tmp/determinism/drive
DET_DRIVE_FRAME ?= 300

determinism-drive-record:
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 $(TARGET) >/dev/null
	@mkdir -p tmp/determinism
	@rm -f $(DET_DRIVE_RUN)*
	REVS_FIXED_RNG=1 REVS_SCREEN_DUMP=$(DET_DRIVE_RUN) \
	  REVS_SCREEN_FRAME=$(DET_DRIVE_FRAME) REVS_MEM_DUMP=1 REVS_QUIT_AFTER_DUMP=1 \
	  ./$(TARGET) >/dev/null 2>&1
	@cp $(DET_DRIVE_RUN).mem.$(DET_DRIVE_FRAME) $(DET_DRIVE_REF)
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory $(TARGET) >/dev/null
	@echo "determinism-drive: recorded frame $(DET_DRIVE_FRAME) -> $(DET_DRIVE_REF)"

determinism-drive:
	@test -f $(DET_DRIVE_REF) || \
	  { echo "no reference — run 'make determinism-drive-record' first"; exit 1; }
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 $(TARGET) >/dev/null
	@mkdir -p tmp/determinism
	@rm -f $(DET_DRIVE_RUN)*
	REVS_FIXED_RNG=1 REVS_SCREEN_DUMP=$(DET_DRIVE_RUN) \
	  REVS_SCREEN_FRAME=$(DET_DRIVE_FRAME) REVS_MEM_DUMP=1 REVS_QUIT_AFTER_DUMP=1 \
	  ./$(TARGET) >/dev/null 2>&1
	@python3 tools/det_compare.py $(DET_DRIVE_REF) $(DET_DRIVE_RUN).mem.$(DET_DRIVE_FRAME) \
	  && r=PASS || r=FAIL; \
	 $(MAKE) --no-print-directory clean >/dev/null; \
	 $(MAKE) --no-print-directory $(TARGET) >/dev/null; \
	 if [ "$$r" = PASS ]; then \
	   echo "determinism-drive: 64K byte-identical (stack scratch aside) at frame $(DET_DRIVE_FRAME), car MOVING — PASS"; \
	 else \
	   echo "determinism-drive: FAIL — the driving trajectory diverged"; exit 1; \
	 fi

# ⭐⭐ …AND A THIRD TRAJECTORY, THROUGH THE CRASH.  determinism-drive above dumps at frame 300 —
# the car is still ON the track there.  This one holds the throttle straight until the car leaves
# the circuit, CRASHES, and the off-line full-track scan/redraw driver (FUN_109b, the freeze
# subtree) runs, resets the car, and it crashes AGAIN: by frame 1500 FUN_109b has executed SEVEN
# times (MEASURED 2026-08-26; parked/drive-300 exercise it 0/2× only).  This is the gate for the
# freeze-subtree conversion, because FUN_109b is a NATIVE_FUNCS driver (transpile.py) with no
# randomised validate fixture — its oracle's first act is to run the rest of the engine — so a
# 64K byte-diff of a trajectory that actually crashes is the only thing that covers its arms.
# Same build as determinism-drive (STRAIGHT_TO_RACE + HOLD_THROTTLE); only the frame differs.
# ⚠ Like determinism-drive it cleans/builds/runs then rebuilds the default, for the same
# no-build-flag-tracking reason; two full builds is the price of not comparing against a stale one.
DET_CRASH_REF   := tmp/determinism/ref_crash.mem
DET_CRASH_RUN   := tmp/determinism/crash
DET_CRASH_FRAME ?= 1500

determinism-crash-record:
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 $(TARGET) >/dev/null
	@mkdir -p tmp/determinism
	@rm -f $(DET_CRASH_RUN)*
	REVS_FIXED_RNG=1 REVS_SCREEN_DUMP=$(DET_CRASH_RUN) \
	  REVS_SCREEN_FRAME=$(DET_CRASH_FRAME) REVS_MEM_DUMP=1 REVS_QUIT_AFTER_DUMP=1 \
	  ./$(TARGET) >/dev/null 2>&1
	@cp $(DET_CRASH_RUN).mem.$(DET_CRASH_FRAME) $(DET_CRASH_REF)
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory $(TARGET) >/dev/null
	@echo "determinism-crash: recorded frame $(DET_CRASH_FRAME) -> $(DET_CRASH_REF)"

determinism-crash:
	@test -f $(DET_CRASH_REF) || \
	  { echo "no reference — run 'make determinism-crash-record' first"; exit 1; }
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 $(TARGET) >/dev/null
	@mkdir -p tmp/determinism
	@rm -f $(DET_CRASH_RUN)*
	REVS_FIXED_RNG=1 REVS_SCREEN_DUMP=$(DET_CRASH_RUN) \
	  REVS_SCREEN_FRAME=$(DET_CRASH_FRAME) REVS_MEM_DUMP=1 REVS_QUIT_AFTER_DUMP=1 \
	  ./$(TARGET) >/dev/null 2>&1
	@python3 tools/det_compare.py $(DET_CRASH_REF) $(DET_CRASH_RUN).mem.$(DET_CRASH_FRAME) \
	  && r=PASS || r=FAIL; \
	 $(MAKE) --no-print-directory clean >/dev/null; \
	 $(MAKE) --no-print-directory $(TARGET) >/dev/null; \
	 if [ "$$r" = PASS ]; then \
	   echo "determinism-crash: 64K byte-identical (stack scratch aside) at frame $(DET_CRASH_FRAME), car CRASHED (FUN_109b ran 7×) — PASS"; \
	 else \
	   echo "determinism-crash: FAIL — the crash/scan trajectory diverged"; exit 1; \
	 fi

# Native-twin validation harness.  Links the full object graph minus main.o (for the
# symbol environment) plus the harness with its own main().
VALIDATE_OBJS := $(filter-out src/main.o,$(OBJS)) tools/validate_native.o
# ⚠ The LINK is its own rule on purpose.  It used to live inside the `validate` recipe, which
# meant `make build/validate_native` matched nothing and printed "Nothing to be done" over a
# stale binary — so anything that builds the harness without running it (a sabotage loop, a
# filtered rerun) silently tested the previous edit.
build/validate_native: $(VALIDATE_OBJS) | build
	$(CXX) $(CXXFLAGS) -o $@ $(VALIDATE_OBJS)
validate: build/validate_native
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

# ⭐ TRACK MENU validation — the port's circuit menu against the REAL REVSMEN, byte for byte.
#   make trackmenu-fixture      record the real pages off jsbeeb (needs volta/node + revs.ssd)
#   make trackmenu              paint them through the port and diff all six
#   make trackmenu PPM=tmp/tm   ...and write each page as a PPM to look at
#   make titlescreen            regenerate the embedded 5TRSCRN header (checked in)
# The menu is PORT-AUTHORED — REVSMEN is BASIC and is not transliterated — so this is the only
# screen in the game whose oracle has to be RECORDED rather than derived.  It is also the reason
# `make mode7` reports the REVSMEN snapshots as SKIPPED: they are BASIC's work, and this is where
# they get checked instead.  tools/validate_trackmenu.c has the scope note.
TRACKMENU_OBJS := src/cpu/cpu.o src/platform/teletext.o src/platform/trackmenu.o \
                  src/gen/revs_tracks.o tools/validate_trackmenu.o
trackmenu: $(TRACKMENU_OBJS) | build
	$(CXX) $(CXXFLAGS) -o build/validate_trackmenu $(TRACKMENU_OBJS)
	./build/validate_trackmenu $(if $(PPM),--ppm=$(PPM),)

trackmenu-fixture:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_trackmenu.mjs \
	    --dump=../../tmp/trackmenu

titlescreen: src/platform/titlescreen.h

# ⚠ A FILE RULE, not just the phony above: trackmenu.c #includes this and it is git-ignored (it is
# a kilobyte of the disc), so a fresh clone must be able to produce it on demand rather than fail
# with a missing-header error that says nothing about revs.ssd.
src/platform/titlescreen.h: revs.ssd tools/gen_titlescreen.py tools/ssd_map.py
	python3 tools/gen_titlescreen.py revs.ssd $@

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

# Regenerate the RACE VIEW's character generator — the OTHER font, the one OSWORD 10 returns.
# ⚠ Unlike `font` above, these 96 glyphs are DRAWN rather than sourced: the MOS software font is
# Acorn's copyrighted ROM with no standards document behind it.  Metrics match, letterforms are
# ours.  Checked in; see tools/gen_mos_font.py.
mos-font:
	python3 tools/gen_mos_font.py

# ⭐ What each expansion track PATCHES INTO THE ENGINE, by replaying its own ModifyGameCode
# rather than pattern-matching it.  The port cannot run the patcher (engine code is C, not bytes
# in mem[]), so it applies the replay's output as data at track-selection time.
#   make track-patch                 the patch sets, and the SMC surface they imply
#   make track-patch VERIFY=1        cross-check against the real-BBC differential
track-patch:
	python3 tools/track_patch.py $(if $(VERIFY),--verify,)

# ⭐⭐ THE SAME SURFACE AT INSTRUCTION GRANULARITY, which is the granularity the transpiler emits
# at.  Folds every circuit's byte writes onto the listing's instruction boundaries and reports
# EXTENTS — a patch that turns `CLC / ADC #$03` into one `JSR $54F1` is one substitution, not two
# mangled instructions.  Also checks the two structural preconditions the emission relies on
# (nothing branches into an extent; no extent crosses a function boundary).
#   make track-smc                the human-readable report (start here)
#   make track-smc EMIT=1         regenerate disasm/track_smc.txt, which `make gen` ingests
#
# ⚠ The committed table is derived from whichever discs are present.  It carries only shapes and
# offsets — no per-circuit values — so it is identical for all six circuits and committing it
# carries no third party's data.  Regenerating it with the Nürburgring disc present is therefore
# safe; it will produce the same file.
track-smc:
	python3 tools/track_smc.py $(if $(EMIT),--emit disasm/track_smc.txt,)

# The staleness guard on its own (`make gen` runs it too).
track-smc-check:
	python3 tools/track_smc.py --check

# ⭐ The per-circuit DATA TABLE the port selects between -> src/gen/revs_tracks.[ch].
# ⚠ The Nürburgring block comes from the git-ignored revs-hack-nurburgring.ssd, so a checkout
# without that disc generates FIVE circuits, not six, and says so.  That is not an error —
# docs/reference-sources.md §The Nürburgring file, MEASURED has the provenance reasoning.
tracks-gen:
	python3 tools/gen_tracks.py

track-fixtures:
	python3 tools/gen_tracks.py --check --fixtures tmp/tracks

# ⭐⭐ THE CIRCUIT INSTALLER, byte-exact against the disc.  Installs each circuit into mem[] the
# way the port will and diffs the whole 64K against an image built down an independent path
# (relocate the disc for THAT circuit, then replay its ModifyGameCode).  So it verifies the
# installer *and* re-proves that nothing outside the two extents differs per circuit.
# ⚠ An expansion circuit REFUSING to install is the correct behaviour until its SMC sites exist,
# and is reported as its own outcome — never counted as a verified install (src/platform/track.h).
# ⚠ revs_track_hooks.o is deliberately NOT here.  The harness needs the hook TABLE (which circuit
# has a body for which address) and never a hook BODY: it installs data and diffs bytes, it runs no
# engine code.  The table is header-only for exactly that reason — linking the bodies would drag
# the whole platform in and turn a data differential into an integration test.
TRACKS_OBJS := src/cpu/cpu.o src/platform/track.o src/gen/revs_tracks.o tools/validate_tracks.o
tracks: track-fixtures $(TRACKS_OBJS) | build
	$(CC) $(CFLAGS) -o build/validate_tracks $(TRACKS_OBJS)
	./build/validate_tracks

# ⭐⭐ DOES THE CIRCUIT'S OWN CODE ACTUALLY EXECUTE?  `make tracks` proves the DATA path; this
# proves the CODE path, and the two are genuinely different questions — an expansion circuit can
# install byte-perfectly and then run Silverstone's control flow over its geometry, which is not a
# crash and not visible in any byte diff of the install.
#
# So: race each circuit for real (STRAIGHT_TO_RACE, so the window is the RACE and not the front
# end) and require g_trackHookCalls > 0 with g_trackHookMissing == 0.  Silverstone is passive and
# must report exactly 0 — which is what makes the others' non-zero counts mean something.
#
# ⚠⚠ THE WINDOW IS THE WHOLE MEASUREMENT.  A plain build reports `hook calls 0` at frame 40 and
# looks like a dead seam; frame 40 is still MODE 7, and the rasteriser — hence every hook — is only
# reached once the race starts.  Hence STRAIGHT_TO_RACE and a small frame number here.
#   make track-run              every circuit in this build
#   make track-run TRACKS="0 1" just those
#   make track-run FRAME=40     dump later (slower: each frame costs seconds on the host)
# ⚠⚠ IT LEAVES THE TREE IN THE DEFAULT CONFIGURATION, and that final rebuild is not tidiness:
# this loop builds `STRAIGHT_TO_RACE=1 TRACK=n` six times, this Makefile tracks no build flag, and
# without the restore the next `make determinism` runs a Nurburgring straight-to-race binary against
# a Silverstone reference and reports a divergence that is entirely the build.  MEASURED — it cost
# a debugging detour on 2026-08-17.
track-run:
	@set -e; \
	frame=$(if $(FRAME),$(FRAME),15); \
	list="$(if $(TRACKS),$(TRACKS),0 1 2 3 4 5)"; \
	fails=0; \
	for t in $$list; do \
	  $(MAKE) --no-print-directory clean >/dev/null; \
	  $(MAKE) --no-print-directory STRAIGHT_TO_RACE=1 TRACK=$$t >/dev/null; \
	  out=$$(REVS_SCREEN_DUMP=tmp/trackrun_$$t.bin REVS_SCREEN_FRAME=$$frame \
	         REVS_QUIT_AFTER_DUMP=1 \
	         timeout $(if $(TIMEOUT),$(TIMEOUT),300) ./build/revs 2>&1 | tail -3); \
	  echo "$$out" | sed -n 's/^/  /p'; \
	  calls=$$(echo "$$out" | sed -n 's/.*hook calls \([0-9]*\).*/\1/p'); \
	  missing=$$(echo "$$out" | sed -n 's/.*missing \([0-9]*\).*/\1/p'); \
	  if [ -z "$$calls" ]; then \
	    echo "  FAIL track $$t: no dump line — the run HUNG or died before frame $$frame."; \
	    echo "       ⚠ 'no output' is a FAILURE here, not a pass: a circuit that hangs mid-race"; \
	    echo "       produces exactly this, and an untimed loop would have hung make instead."; \
	    fails=1; \
	  elif [ "$$missing" != "0" ]; then echo "  FAIL track $$t: $$missing hook(s) with no body"; fails=1; \
	  elif [ "$$t" = "0" ] && [ "$$calls" != "0" ]; then \
	    echo "  FAIL track 0: Silverstone is PASSIVE and called $$calls hooks"; fails=1; \
	  elif [ "$$t" != "0" ] && [ "$$calls" = "0" ]; then \
	    echo "  FAIL track $$t: installed but its hooks NEVER RAN — the seam is dead, or the"; \
	    echo "       window never reached the race (see the note above this target)"; fails=1; \
	  fi; \
	done; \
	$(MAKE) --no-print-directory clean >/dev/null; \
	$(MAKE) --no-print-directory $(TARGET) >/dev/null; \
	[ $$fails = 0 ] || exit 1; \
	echo "track-run: every circuit's own code executes"; \
	python3 -c 'import itertools,sys,glob; \
f={p:open(p,"rb").read() for p in sorted(glob.glob("tmp/trackrun_*.bin"))}; \
same=[(a,b) for a,b in itertools.combinations(sorted(f),2) if f[a]==f[b]]; \
print("track-run: all %d frame pairs differ" % len(list(itertools.combinations(f,2))) if not same \
else "FAIL: identical frames: %s" % same); sys.exit(1 if same else 0)'

# ⭐⭐ DOES ANY 6502 TRANSLITERATION STILL RUN?  The whole port's direction is "delete the
# interpreter", and until this target existed that claim rested on reading the source — which
# cannot see a rare arm, a per-circuit hook body or a self-modifying re-entry.  TRANS_TRAP=1
# makes every non-oracle generated body record its own entry; this drives the scenarios the host
# can drive and FAILS if any of them reports one.
#
#   make transtrap              the front end, a 300-frame race, the crash trajectory, 6 circuits
#   make transtrap FRAMES=...   (the race frame; the others are fixed)
#
# ⚠ A body that no scenario reaches is NOT proven dead — it is unproven, which is why the arms
# this cannot drive (qualifying, the pits, unusual menus) stay documented as unproven rather than
# quietly counted as clean.
# ⚠⚠ It rebuilds with different flags six times over and RESTORES the default build at the end,
# for the reason spelled out above track-run.
transtrap:
	@set -e; \
	mkdir -p tmp/trans; rm -f tmp/trans/*.txt; \
	run() { \
	  tag=$$1; shift; frame=$$1; shift; \
	  $(MAKE) --no-print-directory clean >/dev/null; \
	  $(MAKE) --no-print-directory TRANS_TRAP=1 "$$@" $(TARGET) >/dev/null; \
	  REVS_TRANS_LOG=tmp/trans/$$tag.txt REVS_FIXED_RNG=1 \
	    REVS_SCREEN_DUMP=tmp/trans/$$tag.bin REVS_SCREEN_FRAME=$$frame \
	    REVS_QUIT_AFTER_DUMP=1 timeout $(if $(TIMEOUT),$(TIMEOUT),600) ./$(TARGET) >/dev/null 2>&1; \
	  test -f tmp/trans/$$tag.txt || { echo "  FAIL $$tag: no log — the run died before exit"; exit 1; }; \
	  n=$$(wc -l < tmp/trans/$$tag.txt | tr -d ' '); \
	  echo "  $$tag: $$n transliterated bodies entered"; \
	  sed -n 's/^/      /p' tmp/trans/$$tag.txt; \
	}; \
	run frontend 200; \
	run race $(if $(FRAMES),$(FRAMES),300) STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1; \
	run crash 1500 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1; \
	for t in 0 1 2 3 4 5; do run circuit$$t 60 STRAIGHT_TO_RACE=1 TRACK=$$t; done; \
	$(MAKE) --no-print-directory clean >/dev/null; \
	$(MAKE) --no-print-directory $(TARGET) >/dev/null; \
	if [ -s tmp/trans/frontend.txt ] || [ -n "$$(cat tmp/trans/*.txt)" ]; then \
	  echo "transtrap: FAIL — a production build still executes 6502 transliteration"; exit 1; \
	else \
	  echo "transtrap: no transliteration executed in any scenario (9 runs)"; \
	fi

# ⭐ Which character codes does the RACE VIEW actually ask the MOS for, and where do the glyphs
# land?  Measures it on a real BBC in a real driving race — the input to mos-font above.
# It records CODES and CELLS only, never the ROM's bitmaps.
# ⭐ A real BBC in a COMPETITION race — the session with a FIELD of other cars.  Practice runs
# the player alone, so it cannot answer "does competitor-car rendering work"; this can.
refloop-comp:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
	    --frames=$(FRAMES) --track=$(TRACK) --wing=$(WING) --drive --competition \
	    --dump=tmp/bbccomp

refloop-charset:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
	    --frames=$(FRAMES) --track=$(TRACK) --wing=$(WING) --drive --charset

build:
	mkdir -p build

# ⚠⚠ HEADER DEPENDENCIES ARE TRACKED, and they were not until 2026-08-15.
#
# `make tracks` reported a GREEN, STALE answer: a sabotage run that removed an SMC extent should
# have shown three unhonoured patch bytes and showed none, because track.o had been compiled
# against the previous revs_smc_bytes.h and nothing told make to rebuild it.  A generated header
# whose consumer is not rebuilt does not fail — it answers the OLD question, confidently.
# (Same class as the Amiga Makefile's `make clean` warning in CLAUDE.md, which stays: that build
# tracks neither headers nor PROBES.)
DEPFLAGS := -MMD -MP

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(OBJS:.o=.d) $(TRACKS_OBJS:.o=.d) tools/validate_native.d

# Regenerate the transliterated C from the Ghidra listing.
# Requires disasm/listing.txt to be current (see docs/toolchain.md).
#   make gen              ingest disasm/dashcode.txt too (the $7B00-$7FFF overlay)
#   make gen DASHCODE=0   leave it out; the four $7Bxx call sites keep platform_brk() traps
gen:
	@# ⚠ FIRST: the committed extent table must still match the circuits on disc.  A stale
	@# table is silently wrong rather than loudly missing — a new circuit's patch addresses
	@# would still be in the union revs_smc_bytes.h publishes, so the installer would accept
	@# it and an arm would bake somebody else's operand (docs/phases.md §5b).
	python3 tools/track_smc.py --check
	@# ⚠⚠ THEN: every self-modifying site the entry-point sweep found must be DECLARED in
	@# transpile.py, or waived with a reason.  An undeclared site does not crash — the
	@# transliteration freezes whatever opcode the static image happened to hold, which is
	@# always one of the legal values, so it runs and looks plausible and no mem[] diff can
	@# see it.  That is exactly how the rev counter pointed at 9000 rpm for months
	@# ($5220/$529B, the dial needle's octant).  The sweep had reported both all along.
	python3 tools/sweep_entrypoints.py --audit-smc > disasm/sweep.txt
	REVS_DASHCODE=$(if $(DASHCODE),$(DASHCODE),1) python3 tools/transpile.py
	@$(MAKE) --no-print-directory tracks-gen   # ⚠ AFTER: gen_tracks.py needs revs_smc_bytes.h's
	                                           # sibling outputs to exist for a from-scratch clone
	@$(MAKE) --no-print-directory titlescreen  # the front end's 5TRSCRN page (git-ignored: it is
	                                           # a kilobyte of the disc, like revs_tracks.c)

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
	      tools/validate_sound.o build/validate_sound \
	      tools/validate_tracks.o build/validate_tracks
	rm -f $(OBJS:.o=.d) tools/*.d

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

# ⭐⭐ THE ONE GATE ON A HOOK SEAM'S PATCHED ARM.  `validate`, `determinism` and `-drive` all race
# SILVERSTONE, and Silverstone patches nothing; `tracks` proves an expansion circuit's bytes land
# and `track-run` proves its code runs — neither that it COMPUTES.  This races the same parked
# scene on a real BBC and on the port and compares the frame buffer byte for byte.
#
#   make viewdiff                  every circuit this build has a real-BBC counterpart for
#   make viewdiff CIRCUITS="0 1"   just those (PORT indices: 0 = Silverstone)
#   make viewdiff BBCFRAMES=40 FRAME=60
#
# ⭐ PARKED on both sides, and that is the whole reason a comparison is possible: a moving car
# diverges on the first input frame and every byte then differs for reasons that are not bugs.
# --park leaves the engine running in first gear and touches nothing, which is exactly the state
# src/platform/autorun.cpp parks the port in, so the scene is static and frame alignment stops
# mattering.  Only display lines 82+ are gated — the text rows and the sky band above them carry
# the clocks (which do differ) and, in the sky, live code.
# ⚠ It leaves the tree in the default configuration for the same reason `track-run` does.
BBCFRAMES ?= 40
viewdiff:
	@set -e; mkdir -p tmp; \
	list="$(if $(CIRCUITS),$(CIRCUITS),0 1 2 3 4)"; \
	frame=$(if $(FRAME),$(FRAME),60); fails=0; \
	for t in $$list; do \
	  case $$t in 0) bt=5;; *) bt=$$t;; esac; \
	  (cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
	      --frames=$(BBCFRAMES) --track=$$bt --wing=$(WING) --park --dump=tmp/viewdiff_$$t) \
	      >tmp/viewdiff_$$t.log 2>&1 || { echo "  FAIL circuit $$t: the BBC run died (tmp/viewdiff_$$t.log)"; fails=1; continue; }; \
	  bbc=$$(ls tmp/viewdiff_$$t/bbc_fb_*.bin | tail -1); \
	  $(MAKE) --no-print-directory clean >/dev/null; \
	  $(MAKE) --no-print-directory STRAIGHT_TO_RACE=1 TRACK=$$t >/dev/null; \
	  REVS_SCREEN_DUMP=tmp/viewdiff_port_$$t.bin REVS_SCREEN_FRAME=$$frame \
	      REVS_QUIT_AFTER_DUMP=1 timeout $(if $(TIMEOUT),$(TIMEOUT),300) ./build/revs 2>&1 | tail -1 | sed -n 's/^/  /p'; \
	  echo "  circuit $$t:"; \
	  st=0; python3 tools/fb_diff.py $$bbc tmp/viewdiff_port_$$t.bin >tmp/viewdiff_$$t.diff 2>&1 || st=$$?; \
	  sed -n 's/^/    /p' tmp/viewdiff_$$t.diff; \
	  [ $$st = 0 ] || { echo "    FAIL circuit $$t: the view differs from the real BBC"; fails=1; }; \
	done; \
	$(MAKE) --no-print-directory clean >/dev/null; \
	$(MAKE) --no-print-directory $(TARGET) >/dev/null; \
	[ $$fails = 0 ] || { echo "viewdiff: a circuit's VIEW differs from the real BBC"; exit 1; }; \
	echo "viewdiff: every circuit's view matches the real BBC over display lines 82+"

# ⭐⭐ THE STORE CENSUS — every frame-buffer write a REAL BBC makes, attributed to the routine that
# made it, over the whole picture.  This is the measurement docs/direct-bitplane-plan.md §3's layout
# choice was deferred behind: a direct plotter's cost is STORES, and the port's own shape counters
# are snapshot diffs that can only see CHANGES (a re-plotted identical span costs full price and
# shows up as nothing).  The report prints both, per routine.
#   make fbwrites                    lines 0..207, frames 9..23 of a driving Silverstone practice
#   make fbwrites FILL=80-165 FILLFRAMES=30-40    just the road band, a later window
FILL ?= all
FILLFRAMES ?= 9-23
fbwrites:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
	    --frames=$(FRAMES) --track=$(TRACK) --wing=$(WING) --drive \
	    --fill=$(FILL) --fill-frames=$(FILLFRAMES)

# Is key injection working at all?  Verified at the BASIC prompt, where success is VISIBLE —
# never through the game, where a silent no-op and a rejected value look identical.
refloop-keys:
	cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_return.mjs

# The entry-point sweep report (docs/entrypoint-sweep.md, docs/static-map.md).
#   make sweep                       -> disasm/sweep.txt
#   make sweep TRACE=tmp/trace_SILVER.bin  also cross-check against a real execution trace
sweep:
	python3 tools/sweep_entrypoints.py --audit-smc $(if $(TRACE),--trace $(TRACE),) > disasm/sweep.txt
	@echo "wrote disasm/sweep.txt"
	@sed -n '1,5p' disasm/sweep.txt
