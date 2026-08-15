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

## 4. The constraint that cannot be designed away — and a free win hiding in it

⚠⚠ **THE FRAME BUFFER IS NOT AN OUTPUT BUFFER.** `$5E40-$66FF` — 5.5 KB of engine variables *and*
462 disassembled instructions — sits **inside** the BBC frame buffer and is on display under
palette band 1, where all sixteen entries are the same blue, so the running program renders as flat
sky (`bbc_screen.h` §THE SKY IS A HIDING PLACE FOR LIVE CODE). Nothing clears it and nothing can: it
is the program. So `mem[]` keeps that role whatever this plan does — only the **drawing** moves.

⭐ Which hands over a cheap, independent win, available today with no architecture change: **the
decode does not need to convert the sky band at all.** It renders flat blue whatever the bytes say,
so those ~63 of 208 display lines (~30% of the pass, ~75 ms) can be skipped for a boundary check.
⚠ The band boundary can cut mid-character-row and the boundary moves with the hills, so take it
from `m_plan`/`m_lineMode` — which `decode()` already builds — and not from a literal.

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
