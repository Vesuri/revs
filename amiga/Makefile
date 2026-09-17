# Revs — Amiga cross-build (m68k-amiga-elf-gcc)
#
# Usage (from amiga/ with the toolchain on PATH via `. env.sh`):
#   make                 build out/Revs.exe
#   make clean           remove obj/ and out/
#   make PROBES=1        + headless auto-run and the timing probes (diag_run.sh needs this)
#   make FPSCOUNT=1      the HONEST framerate build: one counter, no probe instrumentation
#   make STRAIGHT_TO_RACE=1  ⭐ boot into a Silverstone practice session, engine running,
#                        then hand the keyboard to the player — the only way to SEE this
#                        port, since the MODE 7 front end is not rendered
#   ./run.sh             boot in FS-UAE (left mouse button quits)
#   ./debug.sh           source-level debug via the FS-UAE GDB stub
#   ./diag_run.sh [secs] headless probe run (needs PROBES=1)
#
# ⚠⚠ `make clean` BEFORE any PROBES=1 build and AFTER editing a widely-included header.
# This Makefile tracks neither the flag nor header deps, so a partial rebuild links stale
# objects into a WORKING-BUT-WRONG binary with silent runtime breakage.  On the Atari port
# that trap cost multiple debugging sessions: treat any unexplained regression right after
# a header edit or a PROBES toggle as a stale build FIRST.  (docs/headless-fsuae.md)
#
# Structure mirrors the repo-root host Makefile: explicit source lists mapped to objects
# in a flat obj/ (keyed by basename — keep basenames unique across the source trees),
# sources located via vpath.

OUT      = out/Revs
CC       = m68k-amiga-elf-gcc
AS       = m68k-amiga-elf-as
VASM     = vasmm68k_mot
OBJDUMP  = m68k-amiga-elf-objdump
SDKDIR   = $(abspath $(dir $(shell which $(CC)))../m68k-amiga-elf/sys-include)
SUPPORT  = $(abspath $(dir $(shell which $(CC)))../../support)

# Repository root (parent of amiga/) and the Amiga source tree.  This directory holds
# only build infrastructure (this Makefile, env.sh, run.sh, the diag scripts, assets/).
REPO      = ..
AMIGA_SRC = $(REPO)/src/platform/amiga

$(shell mkdir -p obj out)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# C++: the vendored dA JoRMaS hardware framework + the Revs app + the shared platform layer.
CXX_SRCS := \
    $(AMIGA_SRC)/framework/AmigaHardware.cpp \
    $(AMIGA_SRC)/framework/Bitmap.cpp \
    $(AMIGA_SRC)/framework/CopperList.cpp \
    $(AMIGA_SRC)/framework/Palette.cpp \
    $(AMIGA_SRC)/framework/Sprite.cpp \
    $(AMIGA_SRC)/framework/Util.cpp \
    $(AMIGA_SRC)/GCCRuntime.cpp \
    $(AMIGA_SRC)/PlatformAmiga.cpp \
    $(AMIGA_SRC)/Revs.cpp \
    $(AMIGA_SRC)/RevsScreen.cpp \
    $(AMIGA_SRC)/RevsPlot.cpp \
    $(AMIGA_SRC)/RevsInput.cpp \
    $(AMIGA_SRC)/RevsAudio.cpp \
    $(REPO)/src/platform/Platform.cpp \
    $(REPO)/src/platform/mos.cpp \
    $(REPO)/src/platform/probe.cpp \
    $(REPO)/src/platform/shape.cpp \
    $(REPO)/src/platform/bbc_hw.cpp \
    $(REPO)/src/platform/teletext.cpp \
    $(REPO)/src/platform/autorun.cpp \
    $(REPO)/src/platform/platform_cbridge.cpp \
    $(REPO)/src/main.cpp

# C: the transpiled 6502 core — cpu/bus emulation, the generated transliteration
# (revs_gen), the hand-written native twins (revs_native), and manual stubs for
# self-modifying routines (revs_manual).  The generated files do not exist until
# `make gen` at the repo root, so they are wildcarded.
CORE_C_SRCS := \
    $(REPO)/src/cpu/cpu.c \
    $(REPO)/src/platform/sound.c \
    $(REPO)/src/platform/track.c \
    $(REPO)/src/platform/trackmenu.c \
    $(wildcard $(REPO)/src/gen/revs_tracks.c) \
    $(wildcard $(REPO)/src/gen/revs_gen.c) \
    $(wildcard $(REPO)/src/gen/revs_track_hooks.c) \
    $(wildcard $(REPO)/src/gen/revs_native.c) \
    $(wildcard $(REPO)/src/gen/revs_native_seam.c) \
    $(wildcard $(REPO)/src/gen/revs_native_abi.c) \
    $(wildcard $(REPO)/src/gen/revs_manual.c)

# Support runtime (freestanding CRT: program entry, memset…) + incbin'd assets.
SUPPORT_C_SRCS := $(SUPPORT)/gcc8_c_support.c
ASM_SRCS       := $(SUPPORT)/gcc8_a_support.s $(AMIGA_SRC)/incbin.s

# Framework hand-written m68k asm (Motorola syntax, assembled by vasm -> ELF).  The
# matching GCC register-marshalling bridges live under
# `#if defined(ASSEMBLER) && !defined(__SASC)` in the corresponding framework .cpp.
FRAMEWORK_ASM_SRCS := \
    $(AMIGA_SRC)/framework/UtilAssembler.s \
    $(AMIGA_SRC)/framework/AmigaHardwareAssembler.s \
    $(AMIGA_SRC)/framework/BitmapAssembler.s \
    $(AMIGA_SRC)/framework/CopperListAssembler.s

# ---------------------------------------------------------------------------
# Build flags / feature toggles
# ---------------------------------------------------------------------------
# Every hand-asm twin gets a seam here as it is written:
#
#   ifndef <NAME>_C
#   FRAMEWORK_ASM_SRCS += $(AMIGA_SRC)/<Name>Assembler.s
#   EXTRA_DEFINES += -DREVS_<NAME>_ASM
#   ifdef VERIFY
#   EXTRA_DEFINES += -DREVS_<NAME>_VERIFY
#   obj/<Name>Assembler.o : VASMFLAGS += -DREVS_<NAME>_VERIFY
#   endif
#   endif
#
# `make <NAME>_C=1` falls back to the clean-C oracle; `make VERIFY=1 PROBES=1` runs the
# asm and the C oracle back-to-back on identical state EVERY CALL and compares, read via
# amiga/<name>_verify.gdb.  ⭐ That in-process differential is the ONLY valid way to price
# an asm twin — a cross-run comparison changes the trajectory and measures a different
# workload.  See docs/perf-method.md.

# `make PROBES=1` — headless auto-run + the sub-frame timing accumulators, read via the
# gdb stub (amiga/diag_run.sh + diag_timing.gdb).  OFF by default: the probes read chip
# registers several times per iteration, which is exactly what a framerate build must not
# do.  Never quote a framerate from a PROBES build.
ifdef PROBES
EXTRA_DEFINES += -DREVS_PROBE
endif

