# Revs working instructions

Faithful BBC Micro to Amiga port of *Revs Plus Revs 4 Tracks* (1986).
Read [README.md](README.md) for the project and [docs/README.md](docs/README.md)
for the documentation index. This file contains standing rules only.

## Shared environment and knowledge

The Amiga toolchain, FS-UAE, Kickstarts, WHDLoad, Ghidra and volta come from AmigaXDev
(`make setup` there); `amiga/env.sh` sources `~/.local/share/amiga/env.sh`. Generic lessons live
in AmigaXDev and are edited there; this repo's docs keep what is specific to Revs.

| AmigaXDev doc | For |
|---|---|
| `docs/environment.md` | shared tools, Kickstarts and variables |
| `docs/playbooks/new-port-playbook.md` | port phases, static map and transpiler rules, release |
| `docs/reference/measurement-method.md` | instruments, A/B, pricing, when to stop |
| `docs/reference/validation-harness.md` | references, differentials, fixtures, sabotage, seam |
| `docs/reference/m68k-optimisation.md` | 68000 cost model and code shape |
| `docs/reference/amiga-hardware.md` | copper, sprites, dual playfield, interrupts, framework |
| `docs/reference/headless-fsuae.md` | launcher, gdb stub, diag runs |
| `docs/reference/port-toolchain.md` | flags, linking, audits, Ghidra, WHDLoad slave and release |
| `docs/native-port-validation.md` | crash dumps, installer updates, release archive checks |

## Working conventions

- Work on `main`, one logical change per commit. Preserve unrelated local changes.
- Use `docs/open-work.md` and `make todo` for outstanding work. Remove completed
  entries instead of appending session logs. Git history holds the implementation history.
- Keep measurements and technical arguments in the relevant subsystem document;
  keep this file short. Mark assumptions as inferred until verified.
- `disasm/symbols.csv` owns names. Never hand-edit generated names. Record unsettled
  names in `docs/rename.md`, with an address and a way to resolve the uncertainty.
- Add newly discovered indirect targets to `ghidra_scripts/entrypoints.csv`.
- Keep copyrighted discs, derived binaries, generated game data, captures, local
  dependencies and build outputs untracked. Preserve local discs and Ghidra projects.
- Gate a change by what it can affect. Documentation-only edits need link and syntax
  checks, not a full gameplay campaign. Do not change fidelity or scope as housekeeping.

## Build and validation

See `docs/toolchain.md` for prerequisites and generation. From the repository root:

```sh
make
make validate FN=name
make endian-lint macro-lint cpu-lint
make todo
```

For the Amiga, run from `amiga/` after `. ./env.sh`. Clean before changing build
flags; Make does not track their values. `make DIST=1` strips diagnostics and audits
the result. Root `make dist` builds the release package; see `docs/whdload.md`.

- The host is deliberately headless. It supports differentials and algebra proofs;
  it is neither visual nor performance ground truth.
- Use the real BBC under jsbeeb for fidelity (`make refloop`, `viewdiff`, `lockstep`,
  `lockstep-race`) and FS-UAE with the Amiga binary for target measurements.
- A change to the transpiler, CPU model or memory representation changes the
  validation oracle too. Record determinism baselines before changing them; compare
  parked, driving and other affected trajectories afterward. Never re-record a
  baseline merely to make a failure pass.
- Practice does not test other cars or race-only branches. Expansion hooks need a
  patched circuit. `tracks` checks installation; `track-run` checks execution;
  `viewdiff` checks the resulting picture. These answer different questions.
- Target-only assembly needs its in-process C differential and a nonzero check count.
  Exercise the affected path and a patched circuit when applicable. Use a few
  deliberate defects to establish that a new gate fails, then remove them.
- A fixture models reachable game inputs. Validate observable results; ignoring a
  scratch cell requires a written reader audit, including hooks and later passes.
  An accepted numerical departure uses a derived tolerance, not an ignore mask.
- A shim must reproduce every register/flag its 6502 callers read, even when the
  native core no longer needs it. A shared mistake in twin and oracle needs the real
  BBC to expose it. See `docs/validation-harness.md` and `docs/faithfulness-seam.md`.

## Source and architecture

- Read `docs/reference-sources.md` before disassembly. The annotated reconstruction
  is a reference map, not source to copy.
- Disassemble `disasm/revs_runtime.bin`, after `make runtime`, not the loader image
  `revs_mem.bin`. `make dashcode` reconstructs the second, time-multiplexed unpack at
  `$7B00`; `make gen` consumes its listing too.
- Generated files are listed in `src/gen/README.md`. Handwritten cores live in
  `revs_native.c`, live ABI seams in `revs_native_seam.c`, oracle-only shims in
  `revs_native_abi.c`. Keep computations typed; reconstruct ABI state at the boundary.
- Retain hardware effects. Pure RAM accesses should use known addresses directly;
  do not add bus dispatch to a path whose destination is statically RAM.
- `mem[]` is little-endian and the Amiga is big-endian. Do not alias it through wide
  pointers except documented uniform-byte operations. `make endian-lint` guards this.
- Shared zero-page cells have multiple tenants. Moving a value requires a transitive
  caller/reader audit; an expansion hook may re-enter transliterated code.
- Preserve MOS outputs, SMC traps, IRQ register state and relevant stack state.
  A no-op or fallback is not evidence that an unsupported path is safe.
- The copper pointer update runs first in the VBI ISR. The game's drawing interrupt
  body drains in main-loop context; it is suspended in the front end. Every spin-wait
  that exposes a screen is also a presentation point. See `docs/amiga-arch.md`.
- The default simulation is decoupled from painting. `SIMLEGACY=1` is a measurement
  control; the accepted true-ratio division and exact BBC mode are documented in
  `docs/faithfulness-seam.md`. `EXACTRATIO=1` is needed for BBC lockstep.

## Performance

The method is AmigaXDev `docs/reference/measurement-method.md`; this harness's recipes and
numbers are in `docs/perf-method.md` and `docs/m68k-optimisation.md`. Rules every change obeys:

- Clean up the C before proposing assembly; price the whole affected path from the target
  instructions and a measurement, never from source size or instruction count.
- Quote ms per frame from matched workloads (displayed FPS is quantised to `50/N`); keep practice,
  race, reset and front-end windows distinct. Read `amiga/.run/gdb-out.log`, check it belongs to
  this run, and assert its build flags, circuit, frozen window and counters.
- Use an in-process oracle for target-only optimisations; measure both the work removed and the
  replacement cost.
- The target is a 68000 A500: reduce memory accesses; no 32-bit software multiply/divide
  (`src/cpu/m68k_math.h`, the Amiga link audits). ISR copper work runs first and the ISR's work
  stays bounded.
- Read the measured dead ends in `docs/open-work.md` before retrying an optimisation; require a
  changed premise and a new measurement.
