#!/usr/bin/env python3
"""make cpu-lint — src/gen/revs_native.c may speak `cpu` only in the argued classes.

⭐⭐ WHY.  The campaign's goal is the 6502 register file GONE from the native surface.  What is
left in revs_native.c is not residue: each site is one of the six classes below, argued at the
code.  A NEW `cpu.` reference in any other function is the thing this lint exists to stop --
either it belongs in a typed core's parameter list, or its shim belongs in revs_native_abi.c.

To add a function here you must also write the argument at the code.  Deleting a row is always
allowed; adding one is a decision.
"""
import re, sys

ALLOWED = {
  # -- class 1: the HOOK / SMC SEAM.  The 6502 hands registers to a patched arm we do not own, so
  #    the register file is genuinely live there (docs/faithfulness-seam.md).

  # -- class 2: the ISR SEAM, and it is the one place where the ambient register file is the
  #    SUBJECT of the code: the "caller" is whatever foreground the interrupt preempted, so
  #    there is nothing to thread an argument from.  The MOS's own IRQ entry is the contract and
  #    g_irqClobberCount asserts it on both backends (docs/native-sweep.md track 4).
  'irq1v_band_schedule': 'ISR seam', 'irq1v_return': 'ISR seam', 'irq1v_chain_on': 'ISR seam',

  # -- class 3: the STACK POINTER.  `cpu.S` here is an ADDRESS, not a value in a register --
  #    the routine is talking about a byte at $0100+S.  C has no equivalent to drop it into.
  #    ⚠⚠ "THE DIFFERENTIAL COMPARES THE PUSHED BYTE" IS NO LONGER A REASON TO BE IN THIS CLASS.
  #    Two rows used to sit here on exactly that ground (place_player_in_section's two PHAs
  #    parking V1/V2, update_lap_timers' PHP whose SIGN is the one bit read back) and both are
  #    gone under THE RESULTS RULE: residue below SP that the routine's own PULL pops is an
  #    implementation detail, `tools/det_compare.py` already exempts $01B8..$01FF, and a scoped
  #    `set_ignore` covers the fixture.  What earns a row here is `cpu.S` used as a VALUE the
  #    routine computes with or hands on -- not a push the oracle happens to make.
  #    ⭐ Down to two: `mul16_by_1_5_core`'s $476C PHA byte and `hook_steer_response_doning`'s
  #    $5791 PHA byte were both residue writes under the same rule, and both went.  What is left
  #    is `cpu.S` read as a VALUE: engine_init_core hands it to `top_level_stack` (abort's unwind
  #    target) and span_abandon_chain passes `cpu.S + 2u` as an argument.
  'engine_init_core': 'cpu.S is an address', 'span_abandon_chain': 'cpu.S is an address',

  # -- class 4: a decimal-mode clear inside a core.  `cpu.D = 0` is the 6502's own CLD and the
  #    oracle pushes/compares P; nothing in any core READS D (docs/static-map.md §Decimal mode).
  'sort_cars_by_key_core': 'CLD', 'add_tally_to_lap_total_core': 'CLD',
  'lap_complete_core': 'CLD',

  # -- class 5: a documented forward of a caller's live flag, argued at the code.
  'race_main_loop_core': 'the closing CLI', 'finish_race_core': 'documented forward',

  # -- class 6: a 6502-ABI SHIM THAT A NATIVE CALLER STILL USES, so it cannot move to
  #    revs_native_abi.c.  ⚠ A shim with NO native caller belongs in that file -- that is the
  #    whole point of it.
  #    ⚠⚠ THE CALLER AUDIT IS A TRANSITIVE CLOSURE, not a grep.  Six rows here used to say
  #    "shim: native callers" about callers that were themselves oracle-only shims in
  #    revs_native_abi.c (mul8 -> mul8_noinit, scale16_by_y -> abs16_math, and the three math
  #    helpers the twin comments called universal), so the whole cluster was oracle-only and
  #    moved out.  Ask it as: is this reachable from anything BUT an oracle-only shim?
  'state_flags_bit6': 'shim: read from revs_native_seam.c',
  'surface_colour_apply': 'shim: revs_native_seam.c',
  'abs8': 'HookRegs shim: scale_by_track_gradient_tail in revs_native_seam.c',
  'store_slip_exit_abi': 'exit ABI: revs_native_seam.c x3',
  'sound_queue_exit_abi': 'exit ABI: revs_native_seam.c x5',
}

def strip_comments(lines):
    out, inc = [], False
    for l in lines:
        o, i = '', 0
        while i < len(l):
            if inc:
                j = l.find('*/', i)
                if j < 0: i = len(l); break
                inc = False; i = j + 2
            else:
                j, k = l.find('/*', i), l.find('//', i)
                if j >= 0 and (k < 0 or j < k): o += l[i:j]; inc = True; i = j + 2
                elif k >= 0: o += l[i:k]; i = len(l)
                else: o += l[i:]; i = len(l)
        out.append(o)
    return out

# ⚠⚠ `hook_cpu_to_regs` / `hook_regs_to_cpu` ARE cpu references -- they copy the whole register
# file in and out through a HookRegs local, and two oracle-only shims (mul8_noinit,
# scale_by_track_gradient) hid from this lint for exactly that reason.
SPEAKS = re.compile(r'\bcpu\.|\bPUSH\s*\(|\bPULL\s*\(|\bPHP\s*\(|\bPLP\s*\(|\bPHA\s*\(|\bPLA\s*\('
                    r'|\bhook_cpu_to_regs\s*\(|\bhook_regs_to_cpu\s*\(')
path = 'src/gen/revs_native.c'
lines = strip_comments(open(path).read().split('\n'))
fn, cand, depth, bad, used = None, None, 0, [], set()
for n, l in enumerate(lines):
    if depth == 0 and not l.startswith('#'):
        # ⚠ allow leading blanks: a `/* promoted ... */ SlotExit foo(void)` line strips down to
        # one with an indent, and anchoring at column 0 attributed its body to the function ABOVE.
        m = re.match(r'\s*[A-Za-z_].*?\b(\w+)\s*\(', l)
        if m: cand = m.group(1)
    o, c = l.count('{'), l.count('}')
    # ⚠ the enclosing name must be resolved BEFORE the check, or a ONE-LINE shim
    # (`void mul8(void) { math_lo = cpu.A; ... }`) is attributed to nothing and skipped.
    if depth == 0 and o: fn = cand
    if (depth > 0 or o) and SPEAKS.search(l):
        if fn in ALLOWED: used.add(fn)
        else: bad.append((n + 1, fn, l.strip()))
    depth += o - c
# ⚠ A STALE ALLOWLIST ENTRY IS A SILENT HOLE -- it would excuse a future function that happens to
# reuse the name.  Every row must still name a function that speaks `cpu` in this file, so the
# list shrinks by itself as the campaign closes and cannot drift into a permission slip.
stale = sorted(set(ALLOWED) - set(used))
if stale:
    print('cpu-lint: %d allowlist rows in tools/cpu_lint.py no longer speak `cpu` in %s:'
          % (len(stale), path))
    for f in stale: print('  %s  (%s)  -> delete the row' % (f, ALLOWED[f]))
    sys.exit(1)

if bad:
    print('cpu-lint: %s speaks `cpu` outside the argued classes (tools/cpu_lint.py):' % path)
    for n, f, l in bad: print('  %s:%d  in %s():  %s' % (path, n, f, l))
    print('  -> pass it as a typed argument, or move the shim to src/gen/revs_native_abi.c.')
    sys.exit(1)
print('cpu-lint: clean (%d functions in the argued classes)' % len(ALLOWED))