# ⭐⭐⭐ `make PROBEFIELDS=N PROBES=1` — BOUND THE MEASUREMENT IN EMULATED TIME, not host time.
# diag_run.sh's window is `sleep <wall seconds>`, and under warp the host's throughput depends on
# what else the machine is doing, so two arms of one A/B can cover very different amounts of GAME
# time (measured: 75 s vs 144 s out of the same 30 s wall window) — and since a STRAIGHT_TO_RACE
# run leaves the track and resets, the longer arm is diluted with a different scene mix.  With
# this set, the phase table, its counts AND g_phaseFrames freeze after N display FIELDS, so every
# arm describes the same emulated window frame for frame.
#   make clean && make -j4 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 PROBEFIELDS=3000   # 60 s of game
#   GDBTAIL=200 EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 60
# ⚠ The wall delay must be long enough for the SLOWEST arm to reach N; `g_probeFrozen` (printed in
# the phase4_prof.gdb header) is the proof it did, and a table with `frozen=0` is an ordinary
# wall-clock sample that may NOT be compared arm to arm.  src/platform/probe.cpp has the reasoning.
# ⭐ Its VALUE is the window in beam ticks and must equal N * 80120 (a verification run read
# 240521225 against 240360000 for N=3000 — 0.07%), which is a second, independent check that the
# freeze fired on the right field and the 4006-ticks/ms phase clock is calibrated.
# ⚠⚠ THE PARTIAL-FREEZE TRAP: the freeze stops the PHASE accumulators, not the program, so every
# counter bumped elsewhere (the body drain, the ISR, the view census, g_vbiCount, g_beamEpoch)
# keeps climbing and any row mixing the two is FICTION — `ONE BODY TICK` read 379 us against a true
# 1414 and the view census 5526 units/frame against 1440, both plausible enough to survive a
# review.  probe.cpp snapshots what it cannot stop and phase4_prof.gdb reads the snapshot whenever
# g_probeFrozen is set; if you add a row to that script, pick $wall/$body/$fields/$u/$r/$l and
# never the live global.  docs/perf-method.md §the partial-freeze trap.
ifdef PROBEFIELDS
EXTRA_DEFINES += -DREVS_PROBE_FIELDS=$(PROBEFIELDS)
endif

# `make VIEWSPLIT=1 PROBES=1` — split phase 24 ($7BE2) into the 2093-unit chain (phase 30) and
# the ~77 lines of driver around it (whatever stays in 24), with an empty bracket at the same
# rate as the control (phase 31).  src/platform/probe.h has the reasoning.  Measurement only:
# two bracket transitions per scan line, so no framerate may be quoted from it.
ifdef VIEWSPLIT
EXTRA_DEFINES += -DREVS_VIEWSPLIT
endif

# `make VIEWP3=1 PROBES=1` — split PHASE 3's per-line driver (phase 34) into its four pieces, with
# an empty bracket at the same rate as the control (phase 31).  Five transitions per line over 25
# lines, so read phase 31 BEFORE any of 35-38.  src/platform/probe.h §PROBE_PHASE_P3_*.
ifdef VIEWP3
EXTRA_DEFINES += -DREVS_VIEWP3
endif
# `make VIEWP3=2` — ⚠⚠ THE PICTURE IS WRONG BY CONSTRUCTION.  Phase 3's line loop keeps its 25
# iterations and loses its body, so phase 34 is the bare loop: the differential that says whether
# the four-way split's milliseconds are in that body at all.  Never quote a framerate from it.
ifeq ($(VIEWP3),2)
EXTRA_DEFINES += -DREVS_VIEWP3_EMPTY
endif
# `make VIEWP3=3` — ⚠⚠ same warning: every computed chain entry (phases 2 and 3) is validated and
# then not run, so the difference against the shipping row prices those chain runs with no bracket
# anywhere in the measurement.
ifeq ($(VIEWP3),3)
EXTRA_DEFINES += -DREVS_VIEWP3_NOCHAIN
endif
# ⭐⭐ `make VIEWCAL=1 PROBES=1` — the CALIBRATION.  14 000 known 68000 cycles per phase-3 line in
# their own bracket (phase 39), so the run reports how many microseconds a known cycle count reads
# as.  Nothing else in this project converts a bracket into cycles.  src/platform/probe.h.
# ⭐ `VIEWCAL=N` burns N x 14 000 cycles, which is how the instrument is VERIFIED: the row must
# scale linearly in N, or the burn is not what is being measured.
ifdef VIEWCAL
EXTRA_DEFINES += -DREVS_VIEWCAL=$(VIEWCAL)
endif

# ⭐⭐ `make GEOSPLIT=1 PROBES=1` — split build_track_geometry (phase 5) into its sub-phases
# (ids 40-43: the near-point reuse, the two distance walks, the horizon tail) AND count the tree's
# transforms/divides per frame.  The "why is phase 5 ~16%" instrument — src/platform/probe.h
# §GEOSPLIT.  Four bracket transitions a frame, so quotable.  Read with amiga/geosplit.gdb.
ifdef GEOSPLIT
EXTRA_DEFINES += -DREVS_GEOSPLIT
PROBE_SYMS_GEO = g_geoFrames g_geoPoints g_geoSubdiv g_geoBearing g_geoProject g_geoDiv g_geoHypot
endif

# ⭐⭐ `make ROADSPLIT=1 PROBES=1` — split draw_road (phase 11) into its three stage types
# (ids 44-46: fill_line_attr / draw_surface_spans / mark_line_surfaces) AND count the tree's
# leaf work per frame.  The "why is phase 11 ~16%" instrument — src/platform/probe.h §ROADSPLIT.
# Six bracket transitions a frame, so quotable.  Read with amiga/roadsplit.gdb.
ifdef ROADSPLIT
EXTRA_DEFINES += -DREVS_ROADSPLIT
PROBE_SYMS_ROAD = g_roadFrames g_roadSpans g_roadSpanLines g_roadCols g_roadColSteps g_roadFillLines g_roadMarkPts
endif

# ⭐ `make CRASHPROBE=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` — measure the ~5s crash-freeze.
# Cheap counters (a few adds per field / per crash) so it rides a near-shipping build for a
# faithful timing; add PROBES for the direct per-field beam cost (g_fieldBeam*).  The counters
# themselves are defined unconditionally in Revs.cpp, so they are in the BASE PROBE_SYMS list
# and need no conditional entry here.  Read with amiga/crashprobe.gdb.  src/platform/probe.h.
ifdef CRASHPROBE
EXTRA_DEFINES += -DREVS_CRASHPROBE
endif

# ⭐⭐ `make VIEWSKIP=1` — the view sweep SKIPS a scan line whose forty sources are all clean,
# whose display line still holds the background byte, and whose last paint was flat.  39% of
# line-visits while driving on the host; this is the switch that turns that into a framerate.
# ⚠ It is gated ONLY by the host differentials (determinism / -drive / -crash / -steer / -race
# and viewdiff) — `make validate` cannot see it.  Read the effect off g_viewSkipLines /
# g_viewSkipPaints; docs/direct-bitplane-plan.md §7i.
ifdef VIEWSKIP
EXTRA_DEFINES += -DREVS_VIEWSKIP
PROBE_SYMS_VIEWSKIP = g_viewSkipLines g_viewSkipPaints
endif

# ⭐⭐⭐ `make VIEWOWN=1` — PHASES 2 AND 3 OWN THEIR RUNS (docs/span-render-plan.md §10p step 4).
# The short phases ask `paint_cells` for ONE run of ONE line, four times a line, through
# `view_enter_chain`'s synthesised 6502 address; this replaces those four entries with
# `view_own_run` — the run, the stop tail and the terminator, and nothing else.  The plants, the
# stop list, `view_consume` and every trap arm are the SAME code, so it is byte-exact by
# construction and `make validate FN=view_paint_lines` + the five `determinism` trajectories
# gate it directly.  It is the step that has to pay before the runs' stores go to the bitplanes.
ifdef VIEWOWN
EXTRA_DEFINES += -DREVS_VIEW_OWN_SHORT
endif

# ⚠⚠ `make VIEWCARVE=1` — THE PICTURE IS WRONG BY CONSTRUCTION, and it prices ONE NUMBER: what
# the decode spends on the view sweep's SHORT phases.  Display lines 117..157 (phase 2's
# 117..132 + phase 3's 133..157, measured in §10p step 2) are claimed as `m_lineMode = 0` and
# nothing paints them, so the dash and the near road freeze.  ph27's delta is the CEILING of
# the phases-2/3 bitplane flip, because `093e560` measured the store half as a wash — a
# bitplane pair costs about what the `mem[]` byte it replaces cost, so the decode carve-out is
# the whole prize.  Run it BEFORE writing the painter; it is one build and one run against a
# `SPANFILL=5 VIEWOWN=1` base.
# ⚠ A ceiling, not the achievable figure: a per-run takeover owns two cell RANGES a line, so
# the cells between the runs still need converting.  Never quote it as a saving.
ifdef VIEWCARVE
EXTRA_DEFINES += -DREVS_VIEW_CARVE
endif

