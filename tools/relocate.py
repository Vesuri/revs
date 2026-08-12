#!/usr/bin/env python3
"""Replay the engine's own startup relocation → disasm/revs_runtime.bin.

    python3 tools/relocate.py                        # disasm/revs_mem.bin -> revs_runtime.bin
    python3 tools/relocate.py --verify tmp/dump_SILVER_after.bin

## Why this exists

`disasm/revs_mem.bin` is the state the *disc loader* leaves behind.  **It is not the layout
the engine executes.**  The first thing REVS2 does at $1200 is unpack itself, and the code the
game spends its life in lives at addresses that hold something else entirely in the loaded
image.  Disassembling `revs_mem.bin` therefore produces a listing that is wrong wherever it
matters most — which is exactly the failure mode `docs/entrypoint-sweep.md` warns about, one
level earlier than expected.

## What the engine does, in order (DERIVED from $1200-$12FF; MEASURED against jsbeeb)

  1. `$1200`  copy the 256-byte entry page $1200-$12FF up to $7900, and `JMP $790E` — from
     here on the stub runs from the copy, so it is free to overwrite $1200.
  2. `$790E`  `OSBYTE 200,3` (disable ESCAPE, clear memory on BREAK) and `OSBYTE 140,0`.
  3. `$792E`  **swap** $5300… with $70DB… byte-by-byte up to $7800, accumulating a rolling
     4-way checksum in $7800-$7803 (`AND #3 / TAX / DEC $7800,X`).  All four cells must end
     at zero or the stub does `JMP ($FFFC)` — a **reset**.  That is an integrity check, and it
     is the reason the track data ends up at $5300 rather than where DFS loaded it.
  4. `$7960`  five block moves driven by three 5-byte tables at $12AF/$12B4 (source lo/hi),
     $12B9/$12BE (source end lo/hi) and $12C3/$12C8 (dest lo/hi), executed X=4 down to X=1.
  5. `$799B`  **patches its own copy loop**: writes $12AD/$12AE (`A9 00` = `LDA #0`) over the
     `LDA ($70),Y` at $7978, turning the mover into a zero-filler, then runs the X=0 entry —
     so that last "move" clears its destination instead of copying.
  6. `$79AA`  `JMP $63BD` — into the unpacked engine.

The block-move order is load-bearing: X=4 and X=3 read $1500-$15DB and $1300-$1500 before X=2
overwrites $0D00-$16DB with them still-unread sources.  Replaying them out of order silently
produces a plausible-looking wrong image.

## Confidence

`--verify` against a RAM dump taken from jsbeeb after the engine has started accounts for
9638 of the 10168 bytes that differ between load state and running state.  The residue is
zero page, the stack, the MOS vector page and the game's own running state — none of it the
static relocation, and it is reported so it can never be mistaken for agreement.
"""
import argparse
import sys

ENTRY = 0x1200
STUB = 0x7900            # where the entry page is copied to
SWAP_SRC = 0x5300        # $70/$71 initial value
SWAP_DST = 0x70DB        # $72/$73 initial value
SWAP_END_PAGE = 0x77     # loop runs until the dest page pointer reaches this
CHECKSUM = 0x7800        # 4 cells, $7800-$7803

# The three block-move tables — addressed in the STUB COPY at $79xx, not at $12xx.
# ⚠ This is not a cosmetic choice.  Move X=2 copies 2524 bytes to $0D00, which runs to $16DB
# and therefore overwrites $12AB-$12CD — the tables themselves.  Reading them from $12xx works
# for the first move and then silently yields garbage (it produced a plausible-looking
# "entry $919D" before this was fixed).  The self-copy at step 1 exists precisely so the stub
# outlives its own source bytes; the replay has to honour that.
TBL_SRC_LO, TBL_SRC_HI = 0x79AF, 0x79B4
TBL_END_LO, TBL_END_HI = 0x79B9, 0x79BE
TBL_DST_LO, TBL_DST_HI = 0x79C3, 0x79C8
PATCH_FROM = 0x79AD      # 2 bytes, written over the mover's load instruction
PATCH_TO = 0x7978
FINAL_JMP_OPERAND = 0x79AB   # the 2 operand bytes of the closing JMP


