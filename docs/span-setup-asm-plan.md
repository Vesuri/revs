# The span SETUP in 68000 asm, fused with the walk — plan for the next session (written 2026-09-23)

**Status: NOT STARTED.** HEAD `a37002d`, frame **126.68 ms bracketed** (Σ(1..39) − ph28,
`PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000`, warp). ph11 `draw_road`
**26.55** against the real BBC's 16.5. It is a queue item with a design attached. Delete this file
in the commit that lands it, and move what it taught into `docs/perf-method.md`.
Read `docs/perf-method.md` §the span walk in 68000 asm and `docs/m68k-optimisation.md` §WRITING A
HAND-ASM ROUTINE first: this is the same method, applied one routine up.

## 1. Why, measured

`amiga/steptrace.gdb` over 24 `interp_edge_core` calls (trace `tmp/steptrace_locals.log` + ELF
`tmp/steptrace_locals.elf`, both local and uncommitted) gives ~430-470 instructions per real call.
8 of the 24 calls are publish-only, at 60-69 instructions each.

| part of one real call | instructions |
|---|---:|
| setup body: `interp_edge_core` lines, clip → endpoint → deltas → patterns → caps → pointers | **~246** |
| the asm walk (`span_walk_m68k`) | 79 |
| the C bridge: `span_asm_variant` + the register loads + unpacking the results | ~59 |
| `span_walk_direct` / `span_walk_fast_run`: entry byte, dest check, write-back | ~52 |
| `span_entry_decode` + `edge_x_word` | ~23 |

The BBC does the whole call in ~180 instructions / ~590 cycles. **The walk is now 79 and everything
around it is ~380 of port C**, most of it zero-page cells held as `mem[]` bytes. Three C passes on
this body have shown where C's floor is:
- the direct entry, −1.21 ms;
- the locals rewrite, −0.34;
- the four shapes of the walk before them.

Rule 1b says a body at per-cell edits is done, so the coarse lever is to take the setup into the
same asm routine as the walk.

**Target:** setup + entry + walk ≈ 150-180 instructions a real call, from ~450, with the register
file shared, no bridge and no pack/unpack. **~5-8 ms off ph11** [ESTIMATE, priced by the arithmetic
of ~280 instructions × ~28 real calls a frame × ~9 cycles. Price it with the phase table, never with
this: the last estimate was 2× off by its denominator].

## 2. Scope

