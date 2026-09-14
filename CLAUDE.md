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
of live code including the view rasteriser and the wing mirrors — at *runtime* from the tails of 41
`$80`-spaced blocks at `$3000`, and stows it back before returning to MODE 7, so the page is empty
in every static image and every out-of-race RAM dump. `make dashcode` replays it into
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
make determinism-race      # ⭐ ...and THE RACE PROPER (session_is_race = $80), the ONLY target
                           #   that reaches any `& $80` arm — every other determinism run is a
                           #   PRACTICE session.  ⚠ 13000 frames, RELEASE=1, ~2 min: ~12000 of
                           #   them are the qualifying session the grid is reached through
make todo                  # ⭐⭐ WHAT IS OPEN: docs/open-work.md's queue + a live sweep for
                           #   TODO/FIXME/HACK markers in the tracked, non-vendored tree.
                           #   Expected output is "none" — a printed marker is either a real
                           #   work item for the queue or a stale marker to delete
make endian-lint           # fail if mem[] is aliased as a wide pointer
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
./diag_run.sh N # headless probe run for N seconds (needs a PROBES=1 build)
make PROBES=1 ISRSPLIT=1  # ⭐ split the VERTB ISR into its own timed slots (amiga/isr_split.gdb)
                          #   — the ONLY instrument that can see it, because the ISR's time is
                          #   charged to whichever phase it preempted.  ISRCAL=1 adds the known-
                          #   quantity calibration burn; ⚠ `make clean` when you turn it back off
EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=x.gdb ./diag_run.sh 60   # ⭐⭐ ~4.9x faster, same numbers
```

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
| **`docs/span-render-plan.md`** ⭐ | **Before touching any plotter or any Phase 6 asm — THE live rendering plan.** The replacement architecture: world points → spans → bitplanes, §10 sizing, step 1 status, §10m the SPRITE lever. (⛔ `docs/direct-bitplane-plan.md` is the OBSOLETE earlier plan — kept only because source/docs cite its §-numbers; read it as history, never as a plan) |
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

**Target: 50 FPS on an A500. Floor: 25 FPS** (user decision; reachability unknown). These are
*displayed* frames (`50 * g_fpsFrames / g_vbiCount`). The **50 Hz sim tick is separate and not
negotiable** — 25 FPS means painting every other frame with the simulation still at full rate.
The A500 is a 7 MHz 68000 and a frame is 20 ms: spending 10 ms on *anything* is half the budget.
Be conscious of absolute milliseconds always.

**Baseline: a ~209 ms FRAME** — i.e. ~10.5 display fields (`PROBES=1 FIXED_RNG=1
STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` + `phase4_prof.gdb`, warp, 30 s, driving, 2026-09-13 at
`9dfdf4e`). **This is the number a change is sized against**, and it is both the bracketed total
(`Σ phaseTicks[1..39]`) and `(elapsed − phase 0) / loopFrames` — they agree to 0.02 ms, so the
brackets account for the whole frame. ⚠ **The raw `elapsed / loopFrames` reads ~219 ms and is NOT
the frame**: phase 0 is boot plus the engine's own 2-second crash pauses. The displayed **~4.93 FPS**
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
⚠⚠ **Writer-maintained framebuffer dirty maps are also dead as a shipping optimisation.**
`CHANGEDIRTY` was complete and byte-exact (0/23 oracle mismatches), but its per-store compare and
two-map RMW traffic measured ~8.5% slower than the batched shadow scanner, so **the code is reverted
and the flag no longer exists** — only its write-up survives. Do not retry it without producer-native
change events.
⭐⭐ **And the decode's own 38 ms turned out to be CODE SHAPE, not algorithm: ~22 ms now, +8.1%
end to end** — the scan was re-reading two loop-invariant stack slots per cell and had spilled its
pointers into data registers. **Read the objdump of a hot loop before theorising about its
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
- ⭐⭐ **Before optimising a loop, check how many times it actually RUNS.** The span rasteriser's
  "~3 500 cycles per DDA scan line" was 24 ms divided by the wrong denominator; the real one is
  **43 spans a frame**, which caps the whole kernel's call-and-search surface at ~5 ms (it measured
  +0.8%). Price the ceiling against the leaf COUNTS before writing code, as with the phase table.
  ⭐⭐ **COUNT IT ON THE HOST — a temporary counter in the host build is a valid proxy for a call
  count and costs no emulator run** (the view sweep's line counts came out 36/16/25, exactly the
  target's split). Licensed for COUNTING only, never for timing. And ⭐ **when a win's per-call
  price comes out implausibly cheap, doubt the denominator**: "~135 plants a frame" was a guess,
  the real count is 25 a sweep, and the arithmetic on a known ms delta is what exposed it.
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
- **An A/B switch must PRINT its own state**, and any new instrument must be sabotaged before its
  output is believed.

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
- ⭐ **`bus_read`/`bus_write` are for the HARDWARE window ($FC00-$FEFF), and a pure-RAM access
  should not pay their range test.** The transpiler already routes every *constant* non-hardware
  address straight to `mem[]`; what leaks is the **indirect modes** (`(zp),Y`, `(zp,X)`) — the
  plotters. Measured on the host: **~10 600 bus calls a game frame, 144 of them real hardware.**
  In a twin, hoist the test to wherever the *pointer* is known (one check per scan line, not per
  cell) and keep the else arm. ⚠ Widening `mem[]` to `uint16_t*`/`uint32_t*` is NOT the fix — see
  the endianness rule below; it is legal only when every byte of the wide value is the same.
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
