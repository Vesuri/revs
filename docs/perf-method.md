# Performance method — how to get a number you can trust

> ⚑ Method inherited from the Atari port (*Rescue on Fractalus!*), where each rule below was
> learned by getting a number wrong first. The *numbers* are Revs's own. Companion docs:
> `docs/m68k-optimisation.md` (what to do once you know where the time goes),
> `docs/headless-fsuae.md` (how to drive the target), `docs/direct-bitplane-plan.md` (the
> representation-level plan the current table points at).

## ⭐⭐ THE PHASE-SHARE PROFILE — the exact recipe, so you never re-derive it

**Question it answers:** where does one painted frame's time go, function by function. SHARES
within one run only — never diff a row across builds (Rule 2). This is a copy-paste recipe; do
NOT reverse-engineer the wiring each time.

```
cd amiga && . ./env.sh
make clean && make -j4 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1
EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 30
cat .run/gdb-out.log          # ⭐ READ THIS FILE, not the terminal output
```

Four things that are already handled — do not go looking for them again:

1. **The car is MOVING.** `PROBES=1` *already implies* hold-the-throttle (`REVS_PROBE` →
   `AUTORUN_HOLD_THROTTLE`, `src/platform/autorun.cpp`). You do NOT need `FPSCOUNT=1` (it disables
   the probes) or `HOLD_THROTTLE=1` (redundant). `STRAIGHT_TO_RACE=1` is what boots into the race;
   without it the run profiles the menu.
2. **`make clean` is mandatory before any PROBES build** (CLAUDE.md) — the Amiga Makefile tracks
   neither defines nor the PROBES toggle, so a partial rebuild links a working-but-wrong binary.
3. **Read `amiga/.run/gdb-out.log`, never the terminal.** `diag_run.sh` ends with `tail -40`, which
   silently drops the header — the accounted-% sanity line, phase 0, `body ticks/field`, and phases
   1–4. The full gdb output is redirected to `.run/gdb-out.log`; `cat` that.
4. **`--warp_mode=1` changes no number** (all figures are ratios of emulated quantities) and runs
   ~4.9× faster. Always use it.

**Read these two sanity lines EVERY time, before trusting a single share:**
- `accounted NN.N%` **must be ~100** — below that the brackets are losing time and the table is
  fiction.
- `body ticks/field` **must be ~1** — the 50 Hz body must run once per display field, or the port
  is running the sim at the wrong rate.

**The phase→function map is DERIVED, not fixed** — the transpiler numbers the main-loop JSRs in
address order (`$1701–$1763`), so it shifts if the loop changes. Regenerate it with
`grep -nE 'PROBE_PHASE\([0-9]+\)' src/gen/revs_gen.c` and read the call on the next line. Current
map (24 phases): 1 `tick_race_timers`, 2 `draw_starting_lights`, 3 `read_driving_controls`,
4 `apply_driving_model`, **5 `build_track_geometry`**, 6 `place_player_in_section`,
7 `advance_player_section`, 8 `update_lap_timers`, 9 `engine_sound_update`, 10 `clear_surface_buffers`,
**11 `draw_road`**, 12 `engine_sound_update`, 13 `fill_line_surface`, 14 `build_road_sign`,
15 `draw_track_object`, 16 `draw_corner_markers`, 17 `move_and_draw_cars`,
**18 `fill_dash_edge_columns`**, 19 `mirrors_update`, 20 `engine_sound_update`,
21 `update_horizon_band`, 22 `process_car_contact`, 23 `check_crash`, **24 `view_paint_lines`**.
The synthetic rows keep fixed ids (`src/platform/probe.h`): 25 the paint call, **26 DRAIN** (50 Hz
body), **27 DECODE** (BBC-buffer → bitplanes, pure port overhead), 28 SPIN, 29 the body arm, 32 the
view-sweep tail, 33/34 `view_paint_lines` painting phases 2/3. ⚠ `view_paint_lines`'s true cost is
**24 + 32 + 33 + 34**, not phase 24 alone.

## The target machine

**A500, 7 MHz 68000, PAL.** A frame is 20 ms. Spending 10 ms on *anything* is half the budget.
Units: 1 raster scanline = 63.56 µs; a PAL frame = 313 lines. Be conscious of absolute
milliseconds, always — a percentage of an unknown total is not a measurement.

## The target ⭐

