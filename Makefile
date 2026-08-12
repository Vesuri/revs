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
##   make sweep               the entry-point sweep report (docs/entrypoint-sweep.md)
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

# C sources: the 6502 CPU model + the generated transliteration + native twins.
# The generated files do not exist until `make gen`; wildcard so a fresh clone builds.
C_SRCS := \
    src/cpu/cpu.c \
    $(wildcard src/gen/revs_gen.c) \
    $(wildcard src/gen/revs_manual.c) \
    $(wildcard src/gen/revs_native.c)

CXX_SRCS := \
    src/platform/Platform.cpp \
    src/platform/mos.cpp \
    src/platform/probe.cpp \
    src/platform/bbc_hw.cpp \
    src/platform/autorun.cpp \
    src/platform/platform_cbridge.cpp \
    src/platform/host/PlatformHost.cpp \
    src/main.cpp

C_OBJS   := $(C_SRCS:.c=.o)
CXX_OBJS := $(CXX_SRCS:.cpp=.o)
OBJS     := $(C_OBJS) $(CXX_OBJS)
TARGET   := build/revs

.PHONY: all clean gen validate image runtime sweep endian-lint

all: $(TARGET)

$(TARGET): $(OBJS) | build
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Native-twin validation harness.  Links the full object graph minus main.o (for the
# symbol environment) plus the harness with its own main().
VALIDATE_OBJS := $(filter-out src/main.o,$(OBJS)) tools/validate_native.o
validate: $(VALIDATE_OBJS) | build
	$(CXX) $(CXXFLAGS) -o build/validate_native $(VALIDATE_OBJS)
	./build/validate_native $(FN)

build:
	mkdir -p build

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Regenerate the transliterated C from the Ghidra listing.
# Requires disasm/listing.txt to be current (see docs/toolchain.md).
gen:
	python3 tools/transpile.py

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
	rm -f $(OBJS) $(TARGET) tools/validate_native.o build/validate_native

# ⭐ Replay the engine's own startup unpack -> disasm/revs_runtime.bin.
# REVS2 relocates itself before running, so revs_mem.bin is NOT the layout the engine
# executes.  THIS is the image to disassemble.  Full mechanism: tools/relocate.py.
#   make runtime                                     from disasm/revs_mem.bin
#   make runtime VERIFY="tmp/dump_SILVER_before.bin tmp/dump_SILVER_after.bin"
#                                                    cross-check against a real BBC
runtime:
	python3 tools/relocate.py $(if $(VERIFY),--verify $(VERIFY),)

# The entry-point sweep report (docs/entrypoint-sweep.md, docs/static-map.md).
#   make sweep                       -> disasm/sweep.txt
#   make sweep TRACE=tmp/trace_SILVER.bin  also cross-check against a real execution trace
sweep:
	python3 tools/sweep_entrypoints.py $(if $(TRACE),--trace $(TRACE),) > disasm/sweep.txt
	@echo "wrote disasm/sweep.txt"
	@sed -n '1,5p' disasm/sweep.txt
