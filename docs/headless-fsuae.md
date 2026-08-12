# Headless FS-UAE measure→fix→verify loop (the Amiga side)

> **Read this when writing a probe, driving FS-UAE headlessly, or debugging a stale build.**
> ⚑ Inherited from the Atari port, where this loop diagnosed several timing and render bugs
> precisely where static reasoning kept failing — **measure, don't theorize.**
>
> Already verified working in this repo: a plain build reads `vbi=823 painted=0`, an
> `FPSCOUNT=1` build `vbi=824 painted=803` (≈48.8 FPS with nothing yet to draw).  So the
> display takeover, the VERTB handler, the frame pump and the embedded 6502 image are all live
> and readable from gdb by name.  The BBC reference side is `docs/bbc-reference-loop.md`.

## The loop

```
cd amiga
. ./env.sh                      # MUST be in the SAME shell command as the run
make clean && make -j4 PROBES=1
./diag_run.sh 25                # or: GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200
```

- **`. ./env.sh` must be sourced in the SAME shell command as the run.**  It puts both
  `fs-uae` (`~/.local/fs-uae`) and `m68k-amiga-elf-gdb` on PATH.  A separate `. env.sh` call does
  not persist between tool invocations, so `fs-uae` will look "not found".
- **`amiga/diag_run.sh [delay]`** is the batch harness: boots `out/Revs.exe` under the FS-UAE gdb
  stub, runs `[delay]` seconds, SIGINTs gdb (breaking its `continue`), runs the print commands in
  **`amiga/diag_timing.gdb`**, and writes everything to `amiga/.run/gdb-out.log` (echoing a
  filtered tail).
- **`-g` is always on** (CORE_CFLAGS), so every global is readable by name and `mem[0xNNNN]`
  reads the 6502 image.  A `while $i < N … end` loop dumps an array.
- **⚠ The `continue` in `diag_timing.gdb` is load-bearing.**  Without it gdb runs the whole
  script at connect time, while the program is still halted at the trigger breakpoint, and every
  counter reads 0 — which looks exactly like a hung build.  (Cost one round trip here.)
- **Probe pattern:** add `volatile` globals under `#ifdef REVS_PROBE` (defined in
  `PlatformAmiga.cpp`), stamp `g_vbiCount` at milestones, print the deltas.  A pure-compute
  stretch shows up as a `g_vbiCount` delta, because the real VBI keeps counting through it.
- **⚠ A dropped probe counter reports GARBAGE, not zero.**  `--gc-sections` removes any counter
  the current configuration never touches, and gdb then resolves the name into `.text` and prints
  instruction bytes.  Every global a committed `.gdb` reads must be in **`PROBE_SYMS`** in
  `amiga/Makefile`; `make probe-audit` runs on every link and fails the build otherwise.  Measured
  here: a non-FPSCOUNT build read `painted=1223110688`.
- **⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol** — from that line onward,
  not just that column.  So deleting a probe global silently kills every committed `.gdb` that
  still prints it.  **When you delete or rename a probe global, grep `amiga/*.gdb` for it**, and
  treat "the trace stopped after the header" as a stale script, not a dead probe.
- **A faster CPU exposes beam-timing races**: `AMIGA_MODEL=A1200`,
  `EXTRA_ARGS=--cpu=68040`.  See `docs/amiga-lessons.md` §SPRxPT — A1200 alone was not enough
  there; the 68040 is what made the violation fire.

## ⚠ The stale-build trap

**Always `make clean && make -j4 PROBES=1` before a headless probe run**, and **run `make clean`
after toggling `PROBES` OR after editing a widely-included header.**

The Amiga Makefile tracks neither the flag nor header dependencies, so a partial rebuild links
**stale object files** against new code.  The failure mode is not a link error — it often links a
**working-but-wrong binary** with **silent runtime breakage** (struct layout / member-offset
mismatches from a changed header, showing up as unrelated corrupted rendering or wrong
behaviour).

**Treat any unexplained runtime regression right after a header edit or a `PROBES` toggle as a
stale build until a clean rebuild rules it out** — don't chase it as a logic bug first.

## ⚠ Never rebuild while a run is live

`diag_run.sh` copies `out/Revs.exe` into `.run/dh1/Revs`, so the emulated Amiga has its own copy —
but **gdb reads symbols from `out/Revs.elf` in place**.  Running `make` (or worse, `make clean`)
in `amiga/` while a run is in flight replaces the file gdb resolved its symbols from, and a
different flag set (`PROBES` vs `FPSCOUNT`) means those symbols no longer describe the inferior.
A long run is exactly when it is tempting to "just do something else in the meantime".  **Don't
build in `amiga/` while `pgrep -f diag_run.sh` says one is alive** — start a second checkout or
wait.  (Measured 2026-08-12: a `make clean && make` landed mid-way through a 1250 s profile run.)

## ⚠ A PROBES profile run needs ~20 minutes of wall clock, not 200 seconds

The two builds are nowhere near the same speed under the gdb stub.  Measured on the 1.4 FPS
baseline build:

| Build | Emulated vblanks per real second |
|---|---|
| `FPSCOUNT=1` | ~10 (vbi 2020 in 200 s) |
| `PROBES=1` | **~0.9** (vbi 262 in 300 s) |

`phase4_prof.gdb` waits for `g_vbiCount >= 900` because the main loop does not start until the
front end releases, so **900 vblanks is not a tunable number** — lower it and the script reads
`loopFrames=0`, every `g_phaseTicks[]` is zero, and it dies on `Division by zero` in the
share computation.  Budget `./diag_run.sh 1250` and run it in the background.

⚠ This is also the third independent reason never to quote a framerate from a PROBES build
(`docs/perf-method.md` Rule 1): it is not "20-35% slower", it is an order of magnitude slower
under the remote debugger.

## Judging appearance

You cannot.  **The remote debugger greys the display**, so a headless run proves cost and state,
never appearance.  For that: `./run.sh` with a wiped `.uss` state (the script deletes it every
run — `diag_run.sh` shares the `--state_dir` and leaves a state saved while the CPU was halted on
a grey first frame, and resuming that makes ANY build look frozen and grey; that cost hours of
false bisecting on the Atari port).

Screenshots: this fsemu-core FS-UAE takes them with **hold F12, press S**, and the destination
comes from the `FSEMU_SCREENSHOTS_DIR` env var (the `--screenshots_output_dir` config key is
parsed but ignored).

For an interactive-only bug the harness can't reproduce: the user drives a live window and you
SIGINT gdb (never `kill -9`) on their cue.

## Housekeeping

`kill -9` stray `fs-uae` processes before a run to avoid attaching to a stale copy (`diag_run.sh`
already does `pkill -9 fs-uae`).
