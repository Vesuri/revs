# Rendering DIRECT to bitplanes — the Phase 6 lever the plan was missing

> **Status: PLAN, not shipped.** Written 2026-08-16 after the user pointed out that the Phase 6
> target list (`docs/phases.md`) priced hand-asm on the hot functions and never questioned the
> arrangement those functions render *into*. It is the right objection: the port currently draws the
> way the BBC drew, into a BBC-shaped buffer, and then pays a whole extra pass to turn that into
> something an Amiga can display. ⚑ **The predecessor project shipped exactly this change and
> measured it** (`~/Documents/Rescue on Fractalus`, `docs/terrain-render-plan.md` +
> `docs/flight-perf-log.md`), so most of what follows is inherited rather than invented.

## 1. What the port does today, and what it costs

The transliterated engine plots into the BBC frame buffer in `mem[]` (`$5A80-$7AFF`, 8320 bytes,
character-cell interleaved — `offset = charRow*320 + cell*8 + lineInRow`). Once per painted frame
`RevsScreen::decode()` walks all 8320 bytes and expands each into two bitplane bytes through a pair
of 16-entry nibble tables.

Measured (`docs/perf-method.md` §THE BASELINE, 2026-08-13):

| build | FPS | frame |
|---|---|---|
| pre-Phase-5 (`Revs::render` counts and returns) | 1.46 | 685 ms |
| + copper bands + 2-plane display DMA + double buffer (`NODECODE=1`) | 0.97 | 1031 ms |
| + the frame-buffer decode (shipping) | 0.78 | 1282 ms |

So **the decode is ~250 ms of a ~1282 ms frame, ~20%** — and every microsecond of it is work the BBC
never did. It exists only because the port renders into one representation and displays another.

⚠⚠ **CORRECTION, 2026-08-16: the decode is 83 ms, not ~250.**  It now has its own phase bracket
(phase 27) and the direct measurement is 83 ms of a ~1040 ms frame, **8.1%** — after the flat-band
skip took 43 ms off it, so ~126 ms before.  The three-row table above is three separate runs, and the
difference it attributed to the decode also contained the 50 Hz body's **51.1%**, which no bracket
could see until the body left the VERTB ISR.  ⭐ This does not retire the plan: 83 ms is still pure
port overhead, the memory-traffic argument in §2 is unchanged, and §3-§5 are what unlock blitter fills
and dirty-region drawing.  It does change the *sizing*: direct rendering is worth ~8%, while the body
is worth 51% and the dashboard 15%.  Order the work accordingly (`docs/phases.md` item 0d).

⚠ The other ~350 ms of the rendering cost is what turning display DMA on costs a CPU whose program
and `mem[]` are both in chip RAM. On a stock A500 that is structural, and this plan does **not**
touch it.

## 2. The memory-traffic argument, which is the one that counts here

`docs/m68k-optimisation.md`'s standing rule is that RAM is uniformly slow, so you optimise by
reducing the **number** of accesses. Per painted frame, over the frame-buffer region:

| | reads | writes | total |
|---|---|---|---|
| today | 8320 (the decode's source) | 8320 (the game's plots) + 16640 (two planes) | **~33 000** |
| direct | — | ~16640 (two planes) | **~17 000** |

⚠⚠ **AND THIS IS WHY "choose the layout wisely" IS LOAD-BEARING RATHER THAN DECORATIVE.** A BBC
MODE 5 byte carries **four** pixels in one byte (two colour bits each, interleaved as bits 7-4 /
3-0 — see `bbc_screen.h`). The same picture on a 320-wide 2-plane Amiga display is **two** bytes,
because those four pixels are doubled to eight. So a plotter writing bitplanes directly writes
**twice as many bytes as the BBC plotter did**. The net is still about half the traffic, but the
margin is not large, and an implementation that recomputes an address per pixel, or keeps two
independent plane pointers, can spend the entire win on addressing.

## 3. What going direct unlocks (and the choices it forces)

⭐ The freedom is the point. RoF's plan puts it exactly right: *"because the native renderer bypasses
the convert entirely, **we choose the planar encoding**"* — polarity, plane assignment and
addressing are all ours, and identical output can be had from whichever is cheapest as long as the
copper palette matches.

