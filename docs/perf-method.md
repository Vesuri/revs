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

## ⭐ MOVING-CAR BASELINE — 1.03 FPS (2026-08-14, after the game body left the VERTB ISR)

⚠ **A different scene from every number below**, and not comparable to them: this one holds the
throttle (`STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1`), so the car is *driving*, where every figure
in the table below was measured with the car standing still. Quote it only against itself.

| Build (same scene, same instrument, `GDBSCRIPT=fps_series.gdb ./diag_run.sh 220`) | FPS |
|---|---|
| body in the VERTB ISR (`BODY_IN_ISR=1`) | 0.96 (206 painted / 10786 vblanks) |
| body drained at the engine's frame wait (default, and correct — `docs/amiga-arch.md`) | **1.03** (223 / 10837) |

+7.7%, which is only just outside the ~3% noise floor, so the honest claim is **"no regression, and
possibly a small win"** — not a speed-up to bank. The reason to make the change was correctness (the
horizon artefact); this table exists so nobody later reads the architecture change as a perf cost.

## ⭐ THE BASELINE — 1.46 FPS unrendered, 0.78 FPS RENDERED (re-measured 2026-08-13)

**The port draws now** (Phase 5, `RevsScreen`), and the honest pair of numbers, taken with the
same instrument on the same day, is:

| Build | FPS | frame | what it is |
|---|---|---|---|
| pre-Phase-5 (`Revs::render` counts and returns) | **1.46** | 685 ms | the old baseline, reproduced |
| + copper bands + 2-bitplane display DMA + double buffer (`NODECODE=1`) | **0.97** | 1031 ms | −35%, and it is DMA/contention, not code |
| + the frame-buffer decode (shipping Phase 5 build) | **0.78** | 1282 ms | −19% more: the decode itself, ~250 ms |

⚠⚠ **THE ~250 ms IN THE THIRD ROW IS WRONG — the decode is 83 ms, measured directly 2026-08-16**
(phase 27, its own bracket; see §Where the time goes).  These three rows are three separate runs, so
the difference between them is a sizing that also swallowed whatever else changed between builds, and
the thing it swallowed turned out to be the 50 Hz body's 51%.  Keep the rows as the record of the
DMA cost; take the decode figure from the bracket.

⭐⭐ **THE DECODE IS PORT OVERHEAD, AND DELETING IT IS A PHASE 6 ITEM IN ITS OWN RIGHT.**  Those
83 ms buy nothing the BBC did: they exist only because the engine plots into a BBC-shaped buffer
in `mem[]` and the display wants bitplanes.  Rendering direct to bitplanes removes the pass entirely
and roughly halves the render path's memory traffic — sizing, layout choices, the constraint that
5.5 KB of live engine code renders as the sky, and how the *existing* decode becomes the validated
oracle for a plotter that no longer writes `mem[]`: **`docs/direct-bitplane-plan.md`**.  ⚑ The
predecessor project shipped it and measured ~339 → ~172 ticks/frame for the stage it replaced.

So **rendering roughly halves the framerate**, and only about a third of that is the decode
loop.  The rest is what turning display DMA on costs a CPU whose program and `mem[]` are in
chip RAM — on a stock A500 (512 KB chip, no fast RAM) that is unavoidable, so treat it as the
new floor of the rendered configuration rather than something to optimise away.  ⚠ The two
rows are separate runs, so per docs' own rule they are a sizing, not a differential: the decode
is worth ~250 ms, ±a trajectory.

Goal 50, floor 25.  Rendered, the port is **~32× short of the floor**.

### ⭐ FIRST PHASE 6 WIN: the flat-band skip, +4.1% (2026-08-16)

`decode()` now skips every display line inside a palette band whose four colour registers hold the
same colour — the BBC's band 1, **64 lines (18..81)** of flat blue over the 5.5 KB of live engine
code that lives inside the frame buffer.  31% of the pass, and nothing in it is observable.

| build | vblanks | painted | frame |
|---|---|---|---|
| `FLATSKIP=0` (decode all 208 lines) | 11778 | **218** | 1081 ms |
| shipping (skip the flat band)       | 11778 | **227** | 1038 ms |

**+4.1%, ~43 ms.**  Same flags (`STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1`), same instrument
(`fps_series.gdb`), and the two runs happen to cover the *identical* vblank count, which is what
makes a 4% difference quotable at all when the standing noise floor is ~3%.

⚠ **`docs/direct-bitplane-plan.md` predicted ~75 ms and got 43** — worth carrying, because it prices
the rest of the plan.  Either the whole decode is nearer ~140 ms than the ~250 ms the NODECODE row
above implies (two separate runs, a sizing not a differential), or the skipped rows still pay their
loop and `m_lineMode` test.  Don't re-quote 250 ms as measured; the 43 ms is.

Verified rather than assumed (`amiga/flatskip.gdb`): `g_decodeFlatLines` = **64** in exactly **1**
band on the target, and the dumped picture is a single colour on every one of lines 18..81 with a
positive control (lines 100-119, 20/20 non-uniform) proving the check can fail.

⚠⚠ **AND EVERY EARLIER FRAMERATE IN THIS PROJECT WAS MEASURED WITH A BIASED INSTRUMENT** — see
§How to quote a framerate below.  `fps_seg.gdb` halts the machine at every call to
`Revs::render` to evaluate a breakpoint condition; on the same binary it read 1.4 where a free
run read 0.43, and on the rendered build it read **0.02 where the truth was 0.66-0.78** — a 30×
error in the direction that looks like a catastrophic regression, which is exactly what it was
first taken for.  The replacement (`amiga/fps_series.gdb` + the in-program sampler in
`PlatformAmiga.cpp`) puts NO gdb stop inside the window: the ISR samples `g_fpsFrames` every
512 vblanks and one stop after the run prints the series.  The 1.46 above is that instrument
agreeing with the old 1.4 for the unrendered build — the old number was right, the instrument
that produced it was not trustworthy, and the difference only showed up on a slower build.

### ✅ CLOSED: the "intermittent stall" was a two-level RTS (2026-08-14)

For weeks this doc said: *an intermittent stall affects EVERY build, `g_fpsFrames` stops
advancing part-way, unexplained, discard frozen rows.*  It was neither intermittent nor
unexplained — it was a **hang**, and a deterministic one:

`$2F7E` does `TSX / INX / INX / TXS` and then `RTS`.  It throws away its own caller's return
address, so the RTS returns **two levels up**.  That is how all four unrolled road-span chains
($2D17, $2D9A, $2E20, $2E99) exit: each repeatedly `JSR road_span_plot[_2]`, and when the
plotter finds `Y == $82` (the last column) it branches to $2F7E, drops the plotter's frame and
returns straight out of the chain.  The transliteration modelled `TXS` as `cpu.S = cpu.X` —
faithful to the register and completely inert on the C call stack — so the plotter returned
normally and the chain kept looping.  Its inner loop is `plot / ADC $83 / BCC`, and at
`Y == $82` the slope byte `$83` reads 0, so the carry never sets.  Infinite loop.

Caught state, `STRAIGHT_TO_RACE=1 FPSCOUNT=1`, A500+: pc parked at `revs_gen.c` inside
`FUN_2e99` (the `ADC mem[0x83]` at $2ECF), `cpu.Y == mem[$82] == $36`, `mem[$83] == 0`, and
`cpu.S` walking +2 per iteration — the drop happening over and over.  `g_smcUnhandled == 0`, so
no SMC dispatch was involved.  Before: frozen from vbi ~5600 for the remaining 4200 vblanks.
After: 12783 vblanks with no gap, and the same configuration reads **0.87 FPS against 0.78** —
the chain had been over-plotting past its own exit.

⭐ **The lesson, which is bigger than this bug: a 6502 idiom that manipulates the STACK POINTER
has no C equivalent, and the transliteration drops it silently.**  No unhandled-SMC counter
fires, nothing crashes, the control flow is just wrong.  Suspect that class FIRST for any hang
whose pc parks inside a generated function.  The fix is an unwind flag placed by the transpiler
(`STACK_DROP_TXS` / `UNWIND_CALLEES` in `tools/transpile.py`, `UNWIND_SET` / `UNWIND_TAKEN` in
`src/cpu/cpu.h`), and `report_stack_drops()` fails `make gen` if the image contains any *other*
`TSX/INX/INX/TXS` run — so the next one is found at generation time, not by a hang.

⚠⚠ **AND THE FIX ITSELF CARRIED THE SEQUEL BUG, for a day (2026-08-15).**  The `TXS()` register
write was left in place next to `UNWIND_SET()`, on the reasoning quoted above — "faithful to the
register and completely inert on the C call stack".  Half of that is wrong: the two bytes
`INX/INX` discards are a **return address**, and this model keeps return addresses on the C
stack, so nothing ever cancels the `+2`.  `S` leaked two bytes per road-span exit, climbed past
its `$F8` entry value, **wrapped `$FF` → `$00`**, and pushes then landed on `mem[$0100]` =
`car_order`.  A COMPETITION race hung in `check_car_pair`'s field walk — a frozen race view where
no key responds — while PRACTICE, which skips the multi-car path entirely, kept looking fine.
The transpiler now emits `UNWIND_SET()` **alone** at `$2F81`.

⭐ So the rule has two halves, and this bug is the second: **model the CONTROL FLOW, and leave
`S` alone.**  Modelling neither is a hang; modelling the register too is a silent leak.

⚠ And the diagnostic note, because it cost the first hour: the low-watermark trap reported
"`S` fell to `$00`" **having never reported `$DF`**.  `S` descends one push at a time, so a
watermark that appears without its predecessors was *arrived at*, not descended to — i.e. the
leak was UPWARD.  `g_stackHigh` now sits next to `g_stackLow` (both are in `PROBE_SYMS` and in
`amiga/competition.gdb`; `$F3..$F8` is the healthy window), and `make STACK_TRAP=1` with
`REVS_STACK_TRAP=<hex>` / `REVS_STACK_CEIL=<hex>` prints ONE host backtrace at the first breach
in either direction.  Because the transliteration keeps the 6502 call graph on the C call stack,
that backtrace names the 6502 routines directly — it found `FUN_2f7e` in a single run.

⚠ What survives from the old note: **the series is still printed per segment, and a row out of
line with its neighbours is still discarded.**  A single low segment (0.29 against 0.87 either
side) still shows up occasionally and is host-side emulator jitter, not a freeze — a freeze read
0.00 for every remaining row.

### The pre-Phase-5 baseline, as it was recorded (2026-08-12, Phase 4)

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
  them the per-pixel span store inside `plot_view_src_line` (`$1DE5 ⇄ $1DE8`).  **Fixed**
  (`tools/transpile.py build_regions`, `docs/static-map.md` §Open items 9), and
  **re-measured: 2.3-2.5 FPS, i.e. no change.**  Worth writing down because the guess was
  wrong: the defect was a real stack-growth hazard (300-1000 live frames on the overlay), but
  it cost no measurable framerate, so the per-call shares below were NOT distorted by it.
  ⚠ A structural defect is not automatically a performance defect — Rule 1 cuts both ways.
