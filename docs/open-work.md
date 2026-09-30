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

**⚠⚠ THIS IS A PRACTICE FRAME — the player alone.  The RACE PROPER measures 95.72 ms (§2c: the other cars are ~21 ms of it).**
**2026-09-30: the object plotter's line side AND its gap walk in 68000 asm — race 100.62 → 95.72 (ph43 13.77 → 9.97, ph15 3.08 → 2.16), `QUICKQUAL=1` arms against `OBJASM=0`; practice ph15 2.64 → 2.33 for the line side alone.**
**Where the frame stands:** **Σ(1..39) − ph28 = 75.10 ms bracketed** (after the rendering-path review's second pass — the VERTB ISR 1002 → 671 µs a field, the line-surface sweep four lines a step, the on-track crash test's redundant publish, §9b — and its first cuts — the MOS round trip, the edge-buffer clear, the PF2 outline — and the source scan's hits stopped being calls, the ownership map stopped being cleared before its template overwrote it, and a run-B seed stopped computing its list's head, ph24 17.69 → 16.11 → 15.87 → 15.51; before it the geometry walk stopped storing eight dead 6502 working cells a point, ph5 10.59 → 10.02; before it three `draw_road` producer cuts, ph11 16.17 → 13.78: `fill_line_attr`'s walk over locals −1.20, the span pass's twelve dead scratch stores −1.00 and `fill_line_attr` un-unrolled −0.15; before them the 6502-residue cleanup, §9 — 84.80 → 83.26 in a field-matched pair at `8d8a45b`; before it the terrain painter stopped repainting unchanged lines, ph33 7.99 → 5.09; before that `buildLineModes` was memoised, ph27 −1.22, and the needle sprites were keyed on the DDA's input so a hit skips the walk, ph32 3.52 → 2.18) (149.18 at the plan's start, which also carried
~3-4 ms of crash reset — see below) — **0.77× the real BBC's 97.0 (0.83× its comparable 91.0), and ~1.11× real-time game speed in the legacy loop (the default build now runs game time at real time)**.
⚠⚠ 2026-09-25: **BASELINE RESTATED −1.71 ms with no code sped up** — a STRAIGHT_TO_RACE window's ~12 front-end frames
billed a whole `decodeTeletext()` each to ph27 (calls = 2 × frames + 12); they are phase 0 now (`ffe602f`). Every frame
figure before that commit is ~1.7 ms high against the same trajectory (91.24 then = 89.53 now).
⚠⚠ 2026-09-25: the crash/session RESET now has its own phase (63) and is excluded like phase 0 — it had been billed to
the drain (ph26) since the hold ends inside a drained tick; the same binary read 99.10 before the split and 95.46 after
(ph26 6.86 → 3.21, ph63 3.66 amortised = 90 fields over two resets). Every earlier frame figure includes it (the sim is framerate-locked; `docs/perf-method.md` §GAME SPEED IS THE
FRAMERATE). 2026-09-24, one session, every arm `PROBEFIELDS=3000` with phase 0 checked equal: terrain painter in asm
115.51 → 110.13; source scan in asm ph24 20.55 → 18.00 (quoted by its row — that pair's `ONE BODY TICK` moved, a trajectory);
own-reset memoised ph24 → 17.68; `prepareFrame` ph27 7.65 → 6.74 (frame 108.39); the walk's width emitter in asm ph5
18.91 → 16.13, frame 108.39 → 104.83. 2026-09-25: the walk's whole point loop in asm, ph5 16.27 → 10.77, frame 105.16 →
**99.10** (control `WALKASM=0`, phase 0 220/218 fields, body tick 1301/1284 µs). Before that session: the whole span pass in asm, 126.64 → 116.47 (ph11 26.62 → 17.10).
The per-phase table below is the 2026-09-23 snapshot (port ~143.5) and its BBC column still ranks what is left.
⭐⭐⭐ **THE PER-PHASE COMPARISON — the port against a real BBC, row for row (2026-09-23).**
`make bbcprof` brackets the real BBC's 24 main-loop call sites plus the three tail calls, one row
per Amiga phase id; its site MEANS sum to 96.4 of 97.0 ms, so it IS the BBC frame. The port side
is the plain-default phase table with ~0.118 ms (one probe transition, 96 instructions) taken off
each row per call. ⚠ The BBC's 97.0 contains a **6.0 ms busy-delay pad** (`move_and_draw_cars`'
practice arm, twin #179) the port rightly does not reproduce ⇒ **the comparable BBC frame is 91.0
ms, and the port ships at ~143.5** (149.2 bracketed − ~5.7 of probe) **= 1.58×.**

| phase | routine | BBC | port | excess |
|---|---|---:|---:|---:|
| 11 | `draw_road` | 16.5 | 34.3 | **+17.8** |
| 24+33 | `view_paint_lines` | 23.5 | 34.9 | **+11.4** |
| 5 | `build_track_geometry` | 21.7 | 26.5 | +4.8 |
| 3 | `read_driving_controls` | 1.0 | 5.0 | **+4.0** |
| 32 | loop tail (`draw_dash_needles` 3.0 of the BBC's) | 3.4 | 5.5 | +2.1 |
| 14+15 | `build_road_sign` + `draw_track_object` | 2.9 | 5.0 | +2.1 |
| — | eight small rows (sound ×3, markers, timers, mirrors, crash, horizon) | 1.3 | 2.6 | +1.3 |
| 27+30 | `prepareFrame` — **port-only** | — | 8.6 | +8.6 |
| 26+29 | the 50 Hz drain — the game's part is inside the BBC rows | ~1.5 | 6.0 | +4.5 |
| 4 | `apply_driving_model` | 8.1 | 3.5 | **−4.6** |
| 18 | `fill_dash_edge_columns` | 7.4 | 5.8 | −1.6 |
| 17,7,10,6 | cars (net of the pad), advance, clear, place | 3.7 | 2.0 | −1.7 |

⭐⭐⭐ **THE PORT BEATS THE 6502 BY UP TO 2.3× WHERE IT DOES ARITHMETIC IN LOCALS, AND LOSES IN THE
ROUTINES THAT WALK `mem[]` BYTE BY BYTE.** The machine is not the constraint; the byte-at-a-time
representation is. ⛔ **Entry 3's "fewer points / fewer spans" is NOT forced and must not be
proposed as the route** — the producers are 1.25× and 2.1× the 6502, not at a floor.

### ⭐⭐⭐ THE PLAN TO 48 ms, in two steps and in this order

**STEP 1 — NEVER SLOWER THAN THE 6502 ON ANY ROW: ~143.5 → ~90 ms.** Every excess above is work
the port adds on top of the game's own, so each is a defect to find, not a trade to make. Ranked
by certainty × size:
1. `read_driving_controls` **−4.0**, certain — seven key tests a frame through a virtual
   `mosCall` → OSBYTE switch → virtual `keyDown` → a 33-entry linear scan. A reverse rawkey map
   and a direct entry (closed §7, perf-method §THE CLOSED QUEUE ENTRIES).
2. `draw_road` **−17.8** — 5819 cyc/span for 1.1 DDA lines and 1.3 columns a span, 1088 per
   plotted column. Still carries the 6502's self-modifying opcode SLOTS (`span_step_take`,
   `sw_marker`, the page-$00 alias guard) that the governing directive says must go. §2.
3. `view_paint_lines` **−11.4** — the consumer, now painting bitplanes behind a dual playfield.
4. `build_track_geometry` −4.8, the sign/object pair −2.1, the tail −2.1, the small rows −1.3.
5. Port-only: the drain's ~442 µs/cycle remainder (~4 ms), `prepareFrame`'s non-copper part.

**STEP 1 PROGRESS (2026-09-24): 149.18 → 116.47 ms bracketed, −32.7.**
- ✅ 1.1 input: the MOS round trip → a direct Amiga answer + a reverse key map, **−4.04**
  (ph3 5.11 → 1.96 against the BBC's 1.0).
- ✅ the span plotters inlined into the walk, **−2.23** (the `noinline` rested on a
  fetch-contention argument a cache-less 68000 does not have).
- ✅ `normalise_for_divide`'s bit-at-a-time loop → a leading-zeros table + one shift, **−2.22**,
  found by the new PC sampler (below).
- ⭐ **`draw_road`, MEASURED BY SINGLE-STEPPING, and the deletion-arm prices were wrong.**
  `ROADARM=1..4` had put the walk + plot at 17.3 ms and the setup at BBC parity; a register walk
  that took every piece of walk state out of `mem[]` then moved ph11 only **−0.46**. The truth came
  from `amiga/steptrace.gdb` + `tools/steptrace_report.py` (single-step whole calls on the target,
  map every PC to a source line): **~550 instructions per real `interp_edge_core` call, 52% of
  them memory operands** — DDA step loop 132, setup ~157 (clip/endpoint 52, caps/pointers 49,
  patterns 30, deltas 19), plot 46, guard/entry 44 — ×42 calls a frame is the whole ~32 ms of
  `draw_surface_spans` (`ROADSPLIT` stage split: fill 3, spans 32, mark 1 of a 38 ms pass). The BBC
  does the same call in ~180 instructions / ~590 cycles: its zero page costs 3 cycles an access and
  a 1:1 `mem[]` mapping of it costs 12-20 — **the byte-at-a-time-in-memory paradigm loses by
  construction** (user's reading, and now measured). The deletion arm over-priced the walk because
  removing it also reshaped `interp_edge_core` around the setup — the RESIDUAL trap again.
  ✅ **landed: `span_walk_fast`, −0.46** (ph11 32.86 → 32.34, byte-identical: validate, all five
  determinism runs, per-circuit views) — the walk's state in locals behind a per-span footprint
  guard, the old walk kept as `span_walk_exact`; the fixtures now reach it (real `row_base` table;
  a walk climbing through page `$2F`, which also exposed and fixed an exact-walk gap).
  ✅ **landed: THE WALK + PLOT IN 68000 ASM, −4.67** (ph11 **32.26 → 27.77**, frame 132.90 →
  128.23, `SPANASM=0` the control). It runs ~79 instructions per real walk against ~222, and the
  gate is `make WALKCHECK=1`: 0 mismatches over ~7560 game spans on five circuits, plus a
  6000-case target-side fuzzer, because Silverstone never produces a steep span or a +1 step.
  Seven sabotages were caught, and the carry derivation was confirmed. docs/perf-method.md §the
  span walk in 68000 asm.
  ✅ **`interp_edge`'s own entry into the walk, −1.21** (ph11 27.77 → 26.78):
  - `span_walk_direct` hands the walk the deltas, end line, block, page, bh and marker switch it
    already holds;
  - it skips `span_walk_fast_ok`, whose page, step and marker tests are provably true on that path;
  - the destination is still checked.

  ✅ **the setup over locals, −0.34** (ph11 → 26.55): no read-back of a cell it has just stored,
  every store kept. Rule 1b's floor has arrived for C on this body.

  ✅ **THE WHOLE SPAN PASS IN 68000 ASM, −10.17** (ph11 **26.62 → 17.10**, against the BBC's 16.5 —
  parity once probe overhead is off). `draw_surface_spans`' loop, `interp_edge`'s setup and the walk
  are one routine (`span_pass_m68k.s`, `SETUPASM=0` the control). It stores every cell the C stores,
  span by span, so memory is byte-identical and no gate needed a re-record. The gate is `make
  SETUPCHECK=1`: all 64 KB compared a pass, 0 mismatches over 400 passes on each of five circuits
  and a 2000-pass fuzzer, and eight sabotages caught (two only by the fuzzer).
  docs/perf-method.md §the span setup in 68000 asm.

  ⏭ **NEXT — the rest of step 1, in this order:**
  1. **`view_paint_lines`** (ph24 20.20 + ph33 14.32 = **34.52 ms** against the BBC's 23.5 — now
     the biggest excess by far). ✅ **SINGLE-STEPPED** (docs/perf-method.md §view_paint_lines,
     single-stepped): ~23.7k instructions a sweep at ~10.3 cycles each, **29% the source scan
     (7.1k), 42% the low block (10.2k: 237 a line, of which the run fill is ~75 and the rest is
     per-run and per-line plumbing), 20% the full-width lines (4.9k)**. ⛔ Repainting the low block
     through the group painter now SHIPS (the needles are sprites, so there is no window): frame
     115.95 → 115.54, and re-traced the sweep is 23.8k instructions — **the full-width painter 45%
     (77 lines at ~140 a line, ~170 on the low block, whose ~2 events a line each cost the group
     byte arm ~45), the scan 35%**. ✅ **The terrain painter is in 68000 asm** (`terrain_m68k.s`,
     `TERRAINASM=0` the control): frame **115.51 → 110.13 (−5.38)**, ph24 22.12 → 20.55, ph33
     11.78 → 7.98. No masked merge after all — a split group is filled whole in the old colour and
     its tail overwritten. ✅ **The scan is in 68000 asm** too (`scan_m68k.s`, `SCANASM=0` the
     control): **ph24 20.55 → 18.00 (−2.55)**. The sweep now reads ph24 18.00 + ph33 8.14 =
     26.1 ms against the BBC's 23.5. Re-stepped after both: **18.4k instructions a sweep — the
     painter 6.9k, the scan 6.9k (72 hit calls, 41 seeds, 504 longwords), the drivers 2.7k**;
     both asm routines are near their store floor. ✅ **An unchanged line is not painted**
     (`69b4f51`): each buffer's row keeps the signature it was painted from, and 61% of phase-1 /
     86% of low-block lines match — **ph33 7.99 → 5.09, frame 87.86 → 84.44**; ph24 only −0.17,
     because phase 1's matching lines are mostly flat and were already cheap. ⛔ The drivers' C
     (2.7k instructions) is NOT a lever — see CLOSED. Re-stepped at 79.92 ms: **15.5k a sweep — the
     scan 6.8k, the painter 5.0k, the drivers 2.7k** — and the scan's hits then stopped being calls
     (per-group hit blocks, **ph24 17.69 → 16.11**, docs/perf-method.md), and `revs_plot_own_reset` stopped
     clearing `g_plotOwn` right before the template copy overwrote all 208 bytes (**−0.23**), and a run-B
     seed tests for an empty list against a `$FF` guard in the previous list's never-used last slot
     instead of computing its head (**−0.34**). What is left in ph24 is the
     scan's walk and record (closed §1, perf-method §THE CLOSED QUEUE ENTRIES: the price of not touching `draw_road`), the painter's signature
     compare (~196 word iterations a sweep at ~42 cycles — a longword compare is the next candidate)
     and phase 1's changing lines; then STEP 2 below.
  1b. ⭐ **`build_track_geometry` (ph5, 18.8 ms) is ~10.2k instructions a frame, single-stepped:
     `emit_edge_width_offset_core` 3.5k (27 calls × ~131), `road_edge_walk_run` 2.1k,
     `bearing_to_section` 2.0k (29 × 69), `project_point` 1.6k (28 × 58).** The emitter has a
     frame pointer, stack spills and a six-byte struct returned through memory, and its exit-V
     replay (`adc_overflow`, ~400 instructions a frame) feeds only the `$248B`/`$261A` hook seams,
     where the code's own argument says V is unobservable — compute it lazily in those cold
     paths (~1 ms). The coarse lever is the whole walk as one register-resident routine, as the
     span pass was (~9.2k of the 10.2k).
     ✅ **The emitter is in 68000 asm** (`emit_width_m68k.s`, `GEOASM=0` the control): **ph5 18.91 →
     16.13, frame 108.39 → 104.83**. V is the `add.w`'s own overflow flag (the 6502's high-byte
     ADC with the low carry in IS the 16-bit signed overflow).
     ✅ **The walk's whole point loop is in 68000 asm** (`walk_m68k.s`, `WALKASM=0` the control):
     bearing, hypot, running nearest, projection, the emitter (entered below its prologue) and the
     step, with the four bases and the camera/heading words in registers. **ph5 16.27 → 10.77
     (−5.50), frame 105.16 → 99.10** — about twice what the static count predicted. Subdivide and
     the `$248B` seam stay C. Gate: `make GEOCHECK=1` + `amiga/walk_check.gdb` — every walk C vs asm
     on all 64 KB, the four words and the exit X, plus a 1000-case fuzzer (diagonal, resume, cap);
     six circuits pass, six sabotages caught. ⇒ What is left in ph5 (~10.8 ms against the BBC's
     21.7) is `road_edge_start`, the subdivide and the tail — re-step before going further; the
     row is now well under the 6502's.
  2. **The port-only rows:** `prepareFrame` **3.09** (was 6.30, of which 1.71 was the front-end artefact above).
     Re-stepped at **1099 instructions a call**: the exact per-frame gap test 327, `snapshotBands`' copy
     and change detect 275, the two tyre outlines 220, the memoised `buildLineModes` 74, the needle
     paint 72 — five pieces of ~0.1-0.2 ms each, so it is closed until a coarser lever reaches it.
     ⛔ **The drain is NOT a lever**: its "excess" was
     the crash reset billed to it (now phase 63). A band cycle single-steps at 119 instructions
     (~170 µs; 236-434 on the rare cycles that run the game's IRQ chain), so the shipping drain is
     ~1.2 ms a frame; ph26 still reads 3.21 in a PROBES build because each tick crosses two brackets.
  3. ✅ **The tail** BEATS the BBC (2.18 vs 3.4) — the needle DDA was 2.7k instructions of it, now asm,
     and it does not run at all on a sprite-cache hit (the image is keyed on the DDA's entry state).
     **Sign/object** 3.86 vs 2.9 after its C was cleaned (4.66 before: the tree replayed the 6502's
     exit registers on every return and re-read its fill drivers per cell). ⛔ Not worth asm now:
     re-stepped at 1254 instructions an object, and what is left is `plot_view_src_line`'s per-call
     zero-page state (~0.13 ms if moved to locals) and the surface-colour lookup per empty cell
     (real work) — and STEP 2 retargets this plotter's output anyway. A race draws ~2.3 objects a
     frame, so a race gains ~2.3× what the practice baseline showed.
  4. **Then STEP 2**: the direct span-to-bitplane renderer that deletes the `$3000` intermediate.
     ⭐⭐ **RE-PRICED 2026-09-26 (`docs/span-render-plan.md` §13): the structure is SPENT as an
     incremental lever.** Its ~16.2k instructions a frame (~26 ms: seam fill 3.5k, scan 4.8k,
     painter 5.2k, drivers 2.6k) are each near their floor — the fill is 85% classification, the
     scan is the cheapest way to find ~150 events, the painter is at its store floor, the drivers
     are register code (a partial fold is already ⛔ CLOSED at −0.03). What is left: the seam fill
     as an exact per-line pass (~−1.3 ms, `determinism`-gated), a driver fold only if a carve arm
     shows >1 ms, and **one real design — the ANALYTIC LINE RENDERER (§13d, ceiling ≈ −5 ms)**.
     ✅ **Its feasibility is SETTLED on the host (§13e): the model "classifier colour + the block's
     byte on each on-line edge cell" reproduces the painted terrain with 0 mismatches over 1.16M
     cells (practice) and 21.2M (race proper), and on Donington, Snetterton and Oulton.** The one
     residual is a kerb-stripe join on Brands (one cell, 8 of 596 sweeps) — find the join rule or
     accept it (user's call). Before building, name the phase-23 producer and choose how the object
     layer is found without the scan.
- ✅ **THE TRUE 68000 RATIO (user decision), −5.77** — ph5 `build_track_geometry` **24.22 →
  18.83**. `bearing_to_section` and `project_point` each take `(S << 8) / L` in one `DIVU` of the
  real operands where the 6502 divided by a divisor truncated to its top byte (0..+2 above the
  true quotient), and project_point writes NOTHING else: no normalise, no shifted lanes, no
  `shared_temp_76` / `math_lo`, no mantissa/exponent float. The two width routines divide by
  `point_dist` themselves (`2^(22-k)/D`, `$2000/D`), keeping the 6502's own computation only for
  the points beside the car where its shift overflowed. (`scale_shape_vectors` was never a
  consumer — it reads `object_width`.) Gated by `validate`'s new TOLERANCE mode
  (docs/validation-harness.md §THE TOLERANCE MODE — the twin exact to the true ratio, the oracle
  within the 6502's measured error), the def-use reader audit (`make rangeaudit DEFUSE=1`, all five
  circuits) and `viewdiff`: HEAD was 0 gated bytes on every circuit; now 1/5/17/0/3, **every one an
  edge transition displaced along its own line** — 20 of 26 lines by one pixel, Donington's
  shallow kerb at 113..119 by one scan line (2..6 px on a ~3:1 slope). That IS the accepted
  ±1 LSB, written up there; the five determinism baselines were re-recorded (only view geometry
  moved — the edge arrays, their scratch, a few view-source bytes; no car state).
- ⭐ **Two new instruments drive the rest of step 1:** `make bbcprof` (per call site and, with
  `--flat`, per function, on a real BBC) and `amiga/pcsample.sh` + `tools/pcsample_report.py`
  (a statistical PC sampler on the target, by function and by source line).

**STEP 2 — BEYOND PARITY: ~90 → 48 ms.** Needs the view pipeline (61.7 BBC ms) at ~3× the 6502,
which is past the 2.3× the best native row demonstrates — so it cannot come from code shape, only
from deleting work the 6502 did *because of its own architecture*: the `$3000` source-block
intermediate between `draw_road` and `view_paint_lines` exists to serve the BBC's screen layout,
and with the car on its own playfield and no `mem[]` decode the constraints that closed the old
direct-plot attempts (§CLOSED: `SPANPAINT`, the source-event consumer) have changed. **Re-price it
with numbers after step 1, not before** — step 1 moves every denominator it depends on.
⚠⚠ **RE-PRICED 2026-09-24, AND IT IS SMALLER THAN "the $3000 intermediate" SUGGESTS: ~6-9 ms net,
not ~34.** `draw_road` (17.1 ms, at BBC parity) is the edge RASTERISATION — 43 spans stepped per
line plus the pixel-precise boundary bytes, which any renderer must still compute; the scan is asm
and ~6 ms, and producer-side notes would move its ~160 recordings a frame into the producers plus a
per-line sort (~40k cycles against its ~44k — the SRCEVENTS closure still holds in substance). What
is purely intermediate is the source-block traffic, `fill_dash_edge_columns` (6.5 ms; dropping its
fill outright is ⛔ wrong on all five circuits) and `clear_surface_buffers` (0.9) — ~13 ms gross,
and replacing it must carry §12b's same-cell composition. ⇒ the geometry walk in asm (entry 1b)
goes first (user decision).
⛔ **The blitter was measured as the consumer (2026-09-25) and CLOSED at +5.25 ms** — the blits are
nearly free, making the toggles is not, and the skipping painter left it nothing to take (§CLOSED).
⇒ STEP 2 continues on the PRODUCERS (user decision). `draw_road` (ph11 **13.99**, against the BBC's
16.5) single-steps at ~11.5k instructions a call before the two cuts below: the span-pass asm 65%,
the walk 16%, `fill_line_attr` 12%, `mark_line_surfaces` 3.5%. ✅ `fill_line_attr`'s walk over
locals with pointer-run fills, **−1.20**. ✅ The span pass no longer stores twelve 6502 working cells
nothing outside it reads (reader audit on five circuits, SETUPCHECK masks exactly those), **−1.00**.
Left in the pass: the cross-span cells it does read back (`Z_CLIP`, `Z_X77`/`Z_X7E`, `Z_CURSOR`,
`Z_LINEEND`, `Z_SWAPPED`, `Z_NEARIDX`/`Z_FARIDX`, `Z_ARM`, `Z_CAPPEND`, `Z_STYLEIDX` — ~10 stores
and ~10 reloads a span); holding them in registers across spans needs the cap's C to take them as
arguments — ⚠ and a free register: the walk already holds every one, so a cell moved to the stack
frame costs what the `(d16,a5)` operand did. Only the redundant reload/copy pairs (the publish's two
mem-to-mem moves, the `Z_CLIP`/`Z_ARM` reloads) are a saving — estimated ~0.2-0.3 ms, below the
~1 ms bar. ✅ `fill_line_attr` un-unrolled (`optimize("no-unroll-loops")` — the pragma cannot reach
the loop GCC synthesises from its `continue` arms), 750 → 201 instructions, **−0.15**.
`build_track_geometry` (ph5 **10.02**, against the BBC's 21.7) single-steps at **6004 instructions a
frame and is ~88% asm**: the walk 54%, the width emitter (asm) 31%, `road_edge_start` 7%,
`bearing_to_section` and the core ~4%. ✅ The walk no longer stores eight 6502 working cells a point
(`$80/$82/$83/$87`, the arctan `$7E`, the far hypot's `math_lo/hi`, the stride) — five-circuit
reader audit in `walk_m68k.s`'s header, GEOCHECK masks exactly those — **−0.58**. ⚠ The audit found
five cells the walk leaves that are read one frame on and must stay: `$86`/`$88` become the first
span's `SPAN_ARM`/`SPAN_CLIP`, `$85` feeds `plot_view_src_line`, `point_dist_hi` feeds
`note_object_contact`, `$8D` feeds `road_edge_start`. What is left in ph5 is asm doing real work
plus ~0.7 ms of `road_edge_start`'s C. `fill_dash_edge_columns` carries a written do-not-retry.
⇒ **the producers are down to real work; the next lever by size is the CONSUMER, ph24 17.69 (15.51 after three scan/reset cuts).**

⇒ **The last 8 ms (48 → 40) is where the `50/N` ladder steps to 25 fps; not planned until 48.**

⭐ **And `ph26`+`ph29` (the 50 Hz drain) SELF-HEALS as the frame shrinks** — the body is a 50 Hz
tick, so a 154 ms frame drains 8.78 of them and a 40 ms frame would drain 2. Never count it as a
target; never count its disappearance as a win either. ⚠ Its old 12.47 ms also held the crash
reset (phase 63 now) — the real drain is ~1.2 ms a frame at ~100 ms.
⭐⭐⭐ **THE DEFAULT NOW CARRIES THE WHOLE STACK — `DUALPF`, `TYRESPRITE` and `LOWOWN` are on, and
there is no per-frame `mem[]` → bitplane conversion left (closed §5b, perf-method §THE CLOSED QUEUE ENTRIES).** Against the previous default
(`DUALPF=0 TYRESPRITE=0`, which turned ownership of 117..157 off with them) that is
**179.83 → 153.78, i.e. −26.05 ms**, of which `ph27` is 32.82 → 8.52. A plain `make clean && make`
reproduces the 153.78 arm bit for bit.
⚠ **Compare arms as Σ(1..39) − ph28**, never as the raw total: the vblank spin absorbs a compute
win (CLAUDE.md), and a faster build paints more frames so it meets more of phase 0's crash holds.
**Target is ~48 ms** (2× the original game; stretch **40 ms**, where the `50/N` display ladder
actually steps to 25 fps), and an entry worth under ~1 ms is not where the answer is.

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

| ms/frame | phase(s) | what | split instrument |
|---:|---|---|---|
| 34.47 | 11 | `draw_road` — 84% the span rasteriser | `ROADSPLIT=1` ✓ current |
| 26.67 | 5 | `build_track_geometry` — 94% the two distance walks | `GEOSPLIT=1` ✓ current |
| 20.58 | 24 | `view_paint_lines` phase 1 — the scan | `VIEWSPLIT=1` ⚠ **predates DUALPF/LOWOWN** |
| 15.59 | 28 | the vblank spin — the `50/N` pad, not a target | — |
| 14.76 | 33 | ...phase 2, the low block | ⚠ **never split since `LOWOWN`** |
| 8.05 | 26+29 | the 50 Hz drain — 8.96 body ticks a frame, ~900 µs each | `BODYSPLIT=1` ✓ new |
| 8.53 | 27+30 | `RevsScreen::prepareFrame()` — all that is left of the decode (closed §5b, perf-method §THE CLOSED QUEUE ENTRIES) | `DECODESPLIT=1` ✓ current |
| 5.93 | 18 | `fill_dash_edge_columns` — ⛔ see CLOSED | — |
| 5.65 | 32 | ...the consumer's tail | ⚠ with 24/33 |
| 5.11 | 14+15 | `build_road_sign` + `draw_track_object` — ONE billboard | — |
| 5.10 | 3 | `read_driving_controls` — 7 MOS key tests; **done, closed §7** | — |
| 3.61 | 4 | `apply_driving_model` — real 6502 arithmetic, the BBC paid it too | — |
| ~7 | rest | twenty rows under 1.3 ms each (closed §7) | — |

⭐⭐ **WHICH SPLITS ARE WORTH RE-READING, and it is not the big two.** The producers were not
touched by the ownership campaign and their splits still describe them; the CONSUMER's do not —
`VIEWSPLIT` was last read before the cockpit became a playfield and before `LOWOWN` moved the
boundary cells out of the terrain painter, and its 41.0 ms is the block §12's directive says should
now be *simple*. ✅ `ph26`+`ph29` HAS one now (`BODYSPLIT=1`) and it paid at once: the reuse
gate was **99 cycles per byte** and is **−4.36 ms**. What is left there is the ~442 µs/cycle
remainder.

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

### 2a. ⭐⭐⭐ ROWS 117..157 — MEASURED AT −1.92 ms AS BUILT, AND THE REST IS THE PAINTER'S SHAPE
⭐⭐⭐ **MEASURED 2026-09-22 (`docs/span-render-plan.md` §12f): the decode gives −4.59 ms and the
painter takes +2.54, net −1.92.** So the row-ownership half of this entry is DONE and behaves
exactly as §12c's ceiling predicted; what is left is not ownership at all but the terrain
renderer's SHAPE — it still paints two runs clipped to the car's silhouette with a composed
boundary cell each, ~4 cells a segment, and `LOWDOUBLE=1` puts 22.06 of ph33's 24.63 in that
painting. The dual playfield licenses one contiguous fill a line instead (§12f-i), which is the
next step and is worth more than everything above it.
⭐⭐⭐ **BUILT AND MEASURED AT −4.76 ms (§12f-iii)**: the four composed boundary cells and the two
clip lookups left the terrain painter and their dash pixels became the cockpit layer's, out of the
same static mask tables — ph33 17.08 → 14.71, ph30 0.95 → 0.72, Σ(1..39)−ph28 **157.31** against
the control's 162.07. `LOWOWNCHECK=1` reads 22656 checks / 0 mismatches and `DUALPFCHECK=1` 0/32.
⛔ Do NOT reach for "paint all forty cells as if the car wasn't there" — built and measured, it
LOSES (+4.86 ms of extra cells against ~2.3 of saved machinery), and a longword fill loses a
further 5 ms at ~5-cell segments (§12f-ii).

✅ **ALL FIVE RECTANGLES ARE CLOSED and every cell of 117..157 has exactly one owner**: the
terrain runs are the PF1 painter's, everything else the cockpit layer's — including the dial art
under the needles, which are prerendered SPRITES since 2026-09-24 (§12d) — the wheel art static
under `TYRESPRITE=1`, and the WING MIRRORS painted onto PF2 by `mirror_draw_car`'s own store site
(`REVS_COCKPIT_BYTE`, §12f-iv — the PF1 delta domain was the wrong home because its base and its
oracle both re-expand a `mem[]` that is frozen for those rows' terrain cells).
✅ **And the writer set is provably complete**: the object plotter is a PRODUCER — it composes into
the `$3000` source blocks, not the frame buffer — so an opponent reaches the screen through the
same painter as the terrain (§12f-iii). The practice-only census was not the risk it looked like.

✅ **THE DEFAULT HAS FLIPPED (2026-09-22, user's call): `DUALPF`, `TYRESPRITE` and `LOWOWN` are
ON in a plain `make`**, `LOWOWN` derived from the other two so every control arm (`DUALPF=0`,
`TYRESPRITE=0`, `NEEDLE=0`) still builds instead of hitting a wall of `#error`s. Worth
**−26.05 ms** against the old default once the conversion deletion (closed §5b, perf-method §THE CLOSED QUEUE ENTRIES) is counted with it.
⏳ **WHAT IS STILL OWED IS THE TARGET-SIDE RACE**, and `RACEPROPER=1` is plumbed into
`amiga/Makefile` for it. ⚠⚠ MEASURED: it is far longer on the target than the host
figure suggests — qualifying ends on `tick_race_timers`, which advances once per GAME FRAME, and
this port paints ~6 a second, so the host's ~12000 frames are ~100 000 FIELDS here
(`session_is_race` still read `$28` at vbi 15200). Budget ~10 minutes of warp. What it would
exercise: the mirrors with a real reflection (`g_cockpitDeltaBytes` reads 26 in a practice lap —
the path fires, the content is barely stressed) and the object plotter with a real field (5a).
⚠ **The flip includes a FAITHFULNESS-SEAM default** — the front-wheel dither is now drawn by two
hardware sprites rather than plotted into the frame buffer (§12's own directive). The pixels are
the game's; what changed is who puts them on screen. `make TYRESPRITE=0` is the control.

⭐⭐⭐ **UNBLOCKED, 2026-09-21: the cockpit is now its own PLAYFIELD (`make DUALPF=1`,
`docs/span-render-plan.md` §12e).** The car body used to belong to nobody once the sweep claimed
these rows — the terrain painter clips to the silhouette and never paints inside it — and the
player saw the cockpit alternate between complete and incomplete every second painted frame.
With the car on PF2 the terrain layer's car cells are DON'T CARE.
⚠ **The entry fee is three retargets, and all three already exist in the right shape.** PF2
carries only the STATIC car; what moves inside 117..157 is left transparent and comes from PF1
today, which stops working the moment PF1 holds road: the rev-counter / steering mark must come
from `NEEDLE=1`'s geometry, the front-wheel dither from `TYRESPRITE=1`'s sprites, and the wing
mirrors from a painter of their own — each writing PF2 instead of PF1.
⚠⚠ **`make DUALPFCHECK=1` is the gate that says when it is done**: it composites the two
playfields and requires the BBC pen back, so a strip that stops arriving shows up on the frame
it moves.
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

### 2c. ⚠⚠ THE OTHER CARS — **~21 ms of a RACE frame (24.7 before the object asm), MEASURED**, and every baseline hides all of it
Phase 17 (`move_and_draw_cars`) reads **0.21 ms** in every practice measurement because
`STRAIGHT_TO_RACE` is a practice session and the player is alone on track.  Measured in the race
proper on the target (2026-09-29: `make PROBES=1 FIXED_RNG=1 RACEPROPER=1 PROBERACE=1
PROBEFIELDS=3000 SIMLEGACY=1 CARSPLIT=1` + `phase4_prof.gdb`, `diag_run.sh 1900` under warp; the
window opens at the race start, `raceOpen=` non-zero, 3000 fields, 523 frames, throttle held):

| row | job | ms/frame | now (2026-09-30, object asm, `QUICKQUAL=1`) |
|---|---|---:|---:|
| 43 | `draw_car_field` — the object plotter | **13.55** | **9.97** |
| 40 | `drive_other_cars` — the per-car AI, 19 cars | 4.01 | 4.15 |
| 42 | the six `stage_nearby_car` → `place_car_world_coords` → four projections each | 3.80 | 3.67 |
| 41 | `check_car_pair` — the overtaking pass | 2.80 | 2.83 |
| 44+17 | the car ahead's staging + the entry | 0.54 | ~0.6 |

The race frame is **~101 ms** (`Σ(1..44) − ph28`) against practice's 75.10, so **the standing
baseline understates a real race by ~25 ms** — five times the ~5 ms the host-census estimate said.
The road sign (phases 14+15) shares the object plotter, so the asm below took its ph15 3.08 → 2.16 too; its C was at its local optimum (§the object plotter in
`docs/perf-method.md`; `scale_shape_vectors` ~0.69 ms at 765 cyc/vertex).
⭐ **Priced per object**: a host census over the same first 523 race frames (a temporary counter at
draw_track_object's plot, `RELEASE=1 RACEPROPER=1 HOLD_THROTTLE=1`, FIXED_RNG) plots **3.28 objects a
frame, 2.37 of them cars** — so row 43 is **~4.1 ms, ~29 000 cycles, per drawn object**, the road
sign's own 3.63 ms/object within the difference in shapes.  ⇒ The plotter is not slower in a race;
a race simply draws ~3x the objects, and the lever is the per-object cost that §the object plotter
already sized (vertex scaling ~4.9k cycles, the edge chain ~18k).
⭐⭐ **SINGLE-STEPPED (2026-09-29, `amiga/race_steptrace.gdb`: three whole `move_and_draw_cars_steps`
calls 150 frames into the race, a busy stretch at 9 objects a frame):** 28.5k instructions a frame,
53% memory operands.  **The draw is ~1 950 instructions an object** — `plot_view_src_line_core`
6.2 calls an object at **156 instructions a call** (entry/mode state ~70, the two fills ~45, the
surface lookup ~12), `column_gap_walk_core` 3.1 calls at 134, `scale_shape_vectors` 240,
`plot_shape_edges` 213 — ~71% of it the line side.  **Every hot line is 6-8 instructions a call,
i.e. one byte RMW on a zero-page cell: there is no fat line, the C is at Rule 1b's floor** (and
~13-15 cyc/instruction, which reconciles 1 950 instructions with the measured ~29k cycles/object).
✅ **BUILT: `plot_view_src_line` IN 68000 ASM, ph43 13.61 → 11.52 (−2.09) and ph15 3.04 → 2.65 in
the race; frame 100.44 → 98.15** (`src/platform/amiga/object_m68k.s`, `make OBJASM=0` the control,
PROBERACE arms at 3000 fields; practice ph15 2.64 → 2.33).  It stores every cell the C stores, so no
record moved.  Gate: `make OBJCHECK=1` + `amiga/obj_check.gdb`, all 64 KB a call: 1502 race calls
(all three modes) and a 400-case fuzzer at 0 mismatches, four sabotages caught (the classifier's only
by the fuzzer; the zero-byte `$55` only by the game until the fuzzer's tables were biased to zero).
✅ **AND THE GAP WALK, −3.06 ms MORE — ph43 11.80 → 9.97, ph15 2.62 → 2.16, frame 98.78 → 95.72**
(three `QUICKQUAL=1` arms, one trajectory: C 100.62 / line side 98.78 / + gap walk 95.72).
Re-single-stepped after the first step: the asm line side ran ~122 instructions a call against the
C's 156 — its fills average <2 cells, so it is per-call decision logic and won on cheaper operands —
and the C gap walk ~118 a call, almost all setup (marshals, three `walk_stores_are_private`, a
`zp_pointer`, an `adc_overflow`).  So the asm walks it inline in the configuration the object path
always meets (branch $09, store through `plot_ptr` — what `fill_dash_edge_columns`' pass A leaves)
and hands any other patch state to the C walk.  Gate: `make OBJCHECK=1` again PASS in the race (1502
calls, all modes), the fuzzer now varies both patch operands; sabotage 5 (the fallback colour
dropped) caught by game and fuzzer.
**Re-traced with both steps in (`QUICKQUAL=1` build, 150 race frames in, 9 objects a frame):**
the draw is ~1 520 instructions an object — `pvs_line_m68k` with its walk ~920 (~150 a call, per-call
decision logic: the fills average <2 cells), `scale_shape_vectors` ~240, `plot_shape_edges` ~215,
`draw_track_object` + `plot_object` ~110, `fill_object_gap` ~30.  **What is left in §2c is small:**
asm for the two C routines would be ~1 ms of an average race (3.28 objects), the line side's own
per-call logic is already asm, and rows 40-42 are ordinary per-car work (~8k instructions a frame).
⇒ **§2c now ranks BELOW the practice-frame entries**; take it up again only if a design reduces the
CALLS (≈6 line-side calls and 3 walks an object) rather than their cost.  ⭐ **And rows 40-42 are NOT fat**: `drive_one_car`
is **113 instructions a car** (19 a frame), `check_car_pair` ~1 600 a frame, staging ~200 a
`place_car_world_coords` — ordinary per-car work at ~13 cyc/instruction, so they are the game's
own cost and rank below the draw.

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

### 9b. 🧹 THE RENDERING-PATH REVIEW (user, 2026-09-26: "a full review of all the functions on the in-game rendering path … Implementation details don't need verification, results do")
Method: one whole loop iteration single-stepped (54.5k instructions, `tmp/price/steptrace_frame.gdb`
shape — break at `build_track_geometry_native`'s EXACT entry, `break *fn`, and step to its next entry;
`break fn` stops after the prologue, where `*(sp)` is not the return address), attributed per SOURCE
function with inlines (`addr2line -f`), plus `make fatscan --profile` for the average over a window,
priced in two worktrees so the tree stays editable. ⚠ ISR work that lands in the vblank spin is
invisible to Σ(1..39)−ph28 — read `VERTB ISR … us each` and the painted-frame count instead.
**Done:** MOS round trip for ADVAL + SOUND/ENVELOPE (−0.36), `buildBands` memo (ISR 1335 → 1002 µs a
field), the four edge buffers' clear in one loop (−0.33), the PF2 tyre outline gated (−0.20); second
pass: the ISR 1002 → 671 µs a field (sound quiet flag, per-channel Paula memo, `present()`'s needle
words — frame −1.07, docs/perf-method.md §The VERTB ISR), `fill_line_surface` four lines a step (ph13
0.86 → 0.34), `check_crash`'s on-track arm stopped publishing the model state it did not touch (ph23
−0.16).  Measured NULL and kept only as cleaner C: `keyDown` flattened (ph3 ±0.01),
`mark_line_surfaces` over locals (±0.00 — GCC was already caching the zero-page cursor).
⚠ The measured build polls the keyboard through the AUTORUN harness (`AutoRun::keyDown` +
`pressBbcKey`, ~310 instructions ≈ 0.5 ms a frame in ph3) — overhead a player never pays, so it is
not a lever and a cut there would not be a win.
**Open, ranked by what is left:**
- **The object plotter tree** (ph15+16 ≈ 3 ms practice, ~5 ms more in a race): single-stepped at ~1280
  instructions a `plot_object_core` call, two calls a frame. Half of `plot_view_src_line_core` is its
  entry state in zero-page cells (`PVS_*`, `EDGE_COLUMN`, `shared_temp_*`, `span_defer_pending`); the gap
  fill goes through the generic `column_gap_walk` with `plot_ptr` byte-lane marshals, `zp_pointer`,
  `walk_stores_are_private` and an `adc_overflow` replay. ⭐ READER AUDIT DONE (`make rangeaudit
  DEFUSE=1`, five circuits, cells $2A-$2B,$35-$37,$47-$48,$70-$8F,$628F-$6292,$62F3,$62FD, the shape
  tables): nothing outside the object set reads a value the object path wrote, EXCEPT the colour
  pattern table $628F-$6292 (`interp_edge+240` reads it next frame — keep) and `$74`, whose only
  outside reader is the practice busy-delay's `DEC $74` ($262F), which the port does not run. ⇒ the
  whole tree's zero-page working state may go to locals under a scoped `set_ignore` + a determinism
  re-record. Gate: `validate` of the tree's twins + the five determinism trajectories + `viewdiff`.
  ⚠ SIZED FIRST, AND IT IS SMALLER THAN THE CELL LIST LOOKS: `plot_view_src_line_core` holds only ~75
  zero-page operands in 851 instructions — most of its memory traffic is the inlined surface
  classifier's six tables (seven copies) and the fills, i.e. real work — so taking the state out of
  mem[] is worth ~10-20% of the ~2.8 ms, against six fixtures and a re-record. Wholly dead 6502
  stores on the path (`DEFUSE=1`'s new DEAD STORES list, Silverstone): `$1D31` ($48), `$1FE0` ($2B),
  `$202C`/`$2040` (scale entries 2/7), `$20A5` ($8C), `$2AF6` ($74).
- **The VERTB ISR** (671 µs a field, 3.4% of wall clock): what is left is per painted frame (the
  screen slot) or real work (a flush's restart) — docs/perf-method.md §The VERTB ISR.
- `model_state` marshals: one IN (ph3, load-bearing — a needle can land in `$62D0..$62EE`) and one
  OUT (ph4, ~93 instructions ≈ 0.16 ms) a frame remain.  The OUT's one twin reader is
  `advance_player_section_core` ($62E2); moving it to `model_state_16` retires it, needs a
  determinism re-record.
- `view_stops_rescan` (~0.3 ms a sweep): stays — circuits patch the chain page's unit slots.
- Near their floor, not levers: the terrain painter (its signature compare saves more than it costs),
  the scan (after 9128841/12b6837), `step_scanline` and the drivers (⛔ CLOSED), `snd_tick`'s body,
  `band_inputs_unchanged`, the section coordinate planes (a representation change —
  docs/wide-value-cleanup.md's null result).

### 9. 🧹 THE 6502 RESIDUE `make fatscan` FINDS — ~960 → 608 target instructions a frame (six batches done)
`make fatscan` (tools/fatscan.py; method in its docstring) ranks four detectors by target instructions
per frame: host `--coverage` executions over frames 20..220 of the driving scene × the plain ELF's own
instructions.

**Done**, commits `6d70795..b1200fe`. Totals went **exit 245 → 84, flag 3 → 0, marshal 591 → 401, zp
123**. The pattern for every item:
- the core stops computing an exit field no native caller reads;
- the oracle-facing shim rebuilds it exactly from state (the integrators' `add_flags_between`, the
  object slot writers, the contact test);
- or, where no 6502 caller reads it either, the fixture stops comparing it, with the reader audit
  written at the mask.

Six of the ten `adc_overflow`/`sbc_overflow` replays are gone, including the whole road-pass V chain
(it reached only hook seams, and no hook reads V). The model-state marshals walk by pointer, which
removed GCC's SLP packing. `build_road_sign` got a `_native` split below its wipe-only INs. Two
latent defects surfaced on the way:
- each road fill's entry carry at the `$1946` hook seam is now the clamp's CMP, as on the 6502;
- `note_object_contact`'s fixture never reached the threshold == distance boundary.

⚠ **`race_main_loop`'s 6502 reads are LIVE hand-offs, not moot.** Its native twin calls the same
shims in the same order and threads `cpu` between them. So an exit register may be dropped only
where the native loop calls the CORE, or where the next shim provably reads nothing: fatscan tags
those reads `(main loop)`.

**What remains, and why each is not low-hanging:**
- **marshal 401**, three kinds:
  - `read_driving_controls_frame`'s model-state IN (141) and `draw_dash_needles`' car-angle IN (28)
    import what a needle line may have written into `$62A0..$62EE`: that is faithfulness. They go
    only if the needle plotter updates the arrays itself when a pixel lands there.
  - The multi-tenant `hypot`/`bearing` round trips in `build_road_sign_native` and
    `build_track_geometry_native` (96) are load-bearing (docs/wide-value-cleanup.md).
  - The `_out` publishes are the `mem[]` mirror determinism compares.
- **exit 84:**
  - `emit_edge_width_offset_core` (55) is a FATSCAN ARTEFACT: the host runs the C core, the target
    runs `emit_width_m68k.s` and calls the C core only on its rare arms. ⚠ fatscan prices a line by
    host executions, so any C the target replaces with asm while keeping the C for a fallback reads
    as if it ran.
  - `update_camera_and_height`'s CameraExit (19) also carries the `$45CB` per-circuit hook's
    register state.
  - `engine_sound_update`'s exit is a main-loop hand-off.
- **flag replays left (4):** the `fill_dash_edge_columns` family (`column_gap_walk`, `edge_run_flat`,
  `edge_runs_asm_raw`): its V leaves through a shim the native loop calls, into
  `engine_sound_update`'s entry, so it needs a whole-loop audit. `hook_merge_horizon_edges` sits at
  a hook seam itself.
- **zp:** `fill_line_attr_core`'s per-point `shared_temp_76` / `$82` / `span_line_cursor` stores are
  gone — the walk holds them in locals and stores each once (`fa7d3a7`, byte-identical).
- **PRICED (2026-09-26): −1.54 ms bracketed field-matched (84.80 → 83.26, `55926fe` vs `8d8a45b`),
  −1.15 frame-matched** (the protocol's field cap lets the faster arm reach a cheaper stretch of
  lap — docs/perf-method.md §bound the window). By row, frame-matched: `draw_road` (ph11) −0.81,
  `build_track_geometry` + `place_player` (ph5+6, the latter byte-identical code) −0.13,
  `build_road_sign` (ph14) −0.13, `apply_driving_model` (ph4) −0.10, `read_driving_controls` (ph3)
  −0.06. The static count said ~350 instructions ≈ 0.5 ms; the phase table paid about twice that —
  [INFERRED] because the deleted instructions were mostly memory operands (16-20 cycles, not ~10).

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

### 🔧 The naming pass's residue (`docs/static-map.md` §Open items 5)
Still unnamed: **the interior of the 3D pipeline below `project_point`, the front end's prompt
chain, and the other cars' AI.** Does not gate anything — `symbols.csv` feeds the transpiler, so a
name learned later propagates on the next `make gen`.

### 🅿️ Comment condensation on `src/gen/revs_native.c` — **PARKED BY THE USER**
1.1 MB, mostly very verbose comments. The user parked it explicitly ("Leave comment condensation
for later and continue on other unfinished business"). **Do not start it unasked.**

---

## ⛔ CLOSED — measured dead ends, one line each. Do not rebuild these.
- ⛔ **§7, the twenty never-profiled small phase rows: every row read, nothing left to build** (2026-09-30) — single-stepped: `build_road_sign` 545 instructions a call spread over eight callees (1.10 ms, ≤0.3 available); `engine_sound_update` 108 a call (557 on the one call in ten that issues an OSWORD); `advance_player_section` is real section building; ph15 went to asm under §2c.  ⚠ Each small row also carries ~0.12 ms of probe transition, and ph3's 1.92 ms is ~80% AUTORUN scaffolding (963 instructions a call against the shipping ~247) — the practice baseline over-reads the game by ~1.4 ms there.  The entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **the TRANSPOSED SCAN (10.00 ms): both routes to it measured and closed** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **the view sweep's DRIVER code: every candidate closed; an objdump delta over-reads this driver ~6x, and §12 deleted phases 2+3** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **the source-block reader audit: DONE (`make srcaudit`, span-render-plan §12b)** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **rows 158..191, the dashboard: DONE, default on (`NEEDLE ?= 1`)** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **rows 117..157 under the pre-§12c model: superseded by §2a** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **the frame-buffer -> bitplane conversion: a cold-start path only; what is left is the copper's band plan, not a decode** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **the pre-§2a framing of the decode: superseded** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **the real BBC profiled per routine (`make bbcprof`): DONE, it ranks everything** — the entry's full text is `docs/perf-method.md` §THE CLOSED QUEUE ENTRIES.
- ⛔ **MODE 7: the TITLE page's one-shot first paint, 78 ms** (user, 2026-09-29: "78 ms is fine") — 146 → 89 → 78 ms from the asm row painter and the four-cells-a-longword pass; the page has no blank rows, so the blank-row skip cannot touch it.  Levers if it is ever reopened: the decoder in asm, the blitter clears issued from the asm, or painting during the load.
- ⛔ **MODE 7: a blank glyph's key as its background alone** (2026-09-28) — painted cells 1836 → 1588 over the menu walk, paint **838k → 968k colour clocks**: the per-cell blank lookup it adds to every VISITED pair costs more than the pairs it skips. Only a scheme whose skip test is already paid (the blitter clear above) can use it.
- ⛔ **MODE 7: a whole display row painted by the BLITTER** (2026-09-28) — the CPU staged a 10-line glyph mask + six pen lines in chip RAM and three `blitterCombineWithMask` blits coloured the row (D = glyph ? fg : bg); byte-exact (TTCHECK 0) and the worst walk page went **1026 → 2142 lines**, the title page 2287 → 2280.  The blits (~9.5k cycles a row) do not overlap the CPU — one bus — and the staging alone was ~26k cycles a row, so it breaks even only at ~15 glyph pairs a row.  The blank-run CLEAR pays because it replaces work outright and is D-only.
- ⛔ **MODE 7: passing the pair painter scalars instead of `TtCell`s, with a separate blank-fill routine** — 852k → 838k colour clocks (inside the noise); the call is not the pair's overhead.
- ⛔ **THE TERRAIN BY BLITTER AREA FILL** — exact (0 mismatches over 616k cells, seven sabotages
  caught) and **+5.25 ms** (84.43 → 89.68), in both shapes: a C toggle writer over the event lists
  (+12.3) and the toggles written by the scan itself (+5.25). The blits are nearly free — dropping
  the fill moved the frame 0.30 ms and the clear 0.07 — so the loss is the CPU work of making the
  toggles (~20 instructions a source hit, ~38 a line for the entry fixup), which exceeds the asm
  painter's 6.87 ms because that painter's unchanged-line skip already paints only ~30% of lines.
  The blitter only pays if the PRODUCERS emit toggles and the scan goes too. Patch:
  `tmp/blitter_terrain.patch`; docs/perf-method.md §the terrain by blitter area fill.
- ⛔ **THE SWEEP DRIVERS' C — the scan-line pair in locals, the `$7EEE` terminator hoisted** —
  −0.03 ms (87.93 → 87.90; ph24 +0.04, ph33 −0.06), validated and reverted. The trace put ~2.7k of
  the sweep's 17.3k instructions in the drivers, but deleting ~10 a line of global RMW paid
  nothing: the drivers now only RECORD a row address and a byte, and the time is in the two asm
  kernels. (The objdump-over-reads rule, closed §1b in perf-method §THE CLOSED QUEUE ENTRIES, once more.)
- ⛔ **THE LOW BLOCK THROUGH THE GROUP PAINTER WITH A NEEDLE WINDOW AND A PER-LINE SEED WALK** —
  +5.74 ms (ph33 14.32 → 20.06). The window variant (~246 instructions a line) and walking each
  line's list to insert run B's entry (~96 a line) were the loss, not the idea: with the needles on
  sprites (no window) and the seed appended by the scan, the same routing SHIPPED at −0.41
  (docs/perf-method.md §view_paint_lines, single-stepped). Do not rebuild the windowed form.
- ⛔ `span_walk_fast` in C, three shapes against the inlined 16-bit-index one (−0.46): one
  out-of-line copy per arm **+0.63** (register pressure inside `interp_edge_core` was the wrong
  mechanism), the same unrolled 8 columns **+0.39** (3014 instructions, 683 stack operands), real
  68000 pointers instead of `mem[]`-base + index **−0.26** (five address registers beat one base).

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
