# Performance method — how to get a number you can trust

> ⚑ Method inherited from the Atari port (*Rescue on Fractalus!*), where each rule below was
> learned by getting a number wrong first. The *numbers* are Revs's own. Companion docs:
> `docs/m68k-optimisation.md` (what to do once you know where the time goes),
> `docs/headless-fsuae.md` (how to drive the target), `docs/direct-bitplane-plan.md` (the
> representation-level plan the current table points at).

## ⭐⭐⭐ WHAT THE ORIGINAL HARDWARE ACHIEVES — 97.0 ms a frame, 10.31 fps

**Every other number in this file is the port's cost with nothing to compare it to.** This one is
the comparison, and it is measured, not remembered:

```
make refloop FRAMES=120          # the last block it prints

THE REAL BBC'S FRAME COST over 165 settled frames at $1701:
   median 193994 cycles = 97.0 ms = 10.31 fps   (mean 112.1 ms, p10 85.4, p90 101.3)
   calibration: one PAL field measures 40000 cycles, a 312x64us field is 39936 — 0.16% off  ✓
```

`$1701` is the engine's own per-frame back-edge, so the gap between two hits is **one whole game
frame** in 2 MHz cycles — the road rasteriser, the view pass, the dashboard, the MOS's interrupts
and the 50 Hz body all inside it. Silverstone practice, driving, the same scene the port is
profiled on. Reported as a **median**: the engine's crash/reset holds are a different workload,
exactly as phase 0 is on the Amiga side. The calibration line is not optional decoration — it is
the known-quantity check (`docs/method-lessons.md`) that the cycle accessor is being read right,
and it is printed beside the figure so the figure is never read without it.

### What it reframes

| | ms/frame | vs the real BBC |
|---|---:|---:|
| **the real BBC, its own hardware** | **97.0** | 1.00× |
| the port, bracketed | ~190 | **1.96× slower** |
| the port, less `decode()` (phase 27, work the BBC never did) | ~173 | 1.78× |
| the stated **floor**, 25 FPS | 40 | **2.43× FASTER than the original** |
| the stated **target**, 50 FPS | 20 | **4.85× FASTER than the original** |

Three things follow, and they are the numeric case for the rewrite the governing directive
licenses rather than for more tuning:

1. **The port is not 10× off a reasonable figure; it is 2× off the original.** "5-10× problem" in
   the queue header is measured against a target nobody ever priced. Against the game as it
   actually ran, the gap is a factor of two.
2. **The targets were never derived from this game.** 50 FPS is the Amiga's field rate, not a
   number Revs ever produced; the original is a ~10 fps game. Reaching 20 ms means running
   Crammond's 1985 simulation **almost five times faster than he did**, on a CPU that is not five
   times the machine.
3. **And the headroom for that is not in the instruction set.** A 68000 at 7.09 MHz has a 4-clock
   bus cycle — 564 ns against the 2 MHz 6502's 500 ns — so **per byte touched it is marginally
   SLOWER**, and it wins only where bytes can be batched into words/longwords or held in its
   sixteen registers. The BBC layout the port inherited forbids exactly that batching: destination
   cells 8 bytes apart, sources 128 apart (`VIEW_UNIT`), which is why the sweep's unit loop
   measured 43 cyc/unit against a modelled 43 and **no code shape can improve it**. ⇒ **A 4.85×
   win cannot come from making this work faster. It can only come from not doing it** — changing
   the layouts on the producer and consumer sides at once so the 68000's word moves apply.

⇒ **Quote every frame figure against 97.0 ms from now on**, and size an architecture proposal by
which side of the 1.96× it is trying to close. Anything that only chips at the port's overhead is
bounded by ~93 ms and cannot reach 40, let alone 20.

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
| 58.7 | 24+33+34+32 | **`view_paint_lines`** (the CONSUMER) — sweep 15.6 · phase-2 12.4 · phase-3 24.5 · tail 6.3 | native, whole tree |
| **34.1** | 11 | **`draw_road`** (`$1A20`) — **was 38.9; the packed span-plotter ABI, §below** | native, whole tree |
| 26.4 | 27 | `RevsScreen::decode()` | port |
| 26.7 | 5 | **`build_track_geometry`** (`$24F6`) | native, whole tree |
| 13.5 | 26 | the 50 Hz drain (`irq1v_band_schedule`) | native |
| 11.0 | 28 | the vblank spin | port |
| **10.4** | 18 | `fill_dash_edge_columns` — **was 17.1, see §fixed below** | native |
| 5.1 · 3.6 · 3.1 | 3 · 4 · 15 | the next three rows down | native |
| ≤2 each | 29, 14, 13, 10, 7, and the rest of the 24-call body | remaining drivers and leaves | mixed |

Re-measured 2026-09-13 at HEAD `9dfdf4e` (`PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1
HOLD_THROTTLE=1`, warp, 30 s, driving), 670 loop frames, **`accounted 95.7%`**: **209.4 ms/frame
bracketed, 218.8 ms wall** (`vbi/loopFrames × 20 ms` — quote that one for the frame, the bracketed
sum omits phase 0). The three pipeline rows are `58.7 + 38.9 + 26.7 = 124.3 ms ≈ 59%` of the
bracketed total. The same-session HEAD-minus-the-change control read **214.4 ms bracketed / 221.0
wall**, and every row above except phase 18 reproduced within 1%, which is what makes this table
diffable against the next one taken the same way.
⭐ **`draw_road` was re-measured at `ea02975`: 34.10 ms, from two bracketing control runs that read
38.89 and 38.90** (the packed span-plotter ABI, §below). Its −4.79 ms did **not** appear in the
frame total — the vblank spin (phase 28) took +3.03 ms of it, which is Rule 1a's pad seen from the
phase table. **Σ(1..39) − phase 28** moved 197.05 → 192.63 ms; that is the row to diff.
⭐ **...and the span walk's step/marker OPCODE SLOTS became values at `8bc45ac`: `draw_road`
34.09 → 33.70 ms (−0.40), the frame 206.09 → 205.49 bracketed (−0.60).** Same-session control
(`HEAD~1`'s `revs_native.c`, a clean build and a 30 s warp run each side, `PROBES=1 FIXED_RNG=1
STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, 668 loop frames both). Every other phase moved ≤0.05 ms except
the vblank spin (−0.11 — Rule 1a's pad again), and the bracketed Σ(1..39) equals `FRAME` to 0.00 ms
on BOTH sides, which is the identity that makes two runs diffable at this size.
⚠ **The prediction was 0.7-1.0 ms and the truth was 0.4-0.6.** Seven `mem[]` byte writes per span
plus two indirect opcode loads and two three-way switches per plot came to 0.40 ms at 43 spans and
~60 plots a frame ⇒ **price this class at ~10 µs per eliminated per-span byte round trip**, and do
not expect a byte-traffic deletion to pay more than its own count. FPS did not move at all (5.56
both sides, ten non-outlier rows): 0.3% of a frame is far under one painted frame's 3.3%, exactly
what Rule 1a says to expect.

⚠⚠ **A frame figure is only comparable WITHIN ITS OWN SCOPE, and three of them are in circulation
for the same frame.** Measured together in one run (2026-09-13, `8bc45ac`): **wall** (`vbi /
loopFrames × 20 ms`) = 214.3; **bracketed Σ(1..39)** = 205.49 — this doc's convention, and the only
one the phase table can verify; **Σ minus the frame wait (25), the 50 Hz drain (26) and the vblank
spin (28)** = 178.94, i.e. engine work with the pads and the body taken out. So a "178.5" sitting
next to a "205.5" is the SAME FRAME TWICE, not a 27 ms regression — check the scope before reading
a drift of that size as either a win or a loss.

⚠ The previous table (2026-09-12, HEAD `8629ff9`, 530 loop frames, `accounted 96.8%`) read
**~256 ms/frame** with the consumer at 70 and the dash edge at 17 — a different session on a
different trajectory, so the drop to 209 is NOT a win anyone earned. ⚠ These are same-run
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
  ⚠⚠ **But do NOT read that 24 ms as "3 500 cycles per DDA scan line" and go hunting the walk's
  inner loop** — dividing by 49 is the wrong denominator and it cost a pass to find out. The call
  and search surface of the whole kernel is ~5 ms and removing it was worth **+0.8%**; see the
  subsection below.

#### ⭐⭐ THE SPAN KERNEL'S CALL AND SEARCH SURFACE IS ONLY ~5 ms OF THE 37 — +0.8% (2026-09-13)

The objdump method that paid on the framebuffer decode was pointed at `interp_edge_core` next (the
span walk is `always_inline`d into it four ways, so the whole kernel is one 5.5 KB function and
`span_walk` has no symbol of its own). It found three real defects:

1. **The span plotter's `SpanPlotter` descriptor was a live memory operand after all.**
   `span_plot_core` is `always_inline` *precisely* so the five slot fields fold to immediates — but
   its only native caller was `sw_plot(usePlot2, ...)` picking `usePlot2 ? &SPAN_PLOT_2 :
   &SPAN_PLOT_1`, so inside the leaf the descriptor was a **runtime value** and nothing folded:
   `lea SPAN_PLOT_2,a2` then `move.l (a2),d2` / `move.l 8(a2),d3` / `move.l 12(a2),d4`.
   ⭐⭐ **`always_inline` on the leaf does not satisfy the descriptor rule — the SELECTION has to be
   specialised too.** Two `noinline` twins (`sw_plot_1`/`sw_plot_2`) fixed it.
2. **`span_end_marker`'s "switched off" answer cost a five-argument out-of-line call**, ~90 times a
   frame, where `interp_edge` plants `RTS` over both markers whenever the run needs no terminator.
3. **`span_entry_decode` SEARCHED three `static const uint8_t[8]` tables** to look up a
   compile-time constant — up to sixteen `move.b <abs.l>` reads, ~300 cycles per span.

`interp_edge_core`'s setup path went 1644 → 1298 instructions. **Measured +0.8%** (4.969 against a
same-session clean control at 4.930; four of ten comparable rows one painted frame better, none
worse) — real, directional, and **well under the 3% floor**, so quoted from the static cycle count.

⭐⭐⭐ **WHY IT IS SMALL, WHICH IS THE ACTUAL RESULT: divide by the right denominator.** A frame holds
**43 spans, 49 DDA scan lines, 60 plotted columns and ~90 marker calls.** The per-plot and
per-marker overhead therefore cannot be more than ~5 ms of a 37 ms pass however badly it is
compiled. "~3 500 cycles per DDA scan line" — the figure that pointed at this work — is an artefact
of dividing 24 ms by 49; the honest denominator is **43 spans at ~6 100 cycles each**, and the
weight is in `interp_edge_core`'s **per-span SETUP**, ~2 100 cycles of byte-level `mem[]` work
(`SPAN_CLIP`/`LINE_END`/`DX`/`DY`/`YSTEP`/`ARM`/`BLOCK`, four column patterns, five SMC slots, two
patched dest operands) that the objdump shows **already compiled tightly** — `mem` held in `a1`,
indexed reads as `lea (0,a1,d.l),a3` + `move.b d16(a3)`, no spilled invariants. There is **no
code-shape defect here of the kind the decode had.**

⇒ **The combined `interp_edge`+`span_walk` kernel's win has to come from deleting the per-span
REPRESENTATION, not from flattening the call graph** — the call graph has now been flattened and it
was worth 0.8%. ⚠ 37 ms to write **60 cell bytes** is the ratio to attack.
⚠ That last sentence stood for one pass and is now **half retracted**: there WAS one more
code-shape defect, and it was worth −4.79 ms. See the next two subsections.

#### ⭐⭐ THE SPAN RASTERISER, DECOMPOSED IN FOUR — AND THE CONTROL BRACKET THAT MAKES IT READABLE (2026-09-13)

`make ROADSPLIT=1 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` +
`GDBSCRIPT=roadsplit.gdb` now carves phase 45 into four (probe.h §47/48/49):

| bracket | what | instances/frame |
|---|---|---|
| 45 | `interp_edge_core`'s per-span SETUP plus the driver | 26 |
| 47 | `span_walk` — the four inlined DDA arms | 83 (= 24 walks + 60 re-opens after each plot) |
| 48 | `span_plot_core` — one column merged into one cell | 60 |
| **49** | **THE CONTROL: an empty bracket at exactly the plot rate** | 60 |

