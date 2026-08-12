#!/usr/bin/env python3
"""Transpile the Ghidra 6502 disassembly listing to C.  BBC Micro Revs.

Reads  disasm/listing.txt        (⚠ the RUNTIME image — see below)
       disasm/symbols.csv        the source of truth for names
Writes src/gen/revs_gen.c            one C function per 6502 routine
       src/gen/revs_decl.h           forward declarations
       src/gen/mem.h                 MEM_<name> offsets + opt-in lvalue aliases
       src/gen/revs_validate_list.h  VALIDATE_FUNCS names, for fixture-or-fail

⚠⚠ `listing.txt` must be a disassembly of `disasm/revs_runtime.bin` (`make runtime`),
NOT `revs_mem.bin`.  REVS2 unpacks itself before running, so an address in the loaded
image means something else entirely in the running engine (docs/static-map.md).

Design
------
* Each 6502 routine becomes a void C function.
* JSR  → direct function call.
* JSR/JMP into the MOS entry block ($FF00-$FFFF) → platform_mos_call(entry).
* RTS  → return;
* RTI  → PLP(); return;
* Branches (BEQ etc.) → if (flag) goto L_xxxx;
* JMP  within same function → goto L_xxxx;
* JMP  to a different function → callee(); return;   (tail call)
* JMP ($xxxx) → platform_indirect_jmp() (Revs has exactly one: the IRQ1V chain-on).
* Stack, flags, registers modelled via cpu.h macros.
* BBC I/O ($FC00-$FEFF) → bus_read/bus_write; OS vector page writes also go through
  bus_write so the platform sees the game claim IRQ1V.
* All other addresses → mem[] direct.
* ZP-indexed wraps using (uint8_t) cast.
* **Self-modifying instructions are emitted as runtime-dispatched forms** rather than
  hand-stubbed — see SMC_SITES.
"""
import re
import sys
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).parent.parent
LISTING  = ROOT / "disasm/listing.txt"
SYM_CSV  = ROOT / "disasm/symbols.csv"
OUT_C    = ROOT / "src/gen/revs_gen.c"
OUT_H    = ROOT / "src/gen/revs_decl.h"
OUT_MAN  = ROOT / "src/gen/revs_manual.c"
OUT_MEM  = ROOT / "src/gen/mem.h"
OUT_VAL  = ROOT / "src/gen/revs_validate_list.h"

# ---------------------------------------------------------------------------
# Self-modifying code
# ---------------------------------------------------------------------------
# ⭐ Revs does NOT get hand-written stubs for its self-modifying routines.
#
# The Atari port's rule was "a routine that writes its own instruction stream cannot be
# transliterated faithfully → hand-stub it in *_manual.c".  That rule costs a hand-written,
# unvalidated, non-regenerable routine per site, and Revs has 24 sites — all of them inside
# the road rasteriser, i.e. the hottest and least-understood code in the binary.
#
# Every one of those 24 sites is one of three MECHANICAL classes, and each class has an
# exactly faithful runtime-dispatched C form.  So the transpiler emits the patchability
# instead of freezing one snapshot of it:
#
#   'operand'  the instruction's operand BYTES are rewritten, the opcode is not.  Emit an
#              instruction whose effective address / immediate is read from mem[] at run
#              time.  Byte-wise reads, so it is endian-safe on the 68000 by construction.
#   'opcode'   a one-byte opcode SLOT is switched between a small, statically known set of
#              values.  Emit a switch on mem[site] with one case per value; an unlisted
#              value is a hard trap, never a silent fall-through.
#   'branch'   a branch OFFSET is rewritten, so the target varies.  Emit the target
#              computed at run time, dispatched over the instruction starts of the
#              enclosing function.  Anything else traps.
#
# Result: `make gen` stays the single source of the corpus, the rasteriser keeps working
# for whatever the writers actually poke (including the per-track $2F23 hook, which only
# two circuits install), and there is nothing hand-written to drift.
#
# Evidence for every row: docs/static-map.md §Self-modifying code (24 sites, from
# `make sweep`), plus the writer instructions read out of the listing — the 'values' below
# are the LDA immediates / sources those writers actually store, not assumptions.
#
# Keyed by the address of the INSTRUCTION whose bytes get patched.
SMC_SITES = {
    # --- 'operand': patched operand bytes, opcode untouched ------------------
    # $196F  STA $0400,Y — LOW operand byte at $1970 rewritten from $193E.
    #        One copy of the loop serving several screen-row destinations.
    0x196F: {'kind': 'operand', 'bytes': {0x1970}, 'from': ['$193E']},
    # $1DDB  LDA #$55 — the IMMEDIATE at $1DDC rewritten from $1DAC.
    0x1DDB: {'kind': 'operand', 'bytes': {0x1DDC}, 'from': ['$1DAC']},
    # $1DDD  STA ($70),Y — the ZERO-PAGE POINTER NUMBER at $1DDE rewritten from $1DA6
    #        (STX), so the store walks a different pointer pair per call.
    0x1DDD: {'kind': 'operand', 'bytes': {0x1DDE}, 'from': ['$1DA6']},
    # $2F4E  STA $7000,Y — BOTH operand bytes rewritten: lo $2F4F from $19C9, hi $2F50
    #        from $19C0, sourced from the table pair $2B22 (lo) / $2B1E (hi).  That table
    #        holds the four per-column edge buffers $0554/$05A4/$0600/$0650, which is how
    #        one span plotter serves four row buffers (docs/static-map.md).
    0x2F4E: {'kind': 'operand', 'bytes': {0x2F4F, 0x2F50}, 'from': ['$19C9', '$19C0']},
    # $2F90  STA $7000,Y — the second plotter, same mechanism ($2F91 from $19CC,
    #        $2F92 from $19C3).
    0x2F90: {'kind': 'operand', 'bytes': {0x2F91, 0x2F92}, 'from': ['$19CC', '$19C3']},

    # --- 'opcode': a 1-byte opcode slot switched between known values --------
    # NOP/INY/DEY slots: the span loop is specialised per frame to step Y forward,
    # backward, or not at all.  ⭐ THREE values, not two — and the third was found by
    # RUNNING the corpus in Phase 4, not by reading it.  g_smcUnhandled reported
    # `site $2F89 holds $0088` on the first frame that reached the rasteriser.
    #
    # The two-value reading came from attributing each store to the nearest preceding
    # `LDA #imm`, and $2C01 has TWO of them:
    #     $2BF9  BPL $2BFF        ; sign of $87 selects the direction
    #     $2BFB  LDA #$88         ; <-- DEY   (the arm the scan walked past)
    #     $2BFD  BNE $2C01
    #     $2BFF  LDA #$C8         ;     INY
    #     $2C01  STA $2F60 / STA $2FA2
    # so $2F60 and $2FA2 take $88 whenever $87 is negative, and $2F47/$2F89 inherit it
    # through `LDA $2F60` at $2CC5, and $2F18 through `LDA $2F47`.  A span plotter that
    # can walk its destination in either direction is exactly what a 3D road rasteriser
    # needs, so this is semantics, not an oddity.
    #
    #   $2F60/$2FA2 written from LDA #$C8 ($2BFF), LDA #$88 ($2BFB) or LDA #$EA ($2CCE)
    #   $2F47/$2F89 written from LDA #$EA ($2C07) or from LDA $2F60 ($2CC5) — i.e. whatever
    #               $2F60 currently holds, which is one of the same three values
    #   $2F18       written from LDA $2F47 ($2F12) — same closure again
    0x2F18: {'kind': 'opcode', 'values': {0xEA: 'NOP', 0xC8: 'INY', 0x88: 'DEY'}, 'from': ['$2F15']},
    0x2F47: {'kind': 'opcode', 'values': {0xEA: 'NOP', 0xC8: 'INY', 0x88: 'DEY'}, 'from': ['$2C09', '$2CC8']},
    0x2F60: {'kind': 'opcode', 'values': {0xEA: 'NOP', 0xC8: 'INY', 0x88: 'DEY'}, 'from': ['$2C01', '$2CD0', '$2BFB']},
    0x2F89: {'kind': 'opcode', 'values': {0xEA: 'NOP', 0xC8: 'INY', 0x88: 'DEY'}, 'from': ['$2C0C', '$2CCB']},
    0x2FA2: {'kind': 'opcode', 'values': {0xEA: 'NOP', 0xC8: 'INY', 0x88: 'DEY'}, 'from': ['$2C04', '$2CD3', '$2BFB']},
    # CPX-or-RTS slots: written from LDA #$E0 ($2CB4, `CPX #$80`) or LDA #$60 ($2CAA, `RTS`).
    # ⚠ The two forms have different LENGTHS (2 vs 1).  That is representable only because
    # $60 TERMINATES: when the slot holds RTS the operand byte is never executed, so no
    # following instruction shifts.  Both $2FC0 and $2FD7 are FUNCTION STARTS, so the whole
    # routine is switched between "compare and continue" and "return immediately".
    0x2FC0: {'kind': 'opcode', 'values': {0xE0: 'CPX', 0x60: 'RTS'}, 'from': ['$2CAF', '$2CB9']},
    0x2FD7: {'kind': 'opcode', 'values': {0xE0: 'CPX', 0x60: 'RTS'}, 'from': ['$2CAC', '$2CB6']},

    # --- 'branch': patched branch offset, so the target varies ---------------
    # $1DD4  BNE — offset at $1DD5 rewritten from $1DA9 (STY), so the target is a
    #        register value, not a constant: it cannot be enumerated statically.
    0x1DD4: {'kind': 'branch', 'bytes': {0x1DD5}, 'from': ['$1DA9']},
    # The four rasteriser BCCs: offset rewritten from a per-column table
    #   $2D28 from $2D1A (LDA $3E50,X)    $2DAB from $2D9D (LDA $40D0,X)
    #   $2E2F from $2E23 (LDA $3ED0,X)    $2EA8 from $2E9C (LDA $3ED8,X)
    # In the static image every one of these reads `BCC +0` (branch to the next
    # instruction — a no-op), which is exactly what an unpatched slot should look like and
    # is why a straight transliteration of this region would run but do nothing.
    0x2D27: {'kind': 'branch', 'bytes': {0x2D28}, 'from': ['$2D1A']},
    0x2DAA: {'kind': 'branch', 'bytes': {0x2DAB}, 'from': ['$2D9D']},
    0x2E2E: {'kind': 'branch', 'bytes': {0x2E2F}, 'from': ['$2E23']},
    0x2EA7: {'kind': 'branch', 'bytes': {0x2EA8}, 'from': ['$2E9C']},
}

# Functions that STILL need a hand-written stub in src/gen/revs_manual.c.
# Empty on purpose: SMC_SITES above covers all 24 self-modifying sites generically.
# Add an address here only when a routine genuinely cannot be expressed as a
# transliteration at all (docs/faithfulness-seam.md), and say why.
MANUAL_FUNCS = set()

# Functions being reimplemented natively, validated against the transliteration.
# For each address here the transpiler still emits the faithful transliterated
# body, but DEFINES it under a `<name>__t6502` suffix instead of the plain name.
# The plain `<name>()` — the one all call sites invoke — is provided by a
# hand-written native version in src/gen/revs_native.c.  Both coexist so the
# validation harness (tools/validate_native.c) can run them on the same input
# state and diff the full machine state.  This is the regen-safe strangler-fig
# seam: drop an address in, write the native twin, prove equivalence, ship it;
# everything not listed here stays transliterated and fully regenerable.
#
# ⚠ Adding an address here is only HALF the job — register a fixture in
# tools/validate_native.c too.  The names are emitted to src/gen/revs_validate_list.h and
# the harness FAILS on a listed name with no fixture, because a fixture-less PASS runs
# zero comparisons (docs/validation-harness.md).
#
# EMPTY until Phase 6 (docs/phases.md).  Phase 4 profiles the transliterated corpus on the
# real A500 FIRST and lets the measurement pick the twins — the Atari port chose them by
# reasoning and got the choice wrong (docs/perf-method.md).
VALIDATE_FUNCS = set()
VALIDATE_SUFFIX = '__t6502'

