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
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SRC_BASE = 0x3000       # dashData block 0; blocks are $80 apart
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


# The engine's own calls into the page — the only way in, since nothing statically
# references the page's interior.  docs/static-map.md §Open items 6.
ENGINE_ENTRIES = {
    0x7B00: [0x1739],
    0x7B4A: [0x1704],
    0x7B9C: [0x502A, 0x503B, 0x6612],
    0x7BE2: [0x16E6, 0x1748],
}


def emit_listing(mem, out):
    """Disassemble $7B00-$7FFF into `disasm/listing.txt` format, for the transpiler.

    ⚠ **This is NOT Ghidra's output, and that is a real difference from every other line the
    transpiler reads.**  Ghidra disassembles `revs_runtime.bin`, in which this page is $00.
    The page only exists after `copy_dash_data` runs, and folding it into that image would
    make `revs_runtime.bin` mean two things at once — blocks 26-40 would also land on top of
    the track data at $70DB-$7813, which is faithful to a running machine and useless as a
    disassembly input.  So the overlay is disassembled here instead, by the SAME
    recursive-descent walker `make sweep` uses (`tools/sweep_entrypoints.py`), which agrees
    with Ghidra to within 4 instructions over the rest of the binary.

    ⚠ It therefore has **no Ghidra cross-check**, unlike the other 7098 instructions.  If this
    page ever disagrees with a real BBC, suspect here first.
    """
    import sweep_entrypoints as S

    sw = S.Sweep(mem, CODE_LO, CODE_HI - 1)
    # Confine the walk to the page: pre-marking everything outside it as "seen" stops the
    # walker the instant it follows a JSR out into the engine, without special-casing
    # anything inside.  The engine's own routines are Ghidra's job, not ours.
    sw.seen.update(a for a in range(0x10000) if not (CODE_LO <= a < CODE_HI))
    sw.run(list(ENGINE_ENTRIES))
    sw.seen.difference_update(a for a in range(0x10000) if not (CODE_LO <= a < CODE_HI))

    decoded = sorted(sw.insn)
    # A function starts at an engine entry or at any in-page JSR target.  Everything else —
    # branch and JMP targets — stays inside the enclosing range, which is what the
    # transpiler wants: it turns those into local gotos.
    starts = sorted(set(ENGINE_ENTRIES) |
                    {t for t in sw.calls if CODE_LO <= t < CODE_HI})

    ranges, names = [], {}
    for i, s in enumerate(starts):
        nxt = starts[i + 1] if i + 1 < len(starts) else CODE_HI
        body = [a for a in decoded if s <= a < nxt]
        last = body[-1]
        ranges.append((s, last + sw.insn[last][3] - 1))
        names[s] = f"FUN_{s:04x}"

    def operand_text(mode, operand, n):
        if mode in (S.IMP, S.ACC):
            return "A" if mode == S.ACC else ""
        if mode == S.IMM:  return f"#0x{operand:x}"
        if mode == S.IZX:  return f"(0x{operand:02x},X)"
        if mode == S.IZY:  return f"(0x{operand:02x}),Y"
        if mode == S.IND:  return f"(0x{operand:04x})"
        if mode == S.ZPX:  return f"0x{operand:02x},X"
        if mode == S.ZPY:  return f"0x{operand:02x},Y"
        if mode == S.ABX:  return f"0x{operand:04x},X"
        if mode == S.ABY:  return f"0x{operand:04x},Y"
        return f"0x{operand:04x}"        # ZP, ABS and REL all print as a bare address

    lines = [
        f"; ⚠ NOT a Ghidra listing — the $7B00-$7FFF overlay that copy_dash_data ($18EA)",
        f"; builds at runtime, disassembled by tools/dashdata.py --listing.  Read its",
        f"; docstring before trusting a line of it.  docs/static-map.md §Open items 6.",
        f"; {len(ranges)} functions, {len(decoded)} instructions defined",
    ]
    for s, e in ranges:
        lines.append(f"; FUNC {names[s]:<24} {s:04x} - {e:04x}")
    for a in decoded:
        mn, mode, operand, n = sw.insn[a]
        bs = " ".join(f"{b:02X}" for b in mem[a:a + n])
        lines.append(f"{a:04x}  {bs:<9} {mn:<3} {operand_text(mode, operand, n)}".rstrip())

    open(out, "w").write("\n".join(lines) + "\n")
    covered = sum(sw.insn[a][3] for a in decoded)
    print(f"  wrote {out}: {len(ranges)} functions, {len(decoded)} instructions, "
          f"{covered}/{CODE_HI - CODE_LO} bytes ({100 * covered // (CODE_HI - CODE_LO)}%)")
    for s, e in ranges:
        callers = ENGINE_ENTRIES.get(s)
        tag = ("engine: " + ", ".join(f"${c:04X}" for c in callers)) if callers else "internal"
        print(f"    ${s:04X}-${e:04X}  {tag}")
    if sw.bad:
        print(f"  ⚠ {len(sw.bad)} undecodable byte(s) reached: "
              + ", ".join(f"${a:04X}=${o:02X}" for a, o in sw.bad[:8]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?", default="disasm/revs_runtime.bin")
    ap.add_argument("-o", "--out", help="write the full 64K image with the copy applied")
    ap.add_argument("--code-only", metavar="PATH",
                    help=f"write just ${CODE_LO:04X}-${CODE_HI - 1:04X} "
                         f"({CODE_HI - CODE_LO} bytes)")
    ap.add_argument("--listing", metavar="PATH", nargs="?", const="disasm/dashcode.txt",
                    help="disassemble the overlay into listing.txt format for the transpiler "
                         "(default disasm/dashcode.txt)")
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
    if args.listing:
        if args.reverse:
            sys.exit("--listing needs the forward (unpacked) direction")
        emit_listing(mem, args.listing)
    return 0


if __name__ == "__main__":
    sys.exit(main())
