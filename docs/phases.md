# Phase plan

Ordering follows the postmortem's checklist rather than the Atari port's actual history — the
whole point is that the discovery and validation infrastructure comes first, not reactively.

**Status legend:** ✅ done · 🔧 in progress · ⬜ not started

---

## Phase 0 — Scaffolding ✅

Repo structure, gitignores, the reusable machinery carried over from the Atari port (6502 CPU
model, platform abstraction, Amiga framework + build system, Ghidra scripts, the transpiler), the
docs that encode what that project learned, and a **bring-up skeleton that runs on the target**.

Verified: host `make` + `make validate` + `make endian-lint` clean; Amiga `make` links with clean
muldiv and probe-symbol audits; a headless FS-UAE run reports `vbi=824 painted=803` on an
`FPSCOUNT=1` build (≈48.8 FPS with nothing yet to draw) and `painted=0` on a plain one — display
takeover, 50 Hz VERTB handler, copper list, frame pump, and the embedded 6502 image all live and
readable from gdb by name.

One real bug was found and fixed by running it rather than reading it: `--gc-sections` was dropping
the framerate counter, and gdb reported instruction bytes as a frame count
(`painted=1223110688`).  Guarded now by `PROBE_SYMS` + `make probe-audit`
(`docs/method-lessons.md`).

---

## Phase 1 — The reference loop ✅ (exit criteria met; loop keeps growing as later phases need it)

`docs/bbc-reference-loop.md`.  There is no `atari800` here; ground truth has to be built.

1. ✅ Install + smoke-test jsbeeb (headless oracle) and b2 (debug HTTP API on `:48075`).  Both
   working, vendored at `tools/jsbeeb` and `tools/b2` (git-ignored). jsbeeb's `debugInstruction`
   hooks are the primary scripted oracle; b2's HTTP API (`peek`/`run` confirmed) is available as a
   secondary/interactive cross-check.
2. ✅ **Boot the real disc, break at the engine entry, dump RAM, diff against
   `disasm/revs_mem.bin`.** Done for all five tracks: `$1200-$6FFF` (the whole REVS2 engine) and
   each track's own data block are **byte-identical** to `make image TRACK=<name>`'s
   `disasm/revs_mem.bin` — no `ssd_load.py` fix was needed for the regions that matter. Remaining
   diffs are zero-page/workspace/ROM, which `revs_mem.bin` never modelled and don't affect the
   transpiler's inputs. Not yet wired into `make` as a repeatable check (still a manual jsbeeb
   script run per track). Full detail and the scripted commands:
   `docs/bbc-reference-loop.md` status section.
3. ⬜ Capture reference state at named milestones, committed as files.

**Exit criteria:** the composed memory image is confirmed byte-correct against a real BBC, and a
scripted "boot to milestone, dump RAM" command exists. **Both are met** — remaining Phase 1 work
(named milestones, the jsbeeb cycle-diff harness, the CRTC/ULA analyser, b2) rounds out the loop
but no longer gates Phase 2.