# ---------------------------------------------------------------------------
# Address ranges
# ---------------------------------------------------------------------------
# BBC memory-mapped I/O: FRED $FC00 / JIM $FD00 / SHEILA $FE00 — one contiguous
# window, matching bus.h's BBC_IO_LO/HI.  Reads and writes both route through the
# platform.  (Revs never touches FRED or JIM, and never addresses the uPD7002 ADC
# directly — the steering arrives via OSBYTE 128.  docs/static-map.md.)
HW_BASE, HW_END = 0xFC00, 0xFF00

# The MOS entry block.  A JSR/JMP in here is an OS call, not a target in the image:
# it must become platform_mos_call(), or the generated C would call into a function
# that does not exist.  Revs uses four entries at 17 sites — OSBYTE $FFF4, OSWORD $FFF1,
# OSWRCH $FFEE, OSRDCH $FFE0 — plus `JMP ($FFFC)` (reset) in the loader stub, which is
# not part of the running engine.  docs/static-map.md §MOS calls.
MOS_BASE, MOS_END = 0xFF00, 0x10000

# Sideways / language / MOS ROM.  Nothing in the engine should call into here; a call
# that does is reported at gen time rather than silently emitted as a missing function.
ROM_BASE = 0x8000

# ---------------------------------------------------------------------------
# Spin-wait hook injection.
# ---------------------------------------------------------------------------
# When a backward branch loops back to one of these addresses, inject the listed
# platform call(s) so the display/VBI can advance.  Without them a tight C spin-wait
# starves the platform and the loop never exits.
#
# ⚠ ONLY for waits that own a whole frame boundary.  A raster-position wait is reset by
# platform_tick_vbi() and could never exit (docs/transpiler.md).  Revs makes that trap
# more likely than the Atari port did: its 50 Hz body is a USER VIA T1 *raster* timer that
# reloads the palette mid-frame, so several of its waits are intra-frame by design.
#
# ⭐ FILLED IN PHASE 4 FROM WHAT ACTUALLY STALLED, not from the candidate list.  `make gen`
# REPORTS 41 candidate spin loops (report_spin_candidates); running the corpus showed that
# exactly ONE of them is a wait on state the port has to supply, and that the other obvious
# one is a HARDWARE wait serviced at a different seam entirely:
#
#   $4E11  hw_init      BIT $FE4D / BEQ — waits for the System VIA vsync flag.  NOT hooked:
#                       it is a wait on an I/O register, so Platform::hwRead answers it
#                       (src/platform/bbc_hw.cpp).  Hooking it here would have worked too
#                       and been wrong — the exit condition belongs to the hardware model,
#                       and a hook would have hidden that $FE4D was unmodelled.
#   $1760  FUN_16dc     LDA $62F7 / BMI — ⭐ THE MAIN LOOP'S FRAME BOUNDARY.  $175D stores
#                       $9C into $62F7 and the 50 Hz interrupt body counts it down, so
#                       under a single-threaded C port nothing ever clears it.  This is the
#                       one true frame-wait in the engine.
#
# The other 39 are ordinary counted loops (DEX/BPL over a table) that the candidate
# heuristic cannot tell apart from a wait, plus intra-frame palette writes inside
# irq1v_handler itself — where a platform_tick_vbi() would be actively harmful.
#
# ⚠ platform_tick_vbi(), NOT platform_render_frame().  On the Amiga the 50 Hz body runs in
# the real VERTB ISR and preempts this loop, so tickVBI is a no-op there and the wait ends
# on its own; on the headless host, which has no preemption, tickVBI is what advances the
# interrupt.  Painting is hooked at the TOP of the loop instead (PRE_INSN_HOOKS below), so
# one painted frame means one game frame whether or not this wait was entered.
SPINWAIT_HOOKS = {
    0x1760: 'PROBE_PHASE(0); platform_tick_vbi(); platform_poll_events();',
}

# ---------------------------------------------------------------------------
# Pre-instruction hook injection.
# ---------------------------------------------------------------------------
# Unlike SPINWAIT_HOOKS (emitted at a branch-target LABEL), these inject a C statement
# immediately BEFORE the instruction at the given address, with no label.  Used for
# faithful hardware seams that land mid-instruction-stream where no branch label exists
# (so a forced label would be unreferenced and trip -Wunused-label).
# Key: 6502 address of the instruction to inject before.  Value: C statement(s).
#
# ⭐ ONE ENTRY, added in Phase 4: the top of the engine's main loop.
#
# $1701 is the first instruction of the ~25-call body at $1701-$1763 that runs the physics
# and the 3D pipeline once per game frame, and it is a plain JSR chain with no branch label
# of its own — which is exactly the case PRE_INSN_HOOKS exists for.
#
# Painting is hooked HERE rather than at the frame-wait ($1760, see SPINWAIT_HOOKS) because
# the wait is CONDITIONAL: $1753 skips it entirely when $62F6 is zero.  A paint hooked to
# the wait would therefore stop counting frames whenever the game took that branch, and the
# framerate would read as a drop in rendering rather than as a change of path — the
# "unattended run ending" trap in docs/perf-method.md §Rule 3, dressed up as a measurement.
# One hook at the top means exactly one painted frame per game frame, always.
PRE_INSN_HOOKS = {
    0x1701: 'platform_render_frame();',
}

# ---------------------------------------------------------------------------
# Phase brackets over the main loop (PROBES builds only).
# ---------------------------------------------------------------------------
# ⭐ Phase 4's "name the hot functions".  The engine's per-frame body is a FLAT sequence of
# JSRs between these two addresses, which makes exact bracketing possible where PC sampling
# would have to fight -O2 inlining for attribution (on the host, -O2 collapsed the entire
# loop into one frame of FUN_16dc).  Generated rather than hand-written so it survives
# `make gen`, and so a phase can never drift away from the call it is supposed to time.
#
# Every JSR whose address is in [lo, hi] gets `PROBE_PHASE(n)` emitted before it, numbered
# in source order; the frame wait re-opens phase 0.  Compiles to nothing without PROBES.
MAIN_LOOP_BRACKET = (0x1701, 0x1748)

# ---------------------------------------------------------------------------
# Parse symbols.csv → addr_int → name
# ---------------------------------------------------------------------------
# addr_int → note (5th CSV column); populated by load_symbols, consumed when
# emitting each function so the symbol's note becomes a C doc-comment.
SYMBOL_NOTES = {}

# addr_int → snake_case name, restricted to non-hardware *var* rows of
# symbols.csv.  These are the named RAM/state addresses; the emitter rewrites a
# direct single-byte access mem[0xADDR] → mem[MEM_<name>] (defined in the
# generated mem.h) so the transliterated C reads as named state rather than
# raw hex.  Indexed / indirect / 16-bit-pointer accesses keep raw hex.
VAR_NAMES = {}

def sanitize_note(note):
    """Make a symbols.csv note safe to paste into a C comment.

    Strips the CSV quoting and neutralises any `*/`, which would otherwise CLOSE the
    generated doc-comment early and turn the rest of the note into stray C tokens — a
    build break caused by a documentation edit, in a file nobody hand-edits.  Cheap to
    prevent, confusing to diagnose."""
    note = note.strip()
    if len(note) >= 2 and note[0] == '"' and note[-1] == '"':
        note = note[1:-1]
    return note.replace('*/', '* /').strip()

def load_symbols(path):
    sym = {}
    SYMBOL_NOTES.clear()
    VAR_NAMES.clear()
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#'): continue
        parts = line.split(',', 4)
        if len(parts) < 2: continue
        addr_s = parts[0].strip().lstrip('$')
        name   = parts[1].strip()
        try: addr_i = int(addr_s, 16)
        except ValueError: continue
        sym[addr_i] = name
        if len(parts) >= 5 and parts[4].strip():
            SYMBOL_NOTES[addr_i] = sanitize_note(parts[4])
        # Collect named RAM state for mem[MEM_*] substitution + AtariMem.h.
        if len(parts) >= 4 and parts[2].strip() == 'var' and parts[3].strip() == '0':
            VAR_NAMES[addr_i] = name
    return sym

def mem_alias(addr):
    """Bare lvalue alias (e.g. `level_stage`) for a named non-hardware address,
    else None.  Used for DIRECT single-byte accesses, which read cleanest as
    `level_stage = cpu.A` (the alias expands to mem[MEM_level_stage] via the
    REVS_MEM_ALIASES block of mem.h)."""
    return VAR_NAMES.get(addr)

def mem_index(addr):
    """Standalone address expression: MEM_<name> when the address has a
    symbols.csv var row, otherwise the raw 0xADDR literal.  Used where an
    *address* (not an lvalue) is needed — INC_M/DEC_M targets, bus_write()."""
    name = VAR_NAMES.get(addr)
    return f'MEM_{name}' if name else f'0x{addr:04X}'

def mem_base(addr):
    """Base expression for an *indexed* access (additive context, e.g.
    `MEM_foo+cpu.X`): MEM_<name> when named, else the hex literal parenthesised
    to dodge the C preprocessor's E+/P+ pp-number tokenisation quirk."""
    name = VAR_NAMES.get(addr)
    return f'MEM_{name}' if name else f'(0x{addr:04X})'

def write_mem_header(path):
    """Generate src/gen/mem.h from VAR_NAMES (symbols.csv var rows).

    Emits MEM_<name> = 0xADDR offset macros (usable in C and C++), plus an
    OPT-IN block of bare `<name> -> mem[MEM_<name>]` lvalue aliases gated on
    REVS_MEM_ALIASES (C files only; they would textually clobber any local of the
    same name, so each consumer opts in deliberately)."""
    items = sorted(VAR_NAMES.items())  # by address
    width = max((len(n) for n in VAR_NAMES.values()), default=1)
    lines = [
        '#pragma once',
        '',
        '// AUTO-GENERATED by tools/transpile.py from disasm/symbols.csv (var rows).',
        '// Do NOT edit by hand — regenerate with `make gen`.',
        '//',
        '// Named offsets into the shared mem[65536] snapshot of the BBC Micro 6502',
        '// address space.  symbols.csv is the source of truth for the names.',
        '//',
        '//   MEM_<name>   numeric offset (C and C++).  Use as mem[MEM_level_stage].',
        '//   <name>       OPT-IN bare lvalue alias for mem[MEM_<name>]; enable with',
        '//                `#define REVS_MEM_ALIASES` before including (C only — a macro',
        '//                of a plain name would clobber same-named locals / C++ members).',
        '//',
        '// ⚠ ADDRESSES ARE IN THE RUNTIME IMAGE (disasm/revs_runtime.bin).  REVS2 unpacks',
        '// itself before running, so the same address in revs_mem.bin holds something else',
        '// entirely (docs/static-map.md).',
        '//',
        '// These are general RAM addresses, not just zero page (e.g. $05F4 the front-end',
        '// state byte, $39E0 menu_key_tbl) — named after the mem[] snapshot they index.',
        '',
    ]
    for addr, name in items:
        note = SYMBOL_NOTES.get(addr, '')
        if len(note) > 64:
            note = note[:61] + '...'
        comment = f'  // ${addr:04X}{(" " + note) if note else ""}'
        lines.append(f'#define MEM_{name:<{width}} 0x{addr:04X}{comment}')
    lines += [
        '',
        '#ifdef REVS_MEM_ALIASES',
        '// Bare lvalue aliases: write `level_stage` for `mem[MEM_level_stage]`.',
    ]
    for addr, name in items:
        lines.append(f'#define {name:<{width}} mem[MEM_{name}]')
    lines += ['#endif /* REVS_MEM_ALIASES */', '']
    path.write_text('\n'.join(lines))
    print(f'Wrote {path}  ({len(items)} named addresses)')

