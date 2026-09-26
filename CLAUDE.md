# Revs — BBC Micro → Amiga port

Reimplementing Geoff Crammond's 1985 BBC Micro F1 simulation *Revs* (Acornsoft) on the Amiga
from the game binary (`revs.ssd`). Pipeline: decompile (Ghidra) → transliterate 6502 → C →
abstract hardware → platform backends. **Faithful 1:1 port** — parity before improvements;
validate against the 6502 + a real BBC emulator, NEVER against the dev-host backend.

⚠⚠ **THIS FILE CARRIES STANDING RULES ONLY — IT IS NOT A LOG.** It is re-read in full on every
turn of every session, so nothing dated, no measurement history, no "X now works" achievement
notes, no war stories: a *rule* or a *pointer*, or it belongs in `docs/`. When something is
learned, write it in the matching `docs/` file and, if it changes how to work, add or amend one
line here. The same discipline governs `docs/rename.md` (a queue, never a log).

⚠ **This project is NOT binary-only, unlike the Atari port.** A complete, annotated source
*reconstruction* of BBC Revs exists (Mark Moxon, <https://revs.bbcelite.com/>), which turns the
entry-point sweep and the naming pass into **cross-checks against a reference** rather than
open-ended searches. It carries **no licence** (commentary intertwined with copyrighted game
code), so use it as a map and never copy from it. **Read `docs/reference-sources.md` before any
disassembly work.**

Revs never received an Amiga port, so this is genuine preservation. Real vehicle dynamics + true
3D Silverstone on a 7 MHz 68000.

> **This project is the second run at a process that already worked once.** The Atari 8-bit →
> Amiga port of *Rescue on Fractalus!* (`~/Documents/Rescue on Fractalus`) shipped the same
> pipeline, and `docs/postmortem.md` is its retrospective, ordered by leverage. **Read
> `docs/postmortem.md` early.** One sentence: *build the discovery and validation infrastructure
> exhaustively up front instead of growing it reactively.*
>
> Everything in `docs/` marked ⚑ is inherited from that project — measured on the same target
> with the same toolchain, so it applies unchanged. Don't soften an inherited rule without a
> measurement that contradicts it, and don't re-derive one from scratch either.

## ⭐ The four things that must happen in order

Doing these out of order is the known-expensive failure mode, so they gate each other:

| # | Gate | Doc |
|---|---|---|
| 1 | ✅ **The BBC reference loop is built and DRIVES** (`make refloop`) | `docs/bbc-reference-loop.md` |
| 2 | **Exhaustive entry-point sweep** before a line of C is generated — every indirect jump, RTS-dispatch table and OS vector seeded, cross-checked against the reference | `docs/entrypoint-sweep.md` + `docs/reference-sources.md` |
| 3 | **Transpiler emits clean C** before mass-generating. One transpiler improvement upgrades the whole corpus; late is pure tax | `docs/transpiler.md` |
| 4 | **Profile an end-to-end skeleton on the real A500** before choosing what to optimise, and before setting any performance target | `docs/perf-method.md` |

Two more that are done and must stay done: the validation harness cannot pass vacuously, hide
endianness or excuse a live exit register (`docs/validation-harness.md`); the faithfulness seam
rule is written down (`docs/faithfulness-seam.md`) because it is a call you make hundreds of times.

## The source binary

`revs.ssd` — 200 KB single-sided 80-track Acorn DFS image, title "REVINST", `*OPT 4,3`.
**Target release: *Revs Plus Revs 4 Tracks*, © Superior/Acornsoft 1986** (user decision) — five
circuits on one engine, with Computer Assisted Steering.

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

⚠⚠ **The expansion track files PATCH THE ENGINE at runtime** — the exec addresses are the binary's
own evidence: Silverstone is data (`$0000`), the other four are *programs* (`$70DB`) carrying hook
code (`ModifyGameCode`, `CallTrackHook`, `HookFieldOfView`, …) that modifies engine code at start
and generates geometry at runtime. **The bytes the engine executes differ per circuit.** Details:
`docs/reference-sources.md`.

⚠⚠ **Disassemble `disasm/revs_runtime.bin` (`make runtime`), never `revs_mem.bin`** — REVS2
unpacks itself before running (self-copy to `$7900`, a checksum-verified `$5300`↔`$70DB` swap, five
block moves, `JMP $63BD`). `tools/relocate.py` replays it, verified against a real BBC. **Every
address in `disasm/symbols.csv`, `ghidra_scripts/entrypoints.csv` and the docs is a runtime-image
address.** `revs_mem.bin` (`tools/ssd_load.py`, `make image TRACK=…`) is only the loader's output
and the replay's input. Mechanism and traps: `docs/static-map.md`.

⚠⚠ **There is a SECOND unpack.** `copy_dash_data` (`$18EA`) assembles `$7B00-$7FFF` — 1280 bytes
of live code including the view rasteriser and the wing mirrors — at *runtime* from 41
`$80`-spaced blocks at `$3000`, and stows it back before returning to MODE 7, so the page is empty
in every static image and every out-of-race RAM dump. ⚠ It copies offsets `dash_block_starts[col]`..`$4F`
— the view's own LIVE SOURCE SPAN, not the block tails (measured, `docs/span-render-plan.md` §12b);
those blocks are time-multiplexed, dash code out of race and source bytes in race. `make dashcode` replays it into
`disasm/dashcode.txt`, which `make gen` ingests by default (`DASHCODE=0` opts out). ⚠ That page is
also the MODE 7 screen (same 1 KB, time-multiplexed) and is heavily self-modifying — 42 patch
sites. `docs/static-map.md` §Open items 6 and 10.

ℹ The 1986 engine differs from the 1985 single-track release in 974 of 24064 bytes (an authentic
revision, not a repack) — relevant only when reading a 1985-era reference.

**Every control, and its Amiga key, is in `docs/controls.md`** — the handbook, `REVINST` and the
engine's own `shift_key_tbl`, reconciled and measured on a real BBC. ⚠ The BBC function keys are
NOT contiguous (f0 `$DF`, f4 `$EB`, f7 `$E9` sit outside row 7); assuming they were put every
function key in the port on the wrong Amiga key. **Amiga input: mouse + keyboard** (user
decision) — the BBC's own keyboard mode is the faithful precedent; mouse replaces the uPD7002
analogue axis.

## Build / run / debug

### Host (macOS dev) — from repo root
```
make                       # build/revs — HEADLESS by design, no renderer (see below)
make validate              # the native-twin byte-exact differential
make validate FN="name"    # only matching tests — prefer this
make determinism           # ⭐ the WHOLE-CORPUS differential: 300 frames of a pinned race,
                           #   all 64 KB byte-compared.  The ONLY check that covers a change
                           #   to the transpiler / cpu.h / the memory model, because those
                           #   change `validate`'s oracle too.  `make clean && make determinism-record` first
make determinism-drive     # ⭐ ...and the same 300 frames with the car MOVING — a DIFFERENT
                           #   trajectory, and it catches defects the parked one cannot.
                           #   RUN BOTH after a change to any driver routine
make determinism-crash     #   ...and the RESET LADDER (car off the track, full_track_scan_rebuild
                           #   runs 7x) — the gate on reset_driving_variables' practice arm
make determinism-steer     #   ...and the same 300 frames with the WHEEL TURNED — a third
                           #   trajectory, the gate on the steering/response path
make lap                   # ⭐⭐ WHOLE LAPS of all six circuits, driven by the AUTOPILOT (autorun.cpp):
                           #   fails on any crash (the car is reset to the grid), stall or airborne
                           #   frame — the only gate that ever takes a corner or crests a hill
make lockstep CIRCUIT=n    # ⭐⭐ ...and that run replayed on a REAL BBC (jsbeeb), poll for poll: the first frame
                           #   the PHYSICS differs, then the VIEW.  Host EXACTRATIO=1 (the physics reads
                           #   the picture, so ±1 LSB is not acceptable there).  5 = the Nurburgring disc.
                           #   ⭐ Bisect with 64 KB dumps at a matched PC and frame on both sides:
                           #   REVS_LOCKSTEP_AT=PC:N:f + --lockstep-at (docs/validation-harness.md)
                           #   ⚠⚠ and run an in-process oracle (TERRAINLOWCHECK, SCANCHECK, ...) under
                           #   the AUTOPILOT too — a STRAIGHT_TO_RACE window hid a steering-only defect
make determinism-lights    #   ...and the STARTING LIGHTS on screen (race proper, frame 3375) —
                           #   the only gate that sees the light column; -race cannot
python3 tools/sim_equiv.py [--steer[=l|r]] [--modes=legacy,h400,h200]   # ⭐ DECOUPLED physics vs
                           #   legacy in GAME time (build STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 first)
make determinism-race      # ⭐ ...and THE RACE PROPER (session_is_race = $80), the ONLY target
                           #   that reaches any `& $80` arm — every other determinism run is a
                           #   PRACTICE session.  ⚠ 13000 frames, RELEASE=1, ~2 min: ~12000 of
                           #   them are the qualifying session the grid is reached through
make fatscan               # the 6502-RESIDUE SCANNER: dead exit fields, flag replay, zp scratch in
                           #   loops, marshal round trips — ranked by TARGET instructions/frame
                           #   (host --coverage counts x amiga/out/Revs.elf; rebuild that PLAIN first)
make todo                  # ⭐⭐ WHAT IS OPEN: docs/open-work.md's queue + a live sweep for
                           #   TODO/FIXME/HACK markers in the tracked, non-vendored tree.
                           #   Expected output is "none" — a printed marker is either a real
                           #   work item for the queue or a stale marker to delete
make endian-lint           # fail if mem[] is aliased as a wide pointer
make macro-lint            # ⭐ fail if a FEATURE macro shares its name with a CALL macro —
                           #   `#define X()` in a feature's OFF branch makes `#ifdef X` TRUE,
                           #   which froze the dash needles and voided an A/B for four commits
make cpu-lint              # ⭐ fail if revs_native.c speaks `cpu` outside the argued
                           #   classes (tools/cpu_lint.py names them, and a STALE allowlist
                           #   row fails too).  The 6502-ABI shims the oracle needs live in
                           #   src/gen/revs_native_abi.c — nothing in the port calls them
make transtrap             # ⭐⭐ does ANY 6502 transliteration still run?  Nine scenarios
                           #   (front end, race, crash, six circuits) under TRANS_TRAP=1; a hit
                           #   is a FAIL.  ⚠ a body no scenario DRIVES is unproven, not dead
make gen                   # regenerate src/gen from listing.txt + dashcode.txt (DASHCODE=0 skips)
make image                 # rebuild disasm/revs_mem.bin from revs.ssd
make runtime               # ⭐ replay the engine's self-unpack -> disasm/revs_runtime.bin
make sweep                 # the entry-point sweep report -> disasm/sweep.txt
make refloop               # ⭐⭐ RACE A REAL BBC under jsbeeb -> tmp/bbcref (the visual ground truth)
make fbwrites              #   ...every frame-buffer store attributed to the PC that made it
make viewdiff              # ⭐⭐ EVERY CIRCUIT'S RACE VIEW vs a real BBC, byte for byte over display
                           #   lines 82..166 — the ONLY gate on an expansion circuit's PATCHED arm
                           #   (CIRCUITS="3 4" narrows it).  Its three probes for turning a pixel
                           #   diff into a cause: --mem-at=<pc> / REVS_MEM_DUMP_AT (64 KB at a named
                           #   PC, because a FRAME-BOUNDARY dump is the wrong instrument for the road
                           #   pass), --watch=<addr> (writes attributed to the writing PC) and
                           #   --trace-edge (the road walk point by point).  docs/bbc-reference-loop.md
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
proves the bytes land; `track-run` proves the circuit's own code executes — an expansion circuit
can install byte-perfectly and then run Silverstone's control flow over its geometry, which no byte
diff can see. `g_trackHookCalls` separates them, and it only counts once the race starts (a plain
build reads 0 at frame 40 because frame 40 is still MODE 7 — use `STRAIGHT_TO_RACE`).

⚠ **`make refloop` is the visual ground truth — use it, never the host backend, to settle any
"faithful or port bug?" question.** It boots the real disc under jsbeeb, drives, and dumps both the
BBC frame buffer and what the real 6845 + Video ULA displayed, plus the measured band schedule.
`make fbwrites` attributes every frame-buffer store to its PC, which is how a routine's real job
gets settled. `docs/bbc-reference-loop.md`.

⚠ **The host build deliberately has NO renderer.** An approximate backend costs hours on bugs that
exist only in the approximation. Visual ground truth is jsbeeb/b2 on the real disc; performance
ground truth is FS-UAE + gdb on the Amiga build. The host build exists for `make validate`,
`make determinism` and host-side algebra proofs. Rationale: `src/platform/host/PlatformHost.h`.

### Amiga cross-build (m68k-amiga-elf-gcc) — from `amiga/`
```
. env.sh        # put the ~/.local Amiga toolchain on PATH (source it, SAME shell command)
make            # build out/Revs.exe (+ Revs.elf; runs the muldiv audit on every link)
make STRAIGHT_TO_RACE=1   # ⭐ boot straight into the race — see below
./run.sh        # boot in FS-UAE (Kickstart 3.1; CTRL + left mouse button quits)
./debug.sh      # source-level debug via the FS-UAE GDB stub (prints its $DEBUG_PORT)
make SIMLEGACY=1 # the engine's own loop (one sim step per painted frame) — the default build
                #   is DECOUPLED (game time = real time, docs/open-work.md §FRAME-RATE-INDEPENDENT)
./diag_run.sh N # headless probe run for N seconds (needs a PROBES=1 build).  ⭐ SILENT
                #   (`--audio_driver=dummy` — a warp run's audio is a screech and no probe reads
                #   it); `FSUAE_SOUND=1` restores it, `FSUAE_SILENT=1` mutes the audible ./run.sh
make PROBES=1 ISRSPLIT=1  # ⭐ split the VERTB ISR into its own timed slots (amiga/isr_split.gdb)
                          #   — the ONLY instrument that can see it, because the ISR's time is
                          #   charged to whichever phase it preempted.  ISRCAL=1 adds the known-
                          #   quantity calibration burn; ⚠ `make clean` when you turn it back off
EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=x.gdb ./diag_run.sh 60   # ⭐⭐ ~4.9x faster, same numbers
EXTRA_ARGS="--warp_mode=1" ./pcsample.sh 150 0.01   # ⭐⭐ STATISTICAL PC SAMPLER: where the time REALLY is
python3 ../tools/pcsample_report.py .run/gdb-out.log --from-field=3000 [--fn=<name>]  # by function / source line
EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=steptrace.gdb ./diag_run.sh 400   # ⭐⭐ SINGLE-STEP whole calls (edit the target in the .gdb)
python3 ../tools/steptrace_report.py .run/gdb-out.log   # instructions + memory operands per source line — run from the repo root
make WALKCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 && GDBSCRIPT=walkcheck.gdb ./diag_run.sh 400   # ⭐ the span-walk ASM vs its C loop, every span + a fuzzer (SPANASM=0 = the C control)
make SETUPCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 && GDBSCRIPT=setupcheck.gdb ./diag_run.sh 500   # ⭐ the whole span PASS in asm vs the C, all 64 KB a pass + a fuzzer (SETUPASM=0 = the C control; TRACK=n per circuit)
make GEOCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 && GDBSCRIPT=walk_check.gdb ./diag_run.sh 500   # ⭐ the geometry WALK in asm vs its C loop, all 64 KB a walk + two fuzzers (WALKASM=0 / GEOASM=0 = the C controls; TRACK=0..5 per circuit)
make EDGECHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 && GDBSCRIPT=edge_check.gdb ./diag_run.sh 400   # ⭐ fill_dash_edge_columns in asm vs edge_run_flat, all 64 KB a frame + a fuzzer (EDGEASM=0 = the C control; TRACK=0..5)
make NDLASMCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 && GDBSCRIPT=ndl_check.gdb ./diag_run.sh 500   # ⭐ the needle DDA in asm vs its C loop, all 64 KB + the pixel list a line + a fuzzer (NDLASM=0 = the C control)
```
⭐⭐ **When a bracket and the source disagree by an order of magnitude, single-step a call** — the
PC sampler costs ~1.2 s a sample through the gdb stub; a stepped trace of 24 calls is ~10k
instructions in minutes and names every line.
⭐⭐ **Rank step-1 work against the REAL BBC, row for row: `make bbcprof` (repo root) brackets its 27
main-loop call sites, `--flat` its functions.** A port row slower than the 6502's is a defect to
find, not a trade; a routine the port already beats (`apply_driving_model`, 2.2×) says what native
C buys. ⚠ Compare the MEAN column (a median hides a routine that works on under half its frames),
and the BBC's practice frame contains a 6.0 ms busy-delay pad the port rightly drops.
⚠ **A pricing arm (`ROADARM=`, `*CARVE=`, …) leaves a WRONG PICTURE in `amiga/out/` — rebuild plain
before handing the machine back**; a user ran one and reported the road missing.

⭐⭐ **Put `EXTRA_ARGS="--warp_mode=1"` on every probe run** — FS-UAE runs ~4.9× faster than real
time and it changes **no** measurement this project takes (FPS is `50 * g_fpsFrames / g_vbiCount`
and the phase clock is beam ticks: both ratios of *emulated* quantities; verified against a
non-warp run).

**Never `pkill fs-uae` / `pkill gdb`** — several Amiga projects run their own emulator at once. The
scripts source `~/.local/share/amiga/fsuae_common.sh` (shared, outside every repo; `$FSUAE_COMMON`
overrides), which kills only the pid recorded in this directory's `.run/fsuae.pid` and gives each
project its own gdb-stub `$DEBUG_PORT`. Stop a stranger's emulator by pid, or not at all.

⚠ **`make clean` before any `PROBES=1` build and after editing a widely-included header.** The
Amiga Makefile tracks neither, so a partial rebuild links stale objects into a
**working-but-wrong** binary. Treat any unexplained regression right after a header edit or a
`PROBES` toggle as a stale build first. `docs/headless-fsuae.md`.

⚠ **Every global a committed `.gdb` script reads must be listed in `PROBE_SYMS` (`amiga/Makefile`).**
`--gc-sections` drops an unreferenced counter and gdb then prints **instruction bytes as a value** —
a fake measurement, not an obvious zero. `make probe-audit` runs on every link.
(`__attribute__((retain))` is ignored on this target.)

### Headless FS-UAE loop — **measure, don't theorize**
`. ./env.sh` (same shell command) then `amiga/diag_run.sh [delay]`, editing a `.gdb` script to
print whatever globals / `mem[0xNNNN]` you need. Details and traps: `docs/headless-fsuae.md`.
Dump what the target is really showing with `amiga/screen_dump.gdb` (race view) or
`amiga/mode7_dump.gdb` (front end) + `tools/amiga_ppm.py --planes=N --height=N` — there are TWO
display configurations and the decoder must be told which.

⭐ **`make STRAIGHT_TO_RACE=1` boots into a Silverstone PRACTICE session with the engine running in
first gear, then hands over the keyboard** — the fast way into the race. It skips **no** game code:
practice needs exactly one menu answer (`$63F7`), which `src/platform/autorun.cpp` gives the
instant it is asked, then SPACE / `T` / `Q`. Verify with `amiga/straight_to_race.gdb`.
⚠ With `FPSCOUNT=1` the script holds the throttle instead of handing over — a **moving** car, which
is a different workload from a parked baseline.

## Reference docs — READ ON DEMAND (this file stays small on purpose)

Hard-won detail lives in `docs/`, not here. **Read the relevant one BEFORE working in its area.**

| Doc | Read it when |
|---|---|
| **`docs/postmortem.md`** | **Early, once, in full.** The retrospective this project is built on |
| **`docs/open-work.md`** ⭐⭐ | **"What is next?" — THE QUEUE.** Ranked open items with their ms sizes and their gates, plus ⛔ one line per measured dead end. `make todo` prints it + a live marker sweep |
| `docs/phases.md` | The gating between phases, and what each phase owes |
| **`docs/reference-sources.md`** ⭐ | **Before any disassembly work.** The annotated reconstruction, its licence limits, what is on this disc |
| `docs/bbc-reference-loop.md` ⭐ | Anything about ground truth, jsbeeb/b2, or trusting an image |
| `docs/entrypoint-sweep.md` ⭐ | Before generating C; whenever you find a dispatch table or vector |
| `docs/bbc-hardware.md` | Touching hardware, MOS calls, screen modes, input, or sound |
| **`docs/controls.md`** | Any question about a KEY — what the game binds, what it does, which Amiga key carries it |
| `docs/toolchain.md` | Running the pipeline: disc tools, Ghidra headless, the builds |
| `docs/transpiler.md` | Working on `tools/transpile.py`, or when generated-code shape surprises you |
| `docs/validation-harness.md` | Writing or trusting a `make validate` fixture |
| `docs/faithfulness-seam.md` | Where a routine lives (validated twin vs Amiga-only), and **how to write a twin** |
| `docs/helper-elimination-audit.md` | The math-helper campaign's per-site KEEP/CONVERT ledger — which `revs_native.c` sites keep a 6502 flag-helper (a flag escapes) and which convert to plain C |
| **`docs/wide-value-cleanup.md`** | The byte-lane→wide-value campaign ledger: replacing 6502 `_lo`/`_hi`/carry handling of 16/24-bit values with plain-C `uintNN_t` math. Tiers, per-base status, the two mechanisms, the SoA `value_16[N]` relocation. ⚠⚠ **MEASURED end to end and it is a NULL RESULT (+0.65%, inside noise)** — the instruction-count win is real but the byte lanes are not where the frame goes; ⭐⭐ **rank a candidate pair by OPS-PER-MARSHAL, never by ref count** (a shared *scratch* cell's huge ref count counts TENANTS, not wide arithmetic — the error that made `math_lo/hi` look like the biggest prize for three passes) |
| `docs/perf-method.md` ⚑ | Quoting, sizing or judging ANY performance number; where the time goes |
| **`docs/span-render-plan.md`** ⭐ | **Before touching any plotter or any Phase 6 asm — THE live rendering plan.** The replacement architecture: world points → spans → bitplanes, §10 sizing, **§10n the measured checkpoint** (the fill is 6.87 cyc/byte and direct-to-bitplane writing is exonerated; a hook-in nets zero), **§11 THE LIVE DESIGN** (row ownership: the decode is the prize, the painter is a wash, and the 208-row ledger is the plan), §10p the build ledger, §10m the SPRITE lever. (⛔ `docs/direct-bitplane-plan.md` is the OBSOLETE earlier plan — kept only because source/docs cite its §-numbers; read it as history, never as a plan) |
| `docs/m68k-optimisation.md` ⚑ | Optimising a hot function or writing an asm twin (68000 rules) |
| `docs/amiga-lessons.md` ⚑ | Copper lists, sprites, the VBI, write-only registers |
| `docs/amiga-arch.md` ⚑ | The Amiga display/interrupt architecture decisions and why |
| `docs/headless-fsuae.md` ⚑ | Writing a probe, driving FS-UAE headlessly, or suspecting a stale build |
| `docs/method-lessons.md` ⚑ | How to work: measuring, bisecting, proving a reordering, recording findings |
| **`docs/static-map.md`** ⭐ | **What the binary IS** — the unpacks, the sweep, hardware/MOS inventories, per-track hooks, the self-modifying regions |
| `docs/rename.md` | A name contradicts behaviour, or you need a name that does not exist yet |

## Architecture

`disasm/symbols.csv` is the **source of truth for names** (the transpiler reads it; never
hand-rename in generated files).

| File | Role |
|---|---|
| `tools/ssd_map.py` / `ssd_load.py` | DFS catalogue dump / post-load memory image builder (per-track) |
| `tools/transpile.py` | The transpiler. Reads `disasm/listing.txt` + `symbols.csv`. Shape and traps: `docs/transpiler.md` |
| `src/gen/revs_gen.c` | Generated 6502→C transliteration (regenerated; do NOT edit by hand) |
| `src/gen/revs_manual.c` | ⚠ **Does not exist, and that is the correct state** — `MANUAL_FUNCS` is empty because `SMC_SITES` covers all 24 self-modifying sites generically. Both Makefiles `wildcard` it. Add an address there only when a routine genuinely cannot be transliterated at all, and say why |
| `src/gen/revs_native.c` | FAITHFUL native twins (idiomatic C `_core` + 6502-ABI shim), `make validate`d, linked into BOTH backends |
| **`src/gen/revs_native_abi.c`** | ⭐ the 56 `void <name>(void)` 6502-ABI shims with **no native caller** — the oracle's and `validate_native.c`'s way in, nothing in the port calls them. It is the only native-surface TU allowed to speak `cpu` freely (`make cpu-lint`). ⚠ Never put a twin's BODY here; before adding a shim, ask the caller question as a TRANSITIVE CLOSURE (a shim called only by oracle-only shims is oracle-only) |
| `src/platform/amiga/revs_native_amiga.cpp` | Genuinely Amiga-only, unvalidated code |
| `src/platform/mos.cpp` | The MOS (Acorn OS) call layer — ⚠ a FLOOR, not a closed surface: three calls were found by RUNNING it |
| **`src/platform/bbc_screen.h`** ⭐ | **THE DISPLAY MODEL** — geometry, pixel format, the five raster bands. Read before anything visual |
| `src/platform/amiga/RevsScreen.*` | BBC frame buffer → 2 bitplanes + the copper palette bands |
| `src/platform/amiga/RevsInput.*` | Mouse + keyboard onto the game's own two input paths |
| **`src/platform/sound.*`** ⭐ | **THE SOUND MODEL** — the MOS sound scheduler + the SN76489 state it drives, measured off a real MOS. Read before any pitch/amplitude/envelope work |
| `src/platform/amiga/RevsAudio.*` | That chip state → Paula. Amiga-only, unvalidated by construction |
| **`src/platform/teletext.*`** ⭐ | **THE MODE 7 MODEL** — the MOS VDU driver + the SAA5050 (⚠ MODE 7 does NOT use the MOS font; `OSWORD 10` belongs to the race view). `teletext_font.h` is generated |
| `src/platform/bbc_hw.cpp` | The BBC hardware model behind `bus_read`/`bus_write`, and the IRQ1V shim |
| **`src/platform/trackmenu.*`** ⭐ | **THE CIRCUIT MENU** — port-authored (REVSMEN is BASIC), validated against a recorded real page. ⚠⚠ It is also the SINGLE circuit installer: installing is not idempotent across circuits, so a second install of a different one is refused and counted (`g_trackOverinstalls`) |
| `src/platform/autorun.cpp` | Scripted keyboard for unattended runs; without it a headless run measures a menu spin. `REVS_AUTORUN_BUILD` is the one predicate for "this build drives itself" |
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
   harness *fails* a validated name with no fixture, because a fixture-less PASS runs zero
   comparisons (`docs/validation-harness.md`).
5. `make validate FN=<name>` runs both on the same inputs and diffs full `mem[]` state.
6. **Sabotage it** — four deliberate defects minimum, each must FAIL. A sabotage that PASSES is
   one of THREE things and only the first costs anything: a fixture gap; a defect unreachable by
   construction; or **no change at all** (a bit provably already clear, a piecewise curve that is
   continuous at the breakpoint you moved). Decide by argument, then check the SIBLING case, and
   write the argument at the code (`docs/validation-harness.md` §FIFTEENTH).
   ⭐ Ask what the TEST BACKEND answers, not just what the fixture randomises — a `Platform`
   virtual with a constant answer is a whole arm that never runs.
   ⭐⭐ **And an IN-PROCESS differential cannot see a defect in how a SHARED INPUT is classified —
   only in what the fast path SKIPS.** `DIRTYCHECK` re-decodes into a scratch buffer from the same
   `m_lineMode`, so sabotaging the dirty pass's skip test fires it (mismatch=4050) while sabotaging
   its uniformity classification survives 31/31: both sides classify the row the same wrong way.
   That is structural, not a fixture to widen — **state each such oracle's scope AT the oracle**,
   and gate a classification by the picture or by argument.
   ⚠⚠ **A scripted sabotage loop MUST `rm` the object file and the binary before every build.**
   Left to `make`, a rewrite-then-rebuild loop reuses the previous iteration's object on some
   iterations and reports that the defect is undetectable. **The tell is two different defects
   printing byte-identical mismatch counts** (`docs/validation-harness.md` §ELEVENTH).

⚠⚠ **A TWIN IS REAL C, AND THAT IS A REQUIREMENT — a transliteration with the macros left in is
NOT a twin.** The point is to delete the interpreter, and macro soup hides the structure the next
optimisation needs. Named locals and ordinary control flow; `mem.h` names for every cell that has
one and a `symbols.csv` row for every one that does not (**an unnamed hex address in a twin is a
rename that was skipped** → `docs/rename.md`); comments that say what it COMPUTES; hardware writes
`#ifdef`-guarded, never deleted. ⭐ The one narrow exception: where a FLAG genuinely leaves the
routine, wrap that operation in a small named helper with the cpu.h macro *inside* it — C has no
carry. Full checklist, the reasoning and the measured traps: `docs/faithfulness-seam.md` §Writing one.

⚠ **One narrow exception to step 4: `transpile.py`'s `NATIVE_FUNCS`** — a native DRIVER whose oracle
cannot be run on randomised memory because its first act is to execute the rest of the engine
(`race_main_loop` is the only member). It gets the same `__t6502` split and the same style rules, but
`make validate` merely *prints* it and **`make determinism` + `make determinism-drive` are its
gate** — which covers its per-frame path and provably not its rare arms
(`docs/validation-harness.md` §a DRIVER with no fixture). Not for a fixture that is merely awkward.

**Which side of the seam:** `revs_native.c` = FAITHFUL, validated, both backends;
`revs_native_amiga.cpp` = genuinely Amiga-only. A faithful pure-`mem[]` routine that merely needs
a small Amiga variation **stays in `revs_native.c`** under `#ifdef REVS_PLATFORM_AMIGA`. Full
decision procedure: `docs/faithfulness-seam.md`.

**Amiga specifics:** the INTB_VERTB vector is taken over wholesale (the handler clears INTREQ
itself; `WaitTOF()` is unavailable). The copper owns the display. ⭐⭐ **EVERY spin-wait is a PRESENTATION POINT**, not
just a timing one: MODE 7 screen RAM *is* the BBC's display, but this port only reaches the
bitplanes through `platform_render_frame()`, so a wait that renders nothing freezes the display on
the previous page — which reads as a logic bug on the *next* page, never as a missing render
(`wait_dismiss` `$34D2` hid the whole wing page this way). ⚠ Guard it with
`REVS_PLATFORM_AMIGA`: on the host `renderFrame()` is the GAME-FRAME counter and `tick_vbi()` IS
the 50 Hz interrupt, so an unguarded hook moves the trajectory and `make determinism` diverges. `bus_write` to BBC hardware is largely ignored.
⚠⚠ The VERTB ISR does the **copper work only** and counts fields; the game's 50 Hz body is drained
from main-loop context at the engine's own frame hook (`$1701`) and frame-wait spin (`$1760`),
because the body DRAWS and running it in the ISR gave ~50 scene changes per painted frame.
`make BODY_IN_ISR=1` reproduces the old model and `amiga/fill_catch.gdb` detects it.
⭐ The 50 Hz body is SUSPENDED while the front end is up (`Revs::setFrontEnd`) — otherwise the
menu's fields hand the engine a backlog to run in one burst. Reasoning: `docs/amiga-arch.md`.

## Performance

⭐⭐⭐ **THE REAL BBC RUNS THIS SCENE AT 97.0 ms/frame — 10.31 fps (`make refloop` measures it and
prints its own calibration).** That is the yardstick: quote every frame figure against it. The
port's current bracketed frame, and its ratio to 97.0, is the header of `docs/open-work.md`.

**⭐⭐ TARGET: ~48 ms a frame, 2× the original game** (user decision, superseding "50 FPS / floor
25 FPS", taken once the original's own cost was known). **Stretch: 40 ms**, and the reason is the
`50/N` display ladder, not ambition — a frame is *displayed* every `ceil(ms/20)` fields, so 41-60 ms
all show an identical **16.7 fps** and only **≤40 ms steps to 25**. ⇒ 48 ms is the commitment,
the last 8 ms is the only part of that band a player can see.

⛔ **20 ms is off the table as a goal**: it is 4.85× the original on a CPU with *no* per-byte
advantage — a 68000's bus cycle is 564 ns against the 6502's 500 — which wins only on batching,
and the BBC's 8-byte-apart destination cells and 128-byte-apart sources forbid it by construction.
⇒ **no code shape reaches these numbers; only changing the layouts on both producer and consumer
sides does.** `docs/perf-method.md` §what the original hardware achieves.
⚠⚠ **THE ENGINE HAS NO FIXED-RATE SIM, AND THE PORT GIVES IT ONE.** On the BBC every sim step runs
once per painted frame (race clock +9.36 cs a frame), so game speed = framerate. The default Amiga
build steps at **25 Hz on a 68000 and 50 Hz on a 68020+** (user decision), with game time = real time
and a slow tick every 93.6 ms carrying the clock, so lap times are the original's. What is scaled,
held or ticked, and why, is `docs/faithfulness-seam.md` §THE FRAME-RATE-INDEPENDENT SIMULATION.
⭐ **Legacy mode is the gate**: the host default and `make SIMLEGACY=1` run the engine's own loop
byte-exact, so every determinism target still means what it did. A decoupled mode is gated by
**`tools/sim_equiv.py`** (physics in game time against legacy) and **`amiga/sim_clock.gdb`**.
⚠ **A new per-frame quantity must be classified** as scaled (it accumulates), slow-tick (it counts
engine frames) or per-render-held — an unclassified one runs 2–4× fast or slow. An event between
two ticks must be latched for the tick code that samples it.
⚠ **Price render work with `SIMLEGACY=1`** (a decoupled window is a different stretch of game time),
and quote the decoupled cost from `fps_series.gdb`: 25 Hz steps cost the A500 ~16% of its displayed
rate today (`docs/open-work.md`).
The A500 is a 7 MHz 68000 and a frame is 20 ms: spending 10 ms on *anything* is half the budget.
Be conscious of absolute milliseconds always.

**Baseline: the bracketed FRAME in `docs/open-work.md`'s header** (`PROBES=1 FIXED_RNG=1
STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000 SIMLEGACY=1` + `phase4_prof.gdb` — ⚠ SIMLEGACY:
a decoupled window is a different stretch of game time, see docs/open-work.md §FRAME-RATE-INDEPENDENT SIMULATION, warp, driving, priced with
`diag_run.sh 45`). **This is the number a change is sized against**, and it is both the
bracketed total (`Σ phaseTicks[1..39]`) and `(elapsed − phase 0) / loopFrames` — they agree to
0.02 ms, so the brackets account for the whole frame. ⚠ **The raw `elapsed / loopFrames` is NOT
the frame**: phase 0 is boot plus the engine's own 2-second crash pauses.
⚠⚠ **Read the phase table out of `amiga/.run/gdb-out.log`, never out of `diag_run.sh`'s stdout** —
that is `tail`-truncated to 40 lines (`GDBTAIL`), which drops phases 1..5 *and* the `frozen=` gate
line, so a total summed from it reads ~36 ms low. It has cost two bad diffs. The displayed **~4.93 FPS**
(`FPSCOUNT=1` + `fps_series.gdb`, 50.5 painted per 512 VBLs, same session) is the standing
*displayed* figure and nothing else.
⚠ One painted frame is 3.3% of an FPS row, so that figure IS the noise floor — always re-run the
control in the same session from a clean build rather than diffing against it
(`docs/perf-method.md` §twin #13).
⚠⚠ **A WORKING CHANGE READS AS PARTLY GIVING ITSELF BACK, and the cause is dilution, not the
engine.** A faster build runs more game frames inside the same window, so it reaches more of the
2-second crash holds that sit in phase 0 — the dash-edge pass moved the frame the full −5.01 ms
while raw `elapsed/loopFrames` moved −2.29, and the entire gap was **one extra 100-field hold**.
So quote the **bracketed** total (= wall − phase 0), and before comparing two runs check phase 0:
its tick count is **bit-identical** across runs of the same trajectory, so any difference at all
means a different workload (`ticks / 80120` says how many holds). When phase 26 moves, read `ONE BODY TICK` first — a per-tick cost that moves with the
body untouched is the trajectory, not the change. `docs/perf-method.md` §the dash-edge walk.
⚠ The whole view pipeline is now real C — `build_track_geometry`'s tree and `draw_road`'s tree both
have **no transliteration left in them** — and neither pass's twins moved the framerate. Removing
the interpreter from this subsystem is DONE; the next win must remove accesses or points.
⚠⚠ **The tested consumer run-entry specialisation is dead** — its single-run, flat-span path
covered every phase-2/3 entry and measured **-0.15%**, retracting its predicted "~10% prize".
Do not retry that code shape. The null does **not** prove setup is free: its emitted helper enlarged
and slowed the retained unit loop, and the original line/run fit was underidentified. Direct target
measurement instead puts the complete unit/run interior at **29 ms/frame: ~8 ms destination stores
and ~21 ms source consume/translation/loop/run control**. `docs/perf-method.md` has the controls.
⭐⭐⭐ **AND THE SWEEP IS 61% PER-LINE DRIVER AND CHAIN-ENTRY CODE, NOT UNIT WORK** — measured by
differencing `NOUNITS=2` (drivers only) against the control: 32.4 ms of driver/entry against
20.4 ms of unit loop, with phase 3 at 80% entry, which independently reproduces the `VIEWP3`
split's 83%. **Phase 1's old "unexplained ~6x" is RETRACTED**: its unit loop is 55 cyc/unit and
the objdump shows the unrolled body is 43 cyc/unit on the clean arm — 12 (load) + 12 (store) +
12 (branch) + ~9 amortised, which is what those three operations COST on a 68000 — plus ~10 for
the 12% dirty arm. ⇒ **No code shape can improve that loop, and widening is impossible** (the
BBC layout puts destination cells 8 bytes apart and sources 128 apart). The 32 ms of drivers is
the larger, separate lever. `docs/perf-method.md` §the sweep is 61% driver/entry.
⭐⭐ **AND THE CHAIN-ENTRY HALF OF IT IS NOW DELETED — −6.58 ms**: the short phases' runs went
INLINE in their drivers (phases 2+3 **35.61 → 29.03 ms**), which killed the poke-then-decode round
trip that reached them. Two transferable moves did it, both below — state in LOCALS rather than a
struct whose address escapes, and a **dead arm selected by its own precondition** into the
existing out-of-line copy. ⭐ **Quote the sweep's CENSUS beside the phase row**: 426/32/16 and
282/50/25 were identical across both arms, which is what makes a moved row a shape win rather
than a trajectory. The ~29 ms that remains is `docs/open-work.md`'s top entry.
⛔⛔⛔ **AND THE REPRESENTATION CHANGE THAT WAS TO REACH THAT 20 ms IS CLOSED — the source-event/run
consumer was built and cost +25.46 ms.** ⭐⭐⭐ **On a 68000 the expensive direction is INDEX →
POINTER**: a cell-indexed event mask pays 476 cyc/event and a 402-cycle per-run prologue where the
scan pays an incremental `addq.l #8`, so break-even is `N = 36.5 + 35.3·E` cells against a 40-cell
line and a 17.6-cell average run — it cannot win at any event density, and 76% of the walk's cycles
are address arithmetic. ⇒ **A sparse-iteration consumer must amortise its addressing over a LINE or
a SWEEP, or take byte OFFSETS the producers already know.** ⭐⭐ **And price any skip scheme with TWO
numbers before building it: how many visits it deletes, AND what one visit of the NEW shape costs** —
a census of deleted visits bounds the saving and says nothing about the replacement, which is the
error that cost this one. `docs/perf-method.md` §the walk walks indices.
⚠⚠ **Writer-maintained dirty maps are dead as a shipping optimisation, twice now and by TWO
DIFFERENT MECHANISMS.** `CHANGEDIRTY`'s per-store compare and two-map RMW traffic measured ~8.5%
slower than the batched shadow scanner (byte-exact, 0/23 — reverted, the flag no longer exists).
Then the *consumer* predicate `g_viewLineDirty` cost **+4.9 ms in the producers against the 3.4 ms
its span saved**, and not through store traffic at all: turning marking on inlines the `REVS_FLAG_OP`
leaf `view_mark_source` into `seam_write`, **a header choke point**, and the objdump counts **164
inlined copies** of the map's address — twenty in `column_gap_walk_core`, whose caller then collapses
532 → 54 instructions. ⭐⭐ **So price a marking scheme by what it does to the PRODUCERS' code shape,
not by its per-store cost** — and prefer a **stateless** predicate the data already carries:
`view_consume`'s destructive read makes a non-zero source at line entry *exactly* "a producer wrote
it since the last sweep", which is what the map was approximating conservatively.
⭐⭐⭐ **AND SIZE A HOOK-IN AGAINST THE DIFFERENTIAL FOR THE PART IT CAN ACTUALLY DELETE, NEVER
AGAINST A CENSUS OF STORES — THE MISSING THIRD NUMBER IS WHAT SURVIVES.** The direct-span checkpoint
deleted 58% of phase 1's units at exactly the predicted 42 cyc/unit and still netted zero, because a
per-line hook-in reaches the **unit loop** while the bracket is **905 cyc/line of driver** plus
units, and `NOUNITS=2` had already published that split (61% driver/entry for the whole sweep) before
the plan was written. This is the companion to the two-number rule above: visits deleted, cost of the
new shape, **and what the hook cannot touch**. `docs/span-render-plan.md` §10n.
⭐⭐⭐ **AND THE THIRD MEMBER OF THAT FAMILY, WHICH COST THREE SESSIONS: NEVER SIZE A PRIZE AS A
RESIDUAL OF A MEASURED BRACKET MINUS A MODELLED PART.** The residual collects every error in the
part you modelled, with its sign pointing at the term you are about to build against — a bracket is
WALL TIME (DMA contention, instruction fetch, instrumentation) and a static cycle count is none of
those. Phase 1's per-line driver was published as `ph24 − modelled painters = 2258 cyc/line ≈
11.5 ms`; an arm that runs the driver and simply **does not paint** reads **3.05 ms total, 601
cyc/line**, and the driver deletion built against that 11.5 collected **−0.33 ms**. ⇒ **Price the
part you intend to delete with an ARM THAT DELETES IT** — one build and one run — and **read the hot
callee's PROLOGUE first**: `revs_plot_chain` opens with seven volatile `SPAN_STAT` global RMWs, a
`movem`, three stack loads, the bus-range test and four table lookups, so most of that "per-line
driver cost" was a per-CALL cost. `docs/span-render-plan.md` §10q.
⭐⭐⭐ **AND THE REASON DIRECT-TO-BITPLANE PAYS IS THAT AN OWNED DISPLAY ROW STOPS BEING DECODED —
NOT THAT THE STORE IS CHEAPER. A BITPLANE PAIR COSTS WHAT THE `mem[]` BYTE COST.** Measured three
arms end to end: phase 1's whole line loop went direct — forty `mem[]` bytes, forty units and a
905 cyc/line driver replaced by two byte writes a cell — and **its own bracket moved −0.03 ms**,
while the frame moved −5.62, *all* of it the decode (28.74 → 24.30 ms for 36 of 208 rows).
⇒ **A direct-to-bitplane painter is worth `rows owned × 0.123 ms` and nothing else, so rank
ownership work by ROWS OWNED and never by the phase's own cost** — and a row wholly inside the flat
blue band is already free. Two invariants for a new domain: the reader gate must be **measured**
(`REVS_FB_POISON`), and ownership is per DISPLAY LINE, not per character row. The 208-row ledger —
every block now priced, −0.08 to −0.12 ms a row — is `docs/span-render-plan.md` §11. ⚠ The end
state is TWO steps and **`ph27` → 0 is RETRACTED**: owning all 208 rows is worth −11.18 ms and the
rest goes with the CALL, of which `snapshotBands` + `buildLineModes` (~2.4 ms) must keep running
forever because `m_plan` is the COPPER's palette schedule, not decode work (§11a).
  ✅⭐⭐⭐ **AND THAT SECOND STEP IS DONE: THERE IS NO PER-FRAME `mem[]` → BITPLANE CONVERSION ANY
  MORE.** Every display line has a painter and the 64-line sky band is FLAT (its four pens share a
  colour — it is the engine's own bytes showing through screen memory, so the picture there was
  always arbitrary), so `convertRace` runs only on a frame where some line is claimed by nobody.
  `RevsScreen::decode()` is accordingly **renamed `prepareFrame()`**: what is left is the copper's
  band plan plus the painters that own the dashboard rows, ~5 ms, and every millisecond of it must
  keep running. ⚠⚠ **The TRIGGER must be exact and per frame** — `own_has_gap` per band, one
  `cmp.l` per four lines — because a fixed cold-frame count is wrong: gap frames stop at frame 2 on
  four circuits and at frame **54** on Donington. ⚠ And a gap appearing LATE in a run is the one
  thing that can put stale pixels on screen, so `g_decodeGapFrames` / `g_decodeGapLastAt` are
  always compiled in. `docs/open-work.md` §5b.
  ⭐⭐⭐ **BUT THE PRIZE IS ONLY HALF AN OWNERSHIP DECISION — RANK A DOMAIN BY ROWS OWNED ÷ WRITER
  SET, because owning a row means RETARGETING EVERY ROUTINE THAT WRITES IT.** The measured
  writer-set ledger (`make fbwrites FILL=all`, §11b) groups all 208 display lines by their writer
  *set*: rows 0..17 + 192..207 have **one** writer, `vdu_char_emit` at ~3.7 stores/frame, and 8 of
  them have **none at all** — 34 rows for −3.31 ms — while the dashboard's needles are 34 rows for
  −2.79 ms and 66 stores/frame from two plotters plus two more routines. ⇒ ✅ **that glyph domain is
  BUILT (`make DELTAOWN=1`): `ph27` 20.13 → 17.19 ms, base-plus-delta into BOTH plane buffers, and
  ⭐ discount a ledger's row price by ~10% (predicted −3.31, measured −2.93 — the second instance)**.
  ⚠ A census that **ranks by
  volume** buries exactly that finding (a `slice(0, 16)` hid `vdu_char_emit` at position 26 of 62 and
  made 16 rows look writer-free for two sessions) — `docs/method-lessons.md`. ⚠⚠ And a **mirrored
  painter whose erase is cross-frame stateful must write BOTH plane buffers**, or the needle from two
  frames ago stays put: "the back buffer is enough" holds only for a painter that repaints a whole row.
  ⛔⛔⛔ **AND THE SECOND DOMAIN WAS BUILT, VALIDATED AND CLOSED ON COST — A DELTA PAINTER'S
  BREAK-EVEN IS ~0.8 DELIVERED BYTES PER OWNED ROW PER FRAME, AND THE NEEDLES ARE AT 1.7.** The
  budget is the decode the rows delete, **~544 cycles a row** (40 cells × 13.6 cyc/`mem[]` byte —
  the 6.87 cyc/plane-byte fill rate); the cost is **~666 cycles per byte the painter delivers**,
  and both sides are measured. Rows 158..191 paid **−5.44 ms of phase 27** (the biggest decode
  prize yet: 0.080 ms/row over 68 rows) and **+5.41 ms of phase 32**, netting **+1.37 ms**.
  ⭐⭐ **61% of that cost — +3.29 of +5.41 — is paid with every call EARLY-RETURNING** (the arm
  where those rows are unowned), so the dominant term is the PLUMBING on bytes that turn out not to
  be in the domain: `plot_line_octant` and `undraw_plot_lines` also write 129..157.
  ⇒ **Read the ownership ledger's `st/f` column, divide by the row count, and count the writers'
  stores OUTSIDE the candidate block, before writing a painter.** And the two placements lose by
  two *different* mechanisms — in the writers' loops the call is an aliasing barrier (+3.45 ms),
  batched after them the second walk of the undo list is ~110 cyc/entry — so there is no third
  place to put the hook, and ⛔ **widening the block so the filter always succeeds is refuted by
  the same arithmetic** (the walk is paid per entry either way; delivering all ~210 of them costs
  11.0 ms against 4.8 of budget). ⇒ ⭐⭐⭐ **A MIRROR CAN ONLY PAY ON ROWS WHOSE WRITERS ARE NEARLY
  SILENT — a row with a busy writer is won by making that writer's own store land in the bitplanes
  INSTEAD of `mem[]`, not in addition to it.** `docs/span-render-plan.md` §11d.
  ⛔⛔⛔ **THE §12c MEASUREMENT THAT WAS SAID TO SUPERSEDE ALL OF THAT IS RETRACTED — BOTH ITS ARMS
  OWNED THE ROWS, AND THE CAUSE WAS A MACRO-NAME COLLISION THAT ALSO FROZE THE DASH NEEDLES ON
  SCREEN FOR FOUR COMMITS.** `revs_plot.h` spelled its invocation `REVS_PLOT_RECTS()` and gave the
  feature-off branch a no-op **of the same name**, so `#ifdef REVS_PLOT_RECTS` read TRUE with the
  feature OFF: `kDeltaBlock` took its three-block form in every build, display lines 158..191 were
  CLAIMED with no painter behind them, and the rev-counter needle and steering mark stopped moving.
  ⇒ "owning all 34 rows moves `convertRace` 7.22 → 7.29" compared **owned against owned**. With the
  collision fixed the control reads `convertRace` **10.13 ms and 80 cells/frame** against the
  broken 7.22 and 55 — so those 34 rows are worth **~2.9 ms, ~0.086 ms a row**, which is §11's
  ledger price and NOT ~0.
  ⇒ ⭐⭐⭐ **AND THE SHAPE OF THE PRIZE IS THE SCAN, NOT THE CELLS: 34 rows of ~2.9 ms while only
  25 more cells a frame get converted (25 × 13.6 cyc = 0.05 ms).** So the retracted rule — "owning
  a row is worth (the cells the dirty decode converts on it) × 13.6" — is false in both halves, and
  `ch/f` is **not** how to rank an ownership domain: the decode is ~99% SCAN, and a row costs what
  it costs to walk whether or not a cell on it moved. Rank by ROWS OWNED, as §11 always did.
  ⚠⚠⚠ **The method lesson is the larger half, and it is new: A SILENT `#ifdef` THAT IS ALWAYS TRUE
  DEFEATS AN A/B WITHOUT DEFEATING ITS BUILD** — no warning, no byte differential, no probe. **An
  A/B switch must print its own state** was already the rule; this adds **prove the CONTROL arm is
  the control** — `amiga/own_map.gdb` (which prints the owned RUNS, not a count) would have shown
  158..207 owned in a DASHOWN=0 build the first time it was run. `make macro-lint` is the guard.
  ⚠ **And separately, and this one stands: 13.6 cyc/byte is a WHOLESALE rate and does not
  transfer.** It is longword-batched, dirty-skipping and amortised over 40 contiguous cells; the
  same expansion over an 8-cell strip measured **143 cyc/byte**. Never price a small-region pass
  at a rate measured on a wholesale one. `docs/span-render-plan.md` §12c.
  ⭐⭐⭐ **AND THE RETARGET HAS ITS OWN BREAK-EVEN, 32x MORE PERMISSIVE, SO RANK AN OWNERSHIP
  DOMAIN BY STORES PER ROW — ASCENDING — AND THE LEDGER'S ORDER INVERTS.** One retargeted byte
  costs ~30-44 cyc (four plane bytes — two planes x two BUFFERS — less the `mem[]` store it
  replaces) against a mirror's 666, so the admission threshold is **12-26 stores per owned row per
  frame** against the mirror's **0.82 delivered bytes**, while the prize is ~789 cyc a row
  **whoever paints it**. ⇒ **a row's ownership COST scales with its writers' store rate and its
  PRIZE does not — so rank by `st/row`, ascending**, which puts the needles (1.6 st/row) first at
  −2.18 ms net and leaves the view sweep's own 117..157 AT break-even (6..28 st/row, ±0.6 ms a
  block): a row repainted 27 times a frame costs 27 retargets to own. A ledger's ROW PRICE cannot
  see that; only `st/row` can.
  ⚠⚠ **THAT `st/row` RANKING AND ITS "row ownership is closed" VERDICT BOTH REST ON THE RETRACTED
  ARITHMETIC ABOVE AND ARE SUSPENDED** — they were computed over "75 unowned rows" in a build that
  had wrongly claimed 34 of them, and their prize term came from `ch/f`. What survives untouched is
  the one MEASUREMENT in them: a cross-TU call placed in a writer's own loop is an aliasing barrier
  and cost **+3.45 ms**, so a retarget must be inline or the writer must be retargeted wholesale.
  ⭐ The live shape is neither a mirror nor a per-store retarget: the writer stops writing `mem[]`
  and hands the renderer its GEOMETRY (§12d's needles — a pixel list, turned into a PRERENDERED
  HARDWARE SPRITE on first sight), which has no per-store term, no undo list, no `mem[]` traffic
  and nothing in either playfield. ⭐ **Anything that moves over the cockpit belongs on a sprite or
  on PF2, never in the terrain's PF1** (user directive) — a hole in PF1 taxes every terrain painter.
  ⚠⚠ **And the arithmetic error that made the needles look like −4.67 is the transferable half: A
  MEASURED DELTA BELONGS TO EVERYTHING THAT CHANGED BETWEEN ITS TWO ARMS, so name what else moved
  before quoting it as one part's price.** §11d's headline −5.44 ms is arm B − arm 0, and arm B owns
  **68** rows (domains A+B) where arm A owns 34 ⇒ the needles' own prize is −5.44 − (−2.93) =
  **−2.51**, half what I first used. Same shape as §10q's residual-`D` error one commit earlier.
  `docs/span-render-plan.md` §11e.
⭐⭐ **And the decode's own 38 ms turned out to be CODE SHAPE, not algorithm — TWICE, and it is
~20 ms now** — the scan was re-reading two loop-invariant stack slots per cell and had spilled its
pointers into data registers; then `make DECODESPLIT=1` attributed the remaining "12 ms floor" and
four of its five rows were **byte loops over longword-aligned data** (−4.20 ms at `151e282`).
⭐ **A fixed per-FRAME row is invisible to a per-ROW or per-CELL ledger** — that is why the ledger
summed to half the phase, and why a split whose rows close against the unsplit row is what settles
it. **Read the objdump of a hot loop before theorising about its
algorithm**, and see `docs/perf-method.md` for what to look for (a `tst.l <n>(sp)` on a loop
invariant; pointers living in `d` registers).
  ⚠ **The counterweight is measured too, and the whole scan for a second instance came back
  empty**: on a register-poor machine **a stack slot is a legitimate home for a loop invariant**.
  One loaded *once* and read from there by each of fourteen out-of-line landing pads is
  indistinguishable, by counting, from the decode's per-cell reload — and undoing it measured
  **+1.38 ms**. ⭐ Rank by reloads **per iteration of the HOT PATH**, never by stack-slot operands
  per loop (an SCC aggregates every path), and identify that path from a census before believing
  a static ranking. `docs/perf-method.md` §the frame-slot defect class is exhausted.
⭐⭐ **The same class caught the span rasteriser a second time, for −4.79 ms: A HOT LOOP'S STATE
LIVES IN MEMORY IF ANYTHING TAKES ITS ADDRESS.** `sw_plot_*(…, &y, &carry, &abandoned)` and
`span_end_marker(…, &colMark, &carry)` *were* `span_walk`'s whole DDA state, so it sat in the stack
frame (265 `n(a5)` operands, three `pea`s per plot). Hand several small results back **packed in
one `d0`** instead. ⚠ And the routine got BIGGER — 1708 → 2217 instructions — and faster; on the
68000 a memory operand is 16-20 cycles against 4-8 for a register op, so **instruction count is not
the scoreboard.** Grep a hot kernel for `n(a5)` / `n(sp)` / `pea` before calling its shape clean.
  ⭐ **Second instance, −6.58 ms, and it names the cheapest fix:** the view sweep's `ViewState` was
  the 6502's A/X/Y and sat in the frame because `&v` reached two callees, so every `v->byte` in a
  per-line driver was a memory access. Hold it in **locals** and sync only around the COLD
  callee — and check whether the sync is needed at all first: the plants write one field that is
  dead in every caller, so 21 of 25 a sweep needed none.
  ⚠⚠ **But PACKING IS NOT FREE, AND THE TEST IS THE RATIO OF RELOADS TO PACKS, NOT THE OPERAND
  COUNT.** The 68000 has no byte-insert, so every pack/unpack is `swap`/`clr.w`/`or.l`/`andi.l`/
  `lsr.l` — ~40 cycles, i.e. what 2-3 `n(sp)` reloads cost. The span plotters paid because ONE pack
  at entry served a 232-step DDA loop; the same ABI applied to `view_paint_lines`'s three threaded
  bytes **cost +0.2 ms** because their address escaped only to CALLS (GCC already kept them in
  registers between calls), so eleven packs bought eighteen loads. ⭐ **Before packing, count packs
  against reloads in the objdump: a per-LOOP-ITERATION reload is the prize, a per-CALL one is
  already nearly free.** `docs/perf-method.md` §packing is not free.
⭐⭐ **The lever is the VIEW PIPELINE: `build_track_geometry` → `draw_road` → `view_paint_lines` is
the dominant subsystem and it is ONE subsystem** — the first two *produce* source bytes into the forty `$80`-spaced blocks
at `$3000..$4380`, the third is the single *consumer*. Current shares, every past change and its
lesson, and the standing conclusion that **the port's biggest costs are the MACHINERY the
transliteration is wrapped in, not the game's algorithms**: `docs/perf-method.md`.
⭐⭐ The first Phase 6 item is the REPRESENTATION, not asm: the engine plots into a BBC-shaped
frame buffer that a decode pass converts every painted frame — work the BBC never did, and asm
written against the current arrangement has to be rewritten after it.
`docs/span-render-plan.md`.

Rules that must survive without opening `docs/perf-method.md`:
- **Quote a framerate ONLY from `GDBSCRIPT=fps_series.gdb`** (in-program sampling, no gdb stop
  inside the window). Conditional-breakpoint scripts have read 0.02 where the truth was 0.78.
  ⭐⭐ **Compare its ROW VECTOR, never its `total painted` line, and average the non-outlier rows.**
  Under `FIXED_RNG=1` + warp the series is DETERMINISTIC row for row, so the limit is one row's
  RESOLUTION (one frame = 3.3%), not variance — eleven rows give ~0.3% and make a 2.5% change
  quotable. The `total` spans a partial trailing row and varies 3% between identical builds.
- **Use 30-second warp runs, not longer**: a `STRAIGHT_TO_RACE` run eventually leaves the track and
  resets, so a longer run DILUTES the measurement with a static scene instead of adding data.
- ⚠⚠ **A COMPUTE WIN PARTLY REAPPEARS AS `phase 28`, THE VBLANK SPIN — that is the `50/N` pad, not
  the change giving itself back.** `draw_road` −4.79 ms read as phase 28 +3.03 and a frame total of
  only −1.39; two identical control runs read the frame total 2 ms apart. **Size a change against
  `Σ(phases 1..39) − phase 28`, or against the single phase row you changed.**
- ⚠⚠ **THE CRASH/SESSION RESET HAS ITS OWN PHASE, 63, AND IS EXCLUDED FROM THE FRAME LIKE PHASE 0.**
  The crash hold can only end inside a drained 50 Hz tick, so for months every reset after it was
  billed to phase 26 ("the drain") — ~3.7 ms/frame amortised, making the drain look like a 7 ms
  lever when a band cycle is 119 instructions. `Σ(1..39) − 28` excludes 63 by construction; a
  change that speeds up the RESET is not a frame win, so read ph63 beside ph0 when two arms differ.
  ⭐ **A bracket can be left OPEN across a control-flow exit — when a row is implausible, find what
  closes it.** `docs/perf-method.md` §the drain was the reset.
- **FPS over-reads wins — under ~3% is noise.** Quote a static cycle count or a differential ratio
  as the win; quote FPS only as the standing baseline. ⚠ Per-iteration ("t/it") phase numbers are
  not a safer alternative (~±10% trajectory noise) and must never be diffed across builds. Phase
  brackets for SHARES, FPS for PROGRESS.
- **Measure a change with the in-process differential** (`make VERIFY=1 PROBES=1`), never
  cross-run; `make FIXED_RNG=1` for every perf run. A render-speed change shifts the sim's
  trajectory and otherwise measures a different workload.
- **Every framerate figure in an older note or commit is wrong — re-measure, don't quote.**
- ⚠⚠ **A TWIN CAN BE SLOWER THAN THE TRANSLITERATION, and an arithmetic one usually is.** The win
  comes from ALGORITHMIC COMPRESSION (a byte-pair shift loop the 68000 does in one word op), never
  from "being real C": a chain of `ADC`/`SBC` whose exit flags are live has no interpreter left to
  delete. Twins #14/#15 measured **9% slower** until `REVS_FLAG_OP`
  (`always_inline`) took `sub_from`/`sbc_step`/`adc_step` back inline — GCC leaves them out of line
  at -O3, so every subtract was paying a `jsr` + `movem.l`. **Grep the objdump for
  `jsr <sub_from>` before believing any arithmetic twin is fast**, and match the build to the
  control (a `PROBES=1` build reads low). `docs/perf-method.md` §twins #14/#15.
  ⭐⭐ **And the first thing to check after ANY size-changing edit to a hot function: count
  `jsr <hot-leaf>` in the objdump and require 0.** GCC's inlining threshold is part of the change
  in both directions — making a body smaller can drop it under the threshold and cost its
  specialisation, and making one BIGGER (inlining it twice to serve two configurations) can evict
  the leaf *it* calls. The second cost 4.3 ms/frame in the dash-edge walk, and register-pressure
  reasoning pointed the wrong way. `always_inline` on a hot leaf and `noinline` on a cold sibling
  are load-bearing, not hints. `docs/m68k-optimisation.md` §inlining threshold.
  ⚠⚠⚠ **AND THAT GREP MUST MATCH GCC'S CLONE SUFFIXES — `.constprop.N`, `.isra.N`, `.part.N` — SO
  GREP THE PREFIX `<name`, NEVER `<name>`.** A call emitted as
  `jsr <view_ev_note_addr.constprop.0>` reads as ZERO calls, which is precisely the answer this
  check treats as success. Measured 2026-09-20: it cost a wrong conclusion ("GCC deleted the
  calls") and a needless edit, while four calls sat in `interp_edge_core` untouched.
  ⭐⭐⭐ **AN OBJDUMP DELTA IS FOR FINDING A DEFECT, NEVER FOR SIZING ONE — ON A REGISTER-BOUND
  DRIVER IT OVER-READS BY ~6x.** Deleting the view sweep's nine-instruction stop tail (seven memory
  operands, 82 runs a frame) statically prices at ~0.7 ms and the phase table paid **0.118**; the
  three edits before it in the same body priced the same way and paid −0.733/−0.136/−0.118.
  ⭐⭐⭐ **The diagnostic is the SPLIT: if a deletion's win does not scale with the count of the
  thing it deletes, the instruction was not on the path the count describes** — that 0.118 was
  −0.111 in phase 2 (32 runs) and −0.008 in phase 3 (50 runs), which is inverted. ⇒ once a body is
  down to 2-7 instruction edits, measure one or move to a coarser lever.
  `docs/perf-method.md` §Rule 1b.
  ⭐⭐ **And read the INSTRUCTION COUNT beside the call list — 726 instructions for a routine that
  tests four bytes is the tell, and it needs no emulator run.** `view_span_line` sorts four
  breakpoints and emits ≤5 intervals; five compare-exchanges handed GCC a *permutation* and the
  unrolled interval chain behind them was specialisable per permutation, so it emitted ~one
  straight-line arm per ordering — 1739 cyc/line. ⭐ **A SORTING NETWORK IS A CODE-SIZE TRAP for
  the same reason a bounded loop over a short list is: it is branchy by construction, and whatever
  follows it gets copied once per outcome.** Sort in registers, emit from a LOOP.
  ⭐⭐ **The general form: a parameter that is a COMPILE-TIME CONSTANT at every call site must be
  `always_inline`d, or it is a memory operand in the inner loop.** A `const SpanPlotter*`
  descriptor in the span rasteriser's leaf cost 2.6% of the frame on its own
  (`docs/perf-method.md` §twins #25-#39). Two refinements, both measured:
  ⭐ **`always_inline` on the LEAF does not fold a descriptor — the SELECTION must be specialised
  too** (a leaf inlined into a caller that picks `cond ? &A : &B` still reads the fields out of
  memory), and ⚠⚠ **making a hot routine SMALLER can revoke its inlining and cost more than the
  edit saved** — a shared out-of-line copy takes its descriptor back as a pointer. Pin
  `always_inline` explicitly and **re-read the objdump's call list after the edit**: a `jsr <name>`
  where there were none is the tell. (`docs/m68k-optimisation.md`)
  ⚠⚠ **The rule holds for a LEAF IN AN INNER LOOP, not for a caller that has run out of
  registers — measure, don't assume it.** `view_plant` takes a literal `page` and a literal
  `opcode` at every call site and folding them does everything the rule predicts (the
  note-vs-forget test collapses to one list walk, the body lands at ~30 instructions), yet
  `always_inline` measured **+1.04 ms/frame** — phase 2's bracket gained and phase 3's lost more,
  because `view_paint_lines_core` grows 757 → 995 instructions and `paint_lines_short`'s per-line
  loop is already at the 68000's register ceiling. It stays out of line; the do-not-retry is
  written at the code.
  ⭐ **A BOUNDED LOOP OVER A SHORT LIST IS A CODE-SIZE TRAP, AND THAT IS HOW A CALL GETS PAID.**
  `for (i = 0; i < n; i++)` over a list that normally holds ONE entry makes gcc peel the trip
  count and unroll eight ways; two such searches plus their shift loops made `view_plant`
  348 instructions and so pushed it over the inlining threshold, costing every plant a
  five-argument call. **Terminate on a sentinel the list already carries and drop the count** —
  worth 0.8 ms/frame in the view sweep, and the end becomes positional.
  ⚠⚠⚠ **AND INLINING IS NOT THE ONLY DECISION AN EDIT CAN REVOKE — UNROLLING AND HOISTING GO THE
  SAME WAY, AND AN OUT-OF-LINE LANDING PAD IS A REGISTER-ALLOCATION BOUNDARY.** In
  `column_gap_walk_core` the loop invariants live in registers *precisely because* the bulky
  inlined classifier sits on cold landing pads; `__builtin_expect` on the arm the game always
  takes pulled it into the loop's main flow, GCC dropped the 4× unroll and evicted four
  invariants to absolute reads, and the change measured **+1.38 ms** for ~20 cycles/cell of
  branches saved. A six-instruction pure helper that deleted real memory traffic cost +0.32 ms
  the same way. ⭐ **The counting test: grep the objdump for each loop invariant's absolute
  address and require the count to stay at 1** — and treat a collapsing instruction count
  (1176 → 340) as the tell that the unroll went with it. So the decode's "spell
  `__builtin_expect` as the MISMATCH" **inverts when the cold arm is a large inlined callee**.
  `docs/perf-method.md` §a fragile local optimum.
- ⭐⭐⭐ **A COARSE TEST ONLY PAYS IF IT REPLACES THE FINE ONES — AND THE SCAN IT MUST DO TO
  QUALIFY IS PART OF ITS PRICE.** Stage A's span painter filled a group of four cells with one
  longword pair whenever the four sources were zero, on the reasoning that those are "the same four
  reads the chain does anyway". They are not: the chain's four reads **are** its four cell tests, so
  the group `or` pays them twice on the groups that fail and saves only 36 of 88 cycles on the ones
  that pass. A wholesale group measured **236 cycles against the 184** of the four cells it
  replaced — **+32 cyc/group on the arm that fires**, at the designed 74% hit rate, for +3.48 ms a
  frame. ⭐⭐ And a *free* version of it still modelled to a wash, which is what closes the idea
  rather than the tuning: the floor is the events (5.57/line) plus forty cells' two plane stores.
  ⇒ **Price a coarse arm as (what it deletes) − (what qualifying costs) − (what the slow arm now
  pays twice), and check the FREE version still wins before building it.**
  `docs/span-render-plan.md` §10q.
- ⭐⭐⭐ **A SAMPLE OF A MOVING THING IS NOT ITS RANGE — ENUMERATE A FOOTPRINT, NEVER SAMPLE IT.**
  `make fbwrites FILL=117-207` over 41 driving frames of a real BBC puts `plot_line_octant` in
  display lines 129..180 x cells 16..22; the truth is **158..191 x cells 16..23**, because a
  needle ROTATES and 41 frames do not hold every rev range. Both bounds were short and an
  area-based painter built on them was wrong on its second frame. The fix is an arm that
  SUPPRESSES the thing under test and lets the oracle's own histograms report the whole set
  (`make DASHBARE=1 DASHCHECK=1`). ⚠ And run it DRIVING: with `STRAIGHT_TO_RACE=1` but no
  `HOLD_THROTTLE=1` the car is parked, revs and steering are constant, the needles land on the
  same bytes every frame and the enumerator reports that nothing moves at all.
- ⭐⭐ **Before optimising a loop, check how many times it actually RUNS.** The span rasteriser's
  "~3 500 cycles per DDA scan line" was 24 ms divided by the wrong denominator; the real one is
  **43 spans a frame**, which caps the whole kernel's call-and-search surface at ~5 ms (it measured
  +0.8%). Price the ceiling against the leaf COUNTS before writing code, as with the phase table.
  ⭐⭐ **COUNT IT ON THE HOST — a temporary counter in the host build is a valid proxy for a call
  count and costs no emulator run** (the view sweep's line counts came out 36/16/25, exactly the
  target's split). Licensed for COUNTING only, never for timing. And ⭐ **when a win's per-call
  price comes out implausibly cheap, doubt the denominator**: "~135 plants a frame" was a guess,
  the real count is 25 a sweep, and the arithmetic on a known ms delta is what exposed it.
  ⭐⭐ **IMPLAUSIBLY EXPENSIVE IS THE SAME TELL, AND A BRACKET SPLIT'S SMALL ROWS ARE WHERE IT
  HIDES: subtracting a fixed control bracket over-credits the SMALLEST blocks**, because the
  subtraction error is a fixed number of cycles and the block is not. An 815 cyc/line control
  taken off a block whose true cost is ~280 made the view sweep's plants read 3x their real size
  and put a whole step in the live plan around them. **Read a split's BIG rows as sizings and
  check every small one against a count** — a host counter costs no emulator run.
- ⚠⚠ **A TWIN CAN ALSO BE SLOWER BECAUSE GCC WAS DELETING WORK.** A 6502 busy-DELAY loop whose
  only observable is its exit value gets folded away in the transliteration (final-value
  replacement), so the port never paid it; written out honestly in C it becomes a real burn. Twin
  #179's practice pad cost **5% of the framerate** this way, invisible to `validate` by
  construction (0 mismatch either way) and to every phase row (it runs on the 50 Hz body, so it
  taxes WALL CLOCK). **Diff the objdump on BOTH sides of a twin, and treat a pure cycle-burn as a
  faithfulness call to argue at the code, not to transliterate.** `docs/perf-method.md`.
- ⭐⭐ **A 6502 macro writes FIVE cpu fields; a routine usually reads one.** `SBC` stores A/N/V/Z/C
  (~16-20 cycles each on a 68000) and computes V through mask chains, so a subtract chain pays it
  over and over for flags that are dead at the exit. Where a flag genuinely escapes, use `sbc_value`
  for the chain and replay the ONE escaping flag from its operands (`sbc_overflow`).
- ⭐⭐ **On the render/geometry path an `adc_value`/`sbc_value` byte chain is just a binary 16-bit
  `+`/`-` — write it as native C.** Decimal mode is inventoried once and for all in
  `docs/static-map.md` §Decimal mode: all 8 `SED` sites are race-stats / marker-draw / front-end
  menu, NONE on `build_track_geometry`→`draw_road`, which is only ever entered with D=0. So the
  byte-pair carry idiom there computes nothing a `uint16_t` add doesn't; keep `adc_value`/`sbc_value`
  ONLY in twins of those 8 BCD routines. Render fixtures pin `c.D = 0` citing that table, and
  `make determinism-drive` is the backstop that D=0 truly holds on the path.
- ⚠⚠⚠ **AND THE BASELINE TRAJECTORY DECIDES WHICH CODE EXISTS AT ALL, NOT JUST HOW HOT IT IS.**
  `STRAIGHT_TO_RACE` is a **PRACTICE** session: the player is alone on track, every car slot is
  empty, and `move_and_draw_cars` (phase 17) therefore reads **0.21 ms** in every measurement this
  project has taken — 22 empty-slot tests and nothing else. A race draws **2.32 objects a frame
  against practice's 0.89**, i.e. **~5 ms the standing baseline does not contain**
  (`docs/perf-method.md` §the object plotter). ⭐ And **gate a session census on the session it is
  about**: `determinism-race` spends ~12000 of its 12600 frames in QUALIFYING, alone on track, so
  an ungated count reads 42% low. Ask what the chosen trajectory never populates.
- ⚠⚠ **SIZE A ROAD-PASS ROUTINE WHILE DRIVING, NOT PARKED — it is 8x.** `div16by8` runs 7.8 times a
  frame parked and **60.5 driving** (`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`), because
  `road_edge_start` reuses last frame's edge points. A parked call count next to a driving
  framerate is two different workloads.
- ⭐⭐ **The VERTB ISR is a tax on WALL CLOCK, not on the frame** — 50 fires a second whatever the
  framerate does, so a microsecond there is permanent and no phase row can see it. Measure it with
  `make PROBES=1 ISRSPLIT=1` (92 µs instrument floor, calibrated to 0.6%; a row within ~1× the floor
  is not resolvable). Two classes of fat live there and are invisible elsewhere: a `volatile`
  diagnostic counter (a 32-bit RMW, ~40 cycles, uncoalescable — 13% of `snd_tick`, now behind
  `SND_STAT()`) and an unmemoised recompute of an unchanged value (`program()`, 94.7% hit rate).
  `docs/perf-method.md` §The VERTB ISR.
- ⚠⚠ **NEVER RUN A MANUAL `REVS_MEM_DUMP=1` BINARY WHILE `make determinism` IS RUNNING** — they
  share `tmp/determinism/`, and the collision shows up as a determinism FAILURE that passes on a
  clean re-run. A whole-corpus gate failing intermittently is the last thing that should be
  waved away as a flake, so keep host probe runs serial with it and re-run twice before
  believing either verdict.
- **An A/B switch must PRINT its own state**, and any new instrument must be sabotaged before its
  output is believed.
  ⚠⚠ **And PROVE THE FLAGS REACHED THE BUILD — `zsh` does not word-split an unquoted parameter**, so
  `COMMON="PROBES=1 FIXED_RNG=1 …"; make $COMMON` passes ONE argument, `make` reads it as
  `PROBES = "1 FIXED_RNG=1 …"`, and **every flag after the first is silently never set**. Use a zsh
  ARRAY (`COMMON=(A=1 B=2)`) or literal flags, and assert the build's own fingerprint — the
  `probe-audit: clean (N symbols)` count is a function of the flag set. ⭐ A frame total that does
  not resemble the published baseline is the cheapest tell that a flag did not land.
  `docs/perf-method.md` §prove the flags reached the build.
  ⚠ **And `make CFLAGS+=-DX` / `EXTRA_DEFINES+=…` on the command line REPLACES every `+=` the
  Makefile makes** — the build silently loses `-O` and every feature define (it turned a driving
  census into a parked one). Add a Makefile `ifdef` for a temporary define instead.
- ⭐⭐⭐ **BOUND A PHASE-TABLE A/B IN EMULATED TIME, NOT HOST TIME — `make PROBEFIELDS=N`, and it is
  the protocol for every arm-against-arm comparison.** `diag_run.sh` bounds a run with `sleep`, i.e.
  HOST seconds, and under warp the emulator's throughput moves with the host's load, so two arms
  covered **75 s against 144 s of emulated time from the same 30 s window** and met different
  numbers of the engine's crash holds. Capping on FIELDS (not painted frames — a faster build drains
  fewer 50 Hz ticks per frame, so a frame cap gives unequal sim time) makes the trajectory identical
  and takes two identical control runs from ±2 ms to **+0.03 ms on a 197.53 ms frame**. Require
  `frozen=` non-zero on **every** arm before diffing, and check its value equals `N × 80120`.
  ⚠⚠ **A frozen numerator over a live denominator prints a PLAUSIBLE LIE** — the freeze stops the
  phase accumulators, not the program, so the body drain, the ISR, the view census and `g_beamEpoch`
  keep climbing (`ONE BODY TICK` read 379 µs against a true 1414; the census 5526 units/frame
  against 1440). The freeze SNAPSHOTS them and `phase4_prof.gdb` reads the snapshot; gating the
  counters instead would put a load in the loop whose unit count is being measured.
  `docs/perf-method.md` §bound the window in emulated time.

## Hard rules (violating these costs a day)

- **Faithfulness first.** Byte-identical twins: `make validate FN=<name>` must show **0 mem
  mismatch**. Validate against the 6502 + a real BBC emulator, not the host backend.
  ⭐⭐ **But a fixture models the GAME, not the input space.** A twin must be correct on the data
  the engine actually produces; `fill_random` is the wrong default for a byte whose
  REPRESENTATION is constrained (packed BCD, a bounded index, an in-range pointer). **If the
  harness is the only reason a twin is not written the obvious way, that is a fixture bug** —
  narrow the domain, state the invariant at the fixture, and fix a runtime violation in whatever
  WROTE the byte. It cost a software reimplementation of the NMOS decimal `ADC` in place of the
  68000's one-instruction `ABCD`. How to narrow one honestly: `docs/validation-harness.md`
  §THE DOMAIN RULE.
  ⭐⭐ **And the same correction applies to the OUTPUT side: validate RESULTS, not implementation
  details** (user-stated). Faithful means faithful *from the player's point of view*, not an exact
  replay of a three-register machine's spills — a scratch `mem[]` cell the oracle stores and
  nothing outside the twin reads is not a result, and reproducing it is pure byte traffic the port
  cannot afford. Exempt one only with a **written reader audit** (the readers include the
  transliteration a track hook re-enters, and the next pass in the pipeline), scoped through
  `set_ignore`. `docs/validation-harness.md` §THE RESULTS RULE.
  ⭐ The audit instrument is `make rangeaudit DEFUSE=1` (each read paired with the store whose value
  it saw), run on all five circuits and followed ONE HOP past any copy — a read is not yet a use.
  ⭐⭐ **A twin that is deliberately BETTER than its oracle (a user-accepted departure, e.g. the true
  68000 ratio) is gated by `set_tolerance`, never `set_ignore`**: the twin exact to its own spec,
  the oracle within a DERIVED error model, a census that must fire, sabotage at bound + 1 — and
  `viewdiff` against HEAD's own result on the same captures, every new gated byte classified.
  `docs/validation-harness.md` §THE TOLERANCE MODE.
- ⭐⭐ **`bus_write`/`bus_read` MUST NEVER BE USED WHERE THE TARGET IS KNOWN NOT TO NEED THEM**
  (user directive) **— and inside the renderer that is everywhere.** They exist for the HARDWARE
  window ($FC00-$FEFF); a pure-RAM access must not pay their range test. The transpiler already
  routes every *constant* non-hardware address straight to `mem[]`; what leaks is the **indirect
  modes** (`(zp),Y`, `(zp,X)`) — the plotters. Measured on the host: **~10 600 bus calls a game
  frame, 144 of them real hardware.**
  ⭐⭐⭐ **THE ANSWER IS A STATICALLY KNOWN TARGET, NOT A CHEAPER TEST — ASK WHERE THE POINTER IS
  BUILT, AND YOU WILL USUALLY FIND AN IMMEDIATE.** The whole view sweep writes `$6700 + cell*8`
  because `view_paint_lines` ($7BE2) opens `LDA #0 / STA $70 / LDX #$67 / STX $71` — the
  destination is an *immediate operand in the game's own code*, and a monotone `+1`/`+$138` walk
  from there cannot reach $FC00 in the 256 steps a byte line counter allows. So the else arm was
  **unreachable, not cold**, and the test selecting it was a constant 1. GCC could not fold it for
  one reason: the base is laundered through `plot_ptr_v`, a GLOBAL fifteen unrelated routines use
  as scratch, so constant propagation dies at the global. ⇒ **carry a hot destination in a LOCAL.**
  `base_span_is_ram` stays for the walks whose base really is a runtime value (script pointer,
  copy destination, field address) — one check per page, never per byte, else arm kept.
  ⛔⛔ **DO NOT PUT THE ELSE ARM BEHIND A `noinline` ESCAPE — MEASURED +0.73 ms, AND THE MECHANISM
  IS THE GENERAL LESSON: BULK IN A COLD ARM IS CHEAP, A CALL BOUNDARY IN A HOT LOOP IS NOT.** The
  escape deleted 598 instructions (`view_paint_lines_core` 1710→1366, `view_own_run` 771→517) and
  cost time, +0.61 ms of it the unit loop's arm alone. An INLINE `bus_write` gets merged with the
  fast arm's store, so neither path contains a call and its ~200 instructions are cold code that
  never runs; a `noinline` callee is an **aliasing barrier** — GCC must assume it writes any
  memory, so the surrounding loop spills (`view_own_run`'s `n(sp)` operands 17→30, CSE'd
  source-displacement reads down ~40%). ⚠ And I first blamed that regression on a hoisted
  `busSafe` local's live range; removing the live range measured **identically**, so
  register pressure was the wrong story — **separate the arms before believing a mechanism.**
  ⚠ Widening `mem[]` to `uint16_t*`/`uint32_t*` is NOT the fix — see the endianness rule below; it
  is legal only when every byte of the wide value is the same.
- ⭐⭐ **THE TOOLCHAIN'S `memset`/`memcpy`/`memmove` ARE BYTE LOOPS (24-40 cyc/BYTE) — the Amiga
  build wraps them with longword versions (`src/platform/amiga/fastmem.c`, `make FASTMEM=0` is the
  control, `make fastmem` the differential, `make FASTMEMCHECK=1` the target-side postcondition
  check). Two traps if you touch that file: `-fno-tree-loop-distribute-patterns` is **load-bearing**
  (GCC turns its own small byte loops into a `memset` call, which `--wrap` sends back into
  `__wrap_memset` — infinite recursion), and `-funroll-loops` peels an already-unrolled loop four
  times more for pure loss. ⚠⚠ **And a target PIXEL diff CANNOT gate a change like this**: a
  render-speed change moves the simulation's trajectory, so the two arms are never on the same
  scene (measured: two `screen_dump.gdb` runs broke four fields apart). `docs/perf-method.md`.
- ⭐ **THE PHASE TABLE'S `ms/frame` COLUMN IS INTEGER-TRUNCATED AND HIDES ~28 ms in twenty rows** —
  compute a small row as `ticks / frames / (frozen/3000/20)`. ⚠ And **read `calls=` before diffing
  any row**: phase 34 is `calls=1`, a ONE-SHOT amortised over the window, so the recurring frame is
  ~169 ms where the table says 172 and a longer run reports a different number for the same binary.
- **RAM is uniformly slow — there is no "fast RAM" on the target A500.** Optimise by reducing the
  NUMBER of reads/writes, never by moving data to a "cheaper" buffer. (`docs/m68k-optimisation.md`)
  ⭐ **And never explain a measurement with fast-vs-chip RAM** (user, 2026-09-12): on an A500 "fast
  RAM" is usually slow RAM on the same bus, and even off-bus the difference is negligible because
  the 68000 is simply slow and every access costs. It is a 68020-era distinction — it is not a
  reason the rig and the target differ, and not a reason a buffer is cheaper.
- **NEVER emit a 32-bit software mul/div** (`__mulsi3`/`__divsi3`/`__udivsi3`/`__modsi3`/
  `__umodsi3`) — the 68000 has none. Use `src/cpu/m68k_math.h`'s 16-bit helpers. `amiga/Makefile`
  audits every link (`muldiv-audit`); keep it clean.
- **Copper bitplane POINTER swaps happen in the VBI ISR, never mid-frame** — a torn pointer
  garbages the whole viewport for a frame. Colour-only pokes mid-frame are tolerable; `SPRxPT`
  operands are stricter still (the copper reads them at scanline 16).
  ⚠⚠ **"In the VBI ISR" ≠ "in the vblank":** anything after the handler's own work lands 100+
  scanlines into the display. The copper work goes FIRST, and `g_beamPresentsLate`
  (`amiga/beam_watch.gdb`) must stay 0 — a rebuilt copper WAIT behind the beam blocks the copper
  for the whole field and no frame-boundary dump can see it. (`docs/amiga-lessons.md`)
- **Work in the vblank ISR is capped at ONE FRAME.** Over that you silently drop a displayed
  frame, and the dropped frame is what the user reports — not the cost.
- **`mem[]` is little-endian; the Amiga is big-endian; the host is little-endian.** Never alias
  `mem[]` as `uint16_t*`/`uint32_t*` for a general value — it reads correct on the host and
  byte-swapped on the target, so `make validate` stays green while the Amiga renders garbage.
  `make endian-lint` guards this.
- **An interrupt handler that leaves a register untouched ⇒ the copper list must too.** But every
  write-only register is still per-scene state with a named owner (`docs/amiga-lessons.md`).
- **The ISR shim must reproduce the MOS's IRQ entry, not just call the handler.** Measured
  contract: A, X and Y are ALL preserved across an engine-context interrupt, and A arrives via
  `mos_irq_a` (`$FC`), which only the MOS's own IRQ entry ever wrote. Asserted at the seam on both
  backends (`g_irqClobberCount`); keep it at 0. ⚠ Invisible on the host, where the ISR fires at a
  controlled point.
- ⭐⭐ **A HOOK/SMC SEAM MUST HAND OVER EVERY REGISTER THE 6502 HAS LIVE THERE** — derive the set
  from the SURROUNDING INSTRUCTIONS, never from what the unpatched callee happens to read.
  Silverstone's callee is not the contract: an expansion circuit's hook reads registers it does
  not. ⚠⚠ And **the patched arm of every hook seam is gated by NOTHING** — `validate`,
  `determinism` and `-drive` all race Silverstone; `tracks` proves the bytes land and `track-run`
  proves the code RUNS, neither that it computes. So settle a hook seam against the real BBC
  (**`make viewdiff`**, which is exactly that differential per circuit), and reach for the phase
  canary (`make INK_WATCH=1`) when a cell is corrupted by an unknown writer.
  ⚠ A stale-register handover is the recurring shape of this defect: the hook runs an INDEXED
  loop, so a wrong Y does not fail, it addresses a NEIGHBOURING TABLE and computes something
  plausible ($2538's leaked `Y=$30` put `$5E68+Y` on `edge_x_hi[8]` and cost one horizon scan
  line on two circuits).
  ⭐⭐ **And a hook can jump BACK INTO the transliteration**: `region_23d8` — `road_edge_walk`'s
  body, dead on Silverstone because the twin runs the whole walk — is re-entered at `$2490` by every
  expansion circuit's hook, so the rest of that walk runs transliterated, reading `mem[]` cells the
  twin's own path no longer uses. **A `region_*` / `FUN_*` name is SHIPPING code until proven
  otherwise, and "proven" cannot come from a Silverstone run.** Never conclude a cell is
  twin-private from a native-surface scan alone. (`docs/faithfulness-seam.md`,
  `docs/wide-value-cleanup.md` §FOURTH eligibility test)
- **A 6502 idiom that touches the STACK POINTER has no C equivalent and is dropped silently** —
  suspect that class first for any hang inside generated code. When the idiom manipulates `S` to
  talk about RETURN ADDRESSES, model the control flow and leave `S` alone: modelling neither is a
  hang, modelling the register too is a silent leak that wraps `S` into page 1. `g_stackLow` AND
  `g_stackHigh` must both read inside `$F3..$F8`; `make STACK_TRAP=1` prints one host backtrace at
  the first breach either way, and `make gen` fails on any undeclared `TSX/INX/INX/TXS`.

## Working conventions

- ⭐⭐ **"What is next?" is answered by `docs/open-work.md` + `make todo`, never by a session
  summary** (which only remembers what that session touched). It is a QUEUE like
  `docs/rename.md`: an entry is **DELETED** in the commit that closes it, and what the work
  taught goes in the doc that was wrong. ⛔ Its CLOSED section is one line per measured dead
  end — read it before proposing a lever, so a negative result is not re-derived.

- ⭐⭐ **CLEAN THE C BEFORE WRITING ASM** (user rule): before an asm twin, the C must already be
  sensible — no byte-wide `mem[]` traffic in a loop, no zero-page scratch cells as working state,
  no dead exit-register/flag replay, no SMC-opcode dispatch. Single-step, fix the C, re-measure,
  and only then price asm for what is left.
- ⭐ **GATE A CHANGE BY WHAT IT CAN AFFECT, NOT WITH THE WHOLE BATTERY** (user rule): an asm check
  on Silverstone (plus ONE circuit only on a hook-patched path), 2-3 sabotages, host gates only
  when host-compiled code changed, one pricing pair — the six-circuit sweep and `viewdiff` belong
  to a checkpoint every few commits, not to every commit.
- **Commit directly to `main`** (no feature branches). Commit each fix as soon as it is confirmed
  to work — one logical change per commit.
- **Misnamed or unnamed things:** whenever a function, table or `mem[]` cell contradicts its name —
  or has none and you are about to reason about it — append it to `docs/rename.md` immediately
  (address, current name, actual behaviour, suggested name). Do not rename piecemeal in generated
  files; `disasm/symbols.csv` is the source of truth and the transpiler applies it.
  **On a binary-only project the names are your map.**
- ⭐⭐ **`docs/rename.md` is a QUEUE, not a log.** An applied rename is **deleted** from it in the
  same commit that applies it; `symbols.csv` then carries the name and its evidence, and what the
  old name *cost* goes in the doc that was wrong. No DONE entries, no history, no process prose.
  **Apply the queue as part of the work, not when asked** — renaming is cheap right up to the
  moment a hand-written twin references the name, and never again.
- **Newly-found dispatch targets / interrupt handlers:** add them to
  `ghidra_scripts/entrypoints.csv` the moment you find one — they are reachable only via indirect
  vectors, so Ghidra never finds them on its own. Record it immediately; don't defer.
- **Mark assumptions as assumptions.** `[ASSUMED]` vs `[DERIVED]` vs `[INFERRED]`, in symbols.csv
  notes and docs alike. The postmortem's failure mode is an assumption calcifying into a documented
  fact; a measurement replaces the tag.
- **Keep this file small, and keep it a rulebook.** New hard-won detail goes in the matching
  `docs/` file (add a row to the index above if it is a new one). Nothing dated, nothing
  celebratory, no measurement history here.
- Ask the user at genuine decision points (they're an experienced retro-porter and want to steer
  architecture/scope choices).
