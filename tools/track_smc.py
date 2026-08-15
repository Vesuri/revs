#!/usr/bin/env python3
"""Classify every per-circuit engine patch AT INSTRUCTION GRANULARITY.

`tools/track_patch.py` answers "which BYTES does each circuit's ModifyGameCode write".  That
is the right question for the installer (which copies bytes) and the wrong one for the
transpiler, which emits C per INSTRUCTION.  A patch that rewrites `LDA $5905,Y` into
`JSR $5672` is three bytes, one instruction, and a completely different operation — it is not
the rasteriser's "operand rewritten, opcode untouched" shape at all.

So this tool folds the byte sets onto the listing's instruction boundaries and reports, per
touched instruction:

  * the base (Silverstone) instruction, decoded
  * one decoded variant per circuit
  * whether every variant has the SAME LENGTH as the base — because if it does not, the
    following bytes' meaning shifts and the site is not a drop-in substitution
  * a suggested SMC_SITES class

⚠ It is deliberately a REPORT, not a generator: the classes it suggests get pasted into
`tools/transpile.py` with the evidence, the same way every other SMC row was justified.

  python3 tools/track_smc.py            # the per-instruction report
  python3 tools/track_smc.py --summary  # just the class histogram and the anomalies
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# ⚠ gen_tracks is imported LAZILY, inside main().  It imports hook_targets() from here, so a
# module-level import either way round is a cycle.

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LISTING = os.path.join(ROOT, "disasm/listing.txt")

INSN_RE = re.compile(r"^([0-9a-f]{4})\s+((?:[0-9A-F]{2} )+)\s*(\S+)\s*(.*)$")


def load_listing():
    """{addr: (bytes, mnem, operand)} for every disassembled instruction."""
    insns = {}
    with open(LISTING) as fh:
        for line in fh:
            if line.startswith(";") or not line.strip():
                continue
            m = INSN_RE.match(line.rstrip("\n"))
            if not m:
                continue
            addr = int(m.group(1), 16)
            raw = bytes(int(b, 16) for b in m.group(2).split())
            insns[addr] = (raw, m.group(3), m.group(4).strip())
    return insns


# The 6502 instruction length by opcode, for decoding a PATCHED byte string that the listing
# has never seen.  Only the forms the patcher actually produces are needed, but the table is
# complete so an unexpected opcode is a length error rather than a silent misparse.
LEN = {}
for _op in range(256):
    LEN[_op] = 1
_L2 = ("09 05 15 01 11 29 25 35 21 31 49 45 55 41 51 69 65 75 61 71 C9 C5 D5 C1 D1 "
       "E0 E4 C0 C4 49 A9 A5 B5 A1 B1 A2 A6 B6 A0 A4 B4 E9 E5 F5 E1 F1 85 95 81 91 "
       "86 96 84 94 06 16 46 56 26 36 66 76 C6 D6 E6 F6 24 10 30 50 70 90 B0 D0 F0")
_L3 = ("0D 1D 19 2D 3D 39 4D 5D 59 6D 7D 79 CD DD D9 CC EC 4D AD BD B9 AE BE AC BC "
       "ED FD F9 8D 9D 99 8E 8C 0E 1E 4E 5E 2E 3E 6E 7E CE DE EE FE 2C 4C 20 6C")
for _b in _L2.split():
    LEN[int(_b, 16)] = 2
for _b in _L3.split():
    LEN[int(_b, 16)] = 3

MNEM = {
    0x20: "JSR", 0x4C: "JMP", 0x60: "RTS", 0xEA: "NOP", 0xA9: "LDA#", 0x29: "AND#",
    0xB9: "LDA abs,Y", 0xBD: "LDA abs,X", 0xAD: "LDA abs", 0x99: "STA abs,Y",
    0x9D: "STA abs,X", 0x8D: "STA abs", 0x85: "STA zp", 0x84: "STY zp", 0x86: "STX zp",
    0xA6: "LDX zp", 0xA2: "LDX#", 0xA0: "LDY#", 0x18: "CLC", 0x69: "ADC#", 0xE0: "CPX#",
    0xF0: "BEQ", 0xD0: "BNE", 0xB0: "BCS", 0x90: "BCC", 0x10: "BPL", 0x30: "BMI",
    0x88: "DEY", 0xC8: "INY", 0x0A: "ASL A", 0x3C: "?", 0x0F: "?",
}


def decode(raw):
    op = raw[0]
    name = MNEM.get(op, f"op${op:02X}")
    if len(raw) == 3:
        return f"{name} ${raw[2]:02X}{raw[1]:02X}"
    if len(raw) == 2:
        return f"{name} ${raw[1]:02X}"
    return name


TRACK_WINDOW = (0x5300, 0x5A25)     # where the unpack swap deposits the circuit's own file


def extent_rows(path=None):
    """[(lo, hi, sigs, per_arm_patchable)] from the committed table.

    `per_arm_patchable[i]` is the set of byte offsets that can vary in arm `i` — see the header
    of disasm/track_smc.txt for why that is per arm and not per extent."""
    path = path or os.path.join(ROOT, "disasm/track_smc.txt")
    rows = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = [x.strip() for x in line.split("|")]
        lo_s, hi_s = f[0].split()
        lo, hi = int(lo_s, 16), int(hi_s, 16)
        sigs, per_arm = [], []
        for fld in f[1:]:
            if not fld.startswith("sig:"):
                continue
            spec, _, pa = fld[4:].partition("@")
            n = [int(x, 16) for x in spec.split(",")]
            sigs.append(tuple(zip(n[0::2], n[1::2])))
            per_arm.append({int(x, 16) for x in pa.split(",") if x})
        rows.append((lo, hi, sigs, per_arm))
    return rows


def hook_targets(base, patchmap, rows=None):
    """The track-window addresses a circuit's patches turn into JSR/JMP TARGETS.

    ⭐ These are the circuit's HOOK ENTRIES, and they are the second half of what the port must
    have before a circuit is playable: the patch bytes go into mem[] (the data path), and then
    something has to be there to CALL.  `revs_track_hook()` owns the address→body map, so this
    is the list `revs_track_check()` tests it against — a circuit whose hooks have no bodies is
    refused, exactly as one with uncovered patch bytes is (src/platform/track.h).

    Derived by decoding the PATCHED extent, not by pattern-matching the patch values: an operand
    byte that happens to look like $56xx is not a hook entry unless a JSR/JMP owns it.
    """
    lo_w, hi_w = TRACK_WINDOW
    out = set()
    for lo, hi, _sigs, _patch in (rows or extent_rows()):
        v = bytearray(base[lo:hi + 1])
        for k in range(len(v)):
            if lo + k in patchmap:
                v[k] = patchmap[lo + k]
        k = 0
        while k < len(v):
            op = v[k]
            n = LEN[op]
            if op in (0x20, 0x4C) and k + 2 < len(v):
                t = v[k + 1] | (v[k + 2] << 8)
                if lo_w <= t <= hi_w:
                    out.add(t)
            if op in (0x4C, 0x6C, 0x60, 0x40):
                break
            k += n
    return sorted(out)


FUNC_RE = re.compile(r"^; FUNC (\S+)\s+([0-9a-f]{4}) - ([0-9a-f]{4})")
TARGET_RE = re.compile(r"^(?:JMP|JSR|B[A-Z]{2})\s+0x([0-9a-f]{4})")


def load_funcs():
    """[(lo, hi, name)] from the listing's own FUNC header comments."""
    out = []
    with open(LISTING) as fh:
        for line in fh:
            m = FUNC_RE.match(line)
            if m:
                out.append((int(m.group(2), 16), int(m.group(3), 16), m.group(1)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--emit", metavar="PATH", nargs="?", const="-",
                    help="write the machine-readable extent table (default stdout)")
    ap.add_argument("--check", metavar="PATH", nargs="?",
                    const="disasm/track_smc.txt",
                    help="re-derive and FAIL if the committed table no longer matches the "
                         "circuits present (a stale table is silently wrong, not missing)")
    args = ap.parse_args()
    if args.check and not args.emit:
        args.emit = args.check

    from gen_tracks import CIRCUITS, runtime_image, patch_list

    insns = load_listing()
    starts = sorted(insns)

    def owner(addr):
        """The instruction start that covers `addr`, or None if no listing instruction does."""
        import bisect
        i = bisect.bisect_right(starts, addr) - 1
        if i < 0:
            return None
        s = starts[i]
        return s if s + len(insns[s][0]) > addr else None

    tracks, patches = [], {}
    base = None
    for ssd, dfs, disp in CIRCUITS:
        if not os.path.exists(ssd):
            print(f"# note: {ssd} absent — {disp} skipped", file=sys.stderr)
            continue
        img = runtime_image(ssd, dfs)
        if base is None:
            base = bytes(img)
        pl = patch_list(runtime_image(ssd, dfs))
        if pl:
            tracks.append(dfs)
            patches[dfs] = dict(pl)

    # ⭐ EXTENTS, not instructions.  A patch is a contiguous byte run, and several of these
    # runs SPAN TWO INSTRUCTIONS: `$12FB 18 / $12FC 69 03` (CLC; ADC #$03) becomes the single
    # `20 F1 54` (JSR $54F1).  Folding onto instruction starts reports that as two mangled
    # instructions; folding onto extents reports it as what it is — a three-byte substitution
    # covering two base instructions, with the trailing bytes of the second becoming dead.
    #
    # So: union the patched addresses, group into maximal contiguous runs, then grow each run
    # to whole base instructions and re-merge until stable.
    allpatched = sorted({a for t in tracks for a in patches[t]})
    code = [a for a in allpatched if owner(a) is not None]
    data = [a for a in allpatched if owner(a) is None]

    extents = []
    for a in code:
        if extents and a <= extents[-1][1]:
            extents[-1][1] = max(extents[-1][1], a + 1)
            continue
        extents.append([a, a + 1])
    changed = True
    while changed:
        changed = False
        grown = []
        for lo, hi in extents:
            s = owner(lo)
            nlo = s
            nhi = hi
            while True:
                s2 = owner(nhi - 1)
                end = s2 + len(insns[s2][0])
                if end > nhi:
                    nhi = end
                    continue
                break
            if (nlo, nhi) != (lo, hi):
                changed = True
            if grown and nlo <= grown[-1][1]:
                grown[-1][1] = max(grown[-1][1], nhi)
                changed = True
            else:
                grown.append([nlo, nhi])
        extents = grown

    TERMINATORS = (0x4C, 0x6C, 0x60, 0x40)      # JMP abs / JMP ind / RTS / RTI

    def stream(buf, lo):
        """Decode a byte run as an instruction stream; returns [(addr, bytes, text, dead)].

        Decoding STOPS at a terminator: bytes after a JMP/RTS are unreachable, which is what
        makes a shorter substitution representable at all (the same argument as $2FC0's
        CPX-or-RTS pair).  Those tail bytes are reported as dead, not decoded."""
        out, k = [], 0
        while k < len(buf):
            n = min(LEN[buf[k]], len(buf) - k)
            out.append((lo + k, buf[k:k + n], decode(buf[k:k + n]), False))
            if buf[k] in TERMINATORS:
                k += n
                if k < len(buf):
                    out.append((lo + k, buf[k:], f"({len(buf) - k} dead byte(s))", True))
                return out
            k += n
        return out

    # ⚠ TWO STRUCTURAL PRECONDITIONS, both of which would make an extent substitution wrong
    # in a way no byte comparison can see:
    #
    #  1. NOTHING MAY JUMP INTO the middle of an extent.  The whole point is that the extent
    #     is replaced as a unit, so its interior addresses hold the middle of a different
    #     instruction in the patched arm.  A branch to $248D would be a branch into the
    #     operand of `JMP $56BC`.
    #  2. An extent may not CROSS A FUNCTION BOUNDARY, because the emitted arm is one
    #     straight-line block inside one C function.
    targets = set()
    with open(LISTING) as fh:
        for line in fh:
            if line.startswith(";") or not line.strip():
                continue
            m = INSN_RE.match(line.rstrip("\n"))
            if not m:
                continue
            t = TARGET_RE.match(m.group(3) + " " + m.group(4))
            if t:
                targets.add(int(t.group(1), 16))
    funcs = load_funcs()

    structural = []
    for lo, hi in extents:
        inside = sorted(t for t in targets if lo < t < hi)
        if inside:
            structural.append((lo, "jumped INTO at " +
                               " ".join(f"${t:04X}" for t in inside)))
        owners = {n for flo, fhi, n in funcs if flo <= lo <= fhi or flo <= hi - 1 <= fhi}
        if len(owners) > 1:
            structural.append((lo, f"crosses a function boundary: {sorted(owners)}"))

    hist, anomalies = {}, []
    table = []
    for lo, hi in extents:
        b = base[lo:hi]
        variants = {}
        for t in tracks:
            v = bytearray(b)
            hit = False
            for k in range(len(b)):
                if lo + k in patches[t]:
                    v[k] = patches[t][lo + k]
                    hit = True
            variants[t] = bytes(v) if hit else None

        nbase = len(stream(b, lo))
        opchanged = any(v is not None and v[0] != b[0] for v in variants.values())
        # Does every variant's own decode still END on the extent boundary?  If it does not,
        # the substitution shifts the meaning of bytes OUTSIDE the extent, and the site is
        # not representable as a self-contained swap.
        for t, v in variants.items():
            if v is None:
                continue
            st = stream(v, lo)
            if st and st[-1][3]:
                continue                    # ends in a terminator, tail is dead — fine
            k = sum(len(x[1]) for x in st[:-1]) + LEN[st[-1][1][0]] if st else 0
            if k != len(v):
                anomalies.append((lo, f"{t}: variant decode overruns the extent by "
                                      f"{k - len(v)} byte(s) with no terminator"))
        cls = "instr" if (opchanged or nbase > 1) else "operand"
        hist[cls] = hist.get(cls, 0) + 1

        # ⭐ THE MACHINE-READABLE ROW, and it deliberately carries NO per-circuit values.
        #
        # A signature is the (offset, opcode) pairs of one instruction-stream shape within the
        # extent — enough to tell the arms apart and to emit each one, and nothing more.  The
        # per-circuit operands stay in mem[], read at run time, which is what the SMC classes
        # already do.  That matters for provenance as well as for cleanliness: the table is
        # identical for all six circuits, so committing it carries no third party's data
        # (docs/reference-sources.md §The Nürburgring file).
        sigs = []
        for v in [b] + [variants[t] for t in tracks if variants[t] is not None]:
            sig = tuple((x[0] - lo, x[1][0]) for x in stream(v, lo) if not x[3])
            if sig not in sigs:
                sigs.append(sig)

        # ⭐⭐ PATCHABLE **PER ARM**, and the difference is not cosmetic.
        #
        # An operand inside an arm is only variable across the circuits that can TAKE that arm —
        # and an arm's guard is often satisfiable by exactly one of them.  $248B's unpatched arm
        # needs `BCS` at offset 0 and `JMP` at offset 2; all five expansion circuits write $4C over
        # offset 0, so only Silverstone can be in that arm, so its branch offset is Silverstone's,
        # statically.
        #
        # Treating the extent's whole patchable set as variable in every arm turned that BCS into a
        # runtime-computed branch — and a runtime branch has to dispatch over the enclosing
        # function's labels, which broke the moment an unrelated change split that function.  So
        # the over-conservative form was not merely wasteful: it manufactured a dispatch that could
        # not always be satisfied, and it broke SILVERSTONE, a circuit with no patches at all.
        #
        # ⚠ Still shape-only: this records WHICH OFFSETS can vary in each arm, never any value.
        per_arm = []
        for sig in sigs:
            need = {(o, op) for o, op in sig}
            who = []                       # circuits whose bytes satisfy this arm's guard
            for t in tracks:
                v = variants[t] if variants[t] is not None else b
                if all(v[o] == op for o, op in need):
                    who.append(t)
            if all(b[o] == op for o, op in need):
                who.append(None)           # the unpatched image itself
            per_arm.append(sorted(k for k in range(len(b))
                                  if any(t is not None and lo + k in patches[t] for t in who)))
        table.append((lo, hi, sigs, per_arm))

        if args.summary or args.emit:
            continue
        print(f"${lo:04X}-${hi - 1:04X}  {cls}")
        for a, bb, txt, dead in stream(b, lo):
            src = (insns[a][1] + " " + insns[a][2]) if a in insns else txt
            print(f"        base     ${a:04X}  {bb.hex(' ').upper():8}  {src}")
        for t in tracks:
            v = variants[t]
            if v is None:
                print(f"        {t:8} unchanged")
                continue
            txt = " ; ".join(x[2] for x in stream(v, lo))
            print(f"        {t:8} {v.hex(' ').upper():11}  {txt}")
        print()

    # ⚠ The arms are emitted as an if/else-if chain over the signature guards, so no guard may
    # be satisfiable by another arm's bytes — otherwise the first match wins and a circuit runs
    # the wrong arm silently.  Two signatures collide when one's (offset, opcode) pairs are a
    # subset of the other's.
    for lo, hi, sigs, _p in table:
        for i, a in enumerate(sigs):
            for j, c in enumerate(sigs):
                if i < j and set(a) <= set(c):
                    structural.append((lo, f"signature {i} is a subset of signature {j} — "
                                           f"the guards are not mutually exclusive"))

    if args.emit:
        out = [
            "# GENERATED by `make track-smc` (tools/track_smc.py --emit) — DO NOT EDIT.",
            "#",
            "# The per-circuit SMC surface, at INSTRUCTION-EXTENT granularity.  One row per",
            "# extent of the engine that some circuit's ModifyGameCode rewrites:",
            "#",
            "#   lo hi | sig:off,opcode,...@patchable-offsets | sig:...@... ",
            "#",
            "# `lo`-`hi` are inclusive engine addresses; each `sig` is one instruction-stream",
            "# shape the extent takes, as (byte offset from lo, opcode) pairs, the FIRST being",
            "# the unpatched Silverstone shape.  After the `@` are the offsets that can VARY IN",
            "# THAT ARM — i.e. offsets patched by a circuit whose bytes satisfy that arm's guard.",
            "# ⚠ Per arm, not per extent: an arm's guard is often satisfiable by only one circuit,",
            "# and then its operands are that circuit's and are static.  Treating the extent's",
            "# whole patchable set as variable in every arm manufactured a runtime-computed branch",
            "# in the UNPATCHED arm, which broke Silverstone (see docs/phases.md \u00a75b).",
            "#",
            "# ⭐ There are NO per-circuit values here by design — the operands live in mem[]",
            "# and are read at run time, so this table is the same for all six circuits and",
            "# carries no third party's data.  tools/transpile.py turns each row into an",
            "# 'extent' SMC site.",
            f"# {len(table)} extents, derived from: {' '.join(tracks)}",
        ]
        for lo, hi, sigs, per_arm in table:
            parts = [f"{lo:04X} {hi - 1:04X}"]
            for sig, pa in zip(sigs, per_arm):
                parts.append("sig:" + ",".join(f"{o:X},{op:02X}" for o, op in sig)
                             + "@" + ",".join(f"{o:X}" for o in pa))
            out.append(" | ".join(parts))
        text = "\n".join(out) + "\n"
        if args.check:
            # ⭐ STALENESS GUARD.  The committed table is derived from whichever discs are present,
            # so adding a circuit changes it — and a stale table is silently wrong rather than
            # loudly missing: the new circuit's patch addresses would still be in the union that
            # revs_smc_bytes.h publishes, so the installer would accept it and an arm would bake
            # somebody else's operand.  `make gen` runs this.
            have = ""
            if os.path.exists(args.emit) and args.emit != "-":
                have = open(args.emit).read()
            # Compare only the data lines: the header carries the derived-from list, which is
            # allowed to differ between a five-disc and a six-disc checkout.
            def rows_of(t):
                return [l for l in t.splitlines() if l and not l.startswith("#")]
            if rows_of(have) != rows_of(text):
                print(f"⚠ {args.emit} is STALE — the circuits present imply a different extent "
                      f"table.\n  -> run `make track-smc EMIT=1` and re-check the diff.",
                      file=sys.stderr)
                return 1
            print(f"track-smc: {args.emit} is current "
                  f"({len(table)} extents, {' '.join(tracks)})")
        elif args.emit == "-":
            sys.stdout.write(text)
        else:
            open(args.emit, "w").write(text)
            print(f"wrote {args.emit}: {len(table)} extents from {' '.join(tracks)}")
        if structural or anomalies:
            for lo, why in structural + anomalies:
                print(f"⚠ ${lo:04X}  {why}", file=sys.stderr)
            return 1
        return 0

    print(f"# {len(extents)} CODE extents across {len(tracks)} circuits: {hist}")
    print(f"# {len(data)} patched bytes are outside every disassembled instruction — i.e. DATA,")
    print(f"#   which the generated C already reads from mem[] and needs no SMC site:")
    for a in data:
        who = [t for t in tracks if a in patches[t]]
        print(f"#     ${a:04X}  ${base[a]:02X} -> "
              f"{' '.join(f'${patches[t][a]:02X}({t})' for t in who)}")
    for label, rows in (("anomalies", anomalies), ("structural problems", structural)):
        if rows:
            print(f"# ⚠ {len(rows)} {label}:")
            for lo, why in rows:
                print(f"#     ${lo:04X}  {why}")
    return 1 if (anomalies or structural) else 0


if __name__ == "__main__":
    sys.exit(main())
