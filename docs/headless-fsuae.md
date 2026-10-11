# Headless FS-UAE measure→fix→verify loop (the Amiga side)

> **Read this when writing a probe, driving FS-UAE headlessly, or debugging a stale build.**
> The general rules — the shared launcher, sourcing `env.sh` in the same shell command, a failed
> run leaving the previous log, conditional breakpoints throttling the emulator, silent debug
> runs, `Remote connection closed` from another session, never rebuilding during a run, judging
> appearance only from a clean run, verdicts by where gdb stopped, `EXTRA_ARGS` and repeated
> options, Kickstart 1.3 and directory drives — are maintained in AmigaXDev
> `docs/reference/headless-fsuae.md`. This page holds what is specific to Revs. The BBC
> reference side is `docs/bbc-reference-loop.md`.

## The loop

```
cd amiga
. ./env.sh                      # MUST be in the SAME shell command as the run
make clean && make -j4 PROBES=1
./diag_run.sh 25                # or: GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200
```

- **`amiga/diag_run.sh [delay]`** boots `out/Revs.exe` under the gdb stub, runs `[delay]` seconds,
  SIGINTs gdb, runs the print commands in **`amiga/diag_timing.gdb`** and writes everything to
  `amiga/.run/gdb-out.log` (echoing the last `${GDBTAIL:-40}` lines). `-g` is always on
  (`CORE_CFLAGS`), so every global is readable by name and `mem[0xNNNN]` reads the 6502 image.
- **The `continue` in `diag_timing.gdb` is load-bearing**: without it the script runs at connect
  time, with the program halted at the trigger, and every counter reads 0.
- **Probe pattern:** `volatile` globals under `#ifdef REVS_PROBE` (defined in `PlatformAmiga.cpp`),
  `g_vbiCount` stamps at milestones, deltas printed at the end.
- **Every global a committed `.gdb` reads must be in `PROBE_SYMS`** in `amiga/Makefile`;
  `make probe-audit` runs on every link. A dropped counter reads garbage (a non-FPSCOUNT build read
  `painted=1223110688`). When you delete or rename a probe global, grep `amiga/*.gdb`.
- **Beam races:** rerun with `AMIGA_MODEL=A1200 EXTRA_ARGS=--cpu=68040`; A1200 alone did not expose
  the `SPRxPT` deadline violation, the 68040 did.

## gdb can READ the emulated machine but NOT WRITE it

`set var` is silently dropped: measured against a `volatile uint8_t` array element
(`g_keyDown[0x21]`), a plain `volatile` global (`g_keyUnmappedCode`) and the 6502 image
(`mem[0x9000]`), all unchanged after the assignment and after a `continue`, with no error.
⚠ **`amiga/keytest.gdb` is built on `set var g_keyDown[…]` and is therefore suspect** (see the
warning at its head). Put the stimulus in the binary instead: `STRAIGHT_TO_RACE`, `FIXED_RNG`.

## Read the table out of `.run/gdb-out.log`

`diag_run.sh`'s stdout is the last 40 lines only. For `phase4_prof.gdb` that drops phases 1..5
(including `build_track_geometry`, 26.7 ms) and the `=== vbi=… frozen=… build=… ===` header, so a
frame summed from stdout reads ~36 ms low, and two arms can be summed on different bases. Copy
`amiga/.run/gdb-out.log` to a per-arm file the moment a run ends, and assert `frozen=` and `build=`
from it before diffing (`GDBTAIL=400` widens the console view; the log is the record).

## The stale-build trap

The Amiga Makefile tracks neither `PROBES` nor the defines in `EXTRA_DEFINES` nor header
dependencies. `make clean` before every differently-flagged build and after editing a widely
included header; a stale object reproduces the previous build's number to the digit, or links a
working-but-wrong binary. Treat an unexplained regression after a header edit or a flag toggle as a
stale build until a clean rebuild rules it out.

## Release checks

- `amiga/quit_test.gdb` compares `$pc` with the breakpoint's address; break on out-of-line
  functions by address (`~PlatformAmiga` resolved to three locations, one inlined).
- Size a run with `CHIP_KB`/`FAST_KB` (slow RAM through `EXTRA_ARGS="--slow_memory=512"`).
- Kickstart 1.3 runs under WHDLoad's kick13 (`tools/test_whdload.py`, [whdload.md](whdload.md)).
