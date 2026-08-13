# Performance method — how to get a number you can trust

> ⚑ **Method inherited from the Atari port**, where these rules were each learned by getting a
> number badly wrong first.  The *numbers* from that project are not carried over.  Revs's own
> baseline was measured on 2026-08-12 and is below — before that date this section said "Revs has
> no baseline yet, and inventing one would repeat the exact mistake this document describes".  Companion: `docs/m68k-optimisation.md` (what to do once you know where the time
> goes), `docs/headless-fsuae.md` (how to drive the target).

## The target machine

**A500, 7 MHz 68000, PAL.**  A frame is 20 ms.  Spending 10 ms on *anything* is half the budget.
Units: 1 raster scanline = 63.56 µs; a PAL frame = 313 lines.  Be conscious of absolute
milliseconds, always — a percentage of an unknown total is not a measurement.

## The target ⭐

**Goal: 50 FPS.  Floor: 25 FPS.**  User decision, 2026-08-12 — a scope call, not a prediction,
and explicitly "whether that's reachable remains to be seen".

- **These are DISPLAYED frames**, as measured by Rule 1: `FPS = 50 * g_fpsFrames / g_vbiCount`.
- **The sim tick is a separate thing and is not negotiable.**  Revs's 50 Hz body is a User VIA
  T1 interrupt on the BBC (`docs/static-map.md` §The interrupt) and runs in the real
  `INTB_VERTB` ISR on the Amiga.  It ticks 50×/s whatever the display does — so 25 FPS means
  painting every other frame with the simulation still at full rate.  A port that hits 50 FPS by
  slowing the game body is not a port that hits the target.
- **25 is a floor, not a fallback to settle into.**  Below it the phase is not done; at or above
  it the remaining gap is an optimisation backlog, not a blocker.
