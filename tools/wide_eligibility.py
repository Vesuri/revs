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
  * Sabotages, each with its own signature: a reference planted in a CALLED transliteration
    (`update_lap_timers`) must raise SHIPPING and flip the verdict; a new UNCALLED `FUN_*`
    reader must surface under ENTRY-WRAPPER ONLY (not SHIPPING — that bucket is the FOURTH
    eligibility test, and dismissing it is the mistake, not the classification); a new
    __t6502 reader must raise only the oracle count; a new _core reader must raise only
    native; a bare `LDA(0x7C)` IMMEDIATE must change nothing; and a local non-address
    `#define ZZ 0x7Cu` used in a twin must change nothing.  All six verified 2026-09-04.
  ⚠⚠ PLANT A SABOTAGE IN THE BODY, NEVER ON THE FUNCTION HEADER LINE.  Appending
    `mem[0x007C] = 1;` after `void update_lap_timers(void) {` stops the line matching either
    header regex, so the WHOLE FUNCTION is skipped and the planted reference is orphaned —
    the sabotage then "survives" for a reason that has nothing to do with what it tests.
    Same class as the FOURTH instrument bug below.

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
ONELINER_RE = re.compile(
    r'^[A-Za-z_][A-Za-z0-9_ \t\*]*?\b([A-Za-z0-9_]+)\s*\([^;{]*\)\s*\{.*\}\s*$')
