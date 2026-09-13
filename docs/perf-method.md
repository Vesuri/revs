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
frame; the modal non-outlier rows read **4.49-4.58, averaging 4.52** (46-47 painted / 512 vbi,
measured 2026-09-06 at `fb13c3f`+the delay-loop fix; one or two reset-dip rows per run, where the
car leaves the track and re-parks — discard them). ⚠ The ABSOLUTE drifts as the
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
| 70 | 24+33+34+32 | **`view_paint_lines`** (the CONSUMER) — sweep 22 · phase-2 15 · phase-3 27 · tail 6 | native, whole tree |
| 40 | 11 | **`draw_road`** (`$1A20`) | native, whole tree |
| 38 | 27 | `RevsScreen::decode()` | port |
| 26 | 5 | **`build_track_geometry`** (`$24F6`) | native, whole tree |
| 17 | 18 | `fill_dash_edge_columns` | native |
| 14 | 26 | the 50 Hz drain (`irq1v_band_schedule`) | native |
| 14 | 28 | the vblank spin | port |
| ≤3 each | 15, 29, 14, 13, 10, and the rest of the 24-call body | remaining drivers and leaves | mixed |

Re-measured 2026-09-12 at HEAD `8629ff9` in ONE run (`PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1
HOLD_THROTTLE=1`, warp, 30 s, driving), 530 loop frames, **`accounted 96.8%`**: **~256 ms/frame
total**, of which the three pipeline rows are `70 + 40 + 26 = 136 ms ≈ 53%`. ⚠ These are same-run
shares — the three pipeline ms are directly comparable to each other in THIS run; **never diff a ms
row against the earlier table it replaced** (Rule 2 — different session, different trajectory).
⭐ The span rasteriser reworked in `004a672`/`f969843` lives in `draw_surface_spans` (inside
draw_road's 40 ms) and in view phase 3's `span_walk` (below); those are where the two span passes'
~+7.6% landed. ⚠ On a ROADSPLIT build `phase4_prof`'s own `accounted` line reads low BY
CONSTRUCTION — draw_road's ms move into ids 44-46, which its `i<40` sum excludes; read draw_road
from `roadsplit.gdb` (which prints "phase 11 whole"). This table is a plain (non-ROADSPLIT) run, so
its `accounted` is the honest 96.8% and phase 11 is draw_road whole.

⚠⚠ **Two rows moved a long way from the previous table and the direction matters.**
`fill_dash_edge_columns` reads **17 ms, not 32**, and the consumer reads **70, not 92** — so the
frame is 256 ms, not 287. Do not treat either as an improvement earned by a change: they are
different sessions on different trajectories, which is exactly what Rule 2 forbids diffing. The
usable content is the RANKING and the within-run shares, and the ranking did change — `draw_road`
and the decode are now the top two rows, and the consumer is a distributed third rather than the
single dominant cost.

Two rows that are not phases and bound everything:
- the **VERTB ISR**: charged pro-rata to whichever phase it preempted, so it appears in no row of
  its own. Measured directly by `make ISRSPLIT=1` — see §The VERTB ISR below. It was ~1.1 ms per
  call and is now ~0.72 ms; because it fires 50 times a second whatever the framerate does, it is a
  tax on WALL CLOCK, not on the frame: at ~4.5 FPS that is ~11 fires per painted frame.
- **one 50 Hz body tick ≈ 1.6-1.7 ms** of its 20 ms budget — faithful, and only a per-painted-frame
  table makes it look large (it runs once per DISPLAY FIELD, not once per painted frame; at low
  FPS one painted frame charges it many times over).


### ⚠⚠ A BUSY-DELAY LOOP THE TRANSLITERATION NEVER PAID — twin #179's practice pad (2026-09-06)

`move_and_draw_cars`'s practice arm (`$262D-$2636`) is not an early RTS: it is a 1536-iteration
busy delay (six passes of 256 `DEC math_lo`) that padded a practice frame by ~6 ms on a 2 MHz 6502
so it paced like a race frame. Twin #179 reproduced it faithfully as a real C loop, and the
framerate fell **4.53 → 4.30 (−5%)**.

**Why it was a REGRESSION and not merely a cost: the port had never executed it.** GCC eliminated
the transliteration's version by *final-value replacement* — the loop's only observable effects are
`mem[$74]` reaching 0 and three `cpu` fields, all of which sink out of the loop, so the whole
practice arm compiled to `clr.b mem+0x74; …; rts` (verified in the `830ae5d` objdump). Every
determinism run, every refloop differential and every FPS baseline this project has recorded was
therefore measured with **no burn**. Writing the loop out honestly in C is what made it real.

The fix is to keep the memory effect (`math_lo = 0`) and drop the cycles, argued at the code: the
pad exists to *slow* practice to 50 Hz and the port is already 12× below it. FPS returned to 4.52.

Three general lessons, in order of leverage:

- ⭐⭐ **A twin can be slower than the transliteration because GCC was DELETING work — check for
  an eliminated loop, not just for a missed inlining.** The known trap (`§twins #14/#15`) is a
  twin paying for out-of-line flag helpers; this is its mirror image, and no objdump of the *twin*
  can reveal it. Diff the objdump of the routine **on both sides**.
- ⭐⭐ **A pure cycle-burn is a faithfulness question a `mem[]` differential cannot ask.** `validate`
  passed at 0 mismatch before and after, by construction — the loop's only memory effect is its
  exit value. The twin's own sabotage ledger had already recorded `math_lo = 0` as a provably
  invisible defect; it took a framerate bisect to notice that "invisible" also meant "free to drop".
- ⭐ **It runs on the 50 Hz BODY, so it is a tax on WALL CLOCK, not on the frame** — fifty fires a
  second whatever the framerate does, exactly like the VERTB ISR below, and no phase-share row can
  see it. 1536 iterations × 50/s ≈ 8% of wall clock.

**How it was found:** a clean-build in-session control read 4.32 against a recorded 4.51, and a
five-step bisect over the 27 commits since `85533ed` (each: worktree checkout → `make gen` →
clean build → the Rule 1 control) landed on `aed19ac` alone. Row vectors under `FIXED_RNG`+warp
were *identical* at 4.30 for four consecutive revisions, which is what made a −5% step legible at
all — the resolution argument in Rule 1 is what carried this.

## The VERTB ISR — measured, not estimated (`make ISRSPLIT=1`)

⭐⭐ **The ISR is the one cost that is a fixed fraction of WALL CLOCK.** Every other row here
scales with the frame: make the renderer twice as fast and it halves. The VERTB handler fires 50
times a second forever, so a microsecond in it is 50 µs/s permanently, and it is stolen from the
main loop no matter what the main loop is doing. That is why it gets its own instrument.

**The instrument.** `make PROBES=1 ISRSPLIT=1` adds a mark/close bracket chain *inside* the
handler (`probe.h` §ISRSPLIT) with its own accumulator, so the published phase table stays
comparable. `amiga/isr_split.gdb` reads it. Two things make its numbers trustworthy:
- **A NULL CONTROL (slot 0)** — two consecutive transitions bracketing nothing at all, on the same
  path at the same rate. **Measured floor: 92 µs.** Every other row means something only above it,
  and a row within ~1× the floor (the mouse, the prologue, the flash tick) is not resolvable — do
  not chase those.
- **A CALIBRATION** — `make ISRCAL=1` puts `probe_burn_cycles()` (exactly 14 000 68000 cycles =
  1.974 ms) in the tail slot. It read **2001 µs above the floor: 0.6% error**, which validates the
  whole beam-tick → µs chain, not just this table. ⚠ `ISRCAL` changes `EXTRA_DEFINES` and the Amiga
  Makefile does not track those — **`make clean` when you turn it off**, or the burn stays linked in
  and the tail row reads ~2 ms of nothing (it happened; CLAUDE.md's stale-build rule, again).
- ⚠ The first bracket **must open BELOW `PROBE_VBI()`**: that advances `g_beamEpoch` by a whole
  display frame, so a bracket straddling it reads ~10 ms of pure artefact.

**Where the 1.1 ms was** (7247 fields, driving; "real" = row − the 92 µs floor):

| slot | µs/field | real | what it is |
|---|---|---|---|
| 8 `snd_tick` ×2 | 784 | **692** | the MOS sound scheduler, twice per field (100 Hz) |
| 5 screen | 256 | 164 | `RevsScreen::vbiUpdate` — ⚠ per PAINTED frame, not per field |
| 2 mouse | 182 | 90 | `sampleMouse` |
| 6 audio rest | 244 | 60 | `revs_audio_vbi` minus its two children |
| 9 `program_paula` | 61 | 51 | only on the ~10% of fields where chip state moved |
| 1 prologue | 130 | 38 | the INTREQ clear + `g_vbiCount` |
| 3 beam entry | 129 | 37 | `noteVbiEntry` — a **pure diagnostic** |
| 4 tt flash | 112 | 20 | `tt_tick_flash` |
| 7 tail | 108 | 16 | the pending-tick accounting |

**Two hypotheses the measurement killed.** The Paula DMA busy-wait in `apply()` looked like the
obvious suspect and is a **non-issue** — 42 restarts in 7247 fields = 2 µs/field. And
`RevsScreen::vbiUpdate` is **not** a 50 Hz tax at all: it early-outs on `!m_ready` on ~93% of
fields, so its cost belongs to the painted frame (~2.3 ms of a ~220 ms frame), not here.

**What actually cut it — 1168 → ~720 µs/field real (−38%), worth +5.6% FPS (4.27 → 4.51):**
1. ⭐⭐ **`program()` was recomputing an unchanged chip state 100 times a second.** Everything it
   writes is a pure function of `(bbcChan, active, pitch, level)` and its `chip_set_*` are already
   no-ops on an unchanged value, so a four-field memo is **exact**, not an approximation. Measured
   hit rate **94.7%**. ⚠ Any other writer of `s_chip` must clear `progValid` (`snd_flush_channel`
   and `snd_reset` do) — the memo is state ABOUT the chip.
2. **`divider_for` did a `divu.w` + `divs.w` (~300 cycles) to look up one of 256 answers.** A
   512-byte table indexed by the pitch byte removes both. ⚠ Build the table by WALKING
   `idx`/`oct` counters: `p % 48` on an `unsigned` is a 32-bit divide, and `muldiv-audit` rejects
   it outright.
3. ⭐ **Force-inline the memo GATE, out-of-line the body.** At −O2 GCC left the four-field compare
   in a function, so 94% of calls paid a `jsr` + `movem` to decide to do nothing: 461 → 368
   µs/field. Same lesson as twins #14/#15.
4. ⭐⭐ **THE STATS WERE 13% OF THE ROUTINE.** The `g_snd*` counters are `volatile unsigned long`
   read-modify-writes to absolute memory — ~40 cycles each, uncoalescable *because* volatile — and
   `snd_tick` bumped ~7 per tick. Only `.gdb` scripts ever read them, so `SND_STAT()` compiles them
   out unless `REVS_PROBE`: **93 µs/field**, 0.47% of all wall clock, for counters no shipping build
   reads. Measured with `-DREVS_SND_STAT_OFF`, which leaves the brackets in and takes the counters
   out. ⚠ The counter DEFINITIONS stay unconditional — `PROBE_SYMS` lists them on every link.
5. `noteVbiEntry` is a pure diagnostic (two chip reads, 37 µs/field) and is now `#ifdef REVS_PROBE`.
   `g_beamPresentsLate`, the one CLAUDE.md requires to stay 0, is recorded in `vbiUpdate()` on the
   present path and is unaffected.

⭐ **The generalisation, and it is not about sound:** a `volatile` counter in a 50 Hz path costs
more than the work it measures, and an unmemoised recompute of an unchanged value costs everything.
Both are invisible in a phase table, because the ISR has no row.

**What is left** (~720 µs/field ≈ 3.6% of wall clock): `snd_tick` ×2 at ~276 µs real is still the
largest and is now mostly the genuine per-tick four-channel walk for the ~1.8 channels that reach
`program()`. Everything else is at or near the 92 µs instrument floor. ⚠ Do not merge the two ticks
into one pass to save a walk: `make sound` compares chip state **tick by tick** against a real MOS,
and the intermediate state is part of the contract.

⭐⭐ **The view pipeline (`build_track_geometry` → `draw_road` → `view_paint_lines`) remains the
dominant subsystem, and its whole call tree has no interpreter dispatch left in it.** Of the ordinary levers,
delete-the-interpreter, inline-the-flag-helpers and de-macro-to-idiomatic-C moved the table
nothing — but a FOURTH did: **removing the 6502 stack ops (`PHP`/`PLP`) the idiom forced into the
hot span setup cut real `mem[]` writes and bought ~+6% on `draw_road`'s row** (`interp_edge`, HEAD
`004a672`). The distinction that matters: a flag-field write GCC already elides; a `mem[]` write it
cannot. What remains is the same class — fewer per-item `mem[]` accesses — plus the representation
(see Lessons below).

⚠⚠ **The former conclusion that the fat is exclusively per-item setup is superseded by the direct
2026-09-12 splits below.** `span_walk` itself is ~24 ms, not near-free; the complete viewport
unit/run interior is 29 ms, not the fit's 4.3 ms; and the geometry walks use native DIVU rather than
the old restoring divider. The counts are modest, but both the loops and their setup matter. What
remains is not "another twin":

1. **fewer POINTS / SPANS / SOURCE VISITS, with setup and loops fused into native value pipelines**
2. **the REPRESENTATION** (`docs/direct-bitplane-plan.md`) — the decode's port overhead, and the
   BBC-shaped buffer the consumer paints into which constrains it
3. **asm, last**, and only against the post-representation arrangement

### ⚠ Historical `build_track_geometry` split — superseded after the native rewrite

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

⚠⚠ **Current measurement (2026-09-12): 28 ms total; 1 + 13 + 12 + ~0 ms across start, the two
walks and tail.** The census still sees 27 points and zero subdivisions, but `g_geoDiv=0`: the old
`div16by8` count above describes the pre-native implementation. Current native cores use
`revs_divu16`/68000 DIVU. The current lever is a native `EdgePoint` value pipeline that avoids
publishing and reconstructing split-byte scratch records between the two walks and the road stage.

### ⚠ Historical `draw_road` split — the "columns are near-free" inference is superseded

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
- ⚠⚠ **The inference that columns are near-free did not follow from their small count.** A fresh
  ROADSPLIT run measures 43 ms total: 4 ms `fill_line_attr`, 37 ms `draw_surface_spans`, 1 ms marks.
  A temporary same-rate bracket around the out-of-line `span_walk` measures about **24 ms corrected**,
  leaving roughly 13 ms in surface setup/remainder. Both halves are material. The next rewrite must
  combine `interp_edge` and `span_walk` into one flat native SpanPlan/DDA kernel; specializing either
  side alone retains the representation and call traffic between them.

### ⭐⭐ Inside `view_paint_lines` (phases 24/33/34/32) — where its ~27% goes, and why

The CONSUMER — the single reader of the forty `$80`-spaced source blocks the two producers fill.
Same run; the painting is split into three phases (probe.h §PROBE_VIEW_* carries UNITS = cells
touched, RUNS = colour runs, LINES = scan lines painted, per phase).

| Painting phase | ms/frame | units | runs | lines | µs per unit | µs per line |
|---|---|---|---|---|---|---|
| phase 1 (24) | 22 | 1442 | 36 | 36 | **15** | 618 |
| phase 2 (33) | 15 | 426 | 32 | 16 | 35 | 958 |
| **phase 3 (34)** | **27** | **282** | **50** | **25** | **98** | **1106** |
| tail (32) | 6 | — | — | — | — | — |
| **total** | **70** | 2150 | 118 | 77 | 33 avg | — |

⚠⚠ **Phase 3 is the single most expensive view phase — 27 ms — on the FEWEST units (282).** Its
cost is the **per-line / per-run driver**, not the per-unit inner loop: ~1.1 ms per painted line.
⚠ Do NOT read the µs/unit column as a premium phase 3 pays per cell — it is ~500 µs of per-run cost
divided by a units/run that falls 40 → 5.6 across the phases, and the arm counters refute the
per-unit reading outright (§BUT DIVIDE IT BY RUNS below). The column is kept because it is what the
probe prints, not because it is the rate of anything.
The lever is that driver (fewer lines, cheaper per-run setup), not the cell loop — the same
per-item-setup shape as the other two stages. Phase 1, by contrast, is the honest throughput
phase (1442 units at a flat 15 µs) and is already near its floor. **§The four view probes below
decomposes phase 3's 27 ms further and names where 83% of it sits.**

#### ⭐⭐ AND THIS TABLE IS WHY THE PER-LINE SKIP WAS A NULL — the skip can only reach phase 1

`make VIEWSKIP=1` (`docs/direct-bitplane-plan.md` §7i) deletes 39.5% of the sweep's line-visits on
target and moved the framerate **-0.4%**. One profile of each arm says why, and the load-bearing
part is a set of COUNTS, not a timing — so the cross-run caveat does not apply to it:

| units/frame | phase 1 | phase 2 | phase 3 |
|---|---|---|---|
| control | 1441 | 426 | 282 |
| `VIEWSKIP=1` | **619** | **426** | **282** |
| runs/frame, control → skip | 36 → **15** | 32 → 32 | 50 → 50 |

⚠⚠ **Phases 2 and 3 skip ZERO units and ZERO runs, and they are 47 of the consumer's 70 ms.** Their
lines are the clipped and short lines around the horizon and the road — the ones that always have
content, so the predicate never holds for them. The skip's whole reachable surface is phase 1, the
flat full-width background lines, and it took **22 → 15 ms** of it: **~7 ms of a ~290 ms frame =
2.4%**, which is exactly at the "under ~3% is noise" floor. **Nothing needs to have cancelled the
saving — the ceiling was below what FPS can resolve, and that was computable from this table
before the skip was built.**

⭐⭐ **The rule this earns: price an optimisation's CEILING against this decomposition first.**
"39% of line-visits" sounds like 39% of the sweep and is 2.4% of the frame, because the visits it
deletes are the cheap ones. Multiply the share you can actually reach by the fraction of it you
can actually remove, and compare the product to the 3% floor — if it does not clear it, the
experiment cannot answer the question whatever it returns.

ℹ Two smaller readings, both consistent and neither quotable on its own (cross-run, ±10%):
phase 1's per-UNIT cost rose 15 → 24 µs while per-LINE fell 616 → 427 µs — the runs removed were
the all-flat cheap ones, so the surviving units carry the per-run setup over fewer of them. And
~8 ms appeared across phases 33/34/11/18, which is within trajectory noise; **the marking hooks
(`view_mark_source`, called from `plot_store_resync` on every plotter store) are a PLAUSIBLE but
UNPROVEN cost** — do not cite it as measured.

### ⭐⭐ The four view probes (2026-09-12) — phase 3 decomposed, and the ~6× note RETRACTED

Phase 3 was the most expensive view phase on the fewest units, and before anything was rewritten
around it two questions had to be settled: **do the beam brackets tell the truth**, and **are its
milliseconds in its body or in interrupts landing inside an open bracket**. Four probes
(`amiga/Makefile` §VIEWCAL / VIEWP3), all on `PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1
HOLD_THROTTLE=1` + warp + `phase4_prof.gdb`.

**1. The calibration (`VIEWCAL=N`) — the brackets are HONEST to 1.005×.** `probe_burn_cycles()`
is exactly 1000 × (`nop` 4 + `dbra` 10) = 14 000 cycles = **1975 µs** at 7.09 MHz, and the probe
demands the row scale linearly in N or the burn is not what is being measured:

| N | phase 39, µs/call |
|---|---|
| 1 | 3235 |
| 2 | 5304 |

The **slope** is what carries the answer: 2069 µs per known 14 000 cycles = **1.048× raw**, and
correcting for the ~0.16/0.27 VERTB ISR fires (828 µs each) that land inside the open bracket gives
**1985 vs 1975 = 1.005×**. The intercept D = 1166 µs/line = 29.2 ms/frame is phase 3's own driver,
independently agreeing with the table above.

⚠ **Read the intercept, not a single row.** A single `VIEWCAL=1` row at 3235 µs looks like 1.64×
inflation and is not: `PROBE_PHASE` **switches** phase, it does not nest, and the loop-top re-arm
is `VIEWP3_PHASE(...)`, compiled out unless `REVS_VIEWP3` is defined — so in a VIEWCAL build
nothing re-arms phase 34 and **phase 39 holds the burn PLUS one whole phase-3 line body**. The
contamination is a CONSTANT while the burn scales with N, which is why the two-point fit removes it
exactly and why the linearity check is the right instrument.

⭐⭐ **So the note that used to stand at `src/platform/probe.h:214` is RETRACTED.** It read "either
the beam brackets inflate or a 68000 instruction here costs far more than an instruction count
suggests". The first disjunct is now refuted to 0.5%; **the surviving one is that the code really
does cost ~6× its instruction count**, and the mechanism is still open. ⚠ It is NOT chip-RAM
contention on the rig versus the target: **fast versus chip RAM is not a usable explanation for
anything on a 68000** (user-stated, 2026-09-12) — on an A500 "fast RAM" is usually slow RAM on the
same bus anyway, and even off-bus the difference is negligible because the 68000 is simply slow and
every access costs. That is a 68020-era distinction; do not reason from it here. The standing rule
holds unchanged: **RAM is uniformly slow — reduce the NUMBER of accesses.**

**2. The four-way split (`VIEWP3=1`), net of its own control.** ⭐ Phase **31 is an EMPTY bracket
at the same rate — THE CONTROL, and it is read first**: 417 ticks = 104 µs/line, i.e. the split's
six extra bracket transitions per line add ~12 ms/frame of pure instrument that must be netted out.

| bracket | ticks/line | net µs/line |
|---|---|---|
| 35 chain A stop | 851 | 108 |
| **36 chain A entry + boundary** | **2045** | **406** |
| 37 chain B stop | 614 | 49 |
| **38 chain B entry + boundary** | **2580** | **540** |
| 34 remainder (2 brackets) | 964 | 32 |

Total ≈1135 µs/line = **28.4 ms/frame — the third independent agreement** with the table's 27 and
the calibration's 29.2. **83% of phase 3 is the two chain-ENTRY brackets; the planted stops are
14%; the cell loop is negligible.**

**3. The empty body (`VIEWP3=2`) answers the interrupt question.** `units=0/frame` proves the body
truly did not run, and phase 34 falls to a net 88 µs/line ≈ 2.2 ms/frame over the same 25
iterations. **Phase 3's milliseconds are genuinely in its body**, not ISR time accumulating inside
an open bracket.

**4. No chains (`VIEWP3=3`) prices the heritage overhead.** ⚠ `=3` uses `ifeq` on top of the base
`ifdef VIEWP3`, so it defines BOTH `REVS_VIEWP3` and `REVS_VIEWP3_NOCHAIN` — which is what makes it
bracket-for-bracket comparable with `=1`. The **sanity check is the two stop brackets holding
still** (35: 434→403, 37: 197→187) while the entry brackets collapse (36: 1628→222, 38:
2163→605).

⚠⚠ **HISTORICAL INFERENCE, RETRACTED BELOW:** the chain runs appeared to be 740 µs/line = 18.5 ms/frame in phase 3, and ~7 ms/frame in phase 2
(bracket 33 went 15 → 8 ms with units 426 → 213 and runs 32 → 16). This is the SMC-simulation
overhead apparently quantified at ~25 ms of a 256 ms frame ≈ 10%.** The fit and the later null did
not identify that cost; the direct phase-30 bracket below replaces this claim.

#### ⚠⚠ HISTORICAL FIT, SUPERSEDED BY THE DIRECT BRACKET BELOW — dividing by runs did not identify a run cost

This section first wrote the 18.5 ms up as *66 µs/unit against phase 1's 15 — 4.4×*, and read that
as a per-unit premium the port's machinery charges on each cell. **That reading is retracted. There
is no per-unit premium at all**, and the correction matters because the two readings name different
code to replace: a per-unit premium says rewrite the cell loop, a per-run cost says rewrite the run
ENTRY and leave the loop alone.

Divide the same table by RUNS instead of by units and the premium vanishes:

| | ms/frame | units | runs | units/run | µs/**unit** | µs/**run** |
|---|---|---|---|---|---|---|
| phase 1 (24) | 22 | 1442 | 36 | 40.06 | 15 | **611** |
| phase 2 (33) | 15 | 426 | 32 | 13.31 | 35 | **469** |
| phase 3 (34) | 27 | 282 | 50 | 5.64 | 98 | **540** |

⚠⚠ **The following was a fit, not a decomposition.** Fitting `cost = A + B·units` on phases 1 and
3 gave A ≈ 528 µs and B ≈ 2.1 µs/unit. That interpretation is now retracted. Runs per line are
1, 2 and 2 in phases 1, 2 and 3, so the predictors "line", "run" and their enclosed boundary work
are nearly collinear. A absorbs every cost that scales with lines-or-runs; B is correspondingly
driven too low. The fit cannot say that a run costs 528 µs, that the unit loop costs 2.1 µs/unit,
or that the unit loop is 7% of the sweep. The direct phase-30 measurement below settles the last
two numbers independently.

⭐ **The arm mix was measured, and it REFUTES the per-unit reading rather than merely failing to
support it.** `src/platform/shape.h`'s view-consume counters (`make SHAPE=1`, `REVS_SHAPE_WATCH=N`)
split every `view_consume` call by the arm it takes — `clean` (zero source, return the carried
byte), `dirty` (zero it, translate through `view_cell_bytes`), `forced` (a run's first unit, always
translating). Host, `REVS_FIXED_RNG=1`, 600 frames, all three sum identities `ok`:

| | clean | dirty | forced | units/run |
|---|---|---|---|---|
| phase 1 | 89% | 10% | 0% | 40.06 |
| phase 2 | 89% | 6% | 3% | 13.31 |
| phase 3 | **64%** | 17% | 17% | 5.64 |

Phase 3 really is on the dear arm three times as often — the hypothesis was the right shape — but
solve the two-arm model `0.90C + 0.10D = 15`, `0.66C + 0.34D = 66` and it returns **C = −6 µs**.
A negative cost for a clean unit is not a fit that needs better data; it is a refutation. No arm
cost whatsoever makes a 24-point shift in the mix produce a 51 µs/unit difference, because the
difference is not per-unit.

⚠ **`units/run` is also the CONTROL that licenses reading host arm shares onto the target.** The
host runs a different trajectory (the 50 Hz body once per game frame, not ~50 ticks per paint), so
its absolute counts are its own — but units/run comes out **40.06 / 13.31 / 5.64** on the host
against **1442/36, 426/32, 282/50 = 40.06 / 13.31 / 5.64** on the Amiga. Identical to three
figures in all three phases, so the sweep's structure is the same on both and the mix carries.

⭐ **What the corrected reading leaves for step 2, and what it hands to a SEPARATE item.** The
~25 ms is per-run entry cost in phases 2 and 3, and `VIEWP3=3` removing the chain entries is
precisely what measured it (~370 of phase 3's ~540 µs/run), so the ~10% prize looked to stand — the
descriptor rewrite must replace the **run entry**, not the cell loop. ⚠⚠ **That conclusion was
tested and is RETRACTED — see the next section.** ⚠⚠ And it cannot be extended
to phase 1: phase 1's row does **not move** under either `VIEWP3=2` or `=3` (22 ms, 1442 units, 36
runs, 619 µs/line in all three builds), because `view_enter_chain` is phases 2/3's entry and not
its. So phase 1's own ~530 µs/line of per-line driver — 40 units at ~2 µs is only 80 µs of the 611
— is **19 ms/frame of a different subject**, one no chain rewrite touches: `step_scanline`, the
background-byte lookup, the segment arithmetic, `view_stop_from` and two `view_span_is_ram` tests,
~3800 cycles for work whose instruction count is nothing like that. It is the same open ~6× that
§The four view probes' calibration left standing, and it is now localised to a named 530 µs.

#### ⭐⭐⭐ …AND THE RUN-ENTRY REWRITE WAS BUILT AND IS A **NULL**: −0.15%. The ~10% prize is RETRACTED

The paragraph above says the ~25 ms is the run ENTRY and the prize stands. It was built, and it is
not. `paint_run_one` — a specialised single-run, single-line, flat-span chain entry in
`view_enter_chain`, replacing `paint_cells` for every one of phases 2 and 3's entries — measured
**4.557 FPS against a 4.564 control**: −0.15%, an order of magnitude below the 3% floor.

What it deleted from 66 of the frame's 118 runs: the eleven-register `movem` frame, the
`oneSeg`/`segBase`/`segEnd`/`segLimit`/`lastSeg`/`curUnit` derivation, the outer segment `for(;;)`
and its crossing tail, the `stopHere` test, the per-unit `busSafe` branch (hoisted to a
precondition), the `advance_first` test and the `$7EEE` terminator read on the stop exit. It kept
the unit loop and the consume, byte for byte.

**Three controls, because a null is only a result if the change provably happened:**

- **It ran.** The fall-back counter (`g_shapeViewSlow`, `make SHAPE=1`) read **0 chain entries** on
  all five trajectories — parked 300, drive 300, steer 300, crash 1500 and the 13 000-frame race,
  1.07 M entries — i.e. no precondition ever failed. Positive control: forcing the fast path off
  made it read 16 236 entries in 250 frames = **65 runs/frame**, the 66 of 118 the table predicts.
- **It was emitted.** `paint_run_one` inlines into `view_enter_chain`, which goes **6 → 324
  instructions** in the objdump while `paint_cells` stays at 642 and keeps phase 1 and the
  fall-back. The specialised path is what the target executes.
- **It was faithful.** `validate FN=view_paint_lines` 700 cases / 0 mismatch, all five determinism
  trajectories byte-identical, `viewdiff` matching the real BBC on every circuit, `mode7`,
  `tracks`, `track-run`, `transtrap` green — plus a per-entry shadow differential (both paths run
  on the same 64 KB, then diffed) finding zero divergence, itself verified by five sabotages: four
  caught, and the fifth (`& 0xFF` → `& 0xFE` on `stopUnit << 3`) is a provable no-change whose
  sibling `& 0xF7` **was** caught.

⭐⭐ **What the null proves is narrower: this implementation and its proposed ~10% prize are dead.**
It does **not** identify the fitted intercept or prove that run setup is absent. Post-hoc objdump of
the saved experimental ELF showed that the specialised `view_enter_chain` became a 1044-byte
out-of-line helper and made values in the retained clean-unit loop stack-resident. The resulting
loop overhead can plausibly repay several milliseconds of the roughly 1-2 ms of setup that was
deleted. Therefore the defensible result is "this shape measured 0%; do not retry it", not "the
intercept is not setup" or "setup is worth at most 1%." The intercept remains unlocated because
the original fit did not identify it.

⚠ **The code was not kept.** 324 instructions duplicating the unit loop, for 0%. The finding is
the deliverable; `src/platform/shape.h`'s arm and run counters stay because they are what priced
it, and re-deriving the path from this section is an afternoon if a later change ever needs it.

⚠ **The earlier static attempt to validate the 530 µs intercept is superseded too.** Counting 637
instructions in `paint_cells` established code size, not the executed path or ownership of the
fitted intercept. It remains useful compiler evidence—the per-line path is duplicated across many
back-edges—but it cannot turn the collinear fit into a measurement. Use the direct bracket below,
or trace an executed path, before assigning that cost.

#### ⭐⭐⭐ DIRECT TARGET MEASUREMENT (2026-09-12) — the unit/run interior is **29.0 ms/frame**, not 4.3 ms

`PROBES=1 VIEWSPLIT=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, FS-UAE warp, measured the
production unit/run interior with phase 30 and the adjacent empty control phase 31. The bracket
starts after base/source/segment/stop/`busSafe` setup and includes `runEnd`, the unit loops,
source consume/translation/clear, destination stores and the stop tail.

| run | phase 30 ticks | phase 31 ticks | frames | differential |
|---|---:|---:|---:|---:|
| 1 | 268099658 | 74577762 | 1480 | **32.641 ms/frame** |
| 2 | 180673369 | 50316070 | 995 | **32.704 ms/frame** |

The repeat spread is 0.2%. Phase 30 also carried probe-only census increments. A temporary build
with those counters compiled out measured 30.306 ms/frame. Correcting the open bracket for its
expected VERTB and band interrupts gives **29.029 ms/frame**. The exact production census is
**2148 unit visits, 118 runs and 77 lines/frame**, hence an effective **13.51 µs/unit** for this
whole interior. VIEWCAL1/VIEWCAL3 measured 3208/7362 µs per call; after the same ISR correction the
known 1975 µs calibration slope was reproduced to about 1%, so the bracket scale is sound.

This directly falsifies B = 2.1 µs/unit by about **6.4×**. It does not assign all 29 ms to the
source byte load: the bracket deliberately contains the complete unit/run interior. A second
controlled split does assign the largest parts:

| build | phase 1 | phase 2 | phase 3 | interpretation |
|---|---:|---:|---:|---|
| control | 22 ms | 15 ms | 27 ms | normal consume + destination store |
| `NOUNITS=3` | 17 ms | 13 ms | 26 ms | consume/clear retained; destination store suppressed |
| difference | **5 ms** | **2 ms** | **1 ms** | about **8 ms/frame of stores** |

The remaining **~21 ms/frame** is source scanning/testing, dirty translation and clearing,
pointer/loop control and run control. The current moving census is approximately **88% clean,
8% dirty and 3% forced**. The old "2093 units / 83 changes = 96% empty" premise mixed unlike
quantities: unit visits in the viewport with framebuffer bytes whose *value changed*. It is not a
source-arm census and must not size an optimisation.

**Consequence.** A local store rewrite can reach only the 8 ms store floor. The next consumer
experiment should change the representation: have producers emit per-line dirty events/runs and
iterate those instead of testing all 2148 source slots. A five-bit line mask alone is not enough if
the selected line still scans all forty units; the representation must name the changed units or
runs. Preserve the full consumer as the byte-exact oracle.

#### ⭐⭐⭐ OTHER TARGET SPLITS (2026-09-12) — where the next frame reductions can come from

| subsystem | measured split | conclusion |
|---|---|---|
| framebuffer decode | **was 38 ms = 23 ms discovery/shadow scan + 15 ms dirty-cell expansion; now ~22 ms** | ⭐ **CLOSED for now at +8.1%** — the cost was CODE SHAPE, not algorithm (next section). The two-map experiment removes the scan but loses ~8.5% end to end: per-store compare + two map RMWs cost more than the batched longword scan, and it has been reverted. |
| `draw_road` | **43 ms = ~24 ms `span_walk` + ~13 ms surrounding surface setup + 4 ms attributes + 1 ms marks** | The old claim that span setup dominates and columns are nearly free is false. Rewrite `interp_edge` and `span_walk` together as one native SpanPlan/DDA kernel; removing only one side preserves the representation tax. |
| `build_track_geometry` | **28 ms = ~25 ms in the two point walks**; 27 points, zero subdivisions | The old restoring divider is not active here (`g_geoDiv=0`); native paths use `revs_divu16`/DIVU. Carry native `EdgePoint` values between stages instead of publishing and reconstructing byte-lane scratch records. |
| dash edge | **17 ms for 151 cells**, all on the same production arm | A production-arm specialization remains a plausible 5-10 ms item, behind the larger representation changes. |

The decoder split has an independent FPS ceiling check from the same session. Shipping control was
**4.570 FPS** (46.8 painted frames/512 VBLs); `NODECODE` was **5.586 FPS** (57.2/512), +22.2%,
implying about 40 ms/frame removed. The screen is intentionally wrong in `NODECODE`, so this is a
ceiling, not a shippable result. The temporary scan-only decoder retained shadow comparison/update
but skipped bitplane expansion and measured 23 ms against the normal phase-27 38 ms. Thus the
23/15 discovery/expansion split agrees with the independent ~40 ms gross ceiling.

The hot view range contains no actual `cpu` state accesses; geometry/road has one hot use
(`cpu.S+2` for a cap helper). Removing `cpu` from the whole file remains worthwhile hygiene, but it
is not the first performance lever. The expensive 6502 inheritance is now chiefly **representation**:
`mem[]` scratch, split byte lanes, simulated self-modifying slots, and values repeatedly published
and reconstructed between stages.

At the then-measured ~4.57 FPS baseline (~219 ms/painted frame), the credible local programme was:

1. a combined native road span kernel (15-25 ms);
2. producer-emitted source dirty events/runs (12-18 ms);
3. a native geometry value pipeline (8-12 ms);
4. dash specialization (5-10 ms).

⚠ Item 0 — the decode itself — has since been taken and paid **+8.1%** (next section), so the
baseline these ranges are measured against is now ~4.93 FPS / ~203 ms and each remaining item is a
smaller FRACTION of the frame than when it was sized. Re-price before building, do not assume.

Those ranges imply roughly **6-7 FPS**, not 25 FPS. Reaching 25 FPS (40 ms/frame) requires the
architectural version: world points → native spans/events → Amiga bitplanes, bypassing the chain
of split BBC edge arrays → SMC-style span scratch → forty source blocks → BBC framebuffer → shadow
decode. The failed direct plotter attached only at the last arrow and therefore retained nearly all
upstream cost.

#### ⭐⭐⭐ THE DECODE WAS CODE SHAPE, NOT ALGORITHM — 38 ms -> ~22 ms, +8.1% (2026-09-13)

**Measured**: control **4.564 FPS**, rewrite **4.934 FPS** — `fps_series.gdb` row vectors, both
clean `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` builds, 30 s warp runs, **same session**, the two
off-track/reset rows (~3.2 / ~3.5) dropped. 219.1 -> 202.7 ms per painted frame = **16.4 ms**.
⭐ The rows do not overlap: the new build's worst non-outlier row (4.78) beats the control's best
(4.68), which is what makes an 8% claim safe at a 3% noise floor — **quote the separation, not just
the means**.

⭐⭐ **THE DIAGNOSIS CAME OUT OF THE OBJDUMP, AND NO EMULATOR RUN WAS NEEDED TO FIND IT.** Four
successive codegen rounds, each read back from `m68k-amiga-elf-objdump`, against zero runs. This is
the cheap high-yield alternative to another probing session (`docs/method-lessons.md`).

What the clean-cell scan path was actually spending its ~140 cycles on — only ~50 of them were the
four essential longword reads:

- `tst.l 48(sp)` + `tst.l 52(sp)` = **32 cycles re-reading the loop-invariant `shadowRow` and
  `rowDirty` off the stack on every one of the 760 cells**;
- five induction variables advanced per cell (a3/a6/d2/a5/d3), with two shadow pointers parked in
  DATA registers and copied into a0/a1 each iteration, plus an a5 spill/reload around the
  expansion. ⭐ **The function had run out of address registers — the signal is pointers living in
  `d` registers and a `tst.l <n>(sp)` on a value that cannot change inside the loop.**
- the expansion used indexed `(0,a4,a0.l)` instead of post-increment, re-tested `mode[l]` per line
  when it is invariant across all 40 cells of a row, and reloaded `moveq #8,d0` every iteration.

The fix is all shape, no algorithm:

1. classify the row ONCE as uniform MODE 5 / MODE 4 / mixed, dispatch a 3-arm switch into
   `always_inline` specialisations — the per-line `mode[]` test folds away for the ~17 of 19 rows
   that are uniform;
2. **split the fused scan+expand into two passes** — find what moved into a `uint8_t changed[40]`,
   then expand only those. This is what frees the registers; fusing was the register pressure.
3. `#pragma GCC unroll 8` on the expansion, post-increment longword walks in the scan.
   ⭐ **GCC 15.1.0 supports `#pragma GCC unroll N` and will NOT unroll a constant-trip-count
   8-iteration loop unasked at -O3** — worth knowing before concluding a small loop is already flat.
4. `__builtin_expect(s0 != h0 || s1 != h1, 0)` so the COMMON clean cell falls through. Without it
   GCC hoisted the second compare out of line and routed the common case through two taken `beq.w`.
   **128 -> ~80 cycles on the clean-cell path.**

⭐ **THE SCAN VISITS 760 CELLS A FRAME, NOT 1040, AND THAT IS DERIVABLE STATICALLY.** Band 1 (flat
blue sky) spans display lines 18.0..81.1 in `src/platform/bbc_screen.h`, so character rows 3..9 lie
wholly inside it, read `any == 0` and are skipped: 19 x 40 = 760. Therefore 23 ms / 760 =
30 us/cell = **~212 cycles at 7.09 MHz against ~140 nominal — a DMA-contention factor of ~1.6**, on
the instruction fetch as much as on the data. Use that factor when converting a cycle count into a
predicted millisecond figure; it took the prediction here (14 ms) to within 15% of the measurement
(16.4 ms).

⚠⚠ **THE PER-LINE MODE-DIRTY REFINEMENT IS A NULL IN THIS WORKLOAD.** Replacing the whole-row
mode-dirty flag with a per-LINE bitmask plus a `revs_expand_line` repair pass gave `modeLines=10`
against `modeDirtyRows=56`, and cells/frame moved 3301 -> 3291 (**0.3%**). Why: `update_horizon_band`
sweeps the boundary ~12 display lines per PAINTED frame, more than a whole character row, so a
mode-dirty row usually has all eight lines changed and takes the full path anyway. It is kept
because it is correct and strictly better in principle; it is credited with nothing.
⭐ The companion finding is the useful one: of ~92 cells expanded per frame, **~60 are the horizon
band sliding, not pixels changing** — and those are rows the rising horizon newly revealed, so they
are genuinely new pixels and irreducible. The expansion's remaining prize is the unroll, not the
selection.

**How it was proven byte-exact — and why ONE oracle was not enough.** `DIRTYCHECK=1` re-decodes each
frame unconditionally and requires byte agreement: `checks=28 mismatch=0 firstOff=65535`, with
`$61=ff $3C=31` proving the car was under power. ⚠ **But the two paths it compares SHARE
`revs_expand_cell`, so it is blind to a bug in the expansion itself** — a common-mode defect passes.
The independent check is `FILLWATCH=1` "check 2", which re-derives the expected bitplane bytes from
`mem[]` through `s_expandLo`/`s_expandHi` and compares against `dst` over display lines 74..167:
`decode mismatch=0 firstLine=65535` over 197 painted frames, with `horizon change max=2091 cells`
confirming the scene was moving. ⭐ **Ask what the two sides of an oracle have in common before
believing it** (`docs/validation-harness.md`).
⚠ A `planes.bin` diff across the two BUILDS was considered and rejected as an equivalence check:
they run at different speeds, so at a fixed `g_vbiCount` the sim has advanced differently and the
picture legitimately differs.
⚠ `DIRTYCHECK=1` converts twice per frame — **never quote a framerate from it**, nor from
`FILLWATCH=1`.

### ⭐ `fill_dash_edge_columns` (phase 18, 17 ms) decomposed — 151 cells, ALL on one arm

Phase 18 is the fifth-biggest row in the frame for a driver whose whole job is **twelve columns**,
and "it is per-item setup like the rest" was an assumption until it was counted. `src/platform/shape.h`
now carries dash-edge counters (`make SHAPE=1`, `REVS_SHAPE_WATCH=N` on the host build; the arms are
hooked in `column_gap_walk_core`, and in a non-SHAPE build the cell counter is `#ifdef`-guarded so
the shipping build provably pays nothing rather than relying on GCC to delete it).

Constant across every watch interval, host, `REVS_FIXED_RNG=1`:
**25 walks/call, 151.00 cells/call, skip=0, table=0, colour=151, fallback=132 (87%)**; cells-per-walk
histogram (buckets of 8) = 20 walks of 0-7, 3 of 8-15, 2 of 16-23.

⭐ **The instrument's self-check is the SUM IDENTITY**: `skip + table + colour == cells`
(`0 + 0 + 151 = 151`), which proves no cell took an uncounted path — the thing that would otherwise
make a zero arm indistinguishable from a misplaced hook. And the CONSTANCY is explained rather than
suspicious: the dash edge is fixed geometry (columns `$03..$06` and `$1A..$22`, block starts from a
static table), so it does not vary with the trajectory.

Three findings:
1. ⭐⭐ **EVERY cell takes the empty-cell arm.** The `$09` skip and the `offset != 0xEF` trap are
   **dead on the game trajectory** — they exist only for the randomised fixture. Under the governing
   rule (validate RESULTS on the data the engine actually produces) they are specialisable.
2. **17 ms for 151 cells is ~113 µs per cell — 7.5× a view-sweep cell**, the most expensive
   per-item cost anywhere in the frame. `column_gap_walk_core` re-reads all three patched operands
   **per cell**, plus `mem[EDGE_BLOCK_START]` as the loop test and `mem[EDGE_COLUMN]` inside
   `surface_colour_at_core`; and each column is walked TWICE (once via `plot_ptr2` into the
   boundary table, once via `plot_ptr` into its own source block).
3. **87% of cells get no surface colour** and fall back to the patched constant — so
   `surface_colour_at_core` is called on all 151 and earns its answer on 19.

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
  ⭐⭐ **AUDITED SITE BY SITE IN `revs_native.c` AND THE CLASS IS CLOSED (2026-09-09).** All 56
  remaining `bus_*` calls there are one of four things, and none is a hoist that was missed:
  **(a) real hardware** — the two VIAs, the CRTC, the Video ULA, and every `USRVIA_T2CL` entropy
  read (the starter's luck, the mirror shudder, the crash disturbance, `check_car_pair`'s cursor);
  **(b) the IRQ1V vector-page writes**, plain RAM but routed deliberately so `platform_shadow_write`
  sees the game claim the vector; **(c) an already-hoisted `else` arm** — `pointer_is_ram(base)`
  + `seam_write(addr, ram, val)` with the fast path a bare `mem[]` store (the road fill, the gap
  walk, the surface writers, `mirrors_update`, the object fields); **(d) a write that is already
  once per SCAN LINE, not per cell** — the view painter's three start/end cell stores sit in the
  sweep's `for(;;)` line loop, so converting them would save ~255 range tests a frame out of
  ~10 600 bus calls. ⚠ Under 3% is noise, so **do not churn (d)** — and the else arm is not
  removable anyway, because under a randomised fixture a pointer really can land in SHEILA.
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
