#!/usr/bin/env python3
"""`make fatscan` — FIND THE 6502 RESIDUE AUTOMATICALLY (docs/open-work.md §9).

A ranked scanner for the fat the object-plotter cleanup removed by hand (docs/perf-method.md
§THE OBJECT PLOTTER'S C): the parts of a native twin that exist because the routine used to be
6502 code, not because anything reads them.  Four detectors:

  exit     DEAD EXIT ABI — a struct a core returns (SlotExit, *Exit, AddFlags, ProjPoint, ...)
           or writes through a struct out-parameter, per FIELD: which fields any consumer reads.
           The audit is per TREE: a caller that just returns the struct threads it, so the field
           is live only if a caller further up reads it.  A field whose only consumer is a
           6502-ABI shim storing it into `cpu` is SHIM-only: the core can drop it, and the shim
           rebuilds it only if an oracle needs it (the shortlist at the end says which).
  flag     FLAG REPLAY — an adc_overflow/sbc_overflow whose value reaches only exit fields that
           are dead or shim-only.
  zp       ZERO-PAGE SCRATCH IN A LOOP — a store to mem[$00..$FF] inside a back-edge of the
           TARGET's code (amiga/out/Revs.elf).  The tool cannot tell scratch from game state;
           it names the cell and a human decides.
  marshal  MARSHAL ROUND TRIPS — `*_marshal_in/out` calls on a path that runs in the window.
           ⭐ Nothing that runs in the window is an oracle (no transliteration runs on
           Silverstone — `make transtrap`), so a shim that marshals per frame is a native path
           going through the 6502 ABI.

⭐⭐ RANKED BY WHAT THE TARGET PAYS PER FRAME, never by raw count.  Two measured inputs:
  - executions: a --coverage HOST build driving STRAIGHT_TO_RACE + HOLD_THROTTLE, counters
    zeroed at the window's first frame (`make fatscan` builds and runs it).  CLAUDE.md licenses
    a host counter for COUNTING, never for timing, and this uses it for nothing else.
  - instructions: the plain Amiga ELF (`objdump -dl --inlines`).  Every target instruction is
    credited with its OWN share of its line's host count — an inlined copy gets the share its
    call site supplied, a copy GCC duplicated (unrolled, tail-duplicated) gets 1/D of it, and one
    outside every natural loop at most one execution per entry (price_insns has the three rules
    and why each was needed).  ⇒ what GCC already deleted after inlining prices at ZERO by
    construction, and so does C the target replaced with asm.
The column is therefore an ESTIMATE of instructions executed per frame on the 68000 — the
construction of the dead fields, not the caller's copy of them — and a ranking, not a timing.

⭐ A FIELD IS FOLLOWED THROUGH COPIES, NOT JUST RETURNS (Model.flow): a value copied through a
local, a mask, a struct local, or a native CALLEE'S PARAMETER into that callee's exit is exactly as
live as that exit.  The 6502's flag chain is a V handed core to core — draw_road's `chainV`, each
core copying its entry V to its exit V untested — and read as "passed to a call" it looked live
at every link.  Its one real consumer class is the CIRCUIT-HOOK SEAM (revs_track_hook_regs), which
is reported as such: live on four circuits, gated by `make viewdiff`, never by this scan.

⚠ THE PART IT CANNOT FINISH (open-work §9): an ORACLE that reaches a native shim may BRANCH on an
exit.  The shortlist follows every JSR to such a shim in disasm/listing.txt forward and says, per
register, whether the next instructions READ it, pass it into another CALL, or carry it out of
the routine (EXIT); a human confirms each.  Circuit hooks (revs_track_hooks.c) are listed, not
analysed.

    make fatscan [FATSCAN_ARGS="--top=60 --detector=exit,zp --fields=<core> --no-shortlist"]
      --fields=<core>   every exit field of one producer, with each consumer reason
      --debug=<fn>      each marshal call of one routine, priced separately
"""
import json, os, re, subprocess, sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.setrecursionlimit(20000)

NATIVE_TUS = ['src/gen/revs_native.c', 'src/gen/revs_native_seam.c', 'src/gen/revs_native_abi.c']
NATIVE_FILES = set(NATIVE_TUS) | {'src/gen/revs_native_seam.h'}
FLAG_HELPERS = {'adc_overflow', 'sbc_overflow'}
MARSHAL_RE = re.compile(r'^(\w+)_marshal_(in|out)$')
WINDOW_GUARD = 'race_resume_point'    # a crash/session reset — must not run in the window

# ────────────────────────────────────────────────────────────────────────────────────────────
# Inputs
# ────────────────────────────────────────────────────────────────────────────────────────────

def rel(path):
    p = os.path.normpath(path)
    return os.path.relpath(p, REPO) if os.path.isabs(p) else p

def host_counts(objdir):
    """((file, line) -> executions, function name -> entries) over the window, summed over every
    host TU that compiled them (a header's inline body is compiled into each TU including it)."""
    counts, entries = defaultdict(int), defaultdict(int)
    gcdas = []
    for root, _, files in os.walk(objdir):
        gcdas += [os.path.join(root, f) for f in files if f.endswith('.gcda')]
    if not gcdas:
        sys.exit(f'fatscan: no .gcda under {objdir} — run `make fatscan`, not this script')
    for g in gcdas:
        stem = g[:-5]
        src = None
        relstem = os.path.relpath(stem, objdir)
        for ext in ('.c', '.cpp'):
            if os.path.exists(os.path.join(REPO, relstem + ext)):
                src = relstem + ext
        if not src:
            continue
        out = subprocess.run(['xcrun', 'llvm-cov', 'gcov', '-t', '-b', '-o', stem, src], cwd=REPO,
                             capture_output=True, text=True).stdout
        cur = None
        for ln in out.splitlines():
            if ln.startswith('function '):
                m = re.match(r'function (\S+) called (\d+)', ln)
                if m: entries[m.group(1)] += int(m.group(2))
                continue
            parts = ln.split(':', 2)
            if len(parts) < 3: continue
            c, n = parts[0].strip(), parts[1].strip()
            if n == '0':
                if parts[2].startswith('Source:'): cur = rel(parts[2][7:])
                continue
            if c in ('-', '#####', '=====') or cur is None: continue
            try: counts[(cur, int(n))] += int(c.rstrip('*'))
            except ValueError: pass
    return counts, entries

