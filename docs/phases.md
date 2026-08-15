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
**`$7BE2` 36.1% (the dashboard), `$1A20` 21.1% (the road rasteriser), `build_road_edge_lists`
`$24F6` 19.0% (the road-geometry pass that FEEDS `$1A20`)** — 76.2% between them, and `$46A1` is
6.6% while `$1B12` is 0.0%.  **The hot path is rasterisation**, the
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

## Phase 5 — Render + input 🔧 (race view, input, sound, MODE 7 and both fonts DONE on the target)

### ✅ 1. The display — `src/platform/bbc_screen.h` + `src/platform/amiga/RevsScreen.*`

**The port draws, and the picture is Revs**: blue sky, green grass, black road, white kerbs, the
red steering wheel, black dials with red numerals, the wing mirrors, cyan dashboard shading.

The BBC side is ONE static 6845 mode whose depth and palette are rewritten five times per field
by the game's own User VIA timer interrupt, and it is all derived and written down in
`bbc_screen.h`: geometry from the CRTC table at `$4F0F` (40×26 cells of 8 lines at `$5A80`, 208
lines, ending exactly where the `$7B00` overlay begins), pixel format from the ULA's shift
register (a pixel's bits land in palette-index bits 3 and 1; bits 2 and 0 are the NEXT pixels' —
which is why the game's palette tables come in groups of four), and the band schedule recorded
live from what `irq1v_handler` writes.

The mapping: 320×208, **two bitplanes**, interleaved, double-buffered with the pointer swap in
the VBI.  MODE 5's high nibble IS plane 2's four pixels and its low nibble plane 1's, so a byte
becomes one byte per plane through two 256-entry tables; MODE 4 goes into plane 2 with plane 1
zeroed.  ⭐ Both modes land on the same four colour registers (BBC logical 0, 2, 8, 10), so the
copper never touches BPLCON0 and the mode is purely a decode choice.

⭐⭐ **The finding that made this harder than it looked: the sky is a hiding place for live
code.**  `$5E40-$66FF` — 5.5 KB of engine variables *and* executable routines — is inside the
frame buffer, on display, invisible only because all sixteen of band 1's palette entries are the
same blue.  Nothing clears it and nothing can: it is the program.  So the raster phase matters to
the pixel, and it is pinned two independent ways (the decoded content of a real frame, and the
reference's stated band heights).  Band 2's duration is the horizon and moves with the hills
(`$4F44` computes it as `$04D8 ±` whole scan lines), which is why the model records what the
handler wrote instead of freezing a table.

Verified ON THE TARGET, not by eye: `amiga/screen_dump.gdb` dumps the displayed bitplane block
and the copper list out of FS-UAE and `tools/amiga_ppm.py` decodes them as the hardware would.
The bands come out at display lines 18 / 81 / 100 / 166 with the derived palettes.

### ✅ 2. Input — `src/platform/amiga/RevsInput.*`

Mouse + keyboard (the user's decision), mapped onto the game's **own** two input paths rather
than a new one: `$05F5` bit 7 picks ADC channels 1/2 + the fire button, or the keys.  The twelve
negative-INKEY codes the game tests were enumerated from every `LDX #imm / JSR $0E50` site, so
the table is closed; an unmapped code is counted, not silently answered.  The mode switches are
the game's own SHIFT+f1 / SHIFT+f2 (decoded from `$3DE2`/`$39D4`), and that decode is what
confirms the BBC key-number layout.  Keyboard via CIA-A's serial interrupt through
`ciaa.resource`; the mouse drives channel 1, sampled in the VBI because the counter wraps.

Verified end to end by making the scripted auto-run drive the real map (it reaches a race).
[ASSUMED], both one-liners: which of -87/-88 is left, and the sign of the mouse axis.

### ✅ 3. The MODE 7 front end — `src/platform/teletext.*` + `RevsScreen`'s second configuration

**The front end RENDERS, and its screen RAM is byte-identical to a real BBC's** (2026-08-15) —
verified on the target, not by eye: the Amiga's `$7C00-$7FFF` matched the reference dump
`tmp/mode7/mode7_23.bin` in **1024 of 1024 bytes**, and the decoded bitplanes are the real menu
(double-height rainbow REVS logo, chequered mosaic bands, blue option chips, flashing cyan
"PRESS").  `make mode7` is the host-side differential; `amiga/mode7_dump.gdb` is the target one.

🛑 **THIS ITEM'S ORIGINAL PREMISE WAS WRONG IN BOTH HALVES, and measuring it shrank the job.**
It read: *"a teletext renderer, and **a font** — `OSWORD 10` fires ~90-850 times per run asking
the MOS for character definitions… the MOS font cannot be lifted from a licensed source, so the
96 glyphs have to be drawn."*  Neither claim survived `tools/bbc_probe_mode7.mjs`:

1. **MODE 7 does not use the MOS font at all.**  `vdu_char_def` (`$5092`) branches on `$64`
   bit 7: the bitmap arm asks `OSWORD 10` for a MOS ROM glyph and plots it into the frame
   buffer, but the MODE 7 arm just calls **OSWRCH** and the **SAA5050** — a Philips teletext
   chip with its own character ROM — draws the cell.  Measured in the front end: **310 OSWRCH
   calls, ZERO OSWORD 10 calls.**  So those `OSWORD 10` hits are the RACE VIEW's text path, a
   separate (still open) gap, and they were attributed to the wrong screen mode.
2. **So the work was a VDU DRIVER, not a font.**  The MOS's driver does **29878** of the writes
   to `$7C00-$7FFF` against the game's **1105**, and the engine's entire vocabulary is FIVE
   commands: `VDU 22,7` / `23,0,10,32` / `12` / `31,x,y` / `127`.  ⚠ A raw byte histogram hides
   that — `VDU 31`'s x/y masquerade as `VDU 2` and `VDU 4` — so the stream has to be parsed with
   per-command parameter counts.  The glyphs themselves cost one generator script.

Three findings worth keeping:
- ⭐ **Screen RAM is the single source of truth**, and that is a measurement: the game also pokes
  the page directly (70 writes from `$3A65`, 8 from `$65BA`, 1 from `$659A`).  A driver with its
  own shadow grid would lose those silently.
- ⭐⭐ **The front end never reaches the main loop's paint hook.**  `menu_wait_key` (`$6571`) spins
  at `$6577` re-reading `key_binding_tbl`, so `$1701` is never executed out of a race and
  *nothing painted* — with a byte-perfect page in `mem[]` and an all-zero bitmap.  ⚠ That is
  indistinguishable from a broken renderer from either dump alone; what separated them was
  dumping BOTH.  `$6577` is now a `SPINWAIT_HOOKS` entry: the front end's frame boundary.
- ⚠ `VDU 127` was derived from the BYTES, not a manual: the engine sends `$9D $7F` and the real
  screen keeps neither, so 127 is backspace-blank-stay.  Treating it as a printable block shifts
  the whole REVS logo one cell.

### ✅ 3b. The RACE VIEW's bitmap font — DONE (2026-08-15)

The other font, the one `OSWORD 10` returns.  `src/platform/mos_font.h`, generated by
`tools/gen_mos_font.py`: 96 printable glyphs, **drawn** rather than sourced, because the MOS
software font is Acorn's copyrighted ROM with no standards document behind it (unlike the SAA5050
set, which the teletext generator could legitimately take from a published spec).  Metrics match
the ROM font; letterforms are ours and do not claim otherwise.

Measured first (`make refloop-charset`): 88 calls, 15 distinct codes, all inside `$20..$74`, from
five callers including the `$7B00` overlay.  That is one practice session's vocabulary, so the
whole printable range is drawn and `mos.cpp` **counts** a request outside it.

⭐⭐ **The measurement found a THIRD entry to `vdu_char_def` that no static read would give you.**
92 reads at `$50AA` against 88 entries at `$5096`: four calls enter at **`$509D` from `$508F`**,
past `$509B`'s `LDA #0 / STA $77`.  `$508C` is therefore a **double-width** entry — `$50AE` halves
the glyph, `AND #$F0` keeping its left four columns and `ASL A` x4 lifting its right four into
place, one MODE 5 cell each — and its only caller is the gearstick readout `$42D0`, which calls it
twice with `$77` = `$22` then `$FF`.  The codes prove it: `{$4E, $31}` = 'N' and '1'.  Reading the
code alone says that arm is dead, because `$509B` zeroes the flag.

⚠ **That decided the glyph geometry**, and it is the one thing here that could have shipped
looking broken: the 6-column body is CENTRED in the 8-column cell (bits 6..1), because a
left-aligned body splits 4 ink columns | 2 under the halving and a narrow glyph like '1' then
renders its second cell BLANK — half a gear indicator.  Centred, the split is 3 | 3.

Verified on the target: served 92 (exactly the real BBC's 92), out-of-range 0, and a decoded frame
shows "Lap Time"/"Best Time" plus a double-width '1' whose base serif crosses into the second cell.
`amiga/charset.gdb`.

### ✅ 4b. ⭐⭐ COMPETITION MODE — DONE (2026-08-15), and it found TWO stack bugs on the way

**Every measurement this project had ever taken was a PRACTICE session**, and `$2637`'s
`LDA $5F3B / BMI $262D` means the engine *skips the entire multi-car path* when the practice flag
is set — `check_car_pair`, the field walk, the overtaking counter.  Practice runs the player alone,
so a clean practice frame is structurally incapable of saying anything about competitor cars.

`make refloop-comp` now drives a real BBC through the long branch (2 COMPETITION → Novice → 5 mins
→ driver name → wings) and `make COMPETITION=1` is the port's equivalent.  Ground truth for a
20-car Silverstone race: `$0003` = `$04`, `car_order` = `[0 1 2 3 19 5 … 18 4]`, player `$6F` = 19,
**`S` = `$F2`**, entry `S` (saved at `$6B`) = **`$F8`**.

⭐⭐ **The port hung in its first competition race, and the cause was that `cpu.S` was never
initialised.**  Full write-up in `src/cpu/cpu.c`; the chain is worth knowing because none of it
looks like a stack bug:

> `cpu` is a global ⇒ `S` = 0 ⇒ the first `PUSH` writes `mem[$0100]` and wraps.  **Page 1's bottom
> is not stack in Revs** — it is eight 20-entry per-car arrays (`$0100 $0114 $0128 $013C` = 
> `car_order`, `$0150 $0164 $0178 $018C`) — so a low `S` silently scribbles pushed bytes across the
> field.  `car_order` filled with ASCII.  `find_player_neighbours` (`$63A2`) then failed to find
> the player, fell out of its loop with `X` = `$FF`, stored that in `$0003`, and
> `check_car_pair`'s walk (`$2797`, `car_index_inc / CPX $03`) can never match it because
> `car_index_inc` wraps 19→0.  Infinite loop.

Fixed and verified on the target (`S` = `$F8`, lowest `$F3`, **0** pushes into the arrays — the
real BBC's shape), with `validate` / `mode7` 3/3 / `sound` 0 of 8083 / endian-lint all still clean.
`g_stackLow` / `g_stackTrespass` now make the class report itself.

#### ⭐⭐ …and then a SECOND stack bug, which was the frozen race view (2026-08-15)

Reported by the user: pick 2 COMPETITION → 1 Novice → 1 five minutes, the race view appears, and
it is **frozen — `T` does nothing, waiting does nothing.**  Two independent defects, found in
order:

1. **The competition harness was never wired on the Amiga.**  `autorun.cpp` selects the
   competition script on `REVS_COMPETITION`, but `PlatformAmiga.h`/`.cpp` gated the `autoRun`
   member and its call site on `FPSCOUNT || PROBE || STRAIGHT_TO_RACE` — so `make COMPETITION=1`
   *compiled* the script and never instantiated the object that runs it.  That is the whole of
   the "the port does not yet reach a competition race on the Amiga / the script's timing is the
   next thing to bisect" item above: it was not timing.  With the flag added, the target reaches
   a competition race immediately (`$5F3B` = `$04`, `$6F` = 19, engine running, a field at a
   spread of distances).

2. ⭐⭐ **The "second, slower stack leak" was a leak UPWARD, at the two-level-RTS site.**
   `$2F81`'s `TSX/INX/INX/TXS` discards a RETURN ADDRESS, and this model keeps return addresses
   on the C call stack — so emitting the register write "because it is faithful to the register"
   added +2 to `S` per road-span exit with nothing to cancel it.  `S` climbed past `$F8`, wrapped
   `$FF` → `$00`, and pushes then landed on `mem[$0100]` = `car_order` — the *same* corruption as
   the uninitialised-`S` bug, arrived at from the opposite direction.  `find_player_neighbours`
   again stored `$FF` in `$0003` and `check_car_pair`'s field walk never terminated: the main
   loop never returns, so nothing paints and no key is polled.  A frozen race view.

   ⚠ **It read as a downward leak and was chased as one.**  The low-watermark trap reported
   "`S` fell to `$00`" having never seen `$DF` — and *that* is the tell: `S` can only descend one
   push at a time, so a value that appears without its predecessors was ARRIVED at, not descended
   to.  `g_stackHigh` now exists next to `g_stackLow` for exactly this, and `make STACK_TRAP=1`
   plus `REVS_STACK_TRAP` / `REVS_STACK_CEIL` prints one backtrace at the first breach in either
   direction — which named `FUN_2f7e` in a single run.

   The transpiler now emits `UNWIND_SET()` alone at that site, with no `cpu.S` write.  ⭐ The
   general rule: **when a 6502 idiom manipulates `S` to talk about return addresses, model the
   CONTROL FLOW and leave `S` alone.**  Modelling the register instead is a silent leak;
   modelling neither is a hang (that was the Phase 5 "intermittent stall").

Measured after both fixes — host and target agree, and both match the real BBC: `S` = `$F8`,
lowest `$F3`, **highest `$F8`**, 0 pushes into the per-car arrays, `$0003` = `$04` (not `$FF`).
`validate` / `mode7` 3/3 / `sound` 0 of 8083 / `endian-lint` / `muldiv-audit` / `probe-audit` all
still clean.

✅ **AND THE COMPETITOR CARS RENDER CORRECTLY — confirmed by the user on the target, 2026-08-15.**
That closes the item this whole sub-phase existed for.  Worth being explicit about how it was
closed, because it is the pattern: the *stimulus* was built headlessly (`make refloop-comp`,
`make COMPETITION=1`, the field populated and lapping) and every counter said the session was
right, but "are the other nineteen cars drawn, and drawn right?" is a judgement about a moving
picture — the remote debugger greys the display, so no dump this project owns can answer it.
⚠ It stayed open through several green measurements for exactly that reason.  **Watch for the
shape: an item whose evidence is necessarily a human looking at the screen should be routed to the
user early rather than accumulating more headless confirmation.**  Same class as the sound
subsystem's still-owed by-ear pass.

### ✅ 4. Sound — DONE (2026-08-15)

Two tones plus periodic noise, a fixed interval of 28 between the tones, pitch chasing the rev
count in steps of 1, and one envelope for the tyre squeal — **all through OSWORD 7/8**, never the
SN76489 directly, so the port reproduces the MOS's *scheduler*.  `src/platform/sound.c` is that
scheduler (validated tick-for-tick against a real BBC: `make sound`, 0 of 8918 and 0 of 8083) and
`src/platform/amiga/RevsAudio.cpp` maps the chip state onto Paula.  Measured on the target with
`amiga/sound.gdb`: 298 SOUND commands, 6482 scheduler ticks, 121 Paula updates, and a final chip
state of the same shape a real BBC ends a drive in.  The write-up is `docs/bbc-hardware.md` §Sound
plus the two headers.

Three things left here, none of them blocking:
- **the pitch ramps ~50× too slowly**, because `sfx_trigger_random` is a MAIN-LOOP call and the main
  loop paints at ~1 FPS.  It is a framerate consequence, not an audio bug, and it fixes itself as
  Phase 6 lands.
- **the by-ear pass has not happened** (audio cannot be verified headlessly).
- **sync/hold/queued sounds are unimplemented and counted**; Revs has never issued one.

### 🔧 5. Track selection — the data path is DONE; the code path is next

**User decision (2026-08-15): ONE BINARY with a runtime track menu**, not five per-track builds.

The BBC's `REVSMEN` menu is BASIC and is not being ported, and the four expansion tracks are
*programs* that patch the engine as it starts — and the port is a transliteration, so there is no
6502 to run that patcher.  What makes the single binary tractable is two measurements:

1. ⭐ **Outside the track window the engine image is byte-identical across all five circuits.**
   The entire per-track delta is the 1830-byte block at `$5300-$5A25` plus the circuit NAME as
   ASCII at `$7808`.  So one embedded engine plus 5x1830 bytes plus five strings covers it.
2. ⭐ **`tools/track_patch.py` REPLAYS each track's `ModifyGameCode`** rather than describing it —
   twelve instruction forms, so the interpreter is exact where a pattern match would be nearly
   right.  The loop bound is read by *executing* `LDX #n` at `$5700` (`$12` for Brands/Oulton,
   `$13` for Donington/Snetterton).  `make track-patch VERIFY=1` cross-checks the output against
   the patch surface measured on a real BBC: **only-in-replay is 0 for every track**, the residue
   being the documented `$5FC9-$5FCD` runtime state.  Verified by sabotage in both directions.

So the patcher never has to run in the port: its output is applied as data at selection time.

### ✅ 5a. The data table, the installer and its differential — DONE (2026-08-15)

`tools/gen_tracks.py` -> `src/gen/revs_tracks.[ch]`, `src/platform/track.[ch]`, `make tracks`.

**Both enabling measurements were re-derived rather than trusted, and the second one moved:**
the per-circuit delta is `$5300-$5A25` (1830 B) **plus `$7800-$78AA` (171 B)**, and that tail bound
comes from the longest track FILE, not from diffing the commercial five.  Those files are
`$738`-`$73C` long so they cannot write past `$7816` — their diff stops exactly there, which makes
`$7816` look like the answer.  ⚠ The Nürburgring file is padded to `$7D0`, reaches `$78AA`, and
*uses* the space: a table of 5-byte records at `$786B-$78AA` that every commercial circuit leaves
zero.  Sizing the extent off the five would have truncated the sixth silently.

**`make tracks` is the differential, and the two sides are built down different paths:** the
installer starts from Silverstone's post-unpack image and writes two extents plus the patches; the
fixture relocates the disc image *for that circuit* and applies its own `ModifyGameCode` replay, as
a whole 64K image.  **6 of 6 circuits byte-identical.**  So it verifies the installer AND re-proves
that nothing outside those extents differs per circuit.

⭐⭐ **The first version of that harness was nearly VACUOUS, and a sabotage run is what caught it** —
truncating the installer's tail loop by one byte still PASSED.  Two causes, both general:
- **The base was the thing under test.**  `mem[]` was seeded from Silverstone's own image and then
  Silverstone's block installed over it: identical bytes, so the diff could not see a byte the
  installer failed to write.  Fixed by POISONING both extents with `$5A` first.  ⭐ *An installer
  test whose "before" state already equals the expected "after" state measures nothing.*
- **It could only ever install 1 circuit of 6**, the other five being correctly refused (below).
  Fixed with a harness-only `revs_track_install_forced()`; the refusal is now asserted separately.
Both sabotages fail loudly now (tail bound -> `$78AA`, dropped patch -> `$4F59`).

⚠ **An unfinished circuit is REFUSED, not attempted.** Installing a patch byte into `mem[]` is
necessary and not sufficient — the transliteration bakes operands and opcodes, so only bytes
declared in `SMC_SITES` are read at run time.  `revs_track_install()` therefore checks every patch
address against `src/gen/revs_smc_bytes.h`, which **the transpiler now generates** from `SMC_SITES`
(a hand-kept list would drift the moment a site was added), and refuses if any is uncovered.
Today: Silverstone installs; the other five report `54-60 patch bytes await SMC sites (first
$1248)`.  `make TRACK=n` selects, `$REVS_TRACK` overrides on the host, `amiga/track.gdb` reads it
on the target.

⚠ And a reporting defect worth remembering, found on the target: the fallback's own check RESET
`g_trackUnhonoured` to 0, so "asked for Silverstone" and "asked for Nürburgring and could not have
it" read identically — `unhonoured=0` beside `first=$1248`, incoherent, and noticed only because
the address survived.  `revs_track_boot()` now re-takes the requested circuit's verdict *after* the
fallback, and `g_trackRequested` records what was asked for.

⭐ **The sixth circuit works in the same machinery** (see docs/reference-sources.md §The Nürburgring
file, MEASURED): identical hook entry, same patcher vocabulary, one extra patch address (`$298E`).
Its block is generated from the **git-ignored** `revs-hack-nurburgring.ssd`, so the repo carries no
third party's file; a checkout without that disc generates five circuits and says so.

### ✅ 5b. The SMC sites — the ENGINE now reads every per-circuit byte (2026-08-15)

`tools/track_smc.py` -> `disasm/track_smc.txt` (committed) -> `tools/transpile.py`'s fifth SMC
class.  `make track-smc` is the report; `make track-smc EMIT=1` regenerates the table.

⭐⭐ **The unit is not an instruction, it is an EXTENT — and getting that wrong would have
produced 60 plausible sites that were each subtly wrong.**  `make track-patch` answers "which
BYTES does each patcher write", which is the installer's question.  Folded onto the listing's
instruction boundaries the same surface is **30 extents**, and **10 of them replace a WHOLE
INSTRUCTION with a different one**:

| | |
|---|---|
| `$1248` | `LDA $5905,Y` → `JSR $5672` — same length, completely different operation |
| `$12FB` | `CLC` + `ADC #$03` → the single `JSR $54F1` — **two instructions, one substitution** |
| `$248B` | `BCS $24B8` + `JMP $2403` → `JMP $56BC` + **2 dead bytes** |
| `$2542` | `JSR $3450` + `LSR A` → `JSR $53F0` + `NOP` — the two arms share their FIRST opcode |
| `$1FE9` | `LDX $1F` (zp) → `LDX #$1F` (imm) — an addressing-mode change |

So the existing four classes (`operand` / `opcode` / `branch` / `call`) do not fit: they all
assume the instruction stays the instruction.  The fifth class, **`extent`**, emits one arm per
instruction-stream SHAPE, guarded on the opcode bytes in `mem[]`, with every patchable operand
read from `mem[]` inside the arm and a trap for an unrecognised shape.  `$2542` is why the guard
is a conjunction over the whole signature and not one byte: both its shapes start with `$20`.

Three things worth carrying forward:

1. ⭐⭐ **An operand is variable PER ARM, not per extent — and the over-conservative version broke
   SILVERSTONE.**  The first cut read every patchable operand from `mem[]` in every arm, on the
   argument that a guard tests opcodes and so cannot prove which circuit is running.  That argument
   is half right: a guard often *does* pin the circuit.  `$248B`'s unpatched arm needs `BCS` at
   offset 0 and `JMP` at offset 2, and all five expansion circuits write `$4C` over offset 0 — so
   only Silverstone can be in that arm, and its branch offset is Silverstone's, statically.
   Treating it as variable turned that `BCS` into a **runtime-computed branch**, which has to
   dispatch over the enclosing function's labels — and when §5c later split that function at
   `$2490`, `$24B8` fell out of the dispatch set and a plain Silverstone race died with
   `SMC UNHANDLED: site $248B holds $24B8`.
   ⭐ *Being conservative about what varies is not free: it manufactures runtime machinery whose
   preconditions then have to keep holding.*  The table now records, per arm, which offsets circuits
   **that satisfy that arm's guard** patch — still shape-only, no values — and all ten multi-arm
   extents' base arms come out empty, i.e. fully static. A staleness guard (`make track-smc
   --check`, run by `make gen`) fails if a newly-present disc would imply a different table, because
   a stale table is silently wrong rather than loudly missing.
2. ⭐ **Two structural preconditions are CHECKED, not assumed** — nothing branches into the middle
   of an extent (its interior is the middle of a *different* instruction in the patched arm), and
   no extent crosses a function boundary.  Both fire under sabotage: widen `$248B`'s extent to
   `$24C0` and five targets appear; span it to `$2545` and it names `FUN_23d2`/`FUN_24f6`.
3. ⭐ **The committed table carries no per-circuit VALUES** — only extents, opcode signatures and
   patchable offsets.  The operands live in `mem[]`, so one table serves all six circuits and the
   file contains no third party's data (`docs/reference-sources.md` §The Nürburgring file).

**And the coverage check learned its second half.**  `revs_track_check()` counted uncovered patch
bytes; with the extents in place that count went to 0 for every circuit — which would have read as
"playable" while every patched `JSR` had nothing to call.  So it now counts **hook entries with no
C body** too, in a *separate* counter (`g_trackHooksUnbuilt`), because the two halves land at
different times and one number would have read identically before and after this work.  It also
learned that a patch to a **data** byte needs no SMC site at all: `$3574`/`$35F4` are reached
through `mem[(0x3500)+cpu.X]`, so the generated C already consults them.  `revs_smc_bytes.h` now
carries a 35-entry **code range** map for exactly that question.

⚠⚠ **A defect the sabotage found in the BUILD, not the code:** dropping an extent from the table
should have shown three unhonoured bytes at `$1248` and showed none — `track.o` had been compiled
against the previous `revs_smc_bytes.h` and nothing told `make` to rebuild it.  ⭐ *A generated
header whose consumer is not rebuilt does not fail; it answers the OLD question, confidently.*
The host Makefile now tracks header dependencies (`-MMD -MP`).  (The Amiga Makefile's `make clean`
rule stands — it tracks neither headers nor `PROBES`.)

Verified: 30 extents emitted, `make tracks` 6/6 byte-exact with every patch byte honoured, host
and Amiga builds clean (muldiv + probe audits clean), and **Silverstone's frame 40 is byte-identical
to the pre-change build** — the arms did not change the circuit that takes the unpatched one.

### ✅ 5c. The hook BODIES — transliterated, and ALL SIX CIRCUITS NOW INSTALL (2026-08-15)

`tools/track_hooks_dis.py` reads them out: bounded recursive descent inside `$5300-$5A25` that
**stops dead at the window boundary** and records the engine address it left for, with
`ModifyGameCode`'s own instructions subtracted (it is start-up-only and its effect is already
applied as data — transliterating it would apply every patch twice).

⚠ **The volume is 3-4x the earlier estimate.**  This section used to say "~90 instructions per
track in ten extents … ~360 over four circuits".  Measured: **254-287 instructions per circuit,
1366 in total**, from 13-15 hook entries, with 10-11 engine exits each and **zero undecodable
bytes**.  The earlier figure came from reading extents out of a sweep rather than walking the
entries the patches actually name.  (The entry list also differs: `$53F0 $54EB $54F1 $5572 $55BD
$5672 $56AF $56BC $56C8 $5772 $57A1 $59E9 $5A1B` for Brands, derived from the patched JSR/JMP
operands themselves.)

The exits are coherent — `$0C00` `$0E40` `$13E0` `$140B` `$1933` `$2490` `$253B` `$3450` `$4610`
`$461B`, i.e. exactly the engine routines the patches displaced, plus the maths helpers.

**The shape chosen: ONE C function per circuit, entered by 6502 address** — exactly the `region`
form `build_regions()` already produces for a cyclic segment group, emitted into
`src/gen/revs_track_hooks.[ch]` by a second `make gen` pass. That choice is what made it tractable:

- every branch and `JMP` inside the window becomes a plain `goto`, so **no function decomposition
  is needed at all** — which matters because Ghidra has never seen this code and has no boundaries
  for it
- the ~5 intra-window `JSR`s per circuit become a recursive `trk_<name>(0xTTTT)`, which **returns**,
  as a `JSR` must and a `goto` cannot
- a transfer out of the window resolves through the engine pass's own symbol and wrapper tables

⚠⚠ **The engine exits had to be collected BEFORE the engine's wrapper fixpoint, not after.** A
circuit's hook returns to the engine at `$2490` / `$253B` / `$461B` — mid-function addresses the
engine itself only ever reaches by falling through or branching, so they exist as `L_2490:` labels
and **not as callable C**. Discovering them downstream would have emitted `FUN_2490()` against a
function that was never defined. So `collect_track_hooks()` runs early in `main()` and feeds its
exits into `external_entries` alongside every other mid-function entry.

One transpiler change was needed for the engine corpus too: a `JSR` now resolves through
`resolve_call()` rather than `resolve_target_name()`, so a target that is a **region entry** is
reached as `region_xxxx(0xTTTT)`. Checked: the engine corpus is byte-identical apart from two sites
where `FUN_7ef3()` became `region_7bf7(0x7EF3)` — and `FUN_7ef3` is literally
`{ region_7bf7(0x7EF3); }`, so that inlines a thin wrapper and changes nothing.

**Result: `make tracks` installs 6 of 6 circuits, byte-exact, with no refusals** — one binary, six
circuits, both halves of "playable" in place. Host and Amiga builds clean.

Three checks that make that more than a build success:
- **The five bodies genuinely differ** (9796-11026 chars, every pair distinct), and an intra-window
  `JSR` dispatches to the circuit's OWN function: `trk_brands(0x55C4)` in Brands, `trk_snetter(0x55C4)`
  in Snetterton. ⭐ `$5572` is a hook entry in **every** circuit and is different code in each, so an
  address-only dispatch would have compiled, run, and taken Brands Hatch's corner through
  Snetterton's hook.
- **Sabotage:** removing one entry from the generated table refuses exactly that circuit and names
  exactly that address (`BRANDS … 1 hook bodies unbuilt (first $5672)`).
- ⭐ **`g_trackHookCalls` counts SUCCESSFUL dispatches**, because "the circuit installed and nothing
  crashed" is entirely compatible with the hooks never being reached — which is precisely what an
  expansion circuit silently running Silverstone's control flow would look like. On an expansion
  circuit it must be > 0.

⚠ A false negative worth remembering from this session: the first Brands run reported `hook calls 0`
and looked like a dead seam. It was reading **frame 3**, which is still the MODE 7 front end — the
rasteriser, and therefore every hook, is only reached once the race starts. The instrument was fine;
the window was wrong. (And the run before *that* reported nothing at all, because `2>&1 | tail`
re-buffered the deliberately-unbuffered stderr — the same trap `docs/method-lessons.md` already
carries, walked into again.)

**What remains for Phase 5:**
- **`CallTrackHook` (`$5A22`) dispatch** — one call, one target, and Silverstone supplies an `RTS`
  stub, which is a far cleaner seam than the patch sites.
- **The menu itself**, and embedding the blocks.

⚠ If a hook is ever reached with no body, `revs_track_hook()` counts it and traps through
`platform_smc_unhandled()` rather than returning quietly — a silent return would be the engine
carrying on with Silverstone's control flow under another circuit's name.

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
| `$7BE2` | 36.1% | the dashboard, in the `$7B00` overlay.  ✅ The vblank wait that used to be folded into this row is now its own phase 25 (2.5%), so the figure is clean |
| `$1A20` | 21.1% | the road rasteriser — `$193E` and `$19AF` → `interp_edge` → the span plotters |
| `build_road_edge_lists` `$24F6` | 19.0% | the road-geometry projection pass — it BUILDS the edge lists `$1A20` draws |

⭐ **Rows 2 and 3 are one subsystem, 40% of the frame: build the road's edge lists, then draw
them.**  That reframes the choice.  It is not "optimise `$1A20` or `$24F6`" — a change to how the
road geometry is represented (`edge_x_lo/hi` + `edge_y`, 2×40 points, produced by phase 5 and
consumed by `interp_edge` in phase 11) moves both rows at once, and is likely worth more than
hand-asm on either half alone.  Look at the representation before writing any asm.

So the eventual hand-asm target IS a rasteriser after all, and the Atari port's terrain-loop
experience transfers as more than method.  The lever is unchanged: control the registers, force
`(a0)+`, verify with an in-process differential, never cross-run.

---

## Phase 7 — Packaging ⬜

WHDLoad slave, a player-facing README (keys, requirements), and an asset audit: ship only what
the port needs, not the original disc image.