An Amiga-only asm `span_setup_m68k`, entered from `draw_surface_spans_core`'s loop in place of
`interp_edge_core` (the C stays as the host's routine and as the reference). It does:
1. the clip bit;
2. the endpoint;
3. the swap;
4. the deltas;
5. the arm;
6. the step;
7. the patterns;
8. the cap codes;
9. the last-span clamp;
10. the block, page and pointers;
11. the marker and step-in decision;
12. the entry byte and its decode;
13. then **falls straight into the walk's jump table with the registers already loaded**.

C keeps everything that can reach circuit code (`span_cap_line`, `span_walk_cap`) and every path the
guard cannot prove. The asm returns a code, and C does the cap. The general dispatcher stays for a
destination out of `$0300..$0800` (fixtures only).

⭐ **The stores (the RESULTS rule, already audited).** `make rangeaudit
RANGE=0077-0088,628F-62A0,337C-337F DEFUSE=1` on all five circuits (2026-09-23, output in
`tmp/audit_setup_{1..5}.txt`) says:
- of everything `interp_edge` stores in `$77`, `$7E`, `$7F`, `$82`-`$88`, `$628F`-`$6292` and
  `$629C`-`$629F`, **the only reader outside `interp_edge` and the span arms and plotters is
  `plot_object` ($1FBB) reading `colour_pattern_tbl`**, 4 a frame, after the pass;
- `$88` (the clip history) and `$86` (the arm shift register) are SEEDED from outside, by
  `bearing_to_section_from` $2176/$2156 and `mark_line_surfaces` $1A98, and read on a pass's first
  span;
- the `$2C16 → $2C16` "read" is the 6502's dummy read on `STA abs,X`, not a use.

⇒ **Carry the cross-span state (`$88`, `$86`, `$7E`, `$7F`, `$77`, the `$82-$87` cells, the pattern
bytes) in registers or locals for a whole PASS, load it at pass entry, and store the final values
at pass exit**, plus before the two C escapes that read `mem[]`: the general walk dispatcher, and
`span_cap_line` for `span_swapped` and the cap codes. Frame-boundary memory is then byte-identical
to now, so **`make determinism*` needs no re-record and `validate` needs no `set_ignore`**: the
`interp_edge` fixture's single call is a pass of one span, loaded and flushed. ⚠ Re-check this at
the code. `math_lo`/`math_hi` are shared scratch too, so their final values must be flushed as
well.
⚠ `colour_pattern_keep_tbl` ($33FC) sits in a page the walk CAN write. Read it live every span,
never cache it. That is why a pattern memo was rejected this session.

## 3. Validation (the method that caught every defect last time)

1. **`make SETUPCHECK=1`** (new), the same pattern as `WALKCHECK`. Per span: snapshot
   - zero page `$70-$8F`,
   - the pattern tables,
   - `span_cap_*`, `span_swapped`, `math_lo`/`math_hi`,
   - the walk's pages and destination.

   Run C `interp_edge_core` (with the C walk), capture, restore, run the asm, and compare everything
   plus the returned indices. ⚠ The C version stores per span and the asm per pass, so compare at
   the END OF THE PASS, or have the check build flush per span. Choose one and state its SCOPE at
   the check.
2. **A target fuzzer before the first real span**, as WALKCHECK does: random style index, edge
   points, clip history, both arms, swaps, `dx == dy == 0`, `block >= $28`, the last-span clamp,
   every pass number. Silverstone alone never reaches steep spans or +1 steps.
3. **Six sabotages minimum**, rebuilding clean each time: the clip shift; the swap's line pair;
   `giveBack`; the arm XOR with `swapped`; the `$55` substitution; the step-in selection.
4. Five circuits driving, 0 mismatches. Host `validate` and all five `determinism` runs must be
   unchanged (they run the C, and must stay green).
5. Price: `SPANASM=0`-style switch (`SETUPASM=0` = the control) with `build=` bit 13. Use
   `PROBEFIELDS=3000`, `frozen=` on both arms, phase 0 fields equal. Runs are deterministic to the
   tick, so one per arm is enough. Confirm with `steptrace.gdb`.

## 4. Traps

- `edge_x_word(span_index_far)` reads the PREVIOUS far index. `draw_surface_spans_core` advances
  `span_index_far` only after the call, which is also why `farX` is `saved_slot_index`'s x.
- `interp_edge_publish` runs on every exit (5 of them). It must see the final endpoint values.
- The shallow step-in nudge moves `startLine`, and the walk's entry decode needs `mem[arm->table +
  phase]` written over `arm->operand`. That store is a real `mem[]` write and stays.
- The instrument hooks (`ROAD_COUNT`, `ROAD_PHASE`) are no-ops in the asm build, so compile the
  asm path out under the same `REVS_SPAN_ASM_ON` condition.
- Registers: the walk uses d0-d7/a0-a6. The setup's own working set is small (~12 values), and most
  of them ARE the walk's inputs. Plan it as one allocation, not two routines glued by a spill.

## 5. Done means

SETUPCHECK 0 mismatches on five circuits with the sabotages firing; ph11 down by a
phase-table-measured amount; the C kept for the host; docs/open-work.md's STEP 1 PROGRESS updated;
this file deleted and its lessons moved.

## 6. After it — the rest of step 1, in order (docs/open-work.md is the queue)

1. **`view_paint_lines`** (ph24 + ph33, ~35 ms against the BBC's 23.5). **Single-step it first**
   (`steptrace.gdb` with the break target changed), because brackets have been wrong twice in this
   subsystem. Read docs/span-render-plan.md §11-§12 and CLAUDE.md's view-sweep rules first.
2. **The port-only rows:** `prepareFrame` 8.8 and the drain's excess of ~6. Measure after 1, since
   the drain self-heals as the frame shrinks.
3. **The tail** (5.07 vs 3.4) **and sign/object** (4.62 vs 2.9): single-step each once.
4. **Then STEP 2**: the direct span-to-bitplane renderer that deletes the `$3000` intermediate.
   Re-price it after step 1.
