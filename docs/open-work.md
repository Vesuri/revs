# THE OPEN-WORK QUEUE — the one place to look for "what is next"

⭐⭐ **Start here.** Open work used to be reconstructed each session from three places at once
(`docs/phases.md`, `docs/static-map.md` §Open items, and a `TODO` sweep), which is how a settled
item stayed on the list for weeks and a stale marker kept sending readers at a rejected design.
This file is the queue. `make todo` prints it together with a live `TODO`/`FIXME` sweep of the
tracked, non-vendored tree, so a marker cannot hide in a file nobody opened.

⚠⚠ **THIS IS A QUEUE, NOT A LOG — exactly like `docs/rename.md`.** An entry is **DELETED** in the
same commit that closes it, and what the work *taught* goes in the doc that was wrong (usually
`docs/perf-method.md`). No DONE entries, no history, no celebration. The one exception is §CLOSED
below, which holds **one line per measured dead end** for the sole purpose of stopping someone
rebuilding it — if an entry there needs a paragraph, the paragraph lives in its own doc.

**Where the other sources of truth still rule:**

| Question | File |
|---|---|
| what gates what, and which phase we are in | `docs/phases.md` |
| what the BINARY is (unpacks, sweep, SMC surface, hardware) | `docs/static-map.md` |
| a name that contradicts behaviour, or does not exist | `docs/rename.md` (a queue; currently empty) |
| how to price any change honestly, and every past measurement | `docs/perf-method.md` |
| the standing RULES | `CLAUDE.md` |

---

## ⭐⭐ THE PERFORMANCE QUEUE, ranked

**Where the frame stands:** **192.64 ms bracketed** (Σ phases 1..39 = wall − phase 0), `PROBES=1
FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000`, warp, driving, at the domain-A
commit (`frozen=240410697`, `loopFrames=298`, `build=1d`, `probe-audit` 173 symbols). **Target is ~48 ms** (2× the original game; stretch **40 ms**, where the
`50/N` display ladder actually steps to 25 fps), and an entry worth under ~1 ms is not where the
answer is.

⭐⭐⭐ **AND THE REAL BBC RUNS THIS SAME SCENE AT 97.0 ms A FRAME — 10.31 fps, measured, `make
refloop` prints it.** So the port is **1.96× the original hardware**, not 5-10× off a reasonable
figure; the old 40 ms floor is **2.43× faster than Crammond ever ran it** and the old 20 ms
target **4.85×** — neither was ever set against this number, and 20 ms is now ⛔ off the table.
⇒ **the queue below cannot reach even the 48 ms target** — every remaining entry summed is a small fraction of the 150 ms gap, and a 68000's bus
cycle is 564 ns against the 6502's 500, so per byte touched it is *slower*; it wins only on
batching the BBC's 8-byte-apart cells and 128-byte-apart sources forbid. The queue is worth
working for what it is (a ~2× machine should not be a 2× *slower* port), but **20 ms needs the
layout change on both producer and consumer sides, not this list**. `docs/perf-method.md`
§what the original hardware achieves. ⚠ Size every candidate in **ms/frame** against its own phase row (Rule 1a);
the framerate is quantised to `50/N` and cannot see it.
⚠⚠ **Read the phase table out of `.run/gdb-out.log`, never out of `diag_run.sh`'s stdout** — that
is `tail`-truncated to the last 40 lines (`GDBTAIL`), which silently drops phases 1..5 AND the
`frozen=` gate line, and a total summed from it reads ~36 ms low. It has now cost two bad diffs.

| ms/frame | phase(s) | what |
|---:|---|---|
| 53.5 | 24+33+34+32 | `view_paint_lines` — the consumer |
| 34.4 | 11 | `draw_road` |
| 26.7 | 5 | `build_track_geometry` |
| 17.2 | 27 | `RevsScreen::decode()` — port overhead, no BBC counterpart (was 20.0; 34 rows are OWNED) |
| 14.1 | 28 | the vblank spin — the `50/N` pad, not a target |
| 12.5 | 26 | the 50 Hz drain |
| 10.2 | 18 | `fill_dash_edge_columns` — ⛔ see CLOSED |

### 1. ⛔ The TRANSPOSED SCAN — **10.00 ms, and BOTH routes to it are now CLOSED**
`docs/perf-method.md` §the transposed scan is at its floor, and §producer-emitted source events.
Its two halves are at the floor (**walk 5.30** + **recording 4.70**, objdump closes to ~10%), and
the representation change that deletes it was **built, proved correct and measured at +14 ms**:
the scan really does go (−10.59) and `draw_road` pays **+15.28** for the hook.
⭐⭐⭐ The same note costs 67 cycles inline in a loop we own and ~2700 inside `interp_edge_core` —
**40×** — because there it is a `jsr` in `draw_road`'s hot loops. Inlining it instead is the other
horn (+4.9 ms precedent). There is no third placement.
⇒ **the 10 ms is the price of not touching `draw_road`.** Reopen only if a producer is rewritten so
its note is inline in a loop it owns; the list machinery is proved (0 mismatch, 3584 sweeps) and
sits behind `SRCEVENTS=1`.