⭐⭐⭐ **49 IS THE LOAD-BEARING PART, AND IT MEASURES THE INSTRUMENT'S FLOOR AT 107 µs (758 cycles)
PER TRANSITION.** Its bracket contains *nothing*, so its ticks ARE the probe's own cost at this
rate; correcting every row by `instances × 107 µs` drives the control itself to **−0.4 ms ≈ 0**,
which is the verification (and it agrees with CLAUDE.md's documented 92 µs ISRSPLIT floor).
⚠⚠ **So no ROADSPLIT row may be quoted raw**: adding brackets inflated the pass from 42 to 65 ms
and the raw rows over-read by 2.8 / 8.9 / 6.4 ms. And ⚠ **`ROAD_COUNT` is not free either** — each
one is a `volatile unsigned long` RMW, ~40 cycles, and the step counter fires 232 times a frame, so
a floor-corrected ROADSPLIT sum still over-reads a plain `PROBES=1` phase 11 by ~15%. Use ROADSPLIT
for the **ratio**, and the plain phase-11 row for the **absolute**.

Floor-corrected, after the packed-ABI change below (~29 ms of `draw_surface_spans`):

| item | ms/frame | per unit |
|---|---|---|
| per-span SETUP (45) | **~11.5** (40%) | 43 spans × ~1 900 cycles |
| `span_walk` scaffolding (47) | **~10.5** (36%) | 24 walks × ~3 100 cycles, i.e. 232 DDA steps × ~320 |
| `span_plot_core` (48) | **~7** (24%) | 60 plots × ~830 cycles |

⭐⭐ **ONLY 24 OF THE 43 SPANS REACH THE WALK** (47's walk-entry count vs `g_roadSpans`) — the rest
return earlier. Per DRAWN span the pass costs ~8 500 cycles to plot 2.5 cells. That, not the
scan-line count, is the ratio to attack.

⭐ **WHICH early return, measured — a host census of `interp_edge_core`'s four exits over 300
driving frames (12 803 spans, `-DIECENSUS`, no emulator run):**

| exit | count | share | returns after |
|---|---:|---:|---|
| `publishOnly` | 3 306 | 25.8% | step 2 |
| both ends clipped (`SPAN_CLIP & 0xC0 == 0xC0`) | 2 451 | 19.1% | step 2 |
| `SPAN_DX == 0 && SPAN_DY == 0` | **0** | 0% | step 3 |
| **`block >= 0x28` off the side** | **0** | **0%** | the END of setup |
| reaches `span_walk` | 7 046 | 55.0% | — |

⛔ **So the obvious coarse lever here is DEAD: the off-side test sits at the very end of the setup —
after the deltas, the four-iteration colour-pattern loop, both cap surfaces and the `LINE_END`
clamp — while `block` derives only from `shared_temp_7e`, which is known right after step 2. It
looks like ~5 ms of wasted setup and it is worth ZERO, because it never fires.** ⚠ Do not delete
either zero-count test: Silverstone practice is one trajectory, an expansion circuit's hook
re-enters this subtree, and CLAUDE.md's rule is that a body no scenario drives is *unproven*, not
dead.
⇒ **The correction that matters: both non-walking exits leave after step 2, so the ~1 900 cyc
setup is paid by the ~24 spans that WALK, not by 43 — i.e. ~3 400 cycles of setup per drawn span,
to plot 2.5 columns.** Any attack on the per-span representation is an attack on that 3 400.

#### ⭐⭐ …AND THE SECOND CODE-SHAPE DEFECT: FIVE POINTER PARAMETERS WERE THE WALK'S LOOP STATE — −4.79 ms (2026-09-13)

`sw_plot_1`/`sw_plot_2`/`span_plot_core` took `&y`, `&carry`, `&abandoned`; `span_end_marker` took
`&colMark`, `&carry`. **Those five pointers are `span_walk`'s entire DDA state**, so address-taking
them pinned the loop state in the stack frame — the objdump read `adda.l -8(a5),a1` … `move.l
d7,-8(a5)` on every DDA step, **265 a5-relative operands** in `interp_edge_core`, three `pea`s per
plot, and `*y` dereferenced a dozen times inside the leaf.

The fix is a value-in / packed-value-out ABI: the plotters take `(column, y, carryIn)` and return
**one `d0`** — y in the high byte, the exit carry in bit 1, "the chain abandoned" in bit 0
(`SPAN_PLOT_PACK`). `span_end_marker` splits into an `always_inline` `_core` taking `colMark` by
value plus the pointer wrapper the 6502-ABI shims need.

| | control (×2 runs) | packed ABI |
|---|---|---|
| `draw_road` (phase 11) | 38.89 / 38.90 ms | **34.10 ms  (−4.79, −12.3%)** |
| compute (Σ1..39 − spin) | 197.05 ms | 192.63 ms (−4.42) |
| `interp_edge_core` `pea` / a5-operands | 60 / 265 | **8 / 0** |
| `jsr span_end_marker` | 4 | **0** (the marker inlines whole) |

⚠ `interp_edge_core` grew 1708 → 2217 instructions and got **faster** — the 68000's memory access
is 16-20 cycles against 4-8 for a register op, so instruction count is the wrong scoreboard here.

⇒ **THE STANDING CONCLUSION, REVISED.** "No code-shape defect of the kind the decode had" was
wrong once, and the tell both times was the same one: **a hot loop whose state is reachable through
a pointer lives in memory.** Grep a hot kernel's objdump for frame-pointer operands (`n(a5)`,
`n(sp)`) and `pea` before concluding the shape is clean. What is left is genuinely the per-span
REPRESENTATION (~11.5 ms for 43 spans) and the per-walk entry/exit (~10.5 ms for 24 walks).

### ⚠⚠ ...and packing is not free — the NULL that bounds the rule above (`view_paint_lines`)

The same packed-register ABI, applied to the other subsystem that had it: `view_paint_lines`'s
chain and its two drivers threaded a `ViewState { byte, line, cell }` (the 6502's A/X/Y) as a
local of `view_paint_lines_core` whose address went to out-of-line `paint_cells`, pinning all three
in the stack frame. `paint_lines_short` made 45 `v->` accesses per scan line and
`paint_lines_clipped` 28 — ~1570 memory operands a frame that should have been register operands.
Converting to one packed `unsigned` (`VS_PACK`/`VS_BYTE`/`VS_LINE`/`VS_CELL`, bit 31 = trap) did
exactly what the objdump promised and **cost +0.2 ms**:

| | control (×2 runs) | packed ABI |
|---|---|---|
| view total (24+33+34+32) | 58.95 / 58.91 ms | **59.11 ms  (+0.20)** |
| phase 33 (view phase 2) | 12.42 / 12.41 | 12.68 (**+0.27**) |
| phase 11 `draw_road` (untouched) | 34.10 / 34.15 | 34.15 |
| `view_paint_lines_core` `n(sp)` data operands | 58 | **27** |

The mnemonic diff on the driver is the whole explanation — **18 `move.l n(sp)` deleted, 53
shift/mask/merge instructions added** (`swap` 1→12, `or.l` 1→12, `clr.w` 6→17, `andi.l` 23→33,
`lsr.l`+`lsl.l` 12→22):

⇒ **THE 68000 HAS NO BYTE-INSERT INSTRUCTION.** A pack or unpack of three bytes is
`swap`/`clr.w`/`or.l`/`andi.l`/`lsr.l` ≈ 40 cycles — the price of 2-3 `move.l n(sp)` (16-20 each).
So the packed ABI wins only when **one pack serves many reloads**: the span plotters packed once
per span and saved 232 DDA iterations' worth (−4.79 ms); here there were **eleven pack sites
against eighteen deleted loads**, a wash that lands slightly negative.

⭐⭐ **THE QUALIFIER TO THE ADDRESS-TAKEN RULE, AND IT IS THE USEFUL PART.** `&x` escaping to a
CALL is not the same defect as `&x` escaping into a LOOP. GCC's alias analysis knows a local whose
address reaches only call sites can stay in registers *between* calls and need only be reloaded
after each one — `paint_lines_short` has ~7 call boundaries per line, so its real traffic was ~21
memory ops a line, not 45, and the pack/unpack at each boundary costs about what those reloads
cost. **Rank a frame-slot candidate by whether the slot is reloaded per LOOP ITERATION.** A
per-call reload is already nearly free; only a per-iteration one has the reload:pack ratio that
pays.

⚠ And two corrections to how the candidate was *ranked*, both of which overstated it:
- **`lea N(sp),sp` is not loop state** — it is post-call stack cleanup, one instruction per call
  with `pea`-pushed arguments. Exclude it (and large `a6` offsets, which are `mem[]` base
  addressing) from any frame-operand ranking.
- **An `n(sp)` count rises harmlessly when GCC duplicates an epilogue.** `paint_cells` read 14 →
  24 after the change with no new unit-loop traffic at all: the same `tst.l` on `advance_first` and
  the same `48(sp)` and-pair, at shifted offsets, in a duplicated exit block.

The change was fully validated (`make validate FN=view_paint_lines` 700 cases / 0 mismatch; all
four determinism ladders byte-identical) and **reverted anyway, because it costs milliseconds.**
Do not retry the packed ABI on this chain. The ~6× "code costs more than its instruction count" in
phase 1's 530 µs/line driver and phase 3's chain-entry brackets is therefore **still unlocalised**.

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

### ⭐⭐⭐ The plants' denominator, and WHY A SPLIT'S SMALL ROWS ARE NOT SIZINGS (2026-09-17)

The `VIEWP3=1` four-way split, re-run on the post-`214c8ae` build, decomposed phase 3's
5346 cyc/line (control-subtracted against phase 31 VIEWCTL at 815 cyc/line):

| block | cyc/line | share |
|---|---|---|
| P3_STOPA (`view_move_stop`, chain A) | 569 | 10.6% |
| **P3_CHAINA** (entry + run + boundary) | **1715** | 32% |
| P3_STOPB (`view_move_stop`, chain B) | 267 | 5.0% |
| **P3_CHAINB** | **2782** | 52% |
| the rest (line step, hoists, loop) | 254 | 4.8% |

It validates two ways — the five rows sum to 5587 against 5346 unsplit (4.5% over), and the
instrument's own predicted inflation (7 transitions × 815 cyc × 25 lines = 20.1 ms) matches the
observed 21 ms split-vs-unsplit gap. **And its two small rows are still wrong by 3×.**

⭐⭐ **A HOST CENSUS SETTLED IT, AND IT COST NO EMULATOR RUN.** Temporary counters in
`revs_native.c` under `determinism-drive`'s flavour (`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`,
296 sweeps of a moving car), per sweep:

| | per sweep |
|---|---|
| `view_move_stop` chain A3 | 25 calls — **6 actually move** |
| `view_move_stop` chain B3 | 25 calls — **3 actually move** |
| **total `view_plant` calls, all phases** | **25** |
| `view_stop_from` | 118 calls, 66 list steps |
| `view_stops_rescan` | 1 · longest list ever seen: **3** |

So 41 of phase 3's 50 `view_move_stop` calls early-return on one `mem[rec]` read, and all
planting in all three phases is **~1 ms** — which is what `view_plant`'s own do-not-optimise
banner had already published ("the sweep makes 25 plants, so one removed RAM access is ~350
cycles, ~0.05 ms/frame"). The split charged those two blocks 836 cyc/line ⇒ **~2.9 ms, 3× the
truth.**

⇒ **THE RULE: subtracting a fixed control bracket over-credits the SMALLEST blocks, because the
subtraction error is a fixed number of cycles and the block is not.** An 815 cyc/line control
taken off a block whose true cost is ~280 cyc/line is dominated by the subtraction; the 4.5%
over-sum is real but it does not distribute proportionally, it piles onto the small rows. **Read a
split's BIG rows as sizings and check every small one against a count** — and the count is cheap,
because CLAUDE.md already licenses host counters as a proxy for call counts. This is the
"when a win's per-call price comes out implausibly cheap, doubt the denominator" rule running in
reverse: *implausibly expensive* is the same tell.

⚠ It also cost a step of the live plan. `docs/span-render-plan.md` §10p step (5b) had been written
around the plants as "the live step, a faithfulness edit" with a RESULTS-RULE audit attached; at
~1 ms it is not a step at all, and the audit's real finding turned out to be the opposite of
permissive (`copy_dash_data_core(0x80)` stows the $7B00 page back into the $3000 block tails at
`race_main_loop`'s exit, so the opcode slots are cross-RACE state).

### ⭐⭐⭐ ...and where phases 2/3's 29 ms IS: per-line SET-UP, with no hotspot (2026-09-17)

Phase row ÷ lines, minus the run at the measured 43 cyc/cell:

| | cyc/line | run | **per-line SET-UP** | cells painted |
|---|---|---|---|---|
| phase 2 (16 lines, 10.16 ms) | 4502 | 1143 | **3358** | 26.6 of 40 = 67% |
| phase 3 (25 lines, 18.86 ms) | 5348 | 485 | **4862** | 11.3 of 40 = 28% |

⭐ **The set-up has no single hotspot, and that IS the finding.** Per line phase 3 makes ~22 indexed
`mem[]` table reads, 2 SMC operand pokes, 4 `view_compose` calls, 2 unit-table lookups and 2
`view_stop_from` calls; the objdump's main body is ~200 instructions at ~20 cyc each **because
nearly every operand is an indexed `mem[]` byte or a stack slot.** Three candidate causes were
checked and all three are too small: the plants (~1 ms, above), the census instrument
(`PROBE_VIEW_*`, ~680 cyc/line ≈ 13% — real, and it is inside every figure here), and the spills.

⚠ **The spills are the GOOD kind and undoing them is the `column_gap_walk_core` trap.** 95 stack
operands in `paint_lines_short.constprop.0`, 35 of them the four invariants (`line` 48(sp) ×15,
`dstLine` 52(sp) ×9, `srcLine` 60(sp) ×6, `stop<<3` 64(sp) ×5) — but the line loop's back-edge is
`128a6 → 1254a` and **both inline run copies sit on pads past it**, so only 15 of those 35 touches
are in the body (~240 cyc/line). One slot loaded once and read by many out-of-line pads is exactly
the case this doc records as costing +1.38 ms to "fix".

⇒ **Nothing local fixes 3358 cyc/line; doing less per line does.** That is the sizing behind
`docs/span-render-plan.md` §10p (5b)'s span painter: ~26.6 cells at 13.7 cyc/cell + ~300 cyc of
span set-up is ~660 cyc/line against 4502.

### ⭐⭐⭐ WHERE PHASES 2/3's 29 ms IS, SETTLED FROM THE OBJDUMP: THE BODY IS LONG, AND ~89% OF IT RUNS EVERY LINE (2026-09-17)

The bracket split said phase 3 spent 4862 cyc/line on "set-up" and no per-item count came within
3000 cycles of it. The objdump settles it, and there is **no hotspot and no marshalling layer to
delete** — the per-line body is simply ~500 instructions long, and nearly all of them run.

Method (cheap, no emulator run): parse `paint_lines_clipped` / `paint_lines_short.constprop.0`
out of `m68k-amiga-elf-objdump -d out/Revs.elf`, find the line loop's back edge, then bracket the
per-line cost two ways — a **floor** (Dijkstra for the cheapest path from loop head to back edge
with every basic block containing a `view_plant` or `platform_smc_unhandled` call priced at
infinity, which also excludes unit-loop iterations by construction) and a **ceiling** (every
non-cold instruction in the body once), weighting by addressing mode because that is what costs
on a 68000: absolute long 20/24, indexed 14/18, `d16(An)` 12/16, register 4/8.

| | floor | **measured** | ceiling | non-cold instrs | cells/line |
|---|---|---|---|---|---|
| phase 2 `paint_lines_clipped` | — (every route touches a plant block) | **4502** | 4374 + extra unit turns | 365 | 26.6 |
| phase 3 `paint_lines_short` | 2154 | **5348** | 6026 | 499 | 11.3 |

⇒ **Phase 3 executes ~89% of its non-cold loop body on every one of its 25 lines.** Independently
confirmed by a measurement already in this file: `NOUNITS=2` put phase 3 at 80% driver/entry.

**Why the body is long: FOUR CHAIN ENTRIES A LINE.** Phase 3 carries two stops and two entries,
and each one pays its own run set-up, stop tail, `view_compose` pair, unit-table lookup and
`g_viewStopList` linear search — for an average run of **5.6 cells** (282 units / 50 runs). Phase 2
carries two entries at 13.3 cells. The per-line items, read off the objdump:

  * `1254a..12598` / `11c0a..11c82` — **`step_scanline` inlined**, the byte-lane dance in full:
    `(ptr+1)&7` test, lane recombine, `+312`, then high-word/high-byte extraction via
    `clr.w`/`swap`/`lsr.l #8` and a shift back up to build `plot_ptr2_v`. ~230 cyc (phase 3 ~280,
    it has the carry tail). ⭐ **41 lines × ~250 = 1.4 ms/frame, and it is a wide-value rewrite.**
  * `12646..12680` — **`view_stop_from` as an inlined linear search** over `g_viewStopList`,
    38 cyc per entry scanned, twice a line in phase 3.
  * ⭐ the per-line table reads are **already optimal**: `lea (0,a3,d7.l),a5` makes `a5 = mem + line`
    once and every table read is a 12-cyc `move.b 12624(a5),d2`. There is nothing to hoist.

⭐⭐ **AND THE UNIT LOOP IS 54 cyc/cell, NOT 43** — the 43 was phase 1's clean-arm figure and using it
elsewhere is what opened the phantom gap. The inline clean path is four instructions
(`move.b d2,d0` 4 / `move.b d0,(a1)` 8 / `move.b 128(a0),d1` 12 / `bne.s` 8 = 32) plus 14.5 cyc/cell
of amortised quad back edge = ~46 clean, ~54 with the 13% dirty arm. Phase 2's budget then closes
to 7% (model 4196 vs 4502 measured) with **no unexplained remainder**, once the `PROBE_VIEW_*`
census instrument's ~680 cyc/line is also counted — it is inside every ms figure this file quotes.

⭐⭐⭐ **THE PRINCIPLE THIS YIELDS: A BITPLANE SPAN PAINTER PAYS IN PROPORTION TO RUN LENGTH, AND THE
THREE PHASES ARE 40 / 13.3 / 5.6 CELLS PER RUN.** So phase 1 is where direct-to-bitplane writing
wins and phases 2/3 are where it structurally cannot — the reverse of the "phase 2 first, it is the
smaller driver" order that was in the plan. ⚠⚠ And in **no** phase does the span painter delete the
**source walk**: `view_consume`'s RLE has to read every cell's source byte whatever the destination
is, so a cell goes 54 → ~48 cyc (12 source + 8 test + 14 back edge + 13.7 two-plane fill), not to
zero. **The painter's prize is the DRIVER and the DECODE CARVE-OUT, never the stores.**

Phase 1 re-costed with the 54: 17.55 ms / 36 lines = 3457 cyc/line, of which 40 cells × 54 = 2160
(62%) is units and 1297 is driver. A full takeover deletes the 905 cyc/line driver (4.6 ms),
improves the store (~1.3 ms) and carves 36 of 208 rows out of the decode (4.2 ms) ⇒ the published
−8 to −11 ms, **whose largest single component is the decode, not the stores.**

⚠ **An instance of the frozen-census rule, for the record:** in a `PROBEFIELDS` run phase 1's row
read `units=4/frame` against its true 1440 (`probe.h:191`, and the non-frozen `ctl2.log`), because
the freeze snapshots the census numerator while `loopFrames` keeps climbing. Phases 2/3's 426/282
survived the same run unharmed, so **one bad row next to two good ones is the tell** — cross-check
any frozen census row against a non-frozen log before reasoning from it.

#### ⭐⭐⭐ AND THE DRIVER'S COST IS ITS MEMORY OPERANDS — 28.10 → 27.37 ms, and MY OWN "~9 ms OF SLACK" IS RETRACTED (2026-09-18)

The section above says the body is long and has no hotspot. The next question is what a long body
*costs*, and answering it with a count of **logical operations** — "the driver does ~66 things a
line, ≈1174 cycles, against a 3785 cyc/line bracket, so there is ~2× slack worth ~9 ms" — is the
error this file already records twice (§never size a prize as a residual). ⛔ **That ~9 ms figure
is retracted; it is a third instance of the same mistake and it was caught by reading the objdump
instead of the residual.** The real accounting, summing all 71 blocks of
`paint_lines_short.constprop.0`:

| | count | unit price | cycles |
|---|---:|---:|---:|
| driver instructions with a **memory operand** (17 absolute, 7 `n(sp)`, rest indexed/displacement) | 63 | ~18 | ~1134 |
| driver instructions, register-only | 133 | ~7 | ~931 |
| the two unit loops | 90 | — | (charged per cell) |
| **driver total, nominal** | **196** | **19.3** | **~2065** |

×~1.3 for DMA contention ≈ 2685, and the probe instrument (~13%) plus ISR landings close the rest
of the bracket. ⇒ **The driver costs approximately what its 196 instructions cost. There is no
slack to find — the only lever is deleting instructions, and a memory operand counts as 2.6.**

⭐⭐ **WHAT IS LEFT IS PER-RUN OVERHEAD, AND IT IS 2.7× THE WORK IT DRIVES.** Of those 196: two run
set-ups ~32, two stop tails ~30, two ENTER decodes + two `g_viewStopList` walks ~45 = **~107
instructions of per-run overhead against ~39 instructions of actual unit work** in phase 3's
5.6-cell runs. That ratio, not any single block, is this entry's remaining prize.

**Two defects read straight off the objdump, measured together at −0.733 ms** (phases 2+3
**28.104 → 27.371**; ph33 10.234 → 10.075, ph34 17.870 → 17.296; census identical on both arms at
426/32/16 and 282/50/25; `frozen` 240519894 vs 240390586, 0.05% apart). Predicted 0.89, got 0.73 —
⭐ the same ~15% over-prediction the row-price ledgers show, in a third place.

⭐⭐⭐ **BOUND A LOOP ON THE POINTER THAT OUTLIVES IT.** `VIEW_SHORT_RUN`'s unit loop advances
`srcp` and `dp` together and used to test `dp != runEnd`. So GCC elected `dp` the induction
variable — and the stop tail needs the final **`srcp`** (it consumes `srcp[0]`), which `dp`'s
choice made it **reconstruct**: a spill of the run's first `dp` to `52(sp)`, a reload, an
`lsl.l #4` of the pointer difference and an `adda`, ~70 cycles a run and twice a line, for a value
the loop had been holding in an address register all along. `dp` is **dead** at the run's end (the
boundary store addresses the screen through `view_screen_addr(cell)`, not `dp`) and `srcp` is not,
so the bound belongs on `srcp`. The tail then reads `(a5)` directly, because `srcEnd` **is** the
final `srcp`: `lsl.l #4` in the shipping driver goes **2 → 0** and its stack operands **24 → 21**.
⇒ **When two pointers step together, test the one whose final value someone still wants.** The
general form is that an induction variable is a *choice*, and GCC makes it from the loop's exit
test, not from the code after the loop.

⭐ **A BYTE DECREMENT, NOT A MASKED WORD ONE.** `line = (line - 1) & 0xFF` on an `unsigned` made
GCC materialise the constant (`moveq #0` + `not.b`) and **spill `line` to `48(sp)`** to free a
register for it — five instructions and two stack accesses a line, in a function that already
opens `movem.l d2-d7/a2-a6` (all eleven usable registers). `(unsigned char)(x - 1u)` *is*
`(x - 1) & 0xFF` for every unsigned x, the `0 → $FF` wrap included, so it is a pure spelling
change with no precondition to prove; it lands as `subq.b #1,d3 / andi.l #255,d3`. Five sites in
the sweep's drivers. ⇒ **On a register-poor machine, spell an operation in the width the hardware
has** — a mask that needs a constant register is not free, and the tell is a stack slot appearing
next to arithmetic that should need none.

⚠ **And the `mem[]` aliasing that blocks CSE of the per-line table reads is compiler conservatism,
not real** — worth stating so nobody sizes it again: every table load sits at block offset `0x50`
or `0x79` within the `$80`-spaced blocks at `$3000` (displacements 12368/12496/12624/14073/14544/
14672 = blocks 0,1,2,13,17,18), while the unit loop writes only block **heads**
(`$3000 + line + cell*$80`, line ≤ 40 ⇒ offsets `0x00..0x28`), the screen at `$6700`, and
`view_plant` writes pages `$7C`/`$7E`. All disjoint. But only **two** duplicate reads survive per
line (`MEM_view_run_right_end + line` at CHAINA and again at STOPB; `MEM_view_edge_phase + line`
at CHAINA and again at CHAINB) ⇒ ~24 cyc/line ≈ **0.25 ms**, which is the size of the prize and
not the size of the aliasing story.

⚠ **The unit loop is CLOSED, confirmed a second time from this objdump**: seven instructions per
clean cell with both pointers advancing (`move.b (a0),d1 / bne / move.b d2,(a1) / lea 128(a0),a0 /
cmpa.l a5,a0 / beq / addq.l #8,a1`). Destination cells are 8 bytes apart and sources 128, so
widening is impossible (§the sweep is 61% driver/entry). No further effort goes there.

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

#### ⭐⭐⭐ THE SWEEP IS 61% DRIVER/ENTRY, NOT UNIT WORK — and phase 1's "unexplained ~6x" is RETRACTED (2026-09-13)

The standing open item was phase 1's ~440 us/line "for work whose instruction count is nothing
like that". It is now decomposed, and **there is no 6x**: the instruction count IS like the
measurement. Two instruments, one measured and one static, agree.

**The differential.** `NOUNITS=2` (`REVS_NO_UNIT_LOOP`) keeps every per-line driver and every run
set-up and does not run the unit loop at all, so `control - NOUNITS=2` is the unit loop and
`NOUNITS=2` is the driver. Both runs `PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`,
FS-UAE warp, 30 s (668 and 820 frames).

| bracket | total | driver/entry | share | unit loop | cyc/unit | driver cyc/line |
|---|---:|---:|---:|---:|---:|---:|
| 24 — phase 1 (36 full-width lines) | 15.83 ms | **4.59 ms** | 29% | 11.23 ms | **55.2** | 905 |
| 33 — phase 2 (16 lines) | 12.41 ms | **8.29 ms** | 67% | 4.13 ms | 68.7 | 3672 |
| 34 — phase 3 (25 lines) | 24.49 ms | **19.48 ms** | 80% | 5.00 ms | 125.8 | 5526 |
| **the whole sweep** | **52.73 ms** | **32.36 ms** | **61%** | **20.36 ms** | 9.5 us/unit | — |

⭐ **Phase 3's 80% independently reproduces the VIEWP3 split's 83% chain-entry share** (§The four
view probes) from a completely different instrument. Two agreeing decompositions, so the
chain-entry cost is settled.

**The static half — the quad loop is AT THE 68000's FLOOR.** `paint_cells`' unrolled body
(`10418` in the shipping objdump) is 17 instructions for four units on the all-clean arm:

    10426 move.b d2,d0        4      1044a move.b d0,24(a2)   12
    10428 move.b d0,(a2)      8      1044e lea 32(a2),a2       8
    1042a move.b 128(a0),d1  12      10452 cmpa.l a2,a4        6
    1042e bne.w (not taken)  12      10454 beq.w (not taken)  12
    10432 move.b d0,8(a2)    12      10458 lea 512(a0),a0      8
    10436 move.b 256(a0),d1  12      1045c move.b (a0),d0      8
    1043a bne.w (not taken)  12      1045e beq.s (taken)      10
    1043e move.b d0,16(a2)   12
    10442 move.b 384(a0),d1  12      = 172 cycles / 4 units = 43 cyc/unit
    10446 bne.w (not taken)  12

12 (load) + 12 (store) + 12 (branch) + ~9 amortised book-keeping is what a byte load, a
zero test and a byte store COST on this machine, and 43 is that number. The census is 88%
clean / 8% dirty / 3% forced, and the dirty arm (`clr.b`, a 255-mask, a `lea`, the cell-byte
lookup, and the branch pair) adds ~80 cycles — **43 + 0.12 x 80 = 53 against 55.2 measured.**
⇒ **No code shape can improve this loop.** Nor can widening help: the BBC layout puts the
destination cells 8 bytes apart and the sources 128 apart, so there is nothing to coalesce into
a `move.l` in either direction.

**Consequence — the lever ordering changes.** The producer-emitted dirty-run representation
(§the unit/run interior) attacks the **20 ms** of unit work and can approach all of it, but the
**32 ms of per-line driver and chain-entry code is the larger half and a different problem.**
Phase 1's 905 cyc/line driver has identifiable GCC fat in it, read off the same objdump:

| what | cycles/line | the shape |
|---|---:|---|
| `line = (line - 1) & 0xFF` | ~50 | `subq.l #1,d3` / `move.l d3,48(sp)` / `moveq #0,d3` / `not.b d3` / `and.l 48(sp),d3` — a stack round trip to build `0xFF` |
| re-biasing `mem` | ~90 | four `adda.l/addi.l #322944` plus two `lea 4ed80 <mem>,aN` per line |
| `clr.l 68(sp)` | 28 | clearing the `forced` parameter in its stack home |
| the background byte | ~28 wasted | two `lea (0,a6,dN.l),aN` where absolute-indexed addressing would do |

⚠⚠ **But `paint_cells` is precisely the function whose 4x unroll must survive, so apply
§a fragile local optimum's counting test before any of it**: grep the objdump for each loop
invariant's absolute address and require the count to stay at 1.

⚠ **Caveats on the differential, stated because they are real.** `NOUNITS=2`'s picture is wrong
by construction, and that is visible downstream: phase 18 reads 5.88 ms against the control's
10.18 because `column_gap_walk` walks a frame buffer that no longer holds road pixels. Phase 0
is 318 fields against 315, so the window reached one more crash hold. What licenses the
subtraction anyway is that **the view sweep's own census is identical across the two runs** —
36/16/25 lines, 36/32/50 runs, 1442/426/282 units — and phase 11 (`draw_road`) is 34 ms in both.
The driver's control flow reads the stop list, `plot_ptr`, and the per-line surface index, none
of which is picture state.

#### ⭐⭐⭐ AND THE ENTRY WAS DELETED: THE SHORT PHASES' RUNS WENT **INLINE** — 35.61 → 29.03 ms, the frame −7.63 (2026-09-17)

The decomposition below named 2616 cycles per chain entry and called it "a poke-then-decode round
trip with zero semantic content". That was actionable, and this is the action: the run is now
**inline in the driver** (`VIEW_SHORT_RUN` in `revs_native.c`), with `byte`/`line`/`cell` — the
6502's A, X and Y — in **registers** for the whole line, and `srcLine`/`dstLine` hoisted once a
line so a run's set-up is one shift and one `lea`.

Both arms `SPANFILL=5 VIEWOWN=1`, `PROBEFIELDS=3000` + warp, `build=1d`, `smc=0`, and the windows
match to 0.0002% (`frozen` 240442943 vs 240443466):

| bracket | control | inline runs | delta |
|---|---:|---:|---:|
| ph33 `VIEWP2` | 11.78 ms | **10.16** | −1.62 (−13.8%) |
| ph34 `VIEWP3` | 23.83 ms | **18.86** | −4.96 (−20.8%) |
| **ph33 + ph34** | **35.61 ms** | **29.03** | **−6.58 (−18.5%)** |
| ph24 view phase 1 | 17.45 | 17.55 | +0.10 |
| ph32 `VIEWTAIL` | 6.26 | 6.39 | +0.13 |
| **Σ(1..39) − ph28** | **192.34 ms** | **184.71** | **−7.63** |

Per line: phase 3 **953 → 754 µs** (6757 → 5346 cyc), phase 2 **736 → 635 µs**.

⭐⭐ **THE CENSUS IS WHAT MAKES IT A CODE-SHAPE RESULT AND NOT A WORKLOAD SHIFT.** Phases 2 and 3
report **426 units / 32 runs / 16 lines** and **282 / 50 / 25** in *both* arms — identical to the
unit. A phase row that moves while its own census holds still is a shape win by construction, and
this is the cheapest way to rule out the trajectory (§bound the window in emulated time warns
about the other direction). Quote the census beside the row.

⭐⭐⭐ **THE MECHANISM: A STRUCT WHOSE ADDRESS ESCAPES IS MEMORY FOR THE WHOLE LOOP, AND
`ViewState` WAS THE 6502'S THREE REGISTERS.** `&v` reaches `view_plant` and `view_own_run`, so GCC
had no choice — every `v->byte` in a per-line driver was a real memory access, and each of the four
chain entries a *marshal* out and back. This is the same class as §a hot loop's state lives in
memory if anything takes its address (the −4.79 ms span rasteriser), arrived at from the other
side: there the fix was packing results into `d0`, here it is holding the state in locals and
syncing **only around the cold call**. ⭐ The enabling observation was that the sync is nearly
free to omit: `view_plant`/`view_move_stop` write only `v->byte`, and that write is **dead** in
both drivers (each overwrites it before the next read), so 21 of the 25 plants a sweep need no
sync at all.

⭐⭐ **THE DEAD-ARM PATTERN, THIRD USE, AND IT IS NOW THE DEFAULT MOVE.** A run with no planted
stop (`stopUnit >= 40`) can only end at the `$7EEE` terminator, which is `RTS` throughout phases 2
and 3 — so the hot path tests the precondition and hands the whole arm to the **existing**
`view_own_run`: the terminator test, both its traps and the multi-line continuation, in code
already written and already gated. **No `#ifdef`, no fixture narrowing, and the faithful arm is
untouched** (as with `column_gap_walk_core`'s `gap_walk_reread`). ⇒ *Don't optimise around a dead
arm; make its own precondition select a cold copy.*

⚠ **The itemised prize over-predicted by ~1/3, and that is the calibration to carry.** Hand-counting
the objdump put the deletable items at ~2150 cyc/line (two `movem` frames ≈600, two per-run
prologue recomputes ≈800, two entry decodes ≈750); the measured saving is **1411 cyc/line**. The
gap is not mystery — what survives is named: the two stop tails, the plants, `step_scanline`'s
28-instruction byte-lane dance, and the new cold-arm test — but a per-item estimate of a
*code-shape* change should be quoted as an upper bound, not a forecast.

⚠⚠ **AND A NOTE IS NOT A LOG — I nearly published a phantom regression.** My own carried-over note
said phase 1 read `units=1440 runs=36`; the new arm read `units=4 runs=0` and I spent a paragraph
theorising about the stateless predicate flipping. **The control log said `units=5 runs=0`.** The
1440 came from a different arm's log (`SPANFILL` without the takeover) that had been summarised
into a note and then trusted over the file. This is §verify the instrument's rule with the
subject changed: *when a reading surprises you, re-read the control's own log before explaining
it* — the explanation was ready and the premise was false.

**What is left in phase 3: 5346 cyc/line for 5.6 painted cells.** ~1100 of that is the unit loop
plus the two composed boundary bytes; the rest is the plants/pokes (STOPA 590 + STOPB 298),
`step_scanline` (~400), the two stop tails and the `ViewState` writes at the exits.
`docs/open-work.md` item 2 carries the remainder and its gate.


#### ⭐⭐⭐ AND THE FOURTH, CALIBRATED DECOMPOSITION NAMES THE CODE: **CHAIN ENTRY IS 84% OF PHASE 3, AT 2616 CYCLES PER RUN** (2026-09-16)

Everything above agreed that phase 3 is run-entry work and not unit work; none of it said *which
lines of C*. That needed the `VIEWP3=1` split re-run on the arm that actually ships — `SPANFILL=5
VIEWOWN=1`, `build=9d`, `PROBEFIELDS=3000` (loopFrames=252, `frozen=240387424` = 3000 × 80120) —
with its own empty bracket read first.

| bracket | raw cyc/line | net (−1 control) | share | ms/frame |
|---|---:|---:|---:|---:|
| ph31 `VIEWCTL` — the empty-bracket CONTROL | 820 | 0 | — | 2.890 raw |
| ph34 `VIEWP3` residual (line decrement, `step_scanline`, entry byte) | 1881 | 236 | 3.4% | 0.83 |
| ph35 `P3_STOPA` (`view_move_stop`, chain A) | 1423 | 590 | 8.4% | 2.08 |
| **ph36 `P3_CHAINA`** (compose ×2 + `view_own_enter` + `view_own_run` + boundary store) | 3168 | **2297** | **32.8%** | 8.09 |
| ph37 `P3_STOPB` | 1125 | 298 | 4.3% | 1.05 |
| **ph38 `P3_CHAINB`** | 4477 | **3577** | **51.1%** | 12.61 |

**The instrument is calibrated and it is honest.** Turning the split on inflated phase 3 from
24.664 to 45.444 ms (+20.78), and 7 bracket transitions/line × 820 cyc (the control) × 25 lines
= 20.2 ms accounts for all of it. The split's brackets sum to 7154 cyc/line against arm A's
unsplit 6998 — a +2.2% perturbation.

⇒ **CHAINA + CHAINB = 5874 cyc/line = 84% of phase 3, and the unit loop is 641 cyc/line = 9%.**
Chain ENTRY is 5233 cyc/line over 2 runs = **2616 cycles per run**; × 82 runs a frame (32 in
phase 2 + 50 in phase 3) = **30.2 ms/frame**, which closes independently on the 31.5 ms
`NOUNITS=2` put on the driver, and reproduces the historical "83% in the two chain-ENTRY
brackets" on a completely different base.

⭐⭐ **And 2616 cycles per run buys nothing.** What chain entry *does* is a poke-then-decode round
trip with zero semantic content:

  * `paint_lines_short` writes the run's start cell as a 6502 **operand byte** —
    `mem[MEM_view_p3_enter_a_site + 1u] = v->byte`;
  * `view_own_enter` immediately reads that byte back and decodes it through
    `g_viewUnitOf[page - VIEW_LOW_PAGE][target & 0xFF]` to recover the same cell index;
  * `view_own_run` then re-discovers the run's END by walking `g_viewStopList` via
    `view_stop_from`;
  * and `view_move_stop` / `view_plant` plant and unplant **RTS opcodes** into page-$7E slots
    (`view_plant` is 106 instructions out of line and calls `memmove`).

The run's geometry is `[first .. stop)`. Both ends are known to the caller before any of this
runs. ⇒ **this is the 6502 SMC emulation the directive names, and it is 30 ms of the frame.**

⚠ **Why the objdump could not settle it and the bracket had to.** `paint_lines_short`'s own body
is **86 instructions**; the cost is in what it inlines and re-derives, not in its size. The
inlined-callee histogram inside `view_paint_lines_core` (from `objdump -dl`, which gives
source-line attribution — plain `-d` does not, and a run of this comparison was wasted that way)
reads `paint_cells` 357, `bus_write` 244, `view_consume` 189, `paint_lines_clipped` 116,
`step_scanline` 98, `view_own_enter` 94, `paint_lines_short` 86. **A static count of 86 cannot
explain 6355 cyc/line**, which is exactly when a bracket split is the right instrument.

#### ⭐⭐ THE SECOND BITE: `bus_write` LEFT THE RENDERER — −1.17 ms, AND A ⛔ NULL THAT NAMES THE MECHANISM (2026-09-16)

The sweep's four store sites each paid a `$FC00-$FEFF` range test, hoisted to one `busSafe` local
per line/run. **The test was a constant 1 and the arm it selected was unreachable**, which the
game's own bytes settle: `view_paint_lines` ($7BE2) opens `LDA #0 / STA $70 / STA $72 / LDX #$67 /
STX $71 / INX / STX $73`, so the sweep has no pointer *input* — it builds `$6700`/`$6800` from
immediates. Every store is then `base0 + cell*8` with `base0 = plot_ptr_v`, and `step_scanline` is
the only in-sweep mutator: monotone `+1` inside a character row, `+$0138` crossing one. The line
loop is bounded by a byte ⇒ ≤ 256 steps, ≤ 32 crossings ⇒ the highest reachable address is
`$6700 + $27E0 + $100 + $140 = $8F60`, and the real geometry stops at `$74C5`. The I/O window
begins 27 KB above that.

| | arm A (control) | arm I (no test) | Δ |
|---|---:|---:|---:|
| ph24 phase 1 | 15.496 | 15.451 | −0.045 |
| ph33 phase 2 | 12.186 | 11.789 | −0.397 |
| ph34 phase 3 | 24.664 | 23.893 | −0.770 |
| **phases 2+3** | **36.850** | **35.682** | **−1.167** |
| Σ(1..39) − ph28 | 191.800 | 190.139 | −1.661 |

`PROBEFIELDS=3000`, phase 0 = 124 fields on both arms (same trajectory). The arithmetic closes:
791 stores a frame (709 units + 82 boundary) at 1.167 ms = **10.5 cycles per store**, which is a
register-resident test plus the addressing its cold arm forced. `view_paint_lines_core`
1710 → 1146 instructions, `view_own_run` 771 → 349.

⛔⛔ **AND THE OBVIOUS FIX WAS THE WRONG ONE: PUTTING THE ELSE ARM BEHIND A `noinline` ESCAPE COSTS
+0.73 ms. BULK IN A COLD ARM IS CHEAP; A CALL BOUNDARY IN A HOT LOOP IS NOT.** Three arms on
`build=1d`, `make clean` between each, noise floor ~0.05 ms:

| arm | ph24 | ph33 | ph34 | phases 2+3 | vs A |
|---|---:|---:|---:|---:|---:|
| **A** control (inline `bus_write` in both cold arms) | 15.496 | 12.186 | 24.664 | 36.850 | — |
| **E** both `noinline` + per-line `busSafe` hoist | 15.522 | 12.423 | 25.174 | 37.597 | +0.747 |
| **F** both `noinline`, test recomputed at the store | 15.466 | 12.365 | 25.213 | 37.579 | +0.729 |
| **G** unit loop back inline, boundary stores still `noinline` | 15.433 | 12.187 | 24.784 | 36.971 | **+0.122** |

⇒ the unit loop's escape was **0.61 ms** of the 0.73; the boundary stores' the remaining 0.12. The
escape *looks* right by every static measure — it deletes 598 instructions
(`view_paint_lines_core` 1710 → 1366, `view_own_run` 771 → 517) and removes `platform_hw_write`
from both call lists. The mechanism is **aliasing**: an *inline* `bus_write` lets GCC merge its
`mem[addr] = val` with the fast arm's store, so neither path contains a call and the 191/244
instructions are cold hardware path that never executes; a `noinline` callee forces GCC to assume
it writes any memory, so the loop's register-cached values spill. **The tells:**
`view_own_run`'s `n(sp)` operands went 17 → 30, and the CSE'd source-displacement reads dropped
(`128(a` 22→12, `256(a` 9→6, `384(a` 8→6) with the `movem` save unchanged at d2-d7/a2-a6.

⚠⚠ **I first blamed that regression on the hoisted `busSafe` local's live range**, citing the
register ceiling that makes `view_plant` refuse `always_inline`. **Arm F removed the live range and
measured identically to E — the hypothesis is withdrawn.** The lesson is procedural: *separate the
arms before believing a mechanism*, and note that the boundary hoist was unjustifiable on its own
arithmetic before any measurement — the unit-loop hoist it was copied from covers **2093** stores a
frame, the boundary hoist **82**, so a 2-instruction test on 82 stores was only ever ~0.02 ms.

⭐ **The transferable rule** is neither "hoist the test" nor "wrap the call": **make the store
target statically known.** GCC could not fold `busSafe` itself for one reason — the base is
laundered through `plot_ptr_v`, a *global* that fifteen unrelated engine routines use as scratch,
so constant propagation dies at the global, not at the arithmetic. **Carry a hot destination in a
local.**

#### ⭐⭐ THE FIRST BITE OUT OF THAT 32 ms OF DRIVER: THE STOP LIST WAS A CODE-SIZE TRAP — −0.84 ms/frame (2026-09-13)

Found by reading the objdump of the phase-3 driver rather than measuring it, exactly as
§the decode was code shape prescribes: 5526 cyc/line is far more than the driver's instruction
count justifies, the plotter macros are `((void)0)` in a control build and the probe RMWs are
~3% of the frame, so the excess had to be in emitted code — and the objdump showed `view_plant`
as a **348-instruction out-of-line five-argument function**, called **25 times a sweep** (the
stop moves on 9 of phase 3's 25 lines and 2 of phase 2's 16 — counted, see §the sweep's census).

⚠⚠ **That denominator was first written down as "~135 plants a frame" and it was an ASSUMPTION,
not a count** — the exact failure mode `docs/postmortem.md` names. Counting it changes what the
win means: −0.84 ms/frame over 25 plants is **238 effective cyc/plant**, which is the right order
for deleting a five-argument call plus four unrolled walks; spread over 135 it would be 44, far
too little for what was removed. ⭐ **When a win's per-call price looks implausibly cheap, the
denominator is the thing to doubt** — the arithmetic is a free check on the call count.

The 348 instructions were not `view_plant`'s own work. It maintains `g_viewStopList[41]` — which
unit stores currently hold a planted `RTS` — and **that list normally holds ONE entry and is
empty through the whole of phase 1**. `view_stop_from` already walked it sentinel-style, with a
comment saying why: spelled `i < g_viewStopN`, gcc peels the trip count and unrolls the search
eight ways. Its two siblings `view_stop_note` and `view_stop_forget` still carried the count, and
gcc had done precisely that to both, plus to both of their shift loops. Four unrolled loops over
a one-entry list made the body big enough to cross gcc's inlining threshold, so every plant paid
the call.

**The fix is the sentinel the list already carries** — every real entry is a unit index 0..39
ascending, terminated by a 40 — so the walks read `while (*p < unit) p++;`, the end becomes
POSITIONAL, and `g_viewStopN` is retired from the binary. Static half: `view_plant`
**348 → 108** instructions, `view_paint_lines_core` **877 → 757**.

`PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, warp, `phase4_prof.gdb`, 30 s, phase 0
bit-identical at **315 fields in every run** (so the same workload), reproduced twice:

| bracket | control | sentinel | delta |
|---|---:|---:|---:|
| 34 — view phase 3 | 24.49 | 23.76 | **−0.73** |
| 33 — view phase 2 | 12.41 | 12.38 | −0.03 |
| 24 — view phase 1 (**the control**) | 15.83 | 15.81 | −0.02 |
| 32 — sweep tail | 6.21 | 6.20 | −0.00 |
| sweep 24+33+34 | 52.73 | 51.95 | **−0.77** |
| FRAME (Σ1..39 − 28) | 179.33 | 178.49 | **−0.84** |

⭐ **Phase 1 is the built-in control for any plant-path change and it must not move** — it plants
nothing, and it read +0.01 / −0.02 across runs. The whole win landing in phase 3, where the
plants are, is what makes the attribution safe without a second instrument.

⭐⭐ **The rule to carry forward: a bounded loop over a short list is a code-size trap, and that
is how a call gets paid.** `docs/m68k-optimisation.md` §the third case has the general form.

⚠⚠ **And `always_inline` on `view_plant` — which CLAUDE.md's constant-parameter rule asks for,
since `page` and `opcode` are literals at every call site — measured +1.04 ms/frame** (phase 3
+1.18), and +1.01 with the list walks additionally forced out of line (phase 3 +1.44). Phase 2's
bracket gained both times and phase 3's lost more: the core grows 757 → **995** instructions and
`paint_lines_short`'s per-line loop is already at the 68000's register ceiling. **The
constant-parameter rule holds for a leaf in an inner loop, not for a caller that has run out of
registers.** Do-not-retry written at the code.

#### ⭐⭐ THE SWEEP'S CENSUS — COUNT IT ON THE HOST, NO EMULATOR RUN NEEDED (2026-09-13)

Every "cost per X" in this file needs a denominator, and the cheapest correct place to get one is
the **host build**, not a probe run. Measured with temporary counters in `PlatformHost.cpp` and
`revs_native.c`, per view sweep:

| quantity | count | where |
|---|---|---|
| `view_plant` calls | **25** | 3 initial + phase 2's pair 2×2 + phase 3's chain A 6×2 and chain B 3×2 |
| `view_enter_chain` calls | **66** | 25 short lines ×2 + 16 clipped lines ×1 |
| lines: phase 1 / phase 2 / phase 3 | **36 / 16 / 25** | `paint_cells` sees 52 = phase 1's 36 + phase 2's 16 entering through it |

⭐⭐ **And the host is a VALID PROXY for this census: those line counts came out 36/16/25, exactly
the Amiga's documented split.** Stable to two decimals over 937 sweeps (sampled at frames 300,
600 and 1200). So a call count is a `make` away and costs no emulator time — which matters
because a wrong denominator is not a small error: see the ~135-vs-25 correction above, where it
changed a win's per-call price by 5.4×. ⚠ This licenses the host for **counting**, never for
timing — the host is a different CPU and `make refloop` remains the visual ground truth.

⚠ It also corrects a second guess: the stop does **not** move "twice a line". It moves on 9 of
phase 3's 25 lines and 2 of phase 2's 16.

#### ⭐⭐⭐ THE RUN CENSUS — the must-visit set is **13% of cell stores, in 1.38 runs per line paint** (2026-09-14)

⛔⛔ **READ THE SEQUEL FIRST — §the walk walks indices, immediately below. The consumer this census
sized was built and cost +25.46 ms.** The counts here are sound and are still the reference for what
the sweep must visit; the *pricing* in this section is what the sequel corrects.

`docs/direct-bitplane-plan.md` §7j item 2 — the viewport source-event/run consumer — was the
largest un-built lever on the board and its pay-off hinged on a number nobody had measured: **how
many cells a sweep would actually have to visit** if the consumer iterated events instead of all
2148 slots. This section is that measurement. Instrument: `make SHAPE=1`'s run census
(`src/platform/shape.h` §THE RUN CENSUS, implementation in `src/platform/shape.cpp`, target script
`amiga/run_census.gdb`, host report in `PlatformHost.cpp`).

⚠⚠ **First, the structural fact that makes this a different question from the ⛔ per-line skip:
`paint_cells` stores on EVERY unit, and a clean unit's store is what ERASES last frame.** "Skip the
clean units" is therefore not a scheme at all. What makes the real thing tractable is
`view_consume`'s representation: a **non-zero** source byte means "new colour here" (return
`view_cell_bytes[source]`), a **zero** source means "same as my left" (return the carried byte). So
a painted line is a **run-length encoded colour**, and the destination store is **idempotent
wherever the cell already holds that colour** — a road edge that moves one cell changes ONE cell,
not the thirty-five to its right, which keep the same run value.

⭐ **So the consumer must visit a cell if EITHER (a) its source is non-zero** — it has to be
consumed, ZEROED and the carried byte updated — **or (b) its store would change the byte already
there.** These are genuinely different sets and neither contains the other: a cell can change with
no event of its own (the byte carried *into* it moved), and an event can change nothing (it
re-states the colour already painted). The census counts both and their union, and counts the
union's **contiguous runs**, because a run list is what the consumer would actually iterate.

| per sweep | host (1 body tick/frame) | **target (~10 ticks/frame)** |
|---|---:|---:|
| cell stores (the cost today) | 2082 over 77 line paints | **2082 over 77** |
| events — source non-zero | 10% | **11%** |
| changed — the store moved the byte | 3% | **4%** |
| ⭐ **UNION — must-visit cells** | **12%** — 3.30/line | **13%** — **3.77/line** |
| …in contiguous runs | 1.28/line, 2.57 cells/run | **1.38/line, 2.71 cells/run** |
| ⛔ producer-EXTENT scan (first..last) | 51% | **54%** |
| line paints needing NOTHING | — | **873 of 3850 (23%)** |

⭐⭐ **The ceiling, and it BRACKETS §7j's 12-18 ms estimate rather than replacing it.** 86% of unit
visits are deletable; priced on the two axes this file keeps separate:

| derivation | unit work today | 86% of it |
|---|---:|---:|
| objdump's 43 cyc/unit × 2082 stores | 12.6 ms | **~10.8 ms** |
| the `NOUNITS=2` differential's unit loop | 20.4 ms | **~17.5 ms** |

⚠ Those are the **two decompositions** §the sweep is 61% driver/entry warns about. Do not add or
subtract them; quote the range.

⚠ **Run and line control is NOT part of the prize.** The union needs **~106 runs a frame against
the 118 chain runs the sweep already pays**, so per-run and per-line work stays roughly constant
and the 32 ms driver/entry lever is untouched by this change. The prize is exactly the per-unit
work, which is 86% of the visits.

⛔ **And the cheap variant is dead, measured.** Bounding each line's scan by a producer-known
first..last extent — the shape that was worth ~324 → 113 ticks in the predecessor project
(`minScan`, one contiguous skyline band) — would still visit **54%** of the cells here, because our
must-visit cells are **few but spread**: 3.77 cells in 1.38 runs, yet their extent spans half the
line. It is a run list or nothing. This is the one of that project's four transferable findings
that does **not** transfer, and it is now a ⛔ line in `docs/open-work.md` so it is not re-derived.

⚠⚠ **The trap this measurement invites, and the control for it: a PARKED car repaints the same
picture, so every cell reads redundant and the census says "skip everything" — true of a static
scene and false of the game.** A flattering union is the *expected* artefact here, which is why
both censuses now print the engine state (`$3C` revs / `$61` engine / `$63` speed / `$40` gear)
beside the numbers and the target reading is only quoted with `$63` non-zero (it read `$22`-`$32`
in gear 2). ⚠ `amiga/view_census.gdb`'s old claim that the host *cannot* produce the moving scene
is stale — it predates `HOLD_THROTTLE` on the host build, which drives in gear 2 — and the note is
fixed. Robustness beyond that: across **2664 host intervals the union stays in 10.7-12.7%** and
does **not** grow with speed (11.8% in the `$63` = 40-50 band against 12.4% at 20-39), so the
feared ~10× displacement effect moves `changed` only 3% → 4%. The target's extra point over the
host is that displacement, and it is the whole reason a target run was spent on a pure count.

**Four sabotages, each caught in its predicted direction and each with a DIFFERENT number** (which
is itself the control for the stale-object failure mode — byte-identical mismatch counts from two
different defects is the tell):

| sabotage | prediction | read |
|---|---|---|
| never set the arm latch | events → 0, losses → all | events 0, `g_shapeRunArmLost` 616272 |
| contiguity test always true | runs collapse | 1.28 → 0.73/line, 2.57 → 4.49 cells/run |
| force `changed` = 0 | union == events | union == events exactly |
| every store joins the union | union == stores | 100% |

⚠ S1's 616272 losses against 614190 stores differ by **exactly 2082 — one sweep** — because losses
increment live while stores accumulate at the per-sweep flush. An explained discrepancy, not a
defect; a *round* difference like that is worth chasing to its explanation rather than shrugging at.

**Free by-products of the census, all of which correct standing guesses:**
- The average line paint stores **27 cells, not 40** (2082/77) — phases 2 and 3 enter mid-line.
- **23% of target line paints need nothing at all** (873 of 3850), which is the per-line skip's
  real headroom and is consistent with its ⛔ null: the lines it could skip are the cheap ones.
- Events run **~225 per sweep** while driving, so the old "~83 non-zero" figure (a parked count,
  paired with a framebuffer-byte denominator) is superseded on both halves of its ratio.

⚠ A `SHAPE=1` build is heavily instrumented (an 8320-byte compare per phase boundary), so it is
**licensed for COUNTING only, never for timing** — the target run got 50 sweeps in 141 s emulated
for that reason, and that is fine for a count.

#### ⛔⛔⛔ ...AND THE CONSUMER WAS BUILT AND IT COSTS **+25.46 ms**: THE WALK WALKS **INDICES**, THE SCAN WALKS **POINTERS** (2026-09-14)

The census above is arithmetically correct and its conclusion was wrong. `make VIEWEVT=1` builds the
consumer it sized — producers mark a per-cell event bit, the consumer walks the set bits and fills
the gaps between them — and it measured, against a same-session control from a clean build
(`PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, `phase4_prof.gdb`, warp, 30 s):

| build | view sweep | bracketed frame |
|---|---:|---:|
| control | 50.71 | 202.09 |
| stage 1, walk inlined | **+31.98** | +41.69 |
| stage 1, walk `noinline` | +40.74 | +46.17 |
| mask maintenance only, scan retained | +3.86 | +5.20 |
| + bit-scan tables (the 68000 has no `bfffo`) | +27.56 | +34.65 |
| + empty-fill guard, unrolling off | **+25.46** | **+30.71** |

⚠ It deletes 86% of the visits the census promised and costs 25 ms. **`VIEWSPLIT=1` puts 24.11 of
them inside the per-run body** (phase 30: 36.80 → 60.91), with the drivers moving +0.6/+0.5/+1.0 and
the empty control bracket flat at 12.5 — so the instrument cancels and the cost is in the work.

**Why — the block-level cycle model, and it closes to 0.9%.** Costing each basic block from the
objdump and weighting it by a host census of the walk (34928 walks, 596736 cells = 17.1/walk, 49189
events, 1.84 fill runs/walk, 3.16 `view_next_event` calls/walk × 1.65 mask bytes each):

| per chain run | control (scan) | event build (walk) |
|---|---:|---:|
| the consumer | **878 cyc** | **2145 cyc** |
| the per-run tail (chain entry + driver) | 748 | 702 |
| **total — modelled** | 1625 | **2848** |
| **total — measured** (`VIEWSPLIT` ÷ 118 runs/frame) | 1438 | **2873** |

The event build closes to **0.9%**; the control reads ~13% high because the estimator prices every
`Bcc` at a flat 10 cycles. And the walk's 2145 cycles decompose so that **only the first row is
work**:

| part of the walk | cyc/run | share |
|---|---:|---:|
| fill stores — the actual painting | 518 | **24%** |
| `view_next_event` × 3.16 (head + found-bit padding) | 474 | 22% |
| run + walk prologue | 402 | 19% |
| event consume × 1.37 | 296 | 14% |
| previous stop's consume + mask bit clear | 216 | 10% |
| fill-run setup × 1.84 | 202 | 9% |
| loop close | 37 | 2% |

⭐⭐⭐ **The mechanism, stated as a rule: on a 68000, converting an INDEX to an ADDRESS is the
expensive direction, and an event mask indexed by cell number forces that conversion at every
step.** The walk holds cell numbers because the mask is addressed by cell number, so each step pays
`lsl.l #3` + `lea (0,a4,d3.l),a2` (cell → destination), `lsl.l #7` + `add.l` + `adda.l #imm`
(cell → source), `lsr.l #3` + `lea (0,a3,d0.l),a0` + `and.l #7` + an indexed table read (cell →
mask byte and bit), and `lsl.l d7,d0` + `not.b` + `and.b` + a store to clear the bit. The scan's
equivalent of all of that is `addq.l #8,a2` and `lea 128(a0),a0` — **8 cycles, incremental, no
arithmetic.** 76% of the walk's cycles are address arithmetic that exists only because the
representation is indexed.

⭐⭐⭐ **And that gives a break-even, which is the number that should have been computed before any
code was written.** Fitting both consumers as functions of run length *N* and events in the run *E*:

```
scan = 43·N + 88·E                     (43 cyc/cell, measured from the unrolled quad body)
walk = 402 + 32·N + 476·E              (32 cyc/cell of fill; 476 per event; 402 fixed per run)
⇒ break-even   11·N = 402 + 388·E   ⇒   N = 36.5 + 35.3·E
```

**Even at zero events the walk needs a 37-cell run to pay for its own prologue. A scan line is 40
cells and the average chain run is 17.6.** The representation cannot win on this geometry at any
event density — not because the events are too many (they are few, exactly as counted) but because
the *per-event* and *per-run* constants are 15× and 9× the scan's per-cell cost. A sparse set is
only cheap to iterate if iterating it is cheap.

⚠⚠ **What the census got wrong was not the count — it was pricing a deleted visit at the cost of the
visit it replaced.** 43 cyc/unit × the deleted 86% is an upper bound on the *saving* and says nothing
about the *replacement*, and the replacement here is 15× more expensive per event. ⭐ **A
skip/sparse-iteration proposal needs TWO numbers: how many visits it deletes, and what one visit of
the new shape costs.** The second is obtainable before building — the addressing shape is visible in
any existing loop over the same data — and on this machine it is the one that decides.

⚠ **Stage 2 (producers write the run list directly, no mask scan) dies with stage 1**, and the model
says why without building it: it removes the `view_next_event` row (474) and the bit clear, and keeps
the 402-cycle per-run prologue and the 476-per-event index→pointer machinery, so break-even stays far
above 40 cells. A working version of this idea has to amortise the addressing over a whole LINE or
SWEEP rather than per run (118 runs over 77 line paints), or have the producers write byte OFFSETS
the consumer can use as pointers without arithmetic.

ℹ **Two free readings, both correcting standing notes.** The per-run *tail* — chain entry plus
driver, ~702-748 cyc × 118 runs ≈ **12.5 ms/frame in BOTH builds** — is untouched by any of this and
is the larger remaining lever (§the sweep is 61% driver/entry, `docs/open-work.md` item 2). And
`-funroll-loops` fully unrolled `view_next_event`'s 5-iteration mask-byte loop inline, then placed
the **common** exit ("this byte has an event") out of line while leaving "keep scanning" as
fall-through — the documented hot/cold inversion, one far branch out and one back on every query.
⚠ My "register-pressure spilling costs ~4 ms" attribution is **RETRACTED**: `paint_cells`'s stack
operands really do go 27 → 75, and the cycle model shows the per-run tail got *cheaper* (748 → 702).
The spill is visible and is not where the time went.

#### ⚠⚠ THE PROBE INSTRUMENT LIVES INSIDE THE VIEW SWEEP'S BRACKETS — ~5.3 ms OF THE SWEEP'S 51.95 DOES NOT SHIP (2026-09-13)

Reading the objdump of `paint_cells` for a different reason turned this up: **~14 instructions of
`PROBE_VIEW_RUN` sit inside the per-run set-up itself** — the `g_viewPhaseIdx` load, two
`add.l d1,d1`, a `lea g_viewUnits`, a `move.l #323644,52(sp)` and an 8-instruction indexed-long
RMW pair. At ~200 raw cycles × 118 runs a frame that is **~5.3 ms/frame**, i.e. essentially all of
CLAUDE.md's "~3% PROBES overhead" lands *inside* the view-sweep brackets rather than spread over
the frame.

What this does and does not invalidate:

- ✅ **Absolute ms deltas are unaffected** — the instrument is in both arms of every differential
  and cancels exactly. Every bracket table in this file stands.
- ⚠ **The sweep's SHARE is inflated.** Of bracket 24+32+33+34 ≈ 51.95 ms, ~5.3 ms is instrument,
  so the shipping sweep is ~46.6 ms and its fraction of the frame is correspondingly smaller.
- ⚠⚠ **A "run set-up" optimisation's share reads bigger than its shipping value**, because the
  instrument is itself run set-up. Price such a change against the ~46.6 ms, not the 51.95.

⭐ The general rule this is an instance of: **an instrument placed per-iteration of the thing you
are optimising distorts exactly the ratio you are trying to read.** `docs/method-lessons.md`
§calibrate with a known quantity is the other half of this.

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

### ⭐⭐⭐ THE VIEW SWEEP, FULLY SPLIT (2026-09-19) — AND THE TRANSPOSED SCAN IS 10 ms

`PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000`, warp, one session,
**frame 182 ms bracketed**.  Two arms against that control, each with every other phase flat:

| arm | phase 24 | delta | what it prices |
|---|---:|---:|---|
| control | 20.8 ms | — | scan + phase 1's driver + phase 1's painter + `own_reset` |
| `TERRAINCARVE=1` | 15.25 | **−5.55** | phase 1's PAINTER, 36 lines direct to bitplanes = 154 µs/line |
| `SCANDOUBLE=1` | 30.8 | **+10.00** | the TRANSPOSED SCAN |

⇒ the whole sweep, 43.2 ms:

| | ms | per line |
|---|---:|---|
| the transposed scan (80 lines × 40 source cells) | **10.00** | — (once a sweep) |
| phase 1's painter (36 lines, 80 plane bytes each) | 5.55 | 154 µs |
| phase 1's driver + `revs_plot_own_reset` | ~5.2 | ~145 µs |
| the low block, phase 33 (41 lines into `mem[]`) | 16.1 | 393 µs |
| the sweep tail, phase 32 | 6.3 | — |

**Three things this settles, two of them retractions.**

1. ⭐⭐⭐ **THE SCAN IS THE BIGGEST SINGLE ITEM IN THE SWEEP, and `view_scan_events`' own banner
   saying "the scan's own work is ~3 ms" IS STALE BY 3×** — it was written when the scan covered
   lines 44..79 (9 longwords a cell); `view_scan_all` covers 0..79, twenty longwords a cell over
   forty cells, plus the low block's much denser lane bodies.  ⚠ **A measurement written at the
   code ages when the code's RANGE changes, and nothing rechecks it** — the figure was quoted
   twice in this session's reasoning before the arm contradicted it.
2. ⭐⭐ **Phase 1's driver is ~145 µs/line and that is exactly what its ~40 instructions should
   cost**, so the "both drivers are 2500 cyc/line" reading — obtained by subtracting a modelled
   painter from a measured bracket — was the residual error CLAUDE.md records three times, in its
   fourth instance.  The arm cost one build and settled it.
3. `plotDeltaBase` is **not** in the loop: `amiga/dbase_probe.gdb` reads `deltaBases=1` over 331
   sweeps (and `deltaBytes=80`), so `revs_plot_own_reset` is the 208-byte claim clear plus a
   five-band rebuild and nothing else.  That hypothesis cost one run to kill.

⇒ **THE NEXT PIECE IS PRODUCER-EMITTED EVENTS** (`docs/open-work.md` entry 3's first sub-lever).
`draw_road` writes ~2082 source bytes a sweep into forty 128-byte-apart blocks and the scan then
reads all 3200 of them back to find which are non-zero.  The producer already knows: appending
`(line, cell, colour)` to `g_viewEv[]` at the store site deletes the scan's 10 ms outright and
makes `draw_road`'s scattered stores sequential.  ⚠ It needs a written RESULTS-rule reader audit —
`copy_dash_data`'s stow, `plot_view_src_line` and every expansion circuit's hook read those
blocks — and a `make determinism` re-record.

### ✅ FIXED — the low block's inner fill was UNROLLED: phase 33 **16.12 → 14.16 ms**

`#pragma GCC unroll 1` on `view_low_run`'s byte fill. One line, `mem[]` byte-identical, all four
`determinism` trajectories clean, **−1.96 ms**, frame 174 → 172.

⭐⭐ **The arm came first and it inverted my expectation.** `make LOWDOUBLE=1` runs the two
`view_low_run` calls twice per line — they are a pure function of the event list and the clip
tables, and `LOW_PUT` writes the byte already there, so nothing changes — and phase 33 went
**16.12 → 33.27 ms**. ⇒ the low block's cost **IS the painting, not the per-line driver**, which is
the opposite of phase 18 and the opposite of what the line count suggested. Guessing would have
sent me at the driver.

⭐⭐⭐ **And the defect the objdump then named: GCC peeled and unrolled that fill EIGHT WAYS —
127 instructions for a body that is `move.b` + `addq`.** The segments between events average ~12
cells over ~200 segment entries a frame, so nearly every entry pays the peel's arithmetic and never
reaches the unrolled core: 164 cycles per byte STORED where the loop body is ~52. With `unroll 1`
the fill is **24 instructions** and `view_paint_lines_core` 2026 → 1922.
⚠ **The static delta over-reads as usual** — 127 → 24 instructions paid 1.96 ms, not the ~6 the
count suggests (CLAUDE.md §Rule 1b). `unroll 1` here is load-bearing, not a hint.

⭐ What remains in phase 33's 14.16 ms is the runs' OUTER loop and the boundary composites — ~5
segment entries a line — not the fill. The fill is now ~5.2 of the 14.

### ⛔⛔⛔ PRODUCER-EMITTED SOURCE EVENTS: BUILT, PROVED CORRECT, CLOSED ON COST (+14 ms)

`make SRCEVENTS=1` has the producers append to the painters' event list as they store, and deletes
the transposed scan. Measured against HEAD (frame **172 → 186**):

| phase | control | arm | Δ |
|---|---:|---:|---:|
| 24 — the scan, deleted | 20.72 | 10.12 | **−10.59** (the arms predicted 10.00) |
| 11 `draw_road` | 34.21 | 49.49 | **+15.28** |
| 18 — `edge_run_flat`'s 136 notes | 5.93 | 7.21 | +1.29 |
| 33 — source-zeroing + list reset | 14.16 | 16.05 | +1.89 |

⭐⭐⭐ **The two producer rows price CLAUDE.md's call-boundary rule at 40× on the same five lines
of code, and that is the result worth keeping.** The identical note costs **67 cycles** at
`edge_run_flat`'s site (1.29 ms / 136 notes, inline in a loop we own) and **~2700 cycles** inside
`interp_edge_core` (15.28 ms / ~40 notes), because there it is a `jsr` in `draw_road`'s hot loops
and a call boundary is an aliasing barrier — GCC must assume it writes any memory, so the loops
spill. Inlining instead is the other horn: six copies of an ordered insert inside the frame's
biggest routine is exactly what cost +4.9 ms when a marking leaf went into `seam_write`.
**There is no third placement.** ⇒ the scan's 10 ms is the price of not touching `draw_road`.

⭐⭐ **The mechanism itself is correct, and the oracle is the honest kind.**
`SRCEVENTS=1 SRCEVENTSCHECK=1` runs the scan and the producers into separate lists and compares
them entry for entry: **0 mismatch in the steady state over 3584 sweeps, ~440 000 entries.** Two
unrelated mechanisms deriving the same list from the same stores, neither seeded from the other.
⇒ if a producer is ever rewritten so its note is inline in a loop it owns — as `edge_run_flat` now
is — the list machinery is proved and ready. Today only ~40 of the 176 stores need the risky site,
but they are the ones that carry the walk.

⚠⚠ **Three defects it cost, each a named trap:** `&EV_ARRAY[line][0]` is a 96-byte stride and GCC
emitted `__mulsi3` (muldiv-audit failed the link — walk the pointer); with the scan gone nothing
reset the lists before the first paint, so sweep 1 handed the painters BSS with no `$FF` sentinel
and the target **hung in the front end with `loopFrames=0`**; and the producer's line range must
equal the consumer's, which is a build *and* runtime question — the first oracle run reported 36
mismatching lines a sweep, all of them lines 44..79 that the host's scan never covers because
`REVS_TERRAIN_SPANS` is Amiga-only.

### ⛔ THE TRANSPOSED SCAN IS AT ITS FLOOR AT THIS GRAIN — 5.30 ms walk + 4.70 ms recording

`make SCANDOUBLE=1` (an extra pass before the real one: full walk + every lane body) prices the
scan at **10.00 ms**; `SCANDOUBLE=2` (an extra pass *after the painters*, when every source in
range is already zeroed, so not one lane body runs) prices the **walk alone at 5.30 ms**. Both are
trajectory-neutral — `consume = 0` writes no `mem[]` byte. ⇒ recording is **4.70 ms for 161
events, 207 cycles each**.

Both halves are at the floor, and the objdump closes to ~10%:
- the walk is ~47 cyc/longword against ~29 for a bare `tst.l` + branch with DMA contention, over
  ~560-800 longwords — the bare cost of testing 3200 source bytes;
- the recording is **15 instructions on the firing path** (~150 cyc, ~195 with DMA): the lane
  test, the `s_lowConsume` gate (hoisted, one reference), the `g_viewEvEnd` pointer load, the
  consume, two byte stores and the pointer bump. Nothing is redundant. ⚠ `MEM_QUAL` is **not**
  volatile in a default build, so this is genuinely optimised code, not a seam artefact.

⇒ **the scan's 10 ms is the REPRESENTATION, not the code**, and the only thing that deletes it is
producer-emitted events. ⚠⚠ **That hook is a real risk, not a formality:** a complete one needs
`plot_store_resync`, which is inlined **nine times** inside `interp_edge_core` — the same shape
CLAUDE.md records at **+4.9 ms** when a marking leaf was inlined into `seam_write`, and the damage
would land on `draw_road`, the frame's biggest row. The producer census (176 stores to 161 events,
1.09:1) says the arithmetic works; the codegen risk is what has to be decided.
⭐ One small piece of real slack found and left: the four lanes' `g_viewEvEnd` slots are 4 bytes
apart, and GCC recomputes `(line + j) * 4` per lane instead of using constant displacements off one
index — ~24 cycles a firing lane, ≈0.55 ms.

### ✅ FIXED — `fill_dash_edge_columns` flattened: phase 18 **10.12 → 5.92 ms**, frame 182 → 174

`make EDGEFLAT=1` (now the default) replaces the four-level 6502-shaped chain with one loop for
this routine's own 22 walks, keeping the generic `column_gap_walk` for `plot_view_src_line`'s
caller and for every oracle. **−4.20 ms on phase 18**, controls flat (`draw_road` +0.06%, sweep
+0.04%, low block 0%), and **`mem[]` is byte-identical** — all four `determinism` trajectories
pass, the five twins in the tree read 0 mismatch, and `viewdiff` is clean on all five circuits.
⚠ The frame reads −8 ms because phase 28 (the vblank pad, −2.93) and phase 26 (the body drain,
−0.43) *follow* a faster frame; compute is 169.4 → 164.3. Quote **−4.20**.

⭐⭐ **What made it small was reading the chain rather than the loop: three of the four values
threaded between its levels are dead on the game path**, and no single level shows that.
`entryY` (the branch offset) and `entryV` are both overwritten by `column_gap_walk` before
anything reads them — live only on its `column >= $28` early return. `entryX` is the store
pointer's zero-page *number*, and it reaches only `surface_colour_at`'s `entryX`, which no arm
lets influence the colour; the walk's exit X is then discarded, `fill_edge_column_run` returning
`column` in its place. **So only `y` actually crosses a walk boundary**, and the rest of the
per-walk marshalling existed to carry values nothing reads.

⚠⚠ **One piece of the original's shape is load-bearing and looks like a bug**, and reproducing it
is the whole difference between this working and not: `mem[EDGE_BLOCK_START]` is written once per
iteration, **above both passes**, so pass A on `column + 1` stops at `dash_block_starts[column]` —
its own block start is never consulted. Sabotaging exactly that fires at **599 of 600**. Five
sabotages, five distinct counts (599 / 447 / 7 / 600 / 600), so the arm is covered by
`fill_dash_edge_columns`' own fixture and no stale object is in play.

⭐ What remains in phase 18 is mostly the classifier: ~136 calls a frame at ~150 cycles is ~2.9 of
the 5.92 ms. That is the next thing there, and it is small — the routine has gone from the frame's
most expensive per-item cost to a 3.4% row.

### ⭐⭐⭐ THE PRODUCERS MAPPED (2026-09-20) — 61 ms for 27 edge points, and they are NEAR THEIR FLOOR

`docs/open-work.md` entries 2+3, the last untouched mass. Both existing splits run, plus an
objdump audit and a real-BBC reader audit of the intermediates. The conclusion is not the one the
queue's sizings imply, so read it before building anything here.

#### The two splits, and only ONE of them is quotable in ms

| | control | split build | instrument cost | verdict |
|---|---:|---:|---:|---|
| `build_track_geometry` (GEOSPLIT) | 26.6 | 28 | **+1.4** | ✅ trustworthy |
| `draw_road` (ROADSPLIT) | 34.3 | **66** | **+31.7** | ⛔ ms unusable — read the COUNTS |

⚠ ROADSPLIT's own empty bracket (49) is **10.4% of its 66 ms**, and it transitions 45 000+ times a
frame. CLAUDE.md §an instrument whose cost exceeds what it measures: its *shares* are indicative,
its milliseconds are not, and its **counts** are the finding.

#### ⭐⭐⭐ THE COUNTS — the whole reason this section exists

| pass | work per frame | measured | per item |
|---|---|---:|---:|
| `build_track_geometry` | **27 edge points** (13 + 13), 89 transforms (30 bearing + 29 project + 30 hypot), 0 subdivisions | 26.6 ms | **~2 120 cyc/transform** |
| `draw_road` | **42 spans**, 47 DDA scan lines, **231 DDA steps**, **58 plotted columns**, 115 fill lines | 34.3 ms | **~4 200 cyc/column** |

`draw_surface_spans` is **91.1%** of `draw_road`; `road_edge_start` is 5.3% of the geometry and the
two edge walks are 92.4% of it. **61 ms turns 27 edge points into 58 plotted columns.**

#### ⛔ AND THERE IS NO BAD KERNEL LEFT TO FIND — the 2x is instruction COUNT and FETCH

- `div16by8`'s eight-step restoring divide is **already off the game path** (GEOSPLIT counts 0):
  `bearing_arm` and `project_point_core` each do their own `revs_divu16`, i.e. a real `DIVU.W`.
  The remaining loop serves only the two `__t6502` oracle bodies.
- `bearing_to_section_core` / `project_point_core` are already native wide-value C with the
  16-bit values relocated out of `mem[]`, at 108 and 149 instructions.
- **Every hot body has ZERO frame operands** (`n(a5)`/`n(sp)`): `interp_edge_core` 2244
  instructions, `sw_plot_1/2` 427/407, `fill_line_attr_core` 813, `draw_surface_spans_core` 169 —
  the "a hot loop's state lives in memory" class that paid −4.79 and −6.58 ms is exhausted here.

What remains is **absolute `mem[]` operands at 10-21% of instructions** (`interp_edge_core` 225 of
2244; `draw_surface_spans_core` 36 of 169; `project_point_core` 23 of 149). On a 68000 each is a
6-byte instruction at ~18 cycles against 4-8 for a register op — and the *fetch* is half of that,
which no data-layout change can avoid. A plausible floor for the work these passes actually do
(≈22 000 instructions a frame) is ~28 ms against the measured 61.

#### ⛔⛔ THE VALUE PIPELINE IS BLOCKED BY ZERO-PAGE TENANCY, NOT BY AN EXTERNAL READER

`make rangeaudit RANGE=0080-0088` (the new general form of §12b's instrument — read/write PC
attribution over an arbitrary range on the authentic engine) on the camera-relative delta vector:
**3062 reads and 1825 writes a frame from 23 distinct readers**, including `rotate_state_pair`,
`scale_shape_vectors`, `plot_line_octant`, `surface_colour_at` and `fill_line_attr` — routines
from unrelated passes.

⚠⚠ **That is the documented trap, not a finding about geometry: `$0084`/`$0085` carry a SECOND
TENANCY as `shared_temp_84`/`shared_temp_85`**, so the reader count counts TENANTS
(CLAUDE.md §rank a candidate pair by ops-per-marshal). The audit's per-offset column is what
separates them — offsets 0, 1, 6, 7 have 1-3 readers each and are candidates; offsets 2, 3, 4, 5
have 8-11 and are not. ⇒ **a value pipeline here is a per-CELL audit with a mostly negative
answer, not a pass-level rewrite.**

#### ⇒ WHAT THIS MEANS FOR THE TARGET, stated plainly

The producers are ~2x a plausible instruction-count floor, and the gap is absolute-`mem[]`
operands whose cells are shared scratch. **So the 61 ms cannot be halved by making it cheaper per
unit; it has to become less work** — fewer edge points than 27, fewer spans than 42. That is an
ALGORITHMIC and VISUAL-FIDELITY decision (`make viewdiff` fails by construction), which is the
user's to take, and it is what `docs/open-work.md` entry 3's title has said all along: **fewer
POINTS / SPANS / SOURCE VISITS.**

### ⚠⚠⚠ THE OBJECT PLOTTER — the road sign is 5.15 ms, and every baseline UNDERSTATES A REAL RACE BY ~5 ms

Profiling the never-examined small rows reached phases 14 + 15, the road sign: `build_road_sign`
1.918 ms and `draw_track_object(slot $17)` 3.231 ms. Two results, and the second is worth more than
the first.

#### The sign itself is at its local optimum — ~1.3 ms available for a large rewrite

Host census (295 phase-15 calls, counters only, no emulator run): `plot_object_core` **0.89** calls
per phase-15 call (11% of frames the slot is empty or off-view), its shape loop 1.37,
`scale_shape_vectors` **6.37 vertices**, `plot_shape_edges` **3.63 edges**, `plot_view_src_line`
3.69, `column_gap_walk` 1.60.

⭐ **`scale_shape_vectors` priced EXACTLY at 0.686 ms by a new trajectory-neutral doubling arm**
(`make SIGNDOUBLE=1`, phase 15 3.231 → 3.917): it is **idempotent** — it derives
`shape_scale_tbl[2..7]` and the sixteen `shape_vertex` entries from `proj_width` and the shape's
vector list, re-reads `OBJ_VECTOR_CURSOR` fresh every call and never advances it — so a second run
writes the same bytes over themselves. That is **765 cycles a vertex**, and the remaining 2.545 ms
is 3.63 edges at **~4 970 cycles an edge**.

⛔ **And there is no pathological shape to find.** The chain is `plot_view_src_line_core` (687
instructions) → `column_gap_walk_core` (1176) → `fill_edge_column_run_core` (540), and all five
bodies in the subtree have **zero** frame operands (`n(a5)`/`n(sp)`), **no** `pea` in a hot body and
**no** absolute `mem+` reads inside a loop. They are the dash-edge campaign's own subjects and
already sit at the optimum it left them at (`column_gap_walk_core`'s 1176 instructions is that
campaign's own figure, and its invariants live in registers precisely because the classifier sits
on cold landing pads). The cost is genuine 6502-shaped work thinly spread over a four-level chain
whose per-walk setup phase 18 already measured at ~2000 cycles. ⇒ **~1.3 ms best case for a large
rewrite, 0.9% of the frame. Not worth it for the sign alone.**

#### ⚠⚠⚠ …BUT THE SIGN IS NOT THE ONLY OBJECT, AND THE BASELINE IS A PRACTICE SESSION

`move_and_draw_cars` (phase 17) reads **0.21 ms** in every measurement this project has ever taken
— 1 500 cycles for ~22 slots, i.e. ~68 cycles each, which is exactly the cost of the empty-slot
test and nothing else. **Every car slot is empty, because `STRAIGHT_TO_RACE` is a PRACTICE session
and the player is alone on the track.**

Host census of `determinism-race` — and it needs a gate, which is the transferable half:

| | slots tested | empty | off-view | **DRAWN** |
|---|---:|---:|---:|---:|
| ungated over 12 573 frames | 23.00 | 21.01 | 0.65 | **1.34** |
| **gated to the race proper** (frame ≥ 12000, 573 frames) | 23.04 | 20.62 | 0.10 | **2.32** |

⚠ **The ungated figure is almost entirely QUALIFYING** — `determinism-race` spends ~12 000 of its
12 600 frames there, alone on track with every slot empty — so it under-reports the race by 42%.
**Gate a census on the session it is about.**

At the target-measured **3.63 ms per drawn object** (3.231 ÷ 0.89):

- practice, 0.89 drawn a frame → **3.23 ms** ← what every baseline contains
- a real race, 2.32 drawn a frame → **8.42 ms**
- ⇒ phase 17 goes 0.21 → **~5.2 ms**, and **the standing 172 ms baseline understates a real race
  by ~5.2 ms.**

⚠ Stated assumption: this multiplies a target-measured per-object cost for the SIGN shape by a
host-measured object COUNT from a race. A car shape may carry a different edge count, so it is a
sized estimate, not a measurement — the measurement needs a `PROBES=1 RACEPROPER=1` target run that
reaches the grid. **But it re-ranks the object plotter from "0.9% available" to ~9 ms of a real
race's frame, which is the same order as `fill_dash_edge_columns` was before it was flattened.**

⭐⭐ **THE GENERAL LESSON, and it applies to every number in `docs/perf-method.md`: THE BASELINE
TRAJECTORY DECIDES WHICH CODE EXISTS, not just how hot it is.** CLAUDE.md already records the
weaker form ("size a road-pass routine while DRIVING, not parked — it is 8x"). This is the stronger
one: a whole subsystem can read as 0.1% of the frame because the chosen session never populates its
inputs, and no amount of care with the instrument can see it.

### ⛔⛔⛔ PRODUCER-EMITTED SOURCE EVENTS ARE CLOSED FOR GOOD — the ceiling is −2.16 ms, and my own −9 was a fifth residual error

`9d503ed` closed the built version on cost (+14 ms). The follow-up idea was "one note per RUN
rather than per byte", on the reasoning that the ~2700-cycle note was a per-CALL cost that fewer
notes would divide. **The idea is dead, and what kills it is the scan's own split, not the note.**

⭐⭐⭐ **THE SCAN'S 10.00 ms IS 5.30 WALK + 4.70 RECORD, AND ONLY THE WALK IS DELETABLE.** The
record half is 161 events × 207 cycles of *appending to the event list* — work that has to happen
wherever the events come from, and the producer route's `view_ev_note` does an **ordered insert**,
which is strictly MORE than the scan's in-order append (the scan walks cells in ascending order, so
it appends; a producer stores in DDA order, so it must search and shift). So:

| | ms |
|---|---:|
| scan WALK — the blind 2188-byte sweep, deletable | **−5.30** |
| the producers' own ~176 `mem[]` source stores a sweep | **−0.75** |
| scan RECORD — **not deletable**, and the replacement is dearer | 0 |
| the call barrier in `interp_edge_core`'s loops, **measured** | **+3.89** |
| **best case, with the insert reduced to zero** | **−2.16** |

…for a new representation that must preserve the producers' read-modify-write composition
(`docs/span-render-plan.md` §12b class B), a scoped `set_ignore`, a `determinism` re-record and
per-circuit `viewdiff` gating. **Not worth building. Do not re-open without a new number here.**

⭐⭐⭐ **AND THE ERROR IS THE FAMILY CLAUDE.md ALREADY RECORDS, IN ITS FIFTH INSTANCE: I PRICED THE
PRIZE AGAINST THE WHOLE MEASURED BRACKET WHEN ONLY PART OF IT WAS DELETABLE.** My reported figure
was "−10 (scan) − 0.75 (stores) + 1.66 (notes) = −9.1". The missing number is the one §10n named:
**what survives.** Same shape as the span hook-in that deleted 58% of phase 1's units and netted
zero, and as §11d's two-block delta quoted as one block's.

#### ⛔ Two sub-ideas killed with it, both cheap to check and worth recording

1. **Bounding the scan by `dash_block_starts` — worth exactly ZERO, measured.** The audit
   (§12b) establishes that offsets below `dash_block_starts[col]` can never hold a source byte:
   1012 of the 3200 block bytes, 32%. The scan already skips them.
   ⭐⭐ **`s_lowConsume[cell] == dash_block_starts[cell] + 1` for all forty cells** (one gdb run),
   and the `+1` is exactly the sentinel `copy_dash_data` never copies. That is a strong
   cross-validation of both tables: `s_lowConsume` is derived at RUNTIME by `view_low_build` from
   the run tables, `dash_block_starts` is STATIC data in the binary, and two independent encodings
   of the dashboard silhouette agree on all forty cells. **The scan is at its floor in both
   senses — per-longword cost and range walked.**
2. ⚠⚠ **The `SRCEVNULL` split is CONFOUNDED BY IPA and its 74%/26% must not be quoted.** The arm
   keeps the note call and empties its body, reading `phase 11` 34.255 / 38.143 / 49.396 for
   control / empty / full. But GCC sees the callee in the same TU: with the body reduced to one
   global increment it knows only that global changes, so the barrier it imposes is **weaker** than
   the full callee's (which touches `EV_ARRAY`, `EV_END`, `s_lowConsume` and `mem[]`). The arms
   differ in the barrier *and* the work. ⇒ **+3.89 ms is a LOWER BOUND on the call-shape cost,
   nothing more**, and there is no arrangement that isolates the two (putting the callee in its own
   TU measures a *stronger* barrier than the real arm has).

#### ⚠⚠⚠ AND A GREP THAT READ ZERO WHILE THE CALL WAS RIGHT THERE — `.constprop.0`

Building the null arm I ran `grep -c "jsr.*<view_ev_note_addr>"`, got **0**, and concluded GCC had
deleted the calls — then added a counter to "force" them back. It had deleted nothing: GCC had
emitted a **constprop clone**, so every call reads `jsr <view_ev_note_addr.constprop.0>` and the
grep for `<name>` matched none of them. Four calls sat in `interp_edge_core` in every arm.

⇒ ⭐⭐⭐ **AN OBJDUMP CHECK FOR A CALL MUST MATCH THE CLONE SUFFIXES — `.constprop.N`, `.isra.N`,
`.part.N` — SO GREP THE PREFIX `<name`, NEVER `<name>`.** This inverts a check CLAUDE.md tells you
to run: "count `jsr <hot-leaf>` in the objdump and require 0" reads 0 just as happily when the leaf
is being called under a clone name, which is the failure it exists to catch.

### ⭐⭐⭐ THE TOOLCHAIN'S `memset`/`memcpy`/`memmove` ARE BYTE LOOPS — every block op in the port

Found by reading the objdump while sizing the twenty never-profiled small phase rows, and it is a
whole CLASS rather than a site. The `-nostdlib` support library (`$(SUPPORT)/gcc8_c_support.c`,
shared with the other Amiga projects and therefore outside this repo) implements all three as
byte-at-a-time loops:

| op | inner loop | cycles per BYTE |
|---|---|---:|
| `memset` | `move.b d0,(a0)+` / `cmpa.l d1,a0` / `bne.s` | 8 + 6 + 10 = **24** |
| `memcpy` | `move.b (a0)+,(a1)+` / `cmp.l a0,d1` / `bne.s` | 12 + 6 + 10 = **28** |
| `memmove` (descending) | four instructions a byte | **~40** |

`src/platform/amiga/fastmem.c` replaces them with longword loops unrolled eight ways (~4.8
cyc/byte as GCC emits it) and `-Wl,--wrap=` redirects every reference. `make FASTMEM=0` is the
control arm and the build reports which one it is in `build=` (bit 11).

**MEASURED, arm against arm at `PROBEFIELDS=3000`: −0.52 ms in phase 10 and −0.53 ms in phase 24.**
Those are the only two phase rows in the frame that contain a block operation and they are the two
that moved; every other row drifted +0.5 the other way on a trajectory that was not bit-identical
(phase 0 read 130 fields against 120), so **the attributable figure is the −1.05 ms on those two
rows, not the −0.42 the compute total showed**. Quote it that way.

⭐⭐ **AND IT IS SMALLER THAN THE BYTE COUNT SUGGESTS, WHICH IS THE TRANSFERABLE HALF: SIZE A FILL
FROM ITS RUNTIME BOUND, NOT ITS DECLARED ONE.** `clear_surface_buffers_core` reads as a 396-byte
fill from the source (four buffers up to `horizon_extent` plus all 80 surface lines, and the code's
own comment reasons about the `$50` ceiling) — but `horizon_extent` is ~28 in a real race, so it
fills ~196, and the whole phase row was only 1.86 ms to begin with. The cycle model closes on the
measured delta at 196 bytes and is 2x out at 396.

⚠⚠ **THE WRAP HAS A TRAP THAT NO TEST WOULD HAVE REACHED, and the objdump is what caught it.** GCC
recognises `fastmem.c`'s OWN small byte loops as the memset idiom and emits a call to `memset` —
which `--wrap` then redirects back into `__wrap_memset` with identical arguments, so any fill of
1..7 bytes recurses until the stack dies. `-fno-tree-loop-distribute-patterns` on that one object
is load-bearing (the toolchain's own file carries the same guard as a per-function attribute). The
tell was `jsr <__wrap_memset>` *inside* `__wrap_memset`; the shipped call sites never pass a length
under 8, so it would have sat there until one did.

⚠ And `-funroll-loops` (which `$(NATIVE_OPT)` carries) **peels an already-unrolled loop four times
more**, adding ~20 instructions of trip-count-modulo dispatch ahead of the first store — pure loss
on the 79..208-byte fills this port actually does. That object builds at plain `-O2`.

⭐⭐⭐ **THE GATE IS THE INTERESTING PART, BECAUSE A TARGET PIXEL DIFF CANNOT SETTLE A CHANGE LIKE
THIS AND I TRIED IT FIRST.** A render-speed change moves the simulation's trajectory, so the fast
and slow arms are never on the same scene: two `screen_dump.gdb` runs broke four fields apart (vbi
600 vs 604, 63 vs 65 painted) and their bitplanes differ for that reason alone. That is not a
weaker gate, it is a **confounded** one, and its output must not be quoted in either direction.
What settles it is two instruments neither of which can be confounded by timing:

1. `make fastmem` — the host differential, all three against libc over every length 0..300 at
   every source/destination alignment pair, 30 100 cases. **Seven sabotages, all FAIL.**
   ⚠⚠ Its first version had a **vacuous** parity check: it computed the expected alignment from its
   own copy of the rule rather than observing the code, so the sabotage that DELETES the parity test
   passed 0-of-30 100 — the defect is invisible on x86, which permits unaligned access, and is an
   address-error crash on a 68000. Fixed by making the code name every pointer it widens (`FM_WIDE`,
   compiled out on the target) so the test asserts the parity from the shipped control flow.
   24 612 wide accesses observed, 0 odd. **Second instance this month of a reference derived from
   the thing under test.**
2. `make FASTMEMCHECK=1` + `amiga/fastmem_probe.gdb` — the TARGET-side postcondition check on the
   real data: every wrapped call verifies that memset left `len` copies of `val`, and that
   memcpy/memmove left the source as it was at entry. That is what the C library *promises*, not
   what this file does, so it cannot be satisfied by a wrong answer both sides compute the same way.
   **2555 checks, 0 mismatch, 0 skipped**; sabotaged (write 7 longwords of 8) it reports 538 of 2559.
   ⚠ It is a correctness arm only — it byte-loops over every byte moved, so its own phase rows are
   void. And its `g_fmSkipped` hole was closed rather than documented: a call too long for the
   1024-byte snapshot is still checked exactly when the regions are disjoint, which covers the one
   >1 KB call in the port (`PlatformAmiga::loadImage`'s 64 KB image load — a `for` loop GCC turns
   into a `memcpy`).

### ⭐⭐⭐ PHASE 18 IS PER-WALK, NOT PER-CELL — and "490 cycles a cell" was the wrong denominator

This is the correction that matters, because three separate optimisations have now been priced
against the per-cell figure and all three failed.

`make EDGEDOUBLE=1` adds eleven extra pass-A walks that skip every (already filled) cell and store
nothing — no `mem[]` byte, pixel or sim step changes — and costs **+5.42 ms**, i.e. **~3490 cycles
a walk**. `make EDGECOUNT=1` counts the population: **22 walks and ~136 cells a frame, ~6 cells a
walk**, and **0 of 11 000 pass-B walks are empty**. Even at a generous 200 cyc/cell that leaves
**~2000 cycles of SETUP per walk — ~6 of the 10.1 ms bracket**, spent four levels deep
(`edge_column_pass` → `fill_edge_column_run` → `fill_column_gaps` → `column_gap_walk` →
`gap_walk_body`): two `movem` frames, `plot_ptr`/`plot_ptr2` byte-lane marshals, three
`walk_stores_are_private` tests, a `zp_pointer` reassembly, an `adc_overflow`, three patch-byte
stores, four `mem[EDGE_*]` stores and two 7-field `SlotExit` returns, per walk.

⇒ **The lever is FLATTENING THE CHAIN.** The three per-cell attempts and their measured prices:
packing the classifier's struct return **+2.3 ms**; hoisting the per-cell re-reads into a
precondition-selected sibling (`EDGEFILL=1`, byte-exact, all four determinism trajectories clean)
**0.0**; skipping "empty" pass-B walks **+1.0 ms, and it never fired at all**.

⛔⛔⛔ **AND THE PREMISE THAT LAST ONE RESTED ON WAS FALSE, WITH A PARTLY VACUOUS ORACLE BEHIND
IT — the sharpest instrument failure of this campaign, because its sabotages still fired.**
I claimed pass B does real work only in the first iteration of each run, reasoning that pass A's
exit lands exactly where the next pass B is told to stop. `mem[EDGE_BLOCK_START]` is written **once
per iteration, above both passes**, so pass A(column+1) stops at `bs[column]` too; the next pass B
walks `bs[column] → bs[column+1]` and is not empty. `EDGECOUNT` says 0% empty.
⇒ `view_edge_start_only` computes only the first walk of each run and is **incomplete**, missing 9
of 11. Its oracle read 0 of 327 424 bytes **because I seeded the scratch with the real routine's
output** "so only the bytes this computes differ" — which compared every byte outside the computed
range against a copy of the answer. All four sabotages perturbed the range that *is* computed, so
they fired and proved nothing about the rest.
⭐⭐⭐ **Never seed a differential's reference from the thing under test.** And a partly vacuous
oracle whose sabotages fire is the hardest shape of this failure to catch — the sabotage test
checks the region you wrote, not the region you forgot.

⚠⚠ **Consequence for the entry below: `EDGESTART=1`'s −13 ms price tag stands (it is a bracket
measurement), but its `viewdiff` failure is CONFOUNDED** between dropping pass A and running an
incomplete pass B, and therefore does **not** establish that the gap fill is load-bearing. Re-run
that arm with a complete pass B before quoting it.

### ⚠ `fill_dash_edge_columns`' GAP FILL — −13 ms to drop, but the picture result is CONFOUNDED

`make EDGESTART=1` keeps the routine's boundary tables and drops its 136-cell source gap fill, on
the argument that `view_consume`'s RLE carries the surface colour across a zero source by itself.
**Measured −13.0 ms** (phase 18 10.12 → 0.68, phase 24 −1.75, phase 33 −2.83, frame 182 → 169,
`draw_road` control −0.12%) — and **`make viewdiff` fails on all five circuits, 353-403 bytes of
the gated road view.**

⭐⭐⭐ **The diff names the error exactly, and that is why `viewdiff` is the only gate that could
have caught it:** the differing cells are **27..34 (carrying to 39) on the right and 4..6 on the
left** — byte for byte the columns pass A writes (`$1B..$22`, `$04..$06`). The carry into them does
not come from the run's entry composite; it comes from whatever `draw_road` last wrote to their
LEFT, which is a *road* colour, and those cells sit beyond the last road edge where the right
answer is the off-road surface. **The routine's own header states this in one sentence** ("there is
no cell to the left, so the gap has to be filled with the colour of whatever surface the road
actually has at that point") and I designed around it without refuting it.
⇒ **When a routine's header states its own reason for existing, refute THAT sentence first.**

⭐⭐ **And the oracle lesson is sharper than the perf one: a routine with TWO outputs needs TWO
gates.** `EDGESTARTCHECK=1` compares the boundary tables against the real routine's own output and
reads **0 mismatch of 327 424 bytes**, sabotaged four ways with four distinct counts (61 / 2 / 279 /
510). It is a genuinely valid in-process differential — it compares a *computation* against an
*output*, not two consumers of one input — and it proves nothing whatever about pass A. A green
oracle on one output stood in, for an hour, for the routine.

⇒ **THE SALVAGE, AND IT IS THE BETTER TRADE:** pass A stays and gets cheap. 136 cells at ~490
cycles each is 9.4 of the 10.4 ms, spent in a four-deep 6502-shaped call chain (two `movem`
frames, `plot_ptr` marshals, three `walk_stores_are_private` tests, a 7-field `SlotExit` per
level); a direct native fill is ~100 cyc/cell ⇒ **−7.5 ms with `mem[]` byte-identical**, so
`determinism` gates it for free. The −1.75/−2.83 ms EDGESTART also took from phases 24 and 33 is
**not** available — it came from deleting 136 real events.

⭐ The exact-and-kept half: `view_edge_start_only` computes both boundary tables in two short
loops, because **pass B does real work only in the FIRST iteration of each run** — pass A's exit
line is `dash_block_starts[column + 1]`, which is precisely where the next iteration's pass B is
told to stop, so every later pass B walks zero cells. Twenty lines a side, not 136.

### ⛔ `surface_colour_at_core`'s STRUCT RETURN IS FREE — packing it cost +2.3 ms

Phase 18 is 490 cycles to fill ONE source byte, and the classifier's objdump is overwhelming:
`SlotExit` is seven fields, it is built at all eight exits, and the two hot callers read two of
them.  Splitting it into a value core returning `colour | class<<8 | carry<<16` plus a flag shim
took `column_gap_walk_core` **1176 → 620** instructions and `fill_edge_column_run_core`
**540 → 61**, with `jsr <surface_colour_value>` still 0 and all five twins in the tree byte-exact.
**Phase 18 went 10.4 → 12.8 ms** (controls flat: phase 11 +0.03%, 24 +0.4%, 33 −0.5%), frame
182 → 186.
⭐⭐⭐ **The mechanism is why the instruction count lied: `always_inline` + SRA means the struct
never exists** — the seven fields are registers and the five nobody reads are dead-code-eliminated,
so the return costs nothing, and those 1176 instructions are the six arms' COLD code with one arm
running per cell.  The pack is real HOT-path work: the 68000 has no byte-insert, so `class << 8` is
an `lsl.l #8` (24 cycles) plus an `or.l`, and the unpack an `lsr.l #8`.
⇒ **AN INSTRUCTION COUNT CANNOT SEE DEAD-CODE ELIMINATION INSIDE AN INLINED CALLEE**, so an
`always_inline` struct return is already free and its apparent size is cold arms.  CLAUDE.md
§packing is not free, second instance, and the discriminator is the same: one pack per CALL loses,
one pack serving a long loop wins.

### ⭐ `fill_dash_edge_columns` (phase 18, 17 ms) decomposed — 151 cells, ALL on one arm

Phase 18 is the fifth-biggest row in the frame for a driver whose whole job is **twelve columns**,
and "it is per-item setup like the rest" was an assumption until it was counted. `src/platform/shape.h`
now carries dash-edge counters (`make SHAPE=1`, `REVS_SHAPE_WATCH=N` on the host build; the arms are
hooked in `column_gap_walk_core`, and in a non-SHAPE build the cell counter is `#ifdef`-guarded so
the shipping build provably pays nothing rather than relying on GCC to delete it).

Constant across every watch interval, host, `REVS_FIXED_RNG=1`:
**25 walks/call, 151.00 cells/call, skip=0, table=0, colour=151, fallback=132 (87%)**; cells-per-walk
histogram (buckets of 8) = 20 walks of 0-7, 3 of 8-15, 2 of 16-23.

⚠⚠ **RE-MEASURED WHILE DRIVING (2026-09-13) AND TWO OF THOSE READINGS DO NOT HOLD.** Same host
instrument, `REVS_FIXED_RNG=1 SHAPE=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, ~780 000 calls (two
consecutive watch intervals agreeing to the second decimal): **23 walks/call, 136.42 cells/call,
skip=1, table=0, colour=135, fallback=117**; histogram 83% of walks at 0-7 cells, the rest at
8-23, none longer. So:
- ⚠ **The `$09` skip arm is NOT dead** — it fires on ~1 cell per call. The "all cells take the
  empty-cell arm" reading below was a property of the run that produced it, and **finding 1's
  "they are specialisable" no longer has a census behind it**; a specialisation that deletes the
  non-zero-source arm would be wrong on this trajectory.
- ⚠ **The walk is NOT trajectory-independent.** The columns are fixed geometry but their LENGTH
  is not: each walk runs from `span_line_cursor` down to `dash_block_starts[column]`, and the
  cursor moves with the scene (151 → 136 cells, 25 → 23 walks). "Constant across every watch
  interval" was constant *within* one trajectory, which is a weaker statement than it reads as.
⇒ the per-cell cost is now ~10.4 ms / 136 cells ≈ **76 µs, ~490 raw cycles**, and the arm split
says the classifier still earns its answer on 18 of 136.

⭐ **The instrument's self-check is the SUM IDENTITY**: `skip + table + colour == cells`
(`0 + 0 + 151 = 151`), which proves no cell took an uncounted path — the thing that would otherwise
make a zero arm indistinguishable from a misplaced hook. And the CONSTANCY is explained rather than
suspicious: the dash edge is fixed geometry (columns `$03..$06` and `$1A..$22`, block starts from a
static table) — ⚠ but the walk LENGTHS do vary; see the correction immediately below.

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

#### ⭐⭐ FIXED (2026-09-13, `9dfdf4e`): 17.10 → 10.40 ms/frame, −39%

Differential: two `make clean` builds, `PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`,
`EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 30`, **same session**, control
built from a stash of the change. Controls: `draw_road` −0.5%, decode −0.0%, `build_track_geometry`
+0.3%, view phase 3 −0.5%, view phase 1 −1.0% — so the workloads match and the row is readable.

The per-cell machinery, traced from the objdump of the empty-cell arm (~650 cycles at HEAD):

| paid per cell | cycles | fate |
|---|---|---|
| `zp_pointer` reassembly of `plot_ptr` from two byte lanes (`addq`, `andi.l`, two indexed byte loads, `lsl.l #8`, `or.b`) | ~76 | **hoisted** |
| `pointer_is_ram` ×2 (`addi.l #256`, `cmpi.l #64512`, `bls`) | ~84 | **compile-time 1** |
| the colour fallback loaded then immediately SPILLED (`move.b mem+$1DDC,46(sp)`) to survive the classifier | ~32 | **moved into the arm that reads it** |
| `plot_store_resync`'s `andi.l #65280` + `bne` | ~26 | **gone with the hoist** |
| the `$1DD4` branch operand, read for an arm the game never takes | ~16 | **gone, decided once per walk** |
| the loop test's carry (`cmp`/`scc`/`neg.b`), live only at the trap exit | ~16 | **re-derived at that exit** |

⭐⭐ **ONE TEST PER WALK BOUGHT ALL OF IT, and the reason nothing was hoisted before is real rather
than cautious**: the walk's own stores can land on the cells that drive it — a boundary-table
pointer of `$005D` (a real randomised fixture case) makes the run cover `$0082` and `$0085`, the
loop's end line and the column it is filling, and the three patch bytes at `$1DD5`/`$1DDC`/`$1DDE`
are reachable the same way. So `walk_stores_are_private(base)` asks it once per walk for the three
bases, and a `no` hands the whole walk to a cold `noinline` copy that re-reads everything exactly as
before. `g_gapWalkSlow` counts those and reads **0** on the target.

⭐ **The predicate paid off twice.** Because it proves `base >= 0x100`, GCC's value-range
propagation derived that the store cannot alias `mem[$82]` and **hoisted the end-line load out of
the loop by itself**, and dropped the `& 0xFFFF` masks. Verified in the fast loop's SCC: `lsl.l #8`
0, `#64512` 0, `mem+0x82` 0, `scc` 0, `mem+0x1dd5` 0 (HEAD: 1, 4, 1, 1, 1).

#### ⚠⚠ AND THE WALL CLOCK APPEARED TO MOVE LESS THAN THE WORK — 221.05 → 218.77, −2.29 ms for −5.01 ms of compute. THE GAP IS ONE EXTRA CRASH HOLD, and the frame really did move the full −5.01

**⚠ The first explanation I wrote here — that the engine's own frame wait at `$1760` "absorbs" a
compute win and hands it back only in lumps — is RETRACTED. It was a plausible cycle-accounting
story with nothing behind it.** What the same two runs actually say, once phase 0 is subtracted:

| | HEAD | after | Δ |
|---|---|---|---|
| `elapsed / loopFrames` (the raw wall frame) | 221.05 | 218.77 | −2.29 |
| **`(elapsed − phase 0) / loopFrames`** | **214.38** | **209.36** | **−5.02** |
| bracketed (`Σ phaseTicks[1..39]`) | 214.40 | 209.39 | −5.01 |

⭐⭐ **`wall − phase 0` equals the bracketed total to 0.02 ms in BOTH runs.** The brackets are
complete; nothing is absorbed anywhere, and the compute win reaches the frame 1:1. What sat in the
raw wall figure was phase 0, and phase 0 is **boot plus the engine's own crash pause**: `$1753`
branches past `$1760` on every ordinary frame, and the only thing that ever spins there is the
2-second hold after a crash (`race_main_loop`'s tail stores `$9C` into `field_countdown` and spins
until it goes positive — **exactly 100 fields**, `symbols.csv` `$62F7`).

Count the fields and the arithmetic closes with nothing left over: phase 0 is **219.9 fields** at
HEAD and **315.0** after — **+95.1, i.e. one more 100-field crash hold** (≈ boot + 2 holds vs boot
+ 3). The faster build ran 11 more game frames inside the same emulated window and reached one more
crash with them. The `+12.6%` drain batches are the same fact seen from the body: during the hold
the spin calls `platform_tick_vbi` every iteration, so each arriving field is drained **as its own
batch** (8.37 → 7.47 ticks/batch) instead of ~11 accumulating behind one slow painted frame.

⭐⭐ **THE RULE, AND IT IS A NEW SHAPE OF AN OLD TRAP.** §Rule 3 says a *longer* run dilutes the
measurement with a static scene. This is the same dilution reached by the other axis: **a FASTER
BUILD gets further down the same trajectory in the same window, so it reaches the dilutant sooner.**
A change that works therefore *pulls in* more crash holds and reads as if it gave part of itself
back. So:
- **Quote `(elapsed − phase 0) / loopFrames`, never `elapsed / loopFrames`** — `phase4_prof.gdb`
  prints both, and the second is the one that is comparable across builds.
- **Check phase 0 between the two runs.** ⭐ Its tick count is **bit-identical** across runs of the
  same trajectory (`17618273` in two separate HEAD runs 40 minutes apart — boot and the holds are
  deterministic), so *any* difference there is a different workload, and the field count
  (`ticks / 80120`) says how many holds' worth.
- **When phase 26 moves, read `ONE BODY TICK` before believing it** (1276 → 1413 µs here, +10.7%
  with the body untouched and body ticks flat at 7 2xx/7 3xx). A per-tick cost that moves on its
  own is the trajectory talking, not the change: nothing edited is reachable from the body at all —
  the walk's only callers are `fill_dash_edge_columns` (phase 18) and `plot_view_src_line_core`
  (the view sweep).

#### ⚠⚠⚠ AND THE 10.4 ms IS A FRAGILE LOCAL OPTIMUM: BOTH FOLLOW-UP EDITS MADE IT SLOWER (2026-09-13)

Two changes were designed off the objdump, both aimed at the per-walk entry and the per-cell
branch layout, both predicted to save ~0.4 ms. **Both measured as regressions, and so did their
sum.** Matched 30 s warp runs, `PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`,
`phase4_prof.gdb` — every row at `loopFrames=668`, phase 0 = **315 fields** in all four, and
phase 11 (`draw_road`, untouched) reads 34.10-34.15 ms in all four, which is what licenses the
comparison:

| variant | phase 18 ticks | ms/frame | Δ |
|---|---|---|---|
| control (HEAD) | 27 245 155 | **10.18** | — |
| **Change 1** — `gap_store_base()`, deleting the zero-page pointer round trip | 28 084 598 | 10.50 | **+0.32** |
| **Change 3** — `__builtin_expect(src != 0, 0)` | 30 925 898 | 11.56 | **+1.38** |
| both | 33 959 745 | 12.52 | **+2.34** |

Neither was wrong about the traffic it deleted. Change 1 really does remove one of the two
zero-page byte-lane reassemblies per walk (`mem+0x1dde` reads 2 → 1, two indexed loads and an
`lsl.l #8`/`or.b` gone), reading `plot_ptr_v`/`plot_ptr2_v` out of registers the walk entry had
just written. Change 3 really does make the empty-cell arm the fall-through — SHAPE's census says
**all 151 cells take it** (`skip=0, table=0, colour=151`; ⚠ re-measured driving it is
`skip=1` — see the census correction above, which does not change this change's verdict) — collapsing fourteen out-of-line
landing pads and the two long branches per cell.

⭐⭐⭐ **WHAT THEY BOTH ALSO DID IS COLLAPSE GCC'S 4× UNROLL AND DE-HOIST FOUR LOOP INVARIANTS,
AND THAT COSTS MORE THAN EITHER SAVES.** Count the absolute reads of the walk's invariants in
`column_gap_walk_core`'s objdump:

| | HEAD | Change 1 | Change 3 |
|---|---|---|---|
| instructions in the body | 1176 | 339 | 344 |
| `mem[$1F]` `horizon_extent` | **1** | 2 | 2 |
| `mem[$85]` `EDGE_COLUMN` | **1** | 3 | 3 |
| `mem[$29]` `line_attr_1_limit` | **1** | 2 | 2 |
| `mem[$82]` `EDGE_BLOCK_START` | **1** | 3 | 3 |

At HEAD each is loaded **exactly once**, into `d3`, `d1`, `43(sp)` and `34(sp)`, and `&mem[srcBase]`
lives in `a1` across the whole walk; the per-cell path is `moveq/move.b/move.b (0,a1,d0.l),d5/
beq.w <pad>` → classifier → `move.b d4,(0,a3,d0.l)/bra.w` back into the middle of the unrolled
body. After either edit the loop is rotated so the empty-cell path re-enters *before* the pointer
biasing: Change 1's hot loop head is `1a056`, but the store path returns to `1a01c`, which re-reads
`mem[$82]` and re-executes three `addi.l #314696` pointer re-biasings per cell; Change 3 reloads
the source base with `movea.l 30(sp),a0` and reads `mem[$1F]` and `mem[$85]` absolute per cell.
**Four invariants × ~16 cycles × 151 cells ≈ 9 700 cycles ≈ 1.36 ms** — which closes against
Change 3's measured +1.38.

⭐⭐⭐ **THE RULE: AN OUT-OF-LINE LANDING PAD IS A REGISTER-ALLOCATION BOUNDARY, NOT JUST A LAYOUT
ARTEFACT.** On a register-poor machine the invariants survive in registers across the loop
*precisely because* the bulky inlined classifier is NOT in the loop's main flow. `__builtin_expect`
pulls it in, and GCC pays for it by evicting them. So the decode's lesson — "spell
`__builtin_expect` as the MISMATCH", §the framebuffer decode — **does not generalise to a loop
whose cold arm is a large inlined callee**; there it inverts.

⭐⭐ **THE COUNTING TEST, and it is cheap enough to run before every such edit:** weigh the
branches saved per iteration (a taken `Bcc.w` + a `bra.w` ≈ 20 cycles) against the loop invariants
that are in registers *only because* the body is out of line (≈16 cycles each per re-read).
**Grep the objdump for each invariant's absolute address and require the count to stay at 1.**
A collapse of the instruction count (1176 → ~340 here) is the same tell seen from the other side:
the unroll went with it.

⚠ This is the fourth instance of the standing trap in `docs/m68k-optimisation.md` §the inline
threshold — *changing a hot function's shape revokes a GCC decision worth more than the edit* —
and the first where the revoked decision was **unrolling plus hoisting** rather than inlining.
The other three: the span rasteriser's descriptor (a smaller body lost four specialisations,
−0.6% → +0.8% with `always_inline`), `gap_walk_body` inlined twice (evicted
`surface_colour_at_core`, +4.3 ms), and the packed `ViewState` ABI (§packing is not free).

⇒ **Do not retry either change.** What remains available in phase 18 is not source-level shape:
the per-walk `plot_ptr2_marshal_in()` (0.24 ms) is circular — the fast/slow gate needs
`plot_ptr2_v` before it can judge the walk safe — and dropping the entry `adc_overflow` (0.31 ms)
needs a written reader audit under the RESULTS rule because the exit V rides out in `SlotExit`.
The honest next levers are **structural** (fewer walks, or a hand-written kernel), not a rewrite
of this C.

#### ⭐⭐ THE FRAME-SLOT DEFECT CLASS IS EXHAUSTED OUTSIDE THE DECODE — A NEGATIVE RESULT (2026-09-13)

The decode's +8.1% came from a hot loop re-reading two loop-invariant stack slots per cell, and
CLAUDE.md turned that into a standing rule. So the whole native surface was scanned for the same
shape: SCCs of every hot `_core`'s objdump, ranked by stack-slot operands in the cyclic region.
**Nothing survived, and the reason generalises.**

| candidate | what the scan said | what it actually is |
|---|---|---|
| `column_gap_walk_core` | 296-insn SCC, **50 sp-operands**, 18 never written in it | `43(sp)`/`34(sp)` are two walk invariants **deliberately parked in the frame and loaded once**, read once per out-of-line landing pad — the arrangement §above measured at +1.38 ms to undo |
| `fill_edge_column_run_core` | 30-insn "loop", 6 invariant reads | **not a loop** — `0x1b042` is the `SlotExit` fill + `rts`, `0x1b06c` the `g_gapWalkSlow` fallback; the back edge is a jump into a shared exit tail |
| `plot_view_src_line_core` | 151-insn SCC, n(sp)=10 | 4 invariant reads, one ref each, in a 151-instruction body — ~2% of the loop |
| `fill_line_attr_core` | 515-insn loop | **n(sp)=0** |
| `interp_edge_core`, `sw_plot_1/2`, `paint_cells` | — | no per-iteration invariant reload; `paint_cells` is at the 43 cyc/unit floor |

⭐⭐⭐ **WHY A BIG `n(sp)` IS NOT A DEFECT SIGNAL ON THIS TARGET, and this is the lesson:** on a
register-poor machine **the frame is a legitimate home for a loop invariant**. A slot loaded once
into `43(sp)` and read from there by each of fourteen landing pads shows up in a static scan as
"14 reads of a never-written slot" — indistinguishable, by counting alone, from the decode's
per-cell reload of `tst.l n(sp)`. The two are opposite in value: one *is* the hoist, the other
defeats it. ⇒ **The metric is reloads per ITERATION OF THE HOT PATH, never stack-slot operands per
SCC** — and that means identifying the hot path first (which arm the census says the cells take),
because an SCC aggregates every path through the loop and a 296-instruction SCC executes ~80 of
those instructions on a pass.

⚠⚠ **AND BOTH LOOP DETECTORS I WROTE WERE WRONG FIRST, each in a way that produced a confident
ranking.** (1) *Any backward branch is a loop* — catches every shared exit tail and cold-path
landing pad, which is what promoted `fill_edge_column_run_core`; the invariants it "found" were
struct fields reloaded after a `jsr`, i.e. correct code. (2) *A natural loop's header must
dominate its back edge, so nothing outside may branch into the body* — GCC rotates loops and
enters them mid-body, so this filter reported **zero loops** in two functions that plainly walk
byte arrays. What works is an SCC decomposition that strips each component's headers and recurses,
reporting the leaves. ⭐ The general form of the trap: **a loop detector that is wrong in either
direction still emits a ranked table, and a ranked table reads as a measurement.**
(`docs/method-lessons.md` §a wrong loop detector still emits a ranked table.)

⇒ **Do not re-run this scan.** The remaining milliseconds in the view pipeline are structural —
the per-span representation, the per-span SMC emulation, fewer walks — not GCC's frame layout.

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

### ⭐⭐⭐ THE SPAN WALK IN 68000 ASM — ph11 **32.26 → 27.77 ms**, frame **132.90 → 128.23** (2026-09-23)

`span_walk_fast`'s loop + `fast_plot` are hand-written 68000 on the Amiga
(`src/platform/amiga/span_walk_m68k.s`; `make SPANASM=0` is the C control, `build=` bit 12 says
which arm ran). Two arms × two runs each, `PROBEFIELDS=3000`, identical to the tick run for run,
phase 0 = 120 fields on all four. The C loop stays as the host's walk and as the asm's reference.

**What it is.** Twelve routines generated from gas macros: shallow {fwd, rev} × {step in, step out} ×
{+1, −1} and steep {fwd, rev} × {+1, −1}. The eight columns are UNROLLED, so which plotter, which
pointer and which pattern byte a column uses are constants in the instruction. A column that
doesn't plot is `add.b`/`bcc` (14 cycles), and the first line's computed entry is a jump table.
The whole state lives in registers: acc, y, both deltas, the block, lineEnd, bh, three pointers,
the destination, and `dash_block_starts[block]` as a pointer. Rare values ride in upper words:
the line count in d5's, markOn in d6's, colMark as bit 8 of the accumulator. The cold arm of the
plot (a filled cell) sits in `.subsection 1`, so an empty cell runs straight through. The C
bridge pins eleven register variables around one `jsr`, and GCC compiles it without complaint.
The asm saves only a5/a6.

**Single-stepped** (`steptrace.gdb`, 24 calls): **~79 instructions per real walk, against ~222**
for the C walk + plot + guard — the plan's 60-80. The rest of a real `interp_edge_core` call is now
~400 instructions of SETUP, of which the pattern-table rebuild is the biggest line
(docs/open-work.md, step 1 NEXT).

⚠ **The estimate was ~8-10 ms and the phase table paid −4.49.** The asm hit its instruction
target, so the gap is the DENOMINATOR: "×42 calls a frame" counted every call as a walk, and
the trace shows 8 of 24 calls are publish-only (61-65 instructions, no walk at all). ⇒ price a
per-call saving by the calls that take the path — CLAUDE.md's "check how many times it RUNS",
once more.

**How it was gated — and the two holes the game alone would have left.**
1. `make WALKCHECK=1` + `amiga/walkcheck.gdb`: on every span the asm takes, the C loop and the asm
   run on the same bytes, and everything either can write is compared (the visited pages plus
   one, the destination, and the handed-back y/pointers/block/abandon). **0 mismatches over ~7560
   game spans on all five circuits**, 0 fallbacks.
2. ⭐⭐ **THE GAME DOES NOT REACH EVERY ROUTINE.** Silverstone, driving, produced **no steep span and
   no +1 step in 4000 fields**, so half the routines had never run. Only Brands Hatch and
   Snetterton reach them, and not all of them. So WALKCHECK starts with a **randomised self-test**:
   6000 spans drawn inside what the guard and `span_asm_variant` admit, on randomised cells,
   `dash_block_starts` and pattern tables. Every byte is saved and restored. It covers all 12
   routines (~300-860 cases each, 2543 abandons). ⇒ **a target-side differential of a routine with
   variants needs a fuzzer as well as the game's own data**, the same way `validate` needs one.
3. ⭐ **The first WALKCHECK found a real gap in the design:** a third of Silverstone's reverse spans
   start with a block low enough to step below 0, and the C reads `dash_block_starts[(uint8_t)block]`.
   The first build refused them (353 fallbacks). The fix is a wrap-exact pointer step (`bcc` over
   a `lea ±256`, 8 cycles), and those spans now take the asm.
4. **Seven sabotages, all caught:** the first-line test off by one; the marker's `$80` test dropped;
   plotter 2 reading through p2; the `$55` substitution dropped; no abandon; a carry-in of 1 into
   each line's first add; the block pointer not following the wrap. Numbers 1 and 7 were caught
   ONLY by the self-test, with 0 mismatches on the game's spans.
5. **The DDA carry derivation is CONFIRMED on the data:** the C reference counts every add whose
   carry-in is non-zero (`g_walkCheckCarryIns`), and it read 0 on all five circuits. Sabotage 6
   shows the check would see a carry of 1.
6. ⚠ The plan's parked-picture compare is **confounded** and was not a gate. Parked in first
   gear the car creeps and the revs move, so two builds paint different sim states at the same
   field: the rev needle differs, and edge bytes differ on 105..132. It is the same rule as the
   `FASTMEM` one: a render-speed change moves the trajectory. The images are identical to the eye.

### ⭐ AFTER THE ASM WALK: THE C AROUND IT, and where C's floor is (2026-09-23)

Re-traced, a real `interp_edge_core` call was ~450 instructions: 79 of them the asm walk and **~130
the C plumbing around it**:
- the bridge, 59 (`span_asm_variant`, eleven pinned registers, the unpack);
- `span_walk_fast_ok`, 56;
- the pointer marshal, 18.

Two lessons came out of it, both measured.
1. ⭐ **A guard that is PROVABLY TRUE ON ONE CALLER'S PATH should not run on that path.**
   `interp_edge` has just set all three pointer pages from a block below `$28`, so every page the
   walk can visit is in `$30..$44`. It has also just set the steps and the marker switch, so they
   can't trap. `span_walk_direct` passes the values it holds and skips the guard, while keeping the
   one test a fixture can defeat (the destination): **−1.21 ms**. State the proof at the code: the
   other callers (the 6502-ABI arms, the fixtures) still take the guard.
2. **Rewriting the setup over locals** (no read-back of a stored cell, every store kept) paid only
   **−0.34**: Rule 1b's per-cell floor. The remaining ~246 instructions are the stores plus the
   computation. The coarse lever was the setup in asm, sharing the walk's registers (next section).

### ⭐⭐⭐ THE SPAN SETUP IN 68000 ASM — THE WHOLE PASS: ph11 **26.62 → 17.10 ms**, frame **126.64 → 116.47** (2026-09-24)

`draw_surface_spans_core`'s loop, `interp_edge_core` and the walk are ONE hand-written routine on the
Amiga (`src/platform/amiga/span_pass_m68k.s`; `make SETUPASM=0` is the C control, `build=` bit 13
says which arm ran: `3c1d` against `1c1d`). One run per arm, `PROBEFIELDS=3000`, `frozen=` on both,
phase 0 equal to 0.1 field. **ph11 −9.52, bracketed −10.17** (the other rows are trajectory, ±0.4,
from 443 painted frames against 414). `draw_road` is now **17.1 against the real BBC's 16.5**, which
is parity once the ~0.118 ms a call of probe overhead is taken off.

**What it is.** The loop picks each span's style exactly as the C does. The setup computes every walk
input IN the register the walk takes it in, then does `jsr span_walk_enter`: the walk's own jump
table, entered with a5/a6 already loaded. The walk file's entry was split for this, and the C bridge
still uses the outer entry. C is called back for two things only: the cap (`span_cap_line`, because
it can run a circuit's hook) and an entry offset the chain cannot mean.
- ⭐ **`a5 = mem + $628F` is one base for everything.** The walk wanted it for the pattern table, and
  through `d16` it also reaches every zero-page cell (12 cycles a byte, no `abs.l`).
- ⭐ **It STORES WHAT THE C STORES, span by span, and the plan's per-pass flush was not built.** The
  walk uses all fifteen registers, so a cross-span value would cross it on the stack, which is no
  cheaper than its zero-page cell. Per-span stores also keep `mem[]` byte-identical after every span,
  which is what the cap's circuit hook sees. So `determinism` and `validate` (which run the C) need
  no re-record and no `set_ignore`.
- The C-private step and marker statics (`g_spanStepIn/Out`, `g_spanMarkOn`) are not written. Every
  C walk path sets them before it reads them.

⚠ **The estimate was 5-8 ms and it paid 9.5.** For once the error is in the good direction, and the
reason is the same denominator as last time, read the other way: the loop moved into the asm too, so
the **publish-only calls (a third of all calls) and the skipped points also stopped paying a C call**,
which the per-real-call arithmetic never counted.

**How it was gated — `make SETUPCHECK=1` + `amiga/setupcheck.gdb`:**
1. Per pass: snapshot all 64 KB, the three pointer words and the cpu struct. Run the C pass with the
   C walk (`g_setupCheckRefC`), keep the result, restore, run the asm pass, and compare everything.
   On a mismatch the C result is put back, so a defect cannot derail the run that is counting it.
   A byte loop over 64 KB was most of the check's cost; comparing two longword COPIES (never
   `mem[]` aliased wide) made it affordable.
2. **A 2000-pass fuzzer first** (24,877 spans; random edge points, style records, clip/arm history,
   split, pass, pointer low bytes, destination, keep/and tables, cells). It covers all twelve walk
   variants, 350-2570 each. **0 mismatches, and 0 over 400 game passes on each of the five circuits**
   (Silverstone: 2654 walks, four variants only; Oulton and Snetterton also reach +1 steps and both
   steep arms). ⚠ **Brands Hatch's 400 were ONE FRAME REPEATED** — its variant counts came out as
   round hundreds (500/900/100/400) — so it was re-run for 1600 passes on a moving scene (9833
   walks, steep ones included), 0 mismatches. **A census of round numbers is a static scene; read
   the counts before the verdict.**
3. **Eight sabotages, all caught:** the endpoint bias; the scan lines not swapped with the ends; the
   `giveBack` shift; the step-in selection inverted; the start line; the clip history not shifted;
   the arm not crossed with the swap; the `$55` substitution dropped. ⚠ **Numbers 2 and 7 were
   caught ONLY by the fuzzer** (0 of 400 Silverstone passes take the endpoint swap), the same
   lesson as WALKCHECK's 1 and 7.
4. It also found that `make cpu-lint` had been RED since `17cdb6e`: the abandon cap moved into
   `span_walk_fast_run` and the allowlist row still said `span_walk_fast`. Run the lints in the
   commit that moves a `cpu` site, not in the next session.

### ⭐⭐ VIEW_PAINT_LINES, SINGLE-STEPPED — and the low block through the group painter is a LOSS: +5.74 ms (2026-09-24)

**The split** (`amiga/steptrace.gdb` targeted at `view_paint_lines`, three whole sweeps, driving;
control ph24 20.20 + ph33 14.32 = 34.52 ms): **~24.6k instructions a sweep**, ~900 of them the
VERTB ISR, so ~10.3 cycles an instruction — the step count and the phase table agree.

| block | instructions a sweep | where |
|---|---:|---|
| `view_scan_all` — the transposed source scan | 7 114 | ~548 longword tests (the floor-skipping loop, 2× unrolled) + ~80 non-zero longwords' lane bodies |
| the low block, display 117..157 (ph33) | ~10 200 | `view_low_run` 6 843 (fill 4 instructions a cell, ~46 a run of set-up), `view_own_low` 2 064, `revs_plot_low_line` 820 (a call a line), `step_scanline` ~500 |
| the full-width lines 81..116 | ~4 900 | `revs_plot_terrain` 3 877 (108 a line), `view_own_full` 594, `step_scanline` ~430 |
| `revs_plot_own_reset` | 638 | |

So the low block is **237 instructions a line** and the full-width lines 108, and the difference is
the clipping to the car's two runs.

⛔⛔ **AND DELETING THAT CLIPPING LOSES.** With the cockpit on PF2, which is opaque over every cell
of 117..157 outside the runs (RevsScreen.cpp §revs_cock_line), the car's own cells are invisible
in PF1, so the low block was sent through the full-width group painter: the line recorded like
the lines above it, run B's entry seeded into the event list, and inside the needle's window
(128..157 × cells 12..27, where PF2 is transparent and PF1 is the needle painter's) only the run
cells stored. It was **correct** — a new oracle compared the VISIBLE picture (every PF1 bit under a
transparent PF2 pixel) against the run painter, with every owed cell poisoned first so an omission
could not hide behind the reference: 0 mismatches in 272 240 cells, and the sabotages that
intrude on the window, drop its edge cell, drop the window or seed a wrong colour all fail. And it
was **+5.74 ms**: ph33 14.32 → 20.06, frame 116.42 → 121.97, phase 0 equal.

Single-stepped again and split PER LINE (the painter's loop head segments the trace), the full-width
group painter costs **~110 instructions a line on display 81..116, ~180 on 117..127 with no
clipping code at all, and ~246 on the window lines 128..157** — against the run painter's ~187 of
painting — before ~96 a line of recording and seeding. ⚠ The first version of this note said the
car groups cost "~86" and that the run cells were "event-dense"; both were inference, and both
were wrong: a host census puts the low block at **~2 events a line**. The cost is the group
painter's BYTE ARM — a group holding an event runs a four-iteration byte loop, ~45 instructions —
so a line's price is ~25 + ~8 per uniform group + ~45 per group with an event in it, and the upper
lines average 110 only because many of them are flat.
⛔ **And a masked-merge byte arm is not the fix** — an event at byte `j` as one masked XOR per
plane with a tail-mask table measured **ph24 +0.44** (a second accumulator pair made GCC spill
`lo4` in the UNIFORM arm) and **+0.73** merged in place (no spill in the uniform arm): on this
data the byte loop is cheaper than the merge's three table loads a event. Both reverted; the
painter's oracle (`TERRAINCHECK`, 0 of 66 920 cells) and four sabotages were green, so it was
the price, not the correctness.
⇒ **The low block through this painter costs about what it saves in C**; the per-line and per-run
plumbing (~160 of the 237) is what is left to win, and that is the span pass's lesson: one
register-resident routine.

⭐ **AND WITH THE NEEDLES ON SPRITES IT SHIPPED — −0.41 ms** (frame 115.95 → 115.54; ph33 14.32 →
11.77, ph24 20.16 → 22.12). The user's directive removed the window's reason to exist (§12d: the
needles are prerendered sprites, so the dial art is on PF2 and PF1 has no hole), and run B's entry
is appended by the SCAN as it passes cell `b0` (O(1), after that cell's own sources) instead of
walked into each line's list. Re-traced: 23.8k instructions a sweep against 24.6k — the drivers
10.9k → 2.7k, the terrain painter 3.9k → 10.8k (77 lines; ~170 a line on the low block), the scan
7.1k → 8.2k (the seeds and their chains). ⇒ the painter is now 45% of the sweep and the C byte arm
is what it pays, so the next move is that painter in asm. Gate: `make LOWFULLCHECK=1` (the
visible-picture oracle, every owed cell poisoned), 0 of 295 200.
⭐⭐ **AND THE PAINTER IN 68000 ASM IS −5.38 ms** (frame 115.51 → 110.13; ph24 22.12 → 20.55, ph33
11.78 → 7.98; `src/platform/amiga/terrain_m68k.s`, `make TERRAINASM=0` the control, both arms in one
session at `PROBEFIELDS=3000`). Three moves, all in the file's banner: the question "does a run
change here?" is asked once per EVENT, never per group — the groups before an event's group are one
computed jump into ten unrolled `move.l` pairs, entered at −(cells to fill) because a group is four
cells AND four bytes of code; a group an event SPLITS is filled whole in the old colour by that same
jump and only its tail rewritten (a byte, a word, or both, per plane), so the masked merge the C lost
twice is not needed at all; and there is no per-line address lookup, because both drivers step one
display row per sweep line (`step_scanline`), so a line's plane-1 row starts where the previous
line's plane-2 pointer stopped — proved at the block's two ends by the caller (a scan-line step cannot
move zero rows), re-proved per line by `TERRAINCHECK`. Gates: `TERRAINCHECK` 0 of 508 200 cells,
row contiguity 0 bad, 0 fallbacks, `LOWFULLCHECK` 0 of 270 600; five assembled sabotages
(`TERRAINASM_SABOTAGE=1..3,5,6`) each caught, by both oracles where the defect reaches the low block.

⚠⚠ **And the oracle had the shared-input blind spot until it was sabotaged.** With the seed moved
into the scan, the REFERENCE run painter saw the seed as a real event at its run's first cell and
took it, so a wrong seed colour PASSED (0 mismatches) where the earlier variant — seeding after the
reference ran — had caught it at 7129. The reference now gets a copy of the list without the seed
(`s_lowSeedPos`), and the same sabotage fails at 7134. ⇒ **when a change MOVES a computation earlier
in the pipeline, re-check that the oracle's reference still runs on the input from BEFORE it.**

Two things this left that stand on their own:
- ⚠ **A sabotage can survive by COINCIDENCE OF DATA, and that is a third outcome beside a fixture
  gap and an unreachable arm.** Dropping the seed, or seeding run A's colour, survived because on
  the driving trajectory both screen edges are the same off-road surface — so the carried colour
  equals the entry. Seeding `~entry` failed at 7 129 cells. ⇒ **when a sabotage survives, re-run it
  with a value that ALWAYS differs before deciding which of the three it is.** And a census beats
  argument: my first reading (a real event always sits at `b0`) was the exact opposite of the truth
  — the seed inserted on 6 806 of 6 806 lines, because `fill_dash_edge_columns` diverts each fill
  run's first cell into `view_right_start_src`.
- ⭐ **A visible-picture oracle is the right gate for any change under a dual playfield**: compare
  PF1 only where PF2 is transparent, and POISON the cells the new code owes after snapshotting the
  reference, or an omission reads as agreement. (Built as `LOWFULLCHECK`, ~60 lines, and not
  kept with the reverted painter: run the old painter into the target, snapshot the rows, invert
  every cell the new painter owes, run it, then require `(old ^ new) & ~(pf2a | pf2b) == 0` per
  byte on both planes.)

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
- ⚠⚠ **THIS HARNESS UNDER-READS A WIN BY ~4x AT THE CURRENT OPERATING POINT — see Rule 1a, which
  measures it both ways with a known burn.** The framerate is quantised to `50/N` and the frame
  wait pads every saving back up to the next field boundary. The older note here said the opposite
  ("over-reads a win; under ~3% is agreement, not evidence"); that reading came from cross-run
  comparisons whose WORKLOAD had shifted, and Rule 1a's controls settle it. **Size a change in
  ms/frame from the phase table; read FPS only as the standing baseline.**
- ⚠ Sample in SHORT segments and discard rows where the run stopped doing the work being measured
  (the car leaving the track). A wide window straddling that under-reports badly.

**So: quote a static cycle count or a differential ratio as the win, and an FPS row only as the
standing baseline.**

## ⭐⭐⭐ Rule 1a — THE FRAMERATE IS QUANTISED TO `50/N`, SO SIZE A CHANGE IN **ms/frame** (2026-09-13)

`PlatformAmiga::renderFrame()` presents and then spins until `g_vbiCount` changes, so **a painted
frame always lasts a whole number of PAL fields** and the framerate can only ever be `50/N`. The
spin pads whatever the frame's work is up to the next field boundary — so a saving *smaller than
the current pad* is entirely real and entirely invisible to FPS. So is a regression.

**Measured, with a known burn.** N x 14 000 known cycles (1975 µs each) added to the painted frame,
one burn per `renderFrame()`, in an otherwise honest `FPSCOUNT=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1`
build, with the burn's own counter printed beside the series so the switch proves it ran (it read
exactly one burn per painted frame):

| burn | added ms/frame | FPS, non-outlier rows | FPS reads | unquantised would be | mean fields/frame |
|---|---|---|---|---|---|
| 0  | —     | 5.053 | —      | —      | 9.895 |
| 4  | +7.9  | 5.004 | −0.97% | −3.8%  | 9.992 |
| 8  | +15.8 | 4.702 | −6.9%  | −7.4%  | 10.634 |
| 12 | +23.7 | 4.563 | −9.7%  | −10.7% | 10.958 |

The first 7.9 ms of REAL added cost bought a 0.97% FPS change — **75% of it absorbed by the pad**.
Past the field boundary the response returns to ~90% of linear. The pad here is ~0.6 field ≈ 12 ms.

⚠⚠ **Both signs of the same factor, measured in one session:**
- the unit-loop unroll saved **−12.5 ms/frame (+6.2%)** on a workload-identical phase differential
  while `fps_series` read **+1.5%** — under-read **4.1x**;
- the burn added **+7.9 ms/frame (−3.8%)** while `fps_series` read **−0.97%** — under-read **3.9x**.

**So the scoreboard is the PHASE TABLE in ms/frame; FPS is a derived `50/N` that follows.** (User,
2026-09-13: *"you should not be looking at FPS numbers (they're indeed 50/N) but pure millisecond
counts and get those down. FPS will follow."*)

⭐⭐ **AND THE PAD HAS ITS OWN PHASE ROW, SO THE PHASE TABLE SHOWS THE ABSORPTION DIRECTLY:
`phase 28` IS the spin.** A change that removes real compute reads as `phase 11 −4.79 ms,
phase 28 +3.03 ms, Σ1..39 −1.39 ms` — and the +3 ms is not the change giving itself back, it is
the pad growing to the same field boundary. ⇒ **size a change against `Σ(phases 1..39) − phase 28`,
or against the one phase row you changed; the bracketed FRAME total moves in 20 ms steps and
reads two identical control runs 2 ms apart.** (Measured on the span plotters' packed-ABI change,
2026-09-13 — the section below.)

⭐⭐ **And the corollary is good news: the payoff is a STEP FUNCTION.** The frame's work now sits at
~9.06 fields, just above the 9-field boundary. The next ~1.5 ms moves the bulk of frames from N=10
to N=9 — about **+10% of framerate for 1.5 ms** — and every millisecond cut before that is banked,
not lost. This is also why several of this project's "null results" deserve re-reading: a change
measured at −0.4% or +0.65% by FPS alone was never shown to be worth zero *milliseconds*.

⚠ To rebuild the burn (it is not committed — it exists to price the grid once, and the grid is now
priced): a `for (int b = 0; b < REVS_FPSBURN; ++b)` around `probe.cpp`'s exact
`move.w #999 / nop / dbra` idiom at the top of `renderFrame()`, a `volatile unsigned long`
call counter beside it, and `ifdef FPSBURN -> EXTRA_DEFINES += -DREVS_FPSBURN=$(FPSBURN)` in
`amiga/Makefile`.

### ⭐⭐ THE DIFFERENTIAL THAT CAN SEE A MILLISECOND — the phase table, arm against arm

```
cd amiga && make clean && make -j4 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1
. ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 30
```
…then the same for the other arm, **in the same session**, and read `ticks/g_phaseFrames/4006` as
ms/frame rather than the truncated integer column.

⭐⭐ **What makes it a fair A/B is that the UNRELATED phases must agree**, and they do to a degree
that no FPS row can approach — which is the proof the two arms ran the same workload despite the
trajectory being free to shift:

| | arm A | arm B | Δ |
|---|---|---|---|
| `build_track_geometry` (5) | 26.70 | 26.69 | −0.01 |
| `draw_road` (11) | 38.97 | 38.95 | −0.02 |
| `fill_dash_edge_columns` (18) | 17.12 | 17.11 | −0.01 |
| frame-buffer decode (27) | 25.05 | 25.13 | +0.08 |
| **control total** | **107.84** | **107.88** | **+0.04%** |
| view phase 1 (24) | 21.99 | 15.80 | **−6.19** |
| view phase 2 (33) | 14.69 | 12.06 | **−2.63** |
| view phase 3 (34) | 25.13 | 23.46 | **−1.68** |
| 50 Hz body drain (26) | 13.79 | 12.99 | −0.81 |
| vblank spin (28) | 11.97 | 10.63 | −1.33 |
| **frame total** | **216.27** | **203.75** | **−12.52 (+6.15%)** |

Arm A is `fd414b4`, arm B the 4-way unrolled unit loop (`fa046af`). The census was identical in
both (2148 unit visits, 118 runs, 77 lines per frame), and the same pair at a 30 s window read
−10.57 ms of view painting with the controls agreeing to 0.29% — **so the number is
window-independent**. The three derived rows (drain, spin, body arm) are consequences of a shorter
frame, not separate wins: fewer 50 Hz ticks to drain per painted frame, and less pad to spin.

⭐ Phase 1's 10.4 µs/unit against ~45 nominal cycles from the objdump reproduces the documented
~1.6 contention factor, so the cycle model behind the prediction was sound all along: the
objdump predicted ~6.8% and the honest differential measured 6.15%.

### ⭐⭐⭐ BOUND THE WINDOW IN EMULATED TIME, NOT HOST TIME — `make PROBEFIELDS=N` (2026-09-16)

**This is now the protocol for every phase-table A/B, and it takes the instrument from ±2 ms to
±0.03 ms.** `diag_run.sh N` bounds a run with `sleep`, i.e. HOST seconds, and under warp the
emulator's throughput moves with whatever else the machine is doing — including this session's own
greps. Two arms of one A/B then cover different amounts of GAME time, and a `STRAIGHT_TO_RACE` run
eventually leaves the track and resets, so the longer arm is diluted with a different scene mix.
Measured: **75 s against 144 s of emulated time from the same 30 s wall window**, one arm meeting
one crash hold and the other three.

`make PROBEFIELDS=N` freezes `g_phaseTicks` / `g_phaseCount` / `g_phaseFrames` — and phase 0 — after
exactly N display fields, so both arms describe the same emulated window:

```
cd amiga && make clean && make -j4 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 PROBEFIELDS=3000
. ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 45
```

⭐ **Fields, not painted frames, and the distinction is the whole point.** A faster build drains
fewer 50 Hz body ticks per loop frame, so capping on *frames* would hand the arms different amounts
of SIM time and hence different scenes. Fields are emulated real time: at the cap the sim
trajectory, the scene sequence and the crash holds are identical, and the arms differ only in how
many frames they PAINTED inside the window — which is the thing being measured.

**What it buys, measured with a second control run of identical source** (`docs/span-render-plan.md`
§10p step 3a, five arms): **+0.03 ms on a 197.53 ms frame — 0.015%**, worst untouched phase 0.023 ms,
phase 0 agreeing to 0.9% and `loopFrames` to 0.4%. Against the wall-clock protocol's ±2 ms two-
control spread and the FPS instrument's 3.3% one-row resolution, that is a different class of
instrument: a 0.2 ms change is now quotable.

⭐ **Two validity fingerprints, and BOTH must be read before diffing two arms:**

- `frozen=` in the header must be **non-zero on every arm.** Zero means that arm never reached N
  fields inside the wall delay, so its window was the wall clock like any other run and it is not
  comparable. The run keeps going after the freeze — the window closes, the program does not — so
  the delay only has to be long enough for the *slowest* arm.
- **`frozen`'s VALUE is the window in beam ticks, and `N × 80120` is what it must equal.** A
  verification run read 240 521 225 against the exact 240 360 000 — **0.07%** — which independently
  confirms the freeze fired on the right field and that the 4006-ticks/ms phase clock is calibrated.
  A value far off `N × 80120` means the field counter and the beam clock disagree; stop.

⚠ `g_vbiCount` is a `uint16_t`, so N must stay under 65536 (21.8 minutes).

#### ⚠⚠ THE PARTIAL-FREEZE TRAP — a frozen numerator over a live denominator prints a plausible lie

Found within minutes of building the cap, and it is the reason the instrument needed sabotaging
before its output was believed. **The freeze stops the phase accumulators. It does not stop the
program**, so every counter bumped from somewhere else keeps climbing: the body drain, the ISR, the
view census, `g_vbiCount`, `g_beamEpoch`. Any row mixing the two is fiction — and it does not look
like fiction:

| row | read | truth | by what factor |
|---|---:|---:|---|
| `ONE BODY TICK` | 379 µs | 1414 µs | the whole run ÷ the window |
| view phase 1 `units/frame` | 5526 | 1440 | ditto, inverted |
| view phase 1 `lines/frame` | 138 | 36 | ditto |
| `ticks/field must be ~1` | 0.27 | 0.99 | ditto |

Every one of those is a *plausible* number in a table nobody flagged, which is the failure mode this
project pays most for. **The fix is a SNAPSHOT at the window's edge**, not a gate on the counters:
`probe.cpp` copies `g_bodyTicks` and the nine census longwords once when the freeze fires, and
`phase4_prof.gdb` reads the snapshot whenever `g_probeFrozen` is set (`$wall` / `$body` / `$fields`
/ `$u` / `$r` / `$l` — there is exactly one place each is chosen).

⭐⭐ **Gating the counters would have been the wrong fix, and the reason generalises: the census
macros run per UNIT VISIT, thousands a frame, so a `g_probeFrozen` test inside them adds a load to
the very loop whose unit count is being A/B'd.** An instrument that moves the measurement is worse
than no instrument. One copy of nine longwords at the window's edge costs nothing measurable.

⚠ **The instrument was verified against its own known-bad control**, which is the cheapest possible
sabotage: the pre-fix run had already published five wrong values, so re-running that exact arm and
requiring all five to land on the independently-known truth (1414 vs 1313–1417 µs, 1440 vs 1442
units, 36/16/25 lines) is a real check and not a self-consistent one.

### ⚠⚠ ...AND PROVE THE FLAGS REACHED THE BUILD — zsh ate all but the first (2026-09-16)

**`zsh` does not word-split an unquoted parameter.** So the obvious way to write an A/B driver
script is silently wrong:

```zsh
COMMON="PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 SPANSTAT=0"
make -j4 $COMMON SPANFILL=4        # ⛔ ONE argument: make reads PROBES = "1 FIXED_RNG=1 …"
```

`make` accepts it — the first `=` makes it a variable assignment — and **every flag after the
first is never set**. Nothing fails, nothing warns; `ifdef STRAIGHT_TO_RACE` is simply false.
It cost a four-arm quad and three oracle runs, in two different ways at once:

- the arm flags were separate words, so `SPANFILL=n` *did* apply while `SPANSTAT=0` did not ⇒ the
  three span arms each carried ~0.6 ms of `volatile` counter RMWs that the control (no span path)
  did not, which is most of the "unexplained" phase-24 rise the arms were being judged on;
- a run whose `SPANFILL=4` sat *inside* `COMMON` built **no span emitter at all**, and its gdb
  script then failed on `No symbol "g_spanEmitLines"` — the one loud symptom in the whole episode;
- and `STRAIGHT_TO_RACE` never applying put the whole quad on a **different trajectory**
  (`FRAME = 266 ms` against this project's 208 ms baseline). ⭐ A frame total that does not
  resemble the published baseline is the cheapest tell that a flag did not land — check it first.

⭐⭐ **The fix is a FINGERPRINT the build prints for itself**, not more care: `probe-audit` reports
`$(words $(PROBE_SYMS))` on every link, and that count is a function of the flag set, so assert it
in the script and abort the arm when it disagrees:

```zsh
COMMON=(PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 SPANSTAT=0)   # ⭐ an ARRAY expands to words
make -j4 $COMMON $X > $arm.build 2>&1
N=$(grep -o 'probe-audit: clean ([0-9]* symbols)' $arm.build | tail -1 | tr -dc 0-9)
[[ "$N" == 152 ]] || { echo "⚠⚠ FLAGS DID NOT REACH THE BUILD"; continue; }
```

⚠ To read what `make` itself computed for a flag set, note that macOS ships GNU make 3.81 (no
`--eval`); pipe a wrapper makefile instead:

```zsh
printf 'include Makefile\nx:;@echo $(words $(PROBE_SYMS))\n' | make -f - x SPANFILL=4 SPANSTAT=1
```

⚠ Every measurement published before that session was invoked with its flags spelled out
literally on the command line (checked, across every transcript that mentions `NOUNITS` or
`SPANFILL`) — the blast radius was the script-driven runs only. The general rule: **a run script
is an instrument, and an instrument must be sabotaged and fingerprinted before its output is
believed** — the A/B switch printing its own state (`g_span*` counters) was not enough here,
because the switch that failed was the one that decides whether those counters exist.

## ⭐⭐⭐ Rule 1b — AN OBJDUMP DELTA IS NOT A FORWARD PREDICTION, AND THE SCALING TEST SAYS SO (2026-09-19)

Reading the objdump is the cheap way to FIND a defect (it found four in this subsystem). It is not
a way to SIZE one. The view sweep's stop tail was nine instructions, seven of them memory operands,
taken on **82 runs a frame**; deleting it is a static ~90 cyc × 82 ≈ **0.7 ms**, and the phase
table paid **0.118**. The three edits before it in the same body went −0.733, −0.136, −0.118
against static predictions of the same shape — so on a driver at the 68000's register ceiling,
**a static count over-reads by roughly 6×.** Why: the count prices each instruction at its
worst-case operand cost, in isolation, on a path the count merely assumes it is on.

⭐⭐⭐ **THE DIAGNOSTIC IS THE SPLIT, NOT THE TOTAL — IF A DELETION'S WIN DOES NOT SCALE WITH THE
COUNT OF THE THING IT DELETES, THE INSTRUCTION WAS NOT ON THE PATH THE COUNT DESCRIBES.** That
0.118 ms is **−0.111 in phase 2 and −0.008 in phase 3**, and phase 3 has **50 runs against phase
2's 32**. A genuinely per-run cost pays out in proportion to runs; this one paid out inverted. No
further objdump reading can say which path it really was on — only an arm can. ⇒ **once a body's
edits are down to the 2-7 instruction grain, stop sizing them and either measure one or move to a
coarser lever.**

⚠ This does not retract "read the objdump before theorising about the algorithm" — that rule is
about FINDING, and it has paid repeatedly (the frame-slot defect, the `n(a5)` DDA state, the
sorting network). It bounds what the reading entitles you to CLAIM.

⭐⭐ **THIRD INSTANCE, AND IT NAMES THE OTHER HALF OF THE ERROR: A BY-BODY INSTRUCTION CENSUS
COUNTS INLINE COPIES, NOT EXECUTIONS — SO DIVIDE BY THE CALL-SITE COUNT BEFORE RANKING.**
`interp_edge_core`'s dump attributes **211 instructions to `plot_store_resync`** plus 124 more at
its dispatch line, second only to `span_walk`'s 965 — which reads as the biggest thing in the
routine after the walk. It is nine inline copies of a six-instruction fast path, and the published
leaf census (**60 plotted columns a frame**, ≤3 guard calls each) caps the whole guard at
**≤0.9 ms**. ⇒ **Rank a body in a dump by `instructions ÷ inline copies × a COUNTED call rate`,
and if you do not have the call rate, get it from the census before reading any further.** The
census cost nothing; the dump reading cost an hour.

⚠ And the host-counter route to that call rate is **not** free the way `docs/method-lessons.md`'s
"count it on the host" rule implies, if you reach for it carelessly: the host Makefile has no
`CFLAGS_EXTRA`, so a counter goes in through `OPT=`, and **`make` does not track flag changes** —
either you get a mixed-optimisation binary (the previous object files survive) or you `make clean`
and a whole-corpus `-O1` build makes a 300-frame run take **>10 minutes** instead of ~1. Check for
an existing published count first.

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