# `make NOUNITS=1` — ⚠⚠ THE PICTURE IS WRONG BY CONSTRUCTION.  The $7BE2 unit loop keeps its
# iterations and loses its memory work, so `phase 24 with this on` is the per-line drivers plus
# the bare loop.  A decomposition the VIEWSPLIT brackets cannot give honestly: bracketing 118
# chain runs a frame costs more than the code inside them.  Never quote a framerate from it.
ifdef NOUNITS
ifeq ($(NOUNITS),3)
EXTRA_DEFINES += -DREVS_NO_UNIT_STORE
else
EXTRA_DEFINES += -DREVS_NO_UNIT_WORK
ifeq ($(NOUNITS),2)
EXTRA_DEFINES += -DREVS_NO_UNIT_LOOP
endif
endif
endif

# `make FPSCOUNT=1` — the HONEST framerate build.  Adds exactly two things to an
# otherwise shipping binary: the headless auto-run, and ONE increment per painted frame.
# g_vbiCount is bumped by the real VERTB handler in every build, so
#     FPS = 50 * g_fpsFrames / g_vbiCount
# — frames per EMULATED vblank, immune to host speed and to the gdb stub.
# Run: . ./env.sh && GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200
ifdef NODECODE
EXTRA_DEFINES += -DREVS_SCREEN_NO_DECODE
endif
ifdef NOBANDS
EXTRA_DEFINES += -DREVS_SCREEN_NO_BANDS
endif
# `make BCDSELFTEST=1 PROBES=1` — sweep src/cpu/bcd.h's ABCD/SBCD paths over all 100x100x2
# valid-BCD inputs on the real 68000 at startup, and compare against the digit algorithm.
# Read with amiga/bcd_selftest.gdb.  Verification only: never in a shipping or perf build.
ifdef BCDSELFTEST
EXTRA_DEFINES += -DREVS_BCD_SELFTEST
PROBE_SYMS_BCD = g_bcdCases g_bcdAddFails g_bcdSubFails g_bcdFirstBad
endif

ifdef FPSCOUNT
EXTRA_DEFINES += -DREVS_FPSCOUNT
endif

# `make STRAIGHT_TO_RACE=1` — boot straight into a Silverstone PRACTICE session with the
# engine running, then hand the keyboard back to the player.
#
# It is NOT a state-seeding hack: front_end_menus ($63E0) runs in full, and the build only
# answers the ONE menu question the practice path has ($63F7 `1 PRACTICE 2 COMPETITION`)
# the instant it is asked, plus 'T' for the starter motor.  Everything else the front end
# would ask — class, qualifying duration, driver names, ANOTHER/START — is on the
# COMPETITION branch and genuinely never reached.  src/platform/autorun.cpp has the trace.
#
# Why it exists: out of a race the game is in MODE 7 teletext, which this port does not
# render, so a plain build is a black screen you cannot navigate.  This is how you SEE the
# port.  Combine with FPSCOUNT=1 for a perf run whose window is a MOVING car — ⚠ which is
# a different workload from every published baseline (those were measured with the engine
# off); do not diff across that boundary.
ifdef STRAIGHT_TO_RACE
EXTRA_DEFINES += -DREVS_STRAIGHT_TO_RACE
endif

# ⭐ `make COMPETITION=1` — boot into a COMPETITION race instead of a practice session, i.e. the
# one that puts a FIELD OF NINETEEN OTHER CARS on the circuit.  Practice runs the player alone,
# so every frame this port has ever measured is structurally silent about competitor-car
# rendering; this is the build that has a stimulus for it.  src/platform/autorun.cpp has the menu
# chain.  ⚠ Takes precedence over STRAIGHT_TO_RACE in autorun.cpp (it is the first #if arm), and
# like every other script flag it needs `make clean` when toggled.
# ⭐ `make TRACK=n` — boot a circuit other than Silverstone.  See src/platform/track.h; an
# unfinished circuit is refused and the build falls back, with g_trackUnhonoured saying why.
ifdef TRACK
EXTRA_DEFINES += -DREVS_TRACK_DEFAULT=$(TRACK)
endif

ifdef COMPETITION
EXTRA_DEFINES += -DREVS_COMPETITION
endif

# ⭐ `make NOAUTORUN=1` — compile the probes WITHOUT the scripted keyboard, so the run stays in
# the front end.  The only way to measure the front end unattended: every other probe flag
# implies the script, and the script answers the first menu in microseconds.
ifdef NOAUTORUN
EXTRA_DEFINES += -DREVS_NOAUTORUN
endif

# ⭐⭐ `make TAPTEST=1 NOAUTORUN=1 PROBES=1` — drive the front end with taps of NO LENGTH, which
# only the tap latch can answer (RevsInput.cpp §THE PROOF).  `NOLATCH=1` is its sabotage arm:
# with the latch compiled out the same build must stall on the circuit menu.
ifdef TAPTEST
EXTRA_DEFINES += -DREVS_TAPTEST
endif
ifdef NOLATCH
EXTRA_DEFINES += -DREVS_NOLATCH
endif

# `make HOLD_THROTTLE=1` — keep the throttle held after the script ends instead of handing the
# keyboard over.  FPSCOUNT and PROBES do this implicitly; this flag is for a diagnostic build that
# wants neither.  ⚠ It matters more than it looks: with the keyboard handed over the car is PARKED
# and the horizon is byte-identical frame after frame (measured: g_horizonChangeSeries all zero over
# 240 painted frames), so a run like that never exercises the fill at all and a detector on it
# reports a confident clean.  The host Makefile has had the same flag since the $FC hunt.
ifdef HOLD_THROTTLE
EXTRA_DEFINES += -DREVS_HOLD_THROTTLE
endif

# ⭐ `make HOLD_STEER=l` / `HOLD_STEER=r` — hold a STEERING key beside the throttle, so an
# unattended run actually TURNS THE WHEEL.  Compile-time here because the target has no
# environment to read; the host takes the same thing at run time as REVS_HOLD_STEER.  It exists
# because the whole steering chain was once dead in the port and NO gate could see it: a
# throttle-only autorun drives in a straight line, so the steering angle stayed 0 either way.
# ⚠ Read it as two runs with OPPOSITE keys and require the sign bits to differ — one run showing
# a moving wheel cannot tell steering from the slip-cancelling self-drive demand.
ifeq ($(HOLD_STEER),l)
EXTRA_DEFINES += -DREVS_HOLD_STEER_LEFT -DREVS_HOLD_THROTTLE
endif
ifeq ($(HOLD_STEER),r)
EXTRA_DEFINES += -DREVS_HOLD_STEER_RIGHT -DREVS_HOLD_THROTTLE
endif

# `make FIXED_RNG=1` — pin any RNG seeding so every build drives the SAME simulation.
# On the Atari port, render speed shifted the free-running main loop against the 50 Hz
# interrupt, which shifted the RNG read count, which selected a DIFFERENT level — the
# reason cross-build timing numbers were never comparable.  Revs is a fixed circuit, so
# the equivalent risk is the driving line and the opponent cars: pin them before quoting
# any per-iteration cost.  OFF by default (it removes real variety — never judge
# rendering or gameplay from a FIXED_RNG build).
ifdef FIXED_RNG
EXTRA_DEFINES += -DREVS_FIXED_RNG
endif

# ⭐ `make SHAPE=1` — the render path's INPUT-DISTRIBUTION counters (src/platform/shape.h),
# read with amiga/dash_shape.gdb.  Phase 6 item 0 step 2.  ⚠ A DIAGNOSTIC build: the dashboard
# sweep's rectangle is re-scanned twice per frame (2880 mem[] reads) and every column unit takes a
# counter increment, so no framerate may be quoted from it.  Pair it with PROBES=1 — the point is
# to put the shape numbers and the phase share in the SAME run.
# `make HWTIME=1` (with PROBES) — count and time every BBC hardware access.  ⚠⚠ OBSERVER EFFECT
# OVER 2x on the row it measures: the counters alone took the body tick from 10.5 to 12.3 ms and the
# timing to 22.7 ms.  Use it to answer "how many, how expensive", never to read a share or a rate.
ifdef HWTIME
EXTRA_DEFINES += -DREVS_PROBE_HWTIME
endif