> ⚠ Zero-page/workspace addresses derived from `revs_mem.bin` are still provisional (real MOS/
> BASIC leaves them populated; `ssd_load.py` doesn't model that). Addresses inside `$1200-$6FFF`
> (REVS2) and the per-track data blocks are now measured, not provisional, for Silverstone.

---

## Phase 2 — Complete static map ✅ (exit criteria met; two residuals carried forward)

> ⚑ **Cheaper than the postmortem assumed:** a fully annotated source reconstruction of BBC Revs
> exists (`docs/reference-sources.md`), so items 1 and 4 are **cross-checks against a reference**
> rather than open-ended searches.  The reference has no licence — it is a map, never a source.

1. **The entry-point sweep** — `docs/entrypoint-sweep.md`.  Every indirect jump, RTS-dispatch
   table, OS vector and hardware vector enumerated and seeded in
   `ghidra_scripts/entrypoints.csv`, before any C is generated.  Self-modifying routines listed —
   including the **known** case: the per-track engine patches (`ModifyGameCode`, `CallTrackHook`,
   `Hook*`), whose inventory falls out of the Phase 1 before/after RAM dumps.
2. **The hardware-access map** — retool `DumpHwAccesses.java` for `$FC00-$FEFF` + `$0200-$0235`.
   Its output *is* the abstraction boundary, and it replaces the **[ASSUMED]** rows in
   `docs/bbc-hardware.md` with measured ones.
3. **Enumerate every MOS call Revs makes** (`docs/bbc-hardware.md` §MOS calls) — the genuinely
   new piece versus the Atari port.
4. **One concentrated behavioural-naming pass** into `disasm/symbols.csv`.

**Findings live in `docs/static-map.md`** — read that, not this summary.

Status:

1. ✅ **The entry-point sweep.**  `tools/sweep_entrypoints.py` (independent recursive-descent) and
   Ghidra agree: 7079 vs 7083 instructions, 222 vs 236 functions.  Roots no static walk can reach:
   the engine entry `$63BD` and the `IRQ1V` handler `$4E5C`.  No indirect dispatch or RTS-tricks
   anywhere else.  24 self-modifying sites located precisely, clustered in `$2C00-$2FFF`.  A jsbeeb
   execution trace (`tools/bbc_trace.mjs`) cross-checks it: nothing executed that the walk missed.
   ⭐ **The sweep also found that the premise was wrong** — REVS2 unpacks itself before running, so
   `revs_mem.bin` was never the right thing to disassemble.  `make runtime` builds the real image,
   verified against a BBC.
2. ✅ **The hardware map.**  `DumpHwAccesses.java` retooled for the BBC; 19 registers, 4 devices.
   Every **[ASSUMED]** row in `docs/bbc-hardware.md` is now **[DERIVED]**, and three of them were
   *wrong*: the interrupt is a User VIA timer not System VIA vsync, and neither the ADC nor the
   sound chip is ever addressed directly.
3. ✅ **The MOS-call inventory.**  Four entries, 17 sites, reason codes read out.  The engine never
   calls the filing system at all.
4. ✅ **The per-track hook inventory** (`tools/track_hooks.py`) — five shared patch sites, 6-7 code
   hooks per track, with the engine's own unpack subtracted out so the numbers mean something.
5. ✅ **The track programs.**  `ModifyGameCode` read out in full and cross-checked against the
   differential with **zero** discrepancies in the direction that matters; 238 instructions of
   track code measured per circuit.  `$5A22` is `CallTrackHook`, a fixed engine→track-file entry
   (`JMP $5700` in all four expansion tracks, `RTS` on Silverstone).
6. ✅ **Static coverage.**  Unclassified bytes cut from **5327 to 685** by a zero-page pointer
   pass, splitting runs at unpack boundaries, and naming the unpack's own leftovers.
7. 🔧 **The naming pass.**  **129 symbols** applied — 19% of call targets but **45% of call
   sites**, because the pass worked down by caller count.  13 marked `[PROVISIONAL]`.

**Exit criteria:** ✅ `listing.txt` has no referenced-but-undisassembled address (the sweep reports
zero undecodable bytes reached, and the residual 685 unclassified bytes are two runs of ~85%-zero
buffer, not code); ✅ the hardware and MOS-call inventories are complete; 🔧 names are roughly right
across the call graph but thin in the physics interior.

**Why the naming residual does not gate Phase 3:** `disasm/symbols.csv` is the transpiler's input,
so a name learned in Phase 4 or 6 propagates through the whole generated corpus on the next
`make gen`.  `docs/toolchain.md` says it outright — the cost of being only roughly right early is
near zero, *provided* the seam exists, and it does.  What genuinely had to be finished first was
the structural work (items 1-6), because a wrong entry set or a wrong memory image poisons
everything downstream.  Both residuals are carried in `docs/static-map.md` §Open items.

---

## Phase 3 — Transpiler quality, then generate ✅

`docs/transpiler.md`, including its porting checklist (now ticked).  ⭐ **Clean C is a
prerequisite, not a later optimisation** — liveness-gated flag elision, named `mem.h` accesses,
folded load→store idioms *before* mass-generating, because one transpiler improvement upgrades
the whole corpus and shrinks the twin backlog.

**Exit criteria: met.**  `make gen` produces 16289 lines of C from 229 routines / **7079
instructions** — exactly the figure `tools/sweep_entrypoints.py` reaches independently — building
clean on the host (`make`, `make validate`, `make endian-lint`) and on the Amiga (`make` with
muldiv-audit and probe-audit clean).  `revs_validate_list.h` is emitted, empty, so fixture-or-fail
is live from twin #0.

What the phase actually produced, beyond the mechanical retarget:

1. **The self-modifying rasteriser is generated, not hand-stubbed.**  All 24 sites are one of
   three mechanical classes with an exactly faithful runtime-dispatched form, so `$2C00-$2FFF`
   stays regenerable and keeps working for whatever its writers poke — including the `$2F23` hook
   only two circuits install.  `MANUAL_FUNCS` is empty.  (`docs/transpiler.md` §Self-modifying
   code.)
2. **Four silent-wrongness classes turned into loud ones**: an unlisted SMC value, a `BRK`, a
   ROM call that is not a MOS entry, and a `VALIDATE_FUNCS` entry with no fixture.  Three of them
   fail at generation time; the other two report at run time.
3. **Two code-dropping boundary bugs fixed** — orphan runs ending in a terminator (one of which
   was the IRQ1V chain-on) and `JSR` to a nested function start.
4. **`SPINWAIT_HOOKS` deliberately empty**, with a 41-entry candidate report for Phase 4 to
   resolve against the real machine instead of guessing.
5. **Both builds now boot the RUNTIME image** (`make runtime`).  They still loaded the pre-unpack
   one, which was correct only while nothing executed 6502 code.

Carried forward: the `$7Bxx` calls into a page nothing loads (`docs/static-map.md` §Open items) —
now trapped rather than silently no-op'd, so Phase 4's first run answers it.  ✅ Closed after
Phase 4: nothing *loads* the page, the game *builds* it, and it now runs (§Open items 6 and 10).

---

## Phase 4 — End-to-end skeleton ON THE TARGET, then profile ✅ (exit criteria met)

Postmortem #4.1: get *something* running end to end on the real machine as early as possible, and
**profile it before choosing what to optimise**.  On the Atari port an "algorithmic floor"
conclusion was reached by reasoning and later disproven by hand-asm — a crude full-pipeline
profile would have shown up front exactly which handful of functions ever needed asm.

**Exit criteria:** the genuine entry chain runs under `PlatformAmiga`, a framerate exists from
`FPSCOUNT=1` + `fps_seg.gdb`, and a profile names the hot functions.

⭐ **The target is 50 FPS, floor 25** (user decision — `docs/perf-method.md` §The target).  That
is a goal, not a baseline: Phase 4's job is unchanged, which is to produce the first honest
number and the hot-function list.  Exiting the phase does *not* require hitting the target —
it requires knowing the distance to it.

Phase 3 leaves three specific questions for the first target run to answer, all of them
instrumented rather than guessed.  **All three fired, and all three were findings** — the
instrumentation is the whole reason this phase cost hours rather than days:

- **Spin-waits: exactly ONE of the 41 candidates needed a hook.**  `$1760` (`LDA $62F7 / BMI`)
  is the main loop's frame boundary.  The other obvious candidate, `$4E11` (`BIT $FE4D / BEQ`),
  is a *hardware* wait and is answered by `Platform::hwRead` instead — hooking it would have
  worked and been wrong, because it would have hidden that `$FE4D` was unmodelled.  The
  remaining 39 are ordinary counted loops and intra-frame palette writes.
- **The `$7Bxx` calls ARE reached — three of them are in the main loop, every frame.**
  `g_brkCount` climbed ~3 per game frame.  ✅ **Resolved after the phase closed**: the page is
  the wing mirrors and the dashboard, *built* at runtime by `copy_dash_data` (`$18EA`), and it
  is now ingested and executing (`g_brkCount` = 0).  Reading out its 42 self-modifying sites
  cost the baseline 36% — see §The numbers below (`docs/static-map.md` §Open items 6 and 10).
- **An SMC slot took an uncovered value**: `$2F89` held `$88` (`DEY`) — the opcode slots have
  three values, not two, and the span loop can walk backwards.  Chasing it also turned up
  **13 bytes of code at `$1DC5` that no static walk can reach**, via the patched branch at
  `$1DD4`.  Both written up in `docs/static-map.md` §Open items 7.

### What the phase produced

1. **The genuine entry chain runs under `PlatformAmiga`.**  `Revs::run()` → `engine_main()`
   (`$63BD`), and gdb shows the real stack: `engine_main → engine_init → front_end_menus →
   FUN_655C → FUN_16DC → platform_render_frame`.  The host backend runs the identical chain.
2. **The MOS layer exists** (`src/platform/mos.cpp`) — the whole closed surface, four entries
   and eight OSBYTE reason codes, implemented ONCE for both backends so they cannot disagree
   about the OS.  Unhandled calls are counted, not absorbed; that is what found OSBYTE 0.
3. **The BBC hardware model exists** (`src/platform/bbc_hw.cpp`) — two VIA flag bits, which is
   all Revs blocks on, plus the ULA/T1 registers recorded for Phase 5's copper work.
4. ⭐ **The 50 Hz body is a RASTER-BAND STATE MACHINE, not a per-frame interrupt.**
   `irq1v_handler` walks `irq_band_state` 0→4→0, repainting the ULA mode and palette per
   horizontal band and reloading User VIA T1 for the next one; only the last band does the game
   work.  One Amiga VERTB therefore drives a whole band cycle — dispatch one band per interrupt
   and the simulation silently ticks at 10 Hz.  (`src/platform/amiga/Revs.cpp` `Revs::vbi()`.)
5. **Scripted input** (`src/platform/autorun.h`), clocked on key-poll count so the sequence is
   bit-identical across builds.  Without it a headless run measures a menu spin.
6. **Phase brackets are generated, not hand-written** (`MAIN_LOOP_BRACKET`), so the profile
   survives `make gen` and a bracket cannot drift off the call it times.

### The numbers — `docs/perf-method.md` §THE BASELINE

**≈1.4 FPS** (FPSCOUNT build, nine segments, all 1.4).  Goal 50, floor 25: **~18× short of the
floor**, with nothing drawn and nothing optimised.  ⚠ This **supersedes the ≈2.2 FPS the phase
originally reported**: that build stubbed the three `$7Bxx` main-loop calls as no-ops, and the
mirrors + dashboard turned out to be ~36% of the frame.

🛑 **AND THE PHASE'S HEADLINE FINDING WAS WRONG.**  It reported the four hottest of the 24 calls
at **57.4%** — `$46A1` (24.6%), `$1E15` (13.1%), `$1B12` (9.9%), `$4CA4` (9.8%) — i.e. "physics
and geometry, not rasterisation", called out as *the* headline difference from the Atari port.
Re-measured 2026-08-13 with a phase-bracket clock that does not wrap every display frame:
**`$7BE2` 34.7% (the dashboard), `$1A20` 27.3% (the road rasteriser), `$24F6` 18.6%** — 80.6%
between them, and `$46A1` is 6.5% while `$1B12` is 0.0%.  **The hot path is rasterisation**, the
Atari port's experience applies more directly than assumed, and Phase 6's premise below is
void.  `docs/perf-method.md` §Where the time goes has the table and the defect.

### Known-unfaithful in this measurement

- ✅ ~~The three `$7Bxx` main-loop calls are no-ops~~ — they run as of 2026-08-12, which is what
  moved the baseline from 2.2 to 1.4.  What is still unverified is the overlay's *bytes*: they
  are DERIVED from a replay of `$18EA`, never dumped off a real BBC.
- ⚰ The phase-share table this phase produced was taken with a broken instrument (above) and
  accounted for ~6% of the frame.  The exit criteria were met; one of the conclusions was not.
- The raster bands all fire at the top of the frame instead of at their scheduled positions;
  invisible while nothing is drawn, and Phase 5's copper work is where it gets fixed.
- Nothing is rendered, so 1.4 FPS will get worse before it gets better.

---

## Phase 5 — Render + input ⬜

- 6845 CRTC + Video ULA composition → copper list + bitplanes (`docs/amiga-lessons.md` for the
  rules; a CRTC/ULA analyser is `docs/bbc-reference-loop.md` step 5).  Note the MODE 7 teletext
  title screen (`5TRSCRN`) is a separate rendering problem from the 3D view.
- **Input: mouse + keyboard** (decided).  A racing sim's feel lives here — and the steering
  response curve is real logic in the binary (there is a per-track `HookJoystick` and a
  "SPACE — amplify steering" key), so read it out rather than tuning by feel.
- Sound: SN76489 (3 tone + 1 noise) → Paula.
- Five tracks: the engine is one binary, but behaviour is per-track (the expansion tracks are
  executable and patch it).  Decide how track selection works on the Amiga — the BBC's `REVSMEN`
  menu is BASIC and is not being ported.

---

## Phase 6 — Native twins, then asm ⬜

`docs/faithfulness-seam.md` for which side of the line each routine lands on;
`docs/validation-harness.md` for the guarantees; `docs/m68k-optimisation.md` for the 68000 rules;
`docs/perf-method.md` for how to price a change honestly.

⚠ ~~The physics hot path replaces the terrain rasterizer as the eventual hand-asm target:
different loop shape, same lever.~~  **Retracted 2026-08-13** — it rested on the Phase 4 share
table, which was measured with a phase-bracket clock that wrapped every display frame.  On the
corrected profile the targets are, in order:

| | | |
|---|---|---|
| `$7BE2` | 34.7% | the dashboard, in the `$7B00` overlay (⚠ ~2.7pp of that row is the vblank wait — split the bracket before optimising it) |
| `$1A20` | 27.3% | the road rasteriser — `$193E` and `$19AF` → `interp_edge` → the span plotters |
| `$24F6` | 18.6% | **unnamed compute.**  Name it before choosing between it and `$1A20` |

So the eventual hand-asm target IS a rasteriser after all, and the Atari port's terrain-loop
experience transfers as more than method.  The lever is unchanged: control the registers, force
`(a0)+`, verify with an in-process differential, never cross-run.

---

## Phase 7 — Packaging ⬜

WHDLoad slave, a player-facing README (keys, requirements), and an asset audit: ship only what
the port needs, not the original disc image.