FUNC_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_ \t\*]*?\b([A-Za-z0-9_]+)\s*\([^;]*\)\s*\{?\s*$')

def functions(path):
    """[(name, first_line, last_line, text)] for every top-level definition."""
    lines = open(path).read().split('\n')
    out, depth, name, start, buf, pending = [], 0, None, 0, [], None
    for i, l in enumerate(lines):
        if depth == 0:
            # ⚠⚠ THE FOURTH INSTRUMENT BUG: the transpiler emits every region ENTRY WRAPPER as
            # a ONE-LINER — `void view_next_scanline(void) { region_7bf7(0x7EF3); }` — and a
            # parser that only accepts a trailing `{` skips all of them.  They are exactly the
            # edges the call graph hangs on: a shipping native caller reaches `region_7bf7`
            # ONLY through one, so dropping them reported the whole view cell chain as dead.
            m1 = ONELINER_RE.match(l)
            if m1:
                out.append((m1.group(1), i + 1, i + 1, l))
                pending = None
                continue
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

MANUAL = os.path.join(ROOT, 'src/gen/revs_manual.c')

# ---------------------------------------------------------------------------
# Reachability.  ⚠⚠ THE THIRD INSTRUMENT BUG: "not an oracle" is NOT the same as
# "shipping".  `region_31d0` (paint_fence_backdrop's fence-fill body) reads plot_ptr
# three times and was reported as a blocker for two passes — but its ONLY callers are
# `FUN_3d68`, called from `paint_fence_backdrop__t6502`, and the uncalled entry wrapper
# `FUN_31d0`.  The twin absorbs the loop, so no shipping path reaches it and nativizing
# it buys nothing (the reader-side twin of the "oracle-only" finding already recorded in
# docs/wide-value-cleanup.md).
#
# This is decidable here because the port has NO dynamic dispatch: `Platform::indirectJmp`
# is a no-op on every backend, so every call in the shipping build is a static C call.
# The one thing the call graph CANNOT see is the FOURTH eligibility test — a circuit's
# hook JMPing back into the transliteration (`FUN_2490`).  Such a target is an UNCALLED
# `FUN_<addr>` wrapper, so those get their own bucket and a manual verdict, never a
# silent "dead".
CALL_RE = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(')

def all_functions():
    out = {}
    for path in [GEN, MANUAL] + NATIVE:
        if not os.path.exists(path):
            continue
        for name, a, b, text in functions(path):
            out[name] = text
    return out

def external_roots(names):
    """Every corpus function named from OUTSIDE src/gen — platform code, the cpu model,
    the backends.  These are the shipping build's real entry points."""
    seen = set()
    for base, _dirs, files in os.walk(os.path.join(ROOT, 'src')):
        if os.path.join('src', 'gen') in base:
            continue
        for f in files:
            if not f.endswith(('.c', '.cpp', '.h')):
                continue
            txt = open(os.path.join(base, f), errors='ignore').read()
            # ⚠ Match a BARE identifier, not `name(`: a VBI handler and a spin-wait hook are
            # passed to `platform_register_vbi` as function POINTERS, so a call-syntax scan
            # misses exactly the entry points that have no static caller.
            for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\b', txt):
                if m.group(1) in names:
                    seen.add(m.group(1))
    seen.discard('__attribute__')       # parser noise, not a routine
    return seen

def closure(seeds, bodies):
    """Forward reachability, NOT traversing out of a `__t6502` oracle: an oracle runs only
    under `make validate`, so its callees are not shipping on its account."""
    reach, work = set(seeds), list(seeds)
    while work:
        f = work.pop()
        if f.endswith('__t6502'):
            continue
        for m in CALL_RE.finditer(bodies.get(f, '')):
            g = m.group(1)
            if g in bodies and g not in reach:
                reach.add(g)
                work.append(g)
    return reach

BODIES = all_functions()
_called = set()
for _n, _t in BODIES.items():
    for _m in CALL_RE.finditer(_t):
        if _m.group(1) in BODIES and _m.group(1) != _n:
            _called.add(_m.group(1))
ROOTS   = external_roots(set(BODIES))
SHIPPING = closure(ROOTS, BODIES)
# Uncalled `FUN_<addr>` wrappers: a sweep-reported branch target with no static caller.
# Either dead (a back-branch label) or a hook/SMC re-entry — the FOURTH test decides, and
# only a human can, so anything reachable only from here is reported, not dismissed.
WRAPPERS = {n for n in BODIES
            if re.fullmatch(r'FUN_[0-9a-f]{4}', n) and n not in _called and n not in SHIPPING}
WRAPPER_REACH = closure(WRAPPERS, BODIES) - SHIPPING

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
    # ⚠⚠ THE FIFTH INSTRUMENT BUG, and it is a FALSE ZERO — the worst kind, because "0 native
    # readers" reads as "nothing to gain" and retires a candidate silently.  A twin may define its
    # own SoA base at the top of the file instead of in the seam header (`#define CAR_DISTANCE_LO
    # 0x08D0` in revs_native.c), and resolving names from mem.h + the seam header alone cannot see
    # it: $08D0 reported 0 native readers against 10 real refs.
    # ⚠ Accepting every local #define blindly would recreate the SECOND bug (immediates counted as
    # addresses): this same file defines `OP_RTS 0x60`, `OP_STA_IND_Y 0x91` and `VIEW_LOW_PAGE
    # 0x7C`, and $7C is point_dist_lo.  So a local define is credible as an address only when it is
    # above the zero page, or when its own name IS the cell's mem.h name at that address.
    for path in NATIVE:
        for m in re.finditer(r'#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+0x([0-9A-Fa-f]+)u?',
                             open(path).read()):
            nm, a = m.group(1), int(m.group(2), 16)
            if a >= 0x0100:
                base.setdefault(a, set()).add(nm)
            elif nm.lower() in {x.lower() for x in exact.get(a, set())}:
                exact.setdefault(a, set()).add(nm)
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
    blocking, oracle, native, hookonly, dead = {}, {}, {}, {}, {}
    for name, a, b, text in functions(GEN):
        h = hits(text, addrs)
        if not h:
            continue
        if name.endswith('__t6502'):
            oracle[name] = h
        elif name in SHIPPING:
            blocking[name] = h
        elif name in WRAPPER_REACH:
            hookonly[name] = h          # only via an uncalled FUN_<addr> entry wrapper
        else:
            dead[name] = h              # only via an oracle: the twin already absorbed it
    for p in NATIVE:
        for name, a, b, text in functions(p):
            h = hits(text, addrs)
            if h:
                native[name] = h

    print("=== %s  (%s)" % (label, ', '.join('$%04X' % a for a in sorted(addrs))))
    print("  native readers        : %d function(s), %d ref(s)"
          % (len(native), sum(len(v) for v in native.values())))
    print("  __t6502 oracles       : %d function(s)  [do NOT block]" % len(oracle))
    print("  oracle-only translit. : %d function(s)  [do NOT block — no shipping caller]"
          % len(dead))
    for n in sorted(dead):
        print("      %-34s %2d ref(s)" % (n, len(dead[n])))
    if hookonly:
        print("  ⚠ ENTRY-WRAPPER ONLY  : %d function(s) — reachable only from an UNCALLED"
              % len(hookonly))
        print("    FUN_<addr> wrapper.  Settle each by the FOURTH eligibility test (a circuit")
        print("    hook can JMP back into the transliteration) before calling it dead:")
        for n in sorted(hookonly, key=lambda k: -len(hookonly[k])):
            print("      %-34s %2d ref(s)" % (n, len(hookonly[n])))
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