1. **Interleaved bitplanes** (`BPL1MOD`/`BPL2MOD` = one row's bytes) put plane 0 and plane 1 for the
   *same display line* a fixed `rowBytes` apart. One address register, `move.b d0,(a0)` +
   `move.b d1,rowBytes(a0)` — instead of two pointers walked in step.
2. **The character-cell interleave disappears, and so does its special case.** The BBC layout makes
   a vertical step within a cell `+1` and a step across a cell row `+$138`; that correction is
   carried in the span code itself (`$7FCC`'s `SBC #$38 / SBC #$01`, `docs/static-map.md` item 6).
   A linear bitplane makes every vertical step a uniform `+rowBytes` and deletes the case analysis.
   ⚠ It also makes the *horizontal* step `+1` instead of `+8`, which is cheaper — but the road
   rasteriser draws mostly **vertical** spans, where the BBC's `+1` was cheaper than `+rowBytes`.
   Price the real loop shape; do not assume the linear layout wins everywhere.
3. **The blitter takes the solid fills.** RoF ended with `blitterFillUp` for the terrain mass and
   `blitterClear` for the plane clear, overlapped with disjoint CPU work; Revs has large flat
   regions (sky, ground, dashboard panel) with the same shape. Blitter time overlapped with CPU
   work is free wall-clock; blitter time waited on is not (RoF has a whole section on the stall).
4. **⚠ SINGLE OR DOUBLE BUFFER IS NOW A DECISION, and the game's renderer constrains it.** Revs's
   renderer *never clears* and paints incrementally across the frame (`amiga/Revs.cpp`), so drawing
   direct into a back buffer that is a frame stale does not compose — the missing paint is the
   previous frame's. The BBC single-buffers, so single-buffering is the faithful default and the
   tearing it shows is the tearing the original showed. ⚑ RoF hit this and had to add a terrain
   double-buffer to stop plane-1 flicker (`afc509f`), so decide it deliberately and measure it.

## 4. The code-in-the-sky is NOT a constraint on this plan — it is a hazard the plan DELETES

🛑 **This section used to be headed "the constraint that cannot be designed away", and that was
wrong** (raised by the user, 2026-08-16, and correct). The claim was that `$5E40-$66FF` — 5.5 KB of
engine variables *and* 462 disassembled instructions sitting **inside** the BBC frame buffer, on
display under palette band 1 — somehow bounds the Amiga's bitplane design. It does not, and the
reason is worth stating plainly because the phrasing had already propagated into `docs/phases.md`:

**From the Amiga's point of view those bytes are just code and variables in `mem[]`.** The
transliteration executes them as C functions; they occupy an address in `mem[]` for the same reason
every other engine routine does. That the BBC's 6845 also *scanned* that address range is a fact
about the BBC's shared-memory video, and it carries over to the Amiga in exactly one respect: the
picture there must come out flat blue. And it does, for free — the BBC's own band 1 maps all sixteen
palette entries to the same blue, so the byte content was **unobservable on the original too**. A
direct-to-bitplane renderer never copies those bytes anywhere, and a bitplane is a separate buffer
in chip RAM, so nothing aliases anything.

⭐⭐ **And the aliasing inverts: direct rendering removes a hazard the decode currently carries.**
`bbc_screen.h` records that the raster phase "matters to the pixel" — get a band boundary wrong by a
few lines and the port shows the engine's own code as noise where the sky belongs, which is precisely
the measured black bar of 2026-08-14. That failure mode exists **only because `decode()` reads those
`mem[]` bytes and expands them into pixels.** Fill the sky instead of decoding it and there is no byte
to misinterpret and no boundary error that can turn code into noise. Likewise a plotter whose address
arithmetic wandered into the range would corrupt the running program on a BBC; on the Amiga the same
write lands in a bitplane and is merely a wrong pixel.

The one thing that genuinely does not move: **`mem[]` keeps its role as the program's address
space.** Nothing here reclaims the frame-buffer region of `mem[]` or shrinks it to a display-sized
buffer. Only the **drawing** moves out.

⭐ And the free win that section 4 was really carrying survives intact, available today with no
architecture change: **the decode does not need to convert the sky band at all.** It renders flat
blue whatever the bytes say, so those ~63 of 208 display lines (~30% of the pass, ~75 ms) can be
skipped for a boundary check. ⚠ The band boundary can cut mid-character-row, so take it from
`m_plan`/`m_lineMode` — which `decode()` already builds — and not from a literal.

### 4a. ⭐⭐ COLOUR-00-AND-A-COPPER-CHANGE: it buys the sky, and it CANNOT buy the terrain

Asked by the user, 2026-08-16, before starting Phase 6: *does the BBC change the background colour
dynamically at the horizon, or does that involve filling?* — with the proposal that the Amiga use
`COLOR00` for **both** sky and terrain and have the copper change it at the horizon, since the view
never tilts. Measured on a driving Silverstone practice session (host dumps, `REVS_SCREEN_DUMP`,
frames 200-202 and 700-703, cross-read against the band record `f.bands`):

**The BBC does BOTH, and the seam between them is at a FIXED display line — not at the horizon.**

| lines | how the colour gets there | filled? |
|---|---|---|
| 0..17 (band 0, MODE 4) | palette: pen 0 blue, pen 1 yellow — the two text rows | the loader blanks rows 0-2 |
| **18..81 (band 1), 64 lines** | **palette ONLY: all sixteen entries → the same blue** | **NO. These bytes are the engine's code and variables (`$5E40-$66FF`) and are never touched by any plotter** |
| 81..horizon (band 2) | palette pen 0 black / 1 blue / 2 white / 3 green | **YES — solid `$0F` (pen 1) across all 40 cells, every line** |
| horizon..165 (band 3) | same, except **pen 1 → RED** | **YES** — pen 3 green grass, pen 0 black road, pen 1 red markings |
| 166..207 (band 4) | pen 3 → cyan; then the game body runs | YES — the dashboard |

- **Band 1's duration is FIXED at 4038 us = 63.1 lines** (the value the handler latches during band
  0, identical in every frame sampled), so the flat-blue region is always lines 18..81.1. What moves
  with the hills is **band 2's** length — `horizon_latch` read `$0558` (1368 us, 21.4 lines) in one
  scene and `$04D8` (1240 us, 19.4 lines = `MoveHorizon`'s neutral value) in another. ⚠ The
  paragraph above used to say the *sky* boundary moves with the hills; it does not, and that makes
  the skip-the-sky win easier than §4 claimed, not harder.
- **The horizon really is a straight, full-width, whole-scan-line boundary** — the strongest possible
  support for the user's premise, because the original machine implements it as a *timer value*, and
  the pixel data agrees: in frame 700 line 99 is 40 cells of `$0F` and line 100 is 40 cells of `$FF`,
  dead straight. (The pixel flip is one line *above* the band 2→3 boundary, which is harmless
  precisely because band 2's palette already carries green.)

**So on the Amiga:**

1. ✅ **Lines 18..81 (64 lines) — take the proposal in full.** `COLOR00` = the band-1 blue, both
   planes left alone. That is 2560 of the 8320 buffer bytes (31%) that need neither a decode nor a
   fill, and it is the same win §4 describes, arrived at from the other end. **Shipped 2026-08-16 as
   the flat-band skip** — §6 step 1; `g_decodeFlatLines` measures 64 in exactly 1 band on the target.
2. ⚠ **Lines 81..horizon — the sky there is real pixel data and the copper alone will not do it.**
   Band 2 uses all four pens above the horizon: sampled frame 700 has a black (pen 0) object at cells
   32-33 spanning lines 94..108 — a trackside structure straddling the horizon — which is exactly the
   real machine's "min 0, max 18 zero bytes, longest run 3 cells" in `docs/bbc-reference-loop.md`
   read as *content* rather than as noise. Make `COLOR00` blue there and that object disappears. The
   strip can still be made write-free, but only by *permuting the pens* for that band (sky → colour
   0, black → a spare register; red is unused above the horizon so there is room) — legal, because a
   direct renderer owns both the encoding and the palette, and then the strip is one blitter clear
   instead of ~21 lines of fill.
3. ❌ **Below the horizon the trick inverts and loses.** Pen 0 there is the **ROAD**, not the grass —
   i.e. the road is *already* the free colour, and the grass is genuine pixel data that has to
   coexist with red markings and white cars. Setting `COLOR00` = green would require swapping road
   and grass in the encoding, which converts a full-width grass *fill* into a full-width road
   *clear* (double-buffered, so "already zero" is never true). No win; keep pen 0 black below the
   horizon and let the ground be drawn.

The net: the copper change at the horizon is the right mechanism, but it is **two** transitions, not
one — a fixed one at line 81 that is pure profit, and the moving horizon one, which is where the
blitter takes over from the CPU rather than where colour replaces pixels.

## 5. Validation — how this stays a faithful port

Today's contract is byte-exact `mem[]` (`make validate FN=<name>` → 0 mismatch). A plotter that
writes bitplanes writes nothing to `mem[]`, so that diff cannot be the check. ⚑ RoF's shipped answer
splits the routine instead of abandoning the oracle:

1. **Split each hot plotter into "compute the geometry" and "plot".** The geometry half keeps a pure
   `mem[]` contract and stays fully `make validate`-able against the `__t6502` oracle. RoF kept its
   per-column silhouette array `$260E` diffable exactly this way while the plotting diverged.
2. **For the plotting half, keep an exact oracle anyway — Revs can do better than RoF could.** RoF
   fell back to screenshot parity against atari800. Here, `RevsScreen::decode()` is *already proven*
   against the game's own output, so it becomes the **reference implementation** rather than the
   shipping path: run the transliterated plotter into `mem[]`, decode it, and require the
   direct-to-bitplane output to be **identical bit for bit**. That is an exact, automatable,
   6502-anchored differential for a routine that no longer touches `mem[]`.
3. **`make refloop` remains the backstop** for anything (2) cannot cover — the real BBC's own frame
   buffer, which is what settled the horizon artefacts.
4. ⚠ **The generalisable trap RoF recorded, and it will apply here.** Converting a `mem[]` handoff
   into a register/direct handoff makes you inherit the callee's preconditions. Its rasteriser
   relied on its own reload to clean a byte-width invariant that the caller did not maintain; the
   in-process differential reported **126 mismatches in 3812 calls** until an `and.w #$FF` was added
   at the call site. Whatever replaces `mem[]` as the handoff, put it under
   `make VERIFY=1 PROBES=1` and read the count.
5. Draw-order changes are a **behavioural** delta, not a representation one: RoF's split of
   "all terrain, then all objects" changed what occludes what. If any restructuring here reorders
   drawing, that part cannot be mem-diffed at all and needs the reference machine.

## 6. Sequencing, and an honest expectation

⭐ This is a **representation** change, which is precisely what `docs/phases.md` Phase 6 already said
to consider *before* writing any asm — it just never named this one. It belongs at the front of
Phase 6, ahead of hand-asm on `$7BE2`/`$1A20`/`$24F6`, because asm written against the current
arrangement is asm that has to be rewritten after it.

Order:

1. **Skip the sky band in the decode** (§4). Independent, cheap, no faithfulness cost. ~75 ms.
2. **Shape-probe before restructuring** (`docs/perf-method.md` Rule 4). Count what the plotters
   actually do per frame: spans by orientation and length, how many of the dashboard overlay's 40
   columns are dirty, how much of the frame is solid fill. RoF's −36% came from input-distribution
   counters, not from PC sampling.
3. **Choose the layout** from those numbers (§3), and write it down with the reasoning.
4. **Split geometry from plotting** on the road subsystem (`$24F6` builds the edge lists `$1A20`
   draws — one subsystem, 40% of the frame) and stand up the decode-as-oracle differential (§5)
   *before* the first direct plot.
5. **Then** the blitter fills, then asm.

**Honest expectation** — RoF's own summary of the same change was *"real but not transformative"*,
and its measured result was **~339 → ~172 ticks/frame** for the stage it replaced. Here, deleting
the decode is worth ~250 ms of a 1282 ms frame; the DMA contention behind it (~350 ms) is
structural, and the engine's own ~700 ms is untouched by any of this. So direct rendering is
**necessary and not sufficient**: nothing else removes that 250 ms, and it is what unblocks blitter
fills and dirty-region drawing — but the floor still needs the engine.

## 7. ⭐⭐ The OTHER inherited lever, which may be bigger — and is not yet measured here

RoF's single largest win in its whole perf log was neither asm nor direct rendering. It was
**per-instrument cockpit dirty flags replacing a 560-cell shadow scan: ~1662 → ~65 ticks/frame,
~23×** (`8b255fa`). Revs's number-one hot item is `$7BE2` — **the dashboard** — at **36.1%** of the
frame, and a dashboard of mostly-static instruments redrawn wholesale is that problem exactly.

⚠ **But do not assume the analogy holds, because Revs may already do it.** The `$7B00` overlay is
two fully-unrolled chains of 17-byte column units, and each unit *opens with a dirty test*
(`LDY table,X ; BEQ +8`), while the COUNT/START self-modification steers how many columns the sweep
touches (`docs/static-map.md` item 10). So the game carries a per-column dirty mechanism of its own,
and the cost may already be dirty-limited — in which case the win is in how those columns reach the
screen (i.e. §3-§5), not in adding flags the game already has.

**The measurement that decides it, and it is cheap:** count how many of the 40 columns are dirty per
frame, and how many bytes each dirty column actually writes. That is one shape counter in the
overlay's unit prologue. Until it exists, "the dashboard is 36% and mostly static" is an assumption,
and this project's own rule is that an assumption with a one-command measurement behind it is a
to-do, not a tag.

### ✅ 7a. MEASURED (2026-08-16) — the sweep is a SCAN, and the analogy does NOT transfer

`src/platform/shape.h` + two hooks around the main-loop `JSR $7BE2` (`make SHAPE=1`;
`amiga/dash_shape.gdb` on the target, `REVS_SHAPE_WATCH=N` on the host). The before/after difference
over the sweep's own rectangle IS its store count, so nothing had to be instrumented inside the
chain. Target, 175 loop frames of a driving Silverstone practice session:

| | per sweep |
|---|---|
| column units that RAN (dirty tests) | **2093** |
| of those, units that STORED a byte | **83** (host, moving car: 84-119) |
| columns holding at least one dirty source | **37 of 40** |
| sources left pending after the sweep | **0** |

⭐⭐ **So 96% of the sweep is a dirty test that finds nothing, and the drawing is ~83 bytes a frame.**
Three consequences, and they redirect two other sections of this plan:

1. **RoF's dirty-flag win cannot be repeated here.** Its 23× came from *adding* per-instrument flags
   where a 560-cell shadow scan had none. Revs already tests per cell — the game is dirty-limited in
   its *stores* and scan-limited in its *cost*. Another layer of flags on top has nothing to remove.
2. **The lever is to stop SCANNING, not to draw less.** The producers know which cells they wrote
   (they write the column sources), so a dirty *list* — append on produce, walk on consume — replaces
   2093 tests with ~83 visits. That is a representation change of exactly the kind §3-§5 are about,
   and it is bigger than anything hand-asm can do to the test itself.
3. ⚠ **It also shrinks §8 (sprites) as a *drawing* win** — 83 bytes/frame is already almost nothing —
   **while leaving it intact as a way to delete the scan**: if the moving instruments become sprites,
   nothing writes those column sources at all, and the sweep they drive can go with them.

⚠ **The dirty columns are spread, not clustered:** 34 of the 40 columns are dirty in nearly every
sweep, and per-row the dirt is confined to the first ~23 rows of the X range ($2C..$42), so the
moving content is a horizontal band across the whole dashboard rather than a few instruments. Any
"only redraw the instrument that moved" scheme has to answer that shape first.

⚠⚠ **AND THE SHARE THIS SECTION QUOTES LOOKS STALE.** The same run reads phase 24 (`$7BE2`) at
**15.9%, 173 ms/frame** — not 36.1% — with `$1A20` at 5.7% and `$24F6` at 7.1% against the published
21.1% and 19.0%. That table is from 2026-08-13 and the port has changed underneath it (the 50 Hz body
moved out of the ISR, the T2 clock became a clock, the flat-band skip landed). It is a PROBES+SHAPE
build, so treat the numbers as provisional until `phase4_prof.gdb` is re-run clean — but do not plan
against 36.1% in the meantime. ⚠ Also worth knowing for every unattended run: a straight-line
autorun leaves the track after ~225 game frames and the car then stalls ($61=00 $3C=00 $63=00), so a
long run measures a moving car and then a parked one. `dash_shape.gdb` now prints the engine state
beside the shape for that reason.

### ✅ 7b. MEASURED (2026-08-16) — **95% of the decode re-converts bytes that did not move**, and
### the main loop draws almost NOTHING

Step 2's other half, finally read out: `amiga/frame_shape.gdb` on a `SHAPE=1 PROBES=1
STRAIGHT_TO_RACE=1 FPSCOUNT=1` target run with the car genuinely under power (`$61=FF`, gear `$1A`),
41 loop frames / 45 paints.

| | per painted frame |
|---|---|
| frame-buffer bytes the decode converts | 8320 |
| **frame-buffer bytes that CHANGED since the previous paint** | **406 (4.9%)** — max 5046, one 207-byte paint spanning lines 24..172 |

⭐⭐ **So `decode()`'s 81 ms is ~95% re-conversion of an unchanged picture.** That reprices the whole
of §1-§6: the decode is not expensive because converting is expensive, it is expensive because it
converts *everything*. A dirty-region decode captures most of that win without writing a single
direct plotter — and it is strictly less work than the plotter rewrite it would defer.
⚠ Two things it must get right, and both are already known: the Amiga is DOUBLE-buffered, so the
shadow to compare against is *this buffer's* last paint (two frames back), not the last paint; and
the comparison must be cheaper than the conversion it skips (a longword compare over the buffer is
2080 reads against 8320 table lookups plus 16640 writes).

⚠⚠ **AND THE SECOND HALF IS A CORRECTION TO THIS PLAN AND TO `docs/phases.md`.** The same run
attributes every frame-buffer write to the main-loop phase that made it:

| phase | bytes/frame | lines | what it really is |
|---|---|---|---|
| 24 `$7BE2` | 116 | 53..191 | the dashboard — **the only main-loop phase that writes real pixels** |
| 5 `$24F6` | 89 | 24..53 | ⚠ inside the flat-blue sky band — engine VARIABLES, not pixels |
| 10 | 80 | 24..39 | ⚠ variables |
| 13 | 79 | 24..39 | ⚠ variables |
| 15 | 7 | 24..55 | ⚠ variables |
| 11 `$1A20` | **6** | 26..55 | ⚠ variables — 6 bytes, and none of them visible |
| 25 | 9 | 50..140 | the paint bracket itself |

**`$24F6` + `$1A20` are 19.6% of the frame and write six visible bytes between them.** This project
has called them "the road subsystem, 40% of the frame, build-then-draw" since 2026-08-13; the build
half is real, but **the draw is not there.** The pixels come from the 50 Hz body, which is not a
main-loop phase and which no bracket in `phase4_prof.gdb` attributes — consistent with
`docs/amiga-arch.md`'s "the body DRAWS", and it is where a direct renderer has to attach.

⚠ **The host cannot measure any of this, by construction.** It runs the body ONCE per painted frame
where the target drains ~30, so its picture is frozen: 2 of 8320 bytes changed between host frames
400 and 410, cross-checked against two independent `REVS_SCREEN_DUMP`s. A host `SHAPE` run also
needs `STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` or the car never leaves neutral and the stimulus is
absent altogether — `frame_shape.gdb` prints `$61`/`$63` beside the shape for exactly that reason.

### ✅ 7c. SHIPPED (2026-08-16) — the dirty-region decode, **1.77 → 1.96 FPS (+10.7%)**

§7b's number acted on. `RevsScreen::convertRace()` compares each **8-byte cell column** against a
shadow of the bytes that produced the buffer's current content and converts only what moved.

| build | vblanks | painted | FPS | note |
|---|---|---|---|---|
| HEAD before this change (twin #2 baseline) | — | — | **1.77** | the standing baseline |
| shipping (dirty) | 9783 | 383 | **1.96** | `g_decodeCells` mean **166 of 1040**, last 31 |
| `make DIRTY=0` | 9781 | 286 | **1.46** | same loop, test disabled |

⚠⚠ **THE CONTROL IS NOT THE OLD CODE, AND THE TWO COMPARISONS DISAGREE BY DESIGN.** `DIRTY=0` is
the *new* cell-major loop with the test switched off, so the honest reading is two facts, not one:
the dirty test is worth **+34%** against its own control, and the **cell-major restructure it
needed costs ~18%** on its own (1.77 → 1.46) — a display line's 40 bytes are 40 sequential
destination stores, a cell column's 8 lines are 8 stores strided by `kRowBytes`. Net **+10.7%**
against the baseline, and quoting the +34% alone would be quoting a win against a build that never
shipped. ⭐ It also means there is ~18% still on the table for a hybrid that takes the old
line-major path when a whole row is dirty — unmeasured, and only worth it if the frame count of
fully-dirty rows justifies it (`g_decodeFullFrames` reads 1, the first frame).

**Three design points, each of which a differential caught rather than reasoning:**

1. **TWO shadows, indexed by `m_back`** — the Amiga is double-buffered, so the comparison base is
   what *this* buffer was decoded from, two decodes ago.
2. **A line's MODE can change while its byte does not.** Diffing `m_lineMode` against a shadowed
   copy and dirtying the whole character row on a change is **load-bearing**: removing it (sabotage
   2) left **1350** wrong bytes in 12 frames.
3. **No "shadow valid" flag is needed** and one should not be added — see the comment at
   `s_shadow`: zero-initialised storage makes frame 1 a full convert, and MODE 7 draws into its own
   bitmap so a race buffer still matches its shadow across a front-end round trip.

**The oracle: `make DIRTYCHECK=1`** re-runs the conversion unconditionally into a copy of the
buffer the dirty pass just wrote and requires byte equality — §5's "the decode is the reference
implementation" idea, arrived at one step early and now standing machinery for the plotter work.
Measured **0 mismatches / 12 checks** with the car under power, and it catches both sabotages
(test fails closed: 17015; mode change ignored: 1350). `amiga/dirty_decode.gdb` reads all of it.

## 8. ⭐⭐ HARDWARE SPRITES for the instruments — capability the BBC never had (user, 2026-08-16)

**The observation.** The BBC has no sprites, so *every* moving thing on the Revs dashboard is drawn
by the CPU into the frame buffer. The Amiga has eight. If the wheel, the rev-counter, the steering
marker and the gear indicator were sprites, the **cockpit bitmap would be fully static — drawn once
and never touched again**. `$7BE2` is the number-one item in the profile at **36.1%** of the frame,
so this aims at the largest single cost the port has.

**Why it is more than "draw it somewhere else".** The instruments are not translating images; their
pixels are a function of a continuous value (wheel angle, rev level). Sprites convert that from
*"redraw the shape every frame"* into *"pick one of N pre-rendered images and set a pointer"* — the
per-frame CPU cost collapses to a pointer write, paid for once in chip RAM at startup. That is the
same trade as `$3980`, the angle-indexed table the wheel drawing already reads (`$5168`); this just
carries it all the way to the hardware.

**Four constraints, and three of them happen to be favourable here:**

- ✅ **Colours fit.** A sprite gives 3 colours + transparent, 15 for an attached pair. The race view
  is **2 bitplanes = 4 colours**, so a single unattached sprite already matches the playfield's whole
  palette. Sprite colours live in entries 16-31, which a 2-bitplane playfield never uses, so there is
  **no palette conflict with the copper's band list** either.
- ✅ **Resolution fits.** The display is **320 px** wide (`bbc_screen.h`), i.e. lores, which is
  exactly sprite resolution — one sprite pixel per display pixel, no halving.
- ✅ **The beam timing is easy for once.** The dashboard is **band 4, display lines 166-208** — the
  bottom of the field — so its sprite data can be updated long after vblank without racing the beam.
  ⚠ But `SPRxPT` *in the copper list* is read at **scanline 16** (CLAUDE.md), so the pointers
  themselves still belong in the VBI; only the sprite data words are late-safe.
- ⚠ **Width is the real limit, and it is unmeasured.** Eight sprites is **128 px per scanline** out
  of 320, and the whole dashboard sits in one 42-line band, so vertical sprite reuse — the usual way
  past the eight-sprite limit — buys nothing here. Whether the wheel rim alone fits inside that
  budget is **the open question**, and it is a pixel-width measurement, not a judgement call.

**What must stay CPU-drawn regardless:** the **wing mirrors**. Their content is the scene behind the
car, not a glyph with N states, so they are rendered output and no sprite can hold them.

**⭐ How this stays a faithful port — the same trick as §5.** Do not redraw the instruments by hand.
**Pre-render every sprite variant by running the game's own drawing code** (`$7BE2`'s wheel/dial
arms) once per angle/level and capturing the pixels it produces. Then the sprite images are
*derived from the oracle* rather than reinterpreted, and the differential is exact: for any state,
sprite output must equal what the decode produces. A hand-drawn wheel that merely looks right is the
failure mode this rule exists to prevent.

**Sequencing — gated behind §6 (user decision, 2026-08-16).** This is a Phase 6 item, *after* direct
bitplane rendering, for the same reason the asm is: both change how the dashboard reaches the screen,
and sprite work written against the current arrangement gets rewritten. It is also gated behind §7's
measurement — if the overlay's existing per-column dirty tests already make the static cockpit nearly
free, then the win here is only the moving instruments, which is a much smaller number than 36.1%.
**Measure §7 first; it is one counter and it sizes this whole item.**
