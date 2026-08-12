# Revs — BBC Micro → Amiga port

Reimplementing Geoff Crammond's 1985 BBC Micro F1 simulation *Revs* (Acornsoft) on the Amiga
**from a binary only** (`revs.ssd`, no source). Pipeline: decompile (Ghidra) → transliterate 6502
→ C → abstract hardware → platform backends. **Faithful 1:1 port** — parity before improvements;
validate against the 6502 + a real BBC emulator, NEVER against the dev-host backend.

Revs never received an Amiga port, so this is genuine preservation. Real vehicle dynamics + true
3D Silverstone on a 7 MHz 68000.

> **This project is the second run at a process that already worked once.** The Atari 8-bit →
> Amiga port of *Rescue on Fractalus!* (`~/Documents/Rescue on Fractalus`) shipped the same
> pipeline, and `docs/postmortem.md` is its retrospective: what to do differently, ordered by
> leverage. **Read `docs/postmortem.md` early.** Its one-sentence version: *build the discovery
> and validation infrastructure exhaustively up front instead of growing it reactively.*
>
> Everything in `docs/` marked ⚑ is inherited from that project — measured on the same target
> with the same toolchain, so it applies unchanged. Don't soften an inherited rule without a
> measurement that contradicts it, and don't re-derive one from scratch either.

## ⭐ The four things that must happen in order

The postmortem's whole argument is about sequencing. Doing these out of order is the known-expensive
failure mode, so they gate each other:

| # | Gate | Doc |
|---|---|---|
| 1 | **Build the BBC reference loop.** There is no `atari800` here — ground truth must be built. First job: prove `disasm/revs_mem.bin` is byte-correct against a real BBC. | `docs/bbc-reference-loop.md` |
| 2 | **Exhaustive entry-point sweep**, before a line of C is generated. Every indirect jump, RTS-dispatch table and OS vector seeded. | `docs/entrypoint-sweep.md` |
| 3 | **Transpiler emits clean C** before mass-generating. One transpiler improvement upgrades the whole corpus; late is pure tax. | `docs/transpiler.md` |
| 4 | **Profile an end-to-end skeleton on the real A500** before choosing what to optimise, and before setting any performance target. | `docs/perf-method.md` |

Two more that are already done, and must stay done:
- **The validation harness cannot pass vacuously, hide endianness, or excuse a live exit
  register** — designed in before twin #1 (`docs/validation-harness.md`).
- **The faithfulness seam rule is written down** (`docs/faithfulness-seam.md`) because it is a
  call you make hundreds of times.

## The source binary

`revs.ssd` — 200 KB single-sided 80-track Acorn DFS image, title "CAR", `*OPT 4,3`.

| File | Load | Length | Ends | What |
|---|---|---|---|---|
| `!BOOT` | — | `$2C` | — | `*BASIC` / `PAGE=&1900` / `*FX21` / `CHAIN "CAR"` |
| `Car` | `$1900` | `$23DB` | `$3CDB` | BASIC front end — "Revs / BBC Version 1 / Copyright (c) Acornsoft Limited 1985", keys, options |
| `REVS` | `$1900` | `$700` | `$2000` | BASIC + embedded machine code; ends with `*R.Revs2` |
| **`Revs2`** | `$1200` | `$5E00` | `$7000` | **the 24 KB machine-code engine** |
| `Revs1` | `$2000` | `$5AF` | `$25AF` | loads *inside* Revs2's range — order matters |
| `Silvers` | `$70DB` | `$739` | `$7814` | Silverstone track data |

⚠ **Two things about this image are open decisions, not settled facts** — see `PROJECT.md`
§Open decisions:
1. It is the **single-track (Silverstone)** release, not the four-track expansion the postmortem
   named as the target.
2. `disasm/revs_mem.bin` is composed by `tools/ssd_load.py` from a **hypothesised load order**.
   A DFS disc has no segment table (unlike an Atari `.xex`), the files overlap, and a BASIC loader
   decides the real sequence. **Until it is diffed against a real BBC, every address derived from
   it is provisional.**