INSN_RE = re.compile(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$')
FUNC_RE = re.compile(r'^([0-9a-f]+) <(.+)>:$')
HEAD_RE = re.compile(r'^(\S+)\(\):$')
LINE_RE = re.compile(r'^(/\S+):(\d+)(?: \(discriminator \d+\))?$')
INL_RE  = re.compile(r'^inlined by (/\S+):(\d+)(?: \(discriminator \d+\))? \((\S+)\)$')
CLONE_RE = re.compile(r'(\.(constprop|isra|part|cold|lto_priv)\.\d+)+$')

def base_fn(nm):
    return CLONE_RE.sub('', nm or '')

def elf_insns(elf):
    """Every target instruction: addr, fn (the ELF function), mn, ops, inner = (file, line) of
    the innermost source line, ifn = the function that line belongs to, chain = the inline
    chain outward [(file, line, caller)], innermost call site first (`objdump -dl --inlines`
    prints it before each instruction)."""
    out = subprocess.run(['m68k-amiga-elf-objdump', '-dl', '--inlines', '--no-show-raw-insn', elf],
                         capture_output=True, text=True).stdout
    if not out:
        sys.exit('fatscan: m68k-amiga-elf-objdump produced nothing — `. amiga/env.sh` first?')
    insns, func, ifn, inner, chain = [], None, None, (None, 0), []
    for ln in out.splitlines():
        m = FUNC_RE.match(ln)
        if m: func = m.group(2); inner = (None, 0); chain = []; continue
        m = HEAD_RE.match(ln)
        if m: ifn = m.group(1); continue
        m = LINE_RE.match(ln)
        if m: inner = (rel(m.group(1)), int(m.group(2))); chain = []; continue
        m = INL_RE.match(ln)
        if m: chain.append((rel(m.group(1)), int(m.group(2)), m.group(3))); continue
        m = INSN_RE.match(ln)
        if m and func:
            insns.append(dict(addr=int(m.group(1), 16), fn=func, mn=m.group(2), ops=m.group(3),
                              inner=inner, ifn=ifn, chain=chain))
            chain = []
    return insns

UNCOND = re.compile(r'^(bra|jmp|rts|rte|rtr)(\.[bswl])?$')

def natural_loops(lst):
    """The addresses of one function's instructions that lie inside a NATURAL loop: basic blocks
    from the branches, a back-edge wherever a block's successor DOMINATES it, and the loop body = the header plus every block that reaches the back-edge's source without
    passing through the header.  (An address RANGE is not a loop: a back-edge from the end of a
    function spans every block laid out between, loop or not.)"""
    addrs = [i['addr'] for i in lst]
    at = {a: k for k, a in enumerate(addrs)}
    tgt = {}
    for k, i in enumerate(lst):
        if BRANCH_MN.match(i['mn']) or i['mn'].startswith('rt'):
            m = TARGET_RE.search(i['ops'])
            tgt[k] = at.get(int(m.group(1), 16)) if m else None
    leaders = {0}
    for k, t in tgt.items():
        if t is not None: leaders.add(t)
        if k + 1 < len(lst): leaders.add(k + 1)
    starts = sorted(leaders)
    block_of = {}
    for bi, st in enumerate(starts):
        end = starts[bi + 1] if bi + 1 < len(starts) else len(lst)
        for k in range(st, end): block_of[k] = bi
    succ = defaultdict(set)
    pred = defaultdict(set)
    for bi, st in enumerate(starts):
        end = (starts[bi + 1] if bi + 1 < len(starts) else len(lst)) - 1
        last = lst[end]
        t = tgt.get(end)
        if t is not None: succ[bi].add(block_of[t])
        if not UNCOND.match(last['mn']) and end + 1 < len(lst): succ[bi].add(bi + 1)
    for u, vs in succ.items():
        for v in vs: pred[v].add(u)
    # ⭐ A back-edge is u -> h with h DOMINATING u, not merely h at a lower address: GCC lays
    # cold blocks out at the end of a function and jumps back to the join point, and treating
    # that as a loop put a whole unrolled marshal "in a loop" and priced it 5x.
    order, seen, stack = [], set(), [(0, iter(sorted(succ[0])))]
    seen.add(0)
    while stack:
        n, it = stack[-1]
        nxt = next(it, None)
        if nxt is None:
            order.append(n); stack.pop()
        elif nxt not in seen:
            seen.add(nxt); stack.append((nxt, iter(sorted(succ[nxt]))))
    rpo = order[::-1]
    num = {b: k for k, b in enumerate(rpo)}
    idom = {0: 0}
    changed = True
    while changed:
        changed = False
        for b in rpo[1:]:
            ps = [p for p in pred[b] if p in idom]
            if not ps: continue
            new = ps[0]
            for p in ps[1:]:
                a, c = p, new
                while a != c:
                    while num[a] > num[c]: a = idom[a]
                    while num[c] > num[a]: c = idom[c]
                new = a
            if idom.get(b) != new:
                idom[b] = new; changed = True

    def dominates(h, u):
        while u in idom:
            if u == h: return True
            if idom[u] == u: return False
            u = idom[u]
        return False

    inloop = set()
    for u, vs in list(succ.items()):
        for h in vs:
            if u not in idom or not dominates(h, u): continue
            body, stack = {h}, [u]
            while stack:
                n = stack.pop()
                if n in body: continue
                body.add(n); stack += list(pred[n])
            inloop |= body
    return {lst[k]['addr'] for k in range(len(lst)) if block_of.get(k) in inloop}

def price_insns(insns, counts, entries, frames):
    """Each instruction's estimated executions per frame, in ins['x'], and whether it sits in
    a natural loop of its target function, in ins['loop'].
      - A line's executions E: a non-inlined line runs as often as the host ran it.  An INLINED
        line's host count is the total over every call site, so this copy gets the share its
        outermost call site supplied: count(line) × count(call site) / entries(the function
        inlined there).
      - ⭐ GCC DUPLICATES a line — unrolling and tail duplication — and a host count cannot tell
        the copies apart, so crediting each copy with E counted a six-way-unrolled marshal,
        duplicated onto six exit paths, 36 times over.  The duplication factor D of a line (at
        one call site, in one function) is the largest number of its RUNS (maximal consecutive
        instructions) that repeat one MNEMONIC SEQUENCE; each instruction is credited E / D.
        ⚠ A heuristic: a line whose instruction scheduling splits it into identical-looking
        runs reads as duplicated and prices low."""
    byfn = defaultdict(list)
    for i in insns: byfn[i['fn']].append(i)
    for fn, lst in byfn.items():
        loop = natural_loops(lst)
        cap = entries.get(base_fn(fn), 0)
        groups = defaultdict(list)                      # key -> [runs] -> [instructions]
        prev = None
        for i in lst:
            key = (i['inner'], tuple(i['chain']))
            if key != prev: groups[key].append([])
            groups[key][-1].append(i)
            prev = key
        for (inner, chain), runs in groups.items():
            f, l = inner
            c = counts.get((f, l), 0) if f else 0
            if c and chain:
                site = chain[-1]
                f1 = chain[-2][2] if len(chain) >= 2 else runs[0][0]['ifn']
                e = entries.get(base_fn(f1), 0)
                c = c * counts.get((site[0], site[1]), 0) / e if e else 0
            shapes = defaultdict(int)
            for r in runs: shapes[tuple(x['mn'] for x in r)] += 1
            d = max(shapes.values())
            for r in runs:
                for i in r:
                    i['loop'] = i['addr'] in loop
                    # outside every natural loop an instruction runs at most once per entry
                    i['x'] = (c / d if i['loop'] else min(c / d, cap)) / frames
    return insns

# ────────────────────────────────────────────────────────────────────────────────────────────
# The clang AST
# ────────────────────────────────────────────────────────────────────────────────────────────

class Ast:
    """One TU's JSON AST with locations resolved and parent links.  ⚠ clang's JSON dumper
    ELIDES `file` and `line` when they equal the previously written location, in document
    order, so every location has to be replayed in exactly the order it was written: a node's
    `loc` (spelling, then expansion), its range's begin, then its end, then its children."""

    def __init__(self, tu, cflags):
        cmd = ['clang', '-fsyntax-only', '-Xclang', '-ast-dump=json'] + cflags + [tu]
        r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'fatscan: clang could not parse {tu}:\n{r.stderr[:2000]}')
        self.root = json.loads(r.stdout)
        self.file, self.line = None, 0
        self.funcs = []                       # FunctionDecl nodes with a body, in our files
        self.records = {}                     # RecordDecl id -> [(field name, type)]
        self.typedefs = {}                    # typedef name -> RecordDecl id
        self._walk(self.root, None, None)

    def _bare(self, d):
        if not d: return None
        if 'file' in d: self.file = d['file']
        if 'line' in d: self.line = d['line']
        return (self.file, self.line)

    def _loc(self, d):
        if not d: return None
        if 'spellingLoc' in d or 'expansionLoc' in d:
            s = self._bare(d.get('spellingLoc'))
            e = self._bare(d.get('expansionLoc'))
            return e or s
        return self._bare(d)

    def _walk(self, n, parent, fn):
        loc = self._loc(n.get('loc'))
        rng = n.get('range') or {}
        b = self._loc(rng.get('begin'))
        e = self._loc(rng.get('end'))
        where = loc or b
        n['_file'], n['_line'] = (where if where else (None, 0))
        n['_end'] = e[1] if e else n['_line']
        n['_parent'] = parent
        k = n.get('kind')
        if k == 'FunctionDecl' and any(c.get('kind') == 'CompoundStmt' for c in n.get('inner', [])):
            if rel(n['_file'] or '') in NATIVE_FILES:
                fn = n
                self.funcs.append(n)
        n['_fn'] = fn
        if k == 'RecordDecl' and n.get('completeDefinition'):
            self.records[n['id']] = [(c.get('name'), c['type']['qualType'])
                                     for c in n.get('inner', []) if c.get('kind') == 'FieldDecl']
        if k == 'TypedefDecl':
            for c in n.get('inner', []):
                d = c.get('ownedTagDecl') or c.get('decl')
                if d and d.get('kind') == 'RecordDecl':
                    self.typedefs[n['name']] = d['id']
        for c in n.get('inner', []):
            self._walk(c, n, fn)

