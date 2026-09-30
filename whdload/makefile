# The WHDLoad slave, cross-assembled on the host (vasm), and the release archive.
#   make              Revs.slave
#   make tune         RevsTune.slave: DEBUG + MEMFREE, to re-measure the memory sizes
#   make dist         (repo root: `make dist`) the release build + slave + Revs-<version>.lha
# docs/whdload.md has the design; RevsSlave.s the reasoning at each choice.
WHDLOAD ?= $(HOME)/.local/share/amiga/WHDLoad
NDK ?= $(HOME)/.local/opt/m68k-amiga-elf/sys-include
VASM ?= $(HOME)/.local/vasmm68k_mot
VASMFLAGS ?=

.PHONY: all tune clean FORCE
all: Revs.slave

# Always assemble: options and SDK includes must never leave a stale or diagnostic variant.
Revs.slave: RevsSlave.s FORCE
	$(VASM) -m68000 -pic -x -devpac -Fhunkexe -nosym $(VASMFLAGS) -I"$(WHDLOAD)/Include" -I"$(NDK)" -o $@ $<

tune: RevsTune.slave
RevsTune.slave: RevsSlave.s FORCE
	$(VASM) -m68000 -pic -x -devpac -Fhunkexe -nosym $(VASMFLAGS) -DTUNE=1 -I"$(WHDLOAD)/Include" -I"$(NDK)" -o $@ $<

clean:
	rm -f Revs.slave RevsTune.slave