**Goal: 50 FPS. Floor: 25 FPS.** A scope call, not a prediction — explicitly "whether that's
reachable remains to be seen".

- **These are DISPLAYED frames**, as measured by Rule 1: `FPS = 50 * g_fpsFrames / g_vbiCount`.
- **The sim tick is a separate thing and is not negotiable.** Revs's 50 Hz body is a User VIA
  T1 interrupt on the BBC (`docs/static-map.md` §The interrupt) and runs in the real
  `INTB_VERTB` ISR on the Amiga. It ticks 50×/s whatever the display does — so 25 FPS means
  painting every other frame with the simulation still at full rate. A port that hits 50 FPS by
  slowing the game body is not a port that hits the target.
- **25 is a floor, not a fallback to settle into.** Below it the phase is not done; at or above
  it the remaining gap is an optimisation backlog, not a blocker.
- ⚠ **The target does not license quoting a number before measuring one.** The first honest
  figure comes from an on-target run, and "we need 50" is never evidence that a change bought
  anything. (Postmortem §4.1: profile a slow end-to-end skeleton on real hardware *before*
  committing to an approach. The Atari port's retired "50 FPS is impossible without an algorithm
  change" conclusion was disproven by hand-asm — the ceiling was GCC, not the algorithm. That
  cuts both ways: don't declare it impossible from reasoning, and don't declare it reached from
  optimism.)

## ⭐⭐ THE CURRENT NUMBERS

**FPS baseline** (rendered, moving car): `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` +
`fps_series.gdb`, warp, 30 s. Under `FIXED_RNG`+warp the row vector is deterministic frame for
frame; the modal non-outlier rows read **~3.5** (36 painted / 512 vbi at HEAD; one reset-dip row
per run, where the car leaves the track and re-parks — discard it). ⚠ The ABSOLUTE drifts as the
port changes (this note read 2.92-3.02 in an earlier session) — the number is only meaningful
against an in-session control, so never quote a delta without re-running this exact control from a
clean build in the same session (Rule 3).

⭐ **The span-rasteriser cpu-removal (`interp_edge`+`draw_surface_spans`, HEAD `004a672`) is the
first twin pass to MOVE this row: 34→36 painted (+5.9%), deterministic, two runs each side.** Why
it broke the "de-macroing buys nothing" streak: it deleted `PHP`/`PLP` — real `mem[]` stack writes
GCC cannot dead-store-eliminate — from a routine run ~43×/frame, not just dead flag-field stores.
See Lessons — implementation.

⭐ **The follow-up leaf pass (`span_plot_core`/`span_end_marker`/`road_span_advance` + the four
statics cpu-free, HEAD `f969843`) added a further +2.5%: 35.7→36.6 painted/512vbi (3.49→3.57 FPS),
deterministic and repeatable, two clean-build runs each side in one session.** Right at the quotable
edge — one painted frame is ~2.7% of a row, but 10-11 non-outlier rows resolve to ~0.3% and the row
vectors separate cleanly (control rows cluster 35-36, HEAD 36-37). Same mechanism as `004a672`: the
gain is fewer `mem[]` accesses in the per-cell inner leaf (`span_plot_core` is `always_inline`, run
per column per scan line), NOT the flag-de-macroing, which GCC already elided. The two span-rasteriser
passes together moved this row 34→36.6 (~+7.6%).

Goal 50, floor 25: **rendered, the port is roughly an order of magnitude short of the floor.**