def walk(n):
    yield n
    for c in n.get('inner', []):
        yield from walk(c)

TRANSPARENT = {'ParenExpr', 'ImplicitCastExpr', 'ExprWithCleanups', 'ConstantExpr',
               'MaterializeTemporaryExpr', 'FullExpr'}

def up(n):
    """The first parent that is not a transparent wrapper, and the child it was reached from."""
    child, p = n, n['_parent']
    while p is not None and p.get('kind') in TRANSPARENT:
        child, p = p, p['_parent']
    return p, child

def ref_decl(n):
    """The declaration a DeclRefExpr (through wrappers) names, or None."""
    while n.get('kind') in TRANSPARENT and n.get('inner'):
        n = n['inner'][0]
    if n.get('kind') == 'DeclRefExpr':
        return n.get('referencedDecl')
    return None

def callee_name(call):
    d = ref_decl(call['inner'][0]) if call.get('inner') else None
    return d.get('name') if d else None

def base_type(qt):
    return qt.replace('const ', '').replace('volatile ', '').strip()

def ret_type(fn):
    return base_type(fn['type']['qualType'].split('(')[0])

# ────────────────────────────────────────────────────────────────────────────────────────────
# Detector 1: per-field exit liveness, per TREE
# ────────────────────────────────────────────────────────────────────────────────────────────