- ⬇ **Nothing is optimised.**  This is pure transliterated C at `-O2`: zero native twins,
  zero asm, and a 6502 `mem[]` byte model throughout.
- ⬆⬆ **THE CAR IS PARKED, ENGINE OFF, IN NEUTRAL.**  Every figure on this page — the 1.4, the
  0.78/1.46 baseline, and the whole share table below — was measured with the default
  `autorun.h` script, which reaches the circuit and then holds only the throttle.  It never
  started the engine (its `-36` press landed in a window where `$4978` was not asking) and it
  never selected a gear, and `$0063` road speed is **0 in neutral no matter what the throttle
  does**.  So the window contains a stationary car on a static piece of Silverstone: no
  opponents closing, no scenery flowing, and the road rasteriser drawing the least it ever
  will.  **The real workload is heavier than every number here.**
  ⭐ `make STRAIGHT_TO_RACE=1 FPSCOUNT=1` is the moving-car window — engine running (`$0061 =
  $FF`), first gear, throttle held; verified on the target with `amiga/straight_to_race.gdb`
  (`$0063` rising).  ⚠ It is a **different workload**: re-baseline before quoting anything from
  it, and never diff a figure taken with it against one on this page.

⚠ Per postmortem §4.1 this figure exists to be *the distance to the target*, not a verdict.
The Atari port's "50 FPS is impossible without an algorithm change" was reached by reasoning
and disproven by hand-asm.  1.4 is a starting line measured on the real machine, which is
exactly what Phase 4 was for.

### ⭐⭐ Where the time goes — RE-MEASURED 2026-08-16, and the answer changed again

`make clean && make PROBES=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1` +
`GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 200`.  **n = 189 painted frames**, 9779 display fields,
780 137 069 loop beam ticks, **accounted 99.5% of elapsed**.  Shares within one run (Rule 2).

⭐ **The paint call is now FOUR rows, and that is the whole story.**  Phase 25 used to be "the
paint plus the frame wait" and read 2.5%; measured properly it was **60.9%**, so it is split into
26 DRAIN / 27 DECODE / 28 SPIN (`src/platform/probe.h`).  The old phase-25 row is their sum.

| Share | ms/frame | Phase | Callee | What it is |
|---|---|---|---|---|
| **51.1%** | **526** | **26** | **the 50 Hz game body** | `drainTicks()` — the IRQ1V band cycle and `tick_wheel_spin`, run from main-loop context at the engine's frame hook.  ⚠ **NOT one slow routine: ~50 ticks per painted frame.**  See the arithmetic below |
| **14.6%** | 150 | 24 | **`$7BE2`** | the dashboard sweep in the `$7B00` overlay (plus the end of the loop body, `$174B-$1763`) |
| **8.1%** | 83 | **27** | `RevsScreen::decode()` | frame buffer → bitplanes.  Pure port overhead — and **83 ms, not the ~250 ms `direct-bitplane-plan.md` §1 assumed** |
| **7.4%** | 77 | 5 | **`build_track_geometry`** (`$24F6`) | the road-geometry projection pass |
| **6.1%** | 63 | 11 | **`$1A20`** | the road pass ⚠ and it writes **6 bytes** of the frame buffer per call — see the note below |
| 3.3% | 34 | 4 | `$46A1` | 16-bit math, the physics core |
| 3.1% | 32 | 18 | `$1E15` | 3D geometry |
| **2.6%** | 27 | **28** | *(the vblank spin)* | waiting for the next field after the paint |
| 0.9% | 10 | 17 | `$2637` | opponent cars |
| ≤0.5% | ≤5 | 25, and 1,2,3,6-10,12-16,19-23 | | phase 25 itself is now 0.0% |

⭐⭐ **THE 51% ROW IS A RATIO, AND IT IS THE PORT'S REAL CEILING.**  The body runs once per display
FIELD — faithfully, because a BBC's User VIA fires no matter how long the foreground takes — so at
~0.95 painted FPS it runs ~52 times per painted frame.  Divide it out: **one body tick costs ~10 ms
of the 20 ms a tick has.**  That is half the machine gone before a single pixel of the main loop's
own rendering, it is *independent of the framerate*, and no rendering change can touch it.  ⭐ It
also explains a 2026-08-13 mystery: that table charged this work to nothing, because the body still
ran inside the VERTB ISR then (`docs/amiga-arch.md`) where no bracket could see it.

**Inside the 51%, measured (`amiga/phase4_prof.gdb`, same run):** one body tick = **11.1 ms** of its
20 ms, of which the body's own arm `tick_wheel_spin` is only **277 µs** — the other **10.8 ms is
`irq1v_band_schedule` itself**, 5 calls per tick at **1983 µs each**.  The VERTB ISR (copper + present +
audio) is **1011 µs** per field on top, charged to whichever phase it preempted.  An arm is ~60 6502
instructions and ~72 BBC hardware accesses per tick go through `platform_hw_write` (counted with
`make HWTIME=1` — ⚠ whose own observer effect is over 2×, so take the *count* and not its
microseconds).  ⭐ **That makes `irq1v_band_schedule` the obvious first native twin**: 51% of the frame, no
drawing, pure `mem[]` + hardware writes, and its output (the band record) is already validated
against a real BBC by `make mode7` and `make refloop`.

⚠⚠ **So the Phase 6 pecking order is now: the 50 Hz body, then the dashboard, then the road
subsystem (5+11 = 13.5%), then the decode.**  `tick_wheel_spin` — the band-4 arm, which is also the only
thing that draws display lines 120-143 — has never been profiled or split, and it is the single
biggest item in the port.

### ⭐⭐ TWIN #1 SHIPPED: `irq1v_band_schedule` native, **0.96 → 1.56 FPS** (+62%, 2026-08-16)

The first native twin the project has (`src/gen/revs_native.c`, `VALIDATE_FUNCS = {0x4E5C}`).

| build | vblanks | painted | FPS | frame |
|---|---|---|---|---|
| transliterated (the flat-band-skip row above) | 11778 | 227 | **0.96** | 1038 ms |
| `irq1v_band_schedule` native                        | 11824 | **369** | **1.56** | 641 ms |

Same flags (`STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1`), same instrument (`fps_series.gdb`,
22 rows, one 0.97 outlier and 21 rows within 1.56-1.66), nearly identical vblank counts.
**~400 ms of a 1038 ms frame, and the routine is ~80 6502 instructions.**

⭐ **WHERE IT WENT, AND THE GENERAL LESSON.**  Nothing algorithmic changed — the twin makes the
same hardware writes in the same order.  What was paid for was *the transliteration itself*:
48 of those 80 instructions are `STA $FE21` inside three 16-entry palette loops, and each one
cost ~20 68000 instructions of N/Z bookkeeping against a struct in memory, plus a C-bridge call
→ virtual dispatch → 12-case switch to reach a one-line model.  Written as C, gcc **constant-folds
and fully unrolls band 1's whole loop into 16 `move.b #imm,abs.l`** — the sixteen palette entries
are `3,$13..$F3`, a fact the 6502 spells as `ADC #$10 / BCC` and C spells as a compile-time
constant.  ⭐⭐ **So a transliterated hot routine can be dominated by cost that has no counterpart in
the original at all, and the twin's win is not "better code" but "the absence of an interpreter".**
Expect the same shape wherever a tight 6502 loop touches hardware or drives a table.

⚠ Two things this did NOT do, so the next measurement is not mis-set: the 51% row is a RATIO
(the body still runs once per field, faithfully — it now just costs less each time), and
`tick_wheel_spin` is untouched.  Re-profile before picking twin #2; the share table above is stale.

**Re-profiled after twin #1** (same command, n = 280 painted frames, ~700 ms/frame in the PROBES
build).  The body tick fell from 11.1 ms to **5.5 ms** and `irq1v_band_schedule` from 1983 µs to 814 µs
per call, exactly as predicted:

| share | ms/frame | phase | what |
|---|---|---|---|
| **27.4%** | 189 | 26+29 | the 50 Hz body — still the largest row, now by much less |
| **21.8%** | 151 | 24 | **`$7BE2`, the dashboard** — now the biggest *main-loop* item |
| 11.9% | 82 | 27 | the decode (port overhead — `docs/direct-bitplane-plan.md`) |
| 11.3% | 78 | 5 | `build_track_geometry` `$24F6` |
| 8.7% | 60 | 11 | `$1A20`, the road pass |
| 4.8% | 33 | 4 | `$46A1` |
| 4.6% | 32 | 18 | `$1E15` |
| 3.5% | 24 | 28 | the vblank spin |

**Re-profiled again after twin #2** (same command, n = 318 painted frames, ~608 ms/frame in the
PROBES build, `bracketed` 99.0% of elapsed).  Only phase 24 was supposed to move, and only phase 24
did — which is also the check that the twin changed nothing else:

| share | ms/frame | phase | what |
|---|---|---|---|
| **26.2%** | 159 | 26 | the 50 Hz body drain (30.8 ticks per painted frame, 5452 µs each) |
| **21.6%** | **131** | 24 | `$7BE2` the dashboard — **was 151**; see the twin-#2 entry below |
| 13.4% | 81 | 27 | the decode (port overhead) |
| 10.0% | 61 | 5 | `build_track_geometry` `$24F6` |
| 9.6% | 58 | 11 | `$1A20`, the road pass |
| 4.8% | 29 | 18 | `$1E15` |
| 4.6% | 28 | 4 | `$46A1` |
| 3.9% | 23 | 28 | the vblank spin |
| 1.2% | 7 | 15 | `$1B93` |
| 1.1% | 7 | 29 | `$52A4`, the body's arm |

⚠ And two rows that are not phases but bound everything: the **VERTB ISR is 9783 calls at 911 µs**
(charged to whichever phase it preempted, ~28 ms/frame spread pro-rata), and **`irq1v_band_schedule` is
49 134 calls at 810 µs** — 154 per painted frame, i.e. **125 of phase 26's 159 ms**.  The 50 Hz body
is still mostly its own band cycle.

**Re-profiled after the dirty-region decode** (same command, n = 357 painted frames, ~545 ms/frame
in the PROBES build, accounted 99.0%).  Again only the row that was supposed to move, moved:

| share | ms/frame | phase | what |
|---|---|---|---|
| **26.1%** | 141 | 26 | the 50 Hz body drain (27.5 ticks per painted frame, 5439 µs each) |
| **24.2%** | 131 | 24 | `$7BE2` the dashboard — unchanged, and now the biggest row after the body |
| 11.3% | 61 | 5 | `build_track_geometry` `$24F6` |
| 10.8% | 59 | 11 | `$1A20`, the road pass |
| **6.6%** | **36** | 27 | the decode — **was 81 ms**, and this is what the dirty region bought |
| 5.4% | 29 | 18 | `$1E15` |
| 5.2% | 28 | 4 | `$46A1` |
| 3.9% | 21 | 28 | the vblank spin |
| 1.4% | 8 | 15 | `$1B93` |
| 1.1% | 6 | 29 | `$52A4`, the body's arm |

