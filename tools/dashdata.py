#!/usr/bin/env python3
"""Replay `CopyDashData` ($18EA) — the SECOND unpack, the one that builds $7B00-$7FFF.

    python3 tools/dashdata.py                    # report the 41-block layout
    python3 tools/dashdata.py -o out.bin         # write the 64K image with the copy applied
    python3 tools/dashdata.py --code-only out.bin  # write just $7B00-$7FFF (1280 bytes)

## Why this exists

`tools/relocate.py` replays the startup relocation and produces `disasm/revs_runtime.bin`.
That image is correct, and it is *still* not everything the engine executes: the page
$7B00-$7FFF is **empty in it**, yet the main loop calls into that page three times per frame
($7B00, $7B4A, $7BE2 from $1701-$1763, plus $7B9C on the init path).  Those seven call sites
were `docs/static-map.md` open item 6 for two phases.

The answer is a routine, not a mystery: **`CopyDashData` at $18EA** assembles the page at
runtime out of the tails of 41 "dashData" blocks that are spaced every $80 bytes from $3000.
`$16E3 JSR $18EA` is immediately followed by `$16E6 JSR $7BE2` — the page is built one
instruction before it is first called.

    $18EA  STA $74            ; bit 7 of A = direction.  Clear = unpack, set = stow back
    $18EC  copy $192F-$1932 -> $70-$73   ; = 00 30 B0 7F, i.e. src $3000, dst base $7FB0
    $18F8  LDY #$4F           ; each block's data ENDS at offset $4F
           LDA ($70),Y / STA ($72),Y     ; forward pass  (skipped when bit 7 set)
           LDA ($72),Y / STA ($70),Y     ; reverse pass  (a no-op read-back when unpacking)
           INC $76 / DEY / TYA / CMP $3900,X / BNE   ; $3900,X = block X's START offset
           $72/$73 -= $76     ; the destination DESCENDS from $7FFF
           $70/$71 += $80     ; next block
           INX / CPX #$29 / BNE          ; 41 blocks, 0..40

Blocks 0-25 carry **code**, and it lands exactly on $7B00-$7FFF (1280 bytes).  Blocks 26-40
carry dashboard **image** and continue downwards into the custom mode's screen memory.  Run
with bit 7 of A set, the whole thing reverses and stows the code back into the block tails
before the game switches to MODE 7 — which is why the page is empty in every static image.

⚠ Block 25 is **both**: its first 10 bytes are image, the remaining 26 are the code at $7B00.
CopyDashData does not know that — it copies the block whole, and the 10 image bytes land at
$7AF6-$7AFF, just below the code, inside screen memory.  The split only matters if you are
slicing the page out on its own (`--code-only`), which is why it is called out here.

## Confidence — this is DERIVED and self-checking, not measured on a BBC

Three independent things have to agree for the layout to be right, and they do:

  * the 26 code blocks sum to exactly 1280 bytes and bottom out on exactly $7B00;
  * block 0 lands at $7FCC-$7FFF and decodes there as `DrawCarInMirror`, engine-shudder
    idiom and all (`LDX $FE68 / AND $2000,X / AND $61`, then `SBC #$38 / SBC #$01` for the
    -$138 character-row crossing);
  * $7B00 decodes as `UpdateMirrors` — `LDA $03C8,X / LSR / LSR / LSR` (objectSize/8) then
    `ADC $B6` and `LDA $B6 / SEC` around the mirror centre line.

None of that was copied from anywhere; the addresses are this disc's.  It has NOT yet been
confirmed against a jsbeeb dump, because reaching $16E3 on a real BBC is behind the front-end
line-editor blocker (`docs/static-map.md` open item 6, the harness fault).  Do that when the
blocker clears — it is a one-line addition to a probe.
"""
import argparse
import sys

SRC_BASE = 0x3000        # dashData block 0; blocks are $80 apart
BLOCKS = 0x29            # 41, from CPX #$29 at $192A
DATA_END = 0x4F          # LDY #$4F at $18F8
PTR_INIT = 0x192F        # 4 bytes -> $70,$71,$72,$73
START_TABLE = 0x3900     # CMP $3900,X — block X's start offset (exclusive)
CODE_LO, CODE_HI = 0x7B00, 0x8000
BLOCK25_IMAGE = 10       # block 25's first 10 bytes are dashboard image, not code


def copy_dash_data(mem, reverse=False, log=print):
    """Replay $18EA in place.  Returns the list of (block, src, dst, length)."""
    p_src = mem[PTR_INIT] | mem[PTR_INIT + 1] << 8
    p_dst = mem[PTR_INIT + 2] | mem[PTR_INIT + 3] << 8
    if p_src != SRC_BASE:
        sys.exit(f"$192F says src ${p_src:04X}, expected ${SRC_BASE:04X} — wrong image?")

    placed = []
    for x in range(BLOCKS):
        start = mem[START_TABLE + x]          # exclusive: the loop stops when Y == start
        n = DATA_END - start
        for y in range(DATA_END, start, -1):
            a, b = (p_src + y) & 0xFFFF, (p_dst + y) & 0xFFFF
            if not reverse:
                mem[b] = mem[a]
            else:
                mem[a] = mem[b]
        lo = (p_dst + start + 1) & 0xFFFF
        placed.append((x, (p_src + start + 1) & 0xFFFF, lo, n))
        log(f"  blk{x:2d}  ${p_src + start + 1:04X}-${p_src + DATA_END:04X} ({n:3d}) -> "
            f"${lo:04X}-${p_dst + DATA_END:04X}")
        p_dst = (p_dst - n) & 0xFFFF
        p_src = (p_src + 0x80) & 0xFFFF
    return placed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?", default="disasm/revs_runtime.bin")
    ap.add_argument("-o", "--out", help="write the full 64K image with the copy applied")
    ap.add_argument("--code-only", metavar="PATH",
                    help=f"write just ${CODE_LO:04X}-${CODE_HI - 1:04X} "
                         f"({CODE_HI - CODE_LO} bytes)")
    ap.add_argument("--reverse", action="store_true",
                    help="run the stow-back direction (bit 7 of A set) instead")
    args = ap.parse_args()

    mem = bytearray(open(args.image, "rb").read())
    if len(mem) != 0x10000:
        sys.exit(f"expected a 64K image, got {len(mem)} bytes")

    print(f"# CopyDashData ($18EA{', reverse' if args.reverse else ''}) on {args.image}")
    placed = copy_dash_data(mem, reverse=args.reverse)

    code = [p for p in placed if p[2] >= CODE_LO or p[0] == 25]
    lowest = min(p[2] for p in code)
    total = sum(p[3] for p in code) - BLOCK25_IMAGE
    print(f"\n  code blocks 0-{code[-1][0]}: {total} bytes, bottoming out at ${lowest:04X}"
          f" (+{BLOCK25_IMAGE} image bytes below it)")
    ok = (lowest + BLOCK25_IMAGE == CODE_LO) and total == CODE_HI - CODE_LO
    print("  " + ("✓ lands exactly on $%04X-$%04X" % (CODE_LO, CODE_HI - 1) if ok else
                  "⚠ DOES NOT land on $%04X — the layout is wrong" % CODE_LO))
    if not ok:
        return 1

    if args.out:
        open(args.out, "wb").write(mem)
        print(f"  wrote {args.out} (64 KB)")
    if args.code_only:
        open(args.code_only, "wb").write(mem[CODE_LO:CODE_HI])
        print(f"  wrote {args.code_only} ({CODE_HI - CODE_LO} bytes, ${CODE_LO:04X} origin)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