# ---------------------------------------------------------------------------
# func_lo: lowest 6502 address that belongs to a function's body.
# Normally the function start, but functions that absorbed an orphan
# prefix run (see attach_orphan_runs) begin lower than their named entry.
# Use this for range/containment tests so branches into the prefix resolve
# as local labels, not stub calls.
# ---------------------------------------------------------------------------
def func_lo(f):
    return f.get('body_start', f['start'])

# ---------------------------------------------------------------------------
# attach_orphan_runs: fix Ghidra function-boundary mis-detection.
#
# Ghidra occasionally clips a routine's entry to a later instruction (e.g. a
# loop *test*), leaving the loop *body* that precedes it stranded in the gap
# between two functions.  Those orphan instructions belong to no function, so
# a backward branch into them would otherwise resolve to an empty FUN_xxxx
# stub + return (silently dropping the loop).  The live instance is FUN_9d6f:
# the 8-byte normalization loop at $9D67-$9D6E was orphaned before the $9D6F
# entry, breaking the terrain projection divide.
#
# Fix: find each maximal run of contiguous orphan instructions that ABUTS a known
# function's start, and prepend that run to the function as a body prefix.  The
# function's named entry stays at its original start (callers still enter there);
# translate_func emits a `goto L_<entry>` so a normal call skips the prefix, while an
# internal branch into the prefix becomes a local `goto`.
#
# ⚠ A run is attached when EITHER it falls through into the function OR the function
# branches back into it.  Requiring fall-through — as this originally did — silently
# DROPPED three runs from Revs, and one of them was `JMP ($4F1D)` at $4E59: the IRQ1V
# chain-on that hands an interrupt that is not Revs's own back to the handler it
# displaced.  It is a 3-byte orphan run ending in a terminator, sitting immediately
# before irq1v_handler ($4E5C) and reached by `BEQ $4E59` at $4E61 from inside it.
# Dropping it produced C where a foreign interrupt fell off the end of the handler
# instead of chaining — a plausible-looking corpus with the interrupt structure quietly
# wrong, which is postmortem finding #1.1 all over again.  The other two are the same
# shape: $262D-$2636 (BMI from $263A in FUN_2637) and $4978-$49CB (BEQ from $49D0 in
# FUN_49ce).  Ending in a terminator is not evidence of not belonging: a prefix loop
# body can perfectly well RTS or JMP out.
# ---------------------------------------------------------------------------
def attach_orphan_runs(all_insns, funcs, func_ranges):
    insns_sorted = sorted(all_insns, key=lambda i: i['addr'])

    def contained(addr):
        for s, e, n in func_ranges:
            if s <= addr <= e:
                return True
        return False

    funcs_by_start = {f['start']: f for f in funcs}
    orphans = [i for i in insns_sorted if not contained(i['addr'])]
    if not orphans:
        return

    # Group into maximal contiguous runs (each insn abuts the next).
    runs = []
    cur = [orphans[0]]
    for prev, nxt in zip(orphans, orphans[1:]):
        if prev['addr'] + len(prev['bytes']) == nxt['addr']:
            cur.append(nxt)
        else:
            runs.append(cur); cur = [nxt]
    runs.append(cur)

    TERMINATORS = {'RTS', 'RTI', 'JMP', 'BRK'}

    def branches_into(f, lo, hi):
        """Does f's own body branch/JMP into [lo,hi]?  That makes the run part of f
        however it ends — the branch is the evidence of ownership."""
        for ins in f['insns']:
            if ins['mnem'] not in BRANCH_FLAGS and ins['mnem'] != 'JMP':
                continue
            m = re.match(r'^0x([0-9a-fA-F]+)$', (ins['op'] or '').strip())
            if m and lo <= int(m.group(1), 16) <= hi:
                return True
        return False

    for run in runs:
        last  = run[-1]
        after = last['addr'] + len(last['bytes'])
        lo, hi = run[0]['addr'], last['addr']
        tgt = funcs_by_start.get(after)
        if tgt is None:
            print(f'[orphan] run ${lo:04X}-${hi:04X} continues to ${after:04X} '
                  f'(not a function start) — left as-is')
            continue
        falls_through = last['mnem'] not in TERMINATORS
        targeted      = branches_into(tgt, lo, hi)
        if not falls_through and not targeted:
            # Abuts a function but neither flows into it nor is branched to from it:
            # unreferenced, so attaching it would invent a caller.  Report it — an
            # orphan run nothing reaches is either dead code or a missed entry point,
            # and both are worth a line of output (docs/entrypoint-sweep.md).
            print(f'[orphan] run ${lo:04X}-${hi:04X} ends in {last["mnem"]} and '
                  f'{tgt["name"]} never branches into it — left as-is (UNREACHABLE?)')
            continue
        tgt['insns']      = run + tgt['insns']
        tgt['body_start'] = lo
        tgt['skip_to']    = tgt['start']   # named entry; prefix runs above it
        why = 'falls through' if falls_through else f'branch target from {tgt["name"]}'
        print(f'[orphan] attached run ${lo:04X}-${hi:04X} as prefix of '
              f'{tgt["name"]} (entry ${tgt["start"]:04X}) — {why}')

# ---------------------------------------------------------------------------
# Parse listing into a list of functions, each with instructions.
# Returns:
#   funcs: list of {start, end, name, insns: [{addr, bytes, mnem, op, raw}]}
#   func_by_addr: addr → function index
# ---------------------------------------------------------------------------
def parse_listing(path, symbols):
    funcs = []
    func_ranges = []   # (start, end) from function-summary header

    # Pass 1: collect function ranges from the header summary.
    with open(path) as f:
        for line in f:
            m = re.match(r'^; FUNC (\S+)\s+([0-9a-f]+) - ([0-9a-f]+)', line)
            if m:
                name  = m.group(1)
                start = int(m.group(2), 16)
                end   = int(m.group(3), 16)
                func_ranges.append((start, end, name))

    # Build addr → (start, end, name) lookup.
    func_ranges.sort()
    func_start_set = {r[0] for r in func_ranges}
    func_info = {r[0]: r for r in func_ranges}

    # Pass 2: parse instructions, grouped into functions by address range.
    insn_re = re.compile(
        r'^([0-9a-f]{4})\s+((?:[0-9A-F]{2} )*[0-9A-F]{2})\s+'
        r'([A-Z]{2,3})\s*(.*?)\s*$'
    )

    current_func = None
    func_insns   = {}   # start_addr → list of insn dicts
    all_insns    = []   # every decoded instruction, regardless of function

    with open(path) as f:
        for line in f:
            m = insn_re.match(line)
            if not m: continue
            addr  = int(m.group(1), 16)
            bs    = bytes(int(x, 16) for x in m.group(2).split())
            mnem  = m.group(3)
            op    = m.group(4).strip()
            ins   = {'addr': addr, 'bytes': bs, 'mnem': mnem, 'op': op}
            all_insns.append(ins)

            # Determine enclosing function by address range.
            fn_start = None
            for (s, e, n) in func_ranges:
                if s <= addr <= e:
                    fn_start = s
                    break
            if fn_start is None:
                continue

            if fn_start not in func_insns:
                func_insns[fn_start] = []
            func_insns[fn_start].append(ins)

    # Assemble final list in address order.
    dropped_rom = []
    for (start, end, name) in func_ranges:
        insns = func_insns.get(start, [])
        if not insns: continue
        # Ghidra creates a "function" at each MOS entry it sees JSR'd ($FFE0/$FFEE/$FFF1/
        # $FFF4) and decodes the $00 our image holds there as BRK.  Those are ROM entries,
        # not routines: every call to them is emitted as platform_mos_call(), so a
        # definition here would be dead code that merely looks like an implementation of
        # the OS.  Drop them, and say so.
        if start >= ROM_BASE:
            dropped_rom.append((start, name))
            continue
        # Override name from symbols if present.
        final_name = symbols.get(start, name)
        funcs.append({'start': start, 'end': end,
                      'name': final_name, 'insns': insns})

    if dropped_rom:
        print('[rom] dropped ' + str(len(dropped_rom)) + ' Ghidra function(s) in ROM space '
              '(MOS entries, intercepted as calls): '
              + ', '.join(f'${a:04X}' for a, _ in dropped_rom))

    # Attach orphan instruction runs (code Ghidra left in inter-function gaps)
    # to the function they fall through into.  See attach_orphan_runs.
    attach_orphan_runs(all_insns, funcs, func_ranges)

    # func_by_addr: addr → function dict
    func_by_addr = {}
    for f in funcs:
        for ins in f['insns']:
            func_by_addr[ins['addr']] = f

    return funcs, func_by_addr

# ---------------------------------------------------------------------------
# Operand → C expression
# ---------------------------------------------------------------------------
def is_hw(addr):
    return HW_BASE <= addr < HW_END

# The MOS entries Revs actually calls, for the comment on each emitted seam.  Naming them
# in the generated C is the difference between "some OS call" and "this is the steering
# input" when someone reads revs_gen.c a phase later.  docs/static-map.md.
MOS_ENTRY_NAMES = {
    0xFFE0: 'OSRDCH', 0xFFE3: 'OSASCI', 0xFFE7: 'OSNEWL', 0xFFEE: 'OSWRCH',
    0xFFF1: 'OSWORD', 0xFFF4: 'OSBYTE', 0xFFF7: 'OSCLI',
    0xFFCE: 'OSFIND', 0xFFD1: 'OSGBPB', 0xFFD4: 'OSBPUT', 0xFFD7: 'OSBGET',
    0xFFDA: 'OSARGS', 0xFFDD: 'OSFILE', 0xFFE9: 'OSWRCR',
}

def is_mos(addr):
    """A JSR/JMP into the MOS entry block is an OS call, not a target in the image."""
    return MOS_BASE <= addr < MOS_END

def mos_name(addr):
    return MOS_ENTRY_NAMES.get(addr, f'MOS ${addr:04X} — UNIDENTIFIED entry')

def addr_read(addr):
    if is_hw(addr):
        return f'bus_read(0x{addr:04X})'
    alias = mem_alias(addr)
    return alias if alias else f'mem[0x{addr:04X}]'

def addr_write(addr, val):
    if is_hw(addr):
        return f'bus_write(0x{addr:04X}, {val})'
    return f'mem[0x{addr:04X}] = {val}'

def parse_operand(op, nbytes, symbols):
    """Return (mode, addr_or_imm, index) where mode is one of:
       imm, zp, abs, zpx, zpy, absx, absy, indy, indx, acc, impl
    """
    op = op.strip()
    if not op:
        return ('impl', 0, None)

    # Immediate: #0x12
    m = re.match(r'^#0x([0-9a-fA-F]+)$', op)
    if m:
        return ('imm', int(m.group(1), 16), None)

    # (zp),Y  post-indexed
    m = re.match(r'^\(0x([0-9a-fA-F]+)\),Y$', op)
    if m:
        return ('indy', int(m.group(1), 16), 'Y')

    # (zp,X)  pre-indexed
    m = re.match(r'^\(0x([0-9a-fA-F]+),X\)$', op)
    if m:
        return ('indx', int(m.group(1), 16), 'X')

    # (abs) or (zp)  — absolute/ZP indirect, used by JMP (DLI chain pattern)
    m = re.match(r'^\(0x([0-9a-fA-F]+)\)$', op)
    if m:
        return ('jmpind', int(m.group(1), 16), None)

    # abs,X or abs,Y or zp,X or zp,Y
    m = re.match(r'^0x([0-9a-fA-F]+),([XY])$', op)
    if m:
        addr = int(m.group(1), 16)
        idx  = m.group(2)
        mode = ('zpx' if nbytes == 2 and idx=='X' else
                'zpy' if nbytes == 2 and idx=='Y' else
                'absx' if idx=='X' else 'absy')
        return (mode, addr, idx)

    # abs or zp (bare address)
    m = re.match(r'^0x([0-9a-fA-F]+)$', op)
    if m:
        addr = int(m.group(1), 16)
        mode = 'zp' if nbytes == 2 else 'abs'
        return (mode, addr, None)

    # Accumulator (e.g. "ASL A" in some disassemblers)
    if op == 'A':
        return ('acc', 0, None)

    return ('impl', 0, None)

