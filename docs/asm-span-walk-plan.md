# The span walk in 68000 assembly — plan for the next session (written 2026-09-23)

**Status: NOT STARTED.** HEAD `d0679b1`, frame **133.24 ms bracketed** (Σ(1..39) − ph28,
`PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000`, warp). This is a queue
item with a design attached: delete this file in the commit that lands the change, and move
what it taught into `docs/perf-method.md` / `docs/m68k-optimisation.md` (CLAUDE.md: docs are
queues, not logs). Read `docs/m68k-optimisation.md` before writing a line of the asm.

---

## 1. Why this, and what it is worth

`draw_road` (phase 11) is **32.34 ms** against the real BBC's 16.5. The `ROADSPLIT` stage split
puts **32 of a 38 ms pass in `draw_surface_spans`** (fill 3, mark 1). Single-stepping whole calls
on the target (`amiga/steptrace.gdb` + `tools/steptrace_report.py`) gave the real budget:

| per real `interp_edge_core` call | instructions | memory operands |
|---|---:|---:|
| DDA step loop (`span_walk_fast`'s i-loop) | 132 | 59 |
| plot body (`fast_plot`) | 46 | 30 |
| guard + entry (`span_walk_fast_ok`, decode) | 44 | 28 |
| per-span SETUP (clip 52, caps/pointers 49, patterns 30, deltas 19, publish 7) | ~157 | ~90 |
| **total** | **~550** (publish-only calls: 61-65) | **52%** |

×42 calls a frame is the whole ~32 ms. The BBC does the same call in ~180 instructions / ~590
cycles, because its zero page costs 3 cycles an access and the port's 1:1 `mem[]` mapping of it
costs 12-20. **The walk + plot + guard is ~222 instructions a call ≈ 14 ms a frame.**

Why assembly and not more C: GCC holds ~22 live values in 15 registers and spills 94-137 stack
operands whatever the C shape. Four C shapes were measured against the control (ph11 32.86):
inlined + locals + 16-bit `mem[]` index **−0.46 (landed)**; real pointers −0.26; out-of-line per
arm +0.63; that unrolled +0.39 (3014 instructions, 683 stack operands). GCC cannot express
"`subtrahend` lives in the upper word of `d2`", and `#pragma GCC unroll` does not help either
(docs/m68k-optimisation.md §HAND-UNROLL).

**Target: a non-plotting DDA step ~4 instructions (from ~17), a plot ~20-25, a span's walk+plot
~60-80 instructions (from ~222) ⇒ ~8-10 ms off phase 11 [ESTIMATE — price it with the phase
table, never with this arithmetic; CLAUDE.md §never size a prize as a residual].**

---

## 2. Scope — exactly what gets replaced

Replace **`span_walk_fast` + `fast_plot`** (src/gen/revs_native.c, "THE SPAN WALK IN REGISTERS")
on the **Amiga build only**, one asm routine per arm (`ARM_SHALLOW_FWD/REV`, `ARM_STEEP_FWD/REV`)
or one routine with the two direction bits as constants selected at entry.

**Stays in C, untouched:**
- `span_walk_fast_ok` (the guard) — it is validated on the host and decides which spans the asm
  may run. The asm is only ever entered for a span the guard proved safe.
- `span_walk_exact` (the aliasing-exact walk; the fixtures' planted cases, never the game).
- `span_entry_decode` and the `mem[arm->operand] = mem[arm->table + phase]` store — do them in
  the C wrapper and hand the asm `startCol`, `forced`, `runTop`.
- Everything after the walk that can call circuit code: the abandon path's
  `span_cap_line(y, cpu.S + 2)` and the rev arms' `span_walk_cap(y)` (both can reach a track hook
  through `span_cap_line_slot_z`), and the write-back of `plot_ptr*_v`, their `mem[]` hi lanes and
  `mem[SPAN_BLOCK]`. The asm RETURNS what those need; C does them.
- The host build keeps the C `span_walk_fast` — it is the reference the asm is checked against
  (§4), and `validate` / `determinism` keep gating the C.

## 3. The semantics the asm must reproduce (from `span_walk_fast`, line for line)

Per span (inputs, all loop-invariant unless noted):
`addend = mem[arm->addend]`, `subtrahend = mem[arm->subtrahend]` (⚠ shallow arms: addend =
SPAN_DY $84, subtrahend = SPAN_DX $83; steep arms the other way — the C locals are misleadingly
called `dx`/`dy`), `lineEnd = mem[$82]`, `block = mem[$85]` (varies), `bh = bearing_hi`,
`dest1`, `dest2` (the two patched operands; every writer makes them equal — keep two anyway),
`p1 = plot_ptr`, `p2 = plot_ptr2`, `p3 = plot_ptr3` (vary by ±$100 per line), `stepIn`,
`stepOut` ∈ {−1, 0, +1} (in practice one is 0), `markOn`, `startLine`, `startCol`, `forced`,
`runTop`, `acc = −subtrahend`.

- **Line loop** (`first` = 1 on entry): `startCol = first ? col : 0`; `force = first && forced`;
  `midAllowed = startCol < 4`; shallow only: `if (!first || runTop) colMark = $80`.
- **Column loop** `i = startCol..7`: `column = rev ? 3 − (i & 3) : (i & 3)`;
  `usePlot2 = rev ? (i < 4) : (i >= 4)`.
  - At `i == 4 && midAllowed`: shallow → the MARKER (rev: through `p1`, fwd: through `p2`); then
    `block += rev ? −1 : +1`.
  - **Shallow:** unless `force` (consume it: plot unconditionally), `acc += addend`; no carry ⇒
    next column; carry ⇒ `acc −= subtrahend`; then `colMark = column`; PLOT.
  - **Steep:** repeat { PLOT; `acc += addend` } until the add carries; then `acc −= subtrahend`.
- **After column 7:** shallow → the MARKER (rev: through `p2`, fwd: through `p1`); pointers
  `±= $100`, `block ±= 1`; stop when `p2`'s high byte == `arm->bound`; else next line, `first = 0`.
- **PLOT** (plotter 1: cell through `p2`, line copy through `p1`, `dest1`; plotter 2: cell through
  `p1`, line copy through `p3`, `dest2`):
  `y += stepIn`; **`y == lineEnd` ⇒ ABANDON** (return y; C writes back and caps);
  `dest[y] = block`; `cell = cellPtr[y]`;
  `cell == 0` ⇒ `a = colour_pattern_tbl[column]` ($628F);
  else `if (y < $2C && y <= dash_block_starts[block])` ⇒ `y += stepOut`, **no further stores**,
  next; else `a = (cell == $55 ? 0 : cell) & colour_pattern_and_tbl[column] ($337C) |
  colour_pattern_or_tbl[column] ($629C)`, `a == 0 ⇒ a = $55`;
  `cellPtr[y] = a`; `linePtr[y] = bh`; `y += stepOut`.
- **MARKER** (only if `markOn`): `if (colMark == $80 && (y >= $2C || y > dash_block_starts[block]))
  ptr[y] = $FF`; then `colMark = $80`.
- **⚠ Read `dash_block_starts` ($3900+block) and the three pattern tables LIVE from `mem[]` on
  every use** — they sit inside the pages the walk writes ($337C is in block 6's tail, $3900 in
  block $12's dead offsets), and a store CAN land on them. Everything else may live in registers:
  the guard proved no store reaches zero page, page $2F or the destinations' own bytes.

⭐ **THE DDA CARRY NEVER MATTERS ON THE FAST PATH [DERIVED — confirm with a WALKCHECK sabotage].**
In `span_walk_fast` the carry into every `acc += addend` is 0: a non-carrying add leaves 0; a
carrying one is followed by the subtract (borrow-free, because the carry was 1) and then a PLOT,
whose ordinary exit returns carry 0; the MARKER returns 0; and a line starts with
`carry = rev ? 0 : (p2hi >= bound)`, which is 0 because the guard only admits a start strictly
before the bound. The two places a non-zero carry-in exists (the fixture's above-bound entry, a
plot whose exit step trapped) are exactly the spans the guard sends to `span_walk_exact`.
⇒ **The DDA is `add.b addend,acc` / `bcc next` / `sub.b subtrahend,acc`** — no ADDX, no X-flag
bookkeeping, no carry register. (If the check disproves this, fall back to ADDX with X seeded by
`lsr.b #1` — NOT `ror`, which leaves X alone; docs/m68k-optimisation.md §ROR/ROL.)

## 4. Validation — the host cannot run 68000 code, so the gate is ON THE TARGET

1. **`make WALKCHECK=1` (new): an in-process differential on the Amiga**, same pattern as
   `FASTMEMCHECK` / `DIRTYCHECK`. Per span the guard admits: snapshot everything the walk can
   write — the destination range (`dest + 0..255`), the pointer pages the walk visits
   (`p* + 0..255` over its `n` lines), `plot_ptr*_v`, `mem[$70..$73,$85,$8E,$8F]`, `y`, the
   return code — run the C `span_walk_fast`, capture, restore, run the asm, compare. Count
   `g_walkCheckSpans` and `g_walkCheckMismatch` (+ first mismatch address/values) in `PROBE_SYMS`.
   **Gate: 0 mismatches over a driving run on all five circuits** (`STRAIGHT_TO_RACE=1
   HOLD_THROTTLE=1 TRACK=0..4`), and `g_walkCheckSpans` in the thousands on each. ⚠ State its
   SCOPE at the check: it shares the guard, so it cannot see a guard defect (CLAUDE.md §an
   in-process differential cannot see a defect in how a shared input is classified) — the host
   `validate` sabotages already gate the guard.
2. **Sabotage the asm under WALKCHECK, four minimum, each must fire** — the first-line test off by
   one, the marker's `$80` test dropped, plotter 2 given `p2`, the `$55` substitution dropped —
   plus one that tests the carry derivation (feed a carry-in of 1 into the first add of a line and
   show the C and asm agree only because the guard keeps it 0, or disprove the derivation).
   Rebuild clean every iteration (CLAUDE.md §a scripted sabotage loop must `rm` the objects).
3. **The picture:** `amiga/screen_dump.gdb` on a parked Silverstone and on Donington, compared
   against the C build's dump of the same frame (parked, so the trajectory cannot differ).
4. `muldiv-audit` / `probe-audit` clean; `make validate`, `determinism*` unchanged (they run the
   C, and must — a change to them means the C moved).

## 5. Interface and register plan (a starting point, not a contract)

**Call:** the C wrapper (Amiga) fills a small struct in a static block and calls
`span_walk_asm_<arm>(SpanAsmArgs *)` (`a0` = the struct, GCC's convention is the stack — use an
`__asm__` register variable or a naked stub). Returns in `d0`: `y` in the low byte, bit 8 =
abandoned; the struct gets back the final `p1/p2/p3` and `block`. C then does the write-back,
the abandon cap or `span_walk_cap(y)`. Save/restore only the registers used (`movem`) — a
10-register `movem` pair is ~180 cycles, ×42 spans = ~1 ms; count it.

| reg | holds | notes |
|---|---|---|
| `d0` | `acc` (byte) | the DDA accumulator |
| `d1` | `y` (low byte), `colMark` (upper word, `swap` for the marker) | |
| `d2` | `addend` (low byte) / `subtrahend` (upper word) | `swap` twice per carry, 8 cyc vs 12 for a stack reload |
| `d3` | `lineEnd` (low) / `bh` (upper) | `bh` needed once per plot |
| `d4` | `block` (low) / column counter (upper), or split | |
| `d5` | scratch: cell / pattern byte | |
| `d6` | scratch: `dash_block_starts` compare, offsets | |
| `d7` | `stepIn` / `stepOut` or specialise them away (§6) | |
| `a0` | `mem` base | patterns at `$628F(a0)`, `$629C(a0)`, `$337C(a0)`, `$3900(a0,block)` |
| `a1` | `p2` as `mem + p2` | cell (plotter 1), marker |
| `a2` | `p1` | cell (plotter 2), line copy (plotter 1), marker |
| `a3` | `p3` | line copy (plotter 2) |
| `a4` | `dest` (dest1 == dest2 in the game — but the guard does not check it; either check it in the guard and use one register, or keep `a5` for dest2) | |

⚠ `p1`/`p2`/`p3` step by $100 per line; the bound test is on `p2`'s page — precompute `n` (the
guard already has it) and count lines down with `dbra` instead of comparing a page byte.
⚠ `y` indexes as `(An,d1.w)` — keep the upper bits of the index clear (the C does `uint8_t` wraps:
`y` stays 0..255 because `(uint8_t)` — use `.b` arithmetic and a zero-extended copy, or keep the
upper byte of the word zero).

## 6. Traps, collected

- **Specialise the Y steps.** One of `stepIn`/`stepOut` is always 0 in the game (interp_edge sets
  in = 0, out = ±1, or moves out → in for two passes). Four variants per arm (in ∈ {0, ±1} ×
  out) is too many; keep them as registers, or generate the plot body for the two live shapes.
- **`ROAD_COUNT` / `PLOT_STORE_MARK` / `ROAD_PHASE`** are instrument hooks inside the C walk. The
  asm has none; build the asm path only when `REVS_ROADSPLIT`, `REVS_SRC_EVENTS*`, the shape probe
  and `VIEW_MARK_SOURCE` are off (an `#if` in the dispatcher), so an instrument build still counts.
- The PC sampler and `steptrace.gdb` map by `interp_edge_core`'s address (`OFF` line) — the asm
  routines need `.type`/`.size` so objdump/addr2line name them.
- ⚠ **"Inline asm for this target is unverified until it has RUN on the target"** (m68k doc,
  §ROR/ROL — the BCD selftest found 4500 failures in code that disassembled perfectly).
- Do not "fix" `span_walk_exact` or the C fast walk while doing this — they are the references.
- Price with the phase table against a same-session control (HEAD's C walk), `PROBEFIELDS=3000`,
  `frozen=` on both arms, phase 0 fields equal; confirm with `steptrace.gdb` that a span's walk is
  now ~60-80 instructions (the trace is also the fastest debugger for a wrong result).

## 7. Done means

WALKCHECK 0 mismatches on five circuits with the sabotages firing; the parked-picture compare
identical; ph11 down by a phase-table-measured amount; `span_walk_fast` (C) kept for the host;
docs/open-work.md's STEP 1 PROGRESS updated; this file deleted and its lessons moved.

---

## 8. WHERE TO CONTINUE AFTER THE ASSEMBLY CHANGE

Current rows (ms/frame, same protocol) against the real BBC (`make bbcprof`, docs/open-work.md's
per-phase table). **Step 1 = no row slower than the 6502.**

| phase | routine | port now | BBC | next move |
|---|---|---:|---:|---|
| 11 | `draw_road` | 32.34 (→ ~23 after the asm) | 16.5 | **(a)** below |
| 24+33 | `view_paint_lines` | 35.28 | 23.5 | **(b)** |
| 27+30 | `prepareFrame` (port-only) | 8.79 | — | **(c)** |
| 26+29 | the 50 Hz drain | 7.49 | ~1.5 | **(c)** |
| 32 | loop tail | 5.07 | 3.4 | **(d)** |
| 14+15 | sign + object | 4.62 | 2.9 | **(d)** |
| 3 | `read_driving_controls` | 1.97 | 1.0 | small |
| 5 | `build_track_geometry` | 18.83 | 21.7 | ✅ now FASTER than the BBC |
| 4 | `apply_driving_model` | 3.61 | 8.1 | ✅ |

**(a) `interp_edge_core`'s per-span SETUP — ~157 instructions a call, ~9 ms, the same disease.**
It writes and re-reads a zero-page working set every span: the clip history `SPAN_CLIP` ($88),
the endpoint `shared_temp_77`/`shared_temp_7e`, `span_line_cursor`, `SPAN_LINE_END/DX/DY/YSTEP/
ARM/BLOCK` ($82-$87), `math_lo/hi`, the cap codes, and it re-derives the eight colour-pattern bytes
($628F / $629C, 8 stores + 8 loads a span) from the style record even when the style has not
changed since the last span. The move: carry the CROSS-SPAN state (endpoint, line cursor, clip
history, current style) in locals of `draw_surface_spans_core`'s loop and hand `interp_edge` its
inputs as arguments; memoise the pattern tables on the style index; fold the setup into the asm
routine's entry once (a) and the walk share one register plan. **Gate first:** `make rangeaudit
RANGE=0077-0088,628F-62A0,337C-337F DEFUSE=1` on all five circuits (the expansion circuits' cap
hook reads registers and cells here — `span_cap_line_slot_z`), then the RESULTS rule for each cell
that stops being written (docs/validation-harness.md §THE RESULTS RULE / §def-use).

**(b) `view_paint_lines` (ph24 20.59 + ph33 14.69 against the BBC's 23.5).** Single-step it with
`steptrace.gdb` (change the break target) before touching it — the last three sessions' split
numbers for this routine came from brackets, and brackets have now been wrong twice in this
subsystem. Expect the same finding: per-line state in `mem[]` / `ViewState`, and the per-cell
source consume. Read docs/span-render-plan.md §11-§12 and CLAUDE.md's view-sweep rules first
(the dead ends there are measured).

**(c) The port-only rows: `prepareFrame` 8.8 and the drain's excess ~6.** `BODYSPLIT=1` and
`DECODESPLIT=1` exist; the drain is mostly the band cycle's per-tick cost (~442 µs a cycle) and
self-heals as the frame shrinks (docs/open-work.md) — measure it after (a)/(b), not before.

**(d) The small rows** — tail (`draw_dash_needles` + three sound calls) and sign/object: single-
step each once; each is worth ~1-2 ms.

**Then STEP 2 (docs/open-work.md): ~90 → 48 ms by deleting the `$3000` source-block intermediate**
— `draw_road` → source blocks → `view_paint_lines` exists only because of the BBC's screen layout;
with the car on its own playfield and no `mem[]` decode, a direct span-to-bitplane road renderer is
the architectural answer (the governing directive). Re-price it with numbers after step 1: every
denominator it depends on moves in (a)-(d).
