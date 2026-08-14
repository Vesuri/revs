# Revs — BBC Micro → Amiga port

Reimplementing Geoff Crammond's 1985 BBC Micro F1 simulation *Revs* (Acornsoft) on the Amiga
from the game binary (`revs.ssd`). Pipeline: decompile (Ghidra) → transliterate 6502 → C →
abstract hardware → platform backends. **Faithful 1:1 port** — parity before improvements;
validate against the 6502 + a real BBC emulator, NEVER against the dev-host backend.

⚠ **This project is NOT binary-only, unlike the Atari port.** A complete, buildable, fully
annotated source *reconstruction* of BBC Revs exists (Mark Moxon, <https://revs.bbcelite.com/>),
which largely pre-solves the postmortem's two highest-leverage items — the entry-point sweep and
the naming pass become **cross-checks against a reference** rather than open-ended searches.
It carries **no licence** (commentary intertwined with copyrighted game code), so use it as a map
and never copy from it. **Read `docs/reference-sources.md` before any disassembly work.**

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
| 1 | ✅ **The BBC reference loop is built and DRIVES** (`make refloop`) — the memory image is proven byte-correct on all five tracks, and a real BBC now races and renders. | `docs/bbc-reference-loop.md` |
| 2 | **Exhaustive entry-point sweep**, before a line of C is generated. Every indirect jump, RTS-dispatch table and OS vector seeded — now *cross-checked against* the reference reconstruction rather than searched blind. | `docs/entrypoint-sweep.md` + `docs/reference-sources.md` |
| 3 | **Transpiler emits clean C** before mass-generating. One transpiler improvement upgrades the whole corpus; late is pure tax. | `docs/transpiler.md` |
| 4 | **Profile an end-to-end skeleton on the real A500** before choosing what to optimise, and before setting any performance target. | `docs/perf-method.md` |

Two more that are already done, and must stay done:
- **The validation harness cannot pass vacuously, hide endianness, or excuse a live exit
  register** — designed in before twin #1 (`docs/validation-harness.md`).
- **The faithfulness seam rule is written down** (`docs/faithfulness-seam.md`) because it is a
  call you make hundreds of times.

## The source binary

`revs.ssd` — 200 KB single-sided 80-track Acorn DFS image, title "REVINST", `*OPT 4,3`.
**Target release: *Revs Plus Revs 4 Tracks*, © Superior/Acornsoft 1986** (user decision) — a
genuine commercial release: five circuits on one engine, with Computer Assisted Steering.

| File | Load | Exec | Length | What |
|---|---|---|---|---|
| `!BOOT` | — | — | `$30` | `*BASIC` / `PAGE=&1900` / `*FX21` / `CHAIN "REVINST"` |
| `REVINST` | `$1900` | `$8023` | `$2C40` | BASIC — instructions, banner, keys |
| `REVSMEN` | `$1900` | `$1900` | `$433` | the track menu; per track does `*LO.<TRACK>` then `*/REVS2` |
| `5TRSCRN` | `$7C00` | `$7C00` | `$400` | MODE 7 teletext title screen the menu `*LOAD`s |
| **`REVS2`** | `$1200` | `$1200` | `$5E00` | **the 24 KB machine-code engine** — one binary, all five tracks |
| `SILVER` | `$70DB` | **`$0000`** | `$739` | Silverstone (the 1985 circuit) — **passive data** |
| `BRANDS` | `$70DB` | **`$70DB`** | `$73A` | Brands Hatch — **executable** |
| `DONING` | `$70DB` | **`$70DB`** | `$73C` | Donington Park — **executable** |
| `OULTON` | `$70DB` | **`$70DB`** | `$739` | Oulton Park — **executable** |
| `SNETTER` | `$70DB` | **`$70DB`** | `$738` | Snetterton — **executable** |

⚠⚠ **The expansion track files PATCH THE ENGINE at runtime**, and the exec addresses above are the
binary's own evidence for it: Silverstone's data has exec `$0000` (not executable), while all four
expansion tracks have exec `$70DB` — they are *programs*. Each carries hook code
(`ModifyGameCode`, `CallTrackHook`, `HookFieldOfView`, `HookFlattenHills`, `HookJoystick`, …) that
modifies the game code as the engine starts, and they generate track geometry at runtime. So
**the bytes the engine executes differ per track**, this is self-modifying code by construction
(→ `revs_manual.c` stubs), and `disasm/revs_mem.bin` is the **pre-patch** state. Details:
`docs/reference-sources.md`.

⚠⚠ **`disasm/revs_mem.bin` is NOT what the engine executes — disassemble
`disasm/revs_runtime.bin` (`make runtime`) instead.** REVS2 unpacks itself before running: the
entry page self-copies to `$7900`, a checksum-verified `$5300`↔`$70DB` swap runs, then five block
moves, the last of which the stub patches into a zero-filler, and `JMP $63BD` enters the unpacked
engine. `tools/relocate.py` replays it, verified against a real BBC (9640 of 10168 changed bytes
explained). **Every address in `disasm/symbols.csv`, `ghidra_scripts/entrypoints.csv` and the docs
is a runtime-image address.** Full mechanism and traps: `docs/static-map.md`.

⚠⚠ **…and `revs_runtime.bin` is still not ALL of it: there is a SECOND unpack.** `copy_dash_data`
(`$18EA`) assembles `$7B00-$7FFF` — 1280 bytes of live code, incl. the wing mirrors — at *runtime*
from the tails of 41 `$80`-spaced blocks at `$3000`, and **stows it back** before returning to
MODE 7, so the page is empty in every static image and every out-of-race RAM dump. `$16E3` builds
it; `$16E6` calls into it. `make dashcode` replays it into `disasm/dashcode.txt`, which `make gen`
now ingests **by default** (`make gen DASHCODE=0` opts out). ⚠ The page is also the **MODE 7
screen** — the same 1 KB, time-multiplexed — and it is heavily self-modifying (two unrolled column
chains steered by 42 patch sites). Full write-up: `docs/static-map.md` §Open items 6 and 10.

`revs_mem.bin` is still built by `tools/ssd_load.py` (default SILVER; `make image TRACK=BRANDS`)
and is the honest record of what the *loader* produces, plus the input to the replay.

ℹ The 1986 engine differs from the **1985** single-track release in 974 of 24064 bytes (same
length). That is an authentic Superior/Acornsoft revision, not a repack — worth knowing only so
that a 1985-era reference (including the annotated reconstruction) is read with it in mind.

Documented keys: `L`/`+` steer, `S` throttle, `A` brake, `T` starter, `Q` gears up, `TAB` gears
down, `SPACE` amplify steering, `SHIFT+f0` return to pits, `SHIFT+f1` keyboard, `SHIFT+f2`
joystick. **Amiga input: mouse + keyboard** (user decision) — the BBC's own keyboard mode is the
faithful precedent; mouse replaces the uPD7002 analogue axis.

## Build / run / debug

### Host (macOS dev) — from repo root
```
make                       # build/revs — HEADLESS by design, no renderer (see below)
make validate              # the native-twin byte-exact differential
make validate FN="name"    # only matching tests — prefer this
make endian-lint           # fail if mem[] is aliased as a wide pointer
make gen                   # regenerate src/gen from listing.txt + dashcode.txt (DASHCODE=0 skips)
make image                 # rebuild disasm/revs_mem.bin from revs.ssd
make runtime               # ⭐ replay the engine's self-unpack -> disasm/revs_runtime.bin
make sweep                 # the entry-point sweep report -> disasm/sweep.txt
make refloop               # ⭐⭐ RACE A REAL BBC under jsbeeb -> tmp/bbcref (the visual ground truth)
make mode7                 # ⭐ the MODE 7 front end vs a real BBC, byte for byte (PPM=tmp/m7 to look)
make mode7-fixture         #   ...re-record that fixture off jsbeeb
make font                  #   regenerate the MODE 7 character generator (checked in)
```

⭐⭐ **`make refloop` is the visual ground truth, and it DRIVES** (2026-08-14): it boots `revs.ssd`,
answers the front end, starts the engine, engages first gear and drives, then dumps both the BBC
frame buffer and **what the real 6845 + Video ULA actually displayed**. Use it to settle any
"faithful or port bug?" pixel question — never the host backend. It also prints the *measured*
band schedule, which confirms `bbc_screen.h`'s derived boundaries against real hardware. What had
blocked it for two days was NOT key injection (that always worked): jsbeeb's default `FakeVideo`
never raises vertical sync, so the engine spun forever at `$4E11` (`BIT $FE4D`). Full write-up:
`docs/bbc-reference-loop.md`.

⚠ **The host build deliberately has NO renderer.** The Atari port's SDL backend was an
approximation of the real machine, and hours went into bugs that were only ever bugs in the
approximation. Visual ground truth here is jsbeeb/b2 on the real disc; performance ground truth is
FS-UAE + gdb on the Amiga build. The host build exists for `make validate` and host-side algebra
proofs. Full rationale: `src/platform/host/PlatformHost.h`.

### Amiga cross-build (m68k-amiga-elf-gcc) — from `amiga/`
```
. env.sh        # put the ~/.local Amiga toolchain on PATH (source it, SAME shell command)
make            # build out/Revs.exe (+ Revs.elf; runs the muldiv audit on every link)
make STRAIGHT_TO_RACE=1   # ⭐ boot straight into the race — see below
./run.sh        # boot in FS-UAE (Kickstart 3.1; left mouse button quits)
./debug.sh      # source-level debug via the FS-UAE GDB stub (prints its $DEBUG_PORT)
./diag_run.sh N # headless probe run for N seconds (needs a PROBES=1 build)
```

**Never `pkill fs-uae` / `pkill gdb`** in these scripts or by hand: several Amiga projects run
their own emulator at the same time.  The run/debug/probe scripts source
`~/.local/share/amiga/fsuae_common.sh` (shared, outside every repo; `$FSUAE_COMMON` overrides the
path), which kills only the pid this directory's previous run recorded in `.run/fsuae.pid` and
gives each project its own gdb-stub `$DEBUG_PORT`.  Stop a stranger's emulator by pid, or not at
all.

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

**Verified working now:** the genuine entry chain runs on the target — gdb shows
`engine_main → engine_init → front_end_menus → FUN_655C → FUN_16DC → platform_render_frame`, the
50 Hz body ticks in the real VERTB ISR, and `FPSCOUNT=1` reads **≈2.2 FPS** (Phase 4 baseline).
Two committed gdb scripts: `amiga/phase4_fps.gdb` (segmented framerate + liveness state) and
`amiga/phase4_prof.gdb` (main-loop phase shares, PROBES build).

⭐ **The port RENDERS as of Phase 5** — `src/platform/bbc_screen.h` is the display model (read it
before any visual work) and `amiga/screen_dump.gdb` + `tools/amiga_ppm.py` dump what the target is
actually showing and decode it on the host (`--planes=N --height=N` — there are now TWO display
configurations and the decoder must be told which).

⭐⭐ **THE MODE 7 FRONT END RENDERS** (2026-08-15), so a plain build is no longer a black screen:
`src/platform/teletext.*` is the model and `make mode7` proves it byte-for-byte against a real BBC
(1024/1024 on the target too). Two things to know before touching it. **MODE 7 does NOT use the
MOS font** — `vdu_char_def` `$5092` branches on `$64` bit 7 and the MODE 7 arm calls OSWRCH so the
**SAA5050** draws the cell; the `OSWORD 10` calls belong to the RACE view (that font is still
open). And **the front end never reaches `$1701`**: `menu_wait_key` spins at `$6577`, which is now
its own `SPINWAIT_HOOKS` paint hook — without it the page is byte-perfect and the screen is black.
MODE 7 is a SECOND display configuration (320x250, three bitplanes, its own copper list) so the
race view's display DMA is untouched; `amiga/mode7_dump.gdb` dumps it.

⭐ **`make STRAIGHT_TO_RACE=1` boots into a Silverstone PRACTICE session with the engine running
and in first gear, then hands the keyboard to the player** — the fast way into the race without
walking the (now rendered) front end. It skips **no** game code: practice needs
exactly ONE menu answer (`$63F7` `1 PRACTICE 2 COMPETITION`; option 1 stores `$5F3B = $FF` at
`$6401` and enters the session at `$6407`), so `src/platform/autorun.cpp` just answers it the
instant it is asked, then SPACE for `SPACE BAR TO CONTINUE`, `T` for the starter (`$4978`) and `Q`
for first gear. Class / qualifying duration / driver names / ANOTHER-START are all on the
COMPETITION branch and genuinely never reached. Verify with `amiga/straight_to_race.gdb`.
⚠ Combine with `FPSCOUNT=1` and the script holds the throttle instead of handing over — a perf
window with a **moving** car, which is a different workload from every baseline below.

⚠ **Every global a committed `.gdb` script reads must be listed in `PROBE_SYMS` (`amiga/Makefile`).**
`--gc-sections` drops an unreferenced counter, and gdb then resolves the name into `.text` and
prints **instruction bytes as a value** — a fake measurement, not an obvious zero. `make
probe-audit` runs on every link and fails the build otherwise. (`__attribute__((retain))` does not
work here — it is ignored on this target.)

## Reference docs — READ ON DEMAND (this file stays small on purpose)

Hard-won detail lives in `docs/`, not here. **Read the relevant one BEFORE working in its area.**

| Doc | Read it when |
|---|---|
| **`docs/postmortem.md`** | **Early, once, in full.** The retrospective this whole project is built on |
| `docs/phases.md` | Deciding what to work on next; the gating between phases |
| **`docs/reference-sources.md`** ⭐ | **Before any disassembly work.** The existing annotated reconstruction, its licence limits, and what is actually on this disc |
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
| **`docs/static-map.md`** ⭐ | **What the binary actually IS — the Phase 2 findings.** The self-unpack, the entry-point sweep, the hardware and MOS inventories, the per-track hooks, the self-modifying regions |
| `docs/rename.md` | A function's name contradicts its behaviour (append to it — see conventions) |

## Architecture

`disasm/symbols.csv` is the **source of truth for names** (the transpiler reads it; never
hand-rename in generated files).

| File | Role |
|---|---|
| `tools/ssd_map.py` / `ssd_load.py` | DFS catalogue dump / post-load memory image builder (per-track) |
| `tools/transpile.py` | The transpiler. Reads `disasm/listing.txt` + `symbols.csv`. ⚠ still carries Atari specifics — see `docs/transpiler.md` §Porting checklist |
| `src/gen/revs_gen.c` | Generated 6502→C transliteration (regenerated; do NOT edit by hand) |
| `src/gen/revs_manual.c` | Hand-written stubs for self-modifying routines |
| `src/gen/revs_native.c` | FAITHFUL native twins (idiomatic C `_core` + 6502-ABI shim), `make validate`d, linked into BOTH backends |
| `src/platform/amiga/revs_native_amiga.cpp` | Genuinely Amiga-only, unvalidated code |
| `src/platform/mos.cpp` | The MOS (Acorn OS) call layer — ⚠ a FLOOR, not a closed surface: OSBYTE 0, OSWORD 0 and OSWORD 7 were all found by RUNNING it |
| **`src/platform/bbc_screen.h`** ⭐ | **THE DISPLAY MODEL** — geometry, pixel format and the five raster bands, derived and cross-checked. Read before touching anything visual |
| `src/platform/amiga/RevsScreen.*` | BBC frame buffer → 2 bitplanes + the copper palette bands |
| `src/platform/amiga/RevsInput.*` | Mouse + keyboard onto the game's own two input paths |
| **`src/platform/teletext.*`** ⭐ | **THE MODE 7 MODEL** — the MOS VDU driver + the SAA5050. Read before any front-end work; it carries the measurements. `teletext_font.h` is generated |
| `src/platform/bbc_hw.cpp` | The BBC hardware model behind `bus_read`/`bus_write`, and the IRQ1V shim |
| `src/platform/autorun.cpp` | Scripted keyboard for unattended runs; without it a headless run measures a menu spin |
| `src/platform/probe.cpp` | Main-loop phase brackets (PROBES only) — the hot-function profile |
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

**Amiga specifics:** the INTB_VERTB vector is taken over wholesale (so the handler clears INTREQ
itself and `WaitTOF()` is unavailable). ⭐⭐ The real VERTB ISR does the **copper work only** and
*counts* fields; the game's 50 Hz body is drained from main-loop context at the engine's own frame
hook (`$1701`) and frame-wait spin (`$1760`) — because **the body DRAWS** (frame buffer
`$6E00-$70FF`, display lines 120-143) and running it in the ISR meant ~50 scene changes per painted
frame under a rasteriser that takes one: measured 238/239 frames torn, 327 decode mismatches, and
the horizon's green/black runs. Now 0/0/0. `make BODY_IN_ISR=1` reproduces the old model;
`amiga/fill_catch.gdb` is the detector. Full reasoning: `docs/amiga-arch.md` §the game body.
The copper owns the display. Spin-wait points in transpiled code become hooks that drive one real
Amiga frame. `bus_write` to BBC hardware is largely ignored on Amiga.

## Performance

**Target: 50 FPS on an A500. Floor: 25 FPS** (user decision; reachability unknown). These are
*displayed* frames (`50 * g_fpsFrames / g_vbiCount`). The **50 Hz sim tick is separate and not
negotiable** — the game body is a VERTB-ISR interrupt, so 25 FPS means painting every other frame
with the simulation still at full rate.

⭐ **BASELINE: 0.87 FPS RENDERED** (2026-08-14; 0.78 before the two-level-RTS fix below, which
stopped the road-span chains over-plotting) **/ 1.46 FPS unrendered** — ~29× short of the floor.
Phase 5 draws now, and rendering roughly halves the frame: two thirds of that cost is display
DMA against a program in chip RAM (structural on a stock A500), one third the frame-buffer
decode (~250 ms).

⚠ **Quote a framerate ONLY from `GDBSCRIPT=fps_series.gdb`** (in-program sampling, no gdb stop
inside the window). `fps_seg.gdb`'s conditional breakpoints halted the machine at every frame
and read **0.02 where the truth was 0.78** — a 30× error that reads as a catastrophic
regression. Every framerate taken before 2026-08-13 came from that instrument.

✅ The **"intermittent stall"** that used to freeze `g_fpsFrames` part-way through a run is
SOLVED, and it was a deterministic hang: `$2F7E`'s `TSX/INX/INX/TXS` + `RTS` returns **two levels
up**, which is how the unrolled road-span chains exit, and the transliteration modelled `TXS` as
a register write — inert on the C call stack. The chain then spun on `ADC $83 / BCC` with `$83`
== 0. ⭐ **A 6502 idiom that touches the STACK POINTER has no C equivalent and is dropped
silently** — suspect that class first for any hang inside generated code. `make gen` now fails on
any *other* `TSX/INX/INX/TXS` (`report_stack_drops`). Full write-up: `docs/perf-method.md`.

⭐⭐ **THE HOT PATH IS RASTERISATION, NOT PHYSICS** (re-measured 2026-08-13). Top three of the
main loop's 24 calls are **76.2%**: `$7BE2` **36.1%** (the dashboard), `$1A20` **21.1%** (the
road rasteriser), `build_road_edge_lists` `$24F6` **19.0%** (the road-geometry projection pass —
it builds the very edge lists `$1A20` draws, so **those two rows are ONE subsystem, 40% of the
frame, build-then-draw**). `$46A1` is 6.6%; the display-frame wait is its own phase 25 at 2.5% (port overhead, not engine
work). ⚠ **This REVERSES the Phase 4 headline**, which had `$46A1` at 24.6% and
concluded "physics and geometry, not rasterisation" — that table was taken with a phase-bracket
clock that wrapped every display frame and accounted for only 4% of the frame. Every share
published before 2026-08-13 is void; `docs/phases.md` Phase 6's premise went with it. Full
table, the defect, and the lesson: `docs/perf-method.md` §Where the time goes.

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
  ⚠⚠ **"In the VBI ISR" ≠ "in the vblank":** this handler runs the game's 50 Hz body, so anything
  after it lands 100+ scanlines into the display (measured 46-149). The copper work goes FIRST in
  the handler, and `g_beamPresentsLate` (`amiga/beam_watch.gdb`) must stay 0. A rebuilt copper
  WAIT behind the beam blocks the copper for the field and skips every band after it — that was
  the horizon's black/green runs, and no frame-boundary dump can see it. (`docs/amiga-lessons.md`)
- **Work in the vblank ISR is capped at ONE FRAME.** Over that you silently drop a displayed
  frame, and the dropped frame is what the user reports — not the cost.
- **`mem[]` is little-endian; the Amiga is big-endian; the host is little-endian.** Never alias
  `mem[]` as `uint16_t*`/`uint32_t*` for a general value — it reads correct on the host and
  byte-swapped on the target, so `make validate` stays green while the Amiga renders garbage.
  `make endian-lint` guards this.
- **An interrupt handler that leaves a register untouched ⇒ the copper list must too.** But every
  write-only register is still per-scene state with a named owner (`docs/amiga-lessons.md`).
- **The ISR shim must reproduce the MOS's IRQ entry, not just call the handler.** `irq1v_handler`
  recovers the interrupted **A from `$FC`** — which only the MOS's `STA $FC` ever wrote — so
  omitting it silently zeroed A on every return. Measured contract (`make refloop --irq-abi`, 2858
  engine-context interrupts): **A, X and Y are all preserved.** It is now asserted at the seam on
  both backends (`g_irqClobberCount`); keep it at 0. ⚠ Invisible on the host, where the ISR fires
  at a controlled point — it only bites where a real VERTB preempts mid-routine.

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