def operand_read(mode, addr, idx):
    """C expression that reads the source value.  Direct (zp/abs) named accesses
    read as the bare alias (level_stage); indexed accesses keep the MEM_<name>
    base in the index arithmetic (mem[MEM_foo+cpu.X])."""
    if mode == 'imm':   return f'0x{addr:02X}'
    if mode in ('zp','abs'):
        return addr_read(addr)
    if mode == 'zpx':   return f'mem[(uint8_t)({mem_base(addr)}+cpu.X)]'
    if mode == 'zpy':   return f'mem[(uint8_t)({mem_base(addr)}+cpu.Y)]'
    if mode == 'absx':  return f'bus_read(0x{addr:04X}+cpu.X)' if is_hw(addr) else f'mem[{mem_base(addr)}+cpu.X]'
    if mode == 'absy':  return f'bus_read(0x{addr:04X}+cpu.Y)' if is_hw(addr) else f'mem[{mem_base(addr)}+cpu.Y]'
    if mode == 'indy':  return f'bus_read(ZP_IND_Y(0x{addr:02X}))'
    if mode == 'indx':  return f'bus_read(ZP_IND_X(0x{addr:02X}))'
    return '0'

# operand_read now handles every mode (incl. abs,X/abs,Y via mem_base), so the
# old _fixed variant is a thin alias kept for call-site compatibility.
def operand_read_fixed(mode, addr, idx):
    return operand_read(mode, addr, idx)

def operand_addr_expr(mode, addr, idx):
    """C expression giving the effective *address* (for INC_M/DEC_M targets and
    indexed write bases).  Always an address, never a bare lvalue alias."""
    if mode in ('zp','abs'):  return mem_index(addr)
    if mode == 'zpx':  return f'(uint8_t)({mem_base(addr)}+cpu.X)'
    if mode == 'zpy':  return f'(uint8_t)({mem_base(addr)}+cpu.Y)'
    if mode == 'absx': return f'{mem_base(addr)}+cpu.X'
    if mode == 'absy': return f'{mem_base(addr)}+cpu.Y'
    if mode == 'indy': return f'ZP_IND_Y(0x{addr:02X})'
    if mode == 'indx': return f'ZP_IND_X(0x{addr:02X})'
    return '0'

def needs_bus_write(addr):
    """True for addresses that must go through bus_write() so the platform layer is
    notified.  BBC I/O ($FC00-$FEFF) is always routed.  The OS vector page
    ($0200-$02FF) must also go through bus_write so platform_shadow_write() is
    called — without it the game claiming IRQ1V ($0204/$0205, written at
    $4E4F/$4E54) is invisible to the platform and the 50 Hz handler is never
    dispatched.  Revs claims IRQ1V and nothing else (docs/static-map.md), but the
    whole page is routed because bus.h routes the whole page: the two must agree or
    a write is notified in one build and not the other.
    Reads of the vector page stay plain mem[] — matching bus_read."""
    return is_hw(addr) or (0x0200 <= addr < 0x0300)

def write_expr(mode, addr, idx, val_expr):
    if mode in ('zp','abs'):
        if needs_bus_write(addr):
            return f'bus_write({mem_index(addr)}, {val_expr})'
        alias = mem_alias(addr)
        if alias:
            return f'{alias} = {val_expr}'           # bare lvalue: level_stage = ...
        return f'mem[0x{addr:04X}] = {val_expr}'
    ea = operand_addr_expr(mode, addr, idx)
    if mode in ('absx','absy','zpx','zpy'):
        return f'mem[{ea}] = {val_expr}'
    # indirect modes
    return f'bus_write({ea}, {val_expr})'

# ---------------------------------------------------------------------------
# Translate one instruction to C statement(s)
# ---------------------------------------------------------------------------
BRANCH_FLAGS = {
    'BEQ': 'cpu.Z', 'BNE': '!cpu.Z',
    'BCS': 'cpu.C', 'BCC': '!cpu.C',
    'BMI': 'cpu.N', 'BPL': '!cpu.N',
    'BVS': 'cpu.V', 'BVC': '!cpu.V',
}

# ---------------------------------------------------------------------------
# Self-modifying instruction emission (SMC_SITES)
# ---------------------------------------------------------------------------
# The three classes get three faithful runtime-dispatched forms.  Every one reads the
# patched byte(s) out of mem[] — which holds the code image as well as the data, so the
# emitted C sees exactly what the 6502 would fetch — and every one traps loudly on a
# value the evidence does not cover.  Nothing here guesses.

# Opcode-slot alternatives: how to emit a 1-byte opcode that is NOT the statically
# decoded one.  Deliberately tiny — an opcode outside this table is an error at gen time,
# not a silent mis-emission.
SMC_ALT_OPCODE = {
    'NOP': 'NOP();',
    'INY': 'INY();',
    'INX': 'INX();',
    'DEY': 'DEY();',
    'DEX': 'DEX();',
    'RTS': 'return;',
}

def smc_dyn_ea(insn, mode, val):
    """Effective-address expression for an instruction whose operand BYTES are patched.
    Both operand bytes are read from mem[] even when only one is rewritten: the static
    one reads back its own image value, and reading both keeps the emitted C honest
    about where the address comes from.  Byte-wise, so it is endian-safe on the 68000."""
    a = insn['addr']
    if mode in ('abs', 'absx', 'absy'):
        base = f'(uint16_t)(mem[0x{a+1:04X}] | (mem[0x{a+2:04X}] << 8))'
    elif mode in ('zp', 'zpx', 'zpy'):
        base = f'(uint16_t)mem[0x{a+1:04X}]'
    else:
        raise SystemExit(f'SMC: no dynamic EA form for mode {mode} at ${a:04X}')
    if mode in ('absx', 'zpx'): return f'(uint16_t)({base} + cpu.X)'
    if mode in ('absy', 'zpy'): return f'(uint16_t)({base} + cpu.Y)'
    return base

def emit_smc_operand(insn, mode, val, site):
    """'operand' class: the opcode stands, the operand bytes are rewritten at run time.
    Length is unchanged, so nothing downstream shifts — only the address/value moves."""
    a, mnem = insn['addr'], insn['mnem']
    frm = ' / '.join(site['from'])
    patched = ' '.join(f'${b:04X}' for b in sorted(site['bytes']))
    out = [f'    /* {a:04x} */',
           f'    /* ⚠ SMC: operand byte(s) {patched} rewritten from {frm} — '
           f'address/value read from mem[] at run time */']
    if mode == 'imm':
        src = f'mem[0x{a+1:04X}]'
        if mnem in ('LDA', 'LDX', 'LDY'):
            out.append(f'    LD{mnem[2]}({src});'); return out
        if mnem in ('CMP', 'CPX', 'CPY', 'ADC', 'SBC', 'AND', 'ORA', 'EOR', 'BIT'):
            out.append(f'    {mnem}({src});'); return out
        raise SystemExit(f'SMC: no immediate form for {mnem} at ${a:04X}')
    if mode in ('indy', 'indx'):
        # The zero-page POINTER NUMBER itself is patched, so the store/load walks a
        # different pointer pair per call.  ZP_IND_Y/X take an expression, so the
        # patched byte drops straight in.
        macro = 'ZP_IND_Y' if mode == 'indy' else 'ZP_IND_X'
        ea = f'{macro}(mem[0x{a+1:04X}])'
    else:
        ea = smc_dyn_ea(insn, mode, val)
    # A runtime EA cannot be range-tested at gen time, so route through the bus: it
    # dispatches I/O vs mem[] itself and is correct for either.
    if mnem in ('STA', 'STX', 'STY'):
        out.append(f'    bus_write({ea}, cpu.{mnem[2]});')
    elif mnem in ('LDA', 'LDX', 'LDY'):
        out.append(f'    LD{mnem[2]}(bus_read({ea}));')
    elif mnem in ('CMP', 'CPX', 'CPY', 'ADC', 'SBC', 'AND', 'ORA', 'EOR', 'BIT'):
        out.append(f'    {mnem}(bus_read({ea}));')
    elif mnem in ('INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR'):
        suffix = '_M'
        out.append(f'    {mnem}{suffix}({ea});')
    else:
        raise SystemExit(f'SMC: no dynamic-EA form for {mnem} at ${a:04X}')
    return out

def emit_smc_opcode(insn, site, normal_lines):
    """'opcode' class: a 1-byte opcode slot switched between known values.  Dispatch on
    the byte in mem[]; the statically decoded value reuses the ordinary translation."""
    a, mnem = insn['addr'], insn['mnem']
    frm = ' / '.join(site['from'])
    out = [f'    /* {a:04x} */',
           f'    /* ⚠ SMC: opcode slot rewritten from {frm} — dispatched on mem[${a:04X}] */',
           f'    switch (mem[0x{a:04X}]) {{']
    if mnem not in site['values'].values():
        raise SystemExit(f'SMC ${a:04X}: listing decodes {mnem}, which is not among the '
                         f'documented values {sorted(site["values"].values())}')
    for byte, alt in sorted(site['values'].items()):
        out.append(f'    case 0x{byte:02X}:  /* {alt} */')
        if alt == mnem:
            # The decoded instruction: emit its ordinary translation (drop the address
            # comment, which the switch header already carries).
            for ln in normal_lines:
                if ln.strip().startswith('/*'): continue
                out.append('        ' + ln.strip())
        elif alt in SMC_ALT_OPCODE:
            out.append('        ' + SMC_ALT_OPCODE[alt])
        else:
            raise SystemExit(f'SMC ${a:04X}: no emission for alternative opcode {alt} — '
                             f'add it to SMC_ALT_OPCODE with evidence')
        if alt != 'RTS':
            out.append('        break;')
    out.append(f'    default: platform_smc_unhandled(0x{a:04X}, mem[0x{a:04X}]); return;')
    out.append('    }')
    return out

def emit_smc_branch(insn, site, mnem, dispatch_targets):
    """'branch' class: the branch OFFSET is rewritten, so the target is only known at run
    time.  Compute it the way the 6502 does and dispatch over the enclosing function's
    instruction starts.  An offset landing anywhere else (mid-instruction, or outside the
    function) traps — a computed goto into the middle of an instruction has no meaning in
    C, and pretending otherwise is how a wrong rasteriser ships looking plausible."""
    a = insn['addr']
    flag = BRANCH_FLAGS[mnem]
    frm = ' / '.join(site['from'])
    off = f'(int8_t)mem[0x{a+1:04X}]'
    out = [f'    /* {a:04x} */',
           f'    /* ⚠ SMC: branch offset at ${a+1:04X} rewritten from {frm} — '
           f'target computed at run time */',
           f'    if ({flag}) {{',
           f'        uint16_t _smct = (uint16_t)(0x{a+2:04X} + {off});',
           f'        switch (_smct) {{']
    for t in dispatch_targets:
        out.append(f'        case 0x{t:04X}: goto L_{t:04x};')
    out.append(f'        default: platform_smc_unhandled(0x{a:04X}, _smct); return;')
    out += ['        }', '    }']
    return out