⭐⭐ **AND THIS IS WHERE THE DECODE STOPS BEING WORTH OPTIMISING.**  36 ms of a ~545 ms PROBES frame
is 6.6%; even deleting it outright — which is what a direct-to-bitplane plotter would do — is now
worth less than the noise floor allows anyone to *quote* from an FPS run.  ⚠ Do not read that as
"direct rendering is dead": §7b's other half says the DRAW is in the 50 Hz body, so direct plotting
attaches to phase 26 (141 ms) and the decode was never the prize there.  The three things left
above the decode are, in order: **the body (141 ms), the dashboard (131 ms, a representation problem
per §7a), and `$24F6`+`$1A20` (120 ms, which write six visible bytes between them)**.

⚠ A back-of-envelope on the 36 ms says the clean-cell path is still ~170 cycles a cell where four
longword reads and a branch should be ~40 — running pointers instead of `rowBase + c * 8`, and a
coarse 32-byte pre-test over four cells at a time, would likely halve it.  **Left undone
deliberately:** ~18 ms of a ~1000 ms shipping frame is under 2%, which this project's own rule says
is unquotable, and the same hour spent on the dashboard's scan is worth ten times more.

### ⭐⭐ WHERE THE TIME GOES — CURRENT TABLE, re-measured 2026-08-17 after twins #6/#7/#8

**This is the table to read; the one below it is the previous measurement, kept for the deltas.**

`make clean && make -j4 PROBES=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1` +
`EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 30`.
**vbi=7251, loopFrames=370, brk=0, smc=0, accounted 98.5% of elapsed** (the sanity check — if that
is not ~100% the shares are fiction).  Shares within one run (Rule 2); **no framerate may be quoted
from a PROBES build.**

⭐ **The car was still DRIVING at the interrupt**, which is the precondition for the table meaning
anything (`amiga/dash_state.gdb`, separate run): `vbi=7110 $61=ff(engine) $3C=a1(revs) $63=32(speed)
$40=02(gear)`.  ⭐ And a free cross-check on this commit's naming: `$2D` — `drive_state` — read
**00** in both the settled and the at-the-interrupt samples, which is what "0 = driving normally"
predicts.