class Model:
    def __init__(self, asts):
        self.fn = {}             # name -> FunctionDecl (first definition seen)
        self.records = {}
        self.typedefs = {}
        seen = set()
        self.calls = defaultdict(list)       # callee name -> [CallExpr]
        self.refs = defaultdict(list)        # decl id -> [DeclRefExpr]
        self.addr_taken = set()
        for a in asts:
            self.records.update(a.records)
            self.typedefs.update(a.typedefs)
            for f in a.funcs:
                key = (f['name'], rel(f['_file']), f['_line'])
                if key in seen: continue          # a header's inline body, seen in another TU
                seen.add(key)
                self.fn.setdefault(f['name'], f)
                for n in walk(f):
                    k = n.get('kind')
                    if k == 'CallExpr':
                        nm = callee_name(n)
                        if nm: self.calls[nm].append(n)
                    elif k == 'DeclRefExpr':
                        d = n.get('referencedDecl', {})
                        self.refs[d.get('id')].append(n)
                        if d.get('kind') == 'FunctionDecl':
                            p, _ = up(n)
                            if not (p and p.get('kind') == 'CallExpr' and ref_decl(p['inner'][0]) is d):
                                self.addr_taken.add(d.get('name'))
        self.memo = {}

    def fields(self, tname, prefix=''):
        """Leaf field paths of a struct type, nested structs flattened ('tail.hi')."""
        rid = self.typedefs.get(base_type(tname))
        if rid is None or rid not in self.records: return None
        out = []
        for nm, ty in self.records[rid]:
            sub = self.fields(ty, prefix + nm + '.')
            out += sub if sub else [prefix + nm]
        return out

    def is_struct(self, tname):
        return self.fields(tname) is not None

    # A reason is (path, kind, detail): kind in read / cpu / escape / thread / thread-out.
    def consume(self, e, g, path=''):
        """Who reads the struct value `e` (inside function g), as reasons."""
        p, child = up(e)
        k = p.get('kind') if p else None
        if k == 'MemberExpr' and not p.get('isArrow'):
            return self.member(p, g, (path + '.' if path else '') + p['name'])
        if k in ('CompoundStmt', 'IfStmt', 'ForStmt', 'WhileStmt', 'DoStmt', 'LabelStmt',
                 'CaseStmt', 'DefaultStmt', None):
            return []                                          # discarded
        if k == 'CStyleCastExpr' and p['type']['qualType'] == 'void':
            return []
        if k == 'ReturnStmt':
            if ret_type(g) == base_type(e['type']['qualType']):
                return [(path, 'thread', g['name'])]
            return [(path, 'escape', 'returned as another type')]
        if k == 'VarDecl':
            return self.var(p, g, path)
        if k == 'BinaryOperator' and p.get('opcode') == '=' and p['inner'][1] is child:
            lhs = p['inner'][0]
            d = ref_decl(lhs)
            if d and d.get('kind') == 'VarDecl':
                return self.var_by_id(d['id'], g, path)
            if lhs.get('kind') == 'UnaryOperator' and lhs.get('opcode') == '*':
                d = ref_decl(lhs['inner'][0])
                i = self.param_index(g, d)
                if i is not None:
                    return [(path, 'thread-out', (g['name'], i))]
            return [(path, 'escape', 'stored through ' + lhs.get('kind', '?'))]
        if k == 'CallExpr':
            return [(path, 'escape', 'passed to ' + str(callee_name(p)))]
        return [(path, 'escape', k)]

    def member(self, m, g, path):
        """A field read `x.path` — is it a store target, a cpu publish, or a real read?"""
        p, child = up(m)
        if p is not None and p.get('kind') == 'MemberExpr' and not p.get('isArrow'):
            return self.member(p, g, path + '.' + p['name'])
        if p is not None and p.get('kind') == 'BinaryOperator' and p.get('opcode') == '=' \
                and p['inner'][0] is child:
            return []                                          # a store into the field
        if p is not None and p.get('kind') == 'UnaryOperator' and p.get('opcode') == '&':
            return [(path, 'escape', 'field address taken')]
        out = []
        for sink in self.flow(m, g, frozenset()):
            if sink[0] == 'cpu': out.append((path, 'cpu', sink[1]))
            elif sink[0] == 'field': out.append((path, 'thread-field', (sink[1], sink[2])))
            else: out.append((path, 'read', sink[1]))
        return out

    STMT = ('CompoundStmt', 'IfStmt', 'ForStmt', 'WhileStmt', 'DoStmt', 'LabelStmt', 'CaseStmt',
            'DefaultStmt', 'SwitchStmt', None)

    def flow(self, e, g, seen):
        """Where a SCALAR value goes, as sinks: ('cpu', (g, R)) — stored into cpu.R;
        ('field', producer key, leaf) — becomes that producer's exit field, so it is exactly as
        live as the field; ('read', where) — anything that USES it (a test, arithmetic, a store
        into game state).  ⭐ It follows copies: casts, `& 1`, locals, a struct local's fields,
        and a native CALLEE'S PARAMETER into that callee's body — the 6502's flag chain is a V
        handed core to core, each copying its entry V to its exit V without testing it, and
        read as "passed to a call" that chain looked live at every link."""
        p, child = up(e)
        k = p.get('kind') if p else None
        if k in self.STMT:
            if k != 'CompoundStmt' and k is not None and 'type' in child and \
                    child is next((c for c in p.get('inner', []) if 'type' in c), None):
                return [('read', f"{g['name']}:{e['_line']} (condition)")]
            return []                                          # discarded
        if k == 'CStyleCastExpr':
            return [] if p['type']['qualType'] == 'void' else self.flow(p, g, seen)
        if k == 'BinaryOperator' and p.get('opcode') == '&':
            other = [c for c in p['inner'] if c is not child]
            if other and other[0].get('kind') == 'IntegerLiteral':
                return self.flow(p, g, seen)
        if k == 'BinaryOperator' and p.get('opcode') == '=' and p['inner'][1] is child:
            return self.store(p['inner'][0], g, seen)
        if k == 'VarDecl':
            return self.var_flow(p['id'], g, seen)
        if k == 'InitListExpr':
            path, il = [], p
            while True:
                ty = base_type(il['type']['qualType'])
                names = [nm for nm, _ in self.records.get(self.typedefs.get(ty), [])]
                i = next((j for j, c in enumerate(il.get('inner', [])) if c is child), None)
                if i is None or i >= len(names): return [('read', 'init list')]
                path.insert(0, names[i])
                q, qc = up(il)
                if q is not None and q.get('kind') == 'InitListExpr':
                    child, il = qc, q
                    continue
                break
            leaf = '.'.join(path)
            while q is not None and q.get('kind') == 'CompoundLiteralExpr':
                q, qc = up(q)
            if q is not None and q.get('kind') == 'ReturnStmt' and ty == ret_type(g):
                return [('field', (g['name'], 'ret'), leaf)]
            if q is not None and q.get('kind') == 'VarDecl':
                return self.struct_field(q['id'], leaf, g, seen)
            return [('read', 'init list ' + str(q.get('kind') if q else None))]
        if k == 'CallExpr' and p['inner'][0] is not child:
            h = self.fn.get(callee_name(p))
            args = p['inner'][1:]
            i = next((j for j, a in enumerate(args) if a is child), None)
            if h is None or i is None:
                return [('read', f"{g['name']}:{e['_line']} -> {callee_name(p)}")]
            parms = [c for c in h.get('inner', []) if c.get('kind') == 'ParmVarDecl']
            if i >= len(parms): return [('read', 'varargs')]
            return self.var_flow(parms[i]['id'], h, seen)
        return [('read', f"{g['name']}:{e['_line']}")]

    def store(self, lhs, g, seen):
        """Sinks of a value stored into `lhs`."""
        path = []
        n = lhs
        while n.get('kind') == 'MemberExpr':
            path.insert(0, n['name'])
            if n.get('isArrow'):
                d = ref_decl(n['inner'][0])
                i = self.param_index(g, d)
                if i is not None and self.out_param(g, i):
                    return [('field', (g['name'], ('out', i)), '.'.join(path))]
                return [('read', 'store through a pointer')]
            n = n['inner'][0]
            while n.get('kind') in TRANSPARENT and n.get('inner'): n = n['inner'][0]
        d = ref_decl(n)
        if d and d.get('name') == 'cpu' and len(path) == 1:
            return [('cpu', (g['name'], path[0]))]
        if d and d.get('kind') in ('VarDecl', 'ParmVarDecl') and d.get('id') in self.locals_of(g):
            if path:
                return self.struct_field(d['id'], '.'.join(path), g, seen)
            return self.var_flow(d['id'], g, seen)
        return [('read', 'store into ' + (d.get('name') if d else lhs.get('kind', '?')))]

    def locals_of(self, g):
        if '_locals' not in g:
            g['_locals'] = {n['id'] for n in walk(g) if n.get('kind') in ('VarDecl', 'ParmVarDecl')
                            and n.get('storageClass') != 'static'}
        return g['_locals']

    def var_flow(self, vid, g, seen):
        """Sinks of every read of a scalar local or parameter."""
        if (vid, 'v') in seen: return []
        seen = seen | {(vid, 'v')}
        out = []
        for r in self.refs.get(vid, []):
            if r['_fn'] is not g: continue
            p, child = up(r)
            if p is not None and p.get('kind') == 'BinaryOperator' and p.get('opcode') == '=' \
                    and p['inner'][0] is child:
                continue
            if p is not None and p.get('kind') == 'UnaryOperator' and p.get('opcode') == '&':
                out.append(('read', 'address taken')); continue
            out += self.flow(r, g, seen)
        return out

    def struct_field(self, vid, leaf, g, seen):
        """Sinks of a value stored into field `leaf` of struct local `vid`: every later read of
        that field, and the struct itself if it is returned."""
        if (vid, leaf) in seen: return []
        seen = seen | {(vid, leaf)}
        out = []
        for r in self.refs.get(vid, []):
            if r['_fn'] is not g: continue
            p, child = up(r)
            if p is not None and p.get('kind') == 'MemberExpr' and not p.get('isArrow'):
                m, path = p, p['name']
                q, qc = up(m)
                while q is not None and q.get('kind') == 'MemberExpr' and not q.get('isArrow'):
                    m, path = q, path + '.' + q['name']; q, qc = up(m)
                if not related(path, leaf): continue
                if q is not None and q.get('kind') == 'BinaryOperator' and q.get('opcode') == '=' \
                        and q['inner'][0] is qc:
                    continue
                out += self.flow(m, g, seen)
                continue
            if p is not None and p.get('kind') == 'BinaryOperator' and p.get('opcode') == '=' \
                    and p['inner'][0] is child:
                continue
            if p is not None and p.get('kind') == 'ReturnStmt':
                out.append(('field', (g['name'], 'ret'), leaf)); continue
            if p is not None and p.get('kind') == 'VarDecl':
                continue                      # copied whole into another local: rare, not followed
            if p is not None and p.get('kind') == 'UnaryOperator' and p.get('opcode') == '&':
                q, _ = up(p)
                if q is not None and q.get('kind') == 'CallExpr' and callee_name(q) == 'revs_track_hook_regs':
                    # ⚠ THE CIRCUIT-HOOK SEAM hands every register to an expansion circuit's hook
                    # (CLAUDE.md §a hook seam) — live on four circuits, and no Silverstone run
                    # can say otherwise; `make viewdiff` is the gate that can.
                    out.append(('read', f"{g['name']}: circuit-hook seam (revs_track_hook_regs)")); continue
            out.append(('read', f'struct {r.get("referencedDecl", {}).get("name")} used whole'))
        return out

    def param_index(self, g, d):
        if not d: return None
        parms = [c for c in g.get('inner', []) if c.get('kind') == 'ParmVarDecl']
        for i, c in enumerate(parms):
            if c.get('id') == d.get('id'): return i
        return None

    def var(self, vd, g, path=''):
        return self.var_by_id(vd['id'], g, path)

    def var_by_id(self, vid, g, path=''):
        out = []
        for r in self.refs.get(vid, []):
            if r['_fn'] is not g: continue
            p, child = up(r)
            k = p.get('kind') if p else None
            if k == 'BinaryOperator' and p.get('opcode') == '=' and p['inner'][0] is child:
                continue                                       # a whole-struct store into v
            if k == 'MemberExpr' and not p.get('isArrow'):
                out += self.member(p, g, (path + '.' if path else '') + p['name'])
                continue
            if k == 'UnaryOperator' and p.get('opcode') == '&':
                q, qc = up(p)
                if q is not None and q.get('kind') == 'CallExpr':
                    h = self.fn.get(callee_name(q))
                    args = q['inner'][1:]
                    i = next((j for j, a in enumerate(args) if a is qc), None)
                    if h is not None and i is not None and self.out_param(h, i):
                        continue                               # an out-param: the reads are v.f
                out.append((path, 'escape', 'address taken'))
                continue
            out += self.consume(r, g, path)
        return out

    def out_param(self, h, i):
        """Does h write a struct through its i-th (pointer) parameter?"""
        parms = [c for c in h.get('inner', []) if c.get('kind') == 'ParmVarDecl']
        if i >= len(parms): return False
        ty = parms[i]['type']['qualType']
        if not ty.endswith('*') or ty.startswith('const') or not self.is_struct(ty[:-1]):
            return False
        for r in self.refs.get(parms[i]['id'], []):
            p, _ = up(r)
            if p is not None and ((p.get('kind') == 'MemberExpr' and p.get('isArrow')) or
                                  (p.get('kind') == 'UnaryOperator' and p.get('opcode') == '*')):
                return True
        return False

    def producers(self):
        """(function, 'ret') and (function, ('out', i)) for every struct a function hands back."""
        out = []
        for nm, f in self.fn.items():
            if self.is_struct(ret_type(f)):
                out.append((nm, 'ret', ret_type(f)))
            parms = [c for c in f.get('inner', []) if c.get('kind') == 'ParmVarDecl']
            for i, c in enumerate(parms):
                if self.out_param(f, i):
                    out.append((nm, ('out', i), base_type(c['type']['qualType'][:-1])))
        return out

    def reasons(self, key, stack=()):
        """Every consumer reason of producer key, with thread/thread-out resolved up the tree."""
        if key in self.memo: return self.memo[key]
        if key in stack: return []                             # recursion: the other arm decides
        nm, which = key
        f = self.fn[nm]
        raw = []
        sites = self.calls.get(nm, [])
        if nm in self.addr_taken:
            raw.append(('', 'escape', 'address taken'))
        if not sites and f.get('storageClass') != 'static' and not f.get('inline'):
            raw.append(('', 'escape', 'no native call site (called from outside the native TUs?)'))
        for c in sites:
            g = c['_fn']
            if which == 'ret':
                raw += self.consume(c, g)
            else:
                i = which[1]
                args = c['inner'][1:]
                if i >= len(args): continue
                a = args[i]
                while a.get('kind') in TRANSPARENT and a.get('inner'): a = a['inner'][0]
                if a.get('kind') == 'UnaryOperator' and a.get('opcode') == '&':
                    d = ref_decl(a['inner'][0])
                    if d and d.get('kind') == 'VarDecl':
                        raw += self.var_by_id(d['id'], g)
                        continue
                d = ref_decl(a)
                j = self.param_index(g, d)
                if j is not None and self.out_param(g, j):
                    raw.append(('', 'thread-out', (g['name'], j)))
                    continue
                raw.append(('', 'escape', 'out-param argument ' + a.get('kind', '?')))
        out = []
        for path, kind, det in raw:
            if kind == 'thread':
                up_ = self.reasons((det, 'ret'), stack + (key,))
                out += [(join(path, p2), k2, d2) for p2, k2, d2 in up_ if related(path, p2)]
            elif kind == 'thread-field':
                key2, leaf2 = det
                if key2[0] not in self.fn: continue
                up_ = self.reasons(key2, stack + (key,))
                out += [(path, k2, d2) for p2, k2, d2 in up_ if related(leaf2, p2)]
            elif kind == 'thread-out':
                up_ = self.reasons((det[0], ('out', det[1])), stack + (key,))
                out += [(join(path, p2), k2, d2) for p2, k2, d2 in up_ if related(path, p2)]
            else:
                out.append((path, kind, det))
        if not stack: self.memo[key] = out
        return out