# ⭐⭐ `make ISRSPLIT=1 PROBES=1` — split the VERTB handler into its seven jobs, with an
# empty-bracket NULL CONTROL in slot 0.  The ISR is a fixed 50 Hz tax that no phase row can
# see, so this is the only instrument that prices it.  src/platform/probe.h §ISRSPLIT.
# ⚠ ~8 bracket transitions per field: quote the SPLIT from it, never an FPS or a phase share.
# Read with amiga/isr_split.gdb.
ifdef ISRCAL
EXTRA_DEFINES += -DREVS_ISRCAL
endif

ifdef ISRSPLIT
EXTRA_DEFINES += -DREVS_ISRSPLIT
PROBE_SYMS_ISRSPLIT = g_isrSplitTicks g_isrSplitCount g_sndProgramRuns g_sndProgramSkips \
                      g_sndEnvSteps g_sndChanVisits
endif

ifdef SHAPE
EXTRA_DEFINES += -DREVS_SHAPE
PROBE_SYMS_SHAPE = g_shapeDashCalls g_shapeDashDirty g_shapeDashLeft g_shapeDashCols \
                   g_shapeDashColsLeft g_shapeDashLastDirty g_shapeDashLastLeft \
                   g_shapeDashLastCols g_shapeDashColHist g_shapeDashPerCol \
                   g_shapeDashPerRow g_shapeDashUnits g_shapeDashLastUnits \
                   g_shapeRoadCalls g_shapeRoadBytes g_shapeRoadLines \
                   g_shapeRoadLastBytes g_shapeRoadLastLines \
                   g_shapeRoadFirstLine g_shapeRoadLastLine \
                   g_shapePhaseBytes g_shapePhaseFrames g_shapePhaseFirst g_shapePhaseLast \
                   g_shapeFrameCalls g_shapeFrameBytes g_shapeFrameLast g_shapeFrameMax \
                   g_shapeFrameFirstLine g_shapeFrameLastLine g_shapeFrameLines \
                   g_shapeLineSweeps g_shapeLineVisited g_shapeLineRedundant \
                   g_shapeLineCleanSrc g_shapeLineUnits g_shapeLineUnitsRedundant \
                   g_shapeLineUnitsCleanSrc g_shapeLineCleanButChanged \
                   g_shapeLineDirtyNoChange g_shapeLinePerVisit g_shapeLinePerRedundant \
                   g_shapeLinePerUnits \
                   g_shapeRunSweeps g_shapeRunStores g_shapeRunEvents g_shapeRunChanged \
                   g_shapeRunUnion g_shapeRunRuns g_shapeRunExtent g_shapeRunLinesAny \
                   g_shapeRunLinesNone g_shapeRunUnionHist g_shapeRunRunsHist \
                   g_shapeRunPerUnion g_shapeRunArmLost
endif

# `make FLATSKIP=0` — decode EVERY display line, including the ones inside a palette band whose
# four colour registers are identical (the BBC's band 1: 63 lines of flat blue hiding live engine
# code).  ON by default; this is the A/B build for pricing it, and the fallback if a band ever
# turns out to be flat in the record but not on screen.  See RevsScreen.cpp g_decodeFlatLines and
# docs/direct-bitplane-plan.md §4a.
ifeq ($(FLATSKIP),0)
EXTRA_DEFINES += -DREVS_NO_FLATSKIP
endif

# `make DIRTY=0` — convert EVERY cell of the frame buffer, instead of only the ones whose eight
# source bytes changed since this buffer was last decoded.  ON by default; measured 4.9% of the
# buffer changes per painted frame (docs/direct-bitplane-plan.md §7b), so this is the control build
# for pricing it.  ⭐ The switch PRINTS its own state without needing a define of its own:
# g_decodeCells reads 1040 of 1040 every frame here and well under it in the shipping build, which
# is the lesson from the A/B whose control was silently rebuilt as the test (docs/method-lessons.md).
ifeq ($(DIRTY),0)
EXTRA_DEFINES += -DREVS_NO_DIRTY
endif

# `make DIRTYCHECK=1` — the dirty decode's ORACLE.  Every painted frame, re-run the conversion
# UNCONDITIONALLY into a copy of the buffer the dirty pass just wrote and require byte equality:
# g_decodeDirtyMismatch must be 0 over a run in which g_decodeDirtyChecks is large.  16 KB of extra
# BSS and a whole second conversion per frame, so it is a diagnostic build — never quote FPS from
# it.  Read with amiga/dirty_decode.gdb.
ifeq ($(DIRTYCHECK),1)
EXTRA_DEFINES += -DREVS_DIRTYCHECK
endif

# ⭐⭐ `make DIRECTPLOT=1` — the 3D view rasteriser ($7BE2) plots RUNS straight into the bitplane
# buffer as well as writing mem[] (src/platform/revs_plot.h, docs/direct-bitplane-plan.md §7e).
# ⚠ In this first form it is REDUNDANT work, not a saving: the mem[] stores stay, so the picture
# cannot break and the run plotter can be proven byte-exact before anything depends on it.
ifeq ($(DIRECTPLOT),1)
EXTRA_DEFINES += -DREVS_DIRECT_PLOT
PROBE_SYMS_PLOT = g_plotRuns g_plotCells g_plotRunsLast g_plotCellsLast \
                  g_plotLineLo g_plotLineHi g_plotNoTarget
endif

# `make DIRECTPLOT=1 DIRECTCHECK=1` — the ORACLE for it (§5).  Brackets the sweep: convert mem[]
# with the shipping decode and seed the buffer, let the sweep plot over it, convert again and
# require the WHOLE buffer to be identical.  g_plotMismatch must be 0 with g_plotChecks large.
# Two full conversions per sweep — never quote a framerate from this build.
# ⚠ `make PLOTONLY=1` — the END STATE, and a MEASUREMENT build until the digits are handled: the
# view rasteriser stops writing mem[] for its own region and the decode stops converting those
# lines, so the whole point (fewer stores, less decode) is in the number.  It loses vdu_char_def's
# digits, which compose against mem[] — see §7e.  The car's trajectory is unaffected either way,
# because the visible frame buffer is write-only (measured, `make fbwrites --fill-reads`), which is
# what makes this a clean FPS comparison against a build that draws them.
ifeq ($(PLOTONLY),1)
EXTRA_DEFINES += -DREVS_DIRECT_PLOT -DREVS_PLOT_ONLY
PROBE_SYMS_PLOT = g_plotRuns g_plotCells g_plotRunsLast g_plotCellsLast \
                  g_plotLineLo g_plotLineHi g_plotNoTarget
endif

# ⛔⛔ `make SPANEMIT=1` — SUPERSEDED AS A TIMING BUILD BY `SPANFILL=1` BELOW, AND KEPT ONLY AS THE
# BYTE ORACLE.  It is additive by construction: `PLOT_UNIT` mirrors every chain store, so the sweep
# runs in full and the plotting is ADDED on top (+54 ms, and the number says nothing about the
# architecture).  Quote a timing from SPANFILL, never from here.  docs/span-render-plan.md §10n.
#
# What it still does, and what it is for: a scan line that no producer wrote and that carries no
# planted stop is ONE run of its background byte, so the emitter writes it as a single span straight
# into the bitplanes.  ~21 of phase 1's 36 lines qualify, carrying 37% of the sweep's 2155 cell
# stores.  ⚠ The census share is NOT the saving — SPANFILL measured 37% of the stores buying 9.6%
# of the sweep, because a per-line hook-in deletes the unit loop and phase 1's bracket is 905
# cyc/line of DRIVER plus units.  §10p's takeover is what deletes the driver.
# ⚠ It implies PLOTONLY's carve-out: the emitted lines are NOT written to mem[], so the decode
# must not convert them.  Validate with SPANEMIT=1 DIRECTCHECK=1 (g_plotMismatch must stay 0
# with g_plotChecks large) before believing anything from it.
# ⭐⭐ `make SPANEMIT=1 SPANVERIFY=1 DIRECTCHECK=1` — the emitter's ORACLE.  Verify mode keeps the
# forty-unit chain running (so mem[] stays true and the decode is a valid reference) and turns the
# per-unit run accumulator OFF, so every plane byte on an emitted line comes from the span emitter
# alone.  Without that the chain re-plots the same cells over the span and a WRONG span compares
# equal.  ⚠ It also drops PLOT_ONLY, because a carved-out line leaves mem[] stale and the oracle
# would then be comparing against last frame.  Diagnostic build: never quote a timing from it.
ifeq ($(SPANVERIFY),1)
EXTRA_DEFINES += -DREVS_SPAN_VERIFY
endif

