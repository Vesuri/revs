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
    $(AMIGA_SRC)/RevsInput.cpp \
    $(AMIGA_SRC)/RevsAudio.cpp \
    $(REPO)/src/platform/Platform.cpp \
    $(REPO)/src/platform/mos.cpp \
    $(REPO)/src/platform/probe.cpp \
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
    $(wildcard $(REPO)/src/gen/revs_gen.c) \
    $(wildcard $(REPO)/src/gen/revs_native.c) \
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
ifdef COMPETITION
EXTRA_DEFINES += -DREVS_COMPETITION
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
PROBE_SYMS = g_vbiCount g_fpsFrames g_smcUnhandled g_smcSite g_smcValue g_brkCount g_brkPC \
             g_mosUnknownCount g_mosUnknownEntry g_mosUnknownA \
             g_mosCharDefCount g_mosCharDefOutOfRange g_mosCharDefLastBad \
             g_hwUnknownReads g_hwUnknownAddr g_ulaControl g_userT1LatchLo g_userT1LatchHi \
             g_badRegionCount g_badRegionEntry \
             g_screenFrontAddr g_screenCopperAddr g_screenBytes g_screenCopperWords \
             g_bandRejects \
             g_bandCount g_bandOverflow g_keyUnmapped g_keyUnmappedCode g_keyEvents g_keyDown \
             g_irqClobberCount g_irqClobberWhich \
             g_irqUnwindPending g_irqUnwindTouched g_irqStackImbalance \
             g_stackLow g_stackTrespass \
             g_beamPresentLine g_beamPresentMin g_beamPresentMax \
             g_beamPresents g_beamPresentsLate g_beamEntryLine g_beamEntries g_beamEntriesLate \
             g_bodyTicksDropped g_bodyDrains g_bodyTicks g_bodyPending \
             g_ttUnknownVdu g_ttLastUnknown g_ttVduBytes g_ttModeSwitches \
             g_ttModeDisagree g_screenPlanes g_screenHeight g_screenMode7 g_ttAllocFailed g_ttFlashPhase g_ttFlashToggles \
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
PROBE_SYMS += g_phaseTicks g_phaseCount g_phaseFrames g_beamEpoch
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