def related(a, b):
    return not a or not b or a == b or a.startswith(b + '.') or b.startswith(a + '.')

def join(a, b):
    return a if len(a) >= len(b) else b

def field_status(model, key, tname):
    """leaf path -> ('dead'|'shim'|'live', [reasons])"""
    rs = model.reasons(key)
    out = {}
    for leaf in model.fields(tname):
        mine = [r for r in rs if related(leaf, r[0])]
        if any(k in ('read', 'escape') for _, k, _ in mine):
            out[leaf] = ('live', mine)
        elif mine:
            out[leaf] = ('shim', mine)
        else:
            out[leaf] = ('dead', [])
    return out

def constructions(model, fname, key, tname):
    """(leaf path, file, line) for every place fname builds a field of its exit."""
    f = model.fn[fname]
    leaves = model.fields(tname)
    out = []
    for n in walk(f):
        k = n.get('kind')
        if k == 'InitListExpr' and base_type(n['type']['qualType']) == tname:
            flat = []
            def flatten(il, prefix):
                ty = base_type(il['type']['qualType'])
                rid = model.typedefs.get(ty)
                names = [nm for nm, _ in model.records.get(rid, [])]
                for nm, el in zip(names, il.get('inner', [])):
                    if el.get('kind') == 'InitListExpr':
                        flatten(el, prefix + nm + '.')
                    else:
                        flat.append((prefix + nm, el))
            flatten(n, '')
            out += [(p, rel(el['_file'] or ''), el['_line']) for p, el in flat if p in leaves]
        elif k == 'BinaryOperator' and n.get('opcode') == '=':
            lhs = n['inner'][0]
            path = []
            while lhs.get('kind') == 'MemberExpr':
                path.insert(0, lhs['name'])
                if lhs.get('isArrow'):
                    d = ref_decl(lhs['inner'][0])
                    ok = key[1] != 'ret' and d and model.param_index(f, d) == key[1][1]
                    break
                lhs = lhs['inner'][0]
                while lhs.get('kind') in TRANSPARENT and lhs.get('inner'): lhs = lhs['inner'][0]
            else:
                d = ref_decl(lhs)
                ok = bool(path) and d and d.get('kind') == 'VarDecl' and \
                     base_type(lhs['type']['qualType']) == tname
            if path and ok:
                p = '.'.join(path)
                out += [(l, rel(n['_file'] or ''), n['_line']) for l in leaves if related(l, p)]
    return out

