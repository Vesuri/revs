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

**Where the frame stands:** **205.5 ms bracketed** (Σ phases 1..39 = wall − phase 0), `PROBES=1
FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, warp, 30 s, driving, HEAD `8bc45ac`. Target is
**20 ms** (50 FPS), floor **40 ms** (25 FPS) — so this is a 5-10× problem, not a tuning problem,
and an entry worth under ~1 ms is not where the answer is. ⚠ Size every candidate in **ms/frame**
against its own phase row (Rule 1a); the framerate is quantised to `50/N` and cannot see it.

| ms/frame | phase(s) | what |
|---:|---|---|
| 58.7 | 24+33+34+32 | `view_paint_lines` — the consumer |
| 33.7 | 11 | `draw_road` |
| 26.7 | 5 | `build_track_geometry` |
| 26.4 | 27 | `RevsScreen::decode()` — port overhead, no BBC counterpart |
| 13.5 | 26 | the 50 Hz drain |
| 11.0 | 28 | the vblank spin — the `50/N` pad, not a target |
| 10.4 | 18 | `fill_dash_edge_columns` — ⛔ see CLOSED |

### 1. ⭐⭐⭐ THE REPRESENTATION — render direct to bitplanes
`docs/direct-bitplane-plan.md`. **Gated FIRST**, and the gate is not about size: asm or sprite work
written against the current BBC-shaped buffer has to be rewritten after this lands. Reaches the
26.4 ms decode directly (it becomes the validated ORACLE rather than the shipping path) and
unconstrains the 58.7 ms consumer, which today paints into a layout the Amiga cannot display.

⚑ **THE INHERITED GATE IS DISCHARGED (2026-09-14)** — `terrain-render-plan.md` +
`flight-perf-log.md` §1 read in full. Four findings that transfer, so nobody re-reads 3 000 lines:
1. The direct renderer replacing the chunky→planar convert measured **~339 → ~172 ticks/frame** for
   the stage it replaced, and the project's own verdict on it was *"real but **not
   transformative**"* — because it left the compute floor (their fractal subdivision, our
   `build_track_geometry`→`draw_road`) untouched. Necessary, not sufficient.
2. ⭐⭐ **Their single biggest win in the whole log was not rendering and not asm**: per-instrument
   **producer-side dirty flags** replacing a 560-cell shadow scan, **~1662 → ~65 ticks (~23×)**.
   That is the shape our ⛔ `CHANGEDIRTY` got wrong — it compared at the STORE instead of letting
   the producer, which already knows what it touched, say so. The live version of it here is the
   viewport source-event representation below.
3. Bounding a scan by an extent the producer already knows (their `minScan` = topmost skyline row)
   was **~324 → 113 ticks** on its own.
4. Reading the scan **4 bytes at a time** and overlapping blitter ops with disjoint CPU work took
   their direct renderer **~478 → ~170 ticks/call**.

⭐⭐ **The live sub-lever, and it is the biggest single un-built item on this board — VIEWPORT
SOURCE EVENTS/RUNS.** `docs/direct-bitplane-plan.md` §7j (the settled costing): the producers know
which of the forty `$80`-spaced source blocks they touch, so `view_paint_lines` can iterate CHANGED
units/runs instead of testing all 2148 slots. Gross source/translation/control surface **~21 ms**,
expected net **12-18 ms**. ⚠ A line-only bit is insufficient if it still causes a forty-unit scan.
Keep the present consumer as the byte-exact oracle until every trajectory agrees.
⚠⚠ **Do not price this against a framebuffer dirty map** — that is ⛔ CLOSED below, and §7j is
explicit that its ceiling omitted the producer-side price.

⚠ **Two decompositions of the same 52.7 ms sweep are in circulation and they are NOT the same
axis** — `NOUNITS=2` differencing says 32.4 ms driver/entry + 20.4 ms unit loop (entry 2 below);
§7j's calibrated bracket says 29.0 ms unit/run *interior*, of which ~8 ms is destination stores and
~21 ms is source/translation/control. Never subtract one from the other.

✅ Not open any more, recorded so it is not re-attempted: **the flat sky band is already skipped**
(shipped 2026-08-16 — `g_decodeFlatLines` reads 63 in one band on the target, `make FLATSKIP=0` is
the A/B). Its "~75 ms" tag was a 1282 ms-frame-era figure. What is still available on that side is
§4a item 2 — lines 81..horizon made write-free by PERMUTING that band's pens — and it is a
direct-renderer-only option, so it is gated behind this entry, not free today.

### 2. ⭐⭐ The view sweep's DRIVER AND CHAIN-ENTRY code — 32.4 ms of the 52.7 ms sweep
`docs/perf-method.md` §the sweep is 61% driver/entry. Measured by differencing `NOUNITS=2`:
**32.36 ms of per-line driver/entry against 20.36 ms of unit loop**, phase 3 at 80% entry (which
the VIEWP3 split independently reproduces at 83%). This is the larger half and it is a *separate*
lever from the representation.
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

### 5. ⭐ HARDWARE SPRITES for the instruments — gated behind 1
`docs/direct-bitplane-plan.md` §8. The BBC had no sprites so every moving dashboard item is
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
- **Consumer run-entry specialisation** (single-run flat-span path) — **−0.15%**, retracting its
  predicted "~10% prize". Do not retry that code shape.
- **The per-line skip** — **−0.4%** for 39.5% of line-visits deleted; phases 2+3 skip zero units.
- **The wide-value campaign, end to end** — **+0.65%, inside noise** (`docs/wide-value-cleanup.md`).
  ⚠ The instruction-count win is real; the byte lanes are simply not where the frame goes.
- **`always_inline` on `view_plant`** — **+1.04 ms**; `view_paint_lines_core` grows 757 → 995
  instructions and `paint_lines_short` is already at the register ceiling.
- **`__builtin_expect` in `column_gap_walk_core`** — **+1.38 ms**; it pulled a bulky inlined
  classifier into the loop's main flow, GCC dropped the 4× unroll and evicted four invariants.
- **The packed-register ABI on `view_paint_lines`' three threaded bytes** — **+0.2 ms**; eleven
  packs bought eighteen loads, because their address escaped only to CALLS.
- **The direct plotter alone** — **9% slower**; `$7BE2`'s cost is the SOURCE SCAN (2093 units read,
  ~83 non-zero), so it is stage two, not stage one.
- **`fill_dash_edge_columns` (phase 18) further changes** — a written do-not-retry at the code.
- **The frame-slot defect class outside the decode** — scanned and EXHAUSTED; on a register-poor
  machine a stack slot is a legitimate home for a loop invariant.
- **Phase 1's unit loop / the "unexplained ~6×"** — RETRACTED; 43 cyc/unit is the 68000's floor and
  widening is impossible given the BBC layout.
- **Re-running the frame-slot scan, the `span_walk` `mem[arm->addend]` hoist, `BODY_IN_ISR`** — all
  settled; `make BODY_IN_ISR=1` survives for A/B measurement only.
