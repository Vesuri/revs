#!/usr/bin/env python3
"""Mechanism-(B) eligibility scan for the wide-value cleanup campaign.

Answers ONE question for an address range: which functions still read or write those
cells, and is every one of them native?  A pair can only be relocated out of mem[] when
no SHIPPING transliteration touches it (docs/wide-value-cleanup.md, "Per-variable
procedure" step 1).

⚠ Three traps this tool exists to avoid, each of which produced a wrong verdict when the
scan was done by hand (docs/wide-value-cleanup.md):

  1. The `__t6502` ORACLES in revs_gen.c read mem[] by construction and do NOT block —
     a relocated pair keeps its mem[] cells for exactly those functions to compare
     against.  Counting them makes every pair look blocked.
  2. Attributing a hit to "the last symbol row at or below this address" credits a label
     inside a real routine's extent with that routine's references.  Here attribution is
     by C FUNCTION, parsed from the generated source, so there is no address arithmetic
     to get wrong.
  3. A `region_*` / `FUN_*` name is SHIPPING code until proven otherwise — an expansion
     circuit's hook can re-enter the transliteration (CLAUDE.md, the FOURTH eligibility
     test).  Nothing here is excluded for looking dead on Silverstone.

Calibration (re-run these if the tool is changed — each was a real bug once):
  * $0A/$0B car_heading and $10/$11 edge_nearest are RELOCATED and green, so their
    shipping-reader list must be either empty (car_heading) or exactly the marshalled
    reader the seam already knows about (edge_nearest -> region_23d8).
  * Sabotages, each with its own signature: a new FUN_* reader must raise SHIPPING; a new
    __t6502 reader must raise only the oracle count; a new _core reader must raise only
    native; a bare `LDA(0x7C)` IMMEDIATE must change nothing; `mem[0x007C]` must raise
    SHIPPING.  All five verified 2026-09-03.

Usage:
    tools/wide_eligibility.py 0x80-0x83          # a range
    tools/wide_eligibility.py 0x5E 0x5F          # individual cells
    tools/wide_eligibility.py --base 0x62A0 --stride 3 --count 3   # an SoA lane
    tools/wide_eligibility.py --all-pairs        # every pair named in the ledger
"""
import re, sys, os, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN    = os.path.join(ROOT, 'src/gen/revs_gen.c')
NATIVE = [os.path.join(ROOT, p) for p in
          ('src/gen/revs_native.c', 'src/gen/revs_native_seam.c')]

