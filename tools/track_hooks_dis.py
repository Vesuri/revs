#!/usr/bin/env python3
"""Disassemble each circuit's HOOK BODIES — the code the engine's patched JSR/JMPs call.

The second half of track selection (docs/phases.md §5b).  The first half puts each circuit's
bytes into mem[] and makes the engine READ the patched operands; this one is about what is at
the other end of them: ~90 instructions per circuit, living in the track window $5300-$5A25,
at the SAME addresses but meaning different code per circuit.

⚠ WHY THIS IS NOT `make sweep --seed`.  The sweep walks the whole engine and treats the window
as ordinary code.  Here the window is the ONLY code: a walk must stop dead at the boundary and
record the engine address it left for, because that transfer is a call back into the shared C.
Spilling into the engine would re-disassemble 7000 instructions per circuit and bury the ~90
that matter.

⚠ AND IT IS NOT THE 6502 THE ENGINE PATCHER WROTE.  ModifyGameCode itself ($5700 onward) is
replayed by tools/track_patch.py and never runs in the port.  Its instructions are excluded
here: they are start-up-only, their effect is already applied as data, and transliterating them
would apply the patches twice.

  python3 tools/track_hooks_dis.py                    the per-circuit report
  python3 tools/track_hooks_dis.py --listing DIR      one listing per circuit, in the same
                                                      format tools/transpile.py already parses
"""
import argparse
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sweep_entrypoints import (OPS, SIZE, BRANCHES, TERMINAL, MOS_ENTRIES,   # noqa: E402
                              IMP, ACC, IMM, ZP, ZPX, ZPY, IZX, IZY, ABS, ABX, ABY, IND, REL)

WINDOW = (0x5300, 0x5A25)          # inclusive
HOOK = 0x5A22                      # CallTrackHook — the engine's fixed entry into the file


def operand_text(mn, mode, operand, pc, n):
    """Ghidra-ish operand text, matching what tools/transpile.py's listing parser expects."""
    if mode in (IMP,):
        return ""
    if mode == ACC:
        return "A"
    if mode == IMM:
        return f"#0x{operand:x}"
    if mode in (ZP, ABS):
        return f"0x{operand:x}"
    if mode in (ZPX, ABX):
        return f"0x{operand:x},X"
    if mode in (ZPY, ABY):
        return f"0x{operand:x},Y"
    if mode == IZX:
        return f"(0x{operand:x},X)"
    if mode == IZY:
        return f"(0x{operand:x}),Y"
    if mode == IND:
        return f"(0x{operand:x})"
    if mode == REL:
        return f"0x{operand:x}"
    raise AssertionError(mode)


def decode(mem, pc):
    op = mem[pc]
    if op not in OPS:
        return None
    mn, mode = OPS[op]
    n = SIZE[mode]
    if mode == REL:
        operand = (pc + 2 + ((mem[pc + 1] ^ 0x80) - 0x80)) & 0xFFFF
    elif n == 3:
        operand = mem[pc + 1] | (mem[pc + 2] << 8)
    elif n == 2:
        operand = mem[pc + 1]
    else:
        operand = None
    return mn, mode, operand, n


def walk(mem, seeds, exclude=()):
    """Recursive descent bounded to WINDOW.

    Returns (insns, exits, bad) where
      insns  {addr: (mnem, mode, operand, nbytes)} — every instruction reached IN the window
      exits  {engine address -> set of window addresses that transfer to it}
      bad    addresses where decoding stopped because the byte is not an opcode
    """
    lo, hi = WINDOW
    insns, exits, bad = {}, defaultdict(set), set()
    work = [s for s in seeds]
    while work:
        pc = work.pop()
        while True:
            if not (lo <= pc <= hi) or pc in insns or pc in exclude:
                break
            d = decode(mem, pc)
            if d is None:
                bad.add(pc)
                break
            mn, mode, operand, n = d
            insns[pc] = (mn, mode, operand, n)

            def target(t):
                """Queue t if it is in the window; otherwise record it as an EXIT."""
                if lo <= t <= hi:
                    work.append(t)
                else:
                    exits[t].add(pc)

            if mn == "JSR":
                target(operand)
                pc += n
                continue
            if mn == "JMP":
                if mode == IND:
                    exits[operand].add(pc)      # a pointer, not a body — report, do not guess
                else:
                    target(operand)
                break
            if mn in BRANCHES:
                target(operand)
                pc += n
                continue
            if mn in TERMINAL:
                break
            pc += n
    return insns, dict(exits), bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listing", metavar="DIR",
                    help="write one <DFS>.txt listing per circuit, in listing.txt format")
    args = ap.parse_args()

    from gen_tracks import CIRCUITS, runtime_image, patch_list
    from track_smc import hook_targets, extent_rows

    rows = extent_rows()
    total = 0
    for ssd, dfs, disp in CIRCUITS:
        if not os.path.exists(ssd):
            print(f"# note: {ssd} absent — {disp} skipped", file=sys.stderr)
            continue
        img = runtime_image(ssd, dfs)
        if img[HOOK] == 0x60:
            print(f"{disp:16} PASSIVE — $5A22 is RTS, no hook bodies")
            continue
        patches = dict(patch_list(runtime_image(ssd, dfs)))
        hooks = hook_targets(bytes(img), patches, rows)

        # ⚠ EXCLUDE ModifyGameCode.  Walk it first from $5A22's JMP target and subtract every
        # instruction it reaches, so the hook walk cannot wander into the start-up patcher and
        # emit code that must never run in the port.
        mgc_entry = img[HOOK + 1] | (img[HOOK + 2] << 8)
        mgc, _e, _b = walk(img, [mgc_entry])

        insns, exits, bad = walk(img, hooks, exclude=set(mgc))
        total += len(insns)
        eng = sorted(a for a in exits if not (WINDOW[0] <= a <= WINDOW[1]))
        mos = [a for a in eng if a in MOS_ENTRIES]
        print(f"{disp:16} {len(hooks):2} entries, {len(insns):3} instructions, "
              f"{len(eng):2} engine exits"
              + (f", {len(mos)} MOS" if mos else "")
              + (f", ⚠ {len(bad)} undecodable" if bad else ""))
        print(f"    entries: {' '.join('$%04X' % a for a in hooks)}")
        print(f"    exits:   {' '.join('$%04X' % a for a in eng)}")
        if bad:
            print(f"    ⚠ undecodable: {' '.join('$%04X' % a for a in sorted(bad))}")
        print(f"    ModifyGameCode excluded: {len(mgc)} instructions from ${mgc_entry:04X}")

        if args.listing:
            os.makedirs(args.listing, exist_ok=True)
            out = [f"; {disp} ({dfs}) hook bodies — GENERATED by tools/track_hooks_dis.py",
                   f"; {len(insns)} instructions, entries "
                   f"{' '.join('$%04X' % a for a in hooks)}"]
            for a in sorted(insns):
                mn, mode, operand, n = insns[a]
                raw = " ".join(f"{b:02X}" for b in img[a:a + n])
                txt = operand_text(mn, mode, operand, a, n)
                out.append(f"{a:04x}  {raw:8} {mn}" + (f" {txt}" if txt else ""))
            p = os.path.join(args.listing, f"{dfs}.txt")
            open(p, "w").write("\n".join(out) + "\n")
            print(f"    -> {p}")
    print(f"# {total} hook instructions across every circuit present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