ifeq ($(SPANEMIT),1)
ifeq ($(SPANVERIFY),1)
EXTRA_DEFINES += -DREVS_SPAN_EMIT -DREVS_DIRECT_PLOT
else
EXTRA_DEFINES += -DREVS_SPAN_EMIT -DREVS_DIRECT_PLOT -DREVS_PLOT_ONLY
endif
PROBE_SYMS_PLOT = g_plotRuns g_plotCells g_plotRunsLast g_plotCellsLast \
                  g_plotLineLo g_plotLineHi g_plotNoTarget
PROBE_SYMS_SPAN = g_spanEmitLines g_spanEmitPaints
endif

# ⭐⭐⭐ `make SPANFILL=1` — THE VALID §10e CHEAP CHECKPOINT, i.e. what SPANEMIT's scaffold was
# MEANT to be (docs/span-render-plan.md §10L).  Same predicate and same span as SPANEMIT, and
# three deletions that are the whole difference:
#   1. NO MIRROR.  SPANEMIT's `PLOT_UNIT` plotted every chain store on every line the emitter did
#      not paint — the ⛔ mirror-each-store plotter §10c closed at -9%, and the larger half of the
#      scaffold's +54 ms.  Here PLOT_UNIT and the phase-2/3 boundary cells compile to NOTHING, so
#      an owned line comes from its span alone and every other line from mem[] as today.
#   2. NO WHOLE-ROW CARVE-OUT.  Ownership is published PER DISPLAY LINE (`g_plotOwn`) and spelled
#      as `m_lineMode[y] = 0`, which the decode already reads as "write nothing".  PLOT_ONLY's
#      `g_plotLineLo/Hi` row carve-out cannot express it — a BBC cell is eight display lines — and
#      is NOT used here, so mem[] stays true everywhere the emitter did not paint.
#   3. THE PLOT TARGET MOVES TO present().  Aimed from decode() the sweep plotted into the buffer
#      just presented; the flip happens between the two (RevsScreen::present()).
# ⭐ WHAT IT IS FOR: rows 81-100 are 20 single-run rows carrying 37% of the sweep's 2155 stores.
# If 37% of them do not come off phase 1's bracket roughly in proportion, §10e's model is wrong and
# almost nothing has been spent finding out.  Measure phase 24 AND the decode against a clean
# PROBES=1 control in the SAME session.
# ⭐⭐ `make SPANFILL=1 SPANVERIFY=1 DIRECTCHECK=1` — the span bytes' ORACLE, identical to
# SPANEMIT's: verify mode keeps the chain running (mem[] stays true, so the decode is a valid
# reference), keeps the mirror LIVE except on an emitted line, and drops the ownership carve-out
# (the reference conversion reads m_lineMode too, so a carved-out mode would blank the reference
# on exactly the lines under test).  g_plotMismatch must stay 0 with g_plotChecks large.
# Diagnostic build: never quote a timing from it.
# ⚠⚠ `make SPANFILL=2` — THE PICTURE IS WRONG BY CONSTRUCTION, and it is the DECOMPOSITION.
# Identical to SPANFILL=1 except the span's longword fill is deleted while the claim, the
# counters and the deletion of the chain's forty units all stay.  Three runs then separate the
# two halves of the checkpoint with no bracket inside the sweep:
#     control            phase 24 = drivers + 1440 units
#     SPANFILL=2         phase 24 = drivers +  591 units + 21 claims          (the DELETION)
#     SPANFILL=1         ... + the 21 x 20 longword pairs                     (the FILL)
# ⭐⭐ RUN THE TRIPLE PARKED, and check it: `STRAIGHT_TO_RACE=1 PROBES=1 FIXED_RNG=1` with NO
# HOLD_THROTTLE.  A driving pair is not comparable across builds at this resolution — the 50 Hz
# body runs on WALL CLOCK, so a build of a different speed takes a different number of body ticks
# per main-loop frame and drives a different scene: a measured driving pair moved phases 11 and 18
# — which SPANFILL cannot touch — by +1.6 and +2.9 ms, against the +1.7 under test.  Parked, the
# scene is static and phase 0 plus every untouched phase must agree to well under a millisecond;
# if they do not, the runs are still not comparable and nothing may be concluded.
# ⚠ Parked INFLATES the emit rate (a static scene's sources are clean, so more lines qualify), so
# read the per-span PRICE from it, never the shipping share — that comes from the driving run.
# ⭐⭐⭐ `make SPANFILL=3` — THE STATELESS PREDICATE, i.e. the §10p takeover's own test in place
# of the ⛔ writer-maintained dirty map.  SPANFILL=1's span and fill, unchanged; the predicate
# becomes "read this line's forty sources" (`view_line_flat`), which is EXACT where the map was
# conservative, needs no priming, and — the point — deletes `view_mark_source` from `seam_write`
# and with it the +4.35 ms of producer collateral the map cost in phases 11 and 18
# (src/gen/revs_native_seam.h §the marking is a measured dead end).  Sizing: the scan costs
# ~480 cyc on a flat line and less on one that exits early, ~1.8 ms over phase 1's 36 lines,
# against 4.35 recovered — so SPANFILL=3 minus the control is the first NET-POSITIVE span build.
# ⭐⭐ `make SPANFILL=4` — ...and the GROUP-OF-FOUR scan: one `or.l` tests four lines at once
# (180 cyc/line instead of 480) because along the line axis the source stride is 1.  The sweep's
# lines are consecutive, so one accumulator is cached across four line entries.
# ⭐⭐ `make SPANFILL=4 SPANSCAN=1` — ITS ORACLE, and it is not optional before believing it:
# the lane->line map is the one endian-dependent part of the longword read, and a wrong map
# answers "flat" about a NEIGHBOURING line, which paints a plausible picture.  Every line entry
# asks both scans and compares; g_spanScanMismatch must stay 0 with g_spanScanChecks large.
# ⭐ Run `make SPANSCAN=1` ON THE HOST too (the repo-root Makefile): the check does NOT depend on
# REVS_SPAN_EMIT, so the host can run it over the whole sweep — and the host is little-endian
# while the Amiga is big-endian, which is the only way one source tree exercises both arms of the
# `#if` that picks the lane order.
#   ✅ MEASURED ON THE TARGET, DRIVING: 3796 checks, 0 mismatches — the big-endian lane order is
#   right, and that is the half no host run can reach (2026-09-16).
#
# ⭐⭐⭐ `make SPANFILL=5` — THE TAKEOVER (docs/span-render-plan.md §10p step 3b), i.e. the first
# build in which the renderer owns phase 1 OUTRIGHT rather than hooking into it.  SPANFILL=4's
# predicate and scan, unchanged; what is new is the arm for a line that is NOT one flat run: it is
# painted cell by cell straight into the two bitplanes (`revs_plot_chain`) instead of into forty
# `mem[]` bytes a decode then converts back.  So all 36 of phase 1's lines are claimed, not 21.
#   WHAT IT DELETES, and why a hook-in could not: not the unit loop — §10n measured that deleting
#   58% of phase 1's units nets −0.82 ms — but the per-line RUN SET-UP the unit loop sits inside
#   (two re-bases, the stop address, the bus-range test, the run end, the probe run bracket), plus
#   the decode's share of 36 lines rather than 21.  Sized at −6.6 to −7.8 ms on phase 24.
#   ⚠⚠ `mem[]` GOES STALE on display lines 81..116.  Licensed only by the reader gate measured at
#   `f80557d` (poisoning those rows reads 0 bytes outside them) and by `mayOwn`, which is 1 from
#   `view_paint_lines_core` alone.
#   ⚠ ITS ORACLES, both in-process (§10n) and neither optional:
#       make SPANFILL=5 SPANVERIFY=1 DIRECTCHECK=1   — the span BYTES: the chain still runs, so
#           mem[] is a valid reference, and g_plotMismatch must stay 0 over a whole frame.
#       make SPANFILL=5 DIRTYCHECK=1                 — the CARVE-OUT: g_decodeDirtyMismatch == 0.
#   ⚠ `units/frame` in phase4_prof.gdb goes to ~0 for phase 1 in this build, BY CONSTRUCTION — the
#   units are not visited, so `us/unit` is meaningless there.  Read `ms/frame` and `us/line`.
ifneq ($(SPANFILL),)
ifeq ($(SPANVERIFY),1)
EXTRA_DEFINES += -DREVS_SPAN_EMIT -DREVS_DIRECT_PLOT
else
EXTRA_DEFINES += -DREVS_SPAN_EMIT -DREVS_DIRECT_PLOT -DREVS_SPAN_OWN
endif
ifeq ($(SPANFILL),2)
EXTRA_DEFINES += -DREVS_SPAN_NO_FILL
endif
ifeq ($(SPANFILL),3)
EXTRA_DEFINES += -DREVS_SPAN_STATELESS
endif
ifeq ($(SPANFILL),4)
EXTRA_DEFINES += -DREVS_SPAN_STATELESS -DREVS_SPAN_GROUPSCAN
endif
ifeq ($(SPANFILL),5)
EXTRA_DEFINES += -DREVS_SPAN_STATELESS -DREVS_SPAN_GROUPSCAN -DREVS_SPAN_TAKEOVER
endif
ifeq ($(SPANSCAN),1)
EXTRA_DEFINES += -DREVS_SPAN_SCANCHECK
PROBE_SYMS_SCAN = g_spanScanChecks g_spanScanMismatch
endif
PROBE_SYMS_PLOT = g_plotRuns g_plotCells g_plotRunsLast g_plotCellsLast \
                  g_plotLineLo g_plotLineHi g_plotNoTarget g_plotChainLines \
                  g_plotChainNZ g_plotChainNZLast