Documented keys (from `Car`): `L`/`+` steer, `S` throttle, `A` brake, `T` starter, `Q` gears up,
`TAB` gears down, `SPACE` amplify steering, `SHIFT+f0` return to pits.

## Build / run / debug

### Host (macOS dev) — from repo root
```
make                       # build/revs — HEADLESS by design, no renderer (see below)
make validate              # the native-twin byte-exact differential
make validate FN="name"    # only matching tests — prefer this
make endian-lint           # fail if mem[] is aliased as a wide pointer
make gen                   # regenerate src/gen from disasm/listing.txt
make image                 # rebuild disasm/revs_mem.bin from revs.ssd
```

⚠ **The host build deliberately has NO renderer.** The Atari port's SDL backend was an
approximation of the real machine, and hours went into bugs that were only ever bugs in the
approximation. Visual ground truth here is jsbeeb/b2 on the real disc; performance ground truth is
FS-UAE + gdb on the Amiga build. The host build exists for `make validate` and host-side algebra
proofs. Full rationale: `src/platform/host/PlatformHost.h`.

### Amiga cross-build (m68k-amiga-elf-gcc) — from `amiga/`
```
. env.sh        # put the ~/.local Amiga toolchain on PATH (source it, SAME shell command)
make            # build out/Revs.exe (+ Revs.elf; runs the muldiv audit on every link)
./run.sh        # boot in FS-UAE (Kickstart 3.1; left mouse button quits)
./debug.sh      # source-level debug via the FS-UAE GDB stub (port 2345)
./diag_run.sh N # headless probe run for N seconds (needs a PROBES=1 build)
```

⚠ **`make clean` before any `PROBES=1` build and after editing a widely-included header.** The
Amiga Makefile tracks neither, so a partial rebuild links stale objects into a
**working-but-wrong** binary with silent runtime breakage. Treat any unexplained regression right
after a header edit or a `PROBES` toggle as a stale build first. Full text:
`docs/headless-fsuae.md`.

### Headless FS-UAE loop — **measure, don't theorize**
Drive FS-UAE + gdb yourself, no display interaction needed. On the Atari port this loop diagnosed
timing and render bugs precisely where static reasoning kept failing.
`. ./env.sh` (same shell command) then `amiga/diag_run.sh [delay]`, editing `amiga/diag_timing.gdb`
to print whatever globals / `mem[0xNNNN]` you need. Details and traps: `docs/headless-fsuae.md`.

**Verified working now:** a 25 s run reports `vbi=1075 painted=1054` — display takeover, 50 Hz
VERTB handler, copper list, frame pump and the embedded 6502 image are all live and readable from
gdb by name.

## Reference docs — READ ON DEMAND (this file stays small on purpose)

Hard-won detail lives in `docs/`, not here. **Read the relevant one BEFORE working in its area.**

| Doc | Read it when |
|---|---|
| **`docs/postmortem.md`** | **Early, once, in full.** The retrospective this whole project is built on |
| `docs/phases.md` | Deciding what to work on next; the gating between phases |
| `docs/bbc-reference-loop.md` ⭐ | Anything about ground truth, jsbeeb/b2, or trusting `revs_mem.bin` |
| `docs/entrypoint-sweep.md` ⭐ | Before generating C; whenever you find a dispatch table or vector |
| `docs/bbc-hardware.md` | Touching hardware, MOS calls, screen modes, or input |
| `docs/toolchain.md` | Running the pipeline: disc tools, Ghidra headless, the builds |
| `docs/transpiler.md` | Working on `tools/transpile.py`, or when generated-code shape surprises you |
| `docs/validation-harness.md` | Writing or trusting a `make validate` fixture |
| `docs/faithfulness-seam.md` | Deciding where a routine lives (validated twin vs Amiga-only) |
| `docs/perf-method.md` ⚑ | Quoting, sizing or judging ANY performance number |
| `docs/m68k-optimisation.md` ⚑ | Optimising a hot function or writing an asm twin (68000 rules) |
| `docs/amiga-lessons.md` ⚑ | Copper lists, sprites, the VBI, write-only registers |
| `docs/amiga-arch.md` ⚑ | The Amiga display/interrupt architecture decisions and why |
| `docs/headless-fsuae.md` ⚑ | Writing a probe, driving FS-UAE headlessly, or suspecting a stale build |
| `docs/method-lessons.md` ⚑ | How to work: measuring, bisecting, proving a reordering, recording findings |
| `docs/rename.md` | A function's name contradicts its behaviour (append to it — see conventions) |

