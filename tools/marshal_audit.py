#!/usr/bin/env python3
"""Does every 6502-ABI shim that needs a relocated global's marshal actually have it?

    python3 tools/marshal_audit.py                      # every relocated base
    python3 tools/marshal_audit.py model_state          # just one

⭐ THE BOUNDARY RULE this checks (docs/wide-value-cleanup.md, mechanism (B)): mem[] and the
relocated global must agree at every 6502-ABI shim, so a shim whose call closure READS the global
needs a marshal-in and one whose closure WRITES it needs a marshal-out too.

Two things make the answer non-obvious, and both have already produced a wrong hand-derived list:

  - the closure is TRANSITIVE and runs through `_core` functions that never appear in the shim's
    own body (tools/native_closure.py's docstring has the two cases), so it is taken from
    `objdump -dr` on the built objects, not from reading the C; and
  - a shim that reaches the global ONLY by calling another SHIM is already bracketed — that inner
    shim marshals for itself.  Cutting the closure at shim boundaries is what clears
    process_car_contact, whose tail call to begin_scrape() does the kicking.

The marshal calls themselves are read TEXTUALLY, because within a translation unit the compiler
may inline one and then no relocation records it.
"""
import re, subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import native_closure as nc

SRC  = ['src/gen/revs_native.c', 'src/gen/revs_native_seam.c']
OBJS = ['src/gen/revs_native.o', 'src/gen/revs_native_seam.o']

# base name -> (global symbol, cores known to WRITE it)
BASES = {
    'edge_nearest':  ('edge_nearest_v',   []),
    'car_heading':   ('car_heading_v',    []),
    'car_angle':     ('car_angle_16',     []),
    'model_state':   ('model_state_16',   ['ms_set_hi', 'ms_set_lo']),
    'view_origin':   ('view_origin_16',   []),
    'car_distance':  ('car_distance_16',  []),
    'hypot_max':     ('hypot_max_v',      []),
    'hypot_min':     ('hypot_min_v',      []),
    'bearing':       ('bearing_v',        []),
}

def shim_bodies():
    """name -> body text, for every `void NAME(void) { ... }` in the native sources."""
    out = {}
    for f in SRC:
        s = open(f, encoding='utf-8').read()
        for m in re.finditer(r'^void ([a-z_0-9]+)\(void\)[^{;]*\{', s, re.M | re.S):
            i = s.index('{', m.end() - 1); d = 0
            for j in range(i, len(s)):
                if s[j] == '{': d += 1
                elif s[j] == '}':
                    d -= 1
                    if d == 0: break
            out[m.group(1)] = s[i:j]
    return out

def main():
    want = sys.argv[1:] or list(BASES)
    edges = nc.graph(OBJS)
    names = set(re.findall(r'"([a-z_0-9]+)"',
                           open('src/gen/revs_validate_list.h', encoding='utf-8').read()))
    shims = names & set(edges)
    bodies = shim_bodies()
    bad = 0
    for base in want:
        sym, writers = BASES[base]
        touch = {n for n in edges if sym in edges[n] and not n.startswith(base)}
        if not touch:
            print('%-14s no references — stale objects?' % base); bad += 1; continue
        rows, info = [], []
        for n in sorted(shims):
            hits = set(touch) & nc.reach(edges, n, stop=shims - {n}) - {n}
            if not hits:
                continue
            need_out = bool(set(writers) & hits) if writers else None
            body = bodies.get(n, '')
            has_in  = (base + '_marshal_in')  in body
            has_out = (base + '_marshal_out') in body
            if not has_in and not has_out:
                rows.append(('NO MARSHAL ', n, ','.join(sorted(hits))))
            elif not has_in:
                # A PRODUCER: it writes the value and publishes it, and every base that shows this
                # pattern is a SCALAR, so there is no partially-written array to import first.
                info.append(('produces   ', n, ','.join(sorted(hits))))
            elif need_out and not has_out:
                rows.append(('MISSING out', n, ','.join(sorted(hits))))
        print('%-14s %d shims in closure, %d problem(s)'
              % (base, sum(1 for n in shims
                           if set(touch) & nc.reach(edges, n, stop=shims - {n}) - {n}), len(rows)))
        for kind, n, via in rows + info:
            print('    %s  %-34s via %s' % (kind, n, via))
        bad += len(rows)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