PROBE_SYMS_SPAN = g_spanEmitLines g_spanEmitPaints g_decodeOwnLines
endif

ifeq ($(DIRECTCHECK),1)
EXTRA_DEFINES += -DREVS_DIRECT_PLOT -DREVS_DIRECT_CHECK
# ⚠ Conditional, because probe-audit is right to fail otherwise: these three exist only in a
# DIRECTCHECK build, and a symbol listed but absent is exactly what the audit is for.
PROBE_SYMS_CHECK = g_plotChecks g_plotMismatch g_plotMismatchOff
PROBE_SYMS_PLOT  = g_plotRuns g_plotCells g_plotRunsLast g_plotCellsLast \
                   g_plotLineLo g_plotLineHi g_plotNoTarget g_plotChainLines \
                  g_plotChainNZ g_plotChainNZLast
endif

# ⭐⭐ `make SPANSTAT=0` — PRICE THE DIAGNOSTICS, and it must come AFTER every block above that
# sets PROBE_SYMS_PLOT / PROBE_SYMS_SPAN, because it takes them away again.
# The span path carries SIX volatile counter RMWs per span (~220 cycles: a 32-bit absolute-address
# read-modify-write cannot be coalesced) plus two more per LINE in the sweep (g_spanEmitLines /
# g_spanEmitPaints, ~44 cycles x 36).  Parked that is ~0.9 ms a frame — a quarter of what deleting
# 851 chain units bought — so the counting build's span price is NOT the shipping price, and this
# is the instrument that separates them.  Same class as SND_STAT() in the VERTB ISR (CLAUDE.md:
# a volatile diagnostic counter is 13% of snd_tick).
# ⚠ Default ON: both span oracles and every committed span .gdb script read these.  A SPANSTAT=0
# build therefore has NO span probe symbols at all, and running a span script against one is a
# probe-audit failure by design rather than a silent zero.
ifneq ($(SPANSTAT),0)
EXTRA_DEFINES += -DREVS_SPAN_STATS
else
PROBE_SYMS_PLOT =
PROBE_SYMS_SPAN =
endif

# `make ISRWATCH=1` — does the 50 Hz body write the fill chain's inputs ($3000-$43FF column
# sources, $7B00-$7FFF overlay code) while the main loop may be inside the chain?  ~11 KB of extra
# reads per field, so a diagnostic build only.  Read with amiga/isr_watch.gdb; the reasoning is at
# the instrument in src/platform/amiga/Revs.cpp.
# `make BODY_IN_ISR=1` — the OLD model: run the game's 50 Hz body inside the VERTB ISR, where it
# preempts the main loop's drawing wherever it happens to be.  Kept only so the artefact it causes
# can be reproduced on demand (amiga/fill_catch.gdb shows the edge jumps and decode mismatches come
# back).  Default is the body at the engine's own frame wait — see Revs.h (drainTicks).
ifdef BODY_IN_ISR
EXTRA_DEFINES += -DREVS_BODY_IN_ISR
endif

# ⭐ `make MEMVOL=1` — the CONTROL build for the mem[]-volatile measurement (src/cpu/mem_decl.h).
# Dropping `volatile` from the 6502 address space is worth 10% of the frame (1.56 -> 1.72 FPS,
# 2026-08-16) because it un-blocks every optimisation gcc can make over the array, and the whole
# transliterated engine is mem[] accesses.  This flag restores the old qualifier so the
# comparison can be re-measured instead of re-argued.  ⚠ `make clean` when you toggle it.
ifdef MEMVOL
EXTRA_DEFINES += -DREVS_MEM_VOLATILE
endif

# ⭐⭐ `make BANDSKIP=0` — run the WHOLE five-interrupt raster-band cycle every field, instead of
# reusing the band record when its 43 input bytes have not moved.  ON by default.  The cycle
# produces a RECORD that the copper executes, not a picture, and that record is a pure function of
# five palette tables plus the horizon — which the main loop moves about once every 25 fields at
# this framerate.  Measured before the change: 6.1 ms of a 20 ms field, of which 233 us ($52A4) was
# the only game work.  This flag is the control and it is the OLD CODE, not a restructure of it —
# see docs/perf-method.md on why that distinction is the measurement.
# ⭐ The switch prints its own state: g_bandRuns vs g_bandSkips (amiga/band_prof.gdb).
ifeq ($(BANDSKIP),0)
EXTRA_DEFINES += -DREVS_NO_BANDSKIP
endif

# ⭐⭐ `make BANDCHECK=1` — the ORACLE for that reuse, and it is not optional diligence: the
# obvious sabotage (drop the horizon from the digest) PASSED the host's 300-frame byte
# differential, because a host run is ~36 fields and the horizon never moves in one.  This build
# always runs the real cycle AND asks what the predicate would have said; every "reuse" it
# predicts must match the record the real cycle then built.  g_bandCheckMismatch must be 0 with
# g_bandCheckChecks large.  Never quote a framerate from it — it skips nothing by construction.
ifeq ($(BANDCHECK),1)
EXTRA_DEFINES += -DREVS_BAND_CHECK
PROBE_SYMS_BANDCHECK = g_bandCheckChecks g_bandCheckMismatch
endif

# `make FILLWATCH=1` — the fill/tear/edge detectors inside decode() (amiga/fill_catch.gdb).
# ⚠ NOT in a default build: they re-read the whole 8320-byte frame buffer to see which rows moved
# under the decode, plus a 93-line bitplane verify — real milliseconds on a 7 MHz 68000, and a
# framerate quoted from a build carrying them is not the baseline.
ifdef FILLWATCH
EXTRA_DEFINES += -DREVS_FILLWATCH
PROBE_SYMS_FILLWATCH = g_planLogBand2 g_planLogBand3 g_planLogGroundL g_planLogGroundR \
                       g_planLogStale g_planLogN g_planStaleFrames \
                       g_tearFrames g_tearRows g_tearRowCount g_tearLastRow \
                       g_fillBadFrames g_fillBadFrameN g_fillBadLine g_fillBadCell \
                       g_fillBadValue g_fillBadRun g_fillGroundLine g_fillEvidence \
                       g_horizonChangeMax g_horizonChangeBig g_horizonChangeSeries \
                       g_decodeMismatch g_decodeMismatchLine \
                       g_edgeJumpFrames g_edgeJumpLine g_edgeJumpPrev g_edgeJumpHere g_edgeJumpNext