## Architecture

`disasm/symbols.csv` is the **source of truth for names** (the transpiler reads it; never
hand-rename in generated files).

| File | Role |
|---|---|
| `tools/ssd_map.py` / `ssd_load.py` | DFS catalogue dump / post-load memory image builder |
| `tools/transpile.py` | The transpiler. Reads `disasm/listing.txt` + `symbols.csv`. ⚠ still carries Atari specifics — see `docs/transpiler.md` §Porting checklist |
| `src/gen/revs_gen.c` | Generated 6502→C transliteration (regenerated; do NOT edit by hand) |
| `src/gen/revs_manual.c` | Hand-written stubs for self-modifying routines |
| `src/gen/revs_native.c` | FAITHFUL native twins (idiomatic C `_core` + 6502-ABI shim), `make validate`d, linked into BOTH backends |
| `src/platform/amiga/revs_native_amiga.cpp` | Genuinely Amiga-only, unvalidated code |
| `src/cpu/` | 6502 register/flag model, the memory bus, the 68000 16-bit math helpers |
| `src/platform/` | `platform.h` abstraction + the C bridge; `host/` and `amiga/` backends |
| `tools/validate_native.c` | The `make validate` harness |

**Making a function native (the regen-safe seam):**
1. Add its address to `VALIDATE_FUNCS` in `tools/transpile.py`.
2. The transpiler emits its transliteration under a `__t6502` suffix (the validation **oracle**),
   and the plain name is linked from `revs_native.c`.
3. In `revs_native.c` write two halves: a typed idiomatic `<name>_core(...)` and a
   `void <name>(void)` 6502-ABI shim marshalling `mem[]`/`cpu` ↔ the core.
4. **Register a fixture in `validate_native.c`.** Step 1 alone only creates the oracle — the
   harness now *fails* if a validated name has no fixture, because a fixture-less PASS runs zero
   comparisons (`docs/validation-harness.md`).
5. `make validate FN=<name>` runs both on the same inputs and diffs full `mem[]` state.

**Which side of the seam:** `revs_native.c` = FAITHFUL, validated, both backends;
`revs_native_amiga.cpp` = genuinely Amiga-only. A faithful pure-`mem[]` routine that merely needs
a small Amiga variation **stays in `revs_native.c`** with the variation under
`#ifdef REVS_PLATFORM_AMIGA`. Full decision procedure: `docs/faithfulness-seam.md`.

**Amiga specifics:** the game's 50 Hz body runs in the *real* INTB_VERTB ISR (the vector is taken
over wholesale, so the handler clears INTREQ itself and `WaitTOF()` is unavailable). The copper
owns the display. Spin-wait points in transpiled code become hooks that drive one real Amiga frame.
`bus_write` to BBC hardware is largely ignored on Amiga.

## Performance

**No target is set yet — and that is deliberate.** Set one from a measured baseline on the real
A500 (Phase 4), not from a wish. The Atari port's retired "50 FPS is impossible without an
algorithm change" conclusion was disproven by hand-asm: the ceiling was GCC, not the algorithm.

The A500 is a 7 MHz 68000 and a frame is 20 ms — spending 10 ms on *anything* is half the budget.
Be conscious of absolute milliseconds always.