### 1b. ⭐⭐ The view sweep's DRIVER code — **phases 2+3 are DELETED by §12; this entry is history**
⛔⛔⛔ **EVERY CANDIDATE THIS ENTRY EVER NAMED IS NOW CLOSED, AND THE REASON TO STOP IS A
MEASURED CALIBRATION, NOT A LACK OF IDEAS: ON THIS DRIVER AN OBJDUMP DELTA OVER-READS THE BRACKET
BY ~6×.** The last edit deleted **seven memory-operand instructions from a tail taken 82 runs a
frame** — a static ~0.7 ms — and the phase table paid **0.118**. The three edits before it went
−0.733, −0.136, −0.118, i.e. the grain is exhausted: what is left in the body is 2-7 instruction
items (the three `addi.l #12416,d6` index rebuilds that miss the `mem + line` base already in
`a2`; the two `VIEW_SHORT_ENTER` page reads; the run set-up's two `lsl.l #7`), and they are worth
**~0.1 ms together**, not the ~0.3 the instruction count says.
⭐⭐⭐ **AND THE SPLIT IS THE TELL, NOT THE TOTAL: phase 2 took −0.111 of that 0.118 and phase 3,
which has HALF AGAIN AS MANY RUNS (50 against 32), took −0.008.** A per-run cost cannot do that.
⇒ **when a deletion's win does not scale with the count of the thing it deletes, the instruction
was not on the path the count describes** — and no amount of further objdump reading will say
which path it *was* on. Size the next one with an arm, or do not build it.
⇒ **What would actually win this 27 ms is not an instruction: it is FEWER CHAIN ENTRIES PER
LINE.** Phase 3 spends **~4880 cyc/line to paint 11.3 cells**, of which the unit loop is ~486
(43 cyc/unit, measured, irreducible) — so **~4400 cyc/line is driver serving four chain entries**
(two stops, two entries), each with its own set-up, tail, `view_compose` pair, unit lookup and
`g_viewStopList` search. That is a REPRESENTATION question and it belongs with §2/§3, not here.

⭐⭐ **−6.58 ms taken** (`214c8ae`, `docs/perf-method.md` §the entry was deleted): the runs are
**inline in both drivers**, `byte`/`line`/`cell` are in registers, `view_own_enter`'s poke-decode
round trip is gone, and phases 2+3 went **35.61 → 29.03 ms** with their census identical to the
unit (426/32/16 and 282/50/25).
⭐ **−0.733 ms more taken** (`422e68c`, `docs/perf-method.md` §the driver's cost is its memory
operands): **28.104 → 27.371 ms**, census identical again, from two objdump-read defects — the
unit loop was bound on `dp`, the pointer that DIES at the run's end, so the stop tail
reconstructed the `srcp` the loop already held; and `line = (line - 1) & 0xFF` cost a
materialised constant and a stack spill where `subq.b` does the job. Phase 3 is now
**4906 cyc/line for 5.6 painted cells**.

**Where that time IS — SETTLED FROM THE OBJDUMP, and it is that THE BODY IS LONG** (floor =
cheapest plant/trap-free path from loop head to back edge; ceiling = every non-cold instruction
once; `docs/perf-method.md` §where phases 2/3's 29 ms is, settled from the objdump):

| | floor | **measured cyc/line** | ceiling | non-cold instrs | cells/line |
|---|---:|---:|---:|---:|---|
| phase 2 (16 lines, 10.08 ms) | — (all routes touch a plant block) | **4502** | 4374 + unit turns | 365 | 26.6 |
| phase 3 (25 lines, 17.30 ms) | 2154 | **5348** | 6026 | 499 | 11.3 |

⭐⭐⭐ **AND THE BODY COSTS WHAT ITS INSTRUCTIONS COST — THE LEVER IS PER-RUN OVERHEAD, 2.7× THE
WORK IT DRIVES.** Summing all 71 blocks: **286 instructions/line, 90 of them the two unit loops ⇒
196 driver instructions, 63 of which carry a memory operand** (~18 cyc against 4-8 for a register
op) ⇒ ~2065 cyc nominal, ×1.3 for DMA ≈ 2685, which closes against the bracket once the probe
(~13%) comes off. ⛔ **So there is no "2× slack" to find, and my own ~9 ms estimate of it is
retracted** — it differenced a modelled operation count against a measured wall-clock bracket,
the error `docs/perf-method.md` records three times. Of the 196: two run set-ups ~32, two stop
tails ~30, two ENTER decodes + two stop-list walks ~45 = **~107 instructions of per-run overhead
against ~39 instructions of real unit work** in phase 3's 5.6-cell runs. ⇒ **rank the remaining
edits by instructions deleted per line** (one deleted instruction ≈ 0.065 ms/frame if phase 3
only, 0.109 ms if it hits both phases).

⭐ **−0.136 ms more taken** (`b6d5374`): **27.371 → 27.230 ms**, census identical again. The
run set-up was spilling the destination pointer purely from register pressure (the function opens
`movem.l d2-d7/a2-a6` — all eleven usable registers), and the cause was not the arithmetic but
**two REDUNDANT REPRESENTATIONS of values the line body already keeps live**: `srcLine` was a
third spelling of `line` (which is live for `line_is_last`, and `mem + line` is already CSE'd into
an address register for the per-line table reads) and `dstLine` a second of `plot_ptr_v` (live
because the chain boundary store composes its address). Deleting both hoists and forming each run
pointer at the point of use took `44(sp)` traffic 8 → 2, all `n(sp)` **21 → 12**, and the function
**571 → 569 instructions**. ⭐ **DELETE A REDUNDANT REPRESENTATION BEFORE FIGHTING THE SPILL IT
CAUSES.** ⚠ Sized at ~0.2-0.5 ms and paid 0.136 — the give-back is real and named below.

⛔ **Two follow-ons to that are CLOSED, both from the objdump and neither needing an emulator
run.** (a) *Hoist `srcLine` only* — the asymmetric version, on the reasoning that `(d8,An,Dn.l)`
has an **eight-bit** displacement so `srcp`'s `$3000` cannot ride the address and must be re-formed
as `adda.l #mem+$3000` a run, while `dp`'s base carries no constant at all and is free. It is a
true asymmetry and it does not help: the spill simply **moves** from `dstLine` to `srcLine`
(`n(sp)` straight back to 21, `44(sp)` to 8, plus a per-cell reload). ⇒ **there is no room for ONE
hoisted pointer either — the allocator is saturated, so "hoist the base that absorbs a
non-displaceable constant" is a real rule with no seat at this table.** (b) *Spelling the base as
`(mem + MEM_view_src_blocks) + (line + …)`* to make GCC keep `mem+$3000` in an address register —
**byte-identical output**, GCC reassociates through the parentheses.

⚠⚠ **And the retraction that goes with them: "the edit grew the function 571 → 645 instructions"
was an ARTEFACT OF DIFFING TWO BUILD CONFIGURATIONS** — the "after" dump was from a `PROBES=1`
build (it carries a `jsr probe_phase` the other has not) and the "before" from a plain one. Built
the same way the edit is 2 instructions *smaller*. ⇒ **an objdump is a measurement and takes the
same rule as a phase table: same flags on both arms, and check the artefact's own fingerprint
(here: the load address moved `0x11ee0` → `0x1293e`) before diffing.**

⛔ **AND THE THREE "smaller follow-ons" THIS ENTRY LISTED ARE ALL CLOSED, none of them needing an
emulator run.** (a) *The two duplicate table reads (~0.25 ms)* — **both are load-bearing**, and the
rule that settles it is worth more than the item was:
⭐⭐⭐ **SOURCE-BLOCK ALIASING DECIDES WHETHER A REPEATED TABLE READ IS REDUNDANT.** The sweep's
source bytes live at `$3000 + cell*$80 + line`, so a per-line table at `T + line` is aliased by a
current-line `view_consume` **iff `cell*$80 == T`** — and `MEM_view_run_right_end` is `$3080`,
which **IS cell 1's source byte**, so the consume can zero it between the two reads.
(`MEM_view_edge_phase` at `$3050` is never aliased, and that is the *other* read.) ⭐ The second
reason is independent and kills the `edge` pair on its own: **`VIEW_SHORT_RUN`'s cold arm can
advance `line`** — `stop_ >= 40` calls `view_own_run`, whose own comment says "the cold run may
have advanced the line and the plot pointer", and a host counter puts it at **333 of 4958 run
entries (6.7%)** ⇒ a per-line value re-read after a run is a real dependency, not a duplicate.
(b) *`step_scanline`'s byte-lane dance* — ⛔ **the ~1.4 ms sizing is RETRACTED and the real ceiling
is ~0.35 ms**: the objdump's common path is **9 instructions ≈ 98 cyc/line**, and the byte-lane
carry arm the wide-value rewrite would delete runs **1 line in 8**. The ~230 cyc/line in the table
below is the bracket including that arm's amortised share, not what a rewrite can collect.
(c) *The `view_stop_from` byte compare (~0.19 ms)* — already **4 instructions (~24 cyc) on the
common path** with its base hoisted to `a6`; there is nothing left in it.

⇒ **Phase 3 runs ~89% of its non-cold body on every line. There is no hotspot and no marshalling
layer to delete** — the body serves **four chain entries a line** (two stops, two entries), each
with its own run set-up, stop tail, `view_compose` pair, unit lookup and `g_viewStopList` search,
for an average run of **5.6 cells**. `NOUNITS=2`'s 80%-driver figure confirms it independently.

⭐⭐⭐ **AND THE PAINTER IS A WASH — RANK OWNERSHIP BY ROWS, NOT BY PHASE COST (§11, 2026-09-17).**
Three arms of one field-bounded session settled it: phase 1's takeover moves **phase 1 by −0.03 ms**
(15.53 → 15.50, forty `mem[]` bytes + forty units + a 905 cyc/line driver replaced by two bitplane
writes a cell) and **the frame by −5.62, all of it `ph27`** (28.74 → 24.30 for 36 of 208 rows =
−0.123 ms/row, which reproduces `VIEWCARVE`'s −0.124 to 1%). ⇒ **A direct-to-bitplane painter is
worth `rows owned × 0.123 ms` and nothing else** (0.08–0.12 depending on the block — every one is
now priced in §11's ledger, and ⚠ **a row price does NOT scale with the decode's total**: the
−4.20 ms shape pass at `151e282` re-priced the dashboard only −4.40 → −4.11, because it deleted
the per-**row** driver while ownership collects the per-**cell** scan). The predicted −6.6 to −7.8 ms on phase 24 is
⛔ **RETRACTED**: it priced a driver deletion that the measurement says is not there to collect.
⚠⚠ In no phase does the painter delete the **source walk** — `view_consume`'s RLE must read every
cell's source byte whatever the destination is. **The prize is the DECODE**, but ⛔ **"the end
state is `ph27` → 0 = 28.74 ms" is RETRACTED as well** (§11a): `DECODESPLIT` attributed the whole
row, and ownership deletes only `convertRace`'s per-row conversion — **−11.18 ms from here, in
step 1 of two**. The rest goes with the CALL (hence `NODECODE` = 0.13 ms), and of it
`snapshotBands` + `buildLineModes` (~2.4 ms) must keep running forever, because `m_plan` is the
COPPER's palette schedule rather than decode work. **End state `ph27` → ~2.4 ms, prize ~17.6 ms.**
The 208-row ledger with every block now priced, the two invariants (the measured reader gate;
per-display-line ownership) are §11's; ⛔ **the "phase 2 at ~1.5 ms instead of
10.16" estimate is RETRACTED** too — it assumed the span deleted the source walk.
⛔⛔⛔ **AND THAT −11.18 ms OF ROWS IS ITSELF CLOSED NOW: 70 ROWS ARE OWNED** (phase 1's 81..116
plus domain A's 0..17 + 192..207), **63 SKY ROWS WERE ALREADY FREE, AND THE REMAINING 75 ARE PRICED
OUT** — the whole remaining ownership campaign is **−3.61 ms best case against a measured
+3.45 ms** for the only placement ever built (see CLOSED). ⇒ **this entry's 27.23 ms has to be won
INSIDE the driver; it cannot be won by taking the decode's rows away from it.**
✅ **Step 0 is DONE: the new pipeline is the DEFAULT build** — `VIEWOWN=0` / `SPANFILL=0` are now
the A/B controls, so the shipping frame goes **196.59 → 182.62 ms (−13.97)**. `validate` PASS, all
five `determinism` trajectories PASS.

⛔ **And WHOLE-LINE ownership of these two phases is closed on arithmetic — they stay per-RUN:**
41 lines × 0.36 ms of painting = 14.8 ms against 4.9 ms of units + 5.07 ms of decode = **+4.8 ms
net**. That −5.07 ms is also the decode's whole ceiling for these lines (`make VIEWCARVE=1`, ~2.0
scan / ~3.0 expand — and a per-run takeover reaches only the expand half).
⚠⚠ **The correctness obstacle, if any of these lines is ever owned, is MIXED CELLS:**
`m_lineMode[y] = 0` is per-line-ALL-40-CELLS, so a cell owned on some of its 8 display lines and
written through `mem[]` on the others repaints from a stale byte. Priced: per-cell RMW **1.6 ms
⛔**; per-row prefix-XOR delta **≈0.9 ms**; per-changed-cell 8-line range test **≈1.1 ms**.
⚠ **Two decompositions of the same 52.7 ms sweep are in circulation and they are NOT the same
axis** — `NOUNITS=2` differencing says 32.4 ms driver/entry + 20.4 ms unit loop; §7j's calibrated
bracket says 29.0 ms unit/run *interior*, of which ~8 ms is destination stores and ~21 ms is
source/translation/control. Never subtract one from the other.

⭐ **Four candidate causes of the per-line cost were checked and ALL are too small:**

| candidate | measured | verdict |
|---|---|---|
| the plants (`view_move_stop`/`view_plant`) | **25 plants a sweep, ~1 ms all phases** | ⛔ not a step — a host census, not the split's 888 cyc/line |
| the census instrument (`PROBE_VIEW_*`) | ~680 cyc/line ≈ 13% | instrument, and it is inside every figure here |
| the four spilled invariants | 15 body touches/line ≈ 240 cyc | ⛔ the `column_gap_walk_core` trap — both runs are on pads past the back-edge |
| phase 2's cold `view_own_run` arm | **16 of 32 runs, one per line** (host census) | ⛔ ~150 cyc/line: a `ViewState` marshal + a call, not a fallback path |
| the per-line table reads | `lea (0,a3,d7.l),a5` once, then 12-cyc `d16(a5)` | ⛔ already optimal, nothing to hoist |
| `step_scanline`'s byte-lane dance | ~230 cyc/line (phase 3 ~280, carry tail) | ⛔ **~0.35 ms, not the ~1.4 ms once claimed** — 9 instructions on the common path, the carry arm is 1-in-8 (above) |

⚠⚠ **`copy_dash_data_core(0x80)` at `race_main_loop`'s exit stows the whole $7B00 page back into
the $3000 block tails**, so the opcode slots and the three `VIEW_REC_*` operands are read,
persisted and re-assembled into the NEXT race's page. They are cross-RACE state — no RESULTS-RULE
exemption can treat them as twin-private.
⚠⚠ **Keep `view_stop_from` as the stop authority and do NOT derive the stop from the tables.**
The planted stop is a **state machine over `mem[rec]`**: `view_move_stop` returns early when
`stop_unchanged(stop, mem[rec])` and `unplant_stops` restores `STA` at the end of phase 3, so on a
phase's FIRST line a table byte equal to the stale record plants nothing and the run legitimately
runs to unit 39.
⚠⚠ **And the geometry tables still cannot be precomputed** — `view_run_right_end` ($3080) collides
with column 1's source block at exactly one byte ($309B, phase 3's topmost line), so every table
read must happen where the 6502 did it.
⚠ Phase 2 also carries **one byte of SMC state across its lines on purpose**: the poke of chain B's
entry sits inside the `stop_unchanged` test, so that entry must be read from `mem[]`, not a local.

⚠⚠ **The unit loop itself is CLOSED** — 43 cyc/unit is what a byte load, a zero test and a byte
store cost on a 68000, and widening is impossible (destination cells 8 bytes apart, sources 128).
⚠⚠ Any edit to `paint_cells` must pass **the counting test**: grep the objdump for each loop
invariant's absolute address and require the count to stay at 1, or the 4× unroll is gone.

### 2. ⭐⭐ The per-span REPRESENTATION — ~11.5 ms of `draw_road`'s 33.7
`docs/perf-method.md` §per-span SETUP (45): **~24 drawn spans × ~3 400 cycles to plot 2.5 columns
each** — a host census of the four exits (12 803 spans, no emulator run) shows both non-walking
exits leave after step 2, so the setup is NOT paid 43 times. ⛔ It also kills the coarse lever that
reading suggests: the `block >= 0x28` off-side test sits at the END of the setup though `block` is
known right after step 2, and it fires **0 times in 12 803 spans**. The remaining phase-11 lever
now that the kernel's shape and its opcode slots are done.
⛔ **And it kills a second reading of the same dump: `plot_store_resync` (the page-$00 alias guard)
is 211 inlined instructions plus 124 at its dispatch line — the single biggest inlined body inside
`interp_edge_core` after the walk itself — and it is worth AT MOST 0.9 ms.** The published leaf
census is the whole argument and it needed no run: **60 plotted columns a frame**
(`docs/perf-method.md` §WHY), ≤3 guard calls per column ⇒ ≤180 calls, and the guard's fast path is
six instructions (GCC folds the `addr >= 0x100` test into the switch's own `addr - $70 <= 31` range
check, so it is one `andi.l` + a five-instruction bounded jump-table test, ~35 cyc). 180 × 35 =
6 300 cyc = **0.9 ms**, and most of that is the mask and the range test the oracle genuinely needs.
⇒ Rule 1b, third instance: **the instruction count measured the nine INLINE COPIES, not the path.** **Needs SPAN_DX/DY/BLOCK moved out of
the 6502 address space**, which changes the measured self-overwriting-DDA behaviour ⇒ a **written
RESULTS-rule reader audit** (`docs/validation-harness.md` §THE RESULTS RULE), a scoped
`set_ignore`, and `make viewdiff`. Sibling, same subsystem: **the per-walk entry/exit, ~10.5 ms
for 24 walks**.

### 2b. ✅ THE SOURCE-BLOCK READER AUDIT IS DONE — `docs/span-render-plan.md` §12b, `make srcaudit`
The RESULTS-rule gate on "the producers stop maintaining the source blocks in `mem[]`" is
**written and measured on all five circuits**: no reader outside the view pipeline touches a live
source byte during a race. What it changed about the plan, and none of it was in the guess it
replaced:
- ⚠⚠ **13% of the reads are the producers reading their OWN byte back** to compose two shape edges
  landing in the same cell. "The producer already knows every byte it writes" is true per STORE and
  false per CELL, so the replacement representation must carry the composition. **This is the real
  constraint on the item and it is a design question, not an audit one.**
- `copy_dash_data` reads the **live span**, not the tails as CLAUDE.md says — but it brackets the
  race (assemble in, stow out) and never interleaves, so it does not block.
- Two producers the practice-session window cannot reach go with the rest: `draw_starting_lights`
  (`$42C0`) and `paint_fence_backdrop`.
- The gate is `make viewdiff` per circuit plus a scoped `set_ignore` and a `determinism` re-record.

⛔⛔⛔ **AND THE ROUTE IT UNBLOCKS IS NOW CLOSED ON ARITHMETIC — the ceiling is −2.16 ms.**
`docs/perf-method.md` §producer-emitted source events are closed for good. The scan's 10.00 ms is
**5.30 walk + 4.70 record**, and only the walk is deletable: the record is 161 event appends that
have to happen wherever the events come from, and the producer route's ordered insert is strictly
MORE work than the scan's in-order append. Against a **measured** +3.89 ms call barrier in
`interp_edge_core`'s loops, best case is −5.30 − 0.75 + 3.89 = **−2.16 ms** — for a new
representation that must preserve class B's read-modify-write composition, plus a scoped
`set_ignore`, a `determinism` re-record and per-circuit `viewdiff` gating. **Do not re-open
without a new number.** Two sub-ideas died with it: bounding the scan by `dash_block_starts` is
worth exactly ZERO (`s_lowConsume[cell] == dash_block_starts[cell] + 1` on all forty cells — the
scan is already at its floor), and the `SRCEVNULL` split is confounded by IPA.

⇒ **The AUDIT keeps its value** (it is the permanent gate, it corrected CLAUDE.md on
`copy_dash_data`, and it is the worked example the RESULTS rule now points at); the route does not.

### 2a. ⭐⭐⭐ ROWS 117..157 — THE BIGGER OF THE DECODE'S TWO REMAINING PRIZES, ~7.2 ms
⚠⚠ **The 2026-09-20 "re-pricing" that used to head this entry is RETRACTED** — both arms of the
§12c A/B owned display lines 158..191 (a macro-name collision; `docs/span-render-plan.md` §12c).
Re-measured with the collision fixed, `decode()` is **17.55 ms** and `convertRace` **10.13 ms at
80 cells a frame**, which splits ~**7.2 ms over 117..157** and ~**2.9 ms over 158..191** — so
`ch/f` does not rank an ownership domain and **both** blocks are worth taking (entry 2d).
⇒ **Own these 41 rows and the decode's conversion very nearly goes to zero.** They are blocked on
exactly what §12 says: the car, the tyres and the dash sides are furniture that lives in `mem[]`
and reaches the screen through the decode, so `TERRAINLOW` deliberately writes `mem[]` and claims
nothing. The furniture is STATIC (measured, `amiga/car_probe.gdb`) ⇒ a one-time base into both
plane buffers, plus a painter for the three things on these rows that move: the wheel dither
(`tick_wheel_spin`, 32 bytes), the steering-wheel mark and the needle column's upper half.
⚠⚠ **And the painter must beat 143 cyc/byte**, which is what §12c's rectangle re-expand measured.
The decode's own 13.6 cyc/byte is a WHOLESALE longword-batched rate and does not transfer to a
small region. Getting the terrain painter to write the planes directly (as `TERRAIN=1` already
does for 81..116) is the shape that has the rate; a strip re-expand is not.
⭐ ...and §12d shows the third shape, which has no rate at all: the writer hands the renderer its
GEOMETRY. The two dash needles are a 36-entry pixel list plus a rectangle copy now, with no
`mem[]` traffic, no undo list and no re-expand — `make NEEDLE=1`.

### 2d. ⭐⭐ ROWS 158..191 — THE DASHBOARD, ~2.9 ms, AND TWO OF ITS FOUR WRITERS ARE ALREADY DONE
Re-opened by the §12c retraction above: these 34 rows really are worth ~0.086 ms each. What has
to stop writing `mem[]` there before they can be claimed:
- ✅ the two dash NEEDLES — `make NEEDLE=1` (§12d): a pixel list painted into the planes, erased
  by a 32-pixel-granular rectangle copy out of a cached clean cockpit. Five gates green.
- ✅ the front-wheel DITHER — `make TYRESPRITE=1`: two precomputed sprites (but its rows are
  133..140, i.e. entry 2a's block, not this one).
- ☐ the two WING MIRRORS (display lines 154..178, cells 0..2 and 37..39, 118 bytes, six segments
  from the game's own `mirror_seg_*` tables). ⚠ Invisible in every practice measurement — an
  empty track reflects nothing — so its gate is the tables, not a run.
- ☐ the GEAR indicator, and whatever else `DASHBARE=1`'s enumerator reports once the needles are
  out of the way. Re-run it: the enumeration on record was taken with these rows wrongly owned.

### 2a-old. ROWS 117..157 under the pre-§12c model — only 17 of the 41 have a single writer
`make fbwrites FILL=117-157 FILLFRAMES=15-70` gives the per-line writer set: **117..128 (12) and
141..145 (5) have the view sweep as their ONLY writer**; 129..132 and 146..157 add
`undraw_plot_lines` + `plot_line_octant`, and 133..140 add `tick_wheel_spin` (the tyres). Those 24
rows OR their pixels into `mem[]`, so owning them means they never reach the screen ⇒ **blocked on
the car becoming sprites/a playfield** (§12's commitments). The 17 ownable rows price at −2.1 ms of
decode against ~0.6 ms of interval-fill painting = **~−1.5 ms**, in two fragmented blocks needing
base-plus-delta and a staleness oracle. ⇒ **Deliberately NOT built** (user decision, 2026-09-20):
entry 3 makes the terrain for every view-sweep-only row bypass `mem[]` anyway, so these rows fall
out of it and building them first is work entry 3 discards.

### 2c. ⚠⚠⚠ THE OBJECT PLOTTER — ~9 ms of a REAL race, and every baseline hides 5 ms of it
`docs/perf-method.md` §the object plotter. The road sign (phases 14+15, 5.15 ms) is at its local
optimum: the chain `plot_view_src_line` → `column_gap_walk` → `fill_edge_column_run` has zero
frame operands, no `pea`, no absolute reads in a loop — the dash-edge campaign already took it
there — so **~1.3 ms is all a large rewrite would buy for the sign alone**. `scale_shape_vectors`
is priced exactly at 0.686 ms (`make SIGNDOUBLE=1`, a new idempotent doubling arm): 765 cyc/vertex
over 6.37 vertices, with 3.63 edges at ~4 970 cyc each.

⚠⚠ **BUT `move_and_draw_cars` (phase 17) reads 0.21 ms in every measurement ever taken because
`STRAIGHT_TO_RACE` IS A PRACTICE SESSION AND THE PLAYER IS ALONE ON TRACK** — that 1 500 cycles is
22 empty-slot tests and nothing else. A host census of the race proper (gated to frame ≥ 12000;
ungated it is 42% low, being almost all qualifying) draws **2.32 objects a frame against practice's
0.89**. At 3.63 ms per drawn object that is **~8.4 ms in a race**, phase 17 becoming ~5.2 ms, so
**the 172 ms baseline understates a real race by ~5 ms.**

⇒ **NEXT STEP IS A MEASUREMENT, NOT A REWRITE:** a `PROBES=1 RACEPROPER=1` target run that reaches
the grid, to replace the estimate (target per-object cost × host object count) with a phase table.
Until then the object plotter is sized, not measured.

### 3. ⭐⭐⭐ FEWER POINTS / SPANS — the producers, 61 ms, and the title is now the whole plan
`docs/perf-method.md` §the producers mapped. **MEASURED 2026-09-20 and it redirects this entry:**
61 ms turns **27 edge points** into **42 spans** and **58 plotted columns** (89 transforms, 231 DDA
steps), and there is no bad kernel left — `div16by8` is already off the game path (a real `DIVU.W`
in both transforms), the transforms are native wide-value C, and **every hot body has ZERO frame
operands**. The residue is absolute `mem[]` operands at 10-21% of instructions, ~2x a plausible
instruction-count floor, and ⛔ **a value pipeline is blocked by ZERO-PAGE TENANCY** — 
`make rangeaudit RANGE=0080-0088` shows 23 readers of the delta vector because `$0084`/`$0085` are
also `shared_temp_84/85`; the per-offset column says offsets 0/1/6/7 are candidates and 2/3/4/5 are
not. ⇒ **the 61 ms must become LESS WORK, not cheaper work: fewer than 27 edge points and fewer
than 42 spans.** That is a visual-fidelity trade (`viewdiff` fails by construction) and needs the
user's decision on how much horizon/far detail may go.
`docs/perf-method.md` §What is left. Setup and loops fused into native value pipelines; the
interpreter is already gone from the whole tree, so nothing is left to delete there. Named
sub-levers: producer-emitted source dirty events/runs (12-18 ms), a native geometry `EdgePoint`
value pipeline (8-12 ms), dash specialisation (5-10 ms).

### 4. ⭐ HARDWARE SPRITES for the instruments — gated behind the §10 renderer
`docs/span-render-plan.md` §10m. ⚠ Its old size figure was against the wrong routine (see §10a); re-size with `make fbwrites` before scheduling. The BBC had no sprites so every moving dashboard item is
CPU-drawn; the Amiga has eight idle. The open constraint is **width** (8 × 16 px = 128 of 320 in
one 42-line band, where vertical reuse buys nothing). Pre-render each variant by running the
game's own drawing code, so the images derive from the oracle.
⭐ **The row-ownership closure below gives this entry a second prize it did not have:** sprites take
the needles' CPU stores off display rows 158..191 entirely, and a block's ownership COST is exactly
its writers' store rate (§11e ranks by `st/row`), so sprites are the one thing that could reopen
those rows' −2.51 ms of decode.

### 5. ⭐ The VERTB ISR — ~720 µs/field left, ≈3.6% of WALL CLOCK
`docs/perf-method.md` §The VERTB ISR. Invisible to every phase row, and permanent: 50 fires a
second whatever the framerate does. `snd_tick` ×2 (~276 µs) is the largest remaining row;
everything else is at or near the 92 µs instrument floor. ⚠ **Do not merge the two ticks into one
pass** — `make sound` compares chip state tick by tick against a real MOS and the intermediate
state is part of the contract.

### 7. ⭐ THE TWENTY NEVER-PROFILED SMALL PHASE ROWS — ~16 ms, plus phases 3/4/15 at 11.8
Nobody has looked inside these, and the phase table's integer `ms/frame` column rounds most of them
to `0`, which is why they stayed invisible — **compute them from `ticks / frames / (frozen/3000/20)`
instead**. What the first pass through them found (2026-09-20):

- ✅ **The block ops were byte loops** — `fastmem.c`, −1.05 ms across phases 10 and 24. CLOSED.
- ⚠ **Phase 34 is a ONE-SHOT, not a per-frame row: `calls=1`.** `view_low_build` runs once and its
  2.7 ms is that single run amortised over the window's 333 frames, so **the real recurring frame is
  ~169 ms, not 172** — and a longer run reports a smaller number for the same binary. Any A/B that
  straddles it is comparing two different amortisations. Check `calls=` on every row before diffing.
- ⭐ **Phase 3 (`read_driving_controls`, 5.11 ms) is ~7 MOS calls a frame, priced at ~1450 cycles
  each ≈ 2.1-2.5 ms.** Host census (`make`, a counter in `Platform::mosCall`): **2097 OSBYTE 129 +
  839 OSWORD 7 over 300 frames = 7.0 key tests and 2.8 sound calls a frame.** One key test is a
  `MosRegs` built on the stack → `jsr platform_mos_call_typed` → **virtual** `Platform::mosCall`
  (468 instructions) → `switch(0xFFF4)` → `osbyte()` → `switch(0x81)` → **virtual**
  `platform->keyDown()` → a **33-entry linear scan** (~760 cycles), with the struct copied by value
  at three frames. The BBC paid an OS call because it had to scan a keyboard matrix; this port is
  asking a byte array the input ISR already maintains. **Two independent fixes, both Amiga-local and
  observably identical: a 256-byte BBC-code→rawkey reverse map inside `RevsInput`, and a direct
  `platform_key_down()` entry that skips the OSBYTE dispatch.** Not yet built.
- The rest, unexamined: phase 4 `apply_driving_model` 3.60 (real 6502 arithmetic, the BBC paid it
  too), **phases 14+15 `build_road_sign` + `draw_track_object` 5.15 ms for ONE billboard** — the
  next thing to read here — phase 23 `check_crash` 0.50, phases 9/12/20 `engine_sound_update` 1.79.

⚠⚠ **THE CEILING IS HONEST AND SMALL: this whole block is ~28 ms of a 172 ms frame, and deleting
every one of them leaves 144 against a 48 ms target.** It is worth doing because it is cheap and
certain, not because it changes the arithmetic — that still rests on `draw_road` (34) and
`build_track_geometry` (26).

### 5b. ⭐⭐ THE DECODE'S BOOKKEEPING — 5 ms around 7.2 ms of real work, never looked inside
`make DECODESPLIT=1` now carves `decode()` SIX ways (§12c added the rect slot). The shipping
15.04 ms is **0.92 `snapshotBands` + 1.63 `buildLineModes` + 2.00 own/carve + 7.22 `convertRace`
+ 3.02 remainder + 0.11 bracket**. `snapshotBands` + `buildLineModes` must survive forever
(`m_plan` is the COPPER's palette schedule, §11a), but **the own/carve loops are 2.00 ms of
per-frame scan over all 208 display lines that exists only because ownership exists**, and the
**3.02 ms remainder is unattributed**. Neither has ever been read. Cheap and certain, unlike the
rows.

### 6. ⭐ RE-PRICE the four FPS-era "nulls" in milliseconds
They were judged with an instrument that cannot see 2% (Rule 1a), so a real 1-3 ms win could be
sitting inside any of them: `paint_run_one` (−0.15% FPS), the wide-value campaign (+0.65%), the
span kernel's call/search flattening (+0.8%), the direct plotter ("9% slower" — it is stage TWO of
a change whose stage one is a producer-maintained per-column dirty mask). **Re-pricing is a
measurement, not a rewrite** — the code shapes themselves are in CLOSED below.

---

## The rest of the port

### ⬜ Phase 7 — packaging (`docs/phases.md`)
WHDLoad slave; a player-facing README (keys → `docs/controls.md`, requirements); an asset audit so
the release ships only what the port needs, not the disc image.

### 🔧 The sound BY-EAR pass (`docs/phases.md` §5.4)
Owed since sound landed, and the one thing in the project that **cannot be verified headlessly**.
`make sound` proves the scheduler tick-for-tick against a real MOS; it cannot prove it sounds right.

### 🔧 The naming pass's residue (`docs/static-map.md` §Open items 5)
Still unnamed: **the interior of the 3D pipeline below `project_point`, the front end's prompt
chain, and the other cars' AI.** Does not gate anything — `symbols.csv` feeds the transpiler, so a
name learned later propagates on the next `make gen`.

### ⬜ The main-loop calls no twin tree covered (`docs/phases.md` §THE TWIN CAMPAIGN)
The other-car AI (`$2937` and its projector), `process_car_contact`, `check_crash`,
`sort_cars_by_key`, and the lap/session bookkeeping. **Understanding, not framerate** — the
campaign's own conclusion is that the port's costs are the machinery, not the game's algorithms.

### 🅿️ Comment condensation on `src/gen/revs_native.c` — **PARKED BY THE USER**
1.1 MB, mostly very verbose comments. The user parked it explicitly ("Leave comment condensation
for later and continue on other unfinished business"). **Do not start it unasked.**

### ○ Optional: `make determinism-race`
13 000 frames, `RELEASE=1`, ~2 min — the **only** target that reaches any `& $80` arm; every other
determinism run is a PRACTICE session. Worth running after a change to session/lap bookkeeping.

---

## ⛔ CLOSED — measured dead ends, one line each. Do not rebuild these.

- ⛔ **THE BOTTOM BAND AS DYNAMIC RECTANGLES** (`make DASHOWN=1`, display lines 158..191; built,
  oracle-green over 75 checks, five sabotages) — **+8.12 ms of `decode()`**, and the painter is
  what is closed: the rectangles ran at a measured **143 cyc/byte** against the decode's 13.6,
  because 13.6 is a wholesale, longword-batched, dirty-SKIPPING rate over 40 contiguous cells.
  **Never price a small-region pass at a rate measured on a wholesale one.**
  ⚠⚠ **Its "+0.07 ⇒ the prize is zero" half is RETRACTED — both arms owned the rows** (a
  macro-name collision that also froze the needles on screen; the rows are worth ~2.9 ms and the
  `ch/f` ranking rule that came out of it is withdrawn). `docs/span-render-plan.md` §12c, and the
  live entry is 2d.
  ⭐ Kept from it: `make DASHBARE=1 DASHCHECK=1` (**the enumerator** — suppresses every rectangle so
  the oracle's histograms report the whole moving footprint) and `make fbwrites`' new per-routine
  **cell** range and re-expand-rectangle table.

- ✅ **`view_low_run`'s inner fill was unrolled eight ways** — `#pragma GCC unroll 1`, phase 33 **16.12 → 14.16 ms**, `mem[]` byte-identical. ⭐ `LOWDOUBLE=1` first established that the low block's cost IS the painting, not the driver — the opposite of phase 18.

- ✅ **`fill_dash_edge_columns` flattened** (`EDGEFLAT=1`, default): phase 18 **10.12 → 5.92 ms**, `mem[]` byte-identical, all four determinism trajectories + viewdiff clean. Its remaining 5.92 is ~2.9 of classifier calls. ⛔ Do not price anything there per CELL — the cost was always per WALK.

- ⛔ **`EDGEFILL=1`, pass A's per-cell re-reads hoisted into a precondition-selected sibling**: byte-exact (6 twins, 4 determinism trajectories) and measured **0.0** — the cost is per-walk, not per-cell.
- ⛔ **Skipping "empty" pass-B walks**: **+1.0 ms and it never fired** — 0 of 11 000 pass-B walks are empty (`EDGECOUNT=1`). The premise came from a partly vacuous oracle; see `docs/perf-method.md`.

- ⛔ **Dropping `fill_dash_edge_columns`' source gap fill** (`EDGESTART=1`): −13 ms and **353-403 bytes of the gated road view wrong on all five circuits**. The carry into cells 4..6 / 27..34 comes from a *road* colour to their left, not from the run's entry composite. Keep the fill, make it cheap (entry 0). `docs/perf-method.md`.

- ⛔ **Packing `surface_colour_at_core`'s `SlotExit` return** (phase 18, 2026-09-19): `column_gap_walk_core` 1176→620 instructions and `fill_edge_column_run_core` 540→61, twins byte-exact, and phase 18 went **10.4 → 12.8 ms**. An `always_inline` struct return is already free (SRA + DCE); the pack is real hot-path work on a machine with no byte-insert. `docs/perf-method.md`.
- ⛔ **`TERRAINCARVE=3` as an instrument** with `TERRAINLOW=1`: it deletes the scan the low block's painter depends on, so the arm measures a collapsed trajectory (phase 33 = 883 ms/frame), not a scan-less frame. Use `SCANDOUBLE=1`. `amiga/Makefile`.
- ⛔ **`plotDeltaBase` re-basing every sweep** as an explanation for phase 24: `amiga/dbase_probe.gdb` reads `deltaBases=1` over 331 sweeps.

Each was built, measured and reverted or retracted. The reasoning is in the named doc; this list
exists so nobody spends a day re-deriving a negative result.

- **Writer-maintained framebuffer dirty maps** (`CHANGEDIRTY`) — byte-exact and **~8.5% slower**;
  reverted, the flag no longer exists. Needs producer-native change events to be worth retrying.
- **A writer-maintained CONSUMER predicate** (`g_viewLineDirty`, `REVS_VIEW_MARKING`, the second
  instance and a **different** mechanism) — the span it qualifies saves 3.35 ms and the marking
  costs **+4.93 ms in the producers**, for a net **+1.0 ms**. Not compare traffic: turning marking
  on inlines the `REVS_FLAG_OP` leaf `view_mark_source` into `seam_write`, a header choke point, and
  the objdump counts **164 inlined copies** of the map's address — twenty in `column_gap_walk_core`,
  whose caller then collapses 532 → 54 instructions. ⭐ The replacement is **stateless and exact**:
  `view_consume`'s destructive read means a non-zero source at line entry *is* the change flag
  (`docs/span-render-plan.md` §10p). **No dirty map, in any form, on this path.**
- **A plotter that runs BESIDE `view_paint_lines`** (mirror-each-store, the `SPANEMIT` scaffold that
  adds plotting without deleting the sweep, or a per-line **hook-in** that stops the sweep on its own
  lines but leaves the sweep's driver running) — loses, now **four** times (§7f −9%, source-event
  +25 ms, the SPANEMIT scaffold +54 ms, `SPANFILL=1`'s hook-in ≈ 0). ⭐⭐ The fourth is the sharp one:
  it *did* delete 58% of phase 1's units at the predicted rate and still netted zero, because a
  hook-in reaches the **unit loop** and phase 1's bracket is **905 cyc/line of driver** plus units.
  ⇒ **Size a hook-in against the differential for the part it can actually delete, never against a
  census of stores — the missing third number is WHAT SURVIVES.** ⚠ **This is NOT the §10
  architecture** (which deletes the driver too — the phase-1 takeover); it forbids bolting onto the
  sweep, not replacing it.
- **STAGE A — a line painted from the ROAD RECORD (spans) instead of the forty cell chains**
  (`make SPANPAINT=1`, built, exact, oracle-green, then closed on cost) — **+7.16 ms/frame**
  (producer +3.68, painter +3.48). ⭐⭐⭐ **A coarse test only pays if it REPLACES the fine ones,
  and a group-of-four source test cannot: the scan it must do to qualify costs what the per-cell
  source test cost** — a wholesale group is 236 cycles against the 184 of the four chain cells it
  replaces, i.e. **+32 cyc/group on the arm that fires** (74% of groups, as designed). And a free
  version of it still models to a wash against the floor (5.57 events a line, forty cells' two
  plane stores), so it is the IDEA that is closed, not the tuning. ⭐ Collateral, and general:
  **a sorting network is a code-size trap** — five compare-exchanges plus an unrolled consumer
  behind them made `view_span_line` **726 instructions**, one arm per ordering of four edges.
  `docs/span-render-plan.md` §10q. **Do not re-propose painting from a span record.**
- **A per-byte DELTA painter for the dashboard's needles** (display lines 158..191, `kDeltaBlock`'s
  third block — built, oracle-green over 1000 sweeps and five sabotages, then **closed on cost**) —
  **−5.44 ms of phase 27 against +5.41 ms of phase 32, net +1.37 ms**, and **+3.29 of that cost is
  paid with every call EARLY-RETURNING** because `plot_line_octant` / `undraw_plot_lines` also write
  129..157. ⭐⭐⭐ **A delta painter's break-even is ~0.8 DELIVERED BYTES per owned row per frame** —
  budget ~544 cyc/row (the decode the row deletes), cost ~666 cyc per delivered byte; the glyph rows
  are at 0.11 and the needles at 1.69. ⇒ read the ownership ledger's `st/f` column and divide by the
  row count first. ⚠ This closes the per-byte mechanism for these rows, **not the rows** — they
  come back only by RETARGETING the plotters' own stores into the planes instead of `mem[]`, which
  is ⛔ closed too, one row below — ⛔ widening the *mirror* to 129..191 is refuted by the same arithmetic, 11.0 ms of walk plus
  delivery against 4.8 ms of budget. `docs/span-render-plan.md` §11d.
- **Consumer run-entry specialisation** (single-run flat-span path) — **−0.15%**, retracting its
  predicted "~10% prize". Do not retry that code shape.
- **The per-line skip** — **−0.4%** for 39.5% of line-visits deleted; phases 2+3 skip zero units.
- **The viewport SOURCE-EVENT/RUN consumer** (`VIEWEVT`, `docs/direct-bitplane-plan.md` §7j item 2,
  **both stages**) — **+25.46 ms on the sweep** for 86% of unit visits deleted. The walk costs
  2145 cyc/run against the scan's 878 for the same 17.6 cells, and only 24% of that is painting:
  a cell-indexed mask forces index→pointer arithmetic at every step (476 cyc/event, 402 cyc/run
  prologue) where the scan pays an incremental `addq.l #8`. Break-even **N = 36.5 + 35.3·E** cells;
  a line is 40 and a run averages 17.6, so it cannot win at any event density.
  `docs/perf-method.md` §the walk walks indices.
- **The producer-EXTENT viewport scan** (a first..last bound per line, RoF's `minScan` shape) —
  measured **54% of cells still visited** (host 51%) by the run census, against 13% for a true run
  list. The must-visit cells are few but spread across the line. It is a run list or nothing.
- **The wide-value campaign, end to end** — **+0.65%, inside noise** (`docs/wide-value-cleanup.md`).
  ⚠ The instruction-count win is real; the byte lanes are simply not where the frame goes.
- **`always_inline` on `view_plant`** — **+1.04 ms**; `view_paint_lines_core` grows 757 → 995
  instructions and `paint_lines_short` is already at the register ceiling.
- **`__builtin_expect` in `column_gap_walk_core`** — **+1.38 ms**; it pulled a bulky inlined
  classifier into the loop's main flow, GCC dropped the 4× unroll and evicted four invariants.
- **The packed-register ABI on `view_paint_lines`' three threaded bytes** — **+0.2 ms**; eleven
  packs bought eighteen loads, because their address escaped only to CALLS.
- **A `noinline` ESCAPE for the view sweep's cold hardware-window arm** — **+0.73 ms**, and the
  mechanism is the general lesson: an *inline* `bus_write` merges its `mem[addr] = val` with the
  fast arm's store so the cold instructions never run, while a `noinline` callee is an **aliasing
  barrier** that makes GCC assume any memory is written — `view_own_run`'s stack slots go 17 → 30
  and its CSE'd displacement reads collapse. ⇒ **bulk in a cold arm is cheap; a call boundary in a
  hot loop is not.** ⚠ The register-pressure explanation was tried and is WITHDRAWN (a third arm
  removed the live range and measured identically) — separate the arms before believing a
  mechanism. And the test itself was **unreachable**, not merely cold: `docs/perf-method.md`
  §the second bite.
- **The direct plotter alone** — **9% slower**; `$7BE2`'s cost is the SOURCE SCAN (2093 units read,
  ~83 non-zero), so it is stage two, not stage one.
- **`fill_dash_edge_columns` (phase 18) further changes** — a written do-not-retry at the code.
- **The frame-slot defect class outside the decode** — scanned and EXHAUSTED; on a register-poor
  machine a stack slot is a legitimate home for a loop invariant.
- **Phase 1's unit loop / the "unexplained ~6×"** — RETRACTED; 43 cyc/unit is the 68000's floor and
  widening is impossible given the BBC layout.
- **Re-running the frame-slot scan, the `span_walk` `mem[arm->addend]` hoist, `BODY_IN_ISR`** — all
  settled; `make BODY_IN_ISR=1` survives for A/B measurement only.
- **The view sweep's own per-line DRIVER, and the RANGE painter that was waiting behind it**
  (`make VIEWFULL=1` — phase 1 gets its own line loop with the stop test hoisted to a sweep-level
  precondition, gated by its own counter: 48 888 lines, 0 disagreements) — **−0.33 ms**, against a
  prediction of −6.6 to −11.5. A third arm settles the phase outright: `VIEWFULLCARVE=1` runs the
  driver and does **not** paint, and reads **ph24 = 3.05 ms** ⇒ the entire per-line driver is
  **601 cyc/line and phase 1 is 83% painter** (~90% shipping — ~1.3 ms of that 3.05 is
  `revs_plot_chain`'s seven volatile `SPAN_STAT` prologue RMWs, and `SPANSTAT` defaults to
  `$(if $(PROBES),1,0)` so they do not ship). ⭐⭐⭐ **The method error is the transferable part:
  the prediction's driver term was a RESIDUAL — a *measured* bracket minus a *modelled* painter —
  so every modelling error landed in the one term being sized.** ⇒ **Price the part you intend to
  delete with an arm that DELETES it, never with a model that subtracts it**: one build and one run
  replaced three sessions of arithmetic that was wrong in both directions (≈6 ms, then ≈11.5, truth
  3.05 total). `docs/span-render-plan.md` §10q; Stages B..E of §10j inherit the closure.
- **ROW OWNERSHIP beyond domain A — every display row the decode still owns, by EITHER mechanism**
  (§11e) — the whole remaining campaign is **−3.61 ms best case** (all 75 unowned rows, an ideal
  *inline* retarget, no shape cost) against a **measured +3.45 ms** for the only store-site
  placement ever built, because a cross-TU call in the writers' own loops is an aliasing barrier and
  the loop spills. ⭐⭐⭐ **Two mechanisms, two break-evens 15-32× apart: a MIRROR costs 666 cyc per
  delivered byte (break-even 0.82 delivered bytes per owned row per frame); a RETARGET costs
  ~30-44 cyc per store (break-even 12-26 stores per owned row per frame) — and the prize, ~789 cyc
  a row, is identical either way, because the decode a row deletes does not care who paints it.**
  ⇒ **a row's ownership COST scales with its writers' store rate and its PRIZE does not: rank by
  `st/row`, ascending, never by the ledger's row price.** That ranks the needles (1.6 st/row) best
  at −2.18 ms net and leaves the view sweep's own 117..157 within 4% of break-even from the wrong
  side — a row repainted 27 times a frame costs 27 retargets to own — which is why this stalled for
  three sessions. ✅ The owed row-149 reader gate dies with it, unasked. `docs/span-render-plan.md`
  §11e.
- **§4a item 2 — permuting the view band's pens so display lines 81..horizon need no writes at
  all** — MOOT rather than measured: phase 1 owns rows 81..116 outright (`make SPANFILL=5`), so the
  decode does not touch that band any more and there is nothing left for a permutation to save.