**Where the time goes** — `make gen` + `cd amiga && make clean && make -j4 PROBES=1
STRAIGHT_TO_RACE=1 FIXED_RNG=1` + `EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb
./diag_run.sh 30`. Confirm the car is actually DRIVING at the interrupt with `amiga/dash_state.gdb`
(a parked car changes the whole table's meaning — the road pass alone is 8× lighter parked). One
loop iteration runs to a few hundred ms of bracketed work; **no framerate may ever be quoted from
a PROBES build** — read `accounted NN.N%` first, it must be ~100 or the shares are fiction.

| ms/frame | Phase(s) | Callee | Code |
|---|---|---|---|
| 92 | 24+33+34+32 | **`view_paint_lines`** (the CONSUMER) — sweep 25 · tail 11 · phase-2 17 · phase-3 39 | native, whole tree |
| 40 | 11+44-46 | **`draw_road`** (`$1A20`) — `draw_surface_spans` 33 · `fill_line_attr` 4 · `mark_line_surfaces` 1 | native, whole tree |
| 34 | 5 | **`build_track_geometry`** (`$24F6`) | native, whole tree |
| 38 | 27 | `RevsScreen::decode()` | port |
| 32 | 18 | `fill_dash_edge_columns` | native |
| 22 | 26 | the 50 Hz drain (`irq1v_band_schedule`) | native |
| 14 | 28 | the vblank spin | port |
| ≤4 each | 15, 32, 33-tail, 3, 29, 14, and the rest of the 24-call body | remaining drivers and leaves | mixed |

Re-measured 2026-08-20 at HEAD `f969843` in ONE run (`ROADSPLIT=1 PROBES=1 FIXED_RNG=1
STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, warp, 30 s, driving), ~440 loop frames: **~287 ms/frame total**
(cross-checks the ~3.5 FPS: 1000/287 ≈ 3.48), of which the three pipeline rows are `92 + 40 + 34 =
166 ms ≈ 58%`. ⚠ These are same-run shares — the three pipeline ms are directly comparable to each
other in THIS run; **never diff a ms row against the earlier table it replaced** (Rule 2 — different
session, different trajectory). ⭐ The span rasteriser reworked in `004a672`/`f969843` lives in
`draw_surface_spans` (33 of draw_road's 40 ms) and in view phase 3's `span_walk` (below); those are
where the two span passes' ~+7.6% landed. ⚠ On a ROADSPLIT build `phase4_prof`'s own `accounted`
line reads low BY CONSTRUCTION — draw_road's ms are moved into ids 44-46, which its `i<40` sum
excludes; read draw_road from `roadsplit.gdb` (which prints "phase 11 whole") and the rest from
`phase4_prof`, as this table does.

Two rows that are not phases and bound everything:
- the **VERTB ISR**: ~1.1 ms per call, ~17 fires per painted frame at ~2.9 FPS ≈ **19 ms/frame
  (~5%)**, charged pro-rata to whichever phase it preempted, so it appears in no row of its own.
  It is proportional to the open phase, so it does not distort the splits.
- **one 50 Hz body tick ≈ 1.6-1.7 ms** of its 20 ms budget — faithful, and only a per-painted-frame
  table makes it look large (it runs once per DISPLAY FIELD, not once per painted frame; at low
  FPS one painted frame charges it many times over).

⭐⭐ **The view pipeline (`build_track_geometry` → `draw_road` → `view_paint_lines`) is ~58% of
the frame, and its whole call tree has no transliteration left in it.** Of the ordinary levers,
delete-the-interpreter, inline-the-flag-helpers and de-macro-to-idiomatic-C moved the table
nothing — but a FOURTH did: **removing the 6502 stack ops (`PHP`/`PLP`) the idiom forced into the
hot span setup cut real `mem[]` writes and bought ~+6% on `draw_road`'s row** (`interp_edge`, HEAD
`004a672`). The distinction that matters: a flag-field write GCC already elides; a `mem[]` write it
cannot. What remains is the same class — fewer per-item `mem[]` accesses — plus the representation
(see Lessons below).

⭐⭐ **The fat is PER-ITEM SETUP, not bulk throughput — measured at every stage** (subsections
below). Each stage costs what it does because of the machinery it runs *per point / per span /
per line*, not because of the pixels it ultimately writes: `draw_road` spends 84% of itself in
per-span rasteriser setup over just **43 spans / 58 columns** a frame (~580 µs/column);
`view_paint_lines`' most expensive phase costs **139 µs/unit on 282 units** while its cheapest costs
17 µs/unit on 1443.
The counts are tiny; the per-item constant is not. So what's left is not "another twin":
1. **fewer POINTS / SPANS / LINES in the producers, or a cheaper per-item constant** — an
   algorithmic question, not a transliteration one, and the biggest single lever now
2. **the REPRESENTATION** (`docs/direct-bitplane-plan.md`) — the decode's port overhead, and the
   BBC-shaped buffer the consumer paints into which constrains it
3. **asm, last**, and only against the post-representation arrangement

### ⭐⭐ Inside `build_track_geometry` (phase 5) — where its ~16% goes, and why

Decomposed with `make GEOSPLIT=1 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1` +
`EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=geosplit.gdb ./diag_run.sh 30` (probe.h §GEOSPLIT — four
beam brackets carve phase 5, and platform-independent call counters explain it). Silverstone
practice, car driving, 348 frames. **A PROBES/warp share table — never diff it across builds.**

**WHERE (time split of the ~64 ms/frame the pass costs in this build):**

| Sub-phase | ms/frame | share of the pass |
|---|---|---|
| `road_edge_start` (near-point reuse) | 3 | 5% |
| `road_side_walk(0)` — one distance walk | 29 | 46% |
| `road_side_walk(0x80)` — the other | 30 | 48% |
| horizon tail + driver remainder | ~0 | <1% |

**The pass IS the two distance walks — 94% of it, and near-symmetric between the sides.** The
reuse pass `road_edge_start` is cheap *because* it reuses last frame's nearest points; the horizon
record and the routine's own driver are free. So an optimisation that does not touch the walk is
optimising the 6%.

**WHY (per-frame call counts, same run):**
- **27 edge points visited** (13 per side), **0 subdivisions** on this near-straight section. Each
  point runs the full transform chain: `bearing_to_section` → `point_distance_hypot` →
  `project_point` → emit. So the walk cost is ~**2.2 ms per edge point**, and the pass scales with
  the point count — which climbs on corners (each subdivision midpoint is another full chain).
- **60 `div16by8` per frame** — the engine's 8-restoring-step divide, ~2.2 per point.
- ⚠ **But not 3 divides per point.** `bearing_to_section` (30 calls, up to 2 divides each) +
  `project_point` (29 calls, 1 each) would be ~89 divides; only **60** ran. The no-divide arms
  fire often — bearing's 45° diagonal (equal magnitudes / dividend catching the divisor) and
  project's far-clip early exit — so it averages ~1 divide per transform call, not the worst case.
  A divide-elimination win is therefore bounded by the ~60 that actually run, not by the point count.

**The lever this points at:** fewer points (the walk emits 13/side even straight; is that floor
necessary?) and/or a cheaper per-point chain (the divide is central but not the whole 2.2 ms —
arctan lookup, the hypot approximation and the emit machinery share it). This is the "fewer POINTS
/ fewer ACCESSES" item above, now sized. Re-run `geosplit.gdb` **on a corner** (more subdivisions)
before committing — this table is a near-straight section and undercounts the subdivision arm.

### ⭐⭐ Inside `draw_road` (phase 11) — where its ~19% goes, and why

Decomposed with `make ROADSPLIT=1 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1` +
`EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=roadsplit.gdb ./diag_run.sh 30` (probe.h §ROADSPLIT +
`amiga/roadsplit.gdb` — three beam brackets 44/45/46 carve the phase, platform-independent leaf
counters explain it). Silverstone practice, car driving. **A PROBES/warp share — never diff across builds.**

**WHERE (time split of the ~68 ms/frame the pass costs):**

| Stage (both road sides) | ms/frame | share of the pass |
|---|---|---|
| `fill_line_attr` (44) — line→point map | 8 | 13% |
| **`draw_surface_spans` (45) — the span rasteriser** | **57** | **84%** |
| `mark_line_surfaces` (46) — line→class | 1-2 | 2% |
| clamp + return remainder (11) | ~0 | <1% |

**The pass IS the span rasteriser — 84% of it.**

**WHY (per-frame leaf counts, same run):**
- **43 spans** handed to `interp_edge`, **49 DDA scan lines**, **60 columns** merged
  (`road_span_plot`, 3 bus accesses each). **115 fill lines** in the line→point map, **15 mark points**.
- ⚠⚠ **The inputs are tiny — 60 columns is ~180 bus accesses — so the cost is NOT the pixels and
  NOT the bus-access count.** 57 ms over 60 columns is ~950 µs "per column", but that is a lie of
  averaging: the columns are near-free and the money is the **per-span setup** — `interp_edge`'s
  edge-interpolation arithmetic, run 43 times to place ~1.2 columns each. The lever here is the
  per-span constant (fewer spans, or cheaper `interp_edge`), exactly as with the geometry walk's
  per-point chain — not the DDA leaf, which the tiny column count proves is already cheap.

### ⭐⭐ Inside `view_paint_lines` (phases 24/33/34/32) — where its ~27% goes, and why

The CONSUMER — the single reader of the forty `$80`-spaced source blocks the two producers fill.
Same run; the painting is split into three phases (probe.h §PROBE_VIEW_* carries UNITS = cells
touched, RUNS = colour runs, LINES = scan lines painted, per phase).

| Painting phase | ms/frame | units | runs | lines | µs per unit |
|---|---|---|---|---|---|
| phase 1 (24) | 25 | 1443 | 36 | 36 | **17** |
| phase 2 (33) | 17 | 426 | 32 | 16 | 40 |
| **phase 3 (34)** | **40** | **282** | **50** | **25** | **142** |
| tail (32) | 11 | — | — | — | — |
| **total** | **95** | 2151 | 118 | 77 | 44 avg |

⚠⚠ **Phase 3 is the single most expensive view phase — 40 ms — on the FEWEST units (282).** It
costs 8× per unit what phase 1 does (142 µs vs 17). With 282 units over 25 lines and 50 runs, its
cost is the **per-line / per-run driver**, not the per-unit inner loop: ~1.6 ms per painted line.
The lever is that driver (fewer lines, cheaper per-run setup), not the cell loop — the same
per-item-setup shape as the other two stages. Phase 1, by contrast, is the honest throughput
phase (1443 units at a flat 17 µs) and is already near its floor.

### ⚠⚠ MEASURED (2026-09-02): the WIDE-VALUE campaign is NOT VISIBLE end to end — +0.65%, inside noise

The byte-lane→wide-value campaign (`docs/wide-value-cleanup.md`) had been argued entirely from
instruction counts and never measured. It has now been measured, and the measurement is the
campaign's most important number.

**Method** (the only quotable one — Rule 1): both builds `make clean` + `make STRAIGHT_TO_RACE=1
FPSCOUNT=1 FIXED_RNG=1`, both run `EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=fps_series.gdb
./diag_run.sh 30`, **in the same session**, control built from a `git worktree` at `b35fe8a^`
(4263f29 — the commit before the campaign's first code change). Row vectors compared, the two
off-track/reset rows (~2.9) dropped as the documented outliers, the remaining ten averaged.

| | non-outlier rows | mean FPS |
|---|---|---|
| control (`4263f29`, pre-campaign) | 10 | **4.136** |
| HEAD (`e6f497b`, ~20 pairs relocated) | 10 | **4.163** |

**+0.027 FPS = +0.65%.** The project's own rule is that **FPS under ~3% is noise**, so this is a
null result: five weeks of pair relocations did not move the displayed framerate measurably. Two
caveats that both cut the same way — the runs are **cross-run**, which Rule 2 forbids for pricing a
change (total vbi differed, 6796 vs 7011, i.e. the two builds do not drive identical trajectories),
and ten rows resolve to ~0.3%, so 0.65% is above resolution but far below the noise band. Nothing
here supports a claim of a win.

⭐⭐ **What this does NOT mean.** It does not mean the byte-lane work was wrong or wasted:
a wide `+` genuinely is fewer instructions than a `move.b`/`lsl`/`or`/`lsr`/`branch` chain, the
twins are now readable C, and several gate-strengthening findings (the driver-bypass class, the
publish-don't-blunt rule) came out of it. It means the **byte lanes were not where the frame went**
— which is the same answer this document already records for the view-pipeline twins
(§CURRENT NUMBERS: "neither pass's twins moved the framerate") and the same standing conclusion
restated once more: **the port's biggest costs are the MACHINERY the transliteration is wrapped in,
not the arithmetic inside it.** ~10 600 `bus_*` calls a frame, a BBC-shaped frame buffer decoded
every painted frame, and per-instruction `cpu` field writes are each an order of magnitude larger
than the byte-lane savings.

⭐⭐ **Consequence for planning.** Do not spend further effort on wide-value pairs on the strength of
ref counts. `math_lo/hi` is retired as ineligible (NINTH lesson) and the two remaining Tier-1 items
(`point_dist` $7C/$7D, `edge_nearest` $10/$11) are each gated on building a **Brands
expansion-circuit real-BBC frame-buffer differential over display lines 82+** — a substantial new
instrument. On this measurement that instrument cannot be justified *by the campaign*; build it
when a correctness question needs it (the hook seams are gated by nothing, which is its own reason)
and take any wide-value pair it unblocks as a by-product. **The next real performance work is the
REPRESENTATION change in `docs/direct-bitplane-plan.md`**, which attacks the machinery this
measurement points at.

## Lessons — measurement

- **Compare FPS row vectors, never the `total painted` line.** The total spans a partial
  trailing row and the run length in vblanks varies between otherwise-identical runs, so totals
  differ by a few percent for identical code. Under `FIXED_RNG`+warp, `fps_series` is
  deterministic row for row — the real resolution limit is one painted frame (≈3.3% of a row),
  not run-to-run variance, which is what makes small (~1-3%) changes quotable at all if enough
  rows are averaged.
- **Never diff a phase-bracket share or a per-iteration ("t/it") number across builds** — only
  within one run (Rule 2). Per-iteration deltas carry ~±10% trajectory noise even under
  `FIXED_RNG`, because it pins the level, not the path flown — two builds still drive their own
  ground.
- **Calibrate a new bracket against a known cycle count before trusting what it reports.** Burn N
  known cycles inside it and check linearity in N; an uncalibrated bracket can look several times
  more expensive than the instruction count predicts, and the gap is usually arithmetic on the
  reader's side (a `move.b dn,abs.l` flag store is 16-20 cycles, not "a handful of table reads"),
  not an instrument fault.
- **A beam-tick bracket must accumulate through a monotonic epoch, not a raw
  `beamTick() - beamTick()` difference** — a raw difference silently discards any bracket that
  straddles the once-per-display-frame wrap, and it discards the LONGEST phases hardest. A
  profile that reads several rows as flat zero is this bug, not evidence those routines are
  trivial.
- **Size a routine's call count while DRIVING, not parked** — some counts run many times higher
  moving (a parked car re-derives almost nothing that a driving one has to recompute every frame).
  A parked call count next to a driving framerate describes two different workloads.
- **`make clean` before every differently-flagged build.** The Makefile tracks neither `PROBES`
  nor a define change in `EXTRA_DEFINES` — a stale object reproduces the PREVIOUS build's number
  to the digit, which is the tell that a rebuild didn't actually happen.
- **Match the build to the control.** A `PROBES=1` build is meaningfully slower on its own merits
  (the brackets are not free); never compare a probe-build number to a shipping one.
- **A `STRAIGHT_TO_RACE` run leaves the circuit and parks after a few hundred frames** — keep
  windows to 30 s. A longer run does not gather more data, it dilutes the measurement with a
  static scene. Check `amiga/dash_state.gdb` at the END of the window, not just the start.
- **Put no gdb stop inside a measurement window.** A conditional breakpoint that halts the machine
  to evaluate its own condition can bias a reading by an order of magnitude or more, in either
  direction, and can agree with the truth on one build while being wildly wrong on another —
  sample from an in-program counter instead (`fps_series.gdb`'s approach).
- **An average over calls that do materially different jobs hides the finding.** Bracket each
  distinct arm separately, and add an empty-bracket control running at the same call rate before
  trusting any number a new bracket reports — the bracket's own overhead can exceed what it's
  trying to measure.

## Lessons — implementation

- **Driver twins collect nothing.** A small native driver wrapped around a still-transliterated
  subtree does not move the framerate: the interpreter it deletes was never where the row's time
  was. Twin a routine for the naming and leaf-freedom it buys, not for FPS, unless the WORK is
  also in that routine — a leaf, or a routine with real arithmetic and few calls into
  still-transliterated callees.
- **The win is algorithmic compression, not "being real C."** An arithmetic leaf where every
  6502 instruction maps to one C operation, with every flag live at the exit, has no interpreter
  overhead left to remove and can come out slower before it comes out even.
- **A 6502 flag write costs five 68000 stores** (A/N/V/Z/C), computed through mask chains for V —
  paid on every subtract whether or not any caller ever reads that flag. Replace the chain's
  interior with the plain value and replay only the ONE flag that provably escapes the routine —
  decide which flag escapes by argument, and check the sibling case, not just the case at hand.
- **A flag-carrying helper must be `always_inline`.** GCC leaves such helpers out of line at -O3
  because they write a global struct and have many callers, so every use pays a call and a
  register-save the 6502 original never had.
- **A parameter that is a compile-time constant at every call site must be `always_inline`d, or
  it is a memory operand in the inner loop.** A descriptor struct (a plotter or arm spec) left out
  of line costs a reload of its fields on every call; inlining turns them into immediates and
  folds away each specialisation's own now-constant tests.
- **A loop is not free where the 6502 unrolled.** Rolling an unrolled chain into `for` recovers
  legibility but pays real per-iteration cost; hoist loop-invariant tests out of the body rather
  than re-deriving them every pass.
- **`bus_read`/`bus_write`'s range test is for the hardware window; hoist it out of any loop whose
  pointer is provably RAM for the loop's duration** (once per scan line, not once per cell) — the
  transpiler already does this for constant addresses, so what leaks is the indirect addressing
  modes.
- **Don't qualify `mem[]` `volatile` unless something on THIS platform actually races it.** The
  qualifier blocks every optimisation over the array that holds the whole engine's state — no
  register-caching, no reordering, no CSE — for a hazard that has to be demonstrated for the
  current ISR contract, not inherited from an earlier project's comment.
- **When an expensive per-tick output is a pure function of a handful of inputs that move rarely,
  compare inputs and reuse the previous output instead of rebuilding it.** This is the single
  biggest lever found in this port so far. Sabotage the reuse against the real routine's rare
  *moving* inputs, not just static ones — a check that only ever sees unchanged input proves
  nothing about the case the reuse exists for.
- **A dirty-region scheme's enabling restructure has its own cost.** Measure the restructure alone
  (the feature forced off) and the feature together; only the pair separates "the test is good"
  from "the change is good" — quoting the test's win alone quotes against a build that never
  shipped.
- **Before building a representation change, ask which quantity the current cost is proportional
  to.** A change that collapses stores when the loop's real cost is source READS moves nothing;
  settle it with a differential that strips one stage at a time, not with a plan's predicted
  before/after count.
- **GCC's dead-store elimination already removes a flag write that nothing reads in straight-line
  code.** A transpiler pass or a hand de-macroing pass aimed at the same target buys nothing
  measurable — the only place a flag write survives is bracketing an opaque call, which is exactly
  where a liveness-based pass also has to assume the callee reads everything.
- ⭐⭐ **But a `PHP`/`PLP` is a `mem[]` write, and GCC canNOT eliminate it — so removing the 6502
  STACK OPS an idiom forced into a hot routine is a real win where de-macroing was not.** The
  span-rasteriser setup (`interp_edge`) took the caller's carry via `PHP`/`PLP` around its
  publish-vs-draw decision; turning that carry into an explicit `publishOnly` argument deleted a
  stack write + read on every one of ~43 calls/frame and moved the `draw_road` FPS row 34→36
  (+5.9%, deterministic, two runs each side; HEAD `004a672`). The rule: **de-macroing dead FLAGS is
  cosmetic (buys 0), but deleting the `mem[]` traffic the 6502 idiom forced — `PHP`/`PLP`, a
  scratch-cell round-trip, an indirect that could be a local — is the access-reduction lever and it
  pays.** Tell them apart by asking whether the store lands in the `cpu` struct (elided) or in
  `mem[]` (not). Measured with an in-session fps_series A/B (Rule 1), not a PROBES share.

## Rule 1 — the ONLY way to quote a framerate

```
cd amiga && make clean && make -j4 FPSCOUNT=1 FIXED_RNG=1
. ./env.sh && GDBSCRIPT=fps_series.gdb ./diag_run.sh 240
```

⚠⚠ **Use `fps_series.gdb`, never a conditional-breakpoint script.** A script that re-arms a
breakpoint per segment halts the machine to evaluate the condition every time, and the resulting
bias can be an order of magnitude in either direction depending on the build — it can read close
to truth on one binary and be wildly wrong on another, which is exactly why it isn't self-evident
from a single reading. `fps_series.gdb` instead reads a series the PROGRAM sampled for itself (the
VERTB handler stores `g_fpsFrames` every 512 vblanks) with no gdb stop inside the window.

`FPS = 50 * g_fpsFrames / g_vbiCount` — painted frames per **emulated** vblank, so host speed and
the gdb stub's own slowness cancel out completely.

⚠ Both counters must be listed in `PROBE_SYMS` (`amiga/Makefile`), or `--gc-sections` drops the
unreferenced one and gdb prints **instruction bytes** in its place — a fake measurement rather
than an obvious zero. `make probe-audit` enforces this on every link.

- `FPSCOUNT=1` adds *only* the headless auto-run and one increment per painted frame.
- ⚠ **Never quote a framerate from a `PROBES` build.** The timing brackets are real cost —
  several register reads and a multiply per bracket transition — and the probes are what the
  honest build exists to measure against.
- ⚠ **This harness OVER-reads a win.** A differential that predicts a small percentage change can
  measure several times that end to end. **Under ~3% is agreement, not evidence.**
- ⚠ Sample in SHORT segments and discard rows where the run stopped doing the work being measured
  (the car leaving the track). A wide window straddling that under-reports badly.

**So: quote a static cycle count or a differential ratio as the win, and an FPS row only as the
standing baseline.**

## Rule 2 — price a native/asm twin with an IN-PROCESS differential, never cross-run

```
make VERIFY=1 PROBES=1     # + amiga/<name>_verify.gdb
```

The twin and the C oracle run back-to-back on the **same inputs in one run**, byte-compared and
beam-ticks tallied per implementation.

Cross-run comparison (build A vs build B) is **not valid by default**. The reason is structural:
the 50 Hz interrupt is asynchronous to the free-running main loop, so any change in render speed
shifts the phase between them — and that shift changes what the simulation actually does. A number
measured on a different workload is not a comparison.

- **`make FIXED_RNG=1` for every perf run.** It pins the simulation so builds are comparable. OFF
  by default — it removes real variety, so never judge *rendering or gameplay* from a
  `FIXED_RNG` build.
- ⚠ **The differential's metric is the twin/C RATIO, not absolute ticks/call.** The same binary
  swings noticeably run-to-run on absolutes while the ratio holds tight.
- ⚠ **Run any baseline ≥2× after a rebuild** before believing a delta. An n=1 baseline can produce
  a bogus regression verdict.
- ⚠ A bracket **includes nested callees**, so it is not that function's own cost. For own-cost
  attribution you need a PC profile.

### ⚠⚠ Per-iteration ("t/it") numbers are NOT a safer alternative

`FIXED_RNG` pins the *level*, not the *path flown* — each build still drives its own ground, and
a phase's cost is partly a property of the view. This was proven by moving the auto-run's start
later with no code change at all: a phase moved by an amount comparable to a "regression" that had
been filed as the top performance item.

**Cross-build t/it deltas carry ~±10% of trajectory noise. Use phase brackets for SHARES (where
does the time go, within one run) and FPS for PROGRESS. A ledger row is not a baseline.**

## Rule 3 — every old number is wrong; re-measure

Any framerate or cost figure in an older note or commit was measured on a different build, a
different probe set, or a different workload. **Re-measure, don't quote.** Two specific traps:

- **Instrumentation** (Rule 1) — probe builds are much slower than shipping ones, and a PC
  profile taken on a probe build inflates exactly the buckets that contain the brackets.
- **The unattended run ending** — a headless run drives no input and eventually stops doing the
  work being measured while the vblank counter keeps ticking.

## Rule 4 — shape-probe the algorithm before optimising it

The biggest single win found so far did not come from PC sampling. It came from **measuring the
distribution of a routine's own inputs** with dedicated shape counters, finding that a small
number of cases covered most calls, and specialising those. Then: prove the algebra over
millions of randomised cases first, *then* write the fast path, *then* run the on-target
differential.

Corollaries worth keeping:
- A "check before drawing" scheme usually re-reads the very byte the check was meant to avoid, so
  it can only recover the bookkeeping around that load — the reject path is already at the floor.
- After special-casing a recursion's leaves, **re-price their parents**.
- Ask whether a "serial" accumulator really has to be serial.

## Rule 5 — interrupt work is capped at one frame

Work in the vblank ISR is capped at **one frame**. Over that, a displayed frame is silently
dropped — and the dropped frame (a stall, a 2× animation jump, a copper write landing behind the
beam) is what the player reports, not the cost. Bracket any ISR-side work with VPOSR/VHPOSR
beam-line reads *before* theorising. A 50 Hz ISR is also a **fixed tax on all wall clock**
regardless of frame rate, which makes its per-firing cost one of the few legitimately comparable
cross-build numbers.

## Rule 6 — suspect a beam-timing race? re-run on a FASTER CPU

`AMIGA_MODEL=` / `EXTRA_ARGS=` on `run.sh` and `diag_run.sh`. A slow A500 can land safely inside a
race window that a faster CPU moves a violation into — re-running on a faster core is how a
beam-timing race gets caught rather than missed. Also: quote the **duration**, not the hit count —
a beam-overlap counter can read differently on two runs of one binary.

## Reporting

Surface numbers honestly. Say which build produced them, which harness, and the window size. If a
change measures at zero, that is a result — record it as closed *on data*, and do not re-open it
on optimism.