# ────────────────────────────────────────────────────────────────────────────────────────────
# Detector 3: zero-page stores inside a back-edge of the target's code
# ────────────────────────────────────────────────────────────────────────────────────────────

BRANCH_MN = re.compile(r'^(b(ra|cc|cs|eq|ne|ge|gt|hi|le|ls|lt|mi|pl|vc|vs)|db\w+|jmp)(\.[bswl])?$')
TARGET_RE = re.compile(r'(?:^|,)([0-9a-f]+) <')
ZP_RE = re.compile(r'<mem(?:\+0x([0-9a-f]+))?>')
WRITE_BASES = {'move', 'clr', 'add', 'addq', 'addi', 'addx', 'sub', 'subq', 'subi', 'subx', 'and',
               'andi', 'or', 'ori', 'eor', 'eori', 'neg', 'negx', 'not', 'bset', 'bclr', 'bchg',
               'lsl', 'lsr', 'asl', 'asr', 'rol', 'ror', 'roxl', 'roxr', 'nbcd', 'abcd', 'sbcd',
               'tas', 'st', 'sf', 'shi', 'sls', 'scc', 'scs', 'sne', 'seq', 'svc', 'svs', 'spl',
               'smi', 'sge', 'slt', 'sgt', 'sle'}

def split_ops(ops):
    out, depth, cur = [], 0, ''
    for ch in ops:
        if ch == '(': depth += 1
        elif ch == ')': depth -= 1
        if ch == ',' and depth == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur: out.append(cur)
    return [o.strip() for o in out]

def zp_in_loops(insns):
    """The instructions that access mem[$00..$FF] inside a back-edge, as (ins, zp, is_store)."""
    out = []
    for i in insns:
        if not i.get('loop'): continue
        parts = split_ops(i['ops'])
        base = i['mn'].split('.')[0]
        for j, o in enumerate(parts):
            m = ZP_RE.search(o)
            if not m: continue
            z = int(m.group(1), 16) if m.group(1) else 0
            if z > 0xFF: continue
            last = j == len(parts) - 1
            store = last and (base in WRITE_BASES) and not (base == 'move' and len(parts) != 2)
            out.append((i, z, store))
    return out

# ────────────────────────────────────────────────────────────────────────────────────────────
# The oracle-branch shortlist: which exit registers does the 6502 read after a JSR to a shim?
# ────────────────────────────────────────────────────────────────────────────────────────────

def oracle_uses(shims, abi={}):
    """shim name -> [(caller, jsr addr, {reg: 'read'|'call'|'exit'})] over disasm/listing.txt.
    `abi` maps a NATIVE shim to (cpu registers its body reads before writing, registers it
    writes): a JSR to one of those is followed THROUGH, since its register reads are known; a JSR
    to anything else is a `call` (the 6502 callee may read any register as an argument)."""
    os.environ.setdefault('REVS_DASHCODE', '1')
    sys.path.insert(0, os.path.join(REPO, 'tools'))
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        import transpile as tp
        syms = tp.load_symbols(tp.SYM_CSV)
        funcs, _ = tp.parse_listing(tp.LISTING, syms)
    by_name = {v: k for k, v in syms.items()}
    want = {by_name[s]: s for s in shims if s in by_name}
    out = defaultdict(list)
    # Which kind of caller: an ORACLE runs only under `make validate`; a NATIVE_FUNCS driver's
    # transliteration runs nowhere (validate only prints it); anything else is a transliteration
    # the port could still execute (`make transtrap` says none does on its nine scenarios, but a
    # body no scenario drives is unproven, not dead).
    def caller_class(fn):
        # ⚠ NOT "moot": race_main_loop's transliteration never runs, but race_main_loop_core calls
        # the SAME shims in the SAME order and threads cpu between them, so a register the 6502
        # loop reads after a JSR is a LIVE hand-off in the port whenever the native loop enters
        # that routine through its shim (and dead when it calls the core and drops the exit).
        if fn['start'] in tp.NATIVE_FUNCS: return 'main loop'
        if fn['start'] in tp.VALIDATE_FUNCS: return 'oracle'
        return 'LIVE'
    for fn in funcs:
        insns = fn['insns']
        idx = {ins['addr']: i for i, ins in enumerate(insns)}
        for i, ins in enumerate(insns):
            if ins['mnem'] not in ('JSR', 'JMP'): continue
            mode, val, _ = tp.parse_operand(ins['op'], len(ins['bytes']), syms)
            if val not in want: continue
            regs = {}
            if ins['mnem'] == 'JMP':
                regs = {r: 'exit' for r in 'AXYNZCV'}
            else:
                seen = set()
                todo = [(i + 1, frozenset('AXYNZCV'))]
                while todo:
                    j, pend = todo.pop()
                    while pend and j < len(insns) and (j, pend) not in seen:
                        seen.add((j, pend))
                        x = insns[j]
                        mn = x['mnem']
                        if mn in ('RTS', 'RTI'):
                            for r in pend: regs.setdefault(r, 'exit')
                            pend = frozenset(); break
                        m2, v2, _ = tp.parse_operand(x['op'], len(x['bytes']), syms)
                        if mn == 'JSR':
                            callee = syms.get(v2)
                            if callee in abi:
                                rd, wr = abi[callee]
                                for r in rd & pend: regs[r] = 'read'
                                pend = pend - rd - wr
                                j += 1
                                continue
                            for r in pend: regs.setdefault(r, 'call')
                            pend = frozenset(); break
                        rd, wr = tp.insn_effects(mn, m2)
                        for r in rd & pend: regs[r] = 'read'
                        pend = pend - rd - wr
                        if mn == 'JMP':
                            if v2 in idx: j = idx[v2]; continue
                            for r in pend: regs.setdefault(r, 'exit')
                            pend = frozenset(); break
                        if mn in tp._BRANCH_READ and v2 in idx:
                            todo.append((idx[v2], pend))
                        j += 1
                    if pend and j >= len(insns):
                        for r in pend: regs.setdefault(r, 'exit')
            out[want[val]].append((fn['name'], ins['addr'], regs, caller_class(fn)))
    return out

# ────────────────────────────────────────────────────────────────────────────────────────────

