#!/usr/bin/env python3
"""Which 6502-ABI shims must marshal a relocated global?  Answered from the COMPILED OBJECTS.

    python3 tools/native_closure.py view_origin_16
    python3 tools/native_closure.py model_state_16 --objs src/gen/revs_native.o

A wide-value relocation (docs/wide-value-cleanup.md, mechanism (B)) moves a lo/hi pair out of
mem[] into a real uintNN_t global.  The boundary rule is that mem[] and the global agree at every
6502-ABI shim, so every shim whose CALL CLOSURE touches the global needs a marshal-in, and every
shim whose closure WRITES it needs a marshal-out as well.

⚠⚠ WHY THIS READS OBJECT FILES AND NOT THE SOURCE.  Two campaigns in a row, a closure derived by
reading C missed shims, and each miss is a silent wrong answer that only the poisoned differential
catches:

  - MODEL_STATE: four shims that never mention the vector needed marshals because a `_core` two or
    three levels down nudged it (update_grip_limits, update_camera_and_height, begin_jump,
    begin_scrape — all via begin_jump_from_a_core).
  - VIEW_ORIGIN: a regex call-graph over the source matched function names inside COMMENTS, which
    invented edges, and its hand-checked shim list then omitted three real ones
    (build_track_geometry, road_edge_start, road_edge_walk).

The relocations are the compiler's own edges, so `objdump -dr` cannot invent an edge that is not
a call and cannot miss one that is.  ⚠ It needs the objects to be CURRENT — build first.
"""
import argparse, collections, re, subprocess, sys

def graph(objs):
    edges = collections.defaultdict(set)
    for o in objs:
        d = subprocess.run(['objdump', '-dr', o], capture_output=True, text=True)
        if d.returncode:
            sys.exit('objdump failed on %s — build first?' % o)
        cur = None
        for line in d.stdout.split('\n'):
            m = re.match(r'^[0-9a-f]+ <_?([A-Za-z_][A-Za-z0-9_]*)>:', line)
            if m:
                cur = m.group(1); continue
            if cur is None:
                continue
            # every relocation in this function's body: calls AND data references
            for m in re.finditer(r'(?:ARM64_RELOC|R_X86_64|R_AARCH64)_\w+\s+_?([A-Za-z_][A-Za-z0-9_]*)',
                                 line):
                edges[cur].add(m.group(1))
    return edges

def reach(edges, n, stop=()):
    """Everything reachable from n, NOT descending through any name in `stop`.

    ⭐ `stop` is the set of OTHER 6502-ABI shims, and cutting there is the whole point: a shim
    that reaches the global only by calling another shim is already bracketed, because that inner
    shim marshals in and out for itself.  Without the cut the tool reports the outer shim as a
    missing marshal — which is how it accused process_car_contact and race_main_loop of a gap in
    committed code.  Both call begin_scrape(), the SHIM (process_car_contact tail-calls it at
    $1C18), and begin_scrape already marshals model_state.
    """
    seen, stack = {n}, [n]
    while stack:
        for t in edges.get(stack.pop(), ()):
            if t not in seen:
                seen.add(t)
                if t not in stop:
                    stack.append(t)
    return seen

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('symbol', help='the relocated global, e.g. view_origin_16')
    ap.add_argument('--objs', nargs='*',
                    default=['src/gen/revs_native.o', 'src/gen/revs_native_seam.o'])
    ap.add_argument('--list', default='src/gen/revs_validate_list.h',
                    help='the generated VALIDATE_FUNCS/NATIVE_FUNCS name list')
    ap.add_argument('--writers', nargs='*', default=[],
                    help='cores known to WRITE the global; without these every hit is reported '
                         'as a read and you must decide IN vs IN+OUT yourself')
    a = ap.parse_args()

    edges = graph(a.objs)
    touch = sorted(n for n in edges if a.symbol in edges[n]
                   and not n.startswith(a.symbol.rsplit('_', 1)[0]))
    if not touch:
        sys.exit('no function references %s — wrong symbol, or the objects are stale' % a.symbol)
    print('functions referencing %s:' % a.symbol)
    for n in touch:
        print('   ', n)
    print()

    names = set(re.findall(r'"([a-z_0-9]+)"', open(a.list, encoding='utf-8').read()))
    writers = set(a.writers)
    shims = names & set(edges)
    print('shims whose closure touches it (a shim reaching a WRITER needs marshal_out too):')
    covered = []
    for n in sorted(shims):
        cut  = reach(edges, n, stop=shims - {n})
        hits = sorted(set(touch) & cut - {n})
        if not hits:
            # nothing on the uncut path — but does it get there THROUGH another shim?
            full = reach(edges, n)
            if set(touch) & full - {n}:
                covered.append((n, sorted((cut & shims) - {n})))
            continue
        kind = 'IN+OUT' if (writers & set(hits)) else ('IN    ' if writers else '?     ')
        print('  %-34s %s  via %s' % (n, kind, ','.join(hits)))
    if covered:
        print()
        print('reached ONLY through an inner shim — already bracketed, no marshal needed:')
        for n, inner in covered:
            print('  %-34s via shim %s' % (n, ','.join(inner)))

if __name__ == '__main__':
    main()
