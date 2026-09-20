#!/usr/bin/env python3
"""make cpu-lint — src/gen/revs_native.c may speak `cpu` only in the argued classes.

⭐⭐ WHY.  The campaign's goal is the 6502 register file GONE from the native surface.  What is
left in revs_native.c is not residue: each site is one of the classes below, argued at the code.
⭐ THREE OF THE SIX ARE NOW EMPTY (the ISR seam, the flag forwards and the class-6 shims), because "it is not
oracle-only, so it cannot go to revs_native_abi.c" was never an argument for it living in a file
whose job is CORES.  Both emptied into revs_native_seam.c, beside the callers.  A NEW `cpu.` reference in any other function is the thing this lint exists to stop --
either it belongs in a typed core's parameter list, or its shim belongs in revs_native_abi.c.

To add a function here you must also write the argument at the code.  Deleting a row is always
allowed; adding one is a decision.
"""
import re, sys

ALLOWED = {
  # -- class 1: the HOOK / SMC SEAM.  The 6502 hands registers to a patched arm we do not own, so
  #    the register file is genuinely live there (docs/faithfulness-seam.md).

  # -- class 2: the ISR SEAM.  ⭐ EMPTY, and that is the point: the IRQ1V handler's register work
  #    (the chain-on to the previous owner, the X save and the PLA/TAX/LDA $FC/RTI exit contract)
  #    moved to `irq1v_band_schedule` in revs_native_seam.c and what is left here is
  #    `irq1v_band_schedule_core`, the raster-band state machine, which touches no register at
  #    all.  The ambient register file IS the subject of that seam code -- the "caller" is
  #    whatever foreground the interrupt preempted, so there is nothing to thread an argument
  #    from -- but that is an argument for where it lives, not for it living in a core file.
  #    g_irqClobberCount asserts the contract on both backends (docs/native-sweep.md track 4).

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

  # -- class 4: a decimal-mode clear inside a core.  `cpu.D = 0` is the 6502's own CLD.
  #    ⚠⚠ THE OLD ARGUMENT HERE WAS WRONG AND IS THE REASON THESE THREE STAY.  It read
  #    "nothing in any core READS D", which is false: `adc_step` (revs_native_seam.h) puts its
  #    add through cpu.h's ADC macro, and that macro consults cpu.D to decide the RESULT BYTE.
  #    Its two live call sites are road_edge_side_core's $2551 `CLC / ADC #$78` and the plot-
  #    pointer marshal at revs_native_seam.c:365 -- both on the RENDER path, whose correctness
  #    rests on D being 0 there (docs/static-map.md §Decimal mode).  D is written to 1 only by
  #    the 8 `SED()` sites in src/gen/revs_gen.c, which `make transtrap` shows no scenario
  #    enters -- but "a body no scenario DRIVES is unproven, not dead", so the CLD is the
  #    mechanism that restores binary mode the moment one ever does run, exactly as on the 6502.
  #    It is three bool stores in cold routines; deleting them buys nothing and removes the
  #    invariant's only enforcement.  add_tally_to_lap_total's fixture ASSERTS D is clear at exit
  #    (dLeftSet), which is the right instrument for a CLD and is why it is not a free-floating
  #    claim.  ⭐ The sibling case: the ISR shim's own CLD in revs_native_seam.c is undetectable
  #    by its fixture for the same reason and is kept on the same argument, written at the code.
  'sort_cars_by_key_core': 'CLD', 'add_tally_to_lap_total_core': 'CLD',
  'lap_complete_core': 'CLD',

  # -- class 5: a documented forward of a caller's live flag.  ⭐ NOW EMPTY TOO, and the whole
  #    class dissolved for one reason: every destination of a "forwarded" flag turned out to be
  #    a balanced PHP/PLP residue below SP.
  #    ⭐ finish_race_core used to forward four ambient flag bits into
  #    tick_race_timers_core, and its own comment said why they could not be proved: the run-out
  #    loop re-enters from two branch-backs with different carries.  But their only destination
  #    was seed_car_track_position's $6362 PHP residue at $0100+S -- stack residue below SP, an
  #    implementation detail and not a result -- so the whole chain went, taking four parameters
  #    off tick_race_timers_core and the WingScaleExit hand-off out of race_main_loop_core's
  #    phase 1 with it.  ⚠ The lesson generalises: a flag that is genuinely UNPROVABLE is a hint
  #    to ask what READS it, because an unprovable value that nothing reads is not a forward.
  #    ⭐ race_main_loop_core's $4F35 CLI went the same way: `cpu.I` is pure bookkeeping on this
  #    port (nothing gates interrupt delivery on it), so once draw_dash_needles_native's $5145
  #    residue -- a PER-FRAME write -- was retired, the bit had no reader anywhere.

  # -- class 6: a 6502-ABI SHIM THAT A NATIVE CALLER STILL USES.  ⭐ ALSO EMPTY NOW, and the
  #    reason is worth keeping: such a shim cannot go to revs_native_abi.c, which is by
  #    definition the TU nothing in the port calls -- but "not revs_native_abi.c" was never an
  #    argument for revs_native.c, which is for CORES.  The answer is the third file:
  #    state_flags_bit6, surface_colour_apply (now the cpu-free surface_colour_at_line_core
  #    here, with the exit ABI replayed by its shim), abs8's `cpu` entry, store_slip_exit_abi
  #    and sound_queue_exit_abi all live in revs_native_seam.c beside their callers.
  #    ⚠⚠ THE CALLER AUDIT IS A TRANSITIVE CLOSURE, not a grep.  Six rows here used to say
  #    "shim: native callers" about callers that were themselves oracle-only shims in
  #    revs_native_abi.c (mul8 -> mul8_noinit, scale16_by_y -> abs16_math, and the three math
  #    helpers the twin comments called universal), so the whole cluster was oracle-only and
  #    moved out.  Ask it as: is this reachable from anything BUT an oracle-only shim?
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
# Declaration qualifiers and statement keywords that can precede the real name on a definition
# line.  ⚠ Anything added here weakens the attribution, so keep it to things that can NEVER be a
# function being defined.
QUALIFIERS = {'__attribute__', '__asm__', 'if', 'while', 'for', 'switch', 'return', 'sizeof',
              'static', 'inline', 'extern', 'const', 'unsigned', 'signed', 'struct', 'union'}

SPEAKS = re.compile(r'\bcpu\.|\bPUSH\s*\(|\bPULL\s*\(|\bPHP\s*\(|\bPLP\s*\(|\bPHA\s*\(|\bPLA\s*\('
                    r'|\bhook_cpu_to_regs\s*\(|\bhook_regs_to_cpu\s*\(')
path = 'src/gen/revs_native.c'
raw_lines = open(path).read().split('\n')
lines = strip_comments(raw_lines)
# ⚠⚠⚠ THE PREPROCESSOR HAS TO BE BALANCED OR THE WHOLE SCAN IS WRONG FROM ONE LINE ONWARD.
# `#ifdef X / if (a) { / #else / if (b) { / #endif` is ordinary C and BOTH arms open a brace, so
# a naive count sees two `{` and one `}`.  Measured 2026-09-20 at revs_native.c:1585: the depth
# desynchronised there and never returned to 0, so every `cpu` site in the remaining ~17 000
# lines was attributed to one function (`view_scan_lanes`), `used` came out EMPTY, every
# allowlist row read as STALE, and the run exited on that before the real check below ever ran.
# A gate that is red and blind at the same time.  Take the FIRST arm as authoritative: the arms
# are alternatives, so any one of them balances the construct.
# ⚠ ...AND A `#define` CONTINUES OVER `\`-TERMINATED LINES.  `#define M(x) do { ... \` skips the
# directive line and then counted the continuation's `}`, losing a brace per multi-line macro —
# which is what took the depth NEGATIVE by line 50, before the #if/#else problem even arrived.
pp = []                     # (depth at the #if, depth at the end of the first arm)
in_pp = False
fn, cand, depth, bad, used = None, None, 0, [], set()
for n, l in enumerate(lines):
    st = l.strip()
    if in_pp or st.startswith('#'):
        # ⚠ The continuation test reads the RAW line: a macro whose continuation carries a
        # /* ... */ that spans lines strips to NOTHING, and testing the stripped line then drops
        # out of the macro mid-way and counts the rest of its braces as code.
        in_pp = raw_lines[n].rstrip().endswith('\\')
        if st.startswith('#if'):
            pp.append((depth, None))
        elif st.startswith(('#else', '#elif')) and pp:
            d0, dend = pp[-1]
            pp[-1] = (d0, depth if dend is None else dend)
            depth = d0
        elif st.startswith('#endif') and pp:
            d0, dend = pp.pop()
            if dend is not None: depth = dend
        continue
    if depth == 0:
        # ⚠ allow leading blanks: a `/* promoted ... */ SlotExit foo(void)` line strips down to
        # one with an indent, and anchoring at column 0 attributed its body to the function ABOVE.
        # ⚠⚠ SKIP THE DECLARATION QUALIFIERS, OR EVERY SITE IS ATTRIBUTED TO `__attribute__`.
        # The first `word(` on `static void __attribute__((noinline)) foo(void)` is the
        # attribute, not the function — so `used` came out EMPTY, every allowlist row read as
        # stale, and the run exited on that before the real "speaks cpu outside the argued
        # classes" check below ever ran.  A gate that is red AND blind (measured 2026-09-20:
        # all five sites, including `add_tally_to_lap_total_core`'s $66B4 CLD, attributed to
        # `__attribute__`).  Take the first candidate that is not a qualifier or a keyword.
        for _m in re.finditer(r'\b(\w+)\s*\(', l):
            if _m.group(1) not in QUALIFIERS: cand = _m.group(1); break
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