- ⚠ **The target does not license quoting a number before measuring one.**  Everything below
  still applies: the first honest figure comes from Phase 4's baseline run, and "we need 50" is
  never evidence that a change bought anything.  (Postmortem §4.1: profile a slow end-to-end
  skeleton on real hardware *before* committing to an approach.  The Atari port's retired
  "50 FPS is impossible without an algorithm change" conclusion was disproven by hand-asm — the
  ceiling was GCC, not the algorithm.  That cuts both ways: don't declare it impossible from
  reasoning, and don't declare it reached from optimism.)

## ⭐ THE BASELINE — measured 2026-08-12, Phase 4

**≈1.4 FPS.**  Goal 50, floor 25.  So the port is **~18× short of the floor** and ~36× short
of the goal, before a single native twin exists and before anything is drawn.

| | |
|---|---|
| Build | `make clean && make -j4 FPSCOUNT=1 FIXED_RNG=1` (no probes) |
| Harness | `GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200`, FS-UAE A500+, Kickstart 3.1 |
| Window | nine ~200-vblank segments from vbi 200 to 2020 |
| Rows | 1.4 in every segment (`f/vbi` = 0.029 in every segment) |
| Workload | Silverstone, scripted input (`src/platform/autorun.h`) past the front end |

The segments agree to better than ±0.05 FPS, so the run was still doing the work in all of
them — this is not the "unattended run ending" artefact of Rule 3.

⚠ **This SUPERSEDES the ≈2.2 FPS first measured on 2026-08-12**, and the difference is not
noise or a regression: that build had the `$7B00` overlay's three main-loop calls stubbed as
`platform_brk()` no-ops.  With the wing mirrors and the dashboard actually executing, the
frame is ~36% longer.  Rule 4 applies to the 2.2 figure now: **do not quote it.**

**What the number does NOT include, and both directions matter:**

- ⬆ **Nothing is rendered.**  `Revs::render()` increments a counter and returns.  The
  moment the 6502 screen RAM is mirrored into bitplanes (Phase 5) this gets worse.
- ✅ ~~**Three main-loop calls are no-ops.**~~  `$1704`/`$1739`/`$1748` (the `$7Bxx` page) used
  to trap through `platform_brk()` and return.  They now run: the page is built by
  `copy_dash_data` (`$18EA`), replayed into a second listing by `tools/dashdata.py --listing`
  and ingested by `make gen`.  ⭐ **Their cost, measured by the difference, is ~36% of the
  frame** (2.2 → 1.4 FPS), and it is *rasterisation* — the mirrors and the dashboard.  ⭐ The
  re-measured share table below agrees and goes further: `$7BE2` (the dashboard) alone is the
  single largest phase at 34.7%.  `docs/static-map.md` §Open items 6 and 10.
- ✅ **Two 6502 loops in this build were compiled as unbounded mutual recursion** — one of
  them the per-pixel span store inside `project_geometry` (`$1DE5 ⇄ $1DE8`).  **Fixed**
  (`tools/transpile.py build_regions`, `docs/static-map.md` §Open items 9), and
  **re-measured: 2.3-2.5 FPS, i.e. no change.**  Worth writing down because the guess was
  wrong: the defect was a real stack-growth hazard (300-1000 live frames on the overlay), but
  it cost no measurable framerate, so the per-call shares below were NOT distorted by it.
  ⚠ A structural defect is not automatically a performance defect — Rule 1 cuts both ways.
- ⬇ **Nothing is optimised.**  This is pure transliterated C at `-O2`: zero native twins,
  zero asm, and a 6502 `mem[]` byte model throughout.

⚠ Per postmortem §4.1 this figure exists to be *the distance to the target*, not a verdict.
The Atari port's "50 FPS is impossible without an algorithm change" was reached by reasoning
and disproven by hand-asm.  1.4 is a starting line measured on the real machine, which is
exactly what Phase 4 was for.

### Where the time goes — main-loop phase shares  ⭐ re-measured 2026-08-13

`make clean && make PROBES=1 FIXED_RNG=1` + `GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 200`,
brackets generated by the transpiler over the 24 top-level calls of the engine's per-frame body
(`$1701-$1748`, `MAIN_LOOP_BRACKET` in `tools/transpile.py`), plus phase 25 for the display-frame
wait.  **n = 272 game frames**, 767 875 062 loop beam ticks.  **Shares within one run — never
diff these across builds (Rule 2).**

⭐ **The accounting cross-check, which is the first thing to read:** the bracketed loop total is
**97.4% of elapsed** and phase 0 (a one-off boot cost) is the remaining 2.6%.  And
767 875 062 / 272 frames = 706 ms/frame = **1.42 FPS**, which reproduces the independently
measured FPS baseline to two significant figures.  A share table that does not pass both of
those is fiction — see the ⚰ note below for what that looked like.

| Share | ms/frame | Phase | Callee | What it is |
|---|---|---|---|---|
| **36.1%** | 254 | 24 | **`$7BE2`** | the **dashboard**, in the `$7B00` overlay.  Also contains the end of the loop body ($174B-$1763), but **no longer the frame wait** — that is phase 25 now |
| **21.1%** | 148 | 11 | **`$1A20`** | the **road rasteriser** — calls `$193E` (the SMC'd `STA $0400,Y` screen-row store loop) and `$19AF` → `interp_edge` → the span plotters |
| **19.0%** | 133 | 5 | **`build_road_edge_lists`** (`$24F6`) | the **road-geometry projection pass** — builds the two 40-point edge lists (`edge_x_lo/hi`, `edge_y`) that phase 11 then rasterises.  Named 2026-08-13; see below |
| 8.0% | 56 | 18 | `$1E15` | `FUN_1DEF`×2 — 3D geometry |
| 6.6% | 46 | 4 | `$46A1` | 16-bit math — the physics core |
| **2.5%** | 18 | **25** | *(the frame wait)* | `platform_render_frame()` — the paint (a no-op today) plus the spin on the next real vblank.  **Port overhead, not engine work.** ~1 display frame per game frame, as expected |
| 2.3% | 16 | 17 | `$2637` | opponent cars |
| 1.3% | 9 | 15 | `$2AD1` | |
| 1.2% | 8 | 14 | `$4CA4` | |
| ≤0.2% | ≤2 | 1,2,3,6-10,12,13,16,19-23 | | includes both other overlay calls: `$7B4A` 0.0%, `$7B00` (mirrors) 0.0% |

⚠ Row-to-row jitter between runs of the SAME binary is real and is Rule 2 in miniature: a 90 s
run of this identical build read phase 24 at 32.9%, phase 11 at 27.4%, phase 5 at 18.6% and
phase 25 at 1.5%.  Treat the ordering and the rough magnitudes as the finding; do not treat a
2-5pp difference as one.

⭐ **The road pipeline is 40% of the frame in two halves — build (phase 5) then draw (phase 11).**
`build_road_edge_lists` is the producer of exactly the arrays `interp_edge` consumes, so these two
rows are one subsystem, and any change to the road's geometry representation moves both.  That is
the most useful thing the naming pass revealed: the #3 cost was not an independent third target.

**Phase 25 exists because the wait used to hide inside phase 24** — the largest row, i.e. the one
someone is about to optimise.  The engine's *own* frame wait at `$1760` already re-opened phase 0,
but `$1753` branches past it whenever `$62F6` is zero, and it turns out to be **never entered**
(`g_phaseCount[0] == 0` over 272 frames), so phase 0 is purely boot: 18.2M ticks, near-identical
across a 90 s and a 200 s run.  The port's wait is now `PROBE_PHASE_FRAMEWAIT` (`src/platform/probe.h`,
injected alongside `platform_render_frame()` by `PRE_INSN_HOOKS`).  It counts toward the loop total
because it is genuinely part of the loop's wall time; it is simply not something to optimise.

🛑 **THIS REVERSES THE PHASE 4 HEADLINE.  The hot path is RASTERISATION, not physics.**
The top three are **76.2%** between them, and all three are the picture: dashboard, road
rasteriser, road geometry.  `$46A1`, recorded for a phase as the 24.6% leader and the evidence
for "physics and geometry, not rasterisation", is **6.6%** — and the two functions ranked 3rd and
4th on the old table are **0.0%** (`$1B12`) and **1.2%** (`$4CA4`).

⚠ Consequences, because this was load-bearing:
- `docs/phases.md` Phase 6 was premised on the physics hot path.  That premise is gone.
- The Atari port's terrain rasteriser *was* the target there, so the "headline difference from
  the Atari port" was never a difference.  The inherited optimisation experience applies more
  directly than this project has been assuming.
- The single biggest item is the dashboard (`$7BE2`), which the port could not even execute
  until 2026-08-13.

Read with these caveats:
- A bracket **includes nested callees**, so each row is a subtree cost, not an own cost.
- **n = 272 frames**, one workload (Silverstone, `autorun.h`), one trajectory.
- This is a PROBES build.  **No framerate may be quoted from it** — that 1.40 FPS is a
  cross-check of the *instrument*, not a measurement of the shipping build.

### ⚰ What the old table looked like, and why it is kept

Until 2026-08-13 this section reported `$46A1` 24.6%, `$1E15` 13.1%, `$1B12` 9.9%, `$4CA4` 9.8%
— "the four leaders are 57.4% between them, and they are physics and geometry, not
rasterisation".  **Every one of those numbers was wrong**, and the cause is the most
instructive thing in this document.

`probe_phase()` timed with `beamTick()` = `line * 256 + hpos`, which **wraps once per DISPLAY
frame** (313 × 256 = 80 128 ticks, 20 ms), and handled the wrap by `if (d >= 0)` — *discarding*
negative deltas.  The comment justified that as "one bracket per frame out of hundreds", which
holds only while a bracket is much shorter than a display frame.  At 1.4 FPS one game-loop
iteration spans ~37 display frames and individual phases routinely exceed 20 ms, so a phase
straddling a wrap was dropped **whole** and one straddling several lost multiples of 80 128.

| Run | Bracketed/frame | Actually elapsed | Captured |
|---|---|---|---|
| `DASHCODE=0`, 2.2 FPS, n=29 | 103 750 | 1 821 091 | **5.7%** |
| `DASHCODE=1`, 1.4 FPS, n=20 | 104 037 | 2 688 859 | **3.9%** |
| after the fix, n=268 | 2 869 286 | 2 938 724 | **97.6%** |

⭐ The tell was that **both broken runs bracketed ~104 000 ticks per frame regardless of how
long the frame actually was** — the sub-display-frame remainder that happened to survive.  The
longest phases lost the most, which is exactly backwards from what a profile is for: `$24F6`
and `$1A20`, now #2 and #3 at 46% combined, previously read **exactly 0** and were dismissed in
a footnote.

**The fix** (`src/platform/probe.cpp`, `PROBE_VBI()` in the VERTB ISR): a `g_beamEpoch` global
that the ISR advances by exactly one frame's worth, so the tick is monotonic across frames.  An
*addition*, never `frames * 80128` — a 32-bit multiply emits `__mulsi3` and the 68000 has none.
Two hazards handled in `beamTick()`: re-read the epoch and retry if the ISR landed between the
two reads, and clamp to the last value returned, because the beam wraps a few microseconds
before the ISR bumps the epoch.

**The lesson, and it is not "the counter was too narrow".**  This document already said, of the
one phase that read zero: *"either it is genuinely trivial or its bracket is losing deltas to
the frame wrap; do not treat 0 as measured."*  That hedge was correct, was written by someone
who had seen the real cause, and was filed as a bullet point **underneath a headline it
invalidated**.  A measurement you have explicitly flagged as possibly broken cannot also be the
evidence for your main conclusion.  **When an instrument has a known failure mode, test for it
before quoting it** — here that was one division: bracketed total vs elapsed time.  It is now
the first line `phase4_prof.gdb` prints.


## Rule 1 — the ONLY way to quote a framerate

```
cd amiga && make clean && make -j4 FPSCOUNT=1 FIXED_RNG=1
. ./env.sh && GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200
```

`FPS = 50 * g_fpsFrames / g_vbiCount` — painted frames per **emulated** vblank, so host speed
and the gdb stub's own slowness cancel out completely.

⚠ Both counters must be listed in `PROBE_SYMS` (`amiga/Makefile`), or `--gc-sections` drops the
unreferenced one and gdb prints **instruction bytes** in its place — a fake measurement rather
than an obvious zero.  `make probe-audit` enforces this on every link.

- `FPSCOUNT=1` adds *only* the headless auto-run and one increment per painted frame.
- ⚠ **Never quote a framerate from a `PROBES` build.**  On the Atari port a lean probe build
  read ~35% slower, because the timing brackets are two chip-register reads plus a 16×16
  multiply several times per iteration.  The probes are what the honest build exists to measure
  against.
- ⚠ **This harness OVER-reads a win.**  Two changes whose differentials predicted +1.9%
  measured +5.9% end to end.  Single-window noise is ~±2%.  **Under ~3% is agreement, not
  evidence.**
- ⚠ Sample in SHORT segments and discard rows where the run stopped doing the work being
  measured.  A wide window straddling a crash under-reports badly (a 3000-vblank window read
  9.0 where its live segments read 12–17).

**So: quote a static cycle count or a differential ratio as the win, and an FPS row only as the
standing baseline.**

## Rule 2 — price an asm twin with an IN-PROCESS differential, never cross-run

```
make VERIFY=1 PROBES=1     # + amiga/<name>_verify.gdb
```

The asm and the C oracle run back-to-back on the **same inputs in one run**, byte-compared and
beam-ticks tallied per implementation.

Cross-run comparison (build A vs build B) is **not valid by default**.  The reason is
structural: the 50 Hz interrupt is asynchronous to the free-running main loop, so any change in
render speed shifts the phase between them — and that shift changes what the simulation
actually does.  On the Atari port it changed the RNG read count and therefore **which level got
flown**: two builds generated entirely different terrain and different object counts.  A number
measured on a different workload is not a comparison.

- **`make FIXED_RNG=1` for every perf run.**  It pins the simulation so builds are comparable.
  OFF by default — it removes real variety, so never judge *rendering or gameplay* from a
  FIXED_RNG build.
- ⚠ **The differential's metric is the asm/C RATIO, not absolute ticks/call.**  The same binary
  swings ~15% run-to-run on absolutes while the ratio holds to ~0.5%.
- ⚠ **Run any baseline ≥2× after a rebuild** before believing a delta.  An n=1 baseline once
  produced a bogus "4% regression" verdict.
- ⚠ A bracket **includes nested callees**, so it is not that function's own cost.  For own-cost
  attribution you need a PC profile.

### ⚠⚠ Per-iteration ("t/it") numbers are NOT a safer alternative
This is the trap that survived longest on the Atari port: phase-bracket t/it rows were treated
as exempt from the trajectory confound because `FIXED_RNG` pinned the level.  They are not.
`FIXED_RNG` pins the *level*, not the *path flown* — so each build still drives its own ground,
and a phase's cost is partly a property of the view.  It was proven by moving the auto-run's
start 120 frames later, with **no code change at all**: one phase moved by +111 t/it, the entire
size of a "regression" that had been filed as the top performance item.

**Cross-build t/it deltas carry ~±10% of trajectory noise.  Use phase brackets for SHARES
(where does the time go, within one run) and FPS for PROGRESS.  A ledger row is not a baseline.**

## Rule 3 — every old number is wrong; re-measure

Any framerate or cost figure in an older note or commit was measured on a different build, a
different probe set, or a different workload.  **Re-measure, don't quote.**  That includes
the 1.4 FPS baseline above the moment anything is drawn — and it already superseded a 2.2 FPS
baseline measured the same day, once three stubbed main-loop calls started running.  Two
specific traps:

- **Instrumentation** (Rule 1) — probe builds are much slower than shipping ones, and a PC
  profile taken on a probe build inflates exactly the buckets that contain the brackets.
- **The unattended run ending** — a headless run drives no input and eventually stops doing the
  work being measured while the vblank counter keeps ticking.

## Rule 4 — shape-probe the algorithm before optimising it

The win that broke a "structural, faithfulness-bound" ceiling on the Atari port (−36%) did not
come from PC sampling.  It came from **measuring the distribution of the algorithm's own
inputs** with dedicated shape counters, finding that two cases covered 47.7% of all calls, and
straight-lining those.  Then: prove the algebra on the HOST over millions of randomised cases,
*then* write the asm, *then* run the on-target differential.

Corollaries worth keeping:
- A "check before drawing" scheme usually re-reads the very byte the check was meant to avoid,
  so it can only recover the bookkeeping around that load — the reject path is already at the
  floor.
- After special-casing a recursion's leaves, **re-price their parents**.
- Ask whether a "serial" accumulator really has to be serial.

## Rule 5 — interrupt work is capped at one frame

Work in the vblank ISR is capped at **one frame**.  Over that, a displayed frame is silently
dropped — and the dropped frame (a stall, a 2× animation jump, a copper write landing behind the
beam) is what the player reports, not the cost.  Bracket any ISR-side work with VPOSR/VHPOSR
beam-line reads *before* theorising.  A 50 Hz ISR is also a **fixed tax on all wall clock**
regardless of frame rate, which makes its per-firing cost one of the few legitimately
comparable cross-build numbers.

## Rule 6 — suspect a beam-timing race? Re-run on a FASTER CPU

`AMIGA_MODEL=` / `EXTRA_ARGS=` on `run.sh` and `diag_run.sh`.  On the Atari port
`EXTRA_ARGS=--cpu=68040` turned a 0-hit probe into 31-of-32 hits: the slow A500 happened to
land safely, and only a faster CPU moved the violation into the danger window.  Also: quote the
**duration**, not the hit count — a beam-overlap counter read 29 and 46 on two runs of one
binary.

## Reporting

Surface numbers honestly.  Say which build produced them, which harness, and the window size.
If a change measures at zero, that is a result — record it as closed *on data*, and do not
re-open it on optimism.