endif

ifdef ISRWATCH
EXTRA_DEFINES += -DREVS_ISRWATCH
PROBE_SYMS_ISRWATCH = g_isrWroteSrc g_isrWroteOvl g_isrWroteCode g_isrSamples \
                      g_isrSrcBlocks g_isrOvlBlocks g_isrCodeBlocks g_isrZpWrites g_isrZpFields
endif

# Objects (flattened into obj/ by basename).
CXX_OBJS       := $(addprefix obj/,$(notdir $(CXX_SRCS:.cpp=.o)))
CORE_C_OBJS    := $(addprefix obj/,$(notdir $(CORE_C_SRCS:.c=.o)))
SUPPORT_C_OBJS := $(addprefix obj/,$(notdir $(SUPPORT_C_SRCS:.c=.o)))
ASM_OBJS       := $(addprefix obj/,$(notdir $(ASM_SRCS:.s=.o)))
FRAMEWORK_ASM_OBJS := $(addprefix obj/,$(notdir $(FRAMEWORK_ASM_SRCS:.s=.o)))

OBJECTS := $(CXX_OBJS) $(CORE_C_OBJS) $(SUPPORT_C_OBJS) $(ASM_OBJS) $(FRAMEWORK_ASM_OBJS)

# Locate sources for the pattern rules.  (gcc8_c_support.c is deliberately NOT on
# vpath %.c — it has its own explicit rule so it builds with CCFLAGS, not CORE_CFLAGS.)
vpath %.cpp $(AMIGA_SRC)/framework $(AMIGA_SRC) $(REPO)/src/platform $(REPO)/src
vpath %.c   $(REPO)/src/cpu $(REPO)/src/gen $(REPO)/src/platform
vpath %.s   $(AMIGA_SRC)/framework $(AMIGA_SRC)

WARN = -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable

CORE_CINCLUDES = -I"$(AMIGA_SRC)/framework/compat-include" \
                 -I"$(REPO)/src/cpu" \
                 -I"$(REPO)/src/gen" \
                 -I"$(REPO)/src/platform"

# Optimisation level — overridable for debug builds:
#   make OPT='-O0' NATIVE_OPT='-O0'    (also drops -fomit-frame-pointer for clean backtraces)
OPT        ?= -O2 -fomit-frame-pointer
NATIVE_OPT ?= -O3 -funroll-loops

# -m68000: the target is an A500 68000.  -msoft-float: no FPU.
CORE_CFLAGS = -g -MP -MMD -m68000 -msoft-float $(OPT) -nostdlib \
              -fno-tree-loop-distribution -ffunction-sections -fdata-sections \
              -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
              -Wno-implicit-function-declaration -Wno-int-conversion \
              -DREVS_PLATFORM_AMIGA \
              $(CORE_CINCLUDES) $(EXTRA_DEFINES)
CCFLAGS   = -g -MP -MMD -m68000 -msoft-float $(OPT) -nostdlib \
            -fno-tree-loop-distribution -ffunction-sections -fdata-sections \
            $(WARN) $(EXTRA_DEFINES)
CPPFLAGS  = $(CCFLAGS) -fno-rtti -fno-exceptions -fno-use-cxa-atexit -std=gnu++17 \
            -fpermissive -Wno-write-strings -Wno-pointer-arith \
            -DREVS_PLATFORM_AMIGA \
            -include "$(AMIGA_SRC)/framework/SASCCompat.h" \
            -I"$(AMIGA_SRC)/framework" \
            -I"$(AMIGA_SRC)/framework/compat-include" \
            -I"$(AMIGA_SRC)" \
            -I"$(REPO)/src/platform" \
            -I"$(CURDIR)"
ASFLAGS   = -mcpu=68000 -g --register-prefix-optional -I"$(SDKDIR)" -I"$(CURDIR)"
# vasm (Motorola syntax) for the framework *Assembler.s.  -m68010: the only >68000
# instruction in the vendored framework is `movec vbr,d0` in getVBR, which is
# behaviour-neutral on the A500 target.
# ⚠ -no-opt is LOAD-BEARING (it keeps hand-written encodings exactly as written), which
# means in-range hot branches must be hand-marked `.s` or they assemble as long branches.
VASMFLAGS = -m68010 -Felf -quiet -x -no-opt -nowarn=62 $(EXTRA_VASMDEFS)
# ⚠⚠ PROBE SYMBOLS — every global a committed .gdb script reads MUST be listed here.
# --gc-sections drops any section nothing in the current configuration references, and a dropped
# probe counter is WORSE than a missing one: gdb resolves the name into .text and prints
# instruction bytes as a value (measured: a non-FPSCOUNT build read `painted=1223110688`, which is
# code inside processBlitterQueue).  `--undefined=<sym>` makes the symbol a gc ROOT, at zero
# runtime cost.  `__attribute__((retain))` does NOT work here — it is ignored on this target.
# Add a symbol here the moment you add a counter; `make probe-audit` enforces it on every link.
ifdef TAPTEST
PROBE_SYMS_TAPTEST = g_tapInjected
endif

PROBE_SYMS = g_vbiCount g_fpsFrames g_smcUnhandled g_smcSite g_smcValue g_brkCount g_brkPC \
             g_mosUnknownCount g_mosUnknownEntry g_mosUnknownA \
             g_mosCharDefCount g_mosCharDefOutOfRange g_mosCharDefLastBad \
             g_hwUnknownReads g_hwUnknownAddr g_ulaControl g_userT1LatchLo g_userT1LatchHi \
             g_viaT2Reads g_viaT2Last \
             g_badRegionCount g_badRegionEntry \
             g_screenFrontAddr g_screenCopperAddr g_screenBytes g_screenCopperWords \
             g_gapWalkSlow g_bandRejects g_decodeFlatLines g_decodeFlatBands \
             g_decodeCells g_decodeCellsMax g_decodeCellsTotal g_decodeFullFrames \
             g_decodeModeLines \
             g_decodeModeDirty g_decodeDirtyChecks g_decodeDirtyMismatch \
             g_decodeDirtyMismatchOff \
             g_bandCount g_bandOverflow g_bandSkips g_bandRuns g_bandHorizonMoves g_keyUnmapped g_keyUnmappedCode g_keyEvents g_keyDown \
             g_spaceEdges g_spaceAnswered g_spacePolls g_keyLatchHits g_screenMode7 \
             $(PROBE_SYMS_TAPTEST) \
             g_mouseBtnMask g_mouseBtnSeen \
             g_irqClobberCount g_irqClobberWhich \
             g_irqUnwindPending g_irqUnwindTouched g_irqStackImbalance \
             g_stackLow g_stackHigh g_stackTrespass \
             g_track g_trackInstalled g_trackRequested g_trackUnhonoured g_trackUnhonouredAddr \
             g_trackHooksUnbuilt g_trackHooksUnbuiltAddr \
             g_trackHookMissing g_trackHookMissingAddr g_trackHookCalls g_trackOverinstalls \
             g_tmPhase g_tmOption g_tmTrack g_tmMisrouted g_tmRefusals g_tmFields \
             g_beamPresentLine g_beamPresentMin g_beamPresentMax \
             g_beamPresents g_beamPresentsLate g_beamEntryLine g_beamEntries g_beamEntriesLate \
             g_bodyTicksDropped g_bodyDrains g_bodyTicks g_bodyPending \
             g_fieldCycles g_fieldBeamTicks g_fieldBeamMax \
             g_crashHolds g_crashHoldVbi g_crashHoldVbiMax g_crashHoldTicks g_crashHoldDrops \
             g_renderGapMax g_renderGapMaxAt g_renderStalls \
             g_crashBodyFieldsMax g_resetFieldsMax g_resetFieldsLast g_resetCount g_resetSplit \
             g_ttUnknownVdu g_ttLastUnknown g_ttVduBytes g_ttModeSwitches \
             g_ttModeDisagree g_screenPlanes g_screenHeight g_screenMode7 g_ttAllocFailed g_ttFlashPhase g_ttFlashToggles \
             g_ttRowDirty g_ttRowsDrawn \
             g_sndCommands g_sndEnvelopes g_sndFlushes g_sndTicks g_sndChipWrites \
             g_sndSyncRequests g_sndHoldRequests g_sndQueued g_sndQueueDrops g_sndBadEnvelope \
             g_audioAllocFailed g_audioRestarts g_audioUpdates g_audioTicks g_audioWaitLines \
             g_sndChip g_audioPaulaPer g_audioPaulaVol
