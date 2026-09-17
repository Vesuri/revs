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

**Where the frame stands:** **192.99 ms bracketed** (Σ phases 1..39 = wall − phase 0), `PROBES=1
FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000`, warp, driving, HEAD `151e282`
(`frozen=240440078`, `loopFrames=298`, `build=1d`). Target is **20 ms** (50 FPS), floor **40 ms**
(25 FPS) — so this is a 5-10× problem, not a tuning problem, and an entry worth under ~1 ms is not
where the answer is. ⚠ Size every candidate in **ms/frame** against its own phase row (Rule 1a);
the framerate is quantised to `50/N` and cannot see it.
⚠⚠ **Read the phase table out of `.run/gdb-out.log`, never out of `diag_run.sh`'s stdout** — that
is `tail`-truncated to the last 40 lines (`GDBTAIL`), which silently drops phases 1..5 AND the
`frozen=` gate line, and a total summed from it reads ~36 ms low. It has now cost two bad diffs.

| ms/frame | phase(s) | what |
|---:|---|---|
| 53.0 | 24+33+34+32 | `view_paint_lines` — the consumer |
| 34.3 | 11 | `draw_road` |
| 26.7 | 5 | `build_track_geometry` |
| 20.0 | 27 | `RevsScreen::decode()` — port overhead, no BBC counterpart |
| 12.4 | 28 | the vblank spin — the `50/N` pad, not a target |
| 12.2 | 26 | the 50 Hz drain |
| 10.2 | 18 | `fill_dash_edge_columns` — ⛔ see CLOSED |

### 1. ⭐⭐⭐ THE REPRESENTATION — ROW OWNERSHIP of the decode — −11.18 ms of rows, then the CALL
⭐⭐ **THE LIVE BUILD IS THE DASHBOARD, display rows 158..207 — `−4.11 ms`, 50 rows, and the
largest UNBLOCKED block** (`make VIEWCARVE=158-207` priced it at HEAD). Rows 117..157 are worth
more (−5.07) and are blocked: three foreign writers plus a **game-logic reader** —
`update_grip_limits` samples the surface colour under a wheel from `mem[$713D]`/`mem[$7205]` on
display line 149, so those rows cannot be owned until that read is served another way. The
dashboard's own hazard is the needles (display lines 129..180 — they cross the boundary).
⭐⭐⭐ **THE ARCHITECTURE IS OPEN AND ITS FIRST HALF IS NOW MEASURED, NOT ARGUED** — `make
SPANFILL=1` is §10e's own cheap checkpoint built and run (`docs/span-render-plan.md` §10n). Three
results, and they point at a different next build than this entry used to name:
- ⭐⭐ **DIRECT-TO-BITPLANE WRITING IS EXONERATED.** The fill costs **1.63 ms for 21 spans
  (550 cyc/span, 6.87 cyc/byte)**, and an objdump model closes to 1%. Every earlier "the plotter is
  too dear" figure was a code shape: 185 cyc/8 B (stack-slot reloads) → 56 (preamble bigger than
  its stores) → **27**.
- ⭐⭐ **A HOOK-IN CANNOT WIN, BECAUSE THE SPAN DELETES THE UNIT LOOP AND THE DRIVER SURVIVES IT.**
  The deletion measures 4.98 ms at **42.0 cyc/unit** — the objdump's clean-arm rate, so it reaches
  the units and nothing else — while phase 1's **905 cyc/line** driver runs on. 58% of the units
  came off and bought 32% of the bracket.
- ⛔ **AND ITS PREDICATE COSTS MORE THAN IT SAVES: net +1.0 ms as built, −0.9 ms stateless.** One
  line in CLOSED below; the mechanism is inline bloat, not compare traffic.

