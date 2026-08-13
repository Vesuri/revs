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
- **⚠⚠ gdb can READ the emulated machine but NOT WRITE it — `set var` is silently dropped.**
  Measured 2026-08-13 against this FS-UAE build, stopped at a `tbreak` in `Revs::render`, with
  three different targets: a `volatile uint8_t` array element (`g_keyDown[0x21]`), a plain
  `volatile` global (`g_keyUnmappedCode`), and the 6502 image (`mem[0x9000]`).  All three read
  back **unchanged immediately after the assignment**, and still unchanged after a `continue`.
  gdb prints **no error** — the write just does not happen, so a poke-and-observe script reports
  "the poke had no effect", which is indistinguishable from "the code under test is broken".
  ⚠ **`amiga/keytest.gdb` is built entirely on `set var g_keyDown[…]`** and is therefore suspect;
  see the warning at its head.  **Verify anything you would have poked with a build flag
  instead** — that is what `STRAIGHT_TO_RACE` and `FIXED_RNG` are for: put the stimulus in the
  binary, where it demonstrably runs.
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

## ⚠⚠ `Remote connection closed` mid-run is usually ANOTHER SESSION, not your build

`diag_run.sh`, `run.sh` and `debug.sh` used to begin with an **unqualified `pkill -9 fs-uae`** —
correct housekeeping against a stale copy of your own, but also a **machine-wide** kill: any other
terminal, session, or *other project* launching a run murdered yours.  They now go through
`~/.local/share/amiga/fsuae_common.sh` (outside the repos) and stop only the emulator recorded in
this directory's `.run/fsuae.pid`, on a per-directory `$DEBUG_PORT`.  If a run still dies, it is
either your own previous run in *this* checkout, or a hand-typed `pkill`.  The victim sees

    <script>.gdb:7: Error in sourced command file:
    Remote connection closed

which reads exactly like the build under test crashing, and `.run/fsuae-dbg.log` stops abruptly
with no error — so the natural (wrong) conclusion is "my code dies after N vblanks".

**Diagnose it in one command before theorising about your own build:**

```sh
pgrep -fl "diag_run.sh|fs-uae" | grep -v $$      # anything here means you are sharing the machine
```

Measured 2026-08-12: three profile runs were read as "the PROBES build crashes somewhere past
vbi 262" and one as "PROBES is ~10× slower than FPSCOUNT, budget 1250 s".  Both were wrong.  A
concurrent Rescue-on-Fractalus session's `diag_run.sh` was `pkill`ing them, and the surviving
short run was simply the one that happened not to overlap a launch.  ⚠ **A killed run is not a
slow run**, and a timing figure derived from a run that ended this way is worthless — the wall
clock includes however long the script sat out its timeout after the emulator was gone.

Corollary worth its own line, because it makes the above much harder to spot: `diag_run.sh` sits
out its **full** `$1` seconds even when FS-UAE and gdb are long dead, and prints only at the end.
**A live `diag_run.sh` in `pgrep` does not mean a live run.**  Check for `fs-uae` itself.

## ⚠ Don't rebuild while a run is live either

`diag_run.sh` copies `out/Revs.exe` into `.run/dh1/Revs`, so the emulated Amiga has its own copy —
but **gdb reads symbols from `out/Revs.elf` in place**.  Running `make` (or worse, `make clean`)
in `amiga/` while a run is in flight replaces the file gdb resolved its symbols from, and a
different flag set (`PROBES` vs `FPSCOUNT`) means those symbols no longer describe the inferior.
A long run is exactly when it is tempting to "just do something else in the meantime".

⚠ This is a real hazard but it was **not** the cause of the 2026-08-12 failures above — that was
the cross-session `pkill`.  Recorded separately so the two are not conflated: this one corrupts
your *symbols*, the `pkill` one ends your *run*.

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

Stray `fs-uae` copies of your own are handled by the scripts (`fsuae_stop_previous` /
`fsuae_claim_port` in `~/.local/share/amiga/fsuae_common.sh`).  Kill anything else **by pid** —
never `pkill fs-uae`, which also takes down the other projects' emulators.