# ⚠ CONDITIONAL, because these only EXIST under PROBES (src/platform/probe.cpp compiles to
# nothing otherwise).  Listing them unconditionally makes --undefined=<sym> unsatisfiable and a
# plain `make` fails at probe-audit — which is the audit working, not a bug in it.  The rule
# stays: a symbol a committed .gdb script reads must be listed in the configuration that
# script is run against.  phase4_prof.gdb is a PROBES script.
ifdef PROBES
PROBE_SYMS += g_phaseTicks g_phaseCount g_phaseFrames g_beamEpoch g_probeFrozen \
             g_probeBuildFlags \
             g_probeFrozenBody g_probeFrozenUnits g_probeFrozenRuns g_probeFrozenLines \
             g_viewUnits g_viewRuns g_viewLines \
             g_probeIsrTicks g_probeIsrCount g_probeIrqTicks g_probeIrqCount \
             g_probeBandTicks g_probeBandCount \

endif
# Same rule, same reason: the FPS series exists only in an FPSCOUNT build.
ifdef FPSCOUNT
PROBE_SYMS += g_fpsSeries g_fpsSeriesVbi g_fpsSeriesN
endif
# Same rule again: these exist only in an ISRWATCH build (see the flag above).
ifdef ISRWATCH
PROBE_SYMS += $(PROBE_SYMS_ISRWATCH)
endif
ifdef FILLWATCH
PROBE_SYMS += $(PROBE_SYMS_FILLWATCH)
endif
ifdef SHAPE
PROBE_SYMS += $(PROBE_SYMS_SHAPE)
endif
ifdef GEOSPLIT
PROBE_SYMS += $(PROBE_SYMS_GEO)
endif
ifdef ROADSPLIT
PROBE_SYMS += $(PROBE_SYMS_ROAD)
endif
ifdef HWTIME
PROBE_SYMS += g_probeHwWrites g_probeHwReads g_probeHwTicks
endif
ifdef ISRSPLIT
PROBE_SYMS += $(PROBE_SYMS_ISRSPLIT)
endif
PROBE_SYMS += $(PROBE_SYMS_CHECK) $(PROBE_SYMS_PLOT) $(PROBE_SYMS_BANDCHECK)
PROBE_SYMS += $(PROBE_SYMS_BCD)
PROBE_SYMS += $(PROBE_SYMS_VIEWSKIP)
PROBE_SYMS += $(PROBE_SYMS_SPAN)
PROBE_SYMS += $(PROBE_SYMS_SCAN)

LDFLAGS   = -Wl,--emit-relocs,--gc-sections,-Ttext=0,-Map=$(OUT).map \
            $(addprefix -Wl$(COMMA)--undefined=,$(PROBE_SYMS))
COMMA := ,

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------
.PHONY: all clean muldiv-audit probe-audit

all: $(OUT).exe

$(OUT).exe: $(OUT).elf
	$(info Elf2Hunk $@)
	@elf2hunk $(OUT).elf $(OUT).exe

$(OUT).elf: $(OBJECTS)
	$(info Linking $@)
	@$(CC) $(CCFLAGS) $(LDFLAGS) $(OBJECTS) -o $@
	@$(OBJDUMP) --disassemble --no-show-raw-ins -S $@ > $(OUT).s 2>/dev/null || true
	@$(MAKE) --no-print-directory muldiv-audit
	@$(MAKE) --no-print-directory probe-audit

# ⚠ MANDATORY AUDIT: the 68000 has NO 32-bit multiply or divide, so GCC lowers any
# 32-bit * / / / % into __mulsi3/__divsi3/__udivsi3/__modsi3/__umodsi3 — ~200-600 cycles
# each.  Use the 16-bit helpers in src/cpu/m68k_math.h instead.  This runs on every link
# so a regression is caught the moment it is introduced, not months later.
muldiv-audit:
	@if $(OBJDUMP) -d $(OUT).elf | grep -qE '__(u?div|u?mod|mul)si3'; then \
	  echo "*** 32-bit software mul/div linked in — see docs/m68k-optimisation.md:"; \
	  $(OBJDUMP) -d $(OUT).elf | grep -E '__(u?div|u?mod|mul)si3' | head; \
	  exit 1; \
	else echo "muldiv-audit: clean"; fi

# ⚠ MANDATORY AUDIT: every PROBE_SYMS symbol must be a real data symbol in the linked ELF.
# A gc-dropped probe counter makes gdb print instruction bytes as a measurement — see PROBE_SYMS.
probe-audit:
	@bad=""; for s in $(PROBE_SYMS); do \
	  $(OBJDUMP) -t $(OUT).elf | grep -qE "[[:space:]]O[[:space:]]+\.(bss|data)[[:space:]].*[[:space:]]$$s$$" \
	    || bad="$$bad $$s"; \
	done; \
	if [ -n "$$bad" ]; then \
	  echo "*** probe symbol(s) missing from $(OUT).elf (gc-sections ate them):$$bad"; \
	  echo "    gdb would resolve these into .text and print instruction bytes as values."; \
	  exit 1; \
	else echo "probe-audit: clean ($(words $(PROBE_SYMS)) symbols)"; fi

# C++ (framework + Revs app + shared platform layer).
obj/%.o : %.cpp
	$(info C++  $<)
	@$(CC) $(CPPFLAGS) -c -o $@ "$(abspath $<)"

# Transpiled 6502 core C.  revs_native.c (hand-written native twins) also gets NATIVE_OPT.
obj/%.o : %.c
	$(info C    $< [core])
	@$(CC) $(CORE_CFLAGS) -c -o $@ "$(abspath $<)"
obj/revs_native.o : CORE_CFLAGS += $(NATIVE_OPT)
obj/revs_native_seam.o : CORE_CFLAGS += $(NATIVE_OPT)
obj/revs_native_abi.o : CORE_CFLAGS += $(NATIVE_OPT)

# Support runtime: gcc8_c_support.c needs CCFLAGS (no core include paths).
obj/gcc8_c_support.o : $(SUPPORT)/gcc8_c_support.c
	$(info C    $<)
	@$(CC) $(CCFLAGS) -c -o $@ "$<"

obj/gcc8_a_support.o : $(SUPPORT)/gcc8_a_support.s
	$(info ASM  $<)
	@$(AS) $(ASFLAGS) -o $@ "$<"

# ⚠ .incbin dependencies are INVISIBLE to `gcc -MMD`, so list them explicitly.  Without
# this, regenerating an embedded blob leaves a stale copy in the binary while the on-disk
# file looks correct.  Paths are relative to this amiga/ directory.
INCBIN_DEPS := ../disasm/revs_runtime.bin
obj/incbin.o : $(AMIGA_SRC)/incbin.s $(INCBIN_DEPS)
	$(info ASM  $<)
	@$(AS) $(ASFLAGS) -o $@ "$(abspath $(AMIGA_SRC)/incbin.s)"

# Framework hand-written asm (vasm, Motorola syntax -> ELF).  Static pattern rule so it
# doesn't collide with the gas-assembled gcc8_a_support.s / incbin.s explicit rules.
$(FRAMEWORK_ASM_OBJS) : obj/%.o : %.s
	$(info VASM $<)
	@$(VASM) $(VASMFLAGS) -o $@ "$(abspath $<)"

clean:
	@$(RM) -r obj/ out/

-include $(CXX_OBJS:.o=.d) $(CORE_C_OBJS:.o=.d) $(SUPPORT_C_OBJS:.o=.d)