def main():
    args = dict(a[2:].split('=', 1) if '=' in a else (a[2:], '1') for a in sys.argv[1:])
    objdir = args.get('objdir', 'build/fatscan')
    frames = int(args.get('frames', '200'))
    top = int(args.get('top', '40'))
    elf = args.get('elf', 'amiga/out/Revs.elf')
    dets = set(args.get('detector', 'exit,flag,zp,marshal').split(','))
    cflags = os.environ.get('FATSCAN_CFLAGS', '').split() or \
        ['-std=c11', '-fsigned-char', '-DREVS_HW_TRACE', '-Isrc', '-Isrc/cpu', '-Isrc/platform',
         '-Isrc/gen', '-DREVS_FATSCAN', '-DREVS_STRAIGHT_TO_RACE', '-DREVS_HOLD_THROTTLE']
    cflags = [c for c in cflags if c not in ('--coverage',) and not c.startswith('-M')]

    # ── the gates on the inputs ──
    elfp = os.path.join(REPO, elf)
    if not os.path.exists(elfp):
        sys.exit(f'fatscan: no {elf} — build the Amiga binary plain first')
    newest = max(os.path.getmtime(os.path.join(REPO, t)) for t in NATIVE_TUS)
    if os.path.getmtime(elfp) < newest:
        print(f'⚠ fatscan: {elf} is OLDER than the native sources — the instruction prices are '
              f'stale (cd amiga && make clean && make)')
    counts, entries = host_counts(os.path.join(REPO, objdir))
    insns = elf_insns(elfp)
    if subprocess.run(['m68k-amiga-elf-objdump', '-t', elfp], capture_output=True,
                      text=True).stdout.count('g_phaseTicks'):
        print(f'⚠ fatscan: {elf} is a PROBES build — its code shape is not the shipping one')
    price_insns(insns, counts, entries, frames)
    by_inner = defaultdict(list)                 # (file, line) -> instructions whose line it is
    by_site = defaultdict(list)                  # (file, line) -> instructions inlined from there
    by_fn = defaultdict(list)
    for i in insns:
        by_inner[i['inner']].append(i)
        for f, l, _ in i['chain']: by_site[(f, l)].append(i)
        by_fn[base_fn(i['fn'])].append(i)

    asts = [Ast(t, cflags) for t in NATIVE_TUS]
    model = Model(asts)

    def execs(file, line):
        return counts.get((file, line), 0) / frames

    def fexecs(nm):
        return entries.get(nm, 0) / frames

    def line_cost(file, line):
        """Target instructions per frame spent on this source line, over every copy of it."""
        return sum(i['x'] for i in by_inner.get((file, line), []))

    def call_cost(c, callee):
        """Target instructions per frame a CALL costs: what was inlined from the call site, or —
        when the callee stayed out of line — its body once per execution of the call."""
        where = (rel(c['_file']), c['_line'])
        # `inlined by S (G)` at chain level k: the function inlined AT S is the next frame
        # inward — ifn at k = 0, else the caller named at level k - 1.  A line holding several
        # calls has one site and several callees, so the callee must match at that level.
        def inlined_at(i):
            ch = i['chain']
            for k, (f, l, _) in enumerate(ch):
                if (f, l) == where:
                    return base_fn(i['ifn'] if k == 0 else ch[k - 1][2]) == callee
            return False
        inl = [i for i in by_site.get(where, []) if inlined_at(i)]
        if inl:
            return sum(i['x'] for i in inl)
        body = by_fn.get(callee)
        return execs(*where) * len(body) if body else 0.0

    if WINDOW_GUARD in model.fn and fexecs(WINDOW_GUARD) > 0:
        sys.exit(f'fatscan: {WINDOW_GUARD} ran inside the window — a crash/session reset is in '
                 f'it and would be ranked as per-frame work.  Move FATSCAN_FROM/TO.')

    rows = []          # (instr/frame, detector, routine, detail, where, execs/frame)
    shim_fields = defaultdict(set)

    # ── 1 + 2: exits and the flag replay into them ──
    status_of = {}
    for nm, which, tname in model.producers():
        st = field_status(model, (nm, which), tname)
        status_of[(nm, which)] = (tname, st)
        fat = {l for l, (s, _) in st.items() if s != 'live'}
        for l, (s, rs) in st.items():
            if s == 'shim':
                for _, _, (shim, reg) in rs: shim_fields[shim].add((nm, l, reg))
        if not fat or 'exit' not in dets: continue
        byline = defaultdict(lambda: [0, 0])
        for leaf, f, l in constructions(model, nm, (nm, which), tname):
            byline[(f, l)][1] += 1
            if leaf in fat: byline[(f, l)][0] += 1
        cost = sum(line_cost(f, l) * d / t for (f, l), (d, t) in byline.items() if t)
        dead = sorted(l for l in fat if st[l][0] == 'dead')
        shim = sorted(l for l in fat if st[l][0] == 'shim')
        detail = []
        if dead: detail.append('dead ' + ','.join(dead))
        if shim: detail.append('shim-only ' + ','.join(shim))
        tag = tname + ('' if which == 'ret' else f' via out-param {which[1]}')
        f = model.fn[nm]
        rows.append((cost, 'exit', nm, f'{tag}: ' + '; '.join(detail),
                     f"{rel(f['_file'])}:{f['_line']}", fexecs(nm)))

    if 'flag' in dets:
        for h in FLAG_HELPERS:
            for c in model.calls.get(h, []):
                g = c['_fn']
                sinks = model.flow(c, g, frozenset())
                if not sinks or any(k[0] != 'field' for k in sinks): continue
                fields = sorted({(k[1], k[2]) for k in sinks})
                if any(f[0] not in status_of or
                       status_of[f[0]][1].get(f[1], ('live',))[0] == 'live' for f in fields):
                    continue
                desc = ', '.join(f"{f[0][0]}.{f[1]} ({status_of[f[0]][1][f[1]][0]})" for f in fields)
                where = (rel(c['_file']), c['_line'])
                rows.append((call_cost(c, h), 'flag', g['name'],
                             f"{h} -> {desc}",
                             f'{where[0]}:{where[1]}', execs(*where)))

    if 'zp' in dets:
        syms = load_syms()
        agg = defaultdict(lambda: [0, {}, 0.0])
        for i, z, store in zp_in_loops(insns):
            if not store or i['x'] <= 0: continue
            a = agg[(base_fn(i['fn']), z)]
            a[0] += 1
            # one store per execution of a (line, call site): an unrolled copy is not counted twice
            k = (i['inner'], i['chain'][-1][:2] if i['chain'] else None)
            a[1][k] = max(a[1].get(k, 0.0), i['x'])
        for (fn, z), (n, per, _) in agg.items():
            ex = sum(per.values())
            (f, l), _site = max(per, key=lambda k: per[k])
            rows.append((ex, 'zp', fn, f"${z:02X} {syms.get(z, '?')} — {n} store instruction(s) in a loop",
                         f'{f}:{l}', max(per.values())))

    if 'marshal' in dets:
        names = {s for s in load_syms().values()}
        bycaller = defaultdict(list)
        for nm in model.fn:
            if not MARSHAL_RE.match(nm): continue
            for c in model.calls.get(nm, []):
                if c['_fn'] is None or MARSHAL_RE.match(c['_fn']['name']): continue
                bycaller[c['_fn']['name']].append((nm, c))
        for g, lst in bycaller.items():
            cost, n, bases = 0.0, 0.0, defaultdict(set)
            for nm, c in lst:
                e = execs(rel(c['_file']), c['_line'])
                if e <= 0: continue
                cc = call_cost(c, nm)
                if args.get('debug') == g:
                    print(f'  debug {g}: {nm} at {c["_line"]} execs {e:.1f} -> {cc:.1f}')
                cost += cc; n = max(n, e)
                b, d = MARSHAL_RE.match(nm).groups()
                bases[b].add(d)
            if n == 0: continue
            rt = [b for b, d in bases.items() if d == {'in', 'out'}]
            gf = model.fn[g]
            shim = g in names and ret_type(gf) == 'void' and \
                not [c for c in gf.get('inner', []) if c.get('kind') == 'ParmVarDecl']
            rows.append((cost, 'marshal', g,
                         ('ROUND TRIP ' + ','.join(sorted(rt)) + '; ' if rt else '') +
                         ' '.join(f'{b}:{"/".join(sorted(d))}' for b, d in sorted(bases.items())) +
                         (' — a 6502-ABI entry, running per frame' if shim else ''),
                         f"{rel(gf['_file'])}:{gf['_line']}", n))

    # ── the table ──
    rows.sort(key=lambda r: -r[0])
    total = defaultdict(float)
    for r in rows: total[r[1]] += r[0]
    print(f'fatscan: {frames} frames of STRAIGHT_TO_RACE + HOLD_THROTTLE (host --coverage), '
          f'priced on {elf} ({len(insns)} target instructions)')
    print('  ≈instr/f = target instructions executed per frame on the finding, each instruction '
          'credited with its own call site\'s share of the host count\n  (an estimate for RANKING, not a timing)\n')
    print(f"{'≈instr/f':>9s} {'det':7s} {'routine':34s} {'exec/f':>8s}  what / where")
    shown = [r for r in rows if r[0] > 0][:top]
    for cost, det, nm, detail, where, n in shown:
        print(f'{cost:9.0f} {det:7s} {nm[:34]:34s} {n:8.1f}  {detail}  [{where}]')
    zero = sum(1 for r in rows if r[0] <= 0)
    print(f"\n  {len(rows)} findings, {len(shown)} shown; {zero} price at 0 (not on the target, "
          f"never run in the window, or already deleted by GCC).  Totals: " +
          ', '.join(f'{d} {total[d]:.0f}' for d in ('exit', 'flag', 'zp', 'marshal') if d in dets))

    if 'fields' in args:
        want = args['fields']
        for (nm, which), (tname, st) in status_of.items():
            if nm != want: continue
            print(f'\n{nm} ({tname}{"" if which == "ret" else " out-param"}):')
            for l, (s, rs) in st.items():
                print(f'  {l:12s} {s:5s} ' + '; '.join(f'{k}:{d}' for _, k, d in rs[:6]))

    if 'no-shortlist' not in args and 'exit' in dets:
        live_shims = {s for s in shim_fields}
        uses = oracle_uses(live_shims, shim_abi(model))
        hooks = open(os.path.join(REPO, 'src/gen/revs_track_hooks.c')).read() \
            if os.path.exists(os.path.join(REPO, 'src/gen/revs_track_hooks.c')) else ''
        print('\n⚠ ORACLE-BRANCH SHORTLIST — shim-only exit fields, and what each 6502 caller does '
              'with the register next\n  (read = an instruction reads it before writing it; call = '
              'passed into another JSR; exit = carried out of the routine).  A HUMAN CONFIRMS each.\n'
              '  A 6502 caller is an oracle unless tagged: (main loop) = race_main_loop, whose native twin\n'
              '  replays its shim calls in order — LIVE if the port enters the routine through its shim '
              '(the "runs N/frame" ones), dead if it calls the core; (LIVE) = a transliteration the port could run.')
        routines = set(load_syms().values())
        for s in sorted(live_shims):
            regs = defaultdict(set)
            for core, leaf, reg in shim_fields[s]: regs[reg].add(f'{core}.{leaf}')
            sites = uses.get(s, [])
            hook = ' ⚠ ALSO CALLED BY A CIRCUIT HOOK' if re.search(r'\b' + s + r'\(\)', hooks) else ''
            native = native_cpu_reads(model, s)
            kind = '' if s in routines else ' — NOT a 6502 routine: only native code can read these'
            ran = f', runs {fexecs(s):.1f}/frame' if fexecs(s) else ''
            print(f'  {s}: {len(sites)} JSR/JMP site(s) in the listing, '
                  f'{len(model.calls.get(s, []))} native call site(s){ran}{kind}{hook}')
            for reg in sorted(regs):
                use = defaultdict(list)
                for caller, addr, rr, cls in sites:
                    k = rr.get(reg, 'dead')
                    use[k if k == 'dead' or cls == 'oracle' else f'{k}({cls})'].append(f'{caller}@${addr:04X}')
                for g in native.get(reg, []):
                    use['native read'].append(g)
                verdict = ', '.join(f'{k} {len(v)}' for k, v in sorted(use.items())) or 'NO READER'
                ex = '; '.join(v[0] for k, v in sorted(use.items()) if k != 'dead')
                print(f'    cpu.{reg:2s} <- {", ".join(sorted(regs[reg]))[:60]:60s} {verdict}'
                      + (f'  e.g. {ex}' if ex else ''))

