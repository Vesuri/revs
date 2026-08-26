#!/usr/bin/env python3
"""Cluster-9 seam relocation.  Splits src/gen/revs_native.c into:
  * revs_native.c        — the cpu-free typed cores + documented keeper-seams
  * revs_native_seam.c   — the thin 6502-ABI marshalling shims
  * revs_native_seam.h   — shared vocabulary: includes, address #defines the shims
                           use, the exit-struct typedefs, the always_inline flag
                           helpers, extern span descriptors, and prototypes for the
                           (now non-static) cores the shims call.
A pure code move — gated by full `make validate` + byte-identical determinism."""
import re, collections

SRC = 'src/gen/revs_native.c'
raw = open(SRC).read().split('\n')
N = len(raw)

# ---- structural view: blank comments / string+char literals (identifiers kept) ----
def build_code_view(lines):
    out = []; in_block = False
    for ln in lines:
        s = []; i = 0; L = len(ln)
        while i < L:
            c = ln[i]
            if in_block:
                if c == '*' and i+1 < L and ln[i+1] == '/':
                    in_block = False; s.append('  '); i += 2; continue
                s.append(' '); i += 1; continue
            if c == '/' and i+1 < L and ln[i+1] == '*':
                in_block = True; s.append('  '); i += 2; continue
            if c == '/' and i+1 < L and ln[i+1] == '/':
                s.append(' '*(L-i)); break
            if c == '"' or c == "'":
                q = c; s.append(' '); i += 1
                while i < L:
                    if ln[i] == '\\': s.append('  '); i += 2; continue
                    if ln[i] == q: s.append(' '); i += 1; break
                    s.append(' '); i += 1
                continue
            s.append(c); i += 1
        out.append(''.join(s))
    return out
code = build_code_view(raw)

# ---- preprocessor lines (incl. backslash continuations) ----
pp = set(); i = 0
while i < N:
    if raw[i].lstrip().startswith('#'):
        pp.add(i)
        while raw[i].rstrip().endswith('\\') and i+1 < N:
            i += 1; pp.add(i)
    i += 1
def brace_delta(k):
    return 0 if k in pp else code[k].count('{') - code[k].count('}')

# ---- enumerate top-level function definitions ----
def top_functions():
    out = []; i = 0
    while i < N:
        if i in pp: i += 1; continue
        ln = code[i]
        if re.match(r'^(static\s+)?[A-Za-z_][\w \t\*]*\**[A-Za-z_]\w*\s*\(', ln) \
           and not ln.lstrip().startswith(('typedef','extern','return','if','for','while','switch','else','do','case')) \
           and '=' not in ln.split('(')[0] and ';' not in ln.split('(')[0]:
            j = i; saw_semi = False
            while j < N:
                if '{' in code[j]: break
                if code[j].rstrip().endswith(';'): saw_semi = True; break
                j += 1
            if saw_semi or j >= N: i += 1; continue
            depth = 0; k = j
            while k < N:
                depth += brace_delta(k)
                if depth <= 0: break
                k += 1
            name = re.search(r'([A-Za-z_]\w*)\s*\(', ln).group(1)
            out.append((name, i, k)); i = k + 1; continue
        i += 1
    return out
funcs = top_functions()
static_fn = {n for n, s, e in funcs if code[s].startswith('static')}
inline_fn = {n for n, s, e in funcs
             if raw[s].startswith('REVS_FLAG_OP')
             or (s > 0 and raw[s-1].lstrip().startswith('static inline'))}

# ---- typedef blocks (-> header, removed from native) ----
typedef_blocks = []; i = 0
while i < N:
    if not (i in pp) and raw[i].startswith('typedef'):
        if code[i].rstrip().endswith(';'):
            typedef_blocks.append((i, i)); i += 1
        else:
            j = i
            while j < N and not re.match(r'^\}\s*\w*\s*;', code[j]): j += 1
            typedef_blocks.append((i, j)); i = j + 1
    else:
        i += 1

# ---- flag-helper cluster (REVS_FLAG_OP ...) + its macro line (-> header, removed) ----
flag_blocks = []
for n, s, e in funcs:
    if raw[s].startswith('REVS_FLAG_OP'):
        flag_blocks.append((s, e))
flag_macro_line = next(i for i in range(N) if raw[i].startswith('#define REVS_FLAG_OP'))

# ---- move-set: thin void-void shims (exclude span-family callers & KEEP list) ----
KEEP = {
 'scale_by_track_gradient','scale_wing_settings','compute_segment_scale',
 'place_player_in_section','tick_wheel_spin','spin_car_out','process_car_contact',
 'advance_dir_on_segment_flag','step_segment_dir_index','build_section_step_delta',
 'build_road_section','cross_section_boundary','load_section_from_segment',
 'step_section_curve','place_car_world_coords','tally_bcd_column','full_track_scan_rebuild',
 'scale16_by_y','mul16_by_1_5','abs16_math','neg16_math','neg16_math_noinit',
 'abs8','mul8_accum','mul8_noinit','mul8','div16by8',
}
CALL_RE = re.compile(r'\b[a-z_]\w*(?:_core|_apply|_exit_abi)\s*\(|\bspan_plot_core\s*\(|\barg_[axy]\s*\(')
SPAN_FAMILY = re.compile(r'\b(span_plot_core|span_walk)\s*\(')
move = []
for n, s, e in funcs:
    if code[s].strip() != 'void %s(void)' % n: continue
    if n in KEEP: continue
    body = '\n'.join(code[s:e+1])
    if (e - s + 1) <= 24 and CALL_RE.search(body) and not SPAN_FAMILY.search(body):
        move.append((n, s, e))