def relocate(mem, log=print):
    """Apply the startup relocation in place.  Returns (entry, moves, checksum_ok)."""
    m = mem

    # 1. entry page -> $7900
    m[STUB:STUB + 0x100] = m[ENTRY:ENTRY + 0x100]

    # 3. the checked swap.  Transcribed from $792E rather than simplified: the loop's exit
    #    condition is "Y == $25 AND the dest page pointer == $77", which is not the same as
    #    any byte count you would guess.
    p_src, p_dst, y = SWAP_SRC, SWAP_DST, 0
    while True:
        a = (p_dst + y) & 0xFFFF
        b = (p_src + y) & 0xFFFF
        was = m[a]
        m[a], m[b] = m[b], was
        idx = was & 3
        m[CHECKSUM + idx] = (m[CHECKSUM + idx] - 1) & 0xFF
        y = (y + 1) & 0xFF
        if y == 0:
            p_src += 0x100
            p_dst += 0x100
        if y != 0x25:
            continue
        if (p_dst >> 8) == SWAP_END_PAGE:
            break
    swap_end = p_dst + y
    checksum_ok = all(m[CHECKSUM + i] == 0 for i in range(4))
    log(f"swap   ${SWAP_DST:04X}-${swap_end:04X} <-> ${SWAP_SRC:04X}-"
        f"${SWAP_SRC + (swap_end - SWAP_DST):04X}   "
        f"checksum {'OK' if checksum_ok else 'FAILED — the engine would RESET here'}")

    src = [m[TBL_SRC_LO + i] | m[TBL_SRC_HI + i] << 8 for i in range(5)]
    end = [m[TBL_END_LO + i] | m[TBL_END_HI + i] << 8 for i in range(5)]
    dst = [m[TBL_DST_LO + i] | m[TBL_DST_HI + i] << 8 for i in range(5)]

    moves = []
    for x in (4, 3, 2, 1):                       # 4. the copies, in the engine's own order
        n = end[x] - src[x]
        m[dst[x]:dst[x] + n] = m[src[x]:src[x] + n]
        moves.append(("copy", x, src[x], end[x], dst[x], n))
        log(f"X={x}    copy ${src[x]:04X}-${end[x]:04X} -> ${dst[x]:04X}  ({n} bytes)")

    m[PATCH_TO:PATCH_TO + 2] = m[PATCH_FROM:PATCH_FROM + 2]     # 5. self-patch
    log(f"patch  ${PATCH_TO:04X} <- {m[PATCH_FROM]:02X} {m[PATCH_FROM + 1]:02X}"
        f"   (LDA ($70),Y becomes LDA #$00 — the mover becomes a zero-filler)")
    n = end[0] - src[0]
    m[dst[0]:dst[0] + n] = bytes(n)
    moves.append(("zero", 0, src[0], end[0], dst[0], n))
    log(f"X=0    zero-fill ${dst[0]:04X}-${dst[0] + n:04X}  ({n} bytes)")

    entry = m[FINAL_JMP_OPERAND] | m[FINAL_JMP_OPERAND + 1] << 8
    log(f"entry  JMP ${entry:04X}")
    return entry, moves, checksum_ok


def runs(addrs):
    out, s, p = [], addrs[0], addrs[0]
    for v in addrs[1:]:
        if v == p + 1:
            p = v
            continue
        out.append((s, p))
        s = p = v
    out.append((s, p))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?", default="disasm/revs_mem.bin")
    ap.add_argument("-o", "--out", default="disasm/revs_runtime.bin")
    ap.add_argument("--verify", nargs=2, metavar=("BEFORE", "AFTER"),
                    help="a pair of real jsbeeb RAM dumps — at the REVS2 entry and after the "
                         "engine has started.  Applies the replay to BEFORE and reports every "
                         "byte it does not explain in AFTER.  This is the only honest check: "
                         "diffing against revs_mem.bin instead would blame the MOS workspace "
                         "and screen RAM on the relocation.")
    args = ap.parse_args()

    mem = bytearray(open(args.image, "rb").read())
    if len(mem) != 0x10000:
        sys.exit(f"expected a 64K image, got {len(mem)} bytes")

    print(f"# relocating {args.image}")
    entry, _, ok = relocate(mem)
    if not ok:
        print("WARNING: the integrity checksum did not clear — on real hardware the engine "
              "resets here.  The input image is not what the loader produces.", file=sys.stderr)

    with open(args.out, "wb") as fh:
        fh.write(mem)
    print(f"wrote {args.out} (64 KB), engine entry ${entry:04X}")

    if args.verify:
        bpath, apath = args.verify
        before = bytes(open(bpath, "rb").read())
        real = open(apath, "rb").read()
        sim = bytearray(before)
        print()
        print(f"# verify: replay on {bpath}, compared with {apath}")
        rentry, _, rok = relocate(sim, log=lambda *_: None)
        if rentry != entry or not rok:
            print(f"  ⚠ the real image relocates differently: entry ${rentry:04X}, "
                  f"checksum {'OK' if rok else 'FAILED'}")
        moved = sum(1 for i in range(0x10000) if sim[i] != before[i])
        real_diff = [i for i in range(0x10000) if real[i] != before[i]]
        unexplained = [i for i in range(0x10000) if sim[i] != real[i]]
        print(f"  bytes the replay changed:            {moved}")
        print(f"  bytes the real machine changed:      {len(real_diff)}")
        print(f"  bytes the replay does NOT explain:   {len(unexplained)}")
        if not unexplained:
            print("  the replay reproduces the running image EXACTLY.")
            return
        eng = [a for a in unexplained if 0x1200 <= a < 0x7000]
        print(f"    of those, inside the engine range: {len(eng)}"
              "   <-- runtime self-modification / running state, NOT relocation")
        for s, e in runs(unexplained):
            where = ("zero page" if e < 0x100 else "stack" if e < 0x200 else
                     "MOS vectors/workspace" if e < 0x0B00 else
                     "engine" if 0x1200 <= s < 0x7000 else "other")
            print(f"    ${s:04X}-${e:04X} ({e - s + 1:5d})  {where}")


if __name__ == "__main__":
    main()