def translate_insn(insn, func, all_funcs_by_start, symbols, local_targets,
                   external_entries=None, wrapper_names=None,
                   smc_dispatch_targets=None, _no_smc=False):
    """external_entries: {addr → container_func} from main pass-1 analysis.
    wrapper_names:      {addr → C name} for mid-function entry wrappers.
    smc_dispatch_targets: sorted instruction starts of the enclosing function, used as
                          the case set for a runtime-computed ('branch' SMC) target."""
    if external_entries is None:  external_entries = {}
    if wrapper_names   is None:  wrapper_names    = {}
    if smc_dispatch_targets is None: smc_dispatch_targets = []

    addr  = insn['addr']
    mnem  = insn['mnem']
    op    = insn['op']
    nbytes = len(insn['bytes'])
    mode, val, idx = parse_operand(op, nbytes, symbols)

    lines = [f'    /* {addr:04x} */']

    # --- Self-modifying instruction? ---
    site = None if _no_smc else SMC_SITES.get(addr)
    if site is not None:
        if site['kind'] == 'operand':
            return emit_smc_operand(insn, mode, val, site)
        if site['kind'] == 'branch':
            if mnem not in BRANCH_FLAGS:
                raise SystemExit(f'SMC ${addr:04X}: marked "branch" but decodes as {mnem}')
            return emit_smc_branch(insn, site, mnem, smc_dispatch_targets)
        if site['kind'] == 'opcode':
            normal = translate_insn(insn, func, all_funcs_by_start, symbols,
                                    local_targets, external_entries, wrapper_names,
                                    smc_dispatch_targets, _no_smc=True)
            return emit_smc_opcode(insn, site, normal)
        raise SystemExit(f'SMC ${addr:04X}: unknown kind {site["kind"]!r}')

    def resolve_target_name(target):
        """Return the C name to call for a branch/JMP to target.
        Checks: local function start, wrapper, symbol, fallback."""
        if target in wrapper_names:
            return wrapper_names[target]
        if target in all_funcs_by_start:
            return all_funcs_by_start[target]['name']
        return symbols.get(target, f'FUN_{target:04x}')

    # --- Branches ---
    if mnem in BRANCH_FLAGS:
        target = val
        flag   = BRANCH_FLAGS[mnem]
        if target in local_targets:
            # Hooks are injected at the label definition; don't duplicate here.
            lines.append(f'    if ({flag}) goto L_{target:04x};')
        else:
            # Cross-function branch → conditional tail call.
            name = resolve_target_name(target)
            lines.append(f'    if ({flag}) {{ {name}(); return; }}')
        return lines

    # --- JMP ---
    if mnem == 'JMP':
        if mode == 'jmpind':
            # Indirect JMP through a pointer cell.  Revs has exactly one — `JMP ($4F1D)`
            # at $4E59, the IRQ1V chain-on to the handler it displaced — so the target is
            # MOS ROM, not engine code, and the platform decides what that means.
            # The high byte wraps WITHIN the page, reproducing the NMOS 6502's
            # JMP-($xxFF) bug: a faithful emitter must not silently fix hardware.
            hi = (val & 0xFF00) | ((val + 1) & 0x00FF)
            lines.append(f'    {{ uint16_t _t = (uint16_t)(mem[0x{val:04X}] | '
                         f'((uint16_t)mem[0x{hi:04X}] << 8)); '
                         f'platform_indirect_jmp(_t); return; }}')
        elif is_mos(val):
            lines.append(f'    platform_mos_call(0x{val:04X});  /* {mos_name(val)} */')
            lines.append('    return;')
        else:
            target = val
            if target in local_targets:
                lines.append(f'    goto L_{target:04x};')
            else:
                name = resolve_target_name(target)
                lines.append(f'    {name}(); return;')
        return lines

    # --- JSR ---
    if mnem == 'JSR':
        target = val
        if is_mos(target):
            # An OS entry, not a routine in the image: A/X/Y cross the seam through the
            # global cpu struct, exactly as on the 6502.  docs/static-map.md §MOS calls.
            lines.append(f'    platform_mos_call(0x{target:04X});  /* {mos_name(target)} */')
            return lines
        name = resolve_target_name(target)
        lines.append(f'    {name}();')
        return lines

    # --- RTS / RTI ---
    if mnem == 'RTS':
        lines.append('    return;')
        return lines
    if mnem == 'RTI':
        lines.append('    PLP(); return;')
        return lines

    # --- NOP / BRK ---
    if mnem == 'NOP':
        lines.append('    NOP();')
        return lines
    if mnem == 'BRK':
        # BRK is NOT a no-op on the BBC: it pushes PC+2/P and vectors through BRKV
        # ($0202) into the MOS error handler, which does not return to the next
        # instruction.  Translating it as a comment (the Atari port's choice, where the
        # engine had no BRKs) would make a routine that traps look like a routine that
        # falls through — and Revs reaches four of these, see report_brk_targets.
        lines.append(f'    platform_brk(0x{addr:04X});')
        lines.append('    return;')
        return lines

    # --- Flag ops ---
    simple_flag = {'CLC':'CLC()','SEC':'SEC()','CLI':'CLI()','SEI':'SEI()',
                   'CLD':'CLD()','SED':'SED()','CLV':'CLV()'}
    if mnem in simple_flag:
        lines.append(f'    {simple_flag[mnem]};')
        return lines

    # --- Stack ---
    if mnem == 'PHA': lines.append('    PHA();'); return lines
    if mnem == 'PLA': lines.append('    PLA();'); return lines
    if mnem == 'PHP': lines.append('    PHP();'); return lines
    if mnem == 'PLP': lines.append('    PLP();'); return lines

    # --- Transfer ---
    for mn, mac in [('TAX','TAX()'),('TAY','TAY()'),('TXA','TXA()'),('TYA','TYA()'),
                    ('TSX','TSX()'),('TXS','TXS()'),
                    ('INX','INX()'),('INY','INY()'),('DEX','DEX()'),('DEY','DEY()')]:
        if mnem == mn:
            lines.append(f'    {mac};')
            return lines

    # --- Load ---
    if mnem in ('LDA','LDX','LDY'):
        reg = mnem[2]  # A, X, or Y
        src = operand_read_fixed(mode, val, idx)
        lines.append(f'    LD{reg}({src});')
        return lines

    # --- Store ---
    if mnem in ('STA','STX','STY'):
        reg = mnem[2]
        stmt = write_expr(mode, val, idx, f'cpu.{reg}')
        lines.append(f'    {stmt};')
        return lines

    # --- ADC / SBC ---
    if mnem == 'ADC':
        src = operand_read_fixed(mode, val, idx)
        lines.append(f'    ADC({src});')
        return lines
    if mnem == 'SBC':
        src = operand_read_fixed(mode, val, idx)
        lines.append(f'    SBC({src});')
        return lines

    # --- Compare ---
    cmp_map = {'CMP': 'CMP', 'CPX': 'CPX', 'CPY': 'CPY'}
    if mnem in cmp_map:
        src = operand_read_fixed(mode, val, idx)
        lines.append(f'    {cmp_map[mnem]}({src});')
        return lines

    # --- Logical ---
    for mn, mac in [('AND','AND'),('ORA','ORA'),('EOR','EOR')]:
        if mnem == mn:
            src = operand_read_fixed(mode, val, idx)
            lines.append(f'    {mac}({src});')
            return lines

    # --- BIT ---
    if mnem == 'BIT':
        src = operand_read_fixed(mode, val, idx)
        lines.append(f'    BIT({src});')
        return lines

    # --- INC / DEC memory ---
    if mnem == 'INC':
        ea = operand_addr_expr(mode, val, idx)
        lines.append(f'    INC_M({ea});')
        return lines
    if mnem == 'DEC':
        ea = operand_addr_expr(mode, val, idx)
        lines.append(f'    DEC_M({ea});')
        return lines

    # --- Shift / Rotate ---
    if mnem == 'ASL':
        if mode == 'impl' or mode == 'acc':
            lines.append('    ASL_A();')
        else:
            ea = operand_addr_expr(mode, val, idx)
            lines.append(f'    ASL_M({ea});')
        return lines
    if mnem == 'LSR':
        if mode == 'impl' or mode == 'acc':
            lines.append('    LSR_A();')
        else:
            ea = operand_addr_expr(mode, val, idx)
            lines.append(f'    LSR_M({ea});')
        return lines
    if mnem == 'ROL':
        if mode == 'impl' or mode == 'acc':
            lines.append('    ROL_A();')
        else:
            ea = operand_addr_expr(mode, val, idx)
            lines.append(f'    ROL_M({ea});')
        return lines
    if mnem == 'ROR':
        if mode == 'impl' or mode == 'acc':
            lines.append('    ROR_A();')
        else:
            ea = operand_addr_expr(mode, val, idx)
            lines.append(f'    ROR_M({ea});')
        return lines

    # --- Unknown ---
    lines.append(f'    /* TODO: {mnem} {op} */')
    return lines

# ---------------------------------------------------------------------------
# Peephole: register/flag liveness + load-immediate→store folding
# ---------------------------------------------------------------------------
# Cleans up the transliteration's `LDA(#imm); STA addr;` two-step (a faithful
# but ugly 6502 idiom) into a direct `addr = imm;` when the loaded register and
# the N/Z flags it set are provably dead afterwards.  Gated on a real liveness
# analysis so the fold can never drop a value/flag a later instruction (or the
# function's exit, treated as live for all regs/flags) still needs.
PEEPHOLE = True

ALL_LIVE = frozenset({'A', 'X', 'Y', 'N', 'Z', 'C', 'V'})

# Branch mnemonic → the flag it reads.
_BRANCH_READ = {'BEQ': 'Z', 'BNE': 'Z', 'BCS': 'C', 'BCC': 'C',
                'BMI': 'N', 'BPL': 'N', 'BVS': 'V', 'BVC': 'V'}

def _mode_index_reg(mode):
    """The register an addressing mode reads to form the effective address."""
    if mode in ('zpx', 'absx', 'indx'): return 'X'
    if mode in ('zpy', 'absy', 'indy'): return 'Y'
    return None

def insn_effects(mnem, mode):
    """Return (reads, writes) sets over {A,X,Y,N,Z,C,V} for one instruction.
    Conservative: anything unmodelled reads+writes everything (so nothing around
    it is ever considered dead).  The index register of an indexed/indirect mode
    is always an additional read (it is needed to compute the address)."""
    rd, wr = _insn_effects_base(mnem, mode)
    ireg = _mode_index_reg(mode)
    if ireg:
        rd = rd | {ireg}
    return (rd, wr)