| Share | ms/frame | Phase | Callee | Code | vs previous |
|---|---|---|---|---|---|
| 15.6% | **60** | 5 | `build_track_geometry` (`$24F6`) | native (driver) | 61 |
| 15.2% | **58** | 11 | `draw_road` (`$1A20`) | native (driver) | 58 |
| 10.9% | **42** | 34 | `view_paint_lines` painting phase 3 | native (twin #2) | 41 |
| 10.2% | **39** | 27 | `RevsScreen::decode()` | port | 35 |
| 7.7% | **30** | 18 | `fill_dash_edge_columns` (`$1E15`) | **native (driver)** — twin #8 | 29, `xlat` |
| 7.4% | **28** | 4 | `apply_driving_model` (`$46A1`) | **native (driver)** — twin #6 | 28, `xlat` |
| 7.0% | **27** | 26 | the 50 Hz drain (`irq1v_band_schedule`) | native (twin #1) | 26 |
| 6.7% | **26** | 24 | `view_paint_lines` painting phase 1 | native (twin #2) | 25 |
| 4.6% | **18** | 33 | `view_paint_lines` painting phase 2 | native (twin #2) | 17 |
| 3.4% | **13** | 28 | the vblank spin | port | 19 |
| 2.9% | **11** | 32 | `race_main_loop`'s tail | native (twin #3) | 11 |
| 1.3% | **5** | 15 | `draw_track_object` (`$2AD1`) | **native (driver)** — twin #7 | 8, `xlat` |
| 1.2% | **4** | 3 | `read_driving_controls` (`$1579`) | xlat | 4 |
| 1.2% | **4** | 29 | `tick_wheel_spin` (`$52A4`) | xlat | 4 |
| 0.6% | 2 | 14 | `build_road_sign` (`$4CA4`) | xlat | |
| 0.6% | 2 | 7 | `advance_player_section` (`$24B9`) | xlat | |
| 0.4% | 1 | 6 | `place_player_in_section` (`$4626`) | xlat | |
| 0.4% | 1 | 10 | `clear_surface_buffers` (`$66B6`) | xlat | |
| 0.3% | 1 | 13 | `fill_line_surface` (`$18BC`) | xlat | |
| ≤0.1% | 0 | 1,2,8,9,12,16,17,19-23,25 | the rest of the 24-call body | xlat | |

⭐⭐ **THE VIEW PIPELINE IS STILL THE LEVER, AND ITS SHARE IS UNCHANGED AT 54%:** the two producers
`build_track_geometry` + `draw_road` = **118 ms / 30.8%**, the consumer `view_paint_lines`
(brackets 24+33+34) = **86 ms / 22.2%**.  One subsystem over one shared data structure.

⚠ **AND THE THREE ROWS THIS COMMIT TOUCHED DID NOT MOVE — 28, 30 and 5 ms against 28, 29 and 8.**
`fill_dash_edge_columns` and `apply_driving_model` are identical to the millisecond, and
`draw_track_object`'s 8 → 5 is a 3 ms row that per-iteration noise (±10%) cannot separate from a real
win, so it is not claimed as one.  That is the third independent confirmation of
`docs/faithfulness-seam.md` §8: **a driver twin does not collect the row it sits in.**  Nine of the
eleven biggest rows now say `native`, and the frame is still 385 ms.

⚠ Two port rows swapped ~6 ms between them (`decode` 35 → 39, the vblank spin 19 → 13).  They are
two ends of the same handoff — the spin is whatever the frame has left after the decode — so read
their SUM (54 → 52) and not either row alone.

### Where the time goes — the PREVIOUS table (2026-08-17, after the band-record reuse)

⚠ **Superseded by the table above.**  Kept because the `vs previous` column refers to it, and
because its `Code` column is where three rows still read `xlat`.

`make clean && make PROBES=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1` +
`EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 30`.  n = 321-328 painted
frames, ~7100 fields, **accounted 98.5% of elapsed**.  Shares within one run (Rule 2).

### ⭐⭐ THE MEASUREMENT WINDOW ENDED ON A PARKED CAR, AND NOW IT DOES NOT (2026-08-17, user)

A `STRAIGHT_TO_RACE` car drives straight, leaves the circuit after ~225 game frames and is reset to
the pits **with the engine off** — and the autorun's script has run out by then, so a held throttle
could not restart it.  Measured, once `amiga/dash_state.gdb` was given a row at the end of the
window instead of only early samples:

    vbi=2413  settled           $61=ff(engine)  $3C=a0(revs)  $63=31(speed)   <- driving
    vbi=6950  AT THE INTERRUPT  $61=00          $3C=00        $63=00          <- parked, stalled

**~36% of every 30 s run was a static scene.**  `AutoRun`'s steady state is now a three-state
machine on the game's own cells — engine off → the starter, neutral → one upshift, otherwise the
throttle — pressing nothing the script did not, so the key set is unchanged.  At the interrupt the
car now reads `$61=ff $3C=a1 $63=32 $40=02`.

⭐ **And it changed no published number, which is worth knowing rather than assuming:** FPS 2.73 →
2.72 and the share table within a millisecond or two per row (phase 24: 82 → 83).  A parked car
still has the whole viewport repainted every frame, so the *render* workload barely moves; what it
does distort is anything about CONTENT — the redundancy census reads 99% on a parked scene against
63% driving (`docs/direct-bitplane-plan.md` §7g).  ⭐⭐ It also means the **host** had never driven
at all: its engine never caught under the old script, so every host-side workload figure was a
parked car.  With `make HOLD_THROTTLE=1` it now drives, and its census (69% / 38%) independently
agrees with the target's (63% / 33%).

⚠ Read the `AT THE INTERRUPT` row beside every share table.  The car still leaves the track — one
row of a 13-row FPS series is the reset — and only the recovery keeps the rest of the run honest.

⚠⚠ **THIRTY SECONDS, NOT NINETY, AND THAT IS NOT ABOUT PATIENCE.** A `STRAIGHT_TO_RACE` run leaves
the track, gets reset, and then **nothing happens** — so a longer run does not gather more data, it
DILUTES the measurement with a static scene.  The 90 s version of this table read `$7BE2` at 130 ms
and the drain at 20 ms; the 30 s version reads 131 and 30.  A run length past the interesting part
is a silent averaging error, in the same family as the per-band average below.

⭐ The **Code** column is the standing answer to "is this row still an interpreter?" — `native` = a
validated twin in `src/gen/revs_native.c`, `xlat` = generated transliteration in `src/gen/revs_gen.c`
(so its cost still includes the per-instruction flag bookkeeping and the `bus_*` trip), `port` =
port-authored C++ that has no 6502 original at all.  Five addresses are native
(`VALIDATE_FUNCS` + `NATIVE_FUNCS` in `tools/transpile.py`): `irq1v_band_schedule`,
`view_paint_lines`, `race_main_loop`, and — since 2026-08-17 — `build_track_geometry` and
`draw_road`.  ⚠ For the last two the column says `native (driver)`, which is a WEAKER claim than
the other three: the driver is C but everything it calls is still transliterated, so the row's
milliseconds barely move (see §the two producers below).

| Share | ms/frame | Phase | Callee | Code | What it is |
|---|---|---|---|---|---|
| **22.3%** | **84** | **24** | **`view_paint_lines`** (`$7BE2`) | **native** (twin #2) | the 3D VIEW rasteriser (not the dashboard — `docs/rename.md`).  2093 units, ~83 change a byte.  **Was 131**: the two passes below took the unit loop and then the per-line drivers.  ⚠⚠ **11 ms of this row is the main-loop TAIL, not the sweep** — it is brackets 24+32+33+34 now, and the sweep splits 25/17/41 by painting phase (§11 ms of phase 24).  That tail is `race_main_loop`, also native |
| 16.3% | 61 | 5 | `build_track_geometry` (`$24F6`) | native (driver) | the road-geometry projection pass |
| 15.6% | 58 | 11 | `draw_road` (`$1A20`) | native (driver) | writes **6 visible bytes**; its output is per-column data, not pixels |
| 9.5% | 35 | 27 | `RevsScreen::decode()` | port | dirty-region.  Port overhead, and DONE |
| 7.8% | 29 | 18 | `fill_dash_edge_columns` (`$1E15`) | xlat | |
| 7.5% | 28 | 4 | `apply_driving_model` (`$46A1`) | xlat | |
| 7.1% | 26 | 26 | the 50 Hz drain (`irq1v_band_schedule`) | **native** (twin #1) | **was 176** before the record reuse |
| 5.2% | 19 | 28 | the vblank spin | port | port overhead |
| 2.1% | 8 | 15 | `draw_track_object` (`$2AD1`) | xlat | |
| 1.2% | 4 | 3 | | xlat | |
| 1.1% | 4 | 29 | `tick_wheel_spin` (`$52A4`) | xlat | the band cycle's only game work |

⭐⭐ **The two biggest levers left are the view pipeline's two producers: `build_track_geometry` +
`draw_road` = 119 ms / 32%** — and see below for why making them native did not collect it.

### ⚠⚠ TWINS #4 AND #5: the two producers are native, and the FRAMERATE DID NOT MOVE (2026-08-17)

`build_track_geometry` (84 bytes) and `draw_road` (120 bytes) are now real C, `make validate`d at
0 mismatch over 400 and 200 randomised cases, ten sabotages each failing.  **FPS 2.73 -> 2.71**,
i.e. nothing: 356 painted frames over 6563 fields against the baseline's 2.72-2.73.

⭐⭐ **AND THAT IS THE LESSON, NOT A DISAPPOINTMENT: A DRIVER TWIN COLLECTS NOTHING.**  Both routines
are ~100 bytes of argument passing around subtrees of hundreds of transliterated instructions
(`road_edge_start` / `road_edge_walk` / `project_point`, and `fill_line_attr` /
`draw_surface_spans` / `mark_line_surfaces` / `interp_edge` / the `$2C00`-`$2FFF` span plotters).
The interpreter that was deleted was never where the 119 ms was.  Contrast twins #1 and #2, which
paid +62% and +28% — both were LEAF-heavy: the work and the interpreter were in the same routine.

**So the rule for picking the next twin is not "which row is biggest" but "how much of that row is
in the routine itself".**  For phases 5 and 11 the answer is: the span plotters and the projection,
which is where the next twin goes — and the twins just written are what makes that legible, because
the pipeline's shared data structure now has names (`view_src_blocks`, `line_attr_0/1` +
their limits, `road_split_index`, `surface_style_base`/`_alt`) instead of nine bare zero-page
addresses.  That was the actual deliverable; `docs/direct-bitplane-plan.md` §7a is the payoff.

⭐⭐ **THE VIEW PIPELINE IS 203 ms OF A ~376 ms FRAME — 54% — AND IT IS ONE SUBSYSTEM.**
(It was 250 of 430 before the two `$7BE2` passes of 2026-08-17; the *share* barely moved because
what came off `$7BE2` came off the frame too.)
`$24F6` and `$1A20` between them write **95 frame-buffer bytes, all inside the flat-blue sky band**
(measured, `make fbwrites`): they are not drawing, they are **producers**, writing *source bytes*
into the forty `$80`-spaced blocks at `$3000..$4380` indexed by screen column.  `$7BE2` is the single
**consumer** that turns those into screen bytes.  So the three biggest rows in the table are
producer → producer → consumer over one shared data structure, and that structure is the lever.

⚠ **The VERTB ISR is now a row worth naming**: 915 us per call, ~22 calls per painted frame ≈ **20
ms/frame**, charged to whichever phase it preempted so it appears in no row of its own.  It does the
copper rebuild, the bitplane-pointer swap and the audio tick.  Unmeasured internally.

### ⚖ TWINS #16-#24: the road-geometry pass has no interpreter left, and the framerate DID NOT MOVE (2026-08-18)

The other nine routines under `build_track_geometry` — the near-slot bookkeeping, the four per-point
primitives, `point_distance_hypot` and `emit_edge_width_offset`.  With them the whole call tree from
`$24F6` is real C: 19 routines, 776 6502 instructions, no transliteration anywhere in it.  Two of
the nine genuinely compress (nine `LSR hi / ROR A` pairs → three 68000 word shifts; a variable
shift the 6502 runs as a loop of up to 255 iterations → one `lsl.w` and a range test).

Two 30 s warp runs, `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` + `fps_series.gdb`, both from a
`make clean`, both in the same session, compared as ROW VECTORS:

| build | rows | 11-row mean |
|---|---|---|
| control (HEAD~1, the same pass with nine transliterated leaves) | ten 2.92, one 2.83, one outlier 1.46 | **2.9118** |
| twins #16-#24 | ten 2.92, one 2.83, one outlier 1.56 | **2.9118** |

**Identical, row for row.**  Not "within noise" — the same integer painted-frame count in eleven of
twelve rows, which is as tight as this instrument reads (one painted frame is 3.3% of a row).

⇒ **What that says, and it is the useful part.**  The nine leaves together are a real share of the
pass's instruction count, and removing their interpreter bought nothing measurable — so the road
pass's cost is NOT the interpreter any more.  Twins #1 and #2 paid because the transliteration was
doing bookkeeping around work C does differently; by twin #24 that surplus is spent.  What is left
in this subtree is the algorithm itself plus `mem[]` traffic: every edge point still walks the
section arrays, the three `point_delta` arrays, the two edge arrays and `edge_style` a byte at a
time through a 64 KB `unsigned char[]`, and no amount of further twinning changes the number of
those accesses.  **The next win in this pass has to REMOVE ACCESSES OR POINTS, not instructions** —
which is the same conclusion `docs/direct-bitplane-plan.md` reaches for the consumer end, one
subsystem earlier.

⚠ Do not read this as "the twins were not worth writing".  They are what makes the above
measurable at all, and §8's non-performance reasons applied to seven of the nine before a run
was made.  Quote them for the names and the leaf-freedom, never for FPS.

### ⚠⚠ TWINS #14/#15: an ALL-ARITHMETIC LEAF TWIN THAT MADE IT SLOWER, and the one-line fix (2026-08-18)

`bearing_to_section_from` (`$2147`) and `project_point_from` (`$2287`) are the road pass's two
coordinate transforms, `div16by8`'s own callers, and the first twins since #2 with **no
transliterated subtree underneath them at all**.  By §8's rule that is the shape that should pay.
Four 30 s warp runs of `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` + `fps_series.gdb`, every one from
a `make clean`, all in one session:

| build | 11-row mean (painted per 512 vblanks) | vs the transliteration |
|---|---|---|
| the transliteration these replaced | 29.91 | — |
| **twins #14/#15 as first written** | **27.20** | **-9.1%** |
| twins #14/#15 + `always_inline` on the flag helpers | 29.18 | -2.4% |
| twins #14/#15 + flag-free subtracts (below) | **29.91** | **+0.0%** |

⭐⭐ **AND THE INSTRUMENT IS BETTER THAN THIS FILE THOUGHT.  `fps_series` under `FIXED_RNG=1` +
warp is DETERMINISTIC ROW FOR ROW** — two runs of the same build return identical vectors, checked
on both builds.  So the "one painted frame is 3.3% of a row, therefore 3% is noise" rule was
describing the wrong thing: the limit is not run-to-run VARIANCE, it is the RESOLUTION of a single
row.  Two consequences, both of which make small changes measurable:

* **Compare ROW VECTORS, never the `total painted` line.**  The total spans a partial trailing row
  and the run length in vblanks varies (6813 vs 7147 on two runs of one build), so totals differ by
  3% for two runs of identical code.  That is what first made this section quote 383 / 371 / 349.
* **Average the non-outlier rows.**  Eleven rows at one-frame resolution give ~0.3%, which is how
  the +2.5% below is quotable at all.  Drop the row where the car leaves the track — it is the one
  row that is a different scene, and it is obvious (14-20 against 28-31).

⭐⭐ **THE TWIN WAS SLOWER THAN THE TRANSLITERATION IT REPLACED — 349 against 383, a 9% LOSS, far
outside the one-frame noise floor.**  The objdump said why in one grep: `jsr <sub_from>`.  The
flag-carrying helpers (`sub_from` / `sbc_step` / `adc_step` / `cmp_ge` / `load_a`) are a few
instructions each, but GCC left them **out of line at -O3** because they write the global `cpu` and
have many callers — so every subtract in a routine that is *nothing but* subtracts paid a `jsr` plus
a `movem.l d2-d7,-(sp)`/restore pair.  The transliteration expands the same `cpu.h` macro INLINE at
every site and therefore never pays it.  `static inline __attribute__((always_inline))` (now
`REVS_FLAG_OP`) took the 16 call sites in the binary to 0 and recovered 349 → 371.

⭐⭐ **AND THE HONEST RESULT AFTER THE FIX IS STILL A SMALL LOSS: 371 against 383/388.**  ~3%, i.e. at
the floor, but on the wrong side of it in every row.  So §8's rule needed narrowing, and this is the
narrowing — **the win is ALGORITHMIC COMPRESSION, not "being real C".**  Twin #13 paid nothing but at
least had a compression available (eight unrolled byte-pair shifts → one 16-bit word).  These two
have none: every 6502 instruction maps to exactly one C operation, **and all four exit flags are
live**, so the twin must compute the same N/V/Z/C the interpreter computed.  There is no interpreter
overhead left to remove once the flags are part of the contract.
⇒ Before twinning an arithmetic leaf, ask **what the C version does in FEWER operations than the
6502 did.**  If the answer is "nothing, it just looks nicer", expect parity and write the twin for
the names (§8) — which is what these two are kept for.

### ⭐⭐ …and where the FEWER OPERATIONS actually were: FLAGS NOBODY READS

The answer to that question turned out not to be the arithmetic at all.  `cpu.h`'s `SBC` writes
**cpu.A, N, V, Z and C** — five `move.b dn,abs.l` stores at ~16-20 cycles each on a 68000, per the
calibration in `docs/headless-fsuae.md` — and computes V through a chain of masks.  A restoring
divide reads none of that: only the VALUE feeds the next step, and **only the last subtract's V ever
leaves the routine.**  `div16by8` was paying all five, seven times a call.

⚠ First the routine had to be SIZED, and the parked baseline would have said don't bother:

| workload | `div16by8` | `bearing_to_section_from` | `project_point_from` |
|---|---|---|---|
| parked (`STRAIGHT_TO_RACE=1`) | 7.8 /frame | 4.0 | 3.8 |
| **driving (`+ HOLD_THROTTLE=1`)** | **60.5 /frame** | **30.8** | **29.8** |

**Eightfold, because `road_edge_start` reuses last frame's edge points and a parked car re-derives
almost nothing.**  Stable to 1% over 300 and 1200 frames.  Any future sizing of the road pass must
be taken while DRIVING — and note the FPS runs already are (`FPSCOUNT=1` holds the throttle), so a
parked call count and a driving framerate do not describe the same workload.

The fix is `sbc_value` / `sbc_overflow` in `src/gen/revs_native.c`: the same subtract with the
bookkeeping removed, decimal mode included (D changes the RESULT BYTE, so this is not plain C
arithmetic), plus a one-shot replay of the single V that escapes.  Applied to `div16by8_core`'s seven
restoring subtracts, `view_delta`'s four, and `bearing_arm`'s negate.  **+2.5%, and it closes the
gap to the transliteration exactly.**
⭐ Where a flag is live is worth working out rather than assuming: `view_delta`'s V is observable
**only** through `project_point`'s clip exit, because `bearing_to_section`'s 45-degree arm overwrites
V with `BIT` and its octant arms with the closing `ADC`.  Three sabotages "survived" until they were
aimed at the right fixture — a sabotage pointed at a fixture that cannot see the flag it breaks
proves nothing.
⚠ Two procedural notes.  `always_inline` is worth ~1% to the twins that were already here (383 →
388) and ~6% to these, so it is a fix for *arithmetic-dense* twins specifically, not a corpus-wide
win — attribute it that way.  And the first measurement of these twins was taken from a `PROBES=1`
build and read 2.53; probe brackets are not free, so **match the build to the control** before
comparing anything.

### ✅ TWIN #13 `div16by8` — the pipeline's first LEAF, framerate UNCHANGED, and the CONTROL moved the baseline (2026-08-17)

`$0C47 div16by8` (94 bytes) is real C: `make validate` 0 mismatch over 4000 randomised cases in four
steered domains (0 still at `REVS_VALIDATE_CASES=4`, 16 000 cases), **8 sabotages injected and 8
detected**, `make determinism` and `make determinism-drive` both 64K byte-identical against
references recorded from the previous commit, `make tracks` 6/6, `make track-run` every circuit's
hooks running, `muldiv-audit` and `probe-audit` clean.

It is the FIRST twin in the view pipeline that is not a driver — no callees at all, every byte of it
arithmetic, and it is the whole of `project_point`'s and `bearing_to_section`'s callee set (between
them those two routines call exactly one function).  §8 of `docs/faithfulness-seam.md` says that is
the shape that should pay.  **It did not.**

⭐⭐ **AND THE REASON THE NULL RESULT IS TRUSTWORTHY IS THE CONTROL, WHICH IS ALSO THE FINDING.**
Two 30 s warp runs of `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` + `fps_series.gdb`, both from a
`make clean`, differing only in this commit:

| build | modal row | rows at it | the outlier |
|---|---|---|---|
| with twin #13 | **2.92** | 10 of 12 | 1.66 — the run leaving the track |
| HEAD, control | **2.92** | 9 of 12 | 1.56 — same |

So no win, and the number to carry forward is 2.92.  ⚠⚠ **The control also contradicts the 2.73-2.83
this file recorded for that very commit one entry below** — same procedure, same script, same warp
setting, nothing changed but the run.  Nobody's build was stale; the earlier figure was simply
another sample.  That is rule 3 biting on a number this file wrote itself: **at ~30 painted frames
per 512-vblank row a single frame is 3.3%, so the whole "under 3% is noise" floor is one frame of
quantisation.**  Do not compare a new measurement against a recorded one — re-run the control, in
the same session, from a clean build.  (The standing baseline in `CLAUDE.md` is updated to 2.92 on
this basis, and it will move again for the same reason.)

⚠ Where the estimate went wrong, because it is a reusable error: the pre-work guess was ~160 calls a
frame at ~1400 interpreted cycles each ≈ 23 ms, i.e. ~6%.  The differential says under a frame.
Either the call count is far lower than "three sites × the edge points" suggests, or the
transliteration of 56 straight-line zero-page instructions is much cheaper than the ~25 cycles each
the older twins were priced at.  **Neither was measured, and an unmeasured call count is not a
budget** — shape-probe the leaf (rule 4) before predicting a leaf twin's win.

### ✅ TWINS #9-#12: build_track_geometry's OWN CALLEES, and the framerate is UNCHANGED (2026-08-17)

`road_edge_start` (188 bytes), `road_edge_walk` (231), `road_edge_side` (27) and `abs8` (8) are real
C, `make validate`d at 0 mismatch over 200/200/400/800 randomised cases (still 0 at
`REVS_VALIDATE_CASES=4`), **26 sabotages injected and 26 detected**, `make determinism` and
`make determinism-drive` both 64K byte-identical against references recorded from the previous
commit, `make tracks` 6/6 byte-exact and `make track-run` every circuit's hooks running.

**FPS: unchanged.** A 30 s warp run of `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` +
`fps_series.gdb` read **2.73-2.83 in twelve of its thirteen rows** (the thirteenth, 1.36, is the run
leaving the track — rule 1's warning about long windows, visible in a single row here), against the
standing 2.71 baseline.  That is inside the ±3% noise floor, so **no win is claimed**.

⚠ **These are NOT ~100-byte drivers, and the null result is the more interesting for it.**  Unlike
twins #4-#8 the two big ones carry real arithmetic — the near-slot bookkeeping and the horizon
maximum, the running nearest and the three-midpoint interpolation.  It still collected nothing,
and the reason is the SHAPE of the call tree rather than the size of the routine: per road side the
walk runs its own ~40 instructions once and then calls `bearing_to_section`, `project_point`,
`emit_edge_bearing` and `emit_edge_width_offset` **up to 18 times each**, all still transliterated.
So §8's rule wants sharpening: *how much of the row is in the routine* is not a byte count, it is
**instructions executed in the routine versus in its callees per invocation** — and a short loop
around four transliterated calls is a driver no matter how long its body reads.

⭐ What these four bought is the naming the representation change needs, and it corrected a fact:
**the edge arrays hold ANGLES, not screen columns** (`$2145` is an arctan, `$23C0` stores
`bearing - car_heading`, `$0BA2` re-bases by the same delta the heading integrates).  That makes
`player_pos_lo`/`_hi` a wrong name rather than a narrow one — they are `car_heading_lo`/`_hi` now — and it is
the first thing `docs/direct-bitplane-plan.md` §7a has to know about these buffers.
⭐ The lever is unchanged and is now unambiguous: **the view pipeline's LEAVES** — the `$2C00`-`$2FFF`
span plotters, `project_point` and `bearing_to_section`, which are what the 18-per-side calls land in.

### ✅ TWINS #6, #7, #8: three more DRIVERS, and the framerate is UNCHANGED — as predicted (2026-08-17)

`apply_driving_model` (136 bytes), `draw_track_object` (61) and `fill_dash_edge_columns` (35) are
real C, `make validate`d at 0 mismatch over 200/300/600 randomised cases, **17 sabotages injected
and 17 detected** (16 as `mem[]`/register mismatches, one — the byte-swapped boundary pointer, which
aims the store into zero page and lets the walk overwrite its own loop bound — as a hang),
`make determinism` and `make determinism-drive` both 64K byte-identical against a
reference recorded from the previous commit.

**FPS 2.92 → 2.92**, measured as a same-session A/B: two 30 s warp runs of
`STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` + `fps_series.gdb`, one on this build and one on a
stashed HEAD build, each reporting 2.92 in both of its two independent rows.

⚠ **That 2.92 is NOT a new baseline and must not be quoted as one.**  It is a much shorter window
than the 2.71-2.73 figures above (≈1800 fields against ≈6500), and this project's own rule 1 is why
that matters: a `STRAIGHT_TO_RACE` run eventually leaves the track, so a short window is a
*different scene*, not a cleaner measurement of the same one.  What the pair of runs is good for is
exactly what it was run for — **the control and the test agree to three digits, so the change costs
nothing** — and nothing more.

⭐ **No performance claim was made for these three before they were written, and that is the point.**
`docs/faithfulness-seam.md` §8's rule (pick a twin by how much of the row is *in* the routine) says
a ~100-byte driver over a transliterated subtree collects nothing, twins #4 and #5 measured exactly
that, and these three were written for the naming instead — the driving model's 16-bit state vector,
the object plotter's argument block, the 24 per-slot object arrays (`docs/static-map.md`).  The
lever is still the view pipeline's LEAVES: the `$2C00`-`$2FFF` span plotters and `project_point`.

⭐ **The phase table was re-measured afterwards and says the same thing from the other side**
(§WHERE THE TIME GOES — CURRENT TABLE): phases 4, 18 and 15 read **28, 30 and 5 ms** against the
previous **28, 29 and 8**.  Three routines rewritten, three rows unmoved.

### ⭐⭐ THE RASTER-BAND RECORD REUSE: **2.05 → 2.63 FPS (+28%)** — the row was 96% MACHINERY (2026-08-17)

The biggest row in the table (the 50 Hz body drain, 141 ms / 26%) carried a note saying its cost
was *unexplained*: "`irq1v_band_schedule` is ~810 us a call for ~16 palette stores plus a handful of VIA
writes, and `make HWTIME=1` says it is not the hardware seam."

**That 810 us was an average over five arms that do different jobs**, and averaging them is what
made it unexplainable. `amiga/band_prof.gdb` brackets each band arm separately, attributing by the
band the call ENTERED on (`mem[$4F43]` read before the handler steps it):

| band | calls | us/call | what the arm does |
|---|---|---|---|
| 0 | 9453 | 792 | MODE 4 + 16 palette bytes |
| 1 | 9454 | 735 | MODE 5 + 16 palette bytes + the horizon split |
| 2 | 9454 | 838 | 16 palette bytes |
| 3 | 9454 | **675** | **4 palette bytes** |
| 4 | 9454 | 1128 | 4 palette bytes + `JSR $52A4` (233 of the 1128) |
| — | — | ~1720 | the `fireIrq1v` shim, paid five times |

⭐ **Band 3 writes FOUR bytes and costs 675 us against band 2's SIXTEEN at 838.** So the cost is
per-CALL, not per-store, and twelve extra palette writes are worth only 163 us of the difference.
⚠ And the instrument is not the story: an **empty bracket** on the same path at the same rate
(slot 6, `probe_irq_null`) reads **33 us**. Add the control before theorising about the rows.

**One field therefore costs 6113 us of its 20000 us budget, and 233 us of that — 4% — is the only
game work in it.** 96% is machinery.

⭐⭐ **AND THE BANDS DO NOT DRAW.** Each arm repaints the Video ULA for the band about to be scanned
and reloads User VIA T1 with its duration — a raster split, which the BBC performs on the CPU
because it has no copper. **This port already runs them on the copper**: `bbc_hw.cpp` records what
the handler wrote and `RevsScreen` re-emits it as copper WAITs; no palette is ever switched from the
CPU here. So the cycle's entire output is the RECORD, and the record is a pure function of five
palette tables (`$3458/$3468/$3478/$347C`), the horizon (`$4F1F/$4F20`) and the entry state
(`$4F43`). Everything else it leaves behind is idempotent — `$4F21/$4F22` is the horizon remainder,
`$4F43` returns to 0, the 6502 stack balances. `update_horizon_band` (`$4F44`) is a **main-loop** routine, so
at this framerate the inputs move about once every 25 fields.

`Platform::fireIrq1vField()` compares the 43 input bytes and, when they have not moved, runs `$52A4`
alone and re-asserts the record. Measured on the target: **235 real cycles, 9232 skipped — 97.5% of
fields**. Drain **6113 → 1308 us per body tick**, **176 → 27 ms per painted frame**.

| build | vblanks | painted | FPS (steady-state segments) |
|---|---|---|---|
| `BANDSKIP=0` — the control, and it IS the old code | 14094 | 563 | **2.05** |
| default (record reuse) | 14297 | 718 | **2.63** |

⚠⚠ **THE FIRST SABOTAGE PASSED.** Dropping the horizon from the digest — a defect that must put the
wrong palette on screen — changed **nothing** in the host's 300-frame 64 KB differential, because a
host run is ~36 **fields** and the horizon never moves in one. *A skip that is never wrong on static
inputs proves nothing about the case the change exists for.* Hence `make BANDCHECK=1`
(`amiga/band_check.gdb`): always run the real cycle, ask what the predicate would have decided, and
require every predicted reuse to equal the record actually built. On the target, driving: **0
mismatches in 14263 predictions** with the horizon moving 30 times — and the two sabotages then read
**21** and **22**. `g_bandHorizonMoves` counts the stimulus **over the run**, because reading
`$61/$63/$3C` at the end reports "parked" for a run that drove and then left the track.

⚠ **A tidy-up that re-seeded the RNG.** Hoisting the IRQ1V gate above `bbc_begin_band_cycle()` looked
free; that call also advances the 1 MHz field clock behind `$FE68`, the game's only entropy source,
and `make determinism` diverged at `mem[$0004]`. ⚠ It was *also* already failing on pristine HEAD —
a stale local reference (`tmp/` is gitignored) — which is why the control build's byte-identical
reproduction of HEAD's failure was the evidence that the routing was clean.

⭐ `BANDSKIP=0` differs from the default by **exactly 2 bytes**: dead 6502 stack scratch at
`$01F7/$01F8`, below the entry `S` of `$F8`. Frame buffer and band record identical.

⭐⭐ **FS-UAE `--warp_mode=1` is now the default way to run a probe** (`EXTRA_ARGS="--warp_mode=1"`):
~4.9x more emulated time per wall second, 60 s of wall clock for what used to need 200+. It disturbs
**nothing** measured here, and that is checked rather than assumed — every figure in this file is a
ratio of EMULATED quantities (painted frames per vblank, beam ticks per phase), so host speed
cancels. Verified by running one build with and without warp: identical per-segment FPS at matched
vbi.

⚠ **`$4E5C` is not "the 50 Hz game body"** — that description, in `symbols.csv` and repeated through
`probe.h`/`Revs.cpp`/`amiga-arch.md`, is what made this row look like non-negotiable engine work.
See `docs/rename.md`.

### ⭐⭐ THE DIRTY-REGION DECODE: **1.77 → 1.96 FPS (+10.7%)**, and the CONTROL IS THE INTERESTING ROW

Phase 6 item 0 step 2's payoff. Only **406 of 8320** frame-buffer bytes change per painted frame
(measured, `docs/direct-bitplane-plan.md` §7b), so `decode()` now compares each **8-byte cell
column** — eight contiguous bytes in the BBC layout, two aligned longwords — against a shadow of
the bytes that produced the buffer's current content, and converts only what moved.

| build | vblanks | painted | FPS |
|---|---|---|---|
| HEAD before the change (the twin-#2 baseline) | 9780 | 346 | **1.77** |
| shipping (dirty) | 9783 | **383** | **1.96** |
| `make DIRTY=0` (same loop, test off) | 9781 | 286 | **1.46** |

⚠⚠ **`DIRTY=0` IS NOT THE OLD CODE, so there are two numbers and both are true.** The dirty test is
worth **+34%** against its own control; the **cell-major restructure it required costs ~18%**
(1.77 → 1.46), because a display line's 40 bytes are 40 sequential destination stores while a cell
column's 8 lines are 8 stores strided by `kRowBytes`. **Net +10.7%.** ⭐ The lesson generalises to
every dirty-region scheme: the enabling restructure has its own price, and only the pair of
measurements separates "the test is good" from "the change is good". Quoting the +34% would be
quoting a win against a build that never shipped.

`g_decodeCells` reads a mean of **166 of 1040** cell columns converted, last frame 31 — the counter
that says the test engages at all, since one that fails open is exactly as fast as no feature and
looks identical on screen. Verified by an exact oracle rather than by inspection: `make
DIRTYCHECK=1` re-runs the conversion unconditionally into a copy of what the dirty pass produced
and requires byte equality (**0 / 12 checks**, car under power), and it catches both sabotages —
test fails closed **17015** wrong bytes, mode-change dirtying removed **1350**. That second one is
worth remembering: `m_lineMode` can re-point a line at a different conversion while its source byte
is unchanged, which no byte compare can see.

### ❌ DIRECT-TO-BITPLANE PLOTTING: built, proven byte-exact, **9% SLOWER**, not shipped

| build | vblanks | painted | FPS |
|---|---|---|---|
| shipping | 9782 | 383 | **1.96** |
| plot runs *as well as* the `mem[]` stores (`DIRECTPLOT=1`) | 9777 | 325 | 1.66 |
| plot runs *instead of* them, decode skips the region (`PLOTONLY=1`) | 9780 | 350 | **1.79** |

The plotter is correct — `DIRECTCHECK=1` compares the whole 16640-byte buffer against the shipping
decode around every sweep: 0 mismatches / 9 checks, and it catches a one-cell sabotage at 210. It
collapses 2148 cells into 360 runs. It is still a loss, for two reasons worth carrying:

1. **A run collapses the STORE, not the ITERATION.** Every unit still reads its own source byte out
   of a `$80`-strided block; that scan *is* the loop. Removing one byte store from a
   thirty-instruction, instruction-fetch-bound unit and adding run bookkeeping to it is negative
   before anything is gained. ⚠ The plan had predicted "2100 iterations become ~150" — the number
   that was actually going to fall was the store count, and nobody was paying for stores.
2. **The prize was already banked.** `PLOTONLY` measured *identically* with and without the decode
   skip, because the dirty-region decode was already skipping those cells. Two optimisations, one
   prize, and the cheap one had taken it three commits earlier.

⭐ The general lesson: **before building a representation change, ask which quantity the current
cost is proportional to.** Here it is source reads per frame (2148), and no layout on either side of
the seam changes that number. Full write-up: `docs/direct-bitplane-plan.md` §7f.

### ⭐⭐ THE UNIT LOOP'S *BYTES*: **2.38 → 2.58 FPS (+8.2%)**, phase 24 **131 → 102 ms** (2026-08-17)

Twin #2 (below) concluded that the sweep is **instruction-fetch bound in chip RAM** — 438 cycles for
a unit whose nominal cost is ~278 — and that conclusion is right.  What was wrongly read *off* it is
"so there is nothing left to do in C": if the loop is fetch bound then **its byte count is its cost**,
and the body still had a third of its instructions doing bookkeeping rather than work.  Four changes,
no change of representation, no asm:

| # | what | why it cost anything |
|---|---|---|
| 1 | ⭐ **the bus's hardware-range test, hoisted out of the loop** (user, 2026-08-17) | the cell store is `STA ($70),Y`, so `bus_write` cannot know statically that it misses $FC00-$FEFF.  The *line's* whole span is known, so one check per scan line replaces 2093 — 6 instructions become 1 (`move.b d2,(a2)`) |
| 2 | the opcode-slot table holds **pointers into `mem[]`**, not addresses | `&mem[$7C0F]` is a link-time constant, so the per-unit SMC check is `movea.l (a4),a0 / move.b (a0),d0` instead of a 32-bit `lea mem` plus a long-indexed load |
| 3 | the source byte and the opcode byte are **`unsigned char`**, not `unsigned` | a zero-extended long needs a separate `tst.l` / `cmpi.l`; the byte load already sets the flags the branch wants |
| 4 | the cell segment is a **pointer END**, not `i == 31` and `i < 40` | two compares and two branches per unit for a boundary crossed twice a line.  The stop path recovers the cell index as `(dp - segBase) & $FF`, which is `i * 8` in both segments |

**The hot path of one unit: 31 → 14 instructions, 86 → 34 bytes, ~278 → ~124 cycles nominal.**
⭐ Quote the static count as the win; the FPS pair is the progress figure, measured control-and-test
in the same session with the same instrument (`fps_series.gdb`, 30 s warp, `STRAIGHT_TO_RACE=1
FPSCOUNT=1 FIXED_RNG=1`): **339 painted / 7118 vblanks → 369 / 7160**, and both runs contain the
same one dropped row where the car leaves the track, so they are comparable row by row (steady rows
2.44-2.63 → 2.73-2.83).  Phase 24 fell **131 → 102 ms**, i.e. −29 ms of a ~410 ms frame.

⚠ **The measured drop is −22% where the static count predicted −55%**, and the gap is the rest of
phase 24: ~77 lines of driver work (the phase 2/3 boundary composition, `view_move_stop`,
`step_scanline`) plus the VERTB ISR preempting this phase.  The unit loop is no longer the whole row.

⭐⭐ **The general lesson, and it is the third costume of the same one:** `bus_read`/`bus_write` are
for the *hardware* window, and paying their range test on a pure-RAM access is machinery, not the
game's algorithm.  The transpiler already routes every *constant* non-hardware address straight to
`mem[]` (`is_hw`), so the corpus is clean — what leaks is the **indirect modes**, `(zp),Y` and
`(zp,X)`, whose address is only known at run time.  Those are exactly the plotters.  Wherever a twin
runs an indirect store in a loop, the range test can be hoisted to wherever the *pointer* is known,
and the arms are provably equivalent for a RAM address (inverting the flag passes all 700 fixture
cases — the proof, not a gap).

⚠ **`mem[]` may NOT be aliased as `uint16_t*`/`uint32_t*` to widen these accesses** — that is the
endian rule (`make endian-lint`), and it would read correct on the host and byte-swapped on the
target.  It would not help here anyway: the sweep's strides are 8 (screen cells) and $80 (source
blocks), so there is no adjacent pair to widen.  A wide store is only endian-neutral when every byte
in it is the SAME value, which is the run-collapsing idea `docs/direct-bitplane-plan.md` §7f measured
as a 9% LOSS.

### ⭐⭐⭐ 11 ms OF PHASE 24 WAS NEVER $7BE2 AT ALL, and the row splits BY PAINTING PHASE (2026-08-17)

**Everything below this section that says "phase 24 = the drivers" is measuring one bracket too
wide.**  Phase 24 opened at `JSR $7BE2` ($1748) and the next bracket was the paint hook at $1701, so
the whole main-loop TAIL after the sweep was charged to the sweep.  ⚠ The reason it hid for so long
is that the tail's other exit *did* re-open phase 0: `$1753 BEQ $178F` skips the frame-wait spin
whenever `$62F6` is zero, and that path — `JSR $0EE5`, `JSR $0E74`, `JSR $513A`, `JMP $1701` — never
touched a bracket.  **`PROBE_PHASE_VIEWTAIL` (32) at $174B costs ONE transition a frame and makes
phase 24 mean the routine: 11 ms of the 82 was the tail.**

⭐⭐ **And then the split that "unit loop vs drivers" was reaching for, at TWO transitions a frame:
the routine's three painting phases are three separate brackets** (`PROBE_PHASE_VIEWP2`/`P3`, 33 and
34 — each runs once per sweep, so unlike VIEWSPLIT there is nothing to subtract).  Beside them,
`PROBE_VIEW_*` counts the work each did — units, chain-run segments and driver lines — accumulated
O(1) per run out of the pointer difference, never per unit:

| bracket | what it is | ms/frame | units | segments | lines | µs/unit | µs/line |
|---|---|---|---|---|---|---|---|
| 24 | phase 1 ($7BE2), full-width lines, **no driver at all** | 28 | 1440 | 72 | 36 | 19 | 785 |
| 33 | phase 2 ($7D13), one planted stop, two chain runs | 19 | 426 | 48 | 16 | 44 | 1187 |
| 34 | phase 3 ($7F18), two planted stops, two computed entries | **42** | 282 | 66 | 25 | 150 | **1696** |

⭐⭐ **Phase 3 is HALF the row while painting 13% of the cells** — 25 lines, eleven cells each, at
1.7 ms a line.  That is the item, and "the per-line drivers" was never one thing.

**Where phase 3's 42 ms is, from two instruments that agree.**  `make VIEWP3=1` brackets its four
pieces per line with an empty bracket at the same rate as the control (read the control first: 25
transitions a frame, 2 ms, ~80 µs each); `make VIEWP3=3` validates every computed chain entry and
then does not RUN it, which prices the same thing with no bracket in the measurement at all:

| piece | VIEWP3=1 | VIEWP3=3 | the difference prices |
|---|---|---|---|
| 36 chain A's entry + boundary cell | 14 ms | 5 ms | **9 ms** of chain run |
| 38 chain B's entry + boundary cell | 26 ms | 15 ms | **11 ms** of chain run |
| 35 / 37 the two planted stops | 5 / 4 ms | 5 / 4 ms | 0 — `view_move_stop` is not the cost |
| 33 phase 2, for comparison | 19 ms | 11 ms | 8 ms of chain run |

⭐ **So ~20 ms of phase 3 is the two `view_enter_chain` calls and ~9 ms is planting stops.**  50
entries a frame for 282 cells is ~400 µs an entry against 721 µs for a full 40-cell line: the cost is
the ENTRY, not the cells.

### ⭐⭐⭐ THE CALIBRATION — `make VIEWCAL=N`, and it says the brackets are HONEST (2026-08-17)

Everything above looked ~6x more expensive than its instruction count, which is the kind of gap that
makes every optimisation decision a guess.  So: **burn a KNOWN number of cycles inside a bracket at
the same rate, in the same run, and read what it comes back as.**  `probe_burn_cycles()` is
1000 x (`nop` 4 + `dbra` taken 10) = 14 000 cycles = 1975 µs at 7.09 MHz, and `VIEWCAL=N` runs it N
times per phase-3 line inside bracket 39.  **The linearity in N is what verifies the instrument** —
one point could not tell a slow CPU from a fixed overhead:

| N | bracket 39, µs/call | minus N=0 | per 14 000 cycles |
|---|---|---|---|
| 0 | 1697 | — | — (this is ONE phase-3 line with no bracket inside it) |
| 1 | 3795 | 2098 | **2098 µs** |
| 2 | 6026 | 4329 | **2165 µs** |
| 3 | 8001 | 6304 | **2101 µs** |

⭐⭐ **14 000 cycles cost 2.10 ms ⇒ ~6.6 MHz effective, 94% of an A500's 7.09 MHz** — the missing 6%
is the interrupt load, and the beam brackets are telling the truth.  **The 6x gap was arithmetic on
my side, not an instrument fault**: the µs/line figures include the two chain runs and the whole
driver, and a 68000 `move.b dn,abs.l` — which is what every `cpu.N`/`cpu.Z` write compiles to — is
16-20 cycles, so a "handful of table reads" is thousands of cycles, not hundreds.

⭐ **What that licenses, and it is the point of building it:** phase 3's line is **1697 µs = ~11 300
cycles** measured with nothing inside it, and `VIEWP3=3` prices the two `paint_cells` calls in it at
~800 µs, of which the eleven cells are ~1000 cycles — so **~4300 cycles a line is the two calls'
FIXED SET-UP**, ~2150 cycles per call, 118 calls a frame, ~38 ms.  That is the biggest single item in
the view rasteriser and it is machinery: the base pointers reassembled out of `mem[$70..$73]` a byte
at a time, `view_stop_from` re-searched, the outer line loop's `advance_first`/`$7EEE`/`CPX` tail and
an eleven-register `movem` — all of it per entry, for four cells of painting.

⭐⭐ **PHASE 3's LINE, IN CYCLES — the whole 11 300, and there is no hot spot in it:**

| per line | cycles | what |
|---|---|---|
| the two `paint_cells` calls' fixed set-up | ~4300 | bases from `mem[$70..$73]`, the stop search, the outer loop's tail, `movem` — twice |
| the boundary cells (brackets 36/38 less their chain runs) | ~4300 | four `view_compose`, the computed entry, `sub_from`, the edge tables, two `bus_write` |
| the two planted stops (35/37) | ~1300 | `stop_unchanged` + `view_plant`, and only when the stop MOVED |
| the eleven cells actually painted | ~1000 | the unit loop, i.e. **9% of the line** |
| the scan-line step and the loop | ~400 | |

⭐ **What that retires: there is nothing left here to delete, only machinery to restructure.**  Two
thirds of the row is spent painting one third of the cells (phases 2+3: 59 ms for 708 cells against
phase 1's 28 ms for 1440), and both halves of that two thirds are *diffuse* — ~100 instructions of
generic entry, and flag/compose helpers whose N/Z go to memory because a following `JSR` could trap.
The two candidates that remain are therefore structural, and both are bigger than a tuning pass: a
**specialised driver entry** that takes the span the driver already knows instead of re-deriving it,
and **deferred flags** — computing `cpu.N/Z/C/V` at the exits and trap sites only, which is the same
argument that made twin #1 worth 62% and which `make validate`'s 196 traps over 100 illegal cases is
sharp enough to police.

⚠⚠ **TWO stale-build readings were produced on the way here and both looked like real data.**
`VIEWCAL=0` and `VIEWCAL=2` first read 8019 and 8007 µs — the `VIEWCAL=3` value, to four digits,
because the Makefile tracks no change to `EXTRA_DEFINES` and nothing in the sources had changed.
`make clean` between calibration points, every time; a value that repeats a previous build's to the
digit is the tell.

### ⭐ ONE SEGMENT PER LINE INSTEAD OF TWO: 186 → 118 chain-run set-ups a frame (2026-08-17)

The unit loop switches base pointers at cell 32 because 40 x 8 = 320 bytes does not fit one page, and
it did that by ENDING the run at cell 31 and going round the outer loop again — a second stop lookup
and a second run set-up on every line.  But `plot_ptr2` is `plot_ptr + 256` for every line the routine
itself steps, and then cell *i* lands on `base0 + i*8` across the whole line, so the run 0..39 is
contiguous and one segment covers it.  The general two-segment path stays, because `paint_lines_short`
steps the pointers itself and its odd carry tail can move one and not the other.

**Chain-run segments 186 → 118 a frame** (phase 1's 72 → 36), and the three brackets read 89 → 83 ms.
⚠ Quote the COUNT, not the milliseconds: that is a cross-run diff of per-iteration figures, which
Rule 2 forbids.  **FPS 2.72 → 2.71, i.e. unchanged** — ~6 ms of a ~370 ms frame is under the noise
floor, exactly as predicted, and it is recorded as a static win rather than a framerate one.
Byte-exact: `make validate` 700/700 and `make determinism` identical over 300 frames.

⚠ **A SABOTAGE THAT PASSED, and it is a fixture gap, not a proof:** forcing the one-segment path on
unconditionally also passes 700/700, because the shim reseeds `plot_ptr`/`plot_ptr2` one page apart on
every call and no fixture can make them differ.  The two-segment path is therefore unreachable from
`make validate` — the equivalence argument above is by construction, and `docs/validation-harness.md`
carries the gap.

### ⭐⭐ WHERE $7BE2's 82 ms ACTUALLY IS — the DRIVERS are 65% of it, and two negative results (2026-08-17)

⚠ **Superseded in part by the section above**: the 82 ms this section splits included 11 ms of main
loop tail, and "the ~77 lines of driver" is really three different jobs with phase 3 dominating.  The
NOUNITS differentials themselves stand.


**The bracket split above was wrong, and a differential build settled it.**  `make VIEWSPLIT=1` read
54 ms of unit loop against 76 ms of driver; its own control read 12 ms over 118 brackets, i.e. ~100 µs
per bracket transition, which is impossible for a beam read and an accumulate.  The brackets cost more
than the code inside them and mis-attributed the difference.  ⚠ Do not use VIEWSPLIT for a share.

The honest instrument is a **differential build** — strip a stage, re-measure the whole row, and take
the difference (`make NOUNITS=1|2|3`, picture wrong by construction, never quote a framerate):

| build | phase 24 | what the difference prices |
|---|---|---|
| shipping | **82 ms** | |
| `NOUNITS=3` — consume the sources, drop the STORE | 75 ms | the 2148 stores: **7 ms** |
| `NOUNITS=1` — keep the iterations, drop all memory work | 55 ms | the source reads + consume: **~20 ms** |
| `NOUNITS=2` — do not run the loop at all | 53 ms | the loop's own iteration overhead: **~2 ms** |

⭐⭐ **So the 2093-unit chain is ~29 ms and the ~77 lines of per-line DRIVER are ~53 ms — 65% of the
row.**  ⚠ `NOUNITS=1`/`2` also stop zeroing the sources, and the control tables overlap the source
blocks ($3080 is column 1's), so the drivers' workload shifts a little in those two; `NOUNITS=3` has
no such confound and is the one to trust.  ~53 ms over 77 lines is **~4400 cycles a line** for a
handful of table reads, two composed bytes and two stores — the machinery pattern again, and the
drivers still run on cpu.h macros (`view_compose` is `LDA`/`AND`/`ORA`, `stop_unchanged` a `CPY`,
`step_scanline` three `ADC`s), each writing N/Z to memory.

**❌ NEGATIVE RESULT, and it is what redirected the search: removing the per-unit SMC check bought
nothing.**  The check was two loads and two compares per cell — 56 of the loop's 124 nominal cycles,
45% — asking whether this unit's store had been overwritten with an `RTS`.  The answer only changes
when a driver plants, and the twin does every plant, so it is now tracked (one scan of the forty slots
per sweep, an update per plant) and the loop consults nothing: **14 → 8 instructions, 34 → 20 bytes,
~124 → ~64 cycles.**  Result: phase 24 **84 → 82 ms**, FPS 2.73 → 2.73.  ⭐ Two of the four memory
accesses per unit gone as well, for ~2 ms — which is only consistent with the loop being ~29 ms, and
is the measurement that proves the split above was wrong.  The change stays (validated, byte-exact,
and a simpler loop for the line-skip work to build on) but it is not a win.

**What this retires:** "attack the unit loop harder".  At ~29 ms for 2148 units it is ~95 cycles a
unit and there are 8 instructions in it; hand-asm's ceiling here is single-digit milliseconds.  ⭐ The
two things left on `$7BE2` are the DRIVERS' machinery (~53 ms, and the same flags-are-dead argument
that made twin #1 worth 62%) and the line skip §7g priced at ~33% of a 29 ms loop, i.e. ~10 ms.
**The drivers are now five times the prize the scan is.**

### ⭐⭐ AND THEN ITS DRIVERS: **2.58 → 2.73 FPS (+6.2%)**, phase 24 **102 → 84 ms** (2026-08-17)

⭐ **First the SPLIT, because "84 ms" does not say what to attack.**  `make VIEWSPLIT=1 PROBES=1`
brackets the unit loop per scan line (phase 30) and leaves the ~77 lines of per-line driver in phase
24, with an EMPTY bracket at the same rate as the control (phase 31):

| | ms/frame | calls/frame |
|---|---|---|
| phase 30, the 2093-unit chain | 54 | 118 chain runs |
| phase 24, what is left: the per-line DRIVERS | 76 → **60** | 77 lines |
| phase 31, the instrument's own cost | 12 | 118 |

⚠ **Read the control first.**  The split build's three rows sum to ~142 ms where the unsplit row is
102, so the brackets inflate what they measure by ~28% and only the *differences* between split runs
are quotable.  What the split settles is the shape: after the byte pass above, **the drivers are the
bigger half** — ~60 ms for 77 lines is ~4400 cycles a line, for what is a handful of table lookups
and two composed bytes.

**What was in there: two LINEAR SEARCHES OVER THE FORTY UNITS.**  The chain is unrolled, so the
drivers work in *addresses* and keep asking which unit an address is — `view_enter_chain` (is this
computed JSR target a unit start, or a unit+$05?) and `view_is_slot` (is this plant target an opcode
slot?).  Between them that is ~190 forty-entry searches a frame, each iteration re-deriving
`VIEW_UNIT_ADDR(i)`.  Replaced by two 256-byte tables indexed by the address's low byte — one load —
**built from the same `VIEW_UNIT_ADDR`/`g_viewSlotP` the rest of the twin uses**, so there is no
second copy of the layout.

⚠ The tables give ONE answer per address where the oracle searched in order, which is only
equivalent while no address is both a unit start and another unit's +$05.  It holds by arithmetic
(chain A is 0 mod 17 from $7C00, chain B is 2, a +$05 entry is 5 or 7), and the twin **counts** every
clash while building — with `make validate` asserting the count is zero, because a counter nobody
reads is not a safeguard.  Forcing a collision makes the harness FAIL.

| build (same instrument, same session) | vblanks | painted | FPS |
|---|---|---|---|
| HEAD before both passes | 7118 | 339 | **2.38** |
| + the unit loop's bytes | 7160 | 369 | **2.58** |
| + the drivers' address lookups | 7187 | 392 | **2.73** |

**+14.5% over the two, and phase 24 is 131 → 84 ms.**  ⭐ The remaining row is ~54 ms of unit loop
and ~30 ms of driver, so the 2093-unit scan is once again the thing to attack — and per §7a/§7f that
means **producing fewer sources**, not scanning faster.

### ⚠⚠ TWIN #2, `$7BE2 view_paint_lines`: **1.71 → 1.77 FPS (+3.6%)**, and that is the FINDING

The dashboard was the biggest main-loop row in the table above (21.8%, 151 ms) and it is now a
native twin — validated 700/700 with thirteen sabotages, byte-identical over `make determinism`'s
300 frames. **It bought 3.6%**, which is barely over this project's own noise floor. ⭐ The number
that did not move is the point of the entry.

| build | vbi | painted | FPS | phase 24 |
|---|---|---|---|---|
| control (HEAD), same command, same session | 9781 | 334 | **1.71** | 151 ms |
| twin, first cut (address arithmetic per unit) | 9811 | 312 | **1.59** | — |
| twin, running pointers + slot table | 9780 | 346 | **1.77** | **131 ms** |

Three things this settles, and the third is the one that matters:

1. ⚠ **The first cut was SLOWER than the transliteration.** The generated code spells each unit's
   opcode slot as an *absolute address baked into that copy* — forty compile-time constants,
   because the chain is unrolled. A twin that rolls the chain into a loop has to *derive* that
   address, and deriving it costs more than the flag bookkeeping the loop removed. Rolling up an
   unrolled 6502 chain is not free; the constants were the unrolling's payload.
2. **Tightening it to running pointers** (source `+= $80`, destination `+= 8`, slot from a table,
   the two destination bases hoisted out of the column) recovered that and 20 ms more.
3. ⭐⭐ **But 131 ms for 2093 units is ~420 cycles a unit, and the unit is ~27 instructions — so
   this routine is INSTRUCTION-FETCH BOUND in chip RAM, not interpreter bound.**  ⚠ True, and it
   does NOT imply the loop was finished: being fetch bound makes the body's BYTE COUNT the cost,
   and shrinking it bought another 8% (the section above). That is why twin
   #1 got 62% and this got 3.6%: `irq1v_band_schedule`'s cost was machinery with no counterpart on the
   BBC (a C bridge, a virtual dispatch, N/Z stores per palette write), and deleting machinery is
   nearly free. The sweep's cost is *2093 iterations of a small loop*, and on an A500 the floor
   for that is set by fetching the loop body over a contended bus. No amount of faithful C
   removes iterations.

⭐ **So the dashboard is a REPRESENTATION target, not a twin target**, exactly as
`docs/direct-bitplane-plan.md` §7a concluded from the other direction: 2093 units run and ~83
bytes change, so the win is in not scanning (a dirty list, or §8's sprites), not in scanning
faster. This twin is the last useful thing to do to the sweep *as written*.

### ⭐⭐ `mem[]` WAS `volatile`, AND THAT COST 10% OF THE FRAME — **1.56 → 1.72 FPS** (2026-08-16)

| build | vblanks | painted | FPS | `.text` |
|---|---|---|---|---|
| `make MEMVOL=1` (the old qualifier) | 11824 | 369 | 1.56 | 246 984 |
| shipping                             | 11775 | **405** | **1.72** | **241 254** |

One qualifier, on one array, and the array is the whole engine: `volatile` forbids gcc every
optimisation over `mem[]` — no CSE on an address, no keeping a byte in a register across two
uses, no reordering — and every transliterated instruction touches it.  5.7 KB less code too,
which on a machine whose program lives in chip RAM is not nothing.

It was there because the declaration said *"shared between main thread and VBI audio thread"*, a
comment inherited from the predecessor project, **which had one and this port does not.**  What
this port has is the VERTB ISR, and in the shipping model that handler does copper work, the
teletext flash counter, the audio scheduler and one `++` — none of them touch `mem[]`.  The 50 Hz
body, the only thing that writes `mem[]` from anywhere, was moved out of the ISR in Phase 5 for
unrelated reasons.  ⚠ Under `make BODY_IN_ISR=1` the qualifier comes back automatically
(`src/cpu/mem_decl.h`), because there the old hazard is real again.

Verified rather than assumed, control and test side by side with `fill_catch.gdb`: **identical**
on every counter — BAD frames 0, tear frames 0, decode mismatch 0, irqClobber 0, stack imbalance
0, body ticks dropped 0, and the same single pre-existing edge jump at line 124.  Plus the whole
host sweep (`validate`, `determinism`, `mode7`, `tracks`, `sound`, `trackmenu`, `endian-lint`).

⭐ **The general lesson, and it is the same one twin #1 taught in a different costume: the port's
biggest costs are not in the game's algorithms, they are in the MACHINERY the transliteration is
wrapped in.** A qualifier, a virtual dispatch, a flag store. Look there before optimising a loop.

### ❌ NEGATIVE RESULT: dead-flag elimination in the transpiler is worth nothing (2026-08-16)

Written down so it is not tried twice.  **Hypothesis:** a 6502 writes N/Z on nearly every
instruction and reads them almost never; `UPD_NZ` is two absolute-long stores on the 68000; the
transpiler already has a backward CFG liveness pass (used only for the load/store fold), so
emitting `_NF` forms where the flags are provably dead should be a corpus-wide win.

**Built it.  2783 of 4390 flag-writing instructions (63%) dropped their flag writes.  Result:
`.text` 0.3% smaller and 369 → 376 painted frames — under the 3% noise floor.  Reverted.**

Why: **gcc's dead-store elimination had already done it.**  `cpu` is a plain global struct, so
within any stretch of straight-line code the later flag store kills the earlier one.  The only
places gcc cannot are across an opaque call — and those are exactly the places the liveness pass
also refuses (it treats `JSR` as reading everything).  Confirmed by relaxing that too: assuming
no callee reads caller flags lifts coverage only 63% → 76%, so an interprocedural version has
almost nothing left to win either.

⭐ **The transferable part: before hand-writing an optimisation the compiler might already be
doing, look at the emitted code for the case you care about.**  The evidence was in the
disassembly all along — the flag stores that survive are the ones bracketing a `jsr`.

That experiment did leave two things behind, both kept: `make determinism` (below) and
`REVS_FIXED_RNG` on the host.

⚠ **And `$1A20` is not "the rasteriser" in the sense the notes claim.**  A snapshot-diff of the
frame buffer around it (`make SHAPE=1`, `src/platform/shape.h`) says it changes **6-7 bytes per
call, at display lines 26..55** — inside the sky band, i.e. engine variables that happen to live in
the frame buffer.  Whatever plots the road, it is not this call's own writes.  ⭐ Per-phase
attribution on the host says the whole main loop changes only **~280 of 8320 frame-buffer bytes per
painted frame** (max 5046 on a scene change), which is a strong argument for dirty-region drawing
and a weak one for making the decode itself faster.

**Superseded table (2026-08-13), kept because two of its conclusions propagated into
`docs/phases.md` and `docs/direct-bitplane-plan.md`:** `$7BE2` 36.1%, `$1A20` 21.1%, `$24F6` 19.0%,
`$1E15` 8.0%, `$46A1` 6.6%, the frame wait 2.5%.  It was taken before the 50 Hz body moved out of
the ISR, so more than half the frame was invisible to it and every share was inflated ~2x.
⚑ The standing lesson holds a third time: **re-measure, never quote.**

<details>
<summary>The 2026-08-13 write-up, in full (it is still the record of how the 2.5% frame-wait row and
the road-subsystem naming were found)</summary>

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
| **19.0%** | 133 | 5 | **`build_track_geometry`** (`$24F6`) | the **road-geometry projection pass** — builds the two 40-point edge lists (`edge_x_lo/hi`, `edge_y`) that phase 11 then rasterises.  Named 2026-08-13; see below |
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
`build_track_geometry` is the producer of exactly the arrays `interp_edge` consumes, so these two
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

</details>

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
. ./env.sh && GDBSCRIPT=fps_series.gdb ./diag_run.sh 240
```

⚠⚠ **`fps_series.gdb`, NOT `fps_seg.gdb` — the instrument itself was biasing the number.**
`fps_seg` re-arms a CONDITIONAL breakpoint per segment (`tbreak Revs::render if g_vbiCount >=
N`), so gdb halts the machine at every call to evaluate it.  Measured 2026-08-13 on the SAME
binaries:

| build | fps_seg (conditional stops) | free run / in-program series |
|---|---|---|
| pre-Phase-5 | 1.4 | 0.43 (single free window) / **1.46** (series) |
| Phase 5, rendering | **0.02** | 0.66 (single free window) / **0.78** (series) |

The 30× error on the rendered build was first read as a catastrophic regression and sent this
session bisecting a renderer that was fine.  Two lessons, both cheap:

- **Put no gdb stop inside a measurement window.**  `fps_series.gdb` reads a series the PROGRAM
  sampled for itself — the VERTB handler stores `g_fpsFrames` every 512 vblanks (a mask and
  three stores) and ONE stop after the run prints all of it.
- **A biased instrument can agree with the truth on one build and be 30× out on another.**
  fps_seg's 1.4 for the unrendered build was right; that is exactly why nobody caught it.

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
