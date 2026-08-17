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
make determinism           # ⭐ the WHOLE-CORPUS differential: 300 frames of a pinned race,
                           #   all 64 KB byte-compared.  The ONLY check that covers a change
                           #   to the transpiler / cpu.h / the memory model, because those
                           #   change `validate`'s oracle too.  `make determinism-record` first
make endian-lint           # fail if mem[] is aliased as a wide pointer
make gen                   # regenerate src/gen from listing.txt + dashcode.txt (DASHCODE=0 skips)
make image                 # rebuild disasm/revs_mem.bin from revs.ssd
make runtime               # ⭐ replay the engine's self-unpack -> disasm/revs_runtime.bin
make sweep                 # the entry-point sweep report -> disasm/sweep.txt
make refloop               # ⭐⭐ RACE A REAL BBC under jsbeeb -> tmp/bbcref (the visual ground truth)
make mode7                 # ⭐ the MODE 7 front end vs a real BBC, byte for byte (PPM=tmp/m7 to look)
make mode7-fixture         #   ...re-record that fixture off jsbeeb
make font                  #   regenerate the MODE 7 character generator (checked in)
make sound                 # ⭐ the MOS SOUND SCHEDULER vs a real BBC, tick for tick (VERBOSE=1)
make sound-fixture         #   ...re-record the MOS sweeps off jsbeeb
make sound-fixture-race    #   ...and REVS'S OWN sound out of a real driving race
make tracks                # ⭐⭐ the circuit installer, 64K byte-exact per circuit (the DATA path)
make track-run             # ⭐⭐ ...and the CODE path: race each circuit, require its hooks to RUN
make track-smc             #   the per-circuit SMC surface as EXTENTS (EMIT=1 regenerates the table)
make track-patch           #   what each circuit's ModifyGameCode writes (VERIFY=1 vs a real BBC)
make trackmenu             # ⭐ the CIRCUIT MENU vs the real REVSMEN, byte for byte (PPM=tmp/tm)
make trackmenu-fixture     #   ...re-record those pages off jsbeeb
make titlescreen           #   regenerate the embedded 5TRSCRN page (git-ignored: disc bytes)
```

⚠ **`make tracks` and `make track-run` answer DIFFERENT questions and you need both.** `tracks`
proves the bytes land; `track-run` proves the circuit's own code executes. An expansion circuit can
install byte-perfectly and then run Silverstone's control flow over its geometry — not a crash, and
invisible to any byte diff of the install. `g_trackHookCalls` is the number that separates them.
⚠⚠ And the window is the whole measurement: a plain build reports `hook calls 0` at frame 40 because
frame 40 is still MODE 7 — the hooks are only reached once the race starts, hence `STRAIGHT_TO_RACE`.

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
./run.sh        # boot in FS-UAE (Kickstart 3.1; CTRL + left mouse button quits)
./debug.sh      # source-level debug via the FS-UAE GDB stub (prints its $DEBUG_PORT)
./diag_run.sh N # headless probe run for N seconds (needs a PROBES=1 build)
EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=x.gdb ./diag_run.sh 60   # ⭐⭐ ~4.9x faster, same numbers
```

⭐⭐ **Put `EXTRA_ARGS="--warp_mode=1"` on every probe run.** FS-UAE then runs the emulated machine
~4.9× faster than real time, so 60 s of wall clock buys what used to need 200+.  It changes **no**
measurement this project takes, and that is verified rather than assumed: FPS is
`50 * g_fpsFrames / g_vbiCount` and the phase clock is beam ticks — both ratios of *emulated*
quantities, so host wall-clock speed cancels.  Confirmed by running one build with and without warp
(identical per-segment FPS at matched vbi).

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

⭐⭐ **THE PORT MAKES SOUND** (2026-08-15).  Revs never touches the SN76489 — every note is an
`OSWORD 7` block plus one `OSWORD 8` envelope — so the port reproduces the MOS's **scheduler**:
`src/platform/sound.*` (faithful, `make sound` diffs it against a real BBC tick for tick, 0/8918 and
0/8083) and `src/platform/amiga/RevsAudio.*` (Paula only).  Read `sound.h` before touching pitch,
amplitude or envelopes and `RevsAudio.h` before touching Paula; `docs/bbc-hardware.md` §Sound has
the five things the measurement settled.  ⚠ The engine note ramps ~50x too slowly and that is the
FRAMERATE (`sfx_trigger_random` is a main-loop call), not an audio bug.  Verify with
`GDBSCRIPT=sound.gdb ./diag_run.sh 70` on a `STRAIGHT_TO_RACE` build — a run that never starts the
engine measures silence.