def _insn_effects_base(mnem, mode):
    if mnem in ('LDA','LDX','LDY'): return (set(),            {mnem[2], 'N', 'Z'})
    if mnem in ('STA','STX','STY'): return ({mnem[2]},        set())
    if mnem == 'TAX': return ({'A'}, {'X','N','Z'})
    if mnem == 'TAY': return ({'A'}, {'Y','N','Z'})
    if mnem == 'TXA': return ({'X'}, {'A','N','Z'})
    if mnem == 'TYA': return ({'Y'}, {'A','N','Z'})
    if mnem == 'TSX': return (set(), {'X','N','Z'})
    if mnem == 'TXS': return ({'X'}, set())
    if mnem in ('INX','DEX'): return ({'X'}, {'X','N','Z'})
    if mnem in ('INY','DEY'): return ({'Y'}, {'Y','N','Z'})
    if mnem in ('ADC','SBC'): return ({'A','C'}, {'A','N','Z','C','V'})
    if mnem in ('AND','ORA','EOR'): return ({'A'}, {'A','N','Z'})
    if mnem == 'CMP': return ({'A'}, {'N','Z','C'})
    if mnem == 'CPX': return ({'X'}, {'N','Z','C'})
    if mnem == 'CPY': return ({'Y'}, {'N','Z','C'})
    if mnem == 'BIT': return (set(), {'N','Z','V'})
    if mnem in ('INC','DEC'): return (set(), {'N','Z'})
    if mnem in ('ASL','LSR'):
        if mode in ('acc','impl'): return ({'A'}, {'A','N','Z','C'})
        return (set(), {'N','Z','C'})
    if mnem in ('ROL','ROR'):
        if mode in ('acc','impl'): return ({'A','C'}, {'A','N','Z','C'})
        return ({'C'}, {'N','Z','C'})
    if mnem in _BRANCH_READ: return ({_BRANCH_READ[mnem]}, set())
    if mnem in ('CLC','SEC'): return (set(), {'C'})
    if mnem == 'CLV': return (set(), {'V'})
    if mnem in ('CLD','SED','CLI','SEI','NOP'): return (set(), set())
    if mnem == 'PHA': return ({'A'}, set())
    if mnem == 'PLA': return (set(), {'A','N','Z'})
    if mnem == 'PHP': return ({'N','Z','C','V'}, set())
    if mnem == 'PLP': return (set(), {'N','Z','C','V'})
    if mnem in ('JMP','RTS','RTI','BRK'): return (set(), set())  # control flow via succ/sink
    # JSR and anything else: assume it reads args + clobbers everything.
    return (set(ALL_LIVE), set(ALL_LIVE))

def compute_liveness(insns, symbols, local_targets):
    """Backward CFG liveness over regs/flags.  Returns live_out[index] (a set).
    Exits (RTS/RTI/BRK, tail-calls, indirect jumps, fall-through) are sinks where
    every reg/flag is live — so a value reaching an exit is never folded away."""
    n = len(insns)
    idx_by_addr = {ins['addr']: i for i, ins in enumerate(insns)}
    eff = []
    succ = []
    for i, ins in enumerate(insns):
        mnem, op, nbytes = ins['mnem'], ins['op'], len(ins['bytes'])
        # An SMC site's real instruction is only known at run time, so model it as
        # reading AND clobbering everything: nothing around it can be folded away on the
        # strength of a decode that the writers are free to change.
        if ins['addr'] in SMC_SITES:
            eff.append((set(ALL_LIVE), set(ALL_LIVE)))
        else:
            eff.append(insn_effects(mnem, parse_operand(op, nbytes, symbols)[0]))
        s = []
        if ins['addr'] in SMC_SITES and SMC_SITES[ins['addr']]['kind'] == 'branch':
            # Patched offset: the taken target is a run-time value, so treat it as an
            # exit (all regs/flags live) plus the fall-through.
            s.append('EXIT')
            s.append(i + 1 if i + 1 < n else 'EXIT')
        elif mnem in BRANCH_FLAGS:
            _, val, _ = parse_operand(op, nbytes, symbols)
            s.append(idx_by_addr[val] if val in local_targets and val in idx_by_addr else 'EXIT')
            s.append(i + 1 if i + 1 < n else 'EXIT')         # fall-through
        elif mnem == 'JMP':
            _, val, _ = parse_operand(op, nbytes, symbols)
            s.append(idx_by_addr[val] if val in local_targets and val in idx_by_addr else 'EXIT')
        elif mnem in ('RTS', 'RTI', 'BRK'):
            s.append('EXIT')
        else:  # JSR, loads/stores, ALU, … fall through to next
            s.append(i + 1 if i + 1 < n else 'EXIT')
        succ.append(s)

    live_in = [set() for _ in range(n)]
    live_out = [set() for _ in range(n)]
    changed = True
    while changed:
        changed = False
        for i in range(n - 1, -1, -1):
            out = set()
            for s in succ[i]:
                out |= ALL_LIVE if s == 'EXIT' else live_in[s]
            rd, wr = eff[i]
            inn = (out - wr) | rd
            if out != live_out[i] or inn != live_in[i]:
                live_out[i], live_in[i] = out, inn
                changed = True
    return live_out

def compute_imm_store_folds(insns, symbols, local_targets, external_entry_labels,
                            blocked_addrs):
    """Fold a `LD{R}(value)` into the run of consecutive `ST{R}` that follows it,
    turning each store into `<store> = value;` and dropping the load.  Returns
    (skip_loads, store_vals): load indices to omit, store index→value-expr.

    Folds handled:
      * load-immediate, single OR multiple stores:  LDA #0; STA a; STA b -> a=0; b=0;
      * load-memory, single store only:             LDA $x; STA $y       -> $y = $x;
        (single-store only: a 2nd store in the run could write $x and change what a
         later store should read.)

    Safe iff: the load and EVERY store in the run are straight-line (not a branch
    target / split / injected-hook addr — so each store is reachable only via the
    load, guaranteeing cpu.R holds `value` there), and R + N + Z are all dead in
    live_out of the LAST store (nothing later needs the register or its flags)."""
    skip_loads, store_vals = set(), {}
    if not PEEPHOLE:
        return skip_loads, store_vals
    live_out = compute_liveness(insns, symbols, local_targets)
    n = len(insns)

    def straight_line(a):
        return not (a in local_targets or a in external_entry_labels or a in blocked_addrs)

    i = 0
    while i < n - 1:
        ld = insns[i]
        if ld['mnem'] not in ('LDA', 'LDX', 'LDY') or not straight_line(ld['addr']):
            i += 1; continue
        reg = ld['mnem'][2]
        lmode, lval, lidx = parse_operand(ld['op'], len(ld['bytes']), symbols)
        if lmode == 'imm':
            val_expr, allow_multi = f'0x{lval:02X}', True
        elif lmode in ('zp', 'abs', 'zpx', 'zpy', 'absx', 'absy', 'indx', 'indy'):
            val_expr, allow_multi = operand_read_fixed(lmode, lval, lidx), False
        else:
            i += 1; continue
        # Maximal run of consecutive same-register stores.
        run = []
        j = i + 1
        while j < n and insns[j]['mnem'] == 'ST' + reg:
            run.append(j); j += 1
        if not run or (not allow_multi and len(run) > 1):
            i += 1; continue
        # Every store in the run must be straight-line; the last store must leave
        # R and the N/Z flags dead.
        if not all(straight_line(insns[k]['addr']) for k in run):
            i += 1; continue
        if {reg, 'N', 'Z'} & live_out[run[-1]]:
            i += 1; continue
        skip_loads.add(i)
        for k in run:
            st = insns[k]
            smode, sval, sidx = parse_operand(st['op'], len(st['bytes']), symbols)
            store_vals[k] = write_expr(smode, sval, sidx, val_expr)
        i = run[-1] + 1
    return skip_loads, store_vals

# ---------------------------------------------------------------------------
# Translate one function
# ---------------------------------------------------------------------------
def translate_func(func, all_funcs_by_start, symbols,
                   external_entry_labels=None,
                   external_entries=None, wrapper_names=None):
    """Translate one 6502 function to C.

    external_entry_labels: set of addresses within this function that are
    entered from outside (e.g. JSR/JMP to a mid-body address).  These are
    used as split-points: the function body stops at each one and emits a
    tail-call to the corresponding wrapper, so that external callers can
    invoke the correct code slice directly.  They are NOT emitted as goto
    labels in the container — instead they become function calls.
    """
    start = func['start']
    name  = func['name']
    insns = func['insns']
    if external_entry_labels is None: external_entry_labels = set()
    if external_entries       is None: external_entries      = {}
    if wrapper_names           is None: wrapper_names         = {}

    if start in MANUAL_FUNCS:
        return [f'/* {name} @ ${start:04X}: manual implementation in revs_manual.c */']

    func_end = func['end']
    # body_lo: lowest body address (start, or lower if an orphan prefix was
    # absorbed).  skip_to: the named entry to jump to past the prefix (None
    # when there is no prefix).
    body_lo = func.get('body_start', start)
    skip_to = func.get('skip_to')
    # local_targets: branch/JMP targets that stay in this function's body.
    # Exclude external entry labels — those become tail-calls, not gotos.
    # Also exclude targets at or beyond the first split point: those are now
    # in a different C function and cannot be reached via goto.
    first_split = min(external_entry_labels) if external_entry_labels else None
    local_targets = set()
    for insn in insns:
        mnem, op, nbytes = insn['mnem'], insn['op'], len(insn['bytes'])
        if mnem in BRANCH_FLAGS or mnem == 'JMP':
            mode, val, idx = parse_operand(op, nbytes, symbols)
            if body_lo <= val <= func_end and val not in external_entry_labels:
                if first_split is None or val < first_split:
                    local_targets.add(val)
    # The named entry needs an L_ label so the prefix-skipping goto can reach it.
    if skip_to is not None:
        local_targets.add(skip_to)

    # A function containing a patched-offset branch (SMC 'branch') needs a label on EVERY
    # instruction start it could reach, because the target is not known until run time.
    # The switch in emit_smc_branch dispatches over exactly this set; -Wno-unused-label
    # (both Makefiles) covers the labels no static branch reaches.
    smc_dispatch_targets = []
    if any(insn['addr'] in SMC_SITES and SMC_SITES[insn['addr']]['kind'] == 'branch'
           for insn in insns):
        for insn in insns:
            a = insn['addr']
            if a in external_entry_labels:      continue   # a different C function now
            if first_split is not None and a >= first_split: continue
            local_targets.add(a)
            smc_dispatch_targets.append(a)
        smc_dispatch_targets.sort()

    # Peephole: fold `LD{R}(#imm); ST{R} addr;` → `addr = imm;` (liveness-checked).
    # Never fold a load/store pair that touches a self-modifying instruction: what it
    # does is not what the listing says, so the liveness proof does not apply to it.
    blocked_addrs = set(PRE_INSN_HOOKS) | set(SPINWAIT_HOOKS) | set(SMC_SITES)
    skip_loads, store_vals = compute_imm_store_folds(
        insns, symbols, local_targets, external_entry_labels, blocked_addrs)

    TERMINATORS = {'RTS', 'RTI', 'JMP', 'BRK'}

    lines = []
    # Emit the symbol's note (symbols.csv col 5) as a doc-comment, plus the
    # original 6502 entry address for provenance (Phase 5 traceability).
    note = SYMBOL_NOTES.get(start)
    if note:
        lines.append(f'/* {name} @ ${start:04X}: {note} */')
    # When validating a native reimplementation, define the transliterated body
    # under the `__t6502` reference name; the plain name is the native version.
    def_name = name + VALIDATE_SUFFIX if start in VALIDATE_FUNCS else name
    if start in VALIDATE_FUNCS:
        lines.append(f'/* faithful transliteration kept as the validation oracle; '
                     f'native {name}() lives in revs_native.c (see VALIDATE_FUNCS) */')
    lines.append(f'void {def_name}(void) {{')
    # Orphan-prefix functions: the named entry is mid-body, so callers must
    # skip the prefix (which is reachable only via an internal backward branch).
    if skip_to is not None:
        lines.append(f'    goto L_{skip_to:04x};  /* enter past orphan-prefix loop body */')
    hit_split = False
    last_insn = None
    # Phase numbers for the main loop's top-level JSRs, in source order.  1-based: phase 0
    # is reserved for everything OUTSIDE the loop (the frame wait and the interrupt), so
    # the shares add up to the whole frame rather than only to the instrumented part.
    _lo, _hi = MAIN_LOOP_BRACKET
    phase_ids = {}
    for _ins in insns:
        if _lo <= _ins['addr'] <= _hi and _ins['mnem'] == 'JSR':
            phase_ids[_ins['addr']] = len(phase_ids) + 1
    for idx, insn in enumerate(insns):
        addr = insn['addr']
        # Peephole: a folded load is dropped entirely (its value moves into the
        # following store); the store is rewritten to assign the literal directly.
        if idx in skip_loads:
            last_insn = insn
            continue
        # Split point: stop the current function and tail-call the wrapper.
        if addr in external_entry_labels:
            wname = wrapper_names.get(addr, f'FUN_{addr:04x}')
            lines.append(f'    {wname}(); return;')
            hit_split = True
            break   # do not translate addr or any subsequent instructions
        if idx in store_vals:
            lines.append(f'    /* {addr:04x} */')
            lines.append(f'    {store_vals[idx]};')
            last_insn = insn
            continue
        if addr in local_targets:
            # Inject spin-wait hook at the label itself so ALL paths
            # reaching this label (including unconditional JMPs) get it.
            hook = SPINWAIT_HOOKS.get(addr, '')
            if hook:
                lines.append(f'L_{addr:04x}:; {hook}')
            else:
                lines.append(f'L_{addr:04x}:;')
        pre = PRE_INSN_HOOKS.get(addr, '')
        if pre:
            lines.append(f'    {pre}')
        # Phase bracket: close the previous phase and open this call's, so a PROBES run
        # reports a per-callee share of the frame.  Only the FLAT top-level JSRs of the
        # main loop are bracketed — a bracket inside a nested callee would be timing a
        # subtree of a subtree and the shares would stop summing to the frame.
        lo, hi = MAIN_LOOP_BRACKET
        if lo <= addr <= hi and insn['mnem'] == 'JSR':
            lines.append(f'    PROBE_PHASE({phase_ids[addr]});')
        stmt_lines = translate_insn(insn, func, all_funcs_by_start, symbols,
                                    local_targets, external_entries, wrapper_names,
                                    smc_dispatch_targets)
        lines.extend(stmt_lines)
        last_insn = insn

    # Fall-through detection: if the function did not end with a terminator
    # (RTS/RTI/JMP/BRK) and was not cut by a split point, the 6502 code
    # falls through into the next function.  Emit a tail-call to it.
    if not hit_split and last_insn is not None:
        last_mnem = last_insn['mnem']
        if last_mnem not in TERMINATORS:
            next_addr = last_insn['addr'] + len(last_insn['bytes'])
            # Prefer a known function start; else check wrapper table.
            next_func = all_funcs_by_start.get(next_addr)
            if next_func and next_func['start'] not in MANUAL_FUNCS:
                lines.append(f'    {next_func["name"]}(); return;')
            elif next_addr in wrapper_names:
                lines.append(f'    {wrapper_names[next_addr]}(); return;')
            elif next_func:
                next_name = symbols.get(next_addr, f'FUN_{next_addr:04x}')
                lines.append(f'    {next_name}(); return;')

    lines.append('}')
    lines.append('')
    return lines

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def find_containing_func(addr, funcs):
    """Return the function whose address range contains addr, or None.
    Uses func_lo so absorbed orphan prefixes count as part of the body."""
    for f in funcs:
        if func_lo(f) <= addr <= f['end']:
            return f
    return None