Three rules that must survive without opening `docs/perf-method.md`:
- **`make FPSCOUNT=1` + `GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200` is the ONLY way to quote a
  framerate**, and it over-reads wins — under ~3% is noise. Quote a static cycle count or a
  differential ratio as the win; quote FPS only as the standing baseline. ⚠ Per-iteration ("t/it")
  phase numbers are **not** a safer alternative — they carry ~±10% of trajectory noise and must
  never be diffed across builds. Use phase brackets for SHARES, FPS for PROGRESS.
- **Measure an asm twin with the in-process differential** (`make VERIFY=1 PROBES=1`), never
  cross-run: a render-speed change shifts the sim's trajectory and measures a different workload.
  `make FIXED_RNG=1` for every perf run.
- **Every framerate figure in an older note or commit is wrong — re-measure, don't quote.**

## Hard rules (violating these costs a day)

- **Faithfulness first.** Byte-identical twins: `make validate FN=<name>` must show **0 mem
  mismatch**. Validate against the 6502 + a real BBC emulator, not the host backend.
- **RAM is uniformly slow — there is no "fast RAM" on the target A500.** Optimise by reducing the
  NUMBER of reads/writes, never by moving data to a "cheaper" buffer. (`docs/m68k-optimisation.md`)
- **NEVER emit a 32-bit software mul/div** (`__mulsi3`/`__divsi3`/`__udivsi3`/`__modsi3`/
  `__umodsi3`) — the 68000 has none. Use `src/cpu/m68k_math.h`'s 16-bit helpers. `amiga/Makefile`
  audits every link (`muldiv-audit`); keep it clean.
- **Copper bitplane POINTER swaps happen in the VBI ISR, never mid-frame** — a torn pointer
  garbages the whole viewport for a frame. Colour-only pokes mid-frame are tolerable.
  `SPRxPT` operands are stricter still (the copper reads them at scanline 16).
  (`docs/amiga-lessons.md`)
- **Work in the vblank ISR is capped at ONE FRAME.** Over that you silently drop a displayed
  frame, and the dropped frame is what the user reports — not the cost.
- **`mem[]` is little-endian; the Amiga is big-endian; the host is little-endian.** Never alias
  `mem[]` as `uint16_t*`/`uint32_t*` for a general value — it reads correct on the host and
  byte-swapped on the target, so `make validate` stays green while the Amiga renders garbage.
  `make endian-lint` guards this.
- **An interrupt handler that leaves a register untouched ⇒ the copper list must too.** But every
  write-only register is still per-scene state with a named owner (`docs/amiga-lessons.md`).

## Working conventions

- **Commit directly to `main`** (no feature branches). Commit each fix as soon as it is confirmed
  to work — one logical change per commit.
- **Misnamed functions:** whenever a function's name clearly contradicts what it does, append it
  to `docs/rename.md` immediately (address, current name, actual behaviour, suggested name). Do
  not rename piecemeal in generated files — `disasm/symbols.csv` is the source of truth; batch-
  rename later via the transpiler. **On a binary-only project the names are your map.**
- **Newly-found dispatch targets / interrupt handlers:** add them to
  `ghidra_scripts/entrypoints.csv` the moment you find one — they are reachable only via indirect
  vectors, so Ghidra never finds them on its own. Record it immediately; don't defer.
- **Mark assumptions as assumptions.** `docs/bbc-hardware.md` uses **[ASSUMED]** vs **[DERIVED]**
  for exactly this reason. The postmortem's failure mode is an assumption calcifying into a
  documented fact; a measurement replaces the tag.
- **Keep this file small.** New hard-won detail goes in the matching `docs/` file (add a row to
  the index above if it's a new one), not here. This file is re-read in full on every turn of
  every session; `docs/` is read only when relevant.
- Ask the user at genuine decision points (they're an experienced retro-porter and want to steer
  architecture/scope choices).