move_names = {n for n, s, e in move}

# ---- direct static callees of moved shims -> un-static + prototype (skip inline & span vars) ----
idpat = re.compile(r'\b([A-Za-z_]\w*)\b')
callees = set()
for n, s, e in move:
    callees |= (set(idpat.findall('\n'.join(code[s:e+1]))) & static_fn)
callees -= move_names
callees -= inline_fn                      # flag helpers live in the header, not un-static'd

# ---- object-like address #defines the shims reference -> header (benign redef) ----
define_val = {}; define_order = []
for i in range(N):
    if i not in pp: continue
    m = re.match(r'^#define\s+([A-Za-z_]\w*)\s+(\S.*?)\s*$', raw[i])
    if m and not re.match(r'^#define\s+[A-Za-z_]\w*\(', raw[i]):
        nm = m.group(1)
        if nm in ('REVS_FLAG_OP', 'REVS_MEM_ALIASES'): continue
        if nm not in define_val:
            define_val[nm] = raw[i]; define_order.append(nm)
shim_ids = set()
for n, s, e in move:
    shim_ids |= set(idpat.findall('\n'.join(code[s:e+1])))
needed_defines = [nm for nm in define_order if nm in shim_ids]

# ---- signature reconstruction (comment-free) ----
def signature_of(name):
    for n, s, e in funcs:
        if n != name: continue
        parts = []; k = s
        while True:
            cb = code[k].find('{')
            if cb >= 0: parts.append(code[k][:cb]); break
            parts.append(code[k]); k += 1
        text = ' '.join(p.strip() for p in parts)
        text = re.sub(r'\s+', ' ', text)
        text = re.sub(r'^static\s+', '', text).strip()
        return text + ';'
    return None

# ==== emit header ====
H = ['/* revs_native_seam.h — cluster-9 seam.  Shared vocabulary between revs_native.c',
     ' * (the cpu-free typed cores) and revs_native_seam.c (the thin 6502-ABI shims).',
     ' * Generated once by tools/split_seam.py; hand-maintained thereafter. */',
     '#ifndef REVS_NATIVE_SEAM_H', '#define REVS_NATIVE_SEAM_H',
     '#include <stdint.h>',
     '#ifndef REVS_MEM_ALIASES', '#define REVS_MEM_ALIASES', '#endif',
     '#include "../cpu/cpu.h"', '#include "../cpu/bus.h"', '#include "../cpu/m68k_math.h"',
     '#include "revs_decl.h"', '#include "mem.h"',
     '#include "../platform/platform_c.h"', '#include "../platform/bbc_screen.h"',
     '#include "../platform/probe.h"', '#include "../platform/shape.h"',
     '#include "../platform/revs_plot.h"', '',
     '/* ---- address constants the shims use (copied from revs_native.c; identical) ---- */']
for nm in needed_defines: H.append(define_val[nm])
H += ['', '/* ---- exit-struct typedefs (moved out of revs_native.c) ---- */']
for s, e in typedef_blocks: H += raw[s:e+1]
H += ['', '/* ---- always_inline 6502 flag helpers (moved out of revs_native.c) ---- */',
      raw[flag_macro_line]]
for s, e in flag_blocks: H += raw[s:e+1] + ['']
H += ['/* ---- span-plotter descriptors (defined in revs_native.c) ---- */',
      'extern const SpanPlotter SPAN_PLOT_1;', 'extern const SpanPlotter SPAN_PLOT_2;', '',
      '/* ---- cpu-free cores the shims call (defined in revs_native.c) ---- */']
for c in sorted(callees):
    p = signature_of(c)
    if p: H.append(p)
H += ['', '#endif /* REVS_NATIVE_SEAM_H */', '']
open('src/gen/revs_native_seam.h', 'w').write('\n'.join(H))

# ==== emit seam.c ====
S = ['/* revs_native_seam.c — the thin 6502-ABI marshalling shims for the cpu-free cores',
     ' * in revs_native.c.  Cluster-9 seam relocation (tools/split_seam.py).  Each shim reads',
     ' * cpu/mem[], calls the typed core, and marshals the result + exit ABI back. */',
     '#include "revs_native_seam.h"', '']
removed = set([flag_macro_line])
for s, e in typedef_blocks:
    removed |= set(range(s, e+1))
for s, e in flag_blocks:
    removed |= set(range(s, e+1))
for n, s, e in move:
    S += raw[s:e+1] + ['']
    removed |= set(range(s, e+1))
open('src/gen/revs_native_seam.c', 'w').write('\n'.join(S))

# ==== rewrite revs_native.c ====
out = []; inserted = False
for idx, ln in enumerate(raw):
    if idx in removed: continue
    m = re.match(r'^static\s+(.*)', ln)
    if m:
        nm = re.search(r'([A-Za-z_]\w*)\s*\(', ln)
        if nm and nm.group(1) in callees:
            ln = m.group(1)
        elif re.match(r'^static\s+const\s+SpanPlotter\s+SPAN_PLOT_[12]\b', ln):
            ln = m.group(1)
    out.append(ln)
    if not inserted and ln.strip() == '#include "mem.h"':
        out.append('#include "revs_native_seam.h"'); inserted = True
open(SRC, 'w').write('\n'.join(out))

print("funcs parsed:      ", len(funcs))
print("moved shims:       ", len(move))
print("un-static'd callees:", len(callees))
print("typedef blocks:    ", len(typedef_blocks))
print("flag helpers moved:", len(flag_blocks))
print("address #defines:  ", len(needed_defines), needed_defines)
print("moved:", sorted(move_names))
