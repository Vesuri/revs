#!/usr/bin/env python3
"""Whole-corpus determinism compare that skips the 6502 hardware-stack scratch.

`make determinism` byte-compares the full 64K RAM image at a pinned frame against a
recorded golden.  Everything the engine computes as game state must match to the byte.
The ONE region that legitimately differs between two faithful builds is the tail of
page 1: the hardware stack.

Page-1 layout (disasm/symbols.csv): the per-car data arrays run up to car_target_speed
($01A4..$01B7); nothing symboled lives above $01B7.  $01B8..$01FF is the 6502 hardware
stack, which grows down from $01FF.  At a frame boundary its occupied slots hold
transient return addresses and its free slots hold whatever a push last left there.

That residue is provably NOT game state — the 6502's own PULL pops each pushed byte
back before it is read, and nothing reads a free slot.  But its exact bytes shift the
moment a native twin preserves a register in a C local instead of on the 6502 stack
(the cpu-elimination campaign does this deliberately).  So we skip $01B8..$01FF and
byte-compare all other 65464 bytes — car tables, zero page, screen, everything.

The window is a FIXED range from symbols.csv, not an SP computation: cpu.S is not a
reliable resting value on the host (the transliteration calls through the C stack, so
cpu.S only moves on explicit PHA/PLA and reads 0 at the dump).  If a real defect ever
corrupted only stack-tail bytes it would, by construction, affect no observable state;
any defect that matters lands in the 65464 bytes still compared.
"""
import sys

STACK_LO = 0x01B8   # first byte of the hardware-stack scratch (car_target_speed ends $01B7)
STACK_HI = 0x01FF   # last byte of page 1

# Cells relocated OUT of mem[] into native wide values by the wide-value cleanup (mechanism B).
# After relocation nothing reads or writes these mem[] bytes, so a fresh run leaves them at
# whatever the loader/unpack put there while the recorded golden still holds the old computed
# value.  They are no longer game state; skip them.  Each entry is (lo, hi) inclusive.
#   $4F21..$4F22  band2_duration  — the horizon-band remainder, now band2_duration_v
#                 (src/gen/revs_native.c, irq1v_band_schedule)
# ⭐ EMPTY, AND IT SHOULD STAY EMPTY.  A relocated pair is published back into mem[] — at its
# producer's 6502-ABI shim where there is one (hypot_max $7A/$7B, bearing $8A/$8B,
# car_lateral_speed_entry $38/$39), or at the producer itself where there is not (band2_duration
# $4F21/$4F22, whose producer is an interrupt entry point).  So a relocated pair stays real game
# state and this gate keeps covering it.  Adding an entry here blunts the whole-corpus
# differential; publish the value instead.
RELOCATED = []

def _skipped(i):
    if STACK_LO <= i <= STACK_HI:
        return True
    for lo, hi in RELOCATED:
        if lo <= i <= hi:
            return True
    return False

def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: det_compare.py <ref.mem> <run.mem>\n")
        return 2
    ref, run = (open(p, "rb").read() for p in sys.argv[1:3])
    if len(ref) != 0x10000 or len(run) != 0x10000:
        sys.stderr.write("det_compare: image not 64K (ref=%d run=%d)\n" % (len(ref), len(run)))
        return 2
    diffs = [(i, ref[i], run[i]) for i in range(0x10000)
             if ref[i] != run[i] and not _skipped(i)]
    if not diffs:
        return 0
    for i, r, n in diffs[:20]:
        sys.stderr.write("  $%04X: ref=0x%02X run=0x%02X\n" % (i, r, n))
    sys.stderr.write("det_compare: %d byte(s) differ outside the stack scratch\n" % len(diffs))
    return 1

if __name__ == "__main__":
    sys.exit(main())
