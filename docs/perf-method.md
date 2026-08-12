# Performance method — how to get a number you can trust

> ⚑ **Method inherited from the Atari port**, where these rules were each learned by getting a
> number badly wrong first.  The *numbers* from that project are not carried over — Revs has no
> baseline yet, and inventing one would repeat the exact mistake §"Measure the baseline" below
> describes.  Companion: `docs/m68k-optimisation.md` (what to do once you know where the time
> goes), `docs/headless-fsuae.md` (how to drive the target).

## The target machine

**A500, 7 MHz 68000, PAL.**  A frame is 20 ms.  Spending 10 ms on *anything* is half the budget.
Units: 1 raster scanline = 63.56 µs; a PAL frame = 313 lines.  Be conscious of absolute
milliseconds, always — a percentage of an unknown total is not a measurement.

**No performance target is set for Revs yet.**  Set one from a measured baseline, not from a
wish: run the end-to-end skeleton on the target, see what it does, then decide what "good"
means.  (Postmortem §4.1: profile a slow end-to-end skeleton on real hardware *before*
committing to an approach.  The Atari port's retired "50 FPS is impossible without an algorithm
change" conclusion was disproven by hand-asm — the ceiling was GCC, not the algorithm.)

## Rule 1 — the ONLY way to quote a framerate

```
cd amiga && make clean && make -j4 FPSCOUNT=1 FIXED_RNG=1
. ./env.sh && GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200
```

`FPS = 50 * g_fpsFrames / g_vbiCount` — painted frames per **emulated** vblank, so host speed
and the gdb stub's own slowness cancel out completely.

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
different probe set, or a different workload.  **Re-measure, don't quote.**  Two specific traps:

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