# ⚠ revs_gen.c writes `void f(void) {`; revs_native.c puts the brace on the NEXT line.  A
# same-line-only regex silently parses zero functions out of the native files, which reads
# as "no native readers" rather than as a parse failure.
FUNC_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_ \t\*]*?\b([A-Za-z0-9_]+)\s*\([^;]*\)\s*\{?\s*$')

def functions(path):
    """[(name, first_line, last_line, text)] for every top-level definition."""
    lines = open(path).read().split('\n')
    out, depth, name, start, buf, pending = [], 0, None, 0, [], None
    for i, l in enumerate(lines):
        if depth == 0:
            m = FUNC_RE.match(l)
            if m and not l.lstrip().startswith(('if', 'for', 'while', 'switch', 'return', '}')):
                pending = (m.group(1), i + 1)
                if l.rstrip().endswith('{'):
                    name, start, buf = pending[0], pending[1], []
            elif l.strip() == '{' and pending:
                name, start, buf = pending[0], pending[1], []
        if name:
            buf.append(l)
        depth += l.count('{') - l.count('}')
        if depth <= 0:
            if name and buf:
                out.append((name, start, i + 1, '\n'.join(buf)))
                pending = None      # ⚠ only on a real close: clearing it every depth-0 line
                                    #   discards the signature before its `{` on the NEXT line
            depth, name, buf = 0, None, []
    return out

MEM_H  = os.path.join(ROOT, 'src/gen/mem.h')
SEAM_H = os.path.join(ROOT, 'src/gen/revs_native_seam.h')

def aliases():
    """addr -> {identifier}.  ⚠⚠ THE TRAP THAT MADE THE FIRST VERSION OF THIS TOOL REPORT
    ZERO NATIVE READERS FOR A PAIR WITH FOUR: only the transliteration spells a cell as raw
    hex.  Native code uses the mem.h names (`point_dist_lo`, `MEM_point_dist_lo`) and the
    seam header's SoA bases (`POINT_DELTA_HI`), so a hex-only scan sees none of it and every
    pair looks native-free.  Calibrating against a KNOWN answer is what caught it."""
    exact, base = {}, {}
    for m in re.finditer(r'#define\s+(MEM_([A-Za-z0-9_]+))\s+0x([0-9A-Fa-f]+)', open(MEM_H).read()):
        a = int(m.group(3), 16)
        exact.setdefault(a, set()).update({m.group(1), m.group(2)})
    for m in re.finditer(r'#define\s+([A-Z][A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)u?', open(SEAM_H).read()):
        base.setdefault(int(m.group(2), 16), set()).add(m.group(1))
    return exact, base

EXACT, BASE = aliases()
INDEX_WINDOW = 8      # how far an SoA base may be indexed before it stops being this cell

def hits(text, addrs):
    """Every reference in `text` to one of `addrs`, by raw hex OR by name."""
    names = set()
    for a in addrs:
        names |= EXACT.get(a, set())
    # An SoA base indexed into the range: `POINT_DELTA_HI + 1` is $84.  Approximate by
    # construction, so these are marked `~` and meant to be read, not counted blindly.
    approx = set()
    for b, ns in BASE.items():
        if any(b <= a <= b + INDEX_WINDOW for a in addrs):
            approx |= ns
    found, lines = [], text.split('\n')
    # ⚠⚠ THE SECOND INSTRUMENT BUG, caught by calibrating against pairs already relocated:
    # matching a BARE hex literal counts IMMEDIATES as address references.  `LDX(0x0A)` is
    # the constant 10, and it made $0A/$0B (car_heading, relocated and green) report four
    # shipping readers that do not touch it at all.  A raw-address access in revs_gen.c is
    # always spelled `mem[0xNNNN...]`; everything else goes through a mem.h name.
    for m in re.finditer(r'mem\[\s*0x([0-9A-Fa-f]{2,4})\b|\b([A-Za-z_][A-Za-z0-9_]*)\b', text):
        if m.group(1) is not None:
            v = int(m.group(1), 16)
            if v not in addrs:
                continue
            tag = v
        elif m.group(2) in names:
            tag = m.group(2)
        elif m.group(2) in approx:
            tag = '~' + m.group(2)
        else:
            continue
        found.append((tag, lines[text[:m.start()].count('\n')].strip()))
    return found

def scan(addrs, label, show=False):
    blocking, oracle, native = {}, {}, {}
    for name, a, b, text in functions(GEN):
        h = hits(text, addrs)
        if not h:
            continue
        (oracle if name.endswith('__t6502') else blocking)[name] = h
    for p in NATIVE:
        for name, a, b, text in functions(p):
            h = hits(text, addrs)
            if h:
                native[name] = h

    print("=== %s  (%s)" % (label, ', '.join('$%04X' % a for a in sorted(addrs))))
    print("  native readers        : %d function(s), %d ref(s)"
          % (len(native), sum(len(v) for v in native.values())))
    print("  __t6502 oracles       : %d function(s)  [do NOT block]" % len(oracle))
    print("  SHIPPING translit.    : %d function(s), %d ref(s)"
          % (len(blocking), sum(len(v) for v in blocking.values())))
    if blocking:
        # ⚠ A shipping transliterated reader does NOT make the relocation illegal — it makes
        # mem[] still authoritative for that reader, so every seam that can reach it must
        # marshal.  That is the expensive route, not the impossible one, and its proof is a
        # real-BBC frame-buffer differential rather than a fixture (the patched arm of a hook
        # seam is gated by nothing).  Route A is to nativize the readers and drop the marshal.
        print("  ⇒ (B) NEEDS DE-TRANSLITERATION (route A) or a shim marshal (route B):")
        for n in sorted(blocking, key=lambda k: -len(blocking[k])):
            print("      %-34s %2d ref(s)" % (n, len(blocking[n])))
    else:
        print("  ⇒ (B) ELIGIBLE — no shipping transliteration touches these cells.")
        print("    ⚠ Eligible is not the same as worth doing: score it site by site")
        print("      (docs/wide-value-cleanup.md §ELEVENTH LESSON) before writing a line.")
    if native:
        print("  native sites (score these):")
        for n in sorted(native, key=lambda k: -len(native[k])):
            print("      %-34s %2d ref(s)" % (n, len(native[n])))
    if show:
        for title, group in (("SHIPPING", blocking), ("native", native)):
            for n, hs in sorted(group.items()):
                for tag, line in hs:
                    print("    [%s] %-26s %-14s %s" % (title, n, tag, line[:74]))
    print()
    return len(blocking)

def parse_addrs(tokens):
    a = set()
    for t in tokens:
        if '-' in t:
            lo, hi = t.split('-')
            a |= set(range(int(lo, 16), int(hi, 16) + 1))
        else:
            a.add(int(t, 16))
    return a

LEDGER = [
    ("point_delta + object_dist", "0x80-0x83 0x55"),
    ("nearest_edge_bearing",      "0x5E 0x5F"),
    ("point_dist",                "0x7C 0x7D"),
    ("lap_length",                "0x59FC 0x59FD"),
    ("plot_ptr",                  "0x70 0x71"),
    ("plot_ptr2",                 "0x72 0x73"),
    ("plot_ptr3 / SLIP_MAG",      "0x8E 0x8F"),
    ("math_lo/hi",                "0x74 0x75"),
]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cells', nargs='*')
    ap.add_argument('--base', type=lambda x: int(x, 16))
    ap.add_argument('--stride', type=int, default=1)
    ap.add_argument('--count', type=int, default=1)
    ap.add_argument('--all-pairs', action='store_true')
    ap.add_argument('--label', default=None)
    ap.add_argument('--show', action='store_true', help='print every matching source line')
    args = ap.parse_args()

    if args.all_pairs:
        for label, cells in LEDGER:
            scan(parse_addrs(cells.split()), label, args.show)
        return 0
    if args.base is not None:
        addrs = {args.base + i * args.stride for i in range(args.count)}
        return 0 if scan(addrs, args.label or ('$%04X stride %d x%d'
                         % (args.base, args.stride, args.count)), args.show) else 0
    if not args.cells:
        ap.print_help()
        return 2
    return 0 if scan(parse_addrs(args.cells), args.label or 'cells', args.show) else 0

if __name__ == '__main__':
    sys.exit(main())