CPU_REGS = {'A', 'X', 'Y', 'N', 'Z', 'C', 'V'}

def shim_abi(model):
    """name -> (cpu registers read before any write, cpu registers written) for every native
    `void f(void)`, by the textual order of the body — what a 6502 JSR to it consumes and clobbers.
    A shim that calls another native function is taken as reading all (its callee is not
    followed), so this errs towards `read`."""
    out = {}
    for nm, f in model.fn.items():
        if ret_type(f) != 'void' or [c for c in f.get('inner', []) if c.get('kind') == 'ParmVarDecl']:
            continue
        rd, wr = set(), set()
        for n in walk(f):
            if n.get('kind') != 'MemberExpr' or n.get('name') not in CPU_REGS: continue
            d = ref_decl(n['inner'][0]) if n.get('inner') else None
            if not d or d.get('name') != 'cpu': continue
            p, child = up(n)
            store = p is not None and p.get('kind') == 'BinaryOperator' and \
                p.get('opcode') == '=' and p['inner'][0] is child
            if store: wr.add(n['name'])
            elif n['name'] not in wr: rd.add(n['name'])
        out[nm] = (rd, wr)
    return out

def native_cpu_reads(model, shim):
    """reg -> [native callers of `shim` that READ cpu.<reg> on a later line of the same body].
    Line order, not control flow: a shortlist for a human, conservative in both directions."""
    out = defaultdict(list)
    for c in model.calls.get(shim, []):
        g = c['_fn']
        if g is None: continue
        regs = set()
        for n in walk(g):
            if n.get('kind') != 'MemberExpr' or n['_line'] <= c['_line']: continue
            d = ref_decl(n['inner'][0]) if n.get('inner') else None
            if not d or d.get('name') != 'cpu': continue
            p, child = up(n)
            if p is not None and p.get('kind') == 'BinaryOperator' and p.get('opcode') == '=' \
                    and p['inner'][0] is child:
                continue
            regs.add(n['name'])
        for r in regs: out[r].append(f"{g['name']}:{c['_line']}")
    return out

def model_keys(model, nm):
    f = model.fn[nm]
    parms = [c for c in f.get('inner', []) if c.get('kind') == 'ParmVarDecl']
    return [(nm, ('out', i)) for i in range(len(parms)) if model.out_param(f, i)]

def load_syms():
    out = {}
    for ln in open(os.path.join(REPO, 'disasm/symbols.csv')):
        if ln.startswith('#'): continue
        p = ln.split(',', 2)
        try: out.setdefault(int(p[0].strip().lstrip('$'), 16), p[1].strip())
        except (ValueError, IndexError): pass
    return out

if __name__ == '__main__':
    main()