⭐⭐ **THE CIRCUIT MENU IS THE PORT'S OWN PAGE, AND IT IS STILL MEASURED** (2026-08-16).  `REVSMEN`
is BASIC, so `src/platform/trackmenu.*` is port-authored — and `make trackmenu` diffs it against the
**real REVSMEN** recorded off jsbeeb (6 pages, byte-exact), while `tools/trackmenu_check.py` does the
same for a page dumped off the Amiga (22 of 22 shared rows, `amiga/trackmenu.gdb`).  Three things the
fixture settled that the BASIC listing would have got wrong: a selection changes **two** bytes not
three (column 6 already holds `$9D`), the title dwell is a **measured** 273 display fields, and
`*LOAD 5TRSCRN` is exactly a memcpy.  One deliberate divergence: a **sixth option, NURBURGRING**
(user decision), so the differential runs at `TM_OPTIONS_FAITHFUL` = 5 and the sixth row's *routing*
is what gets checked instead.
⚠⚠ **The menu is now the SINGLE circuit installer** — `revs_track_boot()` no longer runs at startup,
because installing is **not idempotent across circuits** (`install_data()` cannot undo the previous
circuit's patch bytes) and a default-then-choice sequence would leave 54 of one circuit's bytes in
another's engine.  A second install of a different circuit is refused and counted
(`g_trackOverinstalls`); `revs_track_forget()` is how a caller that restored the boot image says so.
⭐ And the 50 Hz body is SUSPENDED while the menu is up (`Revs::setFrontEnd`): the menu runs ~5.5 s
before `engine_main`, so counting those fields would hand the engine a 200-tick backlog to run in one
burst and fill `g_bodyTicksDropped` with drops that mean nothing.

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
| **`docs/direct-bitplane-plan.md`** ⭐ | **Before touching any plotter, any dashboard work, or any Phase 6 asm.** Rendering DIRECT to bitplanes instead of decoding a BBC-shaped buffer — the ~250 ms of pure port overhead, the layout choices, how the decode becomes the ORACLE, and §8 the SPRITE lever (the BBC had none; the cockpit could be static) |
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
| **`src/platform/sound.*`** ⭐ | **THE SOUND MODEL** — the MOS sound scheduler + the SN76489 state it drives, all of it measured off a real MOS.  Read before any pitch/amplitude/envelope work |
| `src/platform/amiga/RevsAudio.*` | That chip state → Paula: waveforms in chip RAM, the period conversion, the stereo placement.  Amiga-only, unvalidated by construction |
| **`src/platform/teletext.*`** ⭐ | **THE MODE 7 MODEL** — the MOS VDU driver + the SAA5050. Read before any front-end work; it carries the measurements. `teletext_font.h` is generated |
| `src/platform/bbc_hw.cpp` | The BBC hardware model behind `bus_read`/`bus_write`, and the IRQ1V shim |
| **`src/platform/trackmenu.*`** ⭐ | **THE CIRCUIT MENU** — port-authored (REVSMEN is BASIC) and validated against a recorded real page anyway.  The model is shared; only the Amiga drives it |
| `src/platform/autorun.cpp` | Scripted keyboard for unattended runs; without it a headless run measures a menu spin.  `REVS_AUTORUN_BUILD` is the one predicate for "this build drives itself" |
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

⭐⭐ **BASELINE: 2.63 FPS RENDERED** (2026-08-17, `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` +
`fps_series.gdb`) — **0.96 → 1.56 → 1.72 → 1.77 → 1.96 → 2.63, from five changes, and NONE was an
algorithm.**

**5. ⭐⭐ THE RASTER-BAND RECORD REUSE (+28%, control 2.05 → 2.63).**  The biggest row (the 50 Hz
body, 141 ms / 26%) was **96% machinery**: one field cost 6113 µs and only 233 µs of it (`$52A4`)
was game work.  ⭐ The five band interrupts **do not draw** — each repaints the Video ULA for the
band about to be scanned, which the BBC does on the CPU for want of a copper, and **this port
already runs them on the copper**: the arms only produce a RECORD that `RevsScreen` turns into
copper WAITs.  That record is a pure function of five palette tables plus the horizon, and
`MoveHorizon` is a *main-loop* routine — so 97.5% of fields rebuild an identical answer.
`Platform::fireIrq1vField()` compares 43 input bytes and, unchanged, runs `$52A4` alone.  Drain
6113 → 1308 µs/tick, 176 → 27 ms/frame.  `make BANDSKIP=0` is the control.
⚠⚠ **Its first sabotage PASSED** — the host's 64 KB differential is only ~36 *fields* and the
horizon never moves in one, so a skip that is never wrong on static inputs looked correct.  Hence
`make BANDCHECK=1` (0/14263 on the target, sabotages 21 and 22).  ⚠ Diagnosing the row at all
needed a bracket **per band arm** plus an **empty-bracket control**: the published "~810 µs a call,
unexplained" was an average over five arms that do different jobs.
⭐⭐ **`EXTRA_ARGS="--warp_mode=1"` runs FS-UAE ~4.9× faster with no effect on any of these numbers**
(they are all ratios of emulated quantities; verified against a non-warp run).  Use it for every
probe — 60 s of wall clock replaces 200+.

**4. ⭐⭐ THE DIRTY-REGION DECODE (+10.7%, 1.77 → 1.96).**  Only 406 of 8320 frame-buffer bytes
change per painted frame, so `decode()` now compares each 8-byte **cell column** (two aligned
longwords) against a per-buffer shadow and converts only what moved — a mean of **166 of 1040**
columns.  Oracle: `make DIRTYCHECK=1` re-decodes unconditionally into a copy and demands byte
equality (0/12, and it catches both sabotages).  ⚠⚠ **The control is the interesting row**:
`make DIRTY=0` reads **1.46**, not the old 1.77, because the cell-major restructure the test needs
costs ~18% on its own — the test is +34% against its own control and the *change* is +10.7%.  Two
measurements, and only the pair is honest.  Details: `docs/direct-bitplane-plan.md` §7c.  ⭐ The first two are the same lesson: *the port's biggest costs are in the
MACHINERY the transliteration is wrapped in, not in the game's algorithms* — look there before
optimising a loop.  ⚠ **The third is that lesson's limit**, and it is worth as much: where there
is no machinery to delete, a faithful twin buys almost nothing (item 3).

**2. `mem[]` was `volatile` and is not any more (+10%).** One qualifier on one array, and the
array is the whole engine: it forbade gcc every optimisation over `mem[]`, and cost 5.7 KB of code
too.  It was there for a *"VBI audio thread"* the predecessor project had and this one does not —
the VERTB ISR touches no `mem[]` since the 50 Hz body moved to main-loop context in Phase 5.
`src/cpu/mem_decl.h` has the argument; `make MEMVOL=1` (and `BODY_IN_ISR=1`, automatically) is the
control.  ❌ And the experiment that did NOT work, so it is not retried: dead-flag elimination in
the transpiler removed 63% of the flag writes and bought **0.3% of code size and nothing
measurable** — gcc's dead-store elimination was already doing it (`docs/perf-method.md`).

**1. Twin #1, `irq1v_handler` native (+62%).**  ~80 6502 instructions and ~400 ms of a 1038 ms
frame; the win is the *absence of the interpreter*, not better code: 48 of those instructions are
`STA $FE21`, and written as C gcc folds a whole 16-entry palette loop into sixteen immediate
stores.  ⚠⚠ It also found that `make validate` could not see hardware writes at ALL — four of
nine sabotages passed — so `diff_run` now diffs the hardware-write SEQUENCE too
(`docs/validation-harness.md` §a fifth), and **sabotage is a required step for every twin**
(`docs/phases.md` §1a item 9).

**3. ⚠⚠ TWIN #2, `$7BE2 dashboard_sweep` native — 1.71 → 1.77 FPS, and +3.6% IS THE FINDING.**
The biggest main-loop row (21.8% / 151 ms) is now a validated twin (700/700, 13 sabotages,
`determinism` byte-identical) and phase 24 fell to **131 ms** — 20 ms of 151.  ⭐⭐ **Because 131 ms
for 2093 units is ~420 cycles a unit against a ~27-instruction unit, this routine is
INSTRUCTION-FETCH BOUND in chip RAM, not interpreter bound** — twin #1 deleted machinery the BBC
never had, which is nearly free; here there is nothing to delete but iterations, and faithful C
cannot remove those.  ⚠ The first cut was *slower* than the transliteration (1.59): an unrolled
6502 chain bakes forty opcode-slot addresses in as constants, and rolling it into a loop pays to
derive what unrolling had given away.  ⭐ **So the dashboard is a REPRESENTATION target, not a twin
target** — `docs/direct-bitplane-plan.md` §7a from the other side: 2093 units run, ~83 bytes
change, so the win is not scanning, not scanning faster.

⭐ **Re-profiled after the band reuse (2026-08-17, 30 s warp run)**: `$7BE2` **131 ms / 30.5%**,
`$24F6` 61, `$1A20` 58, the decode 37, the 50 Hz drain **30 (was 176)**, `$1E15` 29, `$46A1` 28,
the vblank spin 23.  ⭐⭐ **The view pipeline — `$24F6` → `$1A20` → `$7BE2` — is 250 ms of a ~430 ms
frame (58%) and it is ONE subsystem**: the first two are *producers* writing source bytes into the
forty `$80`-spaced blocks at `$3000..$4380` (they write 95 frame-buffer bytes between them, all
inside the flat sky band), and `$7BE2` is the single *consumer*.  That shared structure is the lever.
⚠ The **VERTB ISR** is ~20 ms/frame (915 µs × ~22 calls) charged to whatever it preempts, so it
appears in no row — copper rebuild + bitplane swap + audio, internally unmeasured.
Full table: `docs/perf-method.md`.
⚠⚠ **Use 30-second warp runs, not longer**: a `STRAIGHT_TO_RACE` run leaves the track, resets, and
then nothing happens, so a longer run DILUTES the measurement with a static scene rather than adding
data (the 90 s table read the drain at 20 ms against 30 ms measured properly).
⚠ **`$4E5C irq1v_handler` is NOT "the 50 Hz game body"** despite what `symbols.csv`, `probe.h`,
`Revs.cpp` and `docs/amiga-arch.md` all say — it is the raster-band palette schedule and it draws
nothing; `$52A4` (a speed-rate XOR animation on display lines 120-143) is the field's only game
work.  That misnomer is why the row was carried as untouchable engine work.  → `docs/rename.md`.

⭐ *(superseded)* **0.87 FPS RENDERED** (2026-08-14; 0.78 before the two-level-RTS fix below, which
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

⚠⚠ **…and the FIX carried the sequel bug (2026-08-15): keeping the `TXS` register write next to
`UNWIND_SET()` LEAKED `S` by 2 per road-span exit.** Those two bytes are a return address, and
this model keeps return addresses on the C stack, so nothing cancels the `+2`: `S` climbed past
its `$F8` entry, **wrapped `$FF` → `$00`**, and pushes landed on `mem[$0100]` = `car_order`. A
COMPETITION race then hung in `check_car_pair`'s field walk — a frozen race view where no key
responds — while PRACTICE (which skips the multi-car path) looked fine. ⭐ **When a 6502 idiom
manipulates `S` to talk about RETURN ADDRESSES, model the control flow and leave `S` alone**:
modelling neither is a hang, modelling the register too is a silent leak. `g_stackLow` AND
`g_stackHigh` must both read inside `$F3..$F8`; `make STACK_TRAP=1` + `REVS_STACK_TRAP=<hex>` /
`REVS_STACK_CEIL=<hex>` prints one host backtrace at the first breach either way.

⭐⭐ **AND THE FIRST PHASE 6 ITEM IS THE REPRESENTATION, NOT THE ASM** (2026-08-16): the engine plots
into a BBC-shaped frame buffer in `mem[]` and `RevsScreen::decode()` converts 8320 bytes to bitplanes
every painted frame — **~250 ms of a ~1282 ms frame, and none of it work the BBC did**, plus ~2x the
render path's memory traffic.  Render DIRECT to bitplanes instead; asm written against the current
arrangement has to be rewritten after it.  ⚑ RoF shipped this (~339 → ~172 ticks/frame) and its own
verdict was "real but not transformative" — necessary, not sufficient.  Full plan, the layout choices,
and **the decode becoming the validated oracle**: `docs/direct-bitplane-plan.md`.  ⚠ Its §4 used to
call the live code inside the frame buffer a constraint on this change; it is not one (corrected
2026-08-16) — on the Amiga those bytes are just code in `mem[]`, and filling the sky instead of
decoding it *deletes* the band-boundary hazard rather than being limited by it.

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
