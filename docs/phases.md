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
   `irq1v_band_schedule` walks `irq_band_state` 0→4→0, repainting the ULA mode and palette per
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
**`$7BE2` 36.1% (the dashboard), `$1A20` 21.1% (the road rasteriser), `build_track_geometry`
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

## Phase 5 — Render + input ✅ (race view, input, sound, MODE 7, both fonts, COMPETITION, track selection AND the menu — all DONE on the target)

### ✅ 1. The display — `src/platform/bbc_screen.h` + `src/platform/amiga/RevsScreen.*`

**The port draws, and the picture is Revs**: blue sky, green grass, black road, white kerbs, the
red steering wheel, black dials with red numerals, the wing mirrors, cyan dashboard shading.

The BBC side is ONE static 6845 mode whose depth and palette are rewritten five times per field
by the game's own User VIA timer interrupt, and it is all derived and written down in
`bbc_screen.h`: geometry from the CRTC table at `$4F0F` (40×26 cells of 8 lines at `$5A80`, 208
lines, ending exactly where the `$7B00` overlay begins), pixel format from the ULA's shift
register (a pixel's bits land in palette-index bits 3 and 1; bits 2 and 0 are the NEXT pixels' —
which is why the game's palette tables come in groups of four), and the band schedule recorded
live from what `irq1v_band_schedule` writes.

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
- **the pitch ramps ~50× too slowly**, because `engine_sound_update` is a MAIN-LOOP call and the main
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

**✅ AND IT RUNS ON THE TARGET** (`make PROBES=1 STRAIGHT_TO_RACE=1 TRACK=1` +
`GDBSCRIPT=track.gdb ./diag_run.sh 150`):

```
=== vbi=1878
=== requested=1  installed=1
=== requested circuit unhonoured bytes=0  first=$0000
=== requested circuit hook bodies unbuilt=0  first=$0000
=== hook calls=323  missing=0
=== mem[$5300]=$01 mem[$5301]=$d1   mem[$1248]=$20 (patched circuits: $20)
```

⚠ Two things about that script, both learned the hard way:
- **It used to sample at the FIRST render** (`tbreak Revs::render`), i.e. vbi≈38 — under a second in,
  with the autorun script still pressing keys. It reported `hook calls=0` beside a flawless install,
  which reads as a dead seam. It now skips 30 painted frames. Third instance this session of *the
  window being the measurement*.
- **`=== name at $7808` is only meaningful BEFORE the race.** `copy_dash_data` drops the dashboard
  bitmap over `$70DB-$7813`, which contains `$7808`, so in-race it prints garbage — faithfully.
  Moving the sampling point turned that line from evidence into noise; `mem[$5300]`/`mem[$5301]`
  are the in-race block check that still holds.

### ✅ 5d. `CallTrackHook` (`$5A22`) — closed with a note, not with code

It is emitted as `return;`, i.e. Silverstone's own `RTS`, and on an expansion circuit `$5A22` holds
`JMP $5700` = `ModifyGameCode`.  **Returning is correct for every circuit** — the port applies that
patcher's *output* as data at selection time, so running it here would patch an already-patched
image.  ⭐ But "correct" and "correct for a recorded reason" are different states, and the second is
the one that survives someone else reading it: as written it was indistinguishable from having baked
Silverstone's byte by accident.  The reason now lives in `disasm/symbols.csv`, so it propagates into
the generated comment on every `make gen`.

The guarantee that the replay covered whatever is actually at that target is a **generation-time**
one: `tools/track_patch.py`'s interpreter raises on any opcode outside its twelve-form vocabulary.
So no runtime dispatch is needed.

~~**What remains for Phase 5: the MENU, and nothing else.**~~  ✅ **Done 2026-08-16 — §5e.**

⚠ If a hook is ever reached with no body, `revs_track_hook()` counts it and traps through
`platform_smc_unhandled()` rather than returning quietly — a silent return would be the engine
carrying on with Silverstone's control flow under another circuit's name.

### ✅ 5e. THE MENU — port-authored, and MEASURED anyway (2026-08-16)

`REVSMEN` is BASIC (43 lines, detokenised straight off the disc), so this is the one screen in the
game with no transliteration to check against.  That is a reason to record an oracle, not a licence
to eyeball it:

| | |
|---|---|
| `tools/bbc_probe_trackmenu.mjs` | CHAINs the **real REVSMEN** under jsbeeb and dumps MODE 7 screen RAM: the title page, the menu page, and the page after each of the five digits |
| `src/platform/trackmenu.c` | paints the same pages through the port's own (already validated) MOS VDU driver — `tt_vdu()`, so screen RAM stays the single source of truth |
| `make trackmenu` | requires them to be **identical**.  13 checks, 0 failures |
| `tools/trackmenu_check.py` | ...and the same diff against a page dumped off the **Amiga** (`amiga/trackmenu.gdb`): **22 of 22 shared rows byte-exact on the target** |

⭐ **Three things the fixture settled that the BASIC listing did not**, and each would have been a
confident wrong answer:

- **Selecting an option changes TWO bytes, not three.**  Line 240 is `VDU129,157,131` at
  `TAB(5,10+2*A%)`, but column 6 already holds `$9D`, so that write is idempotent.  A correct
  implementation checked against the listing would have looked off-by-one.
- **The title dwell is 10900001 cycles = 5.45 s = 273 display fields**, measured between the title
  page appearing and the menu page completing.  The alternative was guessing at BASIC's own
  `FOR X=0 TO 10000:NEXT` speed.  ⭐ The target reproduces it *to the field* (`fields=273`).
- **`*LOAD 5TRSCRN` is exactly a memcpy**: the page a real BBC *displays* is byte-identical to the
  file on the disc, so the title screen needs no interpreting — `src/platform/titlescreen.h`
  (generated, git-ignored: it is a kilobyte of Superior/Acornsoft's screen data).

**The one deliberate divergence: a SIXTH option, NURBURGRING** (user decision).  It was reachable
only via `make TRACK=5` before.  The differential is honest about it — every comparison runs with
`TM_OPTIONS_FAITHFUL` (5), which covers every row, column and attribute the two configurations
share; the sixth row comes out of the same loop, and its **routing** is checked by the
option→`revs_tracks[]` cross-check, which is the half of the menu no page can show.

⚠⚠ **REVSMEN's option order is NOT `revs_tracks[]`'s** (option 5 is track 0 — Silverstone is
first in the table because it is the engine's own default), so an option→index table is
unavoidable and a wrong entry there installs a circuit that works perfectly and is not the one the
player chose.  `tm_begin()` cross-checks every entry against `revs_tracks[].name` and counts
`g_tmMisrouted`; reordering `gen_tracks.py`'s CIRCUITS now breaks loudly.

⚠⚠ **THE MENU EXPOSED A HAZARD THAT DID NOT EXIST BEFORE — installing is not idempotent across
circuits.**  `install_data()` writes the block, the tail and this circuit's patch bytes, and cannot
undo the *previous* circuit's: their unpatched values exist only in the boot image.  So `make
TRACK=1` followed by a player choosing Silverstone would have left 54 of Brands Hatch's bytes in
the engine.  Closed structurally: the menu is the **single installer** (`revs_track_boot()` no
longer runs at startup, and an unattended build reaches its `make TRACK=n` circuit through the
menu's own auto path), and a second install of a *different* circuit is refused and counted
(`g_trackOverinstalls`).  A caller that has really restored the image says so with
`revs_track_forget()`.

⭐ **The 50 Hz body is suspended while the menu is up** (`Revs::setFrontEnd`).  The menu runs ~5.5 s
*before* `engine_main`, so `vbi()` would have counted 273 fields, hit the 200-tick cap and handed
the engine a **200-tick backlog to run in one burst** before it had initialised anything — while
filling `g_bodyTicksDropped` with ~70 drops and thereby retiring a counter whose whole meaning is
"the main loop stopped reaching a drain point".  The *counting* is suppressed, not the ISR: the
flash phase, the copper work and `applyMode()` are all still needed for the menu to be on screen.

**Sabotaged five ways before being believed**: a 38-mosaic rule, a mis-routed option, no highlight,
SPACE accepted with no release, and a missing fixture file (a hard error, not a pass).  ⚠ The first
sabotage round was **contaminated by a stale object file** and produced two coherent wrong answers
— verify the patch took effect, *then* read the result.

### ✅ The four defects the rev-counter measurement turned up — ALL FIXED (2026-08-16)

The dial itself is **fixed and verified pixel-exact** against a real BBC at the same `$3C`
(`make refloop --park --force-revs=40` vs `amiga/screen_dump.gdb`; the needle went from ~40 wrong
pixels to zero).  The four things that measurement turned up beside it are now closed, and three
of them were more interesting than the dial:

1. ✅ **`$FE68` answered a constant `$0`, and it is the User VIA's T2 COUNTER** (not port B — the
   comment in `bbc_hw.cpp` was wrong; `docs/static-map.md` had it right).  It is Revs's only
   entropy source, at six sites plus the mirrors' shudder in the `$7B00` overlay, and a constant
   was not a harmless stub: the starter caught on the FIRST crank poll (`$498C`, `AND $09`), the
   idle sat at exactly `$28` where a real BBC reads `$2C` (`$49BD`, `AND #7`), and the
   gravel/skid trigger fired on EVERY call (`$0E7C`, `CMP #$3F`).
   ⭐ **The model is a CLOCK, not a PRNG, and that is measured rather than argued.**  The new
   `make refloop --park --via-t2` samples the value the real 6502 got at each site — from a
   breakpoint one instruction *after* each read, because reading T2C-L clears the T2 flag and a
   probe that reads the register perturbs what it measures.  At `$635F` (32 reads in one loop):
   28 distinct values over `$18..$CC`, and **22 of 31 successive samples land exactly on
   "previous value minus the microseconds elapsed"**, 24 of 31 within ±2.  So T2 free-runs down
   at 1 MHz past its timeout and the low byte is the elapsed-time low byte, negated.
   `Platform::hwMicros()` is the clock: `steady_clock` on the host, `VHPOSR` + the field count on
   the Amiga (one `move.w`), and under `REVS_FIXED_RNG` the deterministic field-counted base so a
   perf run stays pinned.  Verified: the host idles at `$3C = $2C`, the target jitters `$24..$2F`.
   ⚠ **It broke the autorun script, and that is a lesson about probabilistic effects.**  The
   starter step counted HITS, which was right only while `$FE68` was constant — a hit is not a
   catch (the crank succeeds about one poll in eight), so the engine stayed off for a whole run.
   `AutoStep::until` now holds the key until the GAME'S state says the job is done, with the poll
   count demoted to a failure bound.
2. ✅ **The port's engine never STALLED — and it was not the engine model at all.**  `$16BD`'s
   `DEC $58` runs on any frame a GEAR key is held (`$58` is cleared every frame at `$157F`), and a
   negative `$58` sends `$49D6` into the IDLE arm, which clamps the revs and never reaches the
   gear-driven computation that falls below 3 and stalls at `$4A3F`.  The engine was being told a
   gear key was down forever: `pressBbcKey()` WRITES the rawkey state, and the script's release
   step can only clear the codes the game POLLS while it is in force — two polls, two codes, and
   `'Q'` was not one of them.  Then `done()` stopped the script touching the input at all.
   Fix: one `RevsInput::releaseAllKeys()` at the handover.  Measured on the target before/after:
   `$61=ff $3C=2a $58=ff` at vbi 644/1238/2444 → `$61=00 $3C=00 $58=00` from vbi 641, which is a
   real BBC's parked behaviour.  ⭐ **A stuck key is invisible in every counter this port has**:
   `g_keyEvents` was 0 (no CIA traffic), the gear was stable, and nothing was dropped — only the
   game's own `$58` said so.  `amiga/dash_state.gdb` now prints `$3E/$3F` and `$2D/$58` because
   "the engine never stalls" and "something is holding a key" produce the same `$61/$3C/$63`.
3. ✅ **The gear indicator's 18 pixels: a one-column `'1'` cannot survive the double-width split.**
   `$508C` halves the glyph on the NIBBLE boundary, and the drawn `'1'` had its stem on bit 4
   alone — all of it in the left cell, so the readout drew a thin character hard against the left
   edge of a two-cell field.  A stem on art columns 2-3 straddles the split.  Measured (real BBC
   parked frame buffer vs the host's, same state): **18 differing pixels → 2**, and those 2 are
   the base serif, ours 4 columns where Acorn's ROM is 6 — the deliberate font divergence, so
   that is where it stops.
4. ✅ **`REVS_QUIT_AFTER_DUMP=1` exited before `REVS_MEM_DUMP` could write** — the quit sat
   between the two dumps, so asking for both silently yielded no memory dump.  Moved to the end
   of the block.

⚠ Two process notes from this session, both already in the memory and both hit again anyway:
**`make clean` before toggling a host build flag** (a mixed `STRAIGHT_TO_RACE` build sat in the
front end at frame 60 and its frame buffer read as a wholly different scene — 2562 differing
bytes, which looks like a catastrophic regression), and **two frame buffers can only be compared
if they come from the same model** — a dump taken under the constant-`$FE68` build is a different
simulation from one taken after the fix.

**Exit criteria for Phase 5: ✅ met.**  The race view, input, sound, the MODE 7 front end, both
fonts, COMPETITION mode with its field of cars, per-circuit code execution and now circuit
selection all work on the target.  What Phase 5 does **not** claim is performance: that is Phase 6,
and the baseline is still ~0.87 FPS.

---

## Phase 6 — Native twins, then asm 🔧

> ⭐⭐ **TWIN #1 IS SHIPPED AND IT WAS WORTH 62%** (2026-08-16): `irq1v_band_schedule` ($4E5C) native,
> **0.96 → 1.56 FPS**, `make validate` 25 628 cases / 0 mismatch, nine sabotages caught.
> `docs/perf-method.md` §Twin #1 has the table and the lesson (the win is the *absence of the
> interpreter*, not better code — gcc constant-folds a 16-entry 6502 palette loop into sixteen
> immediate stores).  ⚠⚠ It was also the item that found the harness could not see hardware
> writes at all; `docs/validation-harness.md` §a fifth.
>
> ⚠ **The order below is the order as of the last profile, and twin #1 invalidated it.**
> Re-run `phase4_prof.gdb` before picking twin #2 — the 51% row has just been cut by roughly
> two thirds and something else is now the top of the table.

### ⭐⭐ 0. FIRST, THE REPRESENTATION: render DIRECT to bitplanes — `docs/direct-bitplane-plan.md`

⚠⚠ **This section used to start at the asm, and that was the plan's biggest omission** (raised by
the user, 2026-08-16, and correct).  The port plots into a **BBC-shaped** frame buffer in `mem[]` and
then pays `RevsScreen::decode()` — 8320 bytes expanded to two bitplanes — to turn it into something
an Amiga can display: **~250 ms of a ~1282 ms frame, ~20%, and none of it is work the BBC did.**  It
also roughly doubles the render path's memory traffic (~33 000 accesses per frame against ~17 000),
on a machine whose standing rule is to reduce the NUMBER of accesses.

⚑ **The predecessor project shipped exactly this change and measured it**: ~339 → ~172 ticks/frame
for the stage it replaced (`~/Documents/Rescue on Fractalus`, `docs/terrain-render-plan.md` +
`docs/flight-perf-log.md`).  Its own verdict — "real but **not transformative**" — is the right
expectation here too, and the reason it still comes first is that **asm written against the current
arrangement is asm that has to be rewritten after it.**

The full treatment, including the layout choices it unlocks (interleaved planes, the blitter for
solid fills, single vs double buffer), the ⭐ free ~75 ms available today from skipping the sky band
in the decode, and — the part that keeps it a faithful port — **the decode becoming the validated
ORACLE rather than the shipping path**, is in `docs/direct-bitplane-plan.md`.  Read it before
touching a plotter.

🛑 **What this item does NOT have to design around, corrected 2026-08-16 (user, and correct): the
5.5 KB of live engine code that renders as the sky.** §4 used to call it "the constraint that cannot
be designed away".  It is not a constraint at all — from the Amiga's side those bytes are ordinary
code and variables in `mem[]`, the bitplane is a separate buffer that aliases nothing, and the BBC's
own band 1 made their content unobservable anyway.  ⭐⭐ It in fact **inverts**: the "band boundary
wrong ⇒ engine code shows as noise" failure mode (the measured black bar of 2026-08-14) exists *only*
because `decode()` reads those bytes and expands them into pixels.  Filling the sky rather than
decoding it deletes the hazard.

⭐⭐ It also records the OTHER inherited lever, which may be larger: RoF's biggest single win was not
asm and not direct rendering but **per-instrument dirty flags, ~23×** — and Revs's number-one hot
item is the **dashboard** at 36.1%.  ⚠ With the caveat that the `$7B00` overlay already carries a
per-column dirty test, so the analogy needs one shape counter before it is believed.

### ⭐⭐ 0b. …AND THEN HARDWARE SPRITES for the instruments — `docs/direct-bitplane-plan.md` §8

⚠ **A second omission the user raised** (2026-08-16, and also correct): **the BBC has no sprites, so
every moving thing on the dashboard is CPU-drawn — and the Amiga has eight sitting idle.**  Put the
wheel, the rev-counter, the steering marker and the gear indicator on sprites and the **cockpit
bitmap becomes fully static**, drawn once and never touched.  That aims squarely at `$7BE2`, the
36.1% item.  Three of the four constraints turn out favourable (a 2-bitplane playfield means one
unattached sprite already covers the whole palette, with sprite colours 16-31 free of the copper's
band list; the 320 px display is exactly sprite resolution; and the dashboard's position at the
bottom of the field makes its data late-safe) — the open one is **width**: 8 × 16 px = 128 px of 320,
in a single 42-line band where vertical sprite reuse buys nothing.

**Placement: a Phase 6 item, gated behind item 0** (user decision, 2026-08-16) — sprite work written
against the current frame-buffer arrangement gets rewritten by the representation change, exactly as
asm would.  It is gated behind the §7 dirty-column counter too, which *sizes* it: if the overlay's
existing per-column tests already make the static cockpit nearly free, the win shrinks to the moving
instruments alone.  ⭐ And the faithfulness rule is the same one as the decode's: **pre-render each
sprite variant by running the game's own drawing code**, so the images are derived from the oracle
rather than redrawn by hand.  The wing mirrors stay CPU-drawn — their content is the scene, not a
glyph with N states.

### ⭐ 0c. …AND THE NAMING BATCH RUNS BEFORE THE FIRST TWIN — `docs/rename.md`

**Gated ahead of item 1** (user decision, 2026-08-16), for the same reason items 0 and 0b are gated
ahead of the asm, and the argument is mechanical rather than aesthetic: **a twin is HAND-WRITTEN, so
`make gen` cannot re-rename it.**  Every generated `$1C1C` in `revs_gen.c` is fixed by editing
`disasm/symbols.csv` and regenerating; the same name typed into `revs_native.c` — in the `_core`
signature, in its locals, in its comments — is fixed by hand, once per site, forever.  Renaming is
cheap exactly up to the moment the first twin is written, and stops being cheap immediately after.

Three things this batch has to produce, in this order:

1. **Resolve the queued rename candidates**, `docs/rename.md`'s table plus its ⭐ next-up item
   (`$1C1C` was the ⭐ one, and it is now `plot_view_src_line` — a line plotter into
   `view_src_blocks`, never a projection).  Anything in the three hot subtrees below is in scope;
   the rest of the backlog is not.
2. ⚠ **Name the CELLS, not just the routines** — `src/gen/mem.h` currently carries **17** names,
   generated from `symbols.csv`'s var rows, and a hot-path twin touches far more `mem[]` cells than
   that.  Without this pass "use the `mem.h` name" degrades to `mem[0x62FC]` typed into hand-written
   C, which is the transliteration's readability with none of its regenerability.
3. **Then batch-rename via the transpiler** (`symbols.csv` → `make gen`), and only then start item 1.

Scope is the three item-1 targets and their subtrees — `$7BE2` (the `$7B00` overlay), `$1A20` →
`interp_edge` → the span plotters, and `build_track_geometry` `$24F6` → `road_edge_start` /
`road_edge_walk` / `project_point` / `road_edge_side`.  This is not a re-run of Phase 2.4's
concentrated pass over the whole image; it is that pass finished for the 40% of the frame Phase 6
is about to rewrite by hand.

### ⭐⭐ 0d. …AND THE PROFILE WAS RE-MEASURED FIRST, WHICH MOVED THE TARGETS (2026-08-16)

⚠⚠ **Everything below this line was ordered by a share table that was missing half the frame.**
The 2026-08-13 profile charged the port's paint call 2.5%; split properly it is **60.9%**, and inside
it the **50 Hz game body is 51.1% of the frame on its own** (`docs/perf-method.md` §Where the time
goes, re-measured with phases 26/27/28).  The body was invisible to the old table because it still
ran inside the VERTB ISR then, where no main-loop bracket could see it.

| | share | ms/frame | |
|---|---|---|---|
| the 50 Hz body (phase 26) | **51.1%** | 526 | ~52 ticks per painted frame, so **~10 ms of each tick's 20 ms budget** — half the machine, independent of the framerate, and faithful (a BBC's VIA fires regardless) |
| `$7BE2` the dashboard (24) | 14.6% | 150 | and **2093 dirty tests for 83 stores** per sweep — §7a of the plan |
| the decode (27) | 8.1% | 83 | port overhead; **not** the ~250 ms the plan assumed |
| the road subsystem (5 + 11) | 13.5% | 140 | build then draw, still one subsystem |
| the vblank spin (28) | 2.6% | 27 | |

⭐ **So the first hand-optimisation target is `tick_wheel_spin`, the 50 Hz body's own arm** — never profiled,
never split, and the only thing in the port that both simulates and draws (display lines 120-143).
It is also the one row a faster renderer cannot help.

### 1. Then the twins and the asm

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
| `build_track_geometry` `$24F6` | 19.0% | the road-geometry projection pass — it BUILDS the edge lists `$1A20` draws |

⭐ **Rows 2 and 3 are one subsystem, 40% of the frame: build the road's edge lists, then draw
them.**  That reframes the choice.  It is not "optimise `$1A20` or `$24F6`" — a change to how the
road geometry is represented (`edge_x_lo/hi` + `edge_y`, 2×40 points, produced by phase 5 and
consumed by `interp_edge` in phase 11) moves both rows at once, and is likely worth more than
hand-asm on either half alone.  Look at the representation before writing any asm.

So the eventual hand-asm target IS a rasteriser after all, and the Atari port's terrain-loop
experience transfers as more than method.  The lever is unchanged: control the registers, force
`(a0)+`, verify with an in-process differential, never cross-run.

### ⭐⭐ 1a. The per-function checklist — what "make it native" MEANS, item by item

⚠ **This list did not exist until the user asked for it** (2026-08-16).  Phase 6 named its targets
and its levers and then said "the twins", leaving the actual per-routine work implicit — and two of
the eight items below turn out to be *constrained* rather than free, which is exactly what an
implicit list hides.

✅ **`irq1v_band_schedule` ($4E5C) has been through it** — the first twin the project has, and the list
survived contact with one addition: **item 9, sabotage the fixture before believing it.**  Items 5
(`mem.h` names) and 0c (the naming batch) were *skipped* for it and that was the right call for
this routine — it touches eleven `mem[]` cells, four of which are the ULA tables it indexes by
address — but the argument for doing the batch before a twin with a wide `mem[]` surface stands.

For each routine promoted out of `revs_gen.c`:

1. **Make it native.**  Address into `VALIDATE_FUNCS`, transliteration becomes the `__t6502`
   oracle, plain name links from `revs_native.c`, **and register a fixture in
   `validate_native.c`** — step 1 alone creates an oracle with nothing comparing against it, and the
   harness now fails rather than passing vacuously (`docs/validation-harness.md`).
2. **Real C, no 6502 idioms.**  ⚠ With one class called out by name: an idiom that touches the
   **stack pointer** has no C equivalent and both known ways of handling it were bugs — modelling
   `TXS` as a register write was a hang, modelling it *and* the control flow leaked `S` upward until
   it wrapped.  Model the control flow, leave `S` alone (`docs/perf-method.md`).
3. **A typed `_core(...)` taking real arguments**, plus the `void <name>(void)` 6502-ABI shim that
   marshals `mem[]`/`cpu` ↔ the core.  The shim is the only part that knows about the ABI.
4. **Structured control flow and real locals instead of `mem[]`** — ⚠ **bounded per cell, not
   blanket.**  The harness diffs the whole of `mem[]`, so a cell may become a local only where it is
   *proven* dead (no reader before the next write); an output cell stays an output.  Proven-dead
   cells go in the fixture's ignore list **with the proof in a comment**, never to make a test pass
   (`docs/faithfulness-seam.md` §What "validated" costs).
5. **`mem.h` names for every cell that has one** — which is what item 0c above exists to guarantee.
   A twin written before that pass bakes bare hex into hand-written code.
6. **BBC hardware writes: `#ifdef`-GUARD them, do not delete them.** ⚠ Deleting is the one item on
   this list that can quietly cost the routine its proof: the BBC-faithful path is what the oracle
   diff compares against, so a removed `bus_write` is a permanent hole in the differential rather
   than a saved cycle.  The seam rule already covers it — the Amiga variation is an
   `#ifdef REVS_PLATFORM_AMIGA` *omission* inside a still-validated twin.  And ⚠ "does nothing on
   the Amiga" is a claim to check per register, not per routine: `bbc_hw.cpp` models the **User VIA
   timers**, and those timers ARE the raster band schedule.
7. **Comment what the routine DOES, not what the instructions did.**  The transliteration is already
   a complete record of the instructions and stays checked in as the oracle; a twin that re-narrates
   it adds nothing and ages badly.  Say what it computes, what it reads, what it leaves behind, and
   name any cell the fixture ignores together with why it is dead.
8. **Append every bad or missing name to `docs/rename.md` as you find it** — the standing convention,
   unchanged.  Item 0c is the concentrated batch *before* the twins; this is the trickle *during*
   them, and the trickle still gets batched through `symbols.csv`, never renamed piecemeal in
   generated files.
9. ⭐⭐ **SABOTAGE THE FIXTURE BEFORE BELIEVING IT** — added 2026-08-16, by twin #1.  Inject a
   handful of deliberate defects (an off-by-one in each branch condition, a dropped write, a
   register not restored, the callee not called) and require each to FAIL.  Twin #1 passed on
   its first run and *four* of its nine sabotages passed too: the differential could not see
   hardware writes, which are almost the whole output of that routine.  A green first run on a
   harness that is blind to the output looks exactly like a green first run on a correct twin.

Then `make validate FN=<name>` must show **0 mem mismatch**, and the perf claim comes from the
in-process differential (`make VERIFY=1 PROBES=1 FIXED_RNG=1`), never from a cross-run framerate.

---

## Phase 7 — Packaging ⬜

WHDLoad slave, a player-facing README (keys, requirements), and an asset audit: ship only what
the port needs, not the original disc image.