def main():
    symbols = load_symbols(SYM_CSV)
    write_mem_header(OUT_MEM)
    funcs, func_by_addr = parse_listing(LISTING, symbols)
    funcs_by_start = {f['start']: f for f in funcs}

    print(f'Parsed {len(funcs)} functions, '
          f'{sum(len(f["insns"]) for f in funcs)} instructions')

    # -----------------------------------------------------------------------
    # Pass 1: collect cross-function branch/JMP targets.
    # For each branch/JMP whose target T is NOT in the current function's
    # address range but IS within another function's range (and not at that
    # function's start), record T as an "external entry" needing a wrapper.
    # -----------------------------------------------------------------------
    # func_addr_ranges: list of (start, end) for range containment tests.
    func_addr_ranges = [(f['start'], f['end'], f) for f in funcs]

    # external_entries: target_addr → containing_func
    external_entries = {}
    # external_labels_for_func: func_start → set of addr needing L_xxxx labels
    external_labels_for_func = defaultdict(set)

    for func in funcs:
        for insn in func['insns']:
            mnem, op, nbytes = insn['mnem'], insn['op'], len(insn['bytes'])
            if mnem not in BRANCH_FLAGS and mnem not in ('JMP', 'JSR'):
                continue
            mode, val, _ = parse_operand(op, nbytes, symbols)
            if val == 0:
                continue
            # Is target within THIS function's range (incl. orphan prefix)?
            # A branch/JMP there is a plain local goto — but a JSR is NOT: it has to
            # return, so it needs a real C function even when the caller and callee sit
            # inside the same Ghidra function.  Revs has three such nested routines
            # (Ghidra put FUN_1DA6/FUN_1DAF inside project_geometry and FUN_3273 inside
            # FUN_3261); skipping them here emitted a call to a function that was never
            # defined, which the C compiler caught — a reminder that the C type system is
            # part of this pipeline's error detection, not an obstacle to it.
            if func_lo(func) <= val <= func['end'] and mnem != 'JSR':
                continue
            # Target is outside. Find which function contains it.
            container = find_containing_func(val, funcs)
            if container is None:
                continue
            if val == container['start']:
                continue  # it's a normal tail call to another function's start
            # Mid-function entry: needs a wrapper and a label in the container.
            external_entries[val] = container
            external_labels_for_func[container['start']].add(val)

    wrapper_names = {}  # target_addr → wrapper function name
    for addr, container in external_entries.items():
        wname = symbols.get(addr, f'FUN_{addr:04x}')
        wrapper_names[addr] = wname

    # -----------------------------------------------------------------------
    # Cascading split fixpoint.
    # When a function is split at address S, code in any segment before S
    # may have branches/JMPs that target addresses inside the post-split
    # tail (i.e. >= S within the same container).  Those target addresses
    # also need to become split functions so they can be called by name.
    # Iterate until no new entries are discovered.
    # -----------------------------------------------------------------------
    def segments_for_container(c):
        """Return [(seg_start, seg_end)] for all segments of container c."""
        splits = sorted(
            a for a, cont in external_entries.items() if cont['start'] == c['start']
        )
        # First segment starts at func_lo so an absorbed orphan prefix counts
        # as part of segment 0 (a backward branch into it is intra-segment, not
        # a spurious cross-segment split).
        starts = [func_lo(c)] + splits
        ends   = splits + [c['end'] + 1]
        return list(zip(starts, ends))

    changed = True
    while changed:
        changed = False
        for container in funcs:
            for seg_start, seg_end in segments_for_container(container):
                for insn in container['insns']:
                    if insn['addr'] < seg_start:
                        continue
                    if insn['addr'] >= seg_end:
                        break
                    mnem, op, nbytes = insn['mnem'], insn['op'], len(insn['bytes'])
                    if mnem not in BRANCH_FLAGS and mnem != 'JMP':
                        continue
                    mode, target, _ = parse_operand(op, nbytes, symbols)
                    if target <= 0:
                        continue
                    # Branch/JMP from within this segment to AFTER this segment
                    # but still within the container — needs a new split point.
                    # Branch crosses segment boundary: target is in the container's
                    # range but outside the current segment (forward OR backward).
                    # Skip targets that are already Ghidra function starts — those
                    # are handled as normal tail-calls, not split wrappers.
                    in_container = func_lo(container) <= target <= container['end']
                    in_segment   = seg_start <= target < seg_end
                    already_func = target in funcs_by_start
                    if in_container and not in_segment and not already_func and target not in external_entries:
                        external_entries[target] = container
                        external_labels_for_func[container['start']].add(target)
                        wname = symbols.get(target, f'FUN_{target:04x}')
                        wrapper_names[target] = wname
                        changed = True

    # -----------------------------------------------------------------------
    # Collect all branch/JMP/JSR targets that fall in no function range.
    # Ghidra sometimes misses functions for gap addresses. Generate stubs.
    # -----------------------------------------------------------------------
    jsr_targets_unknown = set()
    all_call_mnems = set(BRANCH_FLAGS.keys()) | {'JMP', 'JSR'}
    for func in funcs:
        for insn in func['insns']:
            if insn['mnem'] not in all_call_mnems: continue
            mode, val, _ = parse_operand(insn['op'], len(insn['bytes']), symbols)
            # Indirect JMP (e.g. JMP ($E0)) is emitted as platform_indirect_jmp;
            # the pointer address ($00E0) is data, not a call target — don't stub it.
            if mode == 'jmpind': continue
            if val == 0: continue
            if val in funcs_by_start: continue
            if val in external_entries: continue
            if val in wrapper_names: continue
            # A MOS entry is an OS call, emitted as platform_mos_call() — not a routine
            # in the image, so it must never become a stub.
            if is_mos(val): continue
            # Check if it falls in any function range
            if find_containing_func(val, funcs) is not None: continue
            jsr_targets_unknown.add(val)

    # A call into ROM that is NOT a MOS entry cannot be emitted at all: there is no code
    # for it in the image and no OS semantics to service.  Revs has none (17 MOS sites and
    # nothing else — docs/static-map.md), so if one appears, the listing or the entry set
    # changed and that is worth stopping for rather than emitting an empty stub.
    rom_calls = sorted(a for a in jsr_targets_unknown if a >= ROM_BASE)
    if rom_calls:
        raise SystemExit('JSR/JMP into ROM that is not a MOS entry: '
                         + ', '.join(f'${a:04X}' for a in rom_calls)
                         + '\n  -> identify each before generating (docs/bbc-hardware.md).')

    # -----------------------------------------------------------------------
    # Forward declarations header — includes wrapper function names.
    # -----------------------------------------------------------------------
    decl_lines = [
        '#ifndef REVS_DECL_H', '#define REVS_DECL_H',
        '/* Auto-generated by tools/transpile.py — do not edit */',
        '#include <stdint.h>', '',
        '/* Forward declarations for all 6502 routines */',
    ]
    for f in funcs:
        decl_lines.append(f'void {f["name"]}(void);')
        # Validated funcs: the plain name (declared above) is the native version
        # in revs_native.c; also declare the transliterated reference twin.
        if f['start'] in VALIDATE_FUNCS:
            decl_lines.append(f'void {f["name"]}{VALIDATE_SUFFIX}(void);')
    # Wrappers for mid-function entry points.
    decl_lines.append('')
    decl_lines.append('/* Wrappers for cross-function branch/JMP entry points */')
    for addr, wname in sorted(wrapper_names.items()):
        decl_lines.append(f'void {wname}(void);')
        # A validated mid-function entry: its split body is emitted under the
        # `__t6502` twin (translate_func, def_name) with the plain name native;
        # declare the twin too so the validation harness can reach it.
        if addr in VALIDATE_FUNCS:
            decl_lines.append(f'void {wname}{VALIDATE_SUFFIX}(void);')
    # Stubs for unlisted JSR targets.
    decl_lines.append('')
    decl_lines.append('/* Stubs for JSR targets without a Ghidra-detected function */')
    for addr in sorted(jsr_targets_unknown):
        name = symbols.get(addr, f'FUN_{addr:04x}')
        decl_lines.append(f'void {name}(void);')
    decl_lines += ['', '#endif /* REVS_DECL_H */']
    OUT_H.write_text('\n'.join(decl_lines) + '\n')
    print(f'Wrote {OUT_H}')

    # -----------------------------------------------------------------------
    # Pass 2: generate C.
    # -----------------------------------------------------------------------
    header = [
        '/* Auto-generated by tools/transpile.py — do not edit */',
        '#include "../cpu/cpu.h"',
        '#include "../cpu/bus.h"',
        '#include "revs_decl.h"',
        '#define REVS_MEM_ALIASES  /* enable bare lvalue aliases (lap_counter = ...) */',
        '#include "mem.h"   /* MEM_<name> offsets + bare aliases for named RAM/state */',
        '#include "../platform/platform_c.h"',
        '#include "../platform/probe.h"   /* PROBE_PHASE(): main-loop phase brackets */',
        '',
    ]
    body = []
    for f in funcs:
        ext_labels = external_labels_for_func.get(f['start'], set())
        body.extend(translate_func(f, funcs_by_start, symbols,
                                   external_entry_labels=ext_labels,
                                   external_entries=external_entries,
                                   wrapper_names=wrapper_names))

    # Stubs for JSR targets Ghidra didn't create functions for.
    if jsr_targets_unknown:
        body.append('/* === Stubs for JSR targets without a known function === */')
        body.append('/* TODO: investigate each — may be data misidentified as code. */')
        for addr in sorted(jsr_targets_unknown):
            name = symbols.get(addr, f'FUN_{addr:04x}')
            body.append(f'void {name}(void) {{ /* stub: no instructions found at ${addr:04X} */ }}')
        body.append('')

    # Emit split functions for mid-function entry points.
    # Each wrapper is the code from the entry address to the end of its
    # container function — a faithful slice, not an approximation.
    body.append('/* === Split functions for cross-function entry points === */')
    body.append('/* Each function starts at the labelled address inside its container. */')
    for addr in sorted(external_entries):
        container  = external_entries[addr]
        wname      = wrapper_names[addr]

        # Build an instruction index for the container.
        insns = container['insns']
        addr_to_idx = {insn['addr']: i for i, insn in enumerate(insns)}
        start_idx   = addr_to_idx.get(addr)

        if start_idx is None:
            # Address not found as an instruction start — emit a plain comment.
            body.append(f'void {wname}(void) {{')
            body.append(f'    /* entry ${addr:04X} not found in {container["name"]} */')
            body.append(f'}}')
            body.append('')
            continue

        # Build a synthetic function dict for the slice.
        sliced_insns = insns[start_idx:]
        sliced_func  = {
            'start': addr,
            'end':   container['end'],
            'name':  wname,
            'insns': sliced_insns,
        }

        # External entry labels within the slice (strictly after addr).
        slice_ext_labels = {
            a for a in external_labels_for_func.get(container['start'], set())
            if a > addr
        }

        body.extend(translate_func(sliced_func, funcs_by_start, symbols,
                                   external_entry_labels=slice_ext_labels,
                                   external_entries=external_entries,
                                   wrapper_names=wrapper_names))

    OUT_C.write_text('\n'.join(header + body) + '\n')
    print(f'Wrote {OUT_C}  ({len(header)+len(body)} lines)')

    # -----------------------------------------------------------------------
    # revs_validate_list.h — the fixture-or-fail list (docs/validation-harness.md).
    # Emitted even when empty: the harness includes it if present, and an empty list
    # must mean "no twins yet", never "the list is stale".
    # -----------------------------------------------------------------------
    val_names = []
    for f in funcs:
        if f['start'] in VALIDATE_FUNCS:
            val_names.append(f['name'])
    for addr, wname in sorted(wrapper_names.items()):
        if addr in VALIDATE_FUNCS:
            val_names.append(wname)
    missing = sorted(set(VALIDATE_FUNCS) - {f['start'] for f in funcs} - set(wrapper_names))
    if missing:
        raise SystemExit('VALIDATE_FUNCS lists addresses that are not a function start '
                         'or a mid-function entry in listing.txt: '
                         + ', '.join(f'${a:04X}' for a in missing))
    val_lines = [
        '#pragma once',
        '/* AUTO-GENERATED by tools/transpile.py from VALIDATE_FUNCS — do not edit.',
        '   Every name here has a native twin in src/gen/revs_native.c and a __t6502',
        '   transliteration oracle in src/gen/revs_gen.c.  tools/validate_native.c FAILS if',
        '   a name here has no fixture: a fixture-less PASS runs zero comparisons.',
        '   docs/validation-harness.md */',
        'static const char* const VALIDATE_NAMES[] = {',
    ]
    val_lines += [f'    "{n}",' for n in sorted(val_names)]
    val_lines += ['    0', '};', '']
    OUT_VAL.write_text('\n'.join(val_lines) + '\n')
    print(f'Wrote {OUT_VAL}  ({len(val_names)} validated names)')

    # -----------------------------------------------------------------------
    # Manual implementations stub — only when something actually needs one.
    # SMC_SITES handles all 24 self-modifying sites generically, so this file does not
    # exist by default.  Both Makefiles wildcard it, so its absence is fine.
    # -----------------------------------------------------------------------
    if MANUAL_FUNCS and not OUT_MAN.exists():
        manual = [
            '/* Hand-written implementations for routines the transliteration cannot',
            '   represent.  NOT auto-generated; edit freely.  One per MANUAL_FUNCS entry,',
            '   each with the evidence for why it cannot be transliterated.',
            '   docs/faithfulness-seam.md */',
            '#include "../cpu/cpu.h"',
            '#include "../cpu/bus.h"',
            '#include "revs_decl.h"',
            '#include <string.h>',
            '',
        ]
        for a in sorted(MANUAL_FUNCS):
            manual.append(f'/* TODO: {symbols.get(a, f"FUN_{a:04x}")} @ ${a:04X} */')
        OUT_MAN.write_text('\n'.join(manual) + '\n')
        print(f'Wrote {OUT_MAN}  (manual stubs)')
    elif OUT_MAN.exists():
        print(f'Skipped {OUT_MAN}  (already exists)')

    # -----------------------------------------------------------------------
    # Gen-time reports.  These exist so the things that bit the Atari port are visible
    # from the first generation instead of being discovered one runtime hang at a time.
    # -----------------------------------------------------------------------
    report_smc_coverage(funcs)
    report_brk_targets(funcs, symbols)
    report_spin_candidates(funcs, symbols)

