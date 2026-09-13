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
  scene and `$04D8` (1240 us, 19.4 lines = `update_horizon_band`'s neutral value) in another. ⚠ The
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

### ⛔⛔ 5z. THE GAME READS ITS OWN FRAME BUFFER — a hard constraint on every layout here (2026-09-08)

**The driving model samples two screen bytes as an input.** `update_grip_limits` reads
`surface_change_0` (`$713D`) and `surface_change_1` (`$7205`), and those are not variables: they
are frame-buffer cells on **display line 149** (inside the track band) at MODE 5 pixels 28..31 and
128..131, symmetric about the 160-pixel centre — the picture under the car's left and right.
`$FF` in either opens `grip_disturbance` and can start a spin; `$FF` in both halves the grip base.
Measured on a real BBC over 600 frames with the wheel held over: one-of-two on 27 frames, both on
24 (`symbols.csv` `$713D`).

⇒ Every option in this document that stops maintaining a BBC-shaped frame buffer — direct
plotting (§7f), the trapezoid fill (§9), sprites for anything in the track band (§8) — **must
still leave those two bytes readable with the values a real BBC would have.** A direct plotter
that never materialises line 149 does not merely lose a pixel: it changes the *physics*, silently,
in a way no frame-buffer diff can see because there is no frame buffer left to diff.

⚠ The cheap discharge is a **producer-side probe**: whatever draws that line computes the two
values anyway, so have it write them to `mem[$713D]`/`mem[$7205]` explicitly and keep the read
side untouched. Cheap — but it has to be *designed in*, and it has to be on the list before any
layout is chosen, not discovered afterwards. Note also that the pair is a *pixel* predicate, so a
representation change that alters the plotted bit pattern at those four-pixel cells (a different
dither, a colour remap, sub-pixel rounding) changes the answer even if the picture looks the same.

✅ **The sweep for siblings is done (2026-09-08) and the answer is: these two, and nothing else.**
Every absolute read (`LDA/LDX/LDY/CMP/CPX/CPY/AND/ORA/EOR/BIT/ADC/SBC`, direct and indexed) in
`disasm/listing.txt` targeting the frame buffer, restricted to the **visible lower picture**
(`$6980+`, display line 96 down — everything above that is the code-and-variables the sky hides,
§4), is eight instructions at six addresses:

| Address | Read by | What it is |
|---|---|---|
| `$713D` | `$4BF0`, `$4BF8` | ⛔ `surface_change_0` — **feeds the physics** |
| `$7205` | `$4BF3`, `$4BFF` | ⛔ `surface_change_1` — **feeds the physics** |
| `$6E85` `$6E8A` `$6FB2` `$6FBD` `$6FC0` `$70F8` | `$52BA`-`$52EA`, all `LDA abs,X` | the dial-needle plotter reading the byte it is about to merge into |

The six needle reads are ordinary read-modify-write of the picture: self-consistent, and any
plotter that keeps read-modify-write semantics reproduces them. **Only the two `surface_change`
cells escape the renderer and reach the simulation**, so the constraint above is exactly two
bytes wide — but it is absolute.

⚠ It was found by accident, and it had been recorded as dead code for three weeks. The sweep
above is what turns "assume there are more" into a number; run it again if the screen base or the
band layout ever moves.

### ⭐⭐ 5a. WHAT THE PRODUCER BUFFERS ACTUALLY HOLD (2026-08-17, twins #9-#12)

Before rearranging these buffers, know what is in them, because the previous names said something
false: **`edge_x_lo`/`edge_x_hi` are ANGLES, not screen columns.** `bearing_to_section` (`$2145`) is
an arctan — it divides the smaller camera-relative section delta by the larger, indexes `arctan_table`
(`$6100`, 256 bytes) and adds a quadrant base of `$20`/`$60`/`$A0`/`$E0` — and `emit_edge_bearing`
(`$23C0`) stores `bearing - car_heading` into the array.  So an entry is the point's azimuth relative to where
the car is pointing, and **`interp_edge` (`$2B26`) is the routine that turns an azimuth into a
column**, i.e. it is the perspective seam this plan has to preserve.  Three consequences:

* the geometry/plot split of §5 item 1 falls naturally at `interp_edge`, not further up: everything
  above it is camera-space angles with a pure `mem[]` contract;
* `rebase_edge_point` (`$0BA2`) re-bases the near slots by the same delta that integrates the
  heading, which is why the near six points survive a frame — any rearrangement has to keep that
  incremental path, or the near road gets rebuilt from the section list every frame;
* ⚠⚠ `edge_opp_x_lo`/`edge_opp_x_hi` (`$5E50`/`$5EA0`) — the OPPOSITE road boundary's azimuth per
  point — **deliberately shares bytes with `edge_x_lo`/`edge_x_hi`**: the base is `edge_x` + `$10`,
  and each 40-entry half only ever holds points 6..23, so index *i* lands in the 25..39 slack of the
  same half (settled 2026-08-17; the highest byte written is `$5E8F`/`$5EDF`, and nothing reaches
  `$5EE0`).  A layout change that spreads the two halves apart, widens an entry, or moves either
  base **breaks the alias silently** — `emit_edge_width_offset` writes through one base and
  `mark_line_surfaces` reads through the other, so make the two arrays explicit here rather than
  inheriting the overlap by accident.

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

### ⛔ 7d. THE STORE CENSUS ON A REAL BBC — **§7a and §7b were both reasoning about the wrong routine**

`make fbwrites` (new, 2026-08-16): `tools/bbc_refloop_race.mjs --fill=all` flags every byte of the
frame buffer and records the **PC of whatever writes one**, on a real BBC driving Silverstone.
15 frames, car under power. Two things it settles, and each one retires a conclusion above.

**1. A STORE IS NOT A CHANGE, and every shape number in §7a/§7b measured changes.**

| per frame, real BBC | |
|---|---|
| frame-buffer **stores** | **2991** |
| of those, stores that **changed the byte** | **567 (19%)** |

`src/platform/shape.h` is a snapshot differ, so it can only ever see the second column — right for
pricing a dirty-region *decode* (which is why that shipped and worked), wrong for pricing a direct
*plotter*, whose cost is stores. ⚠ A rasteriser that re-plots an identical span costs full price and
shows up as **nothing**. That is why §7b concluded "the main loop barely draws": it does draw, and
96% of what it draws was already there.

**2. `$7BE2` IS THE 3D VIEW RASTERISER — `view_paint_lines`, and it is LINE-major.** It writes
display lines 80..157, full width (all 40 cells) on 88..111, then tapers as each line's chain stops
at its own cell. ⚠ **Corrected 2026-08-17**: the first reading of this table said "column-major,
per-column silhouette", and the address arithmetic says otherwise — one chain paints ONE SCAN LINE,
forty cells across (`$70/$71` = `$6700` = character row 10 / cell 0 / line 0, `+1` per line, second
pointer at `+256` = cell 32), so the taper is a per-line *horizontal* silhouette, i.e. the edges of
the cockpit opening. The palette band called "the dashboard" starts at line 166 and **nothing writes
166..207 during driving** but the digits. Names and evidence: `disasm/symbols.csv`. Consequences:

- **§7a is void as written.** "A dashboard of mostly-static instruments redrawn wholesale" describes
  nothing that exists; the 2093 units are the *viewport's* cells and the 83 changed bytes are the
  scene actually moving. What survives is the arithmetic: ~420 cycles a unit, instruction-fetch
  bound (`docs/perf-method.md` twin #2).
- **§8 (sprites) is unsized again**, because it was sized off §7a. The instruments are not what
  `$7BE2` spends its time on, so the sprite item can no longer claim any part of that 131 ms. What
  it might still buy has to be re-derived from whatever actually draws the cockpit.
- ⭐ **And the direct-render attach point is now known, which is what §3 was waiting for**: it is
  `$7BE2`'s two column chains, not the 50 Hz body and not `$1A20`. A column chain writing one byte
  per cell down a column is *already* the shape a direct plotter wants — the same walk, two plane
  bytes per source byte, `kRowBytes` apart.

### ✅ 7e. THE LAYOUT DECISION (§3, finally taken) AND WHAT MAKES IT PAY

Three measurements taken together settle it (`make fbwrites`, real BBC, driving):

1. **The visible frame buffer is WRITE-ONLY.** Lines 80..207 are never read back except
   `vdu_char_def`'s **148 reads/frame at lines 129..180** — the MOS character plot's
   read-modify-write for the digits. Everything else that looked like a read was the 6502's dummy
   read inside `STA (zp),Y` (1:1 with the store count, landing on the un-carried neighbour). ⭐ So
   the port may stop maintaining `mem[]` for the sweep's region and plot straight to bitplanes; the
   digits are the one carve-out and they are 148 bytes.
2. **The sweep's store pattern is HORIZONTAL, and that is the whole argument.** A unit steps `d`
   by **+8** — the next *cell of the same scan line* — and the outer loop advances the base by
   **+1** (a scan line), with the `+$138` correction at a character-row crossing. So the BBC's
   layout makes a run of cells **strided by 8** and a step downward contiguous; the Amiga's
   interleaved 2-plane layout makes a run of cells **contiguous** and the step downward
   `+kRowBytes`. The two are exactly transposed.
3. **`A` carries between units**: a cell whose source byte is zero repeats whatever the cell to its
   left drew. With ~83 non-zero sources per frame over ~77 scan lines, **a line is one to three
   RUNS of identical bytes.**

**The decision: keep today's layout exactly** — 320×208, two interleaved planes, `kRowBytes` 80,
plane 1 at +0 and plane 2 at +40, same polarity, same copper, same palette. Nothing about the
display changes; only who writes it. That keeps `decode()` usable as the oracle *unmodified* (§5),
leaves the band list and MODE 7 untouched, and costs nothing — the freedom §3 talks about is real
but there is no evidence any other encoding is cheaper for this access pattern.

⭐⭐ **What pays for the change is (2)+(3), not the deleted decode.** A run of N identical cells is
N identical *contiguous* plane bytes on the Amiga, so it is `N/4` longword stores per plane instead
of N byte stores — and the 6502 could not do that at any price, because in its layout those bytes
are 8 apart. The per-cell loop collapses into a per-run fill: **~2100 iterations become ~150.**
⚠ And the two `$70`/`$72` bases (cells 0..31 and 32..39) collapse into ONE pointer, because
`$6800 = $6700 + 256` is cell 32 of the same line — 40 contiguous bytes on the Amiga.

⚠⚠ **Sizing, honestly, and it is a correction to §6.** Direct plotting no longer deletes 250 ms, or
even 81: the dirty-region decode (§7c) already collapsed that row to **36 ms**, and it skips exactly
the unchanged cells a plotter would also skip. A plotter that merely *mirrors* each store into
bitplanes would be a LOSS — two plane stores plus an address map, to save a decode that is already
nearly free on unchanged cells. **The win has to come from run-collapsing the 131 ms rasteriser**,
with the decode saving as a side effect. Any implementation that does not collapse runs is not
worth building.

### ❌ 7f. BUILT, PROVEN CORRECT, AND **MEASURED A 9% LOSS** — direct plotting is DEAD for this routine

The plotter §7e specified exists, works, and is byte-exact. It is also **slower**, and the reason
is a mistake in §7e's own reasoning that only a measurement could have caught.

| build (same scene, same instrument, `fps_series.gdb`) | vblanks | painted | FPS |
|---|---|---|---|
| shipping (dirty decode, no plotter) | 9782 | 383 | **1.96** |
| `DIRECTPLOT=1` — plot runs *in addition to* the `mem[]` stores | 9777 | 325 | 1.66 |
| `PLOTONLY=1` — plot runs *instead of* them, decode skips the region | 9780 | 350 | **1.79** |
| `PLOTONLY=1` **without** the decode skip | 9778 | 350 | 1.79 |

**Correctness was never the problem.** `DIRECTCHECK=1` brackets each sweep — convert `mem[]` with
the shipping decode, seed the buffer, let the sweep plot over it, convert again, require the WHOLE
16640-byte buffer to match — and it reads **0 mismatches / 9 checks** first try, with a one-cell
sabotage of the flush caught at 210. The shape is real too: **360 runs for 2148 cells, 6.0 cells a
run**, over exactly lines 81..157.

⚠⚠ **THE ERROR: RUNS COLLAPSE THE STORES, NOT THE ITERATIONS.** §7e claimed "~2100 iterations
become ~150". They do not, and they never could: every unit must still **read its own source byte**
out of a `$80`-strided block and test it — that scan *is* the loop, and it is what the 2148
iterations are. What a run collapses is the *store*, which is one instruction of about thirty. So
the change removes a byte store from an **instruction-fetch-bound** loop (twin #2) and adds run
bookkeeping to it, which is a straight loss before anything is gained.

⚠⚠ **AND THE GAIN IT WAS BANKING ON WAS ALREADY SPENT.** The last two rows of the table are the
same number: skipping the decode of the plotted region bought **nothing measurable**, because the
dirty-region decode (§7c) was *already* skipping those cells — with the sweep no longer writing
`mem[]` there, nothing changes there, so the dirty test skips it either way. ⭐ The two ideas were
competing for one prize, and the cheaper one took it three commits earlier.

**What this retires.** Direct-to-bitplane rendering as the port's next lever — the item that opened
this document — is finished on measurement, not on argument. The machinery stays behind its flags
(default off, `DIRECTPLOT` / `PLOTONLY` / `DIRECTCHECK`, and the shipping build re-measures at 1.96
with it compiled out) because the oracle and the plot layer are exactly what a future attack on the
*scan* would need. **What it does not retire** is §8's sprites, which never depended on this — and
the real target it points at: the rasteriser's cost is **2148 source reads a frame**, so the only
thing that can move it is producing fewer sources, i.e. changing what the *producers* write.

### ✅ 7g. THE PER-LINE CENSUS (2026-08-17) — **63% of the scan is on lines that change NOTHING**, and a producer flag can only see half of that

§7f left one lever: the rasteriser's cost is its 2148 source reads, so the only thing that moves it
is running fewer units.  The unit of skipping is a **scan line** — with every source on a line zero,
every cell takes the carried byte, so the line is one flat run of the background byte.  What that is
worth is now measured rather than argued: `make SHAPE=1` + `amiga/view_census.gdb` counts, per line,
the units it ran, its dirty sources at sweep entry, and — the number that matters — the stores that
**changed the byte already there**.

| per sweep, 50 sweeps of a DRIVING Silverstone practice session | |
|---|---|
| lines painted / units run | 77 / 2148 |
| **REDUNDANT — lines where no store changed a byte** | **54 of 77 lines = 63% of the units** |
| CLEAN SOURCES — lines with no dirty source at entry (what a PRODUCER-side dirty flag could see) | 17 of 77 = **33%** |
| lines with a dirty source that still changed nothing | 1872 of 3850 line-visits |

⭐ **The two predicates are not the same, and the gap is the finding.** Half the redundancy comes from
lines whose sources ARE dirty and whose translated byte is the one already on screen — invisible to
any flag the producers could set, and only detectable by a read-compare per cell, which is the cost
being avoided.  So a producer-maintained dirty flag is worth **33% of the scan (~18 ms)**, not 63%.

⚠ The dirt is a horizontal BAND, as §7a said from the other direction: lines $03-$21 and $39-$4F are
redundant in 49 of 50 sweeps, while $27-$38 change in nearly every one.

⚠⚠ **AND THE FIRST READING OF THIS CENSUS WAS 99%, MEASURED ON A PARKED CAR.**  The host's engine
never caught under the autorun script of the day, so from frame 50 on it repainted the same picture
and *every* line read redundant.  The census printed 97%, 98%, 99% as the run got longer, which is
exactly what a converging measurement looks like.  The engine state is now printed beside the numbers
in both the host watcher and the gdb script, because a redundancy census on a static scene is not a
weak measurement — it is a different question with a plausible answer.

✅ **And the host can produce the moving scene now** (`make SHAPE=1 HOLD_THROTTLE=1
STRAIGHT_TO_RACE=1`): `AutoRun` restarts a stalled engine and selects a gear, so the host reads
**69% redundant / 38% clean sources** against the target's 63% / 33% — two backends, independently,
on the same question.  `docs/perf-method.md` §the measurement window.

### ✅ 7h. THE SKIP PREDICATE IS THREE-PART, AND §7g'S ONE-PART VERSION IS UNSOUND (2026-09-11)

§7g sized a producer-side dirty-line flag at 33% of the scan and left it there.  Building it
needed the other half of the question — *is a clean-source line's picture actually unchanged?* —
and the census already had the counter for it (`g_shapeLineCleanButChanged`).  It is **not** zero:
over a long `make SHAPE=1 HOLD_THROTTLE=1 STRAIGHT_TO_RACE=1` run, **78 286 clean-source lines
still had a store that changed a byte**.  A flag-only skip would have left a stale line roughly
every third sweep, and neither `validate` nor a frame-boundary dump would have shown it.

Two causes, both now measured, and each needs its own term in the predicate:

* **26 205 of them (33%) had a MOVED BACKGROUND BYTE.**  A line with no dirty source is not
  "unchanged", it is FLAT: every cell takes `surface_colours[view_line_surface[line] & 3]`, and
  the road pass rewrites `view_line_surface` every frame.  The line's colour can change with no
  source written at all.
* **The other 52 081 were the line's OWN previous paint.**  Sources are consumed and zeroed as
  they are read, so a line that carried road pixels last frame has clean sources this frame — and
  those pixels must still be erased back to the background.  "Clean now" says nothing; the
  predicate needs "clean now AND clean last time".

⭐⭐ **The three-part predicate — no dirty source, background byte unmoved, and the last paint
itself flat — is sound and nearly free of coverage cost:**

| over 4 319 087 line-visits that satisfy it | |
|---|---|
| units it would skip | 172 763 480 = **38% of the scan** |
| of them WRONG (a store would have changed a byte) | **0** |

So the skip is worth **38%** of `view_paint_lines`, which is 36% of the frame — call it ~13% of
the frame — and the term §7g was missing costs nothing, because a line that is clean two sweeps
running with the same background is exactly the static horizon band the redundancy lives in.

⚠ All three terms are cheap PER LINE (one counter, one byte compare, one flag); none of them is a
per-cell test, which is what §7f established cannot pay for itself.  ⚠ The remaining unknown is
the marking side: every writer into the forty source blocks has to set the line's bit, and
"which routines write `$3000..$43CF`" is a question about a large subtree, so it is a measurement
and not a reading.

### ⭐⭐ 7i. THE SKIP IS BUILT, GREEN — AND A NULL RESULT on the target (2026-09-11)

`make VIEWSKIP=1` implements §7h: per-line producer marks, the background byte and the flat bit,
and the sweep skips a line that passes all three.  Straight out of the box it **FAILED
`make determinism`** — 153 stale frame-buffer bytes on display line 87 — and the reason is the
method lesson, not the arithmetic.

⭐⭐ **§7h's census measured a world the skip destroys.**  It ran on a build where every line is
repainted every frame, so "this paint changed nothing" was only ever asked one frame after the
last paint, of a line whose destination had just been written by that same paint.  Turn the skip
on and neither holds: a line can stay unpainted for many frames.  A predicate validated by a
census that repaints is not validated for a build that does not.  ⚠ **This is the "a control the
instrument erases is not a control" failure in its purest form** — the erasure is the skip itself.

Three further parts turned up, in the order the gates found them:

- **(4) A partial paint is not flat, AND it does not clean the line.**  The chain's first line is
  entered at `unit` rather than 0 (phases 2 and 3, and the forced entry), and a planted stop ends
  the sweep on its own unit; either way the units that did not run kept their sources.  The line
  stays dirty in both cases — `g_viewLineDirty` is cleared only on a run that reached unit 40.
- **(5) The destination MOVES.**  A source line's cells are wherever `plot_ptr` has walked to, and
  that walk starts from a `screenBase` the caller chooses, so the shadow cannot be keyed by SOURCE
  line at all.  It is keyed by DISPLAY LINE (`g_viewDstBg[208]` / `g_viewDstFlat[208]`), and the
  sweep's own run-end composite stores clear the flat bit of whatever line they land on.
- **(6) ⚠⚠ THE FIRST SWEEP HAS NO HISTORY, AND A ZERO-INITIALISED ARRAY SAYS THE OPPOSITE.**
  This was the 153 bytes.  `g_viewLineDirty` is a BSS array, so it starts all-zero = "nobody wrote
  these sources" — and the first sweep therefore booked every line it painted as flat, including
  lines whose blocks already held real pixels from before the hooks were live.  Display line 87
  painted `$77`, was recorded flat with background `$0F`, and was skipped for the rest of the run.
  The fixture path had always called `view_skip_reset()`; the GAME path never did.  It does now,
  once, at the first sweep.

- **(7) `copy_dash_data` is a PRODUCER, and it is not a plotter.**  `make determinism-race` then
  failed with 207 stale SOURCE bytes at rows `$4A..$4F` of every column.  The stow direction of
  `copy_dash_data` writes the dash-code tails straight back into the `$80`-spaced blocks with a
  plain `to[y] = from[y]`, so it passes neither `seam_write` nor `plot_store_resync`.  It marks
  now.  ⚠ The §7h producer census had read 0 written-but-unmarked over 26M line-writes and still
  missed it — the census only samples the rectangle between sweeps, and this writer runs once at
  session entry.

**The gates: `make VIEWSKIP=1` passes `determinism`, `-drive`, `-crash`, `-steer`, `-race` and
`make viewdiff` on all five circuits, and `make VIEWSKIP=2` — which asserts, at every skip, that
all forty sources are zero and that the destination already holds the byte — fires zero times on
all five trajectories.**  `make validate` cannot gate any of it (its fixtures write sources with
`fill_random`, behind the marking hooks), so the harness resets the state to "everything dirty"
per case and the differential gates are the only ones.

⭐ **Measured skip rate: 39% of line-visits while DRIVING** (6004 of 15392 over 300 frames,
`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 REVS_FIXED_RNG=1`; 23% parked, which is the wrong workload to
size it by).  That is §7h's 38% prediction confirmed against a build that actually skips.  The
switch prints the ratio itself at exit.

⭐⭐ **AND IT BUYS NOTHING. Measured on the target: a NULL RESULT.**  Both arms
`STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1`, no `PROBES`, `--warp_mode=1`, `fps_series.gdb`, 30 s,
captured minutes apart in one session from clean builds:

| arm | row vector (FPS per 512 vblanks) | non-outlier mean |
|---|---|---|
| control | 4.49 4.58 4.58 4.68 *3.22* 4.49 4.68 4.49 4.68 *3.22* 4.58 4.58 4.58 | **4.583** |
| `VIEWSKIP=1` | 4.49 4.58 4.49 4.68 *3.12* 4.49 4.58 4.58 4.68 *3.02* 4.58 4.58 4.49 | **4.565** |

(The two italic rows in each series are the off-track/reset scene and are excluded.)  **−0.4% —
inside the one-frame-per-row noise floor.**  ⚠ And the skip provably FIRED in the arm that was
measured: reading its own counters off that same build gave **12536 of 31703 line-visits skipped,
39.5%**, matching the host census — so this is not a build that silently did nothing
(`amiga/viewskip_count.gdb`, `VIEWSKIP=1` builds only).

**Deleting 39% of the sweep's line-visits moved the framerate by nothing measurable**, which says
the forty-unit source scan is NOT where `view_paint_lines` spends its time — a skipped line still
pays its `advance_first` bookkeeping and the chain still walks, so what the skip removes is the
cheap half.  This is the third null in a row on this subsystem (§7d's wide values +0.65%, the
direct plotter 9% *slower*, this).  ⚠⚠ **The skip therefore stays OFF by default** — the code is
correct and fully gated, the null is about its VALUE, so nothing is reverted, but a null does not
earn a default.

⭐⭐ **AND THE NULL IS NOW EXPLAINED, STRUCTURALLY — the skip can only reach phase 1.** One
`PROBES=1` profile of each arm (`docs/perf-method.md` §Inside `view_paint_lines`) gives COUNTS, not
timings, so the cross-run caveat does not apply to the load-bearing part:

| units/frame | phase 1 | phase 2 | phase 3 |
|---|---|---|---|
| control | 1441 | 426 | 282 |
| `VIEWSKIP=1` | **619** | **426** | **282** |
| runs/frame | 36 → **15** | 32 → 32 | 50 → 50 |

⚠⚠ **Phases 2 and 3 skip ZERO units and ZERO runs — and they are 47 of the consumer's 70 ms.**
Their lines are the clipped and short lines around the horizon and the road, which always carry
content, so the predicate never holds there. The skip's entire reachable surface is phase 1's flat
full-width background lines: **22 → 15 ms, ~7 ms of a ~290 ms frame = 2.4%**, right at the "under
3% is noise" floor. Nothing had to cancel the win — **the ceiling was below what FPS can resolve,
and that was computable from the phase table before the skip was built.**

⭐⭐ **So stage two is aimed at the wrong two-thirds as well.** The direct plotter collapses the
STORE, and phases 2+3 spend 1100-1200 µs PER LINE on 282 units between them — that is per-line and
per-run DRIVER cost (50 runs over 25 lines in phase 3), which neither a dirty mask nor a faster
store touches. **Do not build stage two on the "the pair pays off together" assumption: the pair
both attack phase 1.** The lever on the expensive two-thirds is the DRIVER — fewer runs per line,
or a cheaper per-run set-up — and that is a different change.

⭐ **The transferable rule: price an optimisation's CEILING against the phase decomposition
first.** "39% of line-visits" sounds like 39% of the sweep and is 2.4% of the frame, because the
visits it deletes are the cheap ones. Multiply the share you can reach by the fraction of it you
can remove and compare the product to the 3% floor — below that, the experiment cannot answer the
question whatever it returns.

⚠⚠ **THE INK WATCH'S POLL MODE NAMES THE OBSERVER, NOT THE WRITER**, and misreading that cost
three wrong fixes.  `[ink] change 1: $6707 $F3 -> $77 (seen from a bus op at $6C6D)` is the
backtrace of whoever happened to be writing when the change was *noticed* — its own header says
poll mode catches writes the seam cannot see.  I read it as naming `paint_lines_clipped` and
chased that routine's composite stores for three attempts.  ⭐ **What settled it in one run was
three lines of code**: a spy on the single stale byte, printing every change of it tagged by
position in the sweep.  `SPY $6707 -> $77 at p1-line-end line $49 seq 15` said the corruption
happened inside the FIRST sweep, in phase 1, painting source line `$49` — which is not a foreign
tenant at all but the chain itself, and pointed straight at the uninitialised array.  When a
single byte is wrong, watch that byte; do not reason from an attribution tool built for a
different question.

### ⭐⭐⭐ 7j. DIRECT COSTS SETTLED (2026-09-12) — two different scans, and two different dirty representations

The conclusions in §7i about the expensive phases being chiefly per-line/per-run driver work are
superseded. That inference came from an underidentified fit: phases 1/2/3 have 1/2/2 runs per line,
so a line coefficient and a run coefficient cannot be separated honestly. A calibrated direct
target bracket now measures the production unit/run interior itself:

| item | target cost/frame | what is inside |
|---|---:|---|
| viewport unit/run interior | **29.0 ms** | 2148 unit visits, 118 runs, 77 lines; source consume/translation/clear, loop/run control and destination stores |
| destination-store portion | **~8 ms** | `NOUNITS=3` retains consume/clear but suppresses the store |
| source/translation/control remainder | **~21 ms** | the part a source-event representation can attack |
| framebuffer decode | **38 ms** | BBC framebuffer/shadow discovery plus dirty-cell bitplane expansion |
| decoder discovery/shadow scan | **~23 ms** | retained by a scan-only temporary build |
| decoder dirty-cell expansion | **~15 ms** | the remainder |

The viewport's current moving arm census is approximately **88% clean, 8% dirty and 3% forced**.
The older 96% figure paired 2093 viewport unit visits with 83 framebuffer bytes whose *value
changed*. Those are different stages and different events; that quotient is invalid. It must not
be used to price a viewport dirty mask.

There are therefore **two useful dirty representations**, at different seams:

1. **Framebuffer dirty maps, one per backbuffer.** Keep two 1040-bit (130-byte) maps, initially all
   dirty. Whenever a specialised framebuffer store genuinely changes a byte, set its cell bit in
   both maps. The decoder iterates and clears only the displayed backbuffer's map. A mode change
   dirties all forty cells of its character row. The existing full decoder remains the oracle.
   Gross ceiling: the measured 23 ms discovery scan; pre-implementation expected net: 10-15 ms.
2. **Viewport source events/runs.** The producers know which of the forty `$80`-spaced source
   blocks they touch. Emit changed units or contiguous runs and let `view_paint_lines` iterate the
   events instead of testing all 2148 slots. A line-only bit is insufficient if it still causes a
   forty-unit scan. Gross source/control surface: ~21 ms; expected net: roughly 12-18 ms. Keep the
   present consumer as the byte-exact oracle until all trajectories agree.

The failed direct plotter did not test either proposition: it replaced the final store while
retaining the source scan and every upstream BBC-shaped representation. Its 9% loss therefore says
that the last-arrow replacement was a bad trade, not that a higher representation seam is valueless.

**Implemented and rejected as the shipping path (2026-09-12).** `make CHANGEDIRTY=1` is the complete
two-map implementation: generator, generic bus, native seam and hand-written framebuffer stores
all compare before storing and set both maps on a genuine change; decode consumes the current
backbuffer's map, and mode motion dirties the whole character row. Its full-decode oracle measured
**0 mismatches / 23 moving-frame checks**, with `cells last=59`, `max=1040` (initial frame), throttle
`$FF`, gear `$2F`, speed `$0C`. Thus the feature ran, did real skipping, and was byte-exact.

It nevertheless loses decisively. Same-session, clean-build `fps_series.gdb` rows were
**4.10-4.19 FPS** for the map (one 4.00 row in the shared-marker rerun) versus **4.49-4.68 FPS** for
the unchanged shadow control: about **-8.5%**. Moving the bit arithmetic into one cold helper did
not recover the loss. The reason is memory traffic: roughly 2991 candidate framebuffer stores per
frame now need an extra destination read/compare, and each genuine change adds two map read/modify/writes.
The old decoder instead batches discovery into aligned, contiguous longword comparisons over 1040
cells. The proposed 23 ms was a gross decoder ceiling that omitted its producer-side price.

`CHANGEDIRTY` therefore stays **OFF by default**, while remaining buildable and oracle-covered so
the result is reproducible. Do not schedule another per-store change-aware map unless the producer
representation itself supplies change events without rereading framebuffer destinations.

The measured implementation order is now a combined native
`interp_edge` + `span_walk` kernel, then viewport source events. Even if all local items land, their
credible total points to about **6-7 FPS from the current ~4.93**, not the 25 FPS floor. The floor
requires the architectural path: world points → native spans/events → Amiga bitplanes, bypassing
split BBC edge records, SMC-style span scratch, forty source blocks, the BBC framebuffer and the
shadow decoder.

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

## 9. ⭐⭐ VECTOR / TRAPEZOID FILL from the edge lists — RELOCATE the seam, don't break it (user, 2026-08-21) — FUTURE, NOT YET MEASURED

**Why this remains the architectural prize.** §7j directly measures **29 ms/frame** inside the
viewport unit/run interior and **38 ms/frame** in the downstream framebuffer decoder. Producer
events and framebuffer dirty maps can remove much of those scans independently, but they still
preserve the chain of BBC-shaped intermediates. Rendering from geometry can retire the entire
chain rather than optimizing one scan at a time. That is the scale required for the 25 FPS floor.

**The idea.** `draw_road` already computes, faithfully, the left/right road-edge x-position per scan
line (the geometry/plot split point §5a identified at `interp_edge`). Tap *that* and paint the scene
as filled regions — sky, grass, road, kerbs — instead of `view_paint_lines`' cell scan:

- **Blitter LINE mode for the SLOPED edges** (line mode's actual strength — §"line mode is the
  blitter's slowest mode" applies only to *horizontal* runs), then **AREA-FILL between them** per
  bitplane. The ~2148-read scan is gone; you iterate a few dozen edge segments.
- **Lower-risk first flavour:** keep per-line spans but **emit them straight from the edge x-coords**
  (~77 lines × a few spans, computed, not scanned). This still deletes the scan while keeping the
  existing mixed edge bytes, so it dodges the sub-pixel-edge problem below.

**⭐ This RELOCATES the seam rather than abandoning validation.** `build_track_geometry` →
`project_point` → `draw_road`'s edge computation stay faithful and `make validate`-able; only the
pixel emission is replaced. The new seam sits at the edge lists — a cleaner, higher intermediate than
the source blocks — and `make refloop` is the backstop for the pixels (§5 item 3). Contrast §7f's
direct-plot, which stayed *below* the seam (byte-exact to the framebuffer) and therefore could only
ever collapse stores, never the scan.

**The honest risks (all reasons to scout before committing):**
- Revs' road **curves and has hills**, so it is not one convex trapezoid — kerbs are thin edge bands
  and a crest can split the road into more fillable regions than a clean quad. How many is a
  measurement of the edge lists, not a guess.
- **Sub-pixel edge x** currently comes from the mixed edge bytes (`colour_pattern_tbl`, 4-px
  granularity). A vector fill snaps to a cell or needs a dithered edge column; the span-emit flavour
  keeps the edge bytes and sidesteps this.
- **Objects/cars/signs** are composited into the same viewport (`plot_view_src_line`,
  `draw_track_object`). They stack naturally on §2's BOB/sprite idea, but the draw order is a
  behavioural delta (§5 item 5) that has to be reproduced.

**De-risking first step (cheap, read-only):** scout `draw_road` / `interp_edge` output — confirm the
edge lists give a clean per-line left/right x and count how many regions the road actually decomposes
into while driving. That measurement decides whether #1 is a trapezoid fill or a messier region
problem, and it costs nothing but a read.

**Sequencing.** The §7i dirty-line skip measured a null and stays off. First build §7j's exact
per-backbuffer dirty map and the combined native road-span kernel, because each is independently
measurable and preserves an oracle. Treat the geometry-to-bitplane path as the larger successor,
not as an accumulation of local consumer rewrites; it is the natural home for the blitter once it
earns its place.