⇒ **THE LIVE BUILD IS THE PHASE-1 TAKEOVER (§10p): the renderer OWNS the line loop, driver
included.** `view_consume` is RLE with a *destructive* read, so a source is non-zero at line entry
**iff a producer wrote it since the last sweep** — a stateless forty-source test is therefore
**exact where the dirty map was merely conservative**, and `view_stop_from(0) == 40` is a constant
`true` through all of phase 1. The scan costs **180 cyc/line, not 480**, because four consecutive
*lines* of one cell are four consecutive bytes (`or.l`, one test per four lines). Per line:
905 + 40×55 = 3105 cyc becomes ~250 + 180 + 550 ≈ **980** flat, ~2300 changed.
**Size: phase 1 15.83 → ~5.0 ms all-flat, ~8.1 ms at the parked 21/36 split.** Phases 2/3
(12.34 + 23.61 ms at 67%/80% chain-entry) are the larger prize behind the same shape.
**Order of work is §10p's, and steps 1-4 are DONE** — the full ledger with its commits lives
there; the short form: ✅ the host flat-line count (57%, `3b79ebc`); ✅ the reader gate, **OPEN for
rows 81..116** (`f80557d`, `REVS_FB_POISON` — ⛔ the old `column_gap_walk` gate is RETRACTED,
nothing reads those rows); ✅ the stateless predicate (`df4cd40`, free at +0.22 ms); ✅ **phase 1's
takeover — all 36 lines paint straight to the two bitplanes, `make SPANFILL=5`, `5a970bb`**, both
§10n oracles clean on the target driving; ✅ **phases 2/3 own their runs, `make VIEWOWN=1`,
`aa5703a`, −1.74 ms on the sweep and −1.989 ms on Σ(1..39)−ph28**; ✅ **`bus_write` left the
renderer, `b29aab7`, −1.17 ms** (the range test was a constant and its arm unreachable — `$6700` is
an immediate operand in the game's own code).
⇒ **THE LIVE STEP IS §10p (5), THE DRIVER REWRITE, AND IT IS ENTRY 2 BELOW** — the two levers have
converged: what stands between the short phases and the bitplanes is the chain-entry machinery, not
the painting. Two figures bound it: the decode's own ceiling for those lines is **−5.07 ms**
(`make VIEWCARVE=1`, ~2.0 scan / ~3.0 expand, and a per-run takeover reaches only the expand half),
and ⛔ **whole-line ownership of phases 2/3 is closed on arithmetic** — 41 lines × 0.36 ms of
painting = 14.8 ms against 4.9 ms of units + 5.07 ms of decode = **+4.8 ms net.** They stay
per-RUN.
⚠⚠ **The remaining correctness obstacle is MIXED CELLS:** `m_lineMode[y] = 0` is
per-line-ALL-40-CELLS, so a cell owned on some of its 8 lines and written through `mem[]` on the
others repaints from a stale byte. Priced: per-cell RMW **1.6 ms ⛔**; per-row prefix-XOR delta
**≈0.9 ms**; per-changed-cell 8-line range test **≈1.1 ms**.
⚠ Genuine risk still stands: **four** schemes that bolted onto the mem[] scan have now lost (§7f
−9%, source-event +25 ms, the SPANEMIT scaffold +54 ms, and this checkpoint's hook-in ≈ 0). The
takeover differs in kind — it *removes* the driver — but that is an argument until measured.

⭐⭐⭐ **THE SPAN CENSUS HAS RUN (2026-09-14) and it changed the shape of the answer** — the scout
§9 had been asking for, now a committed instrument (`src/platform/shape.cpp` §THE SPAN CENSUS,
`make SHAPE=1` + `REVS_SHAPE_WATCH=N`). The sweep's 2155 cell stores a frame are **a span list**:
**144 solid runs of 13.9 cells + 146 single-cell edge bytes**, with **93% of painted bytes** one of
the four solid MODE 5 values and **≤5 runs on 93% of line paints**. Rows 81–100 alone are 37% of
the stores as **20 single-run rows**. And the whole pipeline turns out to carry **161 source cells +
320 bytes of edge table** of real information per frame, for which it spends **145 ms** (85.1 ms
expanding, 70.8 ms producing). §10 sizes the replacement at **~48 ms of per-frame work** (from
~181), retires **17 of the 20 `SMC_SITES`** plus the consumer's whole stop-planting mechanism, and
names its own cheap checkpoint: **a 20-span emitter for rows 81–100 only, measured before the rest
is written.**
⚠⚠ Two corrections came out of it and both are in §10: the **§8 sprite item is sized against the
wrong routine** (`$7BE2` paints rows 81–157, the VIEW band, never the dashboard's 158–207), and the
per-phase framebuffer instrument's "phase 5 writes lines 24..55" is the **code under the sky**, not
pixels.

⚑ **THE INHERITED GATE IS DISCHARGED (2026-09-14)** — `terrain-render-plan.md` +
`flight-perf-log.md` §1 read in full. Four findings that transfer, so nobody re-reads 3 000 lines:
1. The direct renderer replacing the chunky→planar convert measured **~339 → ~172 ticks/frame** for
   the stage it replaced, and the project's own verdict on it was *"real but **not
   transformative**"* — because it left the compute floor (their fractal subdivision, our
   `build_track_geometry`→`draw_road`) untouched. Necessary, not sufficient.
2. ⭐⭐ **Their single biggest win in the whole log was not rendering and not asm**: per-instrument
   **producer-side dirty flags** replacing a 560-cell shadow scan, **~1662 → ~65 ticks (~23×)**.
   That is the shape our ⛔ `CHANGEDIRTY` got wrong — it compared at the STORE instead of letting the
   producer, which already knows what it touched, say so. ⛔ **And our second attempt at it is
   closed too** — the producer-marked source-event mask below — so the transferable part is
   narrower than it looks: their flag works because ONE flag covers a whole instrument's redraw.
   A per-CELL bit does not amortise on this machine.
3. Bounding a scan by an extent the producer already knows (their `minScan` = topmost skyline row)
   was **~324 → 113 ticks** on its own. ⛔ **And this is the one of the four that does NOT transfer
   — measured, see CLOSED**: the same idea on our viewport would still visit 54% of the cells,
   because our must-visit cells are few but SPREAD (their skyline extent is one contiguous band).
4. Reading the scan **4 bytes at a time** and overlapping blitter ops with disjoint CPU work took
   their direct renderer **~478 → ~170 ticks/call**.

⛔⛔ **AND THE SOURCE-EVENT/RUN SUB-LEVER IS DEAD — BUILT, MEASURED AT +25.46 ms ON THE SWEEP, AND
THE CYCLE MODEL SAYS WHY (2026-09-14).** It was the biggest un-built item on this board, and the
census that sized it counted correctly and **priced wrongly**: 43 cyc/unit × the deleted 86% bounds
the *saving* and says nothing about the *replacement*. One line in CLOSED below; the full account is
`docs/perf-method.md` §the walk walks indices. The short form: on a 68000 the expensive direction is
**index → pointer**, so a cell-indexed event mask pays 476 cyc/event and a 402-cyc per-run prologue
against the scan's incremental 43 cyc/cell — break-even is **N = 36.5 + 35.3·E** cells, a scan line
is 40 and a chain run averages 17.6. Both stages die together: stage 2 keeps the prologue and the
per-event addressing.
⭐⭐ **What survives is a REQUIREMENT ON THIS ENTRY, which is why it is recorded here rather than
re-queued:** a sparse-iteration consumer is only affordable if its addressing amortises over a whole
LINE or SWEEP, or if the producers hand over byte **offsets** the consumer can use as pointers
without arithmetic. The direct-bitplane layout is chosen partly on that criterion now.

⚠ **Two decompositions of the same 52.7 ms sweep are in circulation and they are NOT the same
axis** — `NOUNITS=2` differencing says 32.4 ms driver/entry + 20.4 ms unit loop (entry 2 below);
§7j's calibrated bracket says 29.0 ms unit/run *interior*, of which ~8 ms is destination stores and
~21 ms is source/translation/control. Never subtract one from the other.

✅ Not open any more, recorded so it is not re-attempted: **the flat sky band is already skipped**
(shipped 2026-08-16 — `g_decodeFlatLines` reads 63 in one band on the target, `make FLATSKIP=0` is
the A/B). Its "~75 ms" tag was a 1282 ms-frame-era figure. What is still available on that side is
§4a item 2 — lines 81..horizon made write-free by PERMUTING that band's pens — and it is a
direct-renderer-only option, so it is gated behind this entry, not free today.

### 2. ⭐⭐⭐ The view sweep's DRIVER code — **29.03 ms left in phases 2+3**, and the ENTRY half is DONE
⭐⭐ **−6.58 ms taken** (`214c8ae`, `docs/perf-method.md` §the entry was deleted): the runs are
**inline in both drivers**, `byte`/`line`/`cell` are in registers, `view_own_enter`'s poke-decode
round trip is gone, and phases 2+3 went **35.61 → 29.03 ms** with their census identical to the
unit (426/32/16 and 282/50/25). Phase 3 is now **5346 cyc/line for 5.6 painted cells**.

**Where the 29.03 ms IS — SETTLED FROM THE OBJDUMP, and it is that THE BODY IS LONG** (floor =
cheapest plant/trap-free path from loop head to back edge; ceiling = every non-cold instruction
once; `docs/perf-method.md` §where phases 2/3's 29 ms is, settled from the objdump):

| | floor | **measured cyc/line** | ceiling | non-cold instrs | cells/line |
|---|---:|---:|---:|---:|---|
| phase 2 (16 lines, 10.16 ms) | — (all routes touch a plant block) | **4502** | 4374 + unit turns | 365 | 26.6 |
| phase 3 (25 lines, 18.86 ms) | 2154 | **5348** | 6026 | 499 | 11.3 |

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
per-display-line ownership) and the next domain are §11's; ⛔ **the "phase 2 at ~1.5 ms instead of
10.16" estimate is RETRACTED** too — it assumed the span deleted the source walk.
✅ **Step 0 is DONE: the new pipeline is the DEFAULT build** — `VIEWOWN=0` / `SPANFILL=0` are now
the A/B controls, so the shipping frame goes **196.59 → 182.62 ms (−13.97)**. `validate` PASS, all
five `determinism` trajectories PASS.

⭐ **Four candidate causes of the per-line cost were checked and ALL are too small:**

| candidate | measured | verdict |
|---|---|---|
| the plants (`view_move_stop`/`view_plant`) | **25 plants a sweep, ~1 ms all phases** | ⛔ not a step — a host census, not the split's 888 cyc/line |
| the census instrument (`PROBE_VIEW_*`) | ~680 cyc/line ≈ 13% | instrument, and it is inside every figure here |
| the four spilled invariants | 15 body touches/line ≈ 240 cyc | ⛔ the `column_gap_walk_core` trap — both runs are on pads past the back-edge |
| phase 2's cold `view_own_run` arm | **16 of 32 runs, one per line** (host census) | ⛔ ~150 cyc/line: a `ViewState` marshal + a call, not a fallback path |
| the per-line table reads | `lea (0,a3,d7.l),a5` once, then 12-cyc `d16(a5)` | ⛔ already optimal, nothing to hoist |
| `step_scanline`'s byte-lane dance | ~230 cyc/line (phase 3 ~280, carry tail) | ○ **the one real local item: ~1.4 ms, a wide-value rewrite** |

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

### 3. ⭐⭐ The per-span REPRESENTATION — ~11.5 ms of `draw_road`'s 33.7
`docs/perf-method.md` §per-span SETUP (45): 43 spans × ~1 900 cycles. The remaining phase-11 lever
now that the kernel's shape and its opcode slots are done. **Needs SPAN_DX/DY/BLOCK moved out of
the 6502 address space**, which changes the measured self-overwriting-DDA behaviour ⇒ a **written
RESULTS-rule reader audit** (`docs/validation-harness.md` §THE RESULTS RULE), a scoped
`set_ignore`, and `make viewdiff`. Sibling, same subsystem: **the per-walk entry/exit, ~10.5 ms
for 24 walks**.

### 4. ⭐⭐ Fewer POINTS / SPANS / SOURCE VISITS — `build_track_geometry`, 26.7 ms
`docs/perf-method.md` §What is left. Setup and loops fused into native value pipelines; the
interpreter is already gone from the whole tree, so nothing is left to delete there. Named
sub-levers: producer-emitted source dirty events/runs (12-18 ms), a native geometry `EdgePoint`
value pipeline (8-12 ms), dash specialisation (5-10 ms).

### 5. ⭐ HARDWARE SPRITES for the instruments — gated behind entry 1 (the §10 renderer)
`docs/span-render-plan.md` §10m. ⚠ Its old size figure was against the wrong routine (see §10a); re-size with `make fbwrites` before scheduling. The BBC had no sprites so every moving dashboard item is
CPU-drawn; the Amiga has eight idle. The open constraint is **width** (8 × 16 px = 128 of 320 in
one 42-line band, where vertical reuse buys nothing). Pre-render each variant by running the
game's own drawing code, so the images derive from the oracle.

### 6. ⭐ The VERTB ISR — ~720 µs/field left, ≈3.6% of WALL CLOCK
`docs/perf-method.md` §The VERTB ISR. Invisible to every phase row, and permanent: 50 fires a
second whatever the framerate does. `snd_tick` ×2 (~276 µs) is the largest remaining row;
everything else is at or near the 92 µs instrument floor. ⚠ **Do not merge the two ticks into one
pass** — `make sound` compares chip state tick by tick against a real MOS and the intermediate
state is part of the contract.

### 7. ⭐ RE-PRICE the four FPS-era "nulls" in milliseconds
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
  architecture** (which deletes the driver too — entry 1's takeover); it forbids bolting onto the
  sweep, not replacing it.
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