# ---------------------------------------------------------------------------
# Reports
# ---------------------------------------------------------------------------
def report_smc_coverage(funcs):
    """Every SMC_SITES address must land on a decoded instruction start, and every
    routine that contains one is named.  A site that does not match an instruction is a
    hard failure: it means the listing moved under the table (a re-import, a different
    track's runtime image) and the emitted rasteriser would be quietly wrong."""
    starts = {}
    for f in funcs:
        for ins in f['insns']:
            starts[ins['addr']] = f
    stale = sorted(a for a in SMC_SITES if a not in starts)
    if stale:
        raise SystemExit('SMC_SITES addresses are not instruction starts in listing.txt: '
                         + ', '.join(f'${a:04X}' for a in stale)
                         + '\n  -> re-check against `make sweep` (docs/static-map.md).')
    by_kind = defaultdict(int)
    for site in SMC_SITES.values():
        by_kind[site['kind']] += 1
    owners = sorted({starts[a]['name'] for a in SMC_SITES})
    print(f'SMC: {len(SMC_SITES)} patched instructions emitted as runtime-dispatched '
          f'({", ".join(f"{k}={v}" for k, v in sorted(by_kind.items()))}) '
          f'in {len(owners)} routines: {", ".join(owners)}')

def report_brk_targets(funcs, symbols):
    """Report every routine whose whole body is a BRK, with its callers.

    A `JSR` to an address holding $00 is a call into memory that contains no code.  Revs
    has four such targets — $7B00, $7B4A, $7B9C, $7BE2 — reached from seven sites, and the
    page they live in is loaded by NOTHING: REVS2 covers $1200-$6FFF, a track file
    $70DB-$7814, and a real BBC has leftover front-end text there (docs/static-map.md
    §Open items).  Whether those calls are reachable is unresolved: the one measurement
    available (tools/bbc_probe_unmapped_calls.mjs) shows zero executions and zero writes
    to the page, but it also never gets past the front-end gate at $6560, which is exactly
    what stands between it and the call sites.

    Emitting them as empty functions would have made "calls into nothing" indistinguishable
    from "works".  They now trap through platform_brk(), so the first target run answers
    the question instead of hiding it."""
    lone = {}
    for f in funcs:
        if len(f['insns']) == 1 and f['insns'][0]['mnem'] == 'BRK':
            lone[f['start']] = f['name']
    if not lone:
        print('BRK targets: none')
        return
    callers = defaultdict(list)
    for f in funcs:
        for ins in f['insns']:
            if ins['mnem'] not in ('JSR', 'JMP'): continue
            m = re.match(r'^0x([0-9a-fA-F]+)$', (ins['op'] or '').strip())
            if m and int(m.group(1), 16) in lone:
                callers[int(m.group(1), 16)].append(ins['addr'])
    print(f'BRK-only targets: {len(lone)} routines that are a single $00 — calls into them '
          f'trap via platform_brk() rather than silently returning')
    for a, n in sorted(lone.items()):
        cs = ', '.join(f'${c:04X}' for c in sorted(callers.get(a, []))) or 'no callers'
        print(f'  ${a:04X} {n:<12} called from: {cs}')

def report_spin_candidates(funcs, symbols):
    """List the tight loops that are candidates for a SPINWAIT hook, instead of guessing
    at hooks up front.  A candidate is a backward branch whose whole loop body contains no
    JSR and at least one memory read — i.e. it can only exit when something ELSE changes
    memory, which under a single-threaded C port is nothing.  Reported, not hooked:
    Revs's 50 Hz body is a raster-timed User VIA T1 interrupt, so some of these are
    intra-frame waits where a platform_tick_vbi() would prevent the exit it is meant to
    enable (docs/transpiler.md §Spin-waits and hooks)."""
    cands = []
    for f in funcs:
        idx = {ins['addr']: i for i, ins in enumerate(f['insns'])}
        for i, ins in enumerate(f['insns']):
            if ins['mnem'] not in BRANCH_FLAGS: continue
            _, tgt, _ = parse_operand(ins['op'], len(ins['bytes']), symbols)
            if tgt not in idx or idx[tgt] > i: continue          # forward branch
            body = f['insns'][idx[tgt]:i + 1]
            if len(body) > 8: continue                            # not a tight spin
            if any(b['mnem'] in ('JSR', 'JMP') for b in body): continue
            reads = [b for b in body if b['mnem'] in
                     ('LDA', 'LDX', 'LDY', 'BIT', 'CMP', 'CPX', 'CPY', 'INC', 'DEC')]
            if not reads: continue
            # A loop that only touches registers is a delay, not a wait on state.
            if not any(re.search(r'0x[0-9a-f]+', b['op'] or '') for b in reads): continue
            cands.append((tgt, ins['addr'], f['name'], len(body),
                          ' ; '.join(f'{b["mnem"]} {b["op"]}'.strip() for b in body)))
    lo, hi = MAIN_LOOP_BRACKET
    for f in funcs:
        ids = [(i['addr'], i['op']) for i in f['insns']
               if lo <= i['addr'] <= hi and i['mnem'] == 'JSR']
        if not ids: continue
        print(f'main-loop phase brackets: {len(ids)} top-level calls in {f["name"]} '
              f'(${lo:04X}-${hi:04X}); phase 0 = outside the loop')
        for n, (a, op) in enumerate(ids, 1):
            tgt = int(op, 0) if op.startswith('0x') else 0
            print(f'  phase {n:2d}  ${a:04X}  -> {symbols.get(tgt, op)}')
    hooked = sorted(SPINWAIT_HOOKS)
    print(f'spin-wait candidates: {len(cands)} tight backward loops with no JSR; '
          f'{len(hooked)} HOOKED (' + ', '.join(f'${a:04X}' for a in hooked) + ')')
    for tgt, br, fname, n, txt in sorted(cands):
        mark = ' <-- HOOKED' if tgt in SPINWAIT_HOOKS else ''
        print(f'  ${tgt:04X}..${br:04X}  {fname:<28} {n} insn  {txt}{mark}')

if __name__ == '__main__':
    main()
