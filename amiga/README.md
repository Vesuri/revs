# Amiga development tools

Run these tools from this directory after `. ./env.sh`. Clean before changing
build flags. The normal build runs the complete front end; diagnostic builds can
use `STRAIGHT_TO_RACE=1` or `RACEPROPER=1` to reach a specific workload.

## Build and execution

```sh
make
./run.sh
./debug.sh
```

`run.sh` launches FS-UAE and `debug.sh` connects its debugger. For unattended runs,
follow [the headless guide](../docs/headless-fsuae.md). Read `.run/gdb-out.log`,
including its completion and coverage counters, rather than relying on the
terminal tail. Concurrent runs share `.run/` and must be kept separate.

## Profiling and captures

| Need | Script |
|---|---|
| Main-loop phase profile | `phase4_prof.gdb` |
| Displayed frame rate | `fps_series.gdb`, `fps_seg.gdb` |
| Statistical samples | `pcsample.sh` and `pcsample.gdb` |
| Instruction trace | `steptrace.gdb`, `race_steptrace.gdb` |
| Simulation clock | `sim_clock.gdb` |
| Interrupt timing | `isr_split.gdb`, `isr_watch.gdb` |
| Road, geometry and decode breakdown | `roadsplit.gdb`, `geosplit.gdb`, `decodesplit.gdb` |
| Race display and copper capture | `screen_dump.gdb`, `dualpf_dump.gdb` |
| Consecutive display captures | `fill_frames.gdb`, `fill_frames_plain.gdb` |
| MODE 7 capture | `mode7_dump.gdb`, `mode7_text.gdb` |
| Memory requirements | `mem_target.gdb` |

Use each script's header for its build flags and expected counters. The
`phase4_prof.gdb` name is historical; it remains the main profiling script.
Reports live in `../tools/`, including `pcsample_report.py`,
`steptrace_report.py`, `sim_clock_report.py` and `amiga_ppm.py`.

## Correctness probes

The retained `*_check.gdb`, `walkcheck.gdb`, `setupcheck.gdb`,
`terrain_asm_check.gdb` and `lowfullcheck.gdb` scripts exercise target-only
optimisations against their C controls. Other scripts cover input, sound,
track selection, sprites and display timing. They are not interchangeable:
use the matching feature flag and require a nonzero check count.

Start with [validation](../docs/validation-harness.md) and
[performance measurement](../docs/perf-method.md). Keep new one-off snapshots
in ignored scratch space; commit a probe when it offers a reusable diagnostic
or regression check.
