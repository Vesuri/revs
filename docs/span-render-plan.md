# Rendering DIRECT to bitplanes — the Phase 6 lever the plan was missing

> ## ⭐⭐⭐ READ THIS FIRST — THE PLAN AND ITS STATUS (single source of truth, 2026-09-14)
>
> **THE PLAN is §10** — *the replacement architecture: world points → spans → bitplanes* (user
> directive, committed `ab9724c`). It **deletes** the whole `producer → BBC-framebuffer → decode`
> chain and renders the game's own ~400-byte analytic scene (`surface_edge_0..3` + one colour per
> line) straight to bitplanes as ≤5 longword-filled spans per line. Stages A/B/C/D, sized in §10e at
> ~181 → **~48 ms** of per-frame work.
>
> **THE STATUS, unambiguously:**
> - **Step 1 (`make SPANEMIT=1`) is BUILT and BYTE-EXACT** (oracle green, §10k). It is a
>   **correctness scaffold, not the architecture** — it runs the *entire existing* `view_paint_lines`
>   sweep at full cost and merely *adds* bitplane plotting on top, removing only the mem[] store on
>   full lines. It is **additive by construction.**
> - **Step 1 measured +54 ms** (§10L). ⭐ **This is EXPECTED and does NOT condemn §10.** Adding
>   plotting to a pipeline you have not deleted must cost more. The +54 ms tests the scaffold, not
>   the replacement.
> - **The §10 architecture is NOT closed — it is UNPROVEN.** No build yet *deletes* `view_paint_lines`
>   (58.7 ms) and renders from the analytic scene instead; that is Stage A/B, unbuilt. The only valid
>   test is a full-line renderer that **replaces** the sweep for its lines, never one that adds to it.
> - ⚠ Genuine risk remains (§10e's own warning): this project's record with cycle models at this seam
>   is poor, and three schemes that *bolted onto* the mem[] scan all lost (a mirror-each-store plotter
>   −9%, the source-event consumer +25 ms, this scaffold +54 ms). The architecture is different in
>   kind — it removes the scan rather than adding to it — but "different in kind" is an argument, not
>   yet a measurement.
>
> **THE EARLIER PLAN lives in `docs/direct-bitplane-plan.md`, now marked OBSOLETE.** It was a
> different approach — keep the mem[] framebuffer, mirror stores into bitplanes and/or skip the
> decode, with writer-maintained dirty maps — and it did not work (three measured dead ends). Its
> baselines were from a ~1282 ms-frame era and are meaningless now. It is kept only because shipped
> source and other docs still cite its §1–§9 findings (the layout, several shipped optimisations); do
> **not** follow it as a plan. Every live fact it held (the layout, the "game reads its own
> framebuffer" constraint, the sky-band hazard, the three nulls) is restated below where it is used.
> **This document — `docs/span-render-plan.md` — is the only live rendering plan. Follow only this.**
>
> ---
>
> **Origin (kept for context).** The plan began 2026-08-16 after the user pointed out that the Phase 6
> target list (`docs/phases.md`) priced hand-asm on the hot functions and never questioned the
> arrangement those functions render *into* — the port draws the way the BBC drew, into a BBC-shaped
> buffer, then pays a whole extra pass to turn that into something an Amiga can display. ⚑ **The
> predecessor project shipped a change of this kind and measured it** (`~/Documents/Rescue on
> Fractalus`, `docs/terrain-render-plan.md` + `docs/flight-perf-log.md`).

---

## 10. ⭐⭐⭐ THE REPLACEMENT ARCHITECTURE — world points → spans → bitplanes (user directive, 2026-09-14)

> **The directive.** *"Come up with a plan that allows these problems to be really solved. Remember;
> we are not here to verify faithful implementation details but faithfulness from the user point of
> view. The original way of doing things should go if it can't be written to perform adequately. You
> are allowed to rethink the entire rendering pipeline to make efficient use of the 68000 and Amiga
> architecture. Create an architecture that efficiently writes direct to bitplanes without all the
> memory access the current method adds. Get rid of the 6502 originated self modifying code
> emulation. Do this stuff properly validating the outcome, not the details."*

The plan called for a read-only scout before committing to this. **It has been run** (`src/platform/shape.cpp`
§THE SPAN CENSUS, `make SHAPE=1` + `REVS_SHAPE_WATCH=N`), and it does not merely de-risk the idea —
it changes the shape of the answer. Everything below is sized on it.

### 10a. ⭐⭐⭐ WHAT THE CENSUS SETTLED — the pipeline moves ~500 bytes of information and spends 145 ms

Per frame, driving, measured (practice and the race proper agree within 4%):

| quantity | measured |
|---|---|
| source-block cells written by ALL producers | **161** (`draw_road` 77, objects 13 — **30** in the race, dash-edge fill 65, other 6) |
| the edge record that describes the whole scene | **4 × 80 bytes** (`surface_edge_0..3`) + one colour per line |
| framebuffer cell stores the sweep expands that into | **2155** |
| plane bytes `decode()` then expands THOSE into | **16 640** |
| **cost of the expansion** | **85.1 ms** (sweep 58.7 + decode 26.4) |
| cost of producing the 161 cells + the edge record | **70.8 ms** (`build_track_geometry` 26.7 + `draw_road` 33.7 + dash edges 10.4) |

⭐ **And the 2155 stores are a SPAN LIST, not a bitmap:**

- **144 solid runs averaging 13.9 cells** (111 Amiga pixels) — 2003 of 2155 stored bytes, **93%**,
  and every one of them is one of the **four** solid MODE 5 values (`$00`→0, `$0F`→1, `$F0`→2,
  `$FF`→3; a MODE 5 byte is four bit-interleaved 2-bit pixels, so exactly four values paint a cell
  in one colour).
- **146 mixed runs averaging 1.04 cells** — the colour boundaries, *individual cells*, not runs.
- **3.75 runs per painted line, and 93% of line paints have ≤5 runs.**

The raster map says where, and it is the decisive table:

| display rows | what the sweep does there | cells | **runs** |
|---|---|---|---|
| 0–80 | **nothing** — MODE 4 text + the sky band (which hides 5.5 KB of live code) | 0 | 0 |
| 81–100 | 40 cells in **ONE run** each — the horizon | 800 (**37%**) | **20** |
| 101–116 | 40 cells in 4–5 runs | 640 | ~72 |
| 117–157 | narrowing 27→2, **outer cells only** | ~650 | ~145 |
| 158–207 | **nothing** — the dashboard, owned by other code | 0 | 0 |

⭐ **Rows 117–157 paint only the outer cells because `paint_lines_short` runs two chains — A inward
from the left, B inward from the right — each stopping on a planted cell. The original never
repaints the large uniform middle.** That is not an accident to preserve blindly: it is evidence
that the author also saw the scene as spans.

⭐⭐ **`surface_colour_at` ($1E9E) IS ALREADY A VECTOR RENDERER.** Given a scan line and a
position it compares against all four `surface_edge` buffers and returns the surface's colour from
`surface_colours` ($38FC). So the game already maintains a complete analytic description of the
road scene in ~400 bytes, and **everything downstream is expansion of it.** The architecture below
is not a speculative redesign — it evaluates a function the game already has, **once per REGION
instead of once per CELL.**

⭐ **The model and the measurement agree exactly, which is the reason to trust it.** Four boundaries
per line, sorted, give ≤5 spans; the census independently measured ≤5 runs on 93% of line paints.

### 10b. The architecture

**One representation replaces three.** Today: edge record → 40 `$80`-spaced source blocks → BBC
framebuffer → bitplanes. Proposed:

```
  build_track_geometry      world -> camera azimuths          (kept, faithful, validated)
  road_edges                azimuth -> surface_edge_0..3[line] + view_line_surface[line]
                            (Stage B: the SAME output, a native DDA instead of the SMC chain)
            |
            |   per display row: 4 boundary columns + a background colour
            v
  span_emit                 sorted boundaries -> <=5 spans -> LONGWORD FILL, 2 planes
  edge_merge                <=2 boundary cells per row     -> masked RMW, 2 planes
  object_layer              ~30 sparse cells               -> masked RMW, 2 planes
  physics_tap               2 bytes -> mem[$713D], mem[$7205]   (designed in, not discovered — see hazard 1)
```

**Deleted outright:** the 40 source blocks as a rendering intermediate, the BBC framebuffer as a
rendering intermediate, `RevsScreen::decode()` from the shipping path, `view_paint_lines` and its
unrolled chains, `fill_dash_edge_columns`, and `view_plant`/`view_move_stop` — the consumer's own
SMC emulation, which exists only to plant stops into an unrolled store chain.

**The renderer is stateless.** It repaints rows 81–157 in full every frame, so nothing depends on
what a previous frame left behind. Full coverage is 77 × 40 × 2 = **6160 bytes = 1540 longwords**,
only 1.4× the 4310 plane-bytes the game's own 2155 cells occupy — so statelessness costs ~1 ms and
buys the deletion of every dirty/skip question at this seam. ⭐ Given that three consecutive skip
schemes nulled here (the per-line skip, the direct-plot cost study, and the `CHANGEDIRTY` map), **buying the problem out is the result
the measurements point to.**

### 10c. Why this is not the mirror-each-store plotter, which was built and lost 9%

That plotter stayed **below** the seam: it wrote the same picture but still had to read each unit's own
source byte out of a `$80`-strided block and test it — *that scan was the loop*. **This never reads
a source block at all.** It also never iterates a sparse set of the present layout, which is the
identified common cause of all three nulls. The work it deletes is not stores — stores are
nearly free at 43 cyc/cell — it is the **scan, the addressing, and two whole intermediate
representations.**

### 10d. ⭐⭐ THE ADDRESSING, which is where this win can be spent instead of banked

The index→pointer finding (`N = 36.5 + 35.3·E`) is binding, and the chosen layout (§10d) satisfies
it **by construction** rather than by care:

- `kRowBytes` = 80, plane 1 at +0, plane 2 at **+40** ⇒ the two planes' same-position bytes are a
  fixed **40-byte displacement** apart. **One pointer serves both planes**: `move.l d0,(a0)` /
  `move.l d1,40(a0)` / `addq.l #4,a0`. No second addressing chain, no second pointer to advance —
  which is exactly the failure the layout note warned about ("an implementation that keeps two
  independent plane pointers can spend the entire win on addressing").
- Down one row is `addq.w #80,a0`. Across a span is `addq.l #4,a0`. **Every address in the renderer
  is incremental**; nothing is computed from an index.
- A BBC cell = 4 BBC px = 8 Amiga px = **exactly 1 byte per plane**, so a span of N cells is N
  bytes per plane and the four solid colours are a **4-entry table of longword pairs**
  (`$00000000`/`$FFFFFFFF` per plane). Colour selection is one table read, not a computation.
- A boundary inside a cell lands on a 2-bit Amiga boundary, so the edge masks are the four values
  `$C0`/`$F0`/`$FC`/`$FF` — and they are **derived from the game's own `view_compose(src, mask,
  fill)` tables**, not reinvented.

### 10e. Sizing, in milliseconds

Against the **~181 ms of per-frame work** (the 205.5 ms bracketed frame less the 13.5 ms 50 Hz drain
and the 11.0 ms vblank pad). Floor 40 ms, target 20 ms.

| stage | what | now | after | basis |
|---|---|---|---|---|
| **A** | `view_paint_lines` + `decode()` → span renderer | **85.1** | **~8** | 1540 longword fills (18.5k cyc) + 144 spans × ~100 cyc (14.4k) + 146 edge merges × 60 (8.8k) + 77 rows × ~150 (11.5k) + objects (1.8k) ≈ 55k cyc |
| **B** | `draw_road` → native DDA writing the same 4 edge arrays | **33.7** | **~3** | 43 spans/frame, ~600 DDA steps × 25 cyc + 24 perspective divides × 140 + 80 lines × 40 ≈ 21k cyc |
| **C** | `fill_dash_edge_columns` — **deleted, not optimised** | **10.4** | **0** | its output is source bytes for a sweep that no longer exists, plus mask tables the renderer derives itself |
| **D** | `build_track_geometry` — wide-value math, native divide | **26.7** | **~12** | real game math; the least certain row |
| **E** | the dashboard + dial needle (§10m sprites) | in "other" | **not sized** | see 10h |
| | everything else (sound, physics, front end) | ~25.1 | ~25.1 | untouched by this plan |
| | **per-frame work** | **~181** | **~48** | |

⚠⚠ **These are cycle models, and this project's record with cycle models at this seam is bad** —
the source-event consumer was predicted to win and cost **+25.46 ms**. Two things are different and
one of them is checkable: that scheme *added* per-item addressing to delete per-item stores, whereas
this deletes a representation and its addressing is incremental by construction (10d) — and the
honest calibration from the shipped `SpanStep` work is that **a byte-traffic deletion pays about its
own count, ~10 µs per per-span round trip, no more.** So the plan does not ask for trust:

⭐⭐ **THE CHEAP CHECKPOINT, and it is unusually cheap.** Rows **81–100 are 20 single-run rows
carrying 37% of the sweep's stores.** A direct span emitter for *those rows only* is ~50 lines of
code, needs no edge merging (one run per row, full width), and its share is large enough to measure
against the phase table. **Build that first and measure it before writing the rest.** If 37% of the
sweep's stores do not come off phase 1's bracket roughly in proportion, the model is wrong and
almost nothing has been spent finding out.

> ⛔ **BUILT, MEASURED, AND THIS ROW OF THE TABLE IS WRONG — see §10n.** The stores did come off
> (58% of phase 1's units, at 42 cyc/unit, matching the objdump to 2%) and bought only 32% of phase 1's
> bracket, because **stage A above is sized as "stores deleted" when the bracket is driver + stores and
> a hook-in reaches only the stores.** `NOUNITS=2` had already measured the sweep at **61%
> driver/entry** when this table was written and the table did not apply it. ⇒ **Size a hook-in against
> the differential for the part it can actually delete, never against a census of stores** — and note
> that stage A's ~8 ms remains reachable only by the §10p *takeover*, which deletes the driver too.

⚠ Stage D's ~12 ms is the softest number here and it is also the one that **does not** need to be
believed to justify starting: A+B+C alone take ~181 → ~63 ms.

### 10f. ⭐⭐ THE SMC ACCOUNTING — all 20 sites, and which stage retires each

`SMC_SITES` holds **20 sites** (9 opcode, 6 operand, 5 branch), and they are not scattered — 14 of
them are in `draw_road`'s tree and 13 in the span rasteriser alone:

| stage | sites retired | what they are |
|---|---|---|
| **B** | `$2F4E` `$2F90` (dest operands, naming one of the four `surface_edge` buffers) · `$2F18` `$2F47` `$2F60` `$2F89` `$2FA2` (Y-step INY/DEY/NOP) · `$2FC0` `$2FD7` (end marker CPX/RTS) · `$2D27` `$2DAA` `$2E2E` `$2EA7` (computed entry offsets into the unrolled chain) · `$196F` (`fill_line_attr`) | **14** — a native DDA needs none of them: the step is a variable, the destination a pointer, the end a loop bound |
| **C** | `$1DD4` `$1DDB` `$1DDD` (`column_gap_walk` / `fill_column_gaps` patched operands) | **3** — deleted with the pass |
| **D** | `$23B2`/`$23B3` `smc_stale_horizon_cap` | **1** — ⚠ the per-circuit one, patched at RUN time by every expansion circuit's `$5672` hook and invisible to `make track-smc` |
| **E** | `$5220` `$529B` (`smc_major_step`/`smc_minor_step`, the dial needle's octant DDA) | **2** |

⇒ **Stages A+B+C retire 17 of the 20 sites**, plus — and this is the larger deletion — the
consumer's own SMC emulation: `view_plant`/`view_move_stop`, the `$7C`/`$7D`/`$7E` chain pages, and
the dashcode overlay's **40 opcode slots + 12 computed operands** steering two unrolled column
chains (`DASH_CHAINS` in `tools/transpile.py`). Stage E retires the remaining dashboard SMC.

⭐ The pattern is already shipped and calibrated: the span Y-step and end marker became
`SpanStep g_spanStepIn/g_spanStepOut` + `g_spanMarkOn`, for −0.40 ms on `draw_road`. **Doing that
site by site pays about its own byte count.** The architecture is what makes the sites *not exist*.

### 10g. The hazards, each with its discharge designed in

1. **⛔ The two physics bytes — the absolute constraint.** `update_grip_limits` reads
   `mem[$713D]` and `mem[$7205]`: framebuffer cells on **display row 149, cells 7 and 32** (arithmetic
   confirmed: `$713D − $5A80 = 5821 → row 18×8+5 = 149, cell 7`; `$7205 → row 149, cell 32`),
   symmetric 12.5 cells either side of centre. `$FF` in either opens `grip_disturbance`.
   **Discharge:** row 149 is inside the band the renderer owns and its sorted boundary list is in
   hand when it paints — so the renderer **composes those two bytes exactly as the sweep did and
   stores them to `mem[]`**, leaving the read side untouched and byte-exact. Two bytes, ~50 cycles.
   ⚠ It is a *pixel* predicate: any change to dither, colour mapping or rounding at those two cells
   changes the physics, so they are a differential fixture in their own right, not a comment.
2. **The other framebuffer readers are self-consistent.** the framebuffer-reader sweep is done and the answer is
   exactly two bytes: the six dial-needle reads (`$6E85` `$6E8A` `$6FB2` `$6FBD` `$6FC0` `$70F8`) are
   the needle plotter's own read-modify-write, and `vdu_char_def` ($5092, OSWORD 10 + OSWRCH) merges
   glyphs the same way. **Any plotter that keeps RMW semantics reproduces them** — on bitplanes that
   is a read, an `or`, a write, per plane.
3. **⭐ The code under the sky stops being a hazard.** Rows 0–80 hold 5.5 KB of live code and
   variables at `$5E40-$66FF` *inside* the framebuffer's address range. The census confirms the sweep
   never touches those rows. Once the picture lives in **Amiga** bitplanes and `mem[]` keeps only the
   engine's memory, the aliasing **disappears by construction**, and it
   also explains why the existing per-phase framebuffer instrument reports phases 5/10/11/13 "writing
   lines 24..55": those are variable writes, not pixels. ⚠ Do not read that instrument as a picture.
4. **⚠⚠ The `edge_opp_x` alias.** `edge_opp_x_lo`/`_hi` (`$5E50`/`$5EA0`) deliberately share bytes
   with `edge_x_lo`/`_hi` at base+`$10`; each 40-entry half only holds points 6..23, so index *i*
   lands in the 25..39 slack. Spreading the halves, widening an entry or moving either base breaks it
   **silently**. Stage B consumes these and must not relocate them; Stage D must treat the alias as a
   fixture invariant.
5. **⚠⚠ The per-circuit hooks, and why Stage D is LAST.** `region_23d8` — `road_edge_walk`'s body,
   dead on Silverstone because the twin runs the whole walk — is **re-entered at `$2490` by every
   expansion circuit's hook**, so the rest of that walk runs transliterated over `mem[]` cells the
   twin's path no longer uses. `$23B2` is patched at run time by the same hooks. A `region_*`/`FUN_*`
   name is shipping code until proven otherwise and **"proven" cannot come from a Silverstone run.**
   ⇒ Stages A–C do not touch that tree at all; Stage D does, and its gate is `make viewdiff` per
   circuit, not `determinism`.
6. **Reader audit owed before Stage C.** `fill_dash_edge_columns` writes `view_left_start_src`
   (`$0504`) / `view_right_start_src` (`$4400`) and the per-line mask/fill tables as well as source
   bytes. Deleting the pass needs the written reader audit the §RESULTS rule requires — the readers
   include the transliteration a track hook re-enters.
7. **Draw order is a behavioural delta.** Objects composite over the road today via the shared source
   blocks; a span fill followed by an object layer reorders road-vs-object. The RoF precedent is that
   this is safe (objects only *read* the silhouette) but it is **not mem-diffable** — it is a
   `make viewdiff` question.

### 10h. What is NOT sized, honestly

**The dashboard.** §10m and `shape.h` both call `$7BE2` "the dashboard sweep" and attach **36.1% of the
frame** to it. ⚠⚠ **The raster map shows `view_paint_lines` painting rows 81–157 and never rows
158–207, so that 36.1% is the VIEW sweep, and §10m's sprite case is attached to the wrong routine.**
The instruments in hand cannot size the real dashboard cost: the span census hooks only the view
path, and the per-phase framebuffer diff is confounded by hazard 3 above. ⇒ **§10m must be re-sized
before it is scheduled**, by a census of the writers of rows 158–207 (`make fbwrites` attributes
every store to its PC on a real BBC, which is the instrument that settles it). This does not block
Stages A–D; it does mean the "~25.1 ms other" row is unanalysed and the route from ~48 ms to the
40 ms floor runs through it.

**The 20 ms target is not reached by this plan.** A+B+C+D lands at ~48 ms of per-frame work; the
floor is 40 and the target 20. Stage E plus the remaining geometry is where the rest would have to
come from, and nothing measured so far says it is there.

### 10i. ⭐⭐ Validation — the OUTCOME, and the oracle that survives the change

The seam moves **up**, from source bytes to the per-line edge record, and every gate below validates
a *result*:

1. ⭐⭐⭐ **`decode()` is retained as a byte-exact ORACLE, not shipped.** The layout decision (§10d)
   was taken precisely so this works: build both paths, let the existing pipeline paint the BBC
   framebuffer and `decode()` it, run the span renderer into a second pair of planes, and **compare
   the planes byte for byte.** That is an outcome comparison — same pixels, different route — and it
   needs no agreement about intermediates. It is also how the colour→longword table and the edge
   masks get **derived from the oracle** rather than reinvented (the §10m rule: a hand-built table that
   merely looks right is the failure mode).
2. **`make viewdiff` per circuit is the primary gate** — every circuit's race view against a real
   BBC over display lines 82..166. It is the only check that covers an expansion circuit's patched
   arm, and hazards 5 and 7 are answerable by nothing else.
3. **`make refloop` is the visual ground truth** for any "faithful or port bug?" question.
4. **The five `determinism*` trajectories** (parked, drive, crash, steer, race) are the backstop that
   the *simulation* did not move — which is exactly what hazard 1 threatens, and the reason the
   physics tap is a fixture.
5. **`make transtrap`** must stay green: retiring the SMC sites removes transliterated bodies, and a
   body no scenario drives is unproven, not dead.
6. **`make tracks` / `make track-run`** behind those: the bytes land, and the circuit's own code runs.
7. **The span census itself becomes a regression instrument** — it counts the span list the renderer
   must emit, so it is the cheapest check that a rewrite still decomposes the scene the same way.

⭐ What is deliberately **not** validated: the 40 source blocks' contents, the BBC framebuffer's
contents, `view_plant`'s planted stops, and every scratch cell the unrolled chains use as working
notes. Those are implementation details of a 6502 renderer, and reproducing them is the byte traffic
this plan exists to delete.

### 10j. Sequencing, and the decision points

```
  0. re-run the span census                                      (done — 10a)
  1. ⭐ the flat-line span emitter, A/B against decode()          (BUILT, ORACLE GREEN — 10k)
  2. Stage A in full: span emit + edge merge + object layer + physics tap
  3. Stage B: native DDA producing surface_edge_0..3 (retires 14 SMC sites)
  4. Stage C: delete fill_dash_edge_columns (reader audit first)
  5. re-size §10m via make fbwrites, then Stage E / Stage D as the numbers direct
```

Steps 1–2 are self-contained, touch no producer, and cannot break an expansion circuit's patched arm
(hazard 5), which is why they go first. Step 3 changes a producer but keeps its output format
byte-identical, so `make validate` still applies to it. Step 4 deletes a pass and needs the audit.
Step 5 is gated on a measurement that does not exist yet.

⚠⚠ **LESSON FROM STEP 1 AS BUILT (2026-09-14, §10L):** the flat-line emitter was built as a scaffold
that *adds* plotting while `view_paint_lines` keeps running in full — additive by construction, so it
measured +54 ms and taught nothing about the architecture. **The measuring version of step 1 must
*replace* the sweep for the lines it emits, not run beside it** (stop `view_paint_lines` painting those
lines). Only a build that deletes sweep work can show whether the ~8 ms span-render estimate holds. The
green oracle is reusable; the timing build is not the right experiment until it subtracts sweep cost.

**✅ STEERED (user, 2026-09-14) — all four answered, so these are decisions, not options:**

- ✅ **STATELESS, and for a stronger reason than the ~1 ms.** *"Keeping track of the previous frame
  costs way more than it does to render everything, especially if the blitter gets involved."* ⇒ the
  renderer owns rows 81..157 unconditionally; **no dirty map, no persistence, no previous-frame
  state at this seam, and none is to be proposed again** — that is now four measurements and a
  decision pointing the same way (the per-line skip, the direct-plot cost study, `CHANGEDIRTY`, and the statelessness costing).
- ✅ **Stage D waits, but is not dropped.** `build_track_geometry` *"needs to be improved to the
  maximum extent but can wait for the bigger improvements unless those bigger improvements need
  it."* ⇒ A, B, C first; pull D forward only if one of them turns out to need it.
- ✅ **~48 ms against a 20 ms target is accepted as the outcome of THIS rewrite.** *"Let's see how
  far we get with this rewrite. There are still plenty of levers to pull after that."* ⇒ do not
  narrow the scope of A–C to chase the target, and do not treat missing 20 ms as a failure of the
  architecture.
- ✅ **CPU first; the blitter is evaluated, not assumed.** ⭐⭐ And the reason to be sceptical of it
  is the opposite of the usual one (user): **with a faster processor the blitter easily becomes the
  BOTTLENECK** — it is a fixed-rate DMA engine, so an A500 68000 that has been made to wait less
  starts waiting on the blitter instead. So a blitter fill is a candidate to be *measured against*
  the 1540-longword CPU fill, never a default because "the blitter is free". Sprites belong to the
  re-sized §10m.

### 10k. ✅ STEP 1 IS BUILT AND BYTE-EXACT (`make SPANEMIT=1`, 2026-09-14)

`revs_native.c` §THE SPAN EMITTER, in `paint_cells`'s per-line entry. A scan line that no producer
wrote and that carries no planted stop is one run of its background byte
(`surface_colours[view_line_surface[line] & 3]`), so it goes out as a single `revs_plot_run` into
the bitplanes and the forty-unit chain is skipped.

**The oracle (`SPANEMIT=1 SPANVERIFY=1 DIRECTCHECK=1`, driving, `$63 = $0B`):**

```
SPAN EMIT: lines emitted as one span 306   lines that ran the chain 838
plot: runs=7627 cells=45108   last sweep: 346 runs / 2148 cells   lines 81..157
ORACLE checks=21 mismatch=0                          <- byte-exact over the WHOLE buffer
```

⭐ **The census predicted the emit rate before the code existed**: ~20 of 77 painted lines = 26%,
measured **306/1144 = 26.7%**. And the last sweep's 2148 plotted cells over lines 81..157
reproduce the census's 2155 over rows 81..157 from a completely different instrument.

⭐⭐ **The predicate is strictly weaker than the ⛔ skip's, and that is the architectural result.**
The skip needed parts (2)-(4) — a destination shadow keyed by display line, proving the cells
already held the byte it declined to write, the half that took a 153-stale-byte bug to get right.
The emitter needs part (1) alone, because it writes the pixels. *Writing is cheaper than
remembering* (§10j), and here that is not a cost argument but a **correctness** argument: the
fragile half of the skip simply does not exist in an emitter.

⚠⚠ **TWO WAYS THIS ORACLE READS GREEN WHILE PROVING NOTHING, both hit on the first run:**
1. **Suppress the chain's run accumulator globally** and the chain's own pixels never reach the
   planes — the check then compares `decode(new mem[])` against `decode(old mem[])` and mismatched
   **5950** bytes. A check must reproduce *everything* the sweep changed, not just the part under
   test.
2. **Leave the accumulator on** and the chain re-plots the same forty cells straight over the
   span, so a **wrong span compares equal**. It has to be suppressed per LINE, on exactly the
   lines the emitter painted. Plain `SPANEMIT=1 DIRECTCHECK=1` is worthless for the third reason:
   `PLOT_ONLY`'s carve-out leaves `mem[]` stale, so the reference is last frame's picture.

⚠ And the first build **emitted zero spans** — `view_skip_reset()` primes every line dirty and the
only code that cleared a line again was inside the `VIEWSKIP` block, so with `SPANEMIT` alone the
predicate could never become true. The `g_spanEmitLines`/`g_spanEmitPaints` pair is what caught it:
a build that emits nothing and an emitter that buys nothing read identically without it.

ℹ `g_plotNoTarget` reads 1788 and is benign: `revs_plot_run` returns before touching
`g_plotLineLo/Hi`, so a sweep with no bitplane buffer publishes no carve-out and the decode
converts everything. No-target ⇒ no skipped decode, by construction.

**✅ MEASURED — SEE §10L: the scaffold is +54 ms, which is EXPECTED and does NOT close §10.** The
timing build was `SPANEMIT=1 PROBES=1` (no verify, so `PLOT_ONLY`'s carve-out is live) against a
`PROBES=1` control from a clean tree, read on phase 24's row with `phase4_prof.gdb`. ⚠ **What this
build is matters more than the number:** it still runs the *entire* `view_paint_lines` sweep and only
*adds* plotting — it is not the §10 renderer, which *deletes* the sweep. So its +54 ms measures an
additive scaffold, not the architecture. Nothing about the architecture should be believed from the
green oracle alone (it proves the emitter is *correct*, not *fast*) — and nothing about it should be
*disbelieved* from this scaffold's cost either.

### 10L. ⚠ THE SCAFFOLD (STEP 1) MEASURED +54 ms — WHICH IS EXPECTED, AND DOES NOT CLOSE §10 (2026-09-14)

> ⚠⚠ **RETRACTION (2026-09-14).** An earlier version of this section concluded from this measurement
> that "the direct-bitplane premise is stale" and "the ceiling on the whole architecture is ~3–4 ms",
> and closed the architecture. **That conclusion was wrong** and is withdrawn. It measured an
> *additive scaffold* and reasoned as if it had measured the *replacement*. The correction is below.
> (The flawed verdict was committed at `eee1c8c`; this supersedes it.)

`SPANEMIT=1 PROBES=1` vs a clean `PROBES=1` control, same session, warp, 30 s, driving
(`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 FIXED_RNG=1`), `phase4_prof.gdb`. ms/frame:

| phase | what | control | span | Δ |
|---|---|---|---|---|
| 24 | view P1 ($7BE2), 36 lines | 15 | 32 | **+17** (units 1440 → 604: emitter deletes 836) |
| 33 | view P2 ($7D13), 16 lines | 12 | 27 | **+15** (units 426 → 426, *unchanged*) |
| 34 | view P3 ($7F18), 25 lines | 23 | 48 | **+25** (units 282 → 282, *unchanged*) |
| 27 | **DECODE** (carve-out) | 26 | 23 | **−3** |

**Sweep +57 ms, decode −3 ms, net +54 ms.** These numbers are SOUND. The mistake was the inference
drawn from them.

**⭐⭐⭐ WHAT THIS BUILD ACTUALLY IS, and why +54 ms is the expected sign.** Step 1 is a **correctness
scaffold bolted onto the existing pipeline**, not the §10 renderer:

- it still runs **every producer** (`build_track_geometry`, `draw_road`) unchanged;
- it still runs the **entire `view_paint_lines` sweep** — phases 33/34 show units *unchanged* at
  426/282 and the ms *up*, because the sweep does all its old work **plus** the added plotting;
- it merely **adds** `revs_plot_run` bitplane writes and removes only the mem[] byte store on full
  lines.

So the build's cost is `view_paint_lines (unchanged, 58.7) + plotting (added) − decode road (−3)`. It
is **additive by construction**, and an additive build must be slower. **+54 ms is what "add plotting
to a pipeline you did not delete" costs — it is not a measurement of "delete the pipeline and render
from the analytic scene", which is what §10 proposes and which no build here has done.**

**⭐⭐⭐ WHY THE OLD "~3–4 ms ceiling" ARGUMENT WAS WRONG.** It framed the win as "what the decode
carve-out gives back" (~3–4 ms, decode's road cost) and said a direct plotter must redo that same
expansion for no more than that. That accounting is correct **only for the scaffold**, in which
`view_paint_lines` keeps running — so decode is indeed the only thing left to reclaim. But the mem[]
pipeline expands the scene **twice**:

| expansion | from → to | cost |
|---|---|---|
| `view_paint_lines` (the sweep) | 161 source cells → **2148** framebuffer cells | **58.7 ms** |
| `decode()` (road portion) | 2148 changed cells → bitplane bytes | **~3–4 ms** |

The §10 renderer deletes **both** and replaces them with ≤5 longword-filled spans per line read from
`surface_edge` (§10e: ~8 ms). The old argument counted only the second row (~3–4 ms) as reclaimable,
because the scaffold never removes the first. **The 58.7 ms of `view_paint_lines` is the win the
scaffold hides and §10L originally omitted.** The ceiling is ~54 ms, not ~3–4 ms.

**What the scaffold DOES legitimately show.** Bolting plotting onto the mem[] scan loses — now
measured three times (a mirror-each-store plotter −9%, the source-event consumer +25 ms, this scaffold +54 ms), and exactly
what `revs_plot.h`'s header warned about a mirror-each-store plotter. That is a real, consistent
result: **do not ship a plotter that runs alongside `view_paint_lines`.** It says nothing about a
renderer that *replaces* `view_paint_lines`, because none of the three tested that.

**Instrument caveat (unchanged, still true, still not decisive):** the timing build carries diagnostic
overhead the end state would not — six volatile counters per run (not `PROBES`-gated) and an
out-of-line `revs_plot_run`. Irrelevant to the corrected conclusion either way, since the scaffold is
the wrong experiment regardless of how cleanly it is built.

**⭐ STATUS — SUPERSEDED BY §10n: that experiment was built as `make SPANFILL=1` and it has
answered.** A full-line direct renderer that stops the sweep painting the lines it owns measures
**−3.35 ms** on phase 1, its fill costs only **1.63 ms (6.87 cyc/byte, direct-to-bitplane writing
exonerated)**, and it still nets **≈ 0** — because what it deletes is the unit loop at 42 cyc/unit
while the **905 cyc/line driver survives**, and because its dirty-map predicate costs +4.4 ms in the
producers. ⇒ **Read §10n for the measurement and §10p for the shape that follows from it** (the
renderer owns phase 1's line loop rather than hooking into it). The three "bolt onto the scan"
results below stand and are now four.

### 10m. ⭐⭐ HARDWARE SPRITES for the instruments — a separate future lever (user, 2026-08-16)

> ⚠⚠ **RE-SIZE THIS BEFORE SCHEDULING IT — the 36.1% below is the VIEW sweep, not the dashboard.**
> The span census's raster map (§10a) shows `$7BE2` painting display rows 81..157 and never rows
> 158..207, so "the number-one item in the profile at 36.1%" is the road view. The sprite case for
> the instruments may still be good, but its size is **unmeasured**; §10h names the instrument that
> would settle it (`make fbwrites`, which attributes every framebuffer store to its PC on a real
> BBC). Everything else in this section — the four constraints, the oracle-derived pre-render rule —
> stands unchanged.

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

**⭐ How this stays a faithful port — the same trick as §10i (validation via the oracle).** Do not redraw the instruments by hand.
**Pre-render every sprite variant by running the game's own drawing code** (`$7BE2`'s wheel/dial
arms) once per angle/level and capturing the pixels it produces. Then the sprite images are
*derived from the oracle* rather than reinterpreted, and the differential is exact: for any state,
sprite output must equal what the decode produces. A hand-drawn wheel that merely looks right is the
failure mode this rule exists to prevent.

**Sequencing — gated behind the §10 renderer (user decision, 2026-08-16).** This is a Phase 6 item, *after* direct
bitplane rendering, for the same reason the asm is: both change how the dashboard reaches the screen,
and sprite work written against the current arrangement gets rewritten. ⚠ In the §10 world the renderer is STATELESS (§10j) — it repaints the view every frame with no
per-column dirty test — so the sprite win is the full per-frame cost of whatever instrument pixels
move, not a residue left after a dirty-map.
**`make fbwrites` sizes this whole item (§10h); measure it before scheduling.**


### 10n. ⭐⭐⭐ THE §10e CHECKPOINT IS BUILT AND MEASURED — the fill is CHEAP, the PREDICATE is the cost, and the DRIVER is what has to go (2026-09-15)

`make SPANFILL=1` (§10L's "valid next experiment": spans the lines it owns and **stops the sweep
painting them**, no mirror, ownership per display line). Four builds, one session, **parked**
(`STRAIGHT_TO_RACE=1 PROBES=1 FIXED_RNG=1`, warp, 30 s, `phase4_prof.gdb`), 4020 beam ticks/ms:

| build | what it is | ph 5 | ph 11 | ph 18 | **ph 24** | ph 27 | ph 33 | ph 34 | ph 0 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| CTL | clean control | 26.56 | 33.63 | 10.12 | **15.68** | 25.90 | 12.34 | 23.61 | 315 |
| SF2 | `SPANFILL=2` — claim, no fill | 26.44 | 35.89 | 12.96 | **11.77** | 26.09 | 12.65 | 23.93 | 218 |
| SF1 | `SPANFILL=1` — shipping arm | 26.43 | 35.69 | 12.96 | **13.39** | 25.97 | 12.68 | 23.90 | 217 |
| SF1-NOSTAT | ...+ `SPANSTAT=0` | 26.51 | 35.14 | 12.98 | **12.33** | 25.93 | 12.67 | 23.84 | 311 |

Census: units/frame 1440 → 606 → 602 → 592 over 36 lines, so **21 of 36 lines were spanned** (runs
36 → 15; phase 1's lines are one run each — see below).

**⭐⭐ READ THE QUAD AS TWO PAIRS, AND PHASE 0 IS WHAT PARTITIONS IT.** Phase 0's field count says how
many of the engine's 100-field crash holds the window reached, and a hold is a *static* scene whose
sweeps sit at the cheap end — so it dilutes the view phases' per-frame averages downward. CTL and
SF1-NOSTAT reached 3.15/3.11 holds; SF1 and SF2 reached 2.17/2.18. **Each derived quantity therefore
wants its own pair:**

| quantity | pair | dilution | value |
|---|---|---|---:|
| the NET, and the collateral | CTL ↔ SF1-NOSTAT | matched | **−3.35 ms** on ph 24 |
| the FILL | SF1 ↔ SF2 | matched | **+1.63 ms** |
| the DIAGNOSTICS | SF1 ↔ SF1-NOSTAT | **crosses** | **≤ +1.06 ms** (upper bound) |

⚠ This supersedes an earlier reading of this same quad that called phase 0 "not a reliable
discriminator" and judged soundness from untouched-phase agreement alone. Both tests matter and they
say different things: untouched-phase agreement rejects a *build* difference, matched phase 0 rejects
a *workload* difference. SF1↔SF2 passes both (every untouched phase inside 0.20 ms, ph 18 to 0.00).

#### ⭐⭐⭐ THE FILL IS CHEAP, AND DIRECT-TO-BITPLANE WRITING IS EXONERATED

**+1.63 ms/frame for 21 spans × 80 bytes = 550 cycles/span = 6.87 cycles/byte**, against an objdump
prediction of ~550 (call+prologue+epilogue+rts 82, two bounds tests ~30, `s_lineOf` ~18, `g_plotOwn`
store ~20, `s_planeOff` ~24, the `lea`/`lsl`/two `s_expand4` reads ~60, and 316 for the twenty
stores). **The model closes to 1%.**

The fill's whole measured history is now consistent, and every step of it was code shape:

| build | µs/frame | cyc per 8 B | what was wrong |
|---|---:|---:|---|
| generic `revs_plot_run`, original loop | +5470 | 185 | the loop re-derived its advancing pointer and re-read lo4/hi4/limit out of three loop-invariant stack slots every turn — CLAUDE.md's frame-slot class |
| ...loop fixed | +4050 | 56 | the loop was at the floor; the **preamble** (variable count to split, alignment step, byte tail, two synthesised broadcasts, an 8-register `movem`) was bigger than the stores |
| **specialised `revs_plot_span`** | **+1630** | **27** | nothing left above the store floor |

⇒ **The deficit was never the bitplanes.** It was, in order: a stack-slot-reloading inner loop, then
a preamble bigger than its stores, then the predicate below. ⭐ And the last three cycles per byte are
known and declined: GCC emits the twenty stores as `move.l Dn,d16(a0)` (16 cyc) rather than
`move.l Dn,(a0)+` (12), which is 316 against 240 — ~0.22 ms/frame that only inline asm can take, i.e.
0.1% of frame. `movem.l` is costed and **rejected** at the code (368 cycles against 240: the ten
registers must be reloaded with hi4 between planes, and eight are callee-saved).

#### ⭐⭐ THE DIAGNOSTICS COST ≤1.06 ms — SHIPPING PRICE ≠ COUNTING PRICE

Six `volatile` absolute-address RMWs per span plus two per line: ≤359 cycles/span. `make SPANSTAT=0`
removes them. Same class as `SND_STAT()` in the VERTB ISR. **Any span price quoted from a counting
build is ~25% too high**, which is a quarter of what the whole deletion bought.

#### ⭐⭐⭐ AND THE SPAN DELETES THE UNIT LOOP AND ESSENTIALLY NOTHING ELSE — 42 cyc/unit

Backing the diagnostics out of SF2 puts the pure deletion at **4.98 ms for 834 units = 1681
cycles/line = 42.0 cyc/unit** (an *upper* bound, since the diagnostics figure crosses the pairs and
so is itself an upper bound). The objdump's clean-arm quad body is **43 cyc/unit**
(`docs/perf-method.md` §the sweep is 61% driver/entry). **These are the same number.**

That is the finding, and it is sharper than a sizing error: the span skips the run set-up as well as
the forty units, and the measurement gives that **no credit at all**. What a span deletes is the unit
loop, at exactly the rate the unrolled body costs. **The per-line driver survives it.**

⚠⚠ **A TWO-POINT FIT ON (LINES, UNITS) CANNOT SEE THIS, AND MINE DIDN'T.** Solving
`36D + 1440u = 15.68` / `36D + 606u = 11.77` gives u = 33 cyc and D = 248 µs/line — a ~8.9 ms
per-line driver, 57% of the bracket — and **that fit is withdrawn.** It assumes one uniform `u`
across all 1440 units, and the units are not uniform: the 834 deleted ones are *by construction*
the all-clean ones at 42 cyc, while the 606 survivors carry the dirty arm (+80 cyc each) and
average ~72. With `NOUNITS=2`'s independently measured 905 cyc/line driver the parked numbers close
exactly on that split. **This is the underidentified line/run fit that CLAUDE.md already records
from the run-entry specialisation, made a second time on the same routine** — a two-parameter fit
on two points has no residual to warn you with, so it always looks like it worked.

#### ⛔⛔⛔ THE PREDICATE IS A WRITER-MAINTAINED DIRTY MAP, AND IT COSTS MORE THAN THE SPAN SAVES

CTL ↔ SF1-NOSTAT, matched dilution:

| phase | what | Δ |
|---|---|---:|
| 24 | `view_paint_lines` phase 1 | **−3.35** ← the span's own win |
| 11 | `draw_road` | **+1.51** |
| 18 | `fill_dash_edge_columns` | **+2.86** |
| 33 / 34 | view phases 2 / 3 | +0.33 / +0.23 |
| 5 / 27 | untouched | −0.05 / +0.03 |

⇒ **net +1.0 ms/frame.** The collateral is the build, not the trajectory: all three span builds read
ph 18 at 12.96 / 12.96 / 12.98, agreeing to 0.02 ms across two different dilutions.

**⭐⭐⭐ The mechanism is inline bloat, not the map's arithmetic.** `REVS_SPAN_EMIT` turns on
`REVS_VIEW_MARKING`, which inlines the `REVS_FLAG_OP` leaf `view_mark_source` into `seam_write` —
the choke point every indirect store passes. The objdump counts **164 inlined copies of
`g_viewLineDirty` in the span build and 0 in the control.** Twenty land inside
`column_gap_walk_core` (1149 → 924 instructions), whose caller `fill_edge_column_run_core` then
collapses **532 → 54 instructions**: it lost its callee's inlining outright. That is ph 18's +2.86.
Another ~46 are spread through `draw_road`'s tree (`plot_view_src_line_core` 19, `interp_edge_core`
8, `fill_object_gap_core` 7, `sw_plot_1/2` 7, `paint_fence_backdrop_core` 5) — ph 11's +1.51.
`draw_road_core` (192 instrs) and `draw_surface_spans_core.part.0` (169) are byte-identical in shape
between builds, so the cost is entirely in the source-writing leaves.

⇒ **A CONSUMER-SIDE PREDICATE MAY NOT BE MAINTAINED BY THE WRITERS.** Second measured instance behind
`CHANGEDIRTY`, by a **different** mechanism — not per-store compare traffic but inline bloat arriving
through a header — and a violation of the standing STATELESS directive either way. Annotated at
`src/gen/revs_native_seam.h`'s `REVS_VIEW_MARKING` gate.

#### ⭐⭐ WHAT §10e GOT WRONG, IN ITS OWN TERMS

§10e's checkpoint asked: *"if 37% of the sweep's stores do not come off phase 1's bracket roughly in
proportion, the model is wrong."* **58% of phase 1's units came off and bought 32% of phase 1's
bracket** (4.98 of 15.68); against the whole sweep, 39% of its 2155 stores bought 9.6% of its
51.63 ms. Not in proportion.

The error is **not** the fill (1% of prediction) and **not** the store rate (2% of prediction). It is
that **§10e sized stage A as "stores deleted" when the bracket is driver + stores, and a hook-in
reaches only the stores.** That was already published when §10e was written — `NOUNITS=2` puts phase 1
at 29% driver / 71% units and the whole sweep at **61% driver/entry** — and §10e simply did not apply
it. ⇒ **Size a hook-in against the differential for the part it can actually delete, never against a
census of stores.** (CLAUDE.md already carries the general form: *price a skip scheme with TWO
numbers — visits deleted, and what one visit of the NEW shape costs.* The missing third number is
**what survives**.)

**The floor this puts under a hook-in.** Even a free, exact predicate and a free fill leave phase 1's
driver standing: spanning *all* 36 lines would delete 11.23 ms of unit loop and leave 4.59 ms. And a
stateless predicate is not free — reading the forty sources per line is ~2.44 ms over 36 lines (below)
— so the best hook-in available is:

| hook-in variant | ph 24 | collateral | net |
|---|---:|---:|---:|
| as built (dirty map) | −3.35 | +4.93 | **+1.6** |
| stateless 40-source scan instead | −3.35 + 2.44 | 0 | **−0.9** |

**Both are zero.** ⇒ **THE DRIVER MUST GO. The span renderer has to OWN phase 1's line loop, not hook
into it** — where the source scan *replaces* the chain's own forty source reads instead of being added
to them. That is §10p.

#### ⭐ Correctness, and one instrument retired

Both in-process oracles were green against the specialised span: **the span bytes**
(`SPANFILL=1 SPANVERIFY=1 DIRECTCHECK=1` + `span_emit.gdb`) `checks=21 mismatch=0`, and **the
carve-out** (`SPANFILL=1 DIRTYCHECK=1` + `span_fill.gdb`) `checks=29 mismatch=0`, exact by
construction because an owned line is `m_lineMode[y]=0` and the reference conversion writes nothing
there. ⚠⚠ **Cross-build pixel dumps at the same game frame are retired** (`amiga/span_fill_dump.gdb`
carries the reasoning): the 50 Hz body runs on wall clock, so at equal game frames a faster build has
taken a different number of body ticks — frame 40 was vbi=556 in the control and 574 under
`SPANFILL=1`. That trajectory divergence is what produced the "lines 133..140, plane 2" diff that
stood open for a session.

### 10p. ⭐⭐⭐ THE PHASE-1 TAKEOVER — the renderer owns the line loop (the live next build)

§10n's verdict in one line: **a span deletes 42 cyc/unit and the 905 cyc/line driver survives, so the
win is in owning the loop, not in being called from it.** Phase 1 is the right first owner: 36 lines,
full width, and — measured below — structurally the simplest of the three phases.

**What `view_paint_lines` phase 1 actually does per line**, read off `revs_native.c`:

1. `step_scanline()` advances `plot_ptr`/`plot_ptr2` ($70-$73);
2. `byte = surface_colours[view_line_surface[line] & 3]` — **the line's colour is one table lookup
   off a per-line index.** No chain state.
3. run set-up: `view_stop_from(0)`, the segment pointers, `base_span_is_ram`;
4. forty units, each `view_consume(src) → store`.

⭐⭐ **AND `view_consume` IS RLE WITH A DESTRUCTIVE READ, WHICH IS WHAT MAKES A STATELESS PREDICATE
EXACT.** A zero source means "same as my left"; a non-zero source means "change to
`view_cell_bytes[source]`" **and is then cleared to zero**. So at line entry a source byte is
non-zero *iff a producer wrote it since the last sweep consumed it*, and:

> **a line is one flat run ⟺ all forty of its sources are zero.**

Two consequences, both load-bearing:

- **⭐ The stateless test is EXACT where the dirty map is CONSERVATIVE.** `g_viewLineDirty` marks on
  *any* write, including a write of zero, so it rejects lines that are in fact flat. The 40-source OR
  therefore qualifies **at least** the 21 of 36 lines the map qualified, and possibly more.
  ✅ **MEASURED at 57% — 20.5 of 36 — over 595 DRIVING sweeps** (the count below). So the exact test
  and the conservative map qualify the *same* set to within the workload difference; the map's 21 was
  not leaving lines on the table. What the map cost was its **producers**, not its precision.
- **⭐ `view_stop_from(0) == 40` is a constant `true` in phase 1** — the planted-stop list "is empty
  through all of phase 1" (`view_stop_from`'s own note; `stopUnit == 40` is "none in this chain run,
  which is every one of phase 1's lines"). The `fullRun` conjunct is dead weight here, which is why
  phase 1's census reads exactly one run per line. **Phase 1 needs the source test and nothing else.**
  ✅ **MEASURED AND CONFIRMED: `full == lines` exactly, over 21456 phase-1 lines.** This was a claim
  read off a comment; it is now a count, and the conjunct comes out of the predicate.

⭐⭐ **THE SCAN, AND ITS 3× SHORTCUT.** Source bytes sit at `view_src_blocks + (cell << 7) + line`, so
for one line they are forty reads 128 bytes apart: `or.b d16(a0),d0` × 40 = **480 cycles** (offsets
0..4992 fit the signed displacement, one base register, no table). But **along the other axis the
stride is 1** — four *consecutive lines* of one cell are four consecutive bytes — so one
`or.l d16(a0),d0` (18 cyc) tests four lines at once and forty of them answer **four lines for 720
cycles = 180 cyc/line**, a 2.7× reduction, with a zero byte lane naming each flat line. Phase 1's 36
lines are 9 groups: **0.91 ms instead of 2.44.**
⚠ Two constraints, both to be discharged in code: the group base must be **even** (a `move.l` at an
odd address is an address error on the 68000), and reading mem[] four bytes at a time is exactly the
alias `make endian-lint` forbids — legal here only because the accumulator is used as four
*independent byte lanes* and OR is lane-order-independent, but the lane→line map is not, so the
exception must be declared at the code and the lane test written per byte.

**Sizing, per line, against `NOUNITS=2`'s measured 905 cyc/line driver + 55 cyc/unit:**

| line kind | now | takeover | basis |
|---|---:|---:|---|
| flat | 905 + 40×55 = **3105** | ~250 driver + 180 scan + 550 fill = **~980** | the fill is measured; the driver is a hand-written loop with no stack round trips |
| not flat | **3105** | ~250 + 180 + 480 positional walk + 550 fill + boundaries×~70 ≈ **~2300** | the OR loses position, so a changed line pays the byte scan too |

| phase 1 | ms | saving |
|---|---:|---:|
| now (`NOUNITS=2`, driving) | **15.83** = 4.59 driver + 11.23 units | — |
| takeover, all 36 lines flat (the best sweep observed) | **~5.0** | −10.7 |
| **takeover at the MEASURED 57% flat** | **~7.9** | **−7.8** |
| takeover with nothing flat (the worst sweep observed) | **~11.7** | −4.0 |

⇒ **−7.8 ms on phase 1 on the average frame, against the hook-in's ≈ 0**, bracketed −4.0 to −10.7 by
the observed per-sweep spread. ⭐ The average is the right headline number and the spread does not
dilute it, because the cost is **linear** in the flat count: `Σ(flat·980 + (36−flat)·2300)` is
`36·2300·N − 1320·Σflat`, which depends on Σflat and on nothing else about its distribution. The
spread bounds the WORST FRAME instead — a jitter question, and a real one at 11.7 ms.
And the same shape then addresses phases 2
and 3 — 12.34 + 23.61 = 36 ms at **67% and 80% chain-entry**, the larger prize, which is why the
takeover is built on phase 1 first and not last.

#### ✅ STEP 1 IS DONE — THE FLAT-LINE COUNT, MEASURED (`3b79ebc`, 2026-09-15)

The cheap checkpoint for this stage, and it needed no emulator run: `shape_view_flat` (`make SHAPE=1`,
`REVS_SHAPE_WATCH=N`) asks the takeover's own predicate at the exact point the takeover will ask it —
in `paint_cells`'s `advance_first` arm, after the line is stepped and its background byte resolved,
before any unit runs. Host, `REVS_FIXED_RNG=1`, **`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` so the car
MOVES** (the parked-car trap: a static scene reports "skip everything"), 595 sweeps:

| phase | lines/sweep | flat | `full` | flat&&full | per sweep |
|---|---:|---:|---|---:|---|
| 1 (`view_paint_lines_core`) | **36** | **57%** | **== lines** | **57%** | 0..36, last 21 |
| 2 (`paint_lines_clipped`) | **16** | **0%** | == 0 | 0% | 0..0 |
| 3 (`paint_lines_short`) | — | — | — | — | never enters the hook |

**Read in the order the instrument's own comment gives:**

1. **36 and 16 reproduce the target's line split**, so the row carries across to the Amiga. (Phase 3
   is absent because only `view_paint_lines_core` and `paint_lines_clipped` call `paint_cells`;
   `paint_lines_short` drives its own line loop. That is a structural finding, not a gap.)
2. **`full == lines`, exactly, over 21456 lines** ⇒ §10p's no-planted-stops claim holds and the
   `fullRun` conjunct leaves the predicate.
3. **57% flat ⇒ −7.8 ms** on the average frame (the table above).

⭐ **And it cross-checks the PARKED span census.** `SPANFILL=1` found 21 of 36 lines spanned with the
dirty-map predicate; this **driving** run's last sweep reads **21**, and its mean is 20.5. The
standing doubt about that 21 — that a parked car reports a static scene — is discharged.

⛔ **THE FLAT PREDICATE IS PHASE-1-ONLY, AND THAT IS THE ONE UNWELCOME RESULT.** Not one of 9536
phase-2 lines had forty zero sources, and every one of them had stops planted (2.0 runs/line). Phase
3's lines visit **11.2 of 40** cells, so a forty-cell scan asks the wrong question there before it is
even tried — 180 cyc/line of scan to save 11 × 55 = 605. ⇒ **phases 2 and 3 are a DRIVER lever, never
a span-fast-path one**, and the 36 ms behind them has to be reached by owning their loops, not by
qualifying their lines. The takeover's shape must not assume otherwise.

⚠⚠ **A caveat this doc and `shape.h` both carried is WITHDRAWN, and the correction is the
transferable part.** I wrote that a whole-run ratio "cannot answer the sizing question" because
21-of-36 averaged could be bimodal, and built a per-sweep min/max tally to defend against it. It can:
the cost is **linear** in the flat count, so `Σflat` is a *sufficient statistic* for the average frame
and the distribution is irrelevant to it. ⭐⭐ **Ask whether the statistic is sufficient for the COST
FUNCTION before demanding a distribution** — a linear cost needs only the total; the min/max earns its
keep on the *jitter* question (worst frame 11.7 ms) and not on the sizing one.

⚠ Sabotaged four ways before its output was believed: one cell instead of forty reads 96%, the
inverted test 0%, a 64-byte source stride 0%, and the restored control reproduces 6109 exactly.
`view_stop_from` is pure, so the SHAPE build's trajectory is the default's; `determinism`,
`-drive` and `-steer` all PASS.

**What the takeover still owes `mem[]` — the reader audit, which is the hard half (§10i, the RESULTS
rule):**

| what | who reads it | status |
|---|---|---|
| `plot_ptr` / `plot_ptr2` ($70-$73) | phases 2 and 3 continue the walk from them | **must be left exactly as the chain leaves them** — `step_scanline` stays |
| the forty source bytes | nothing, once consumed | free on the flat path: already zero, so "not consuming them" is exact |
| `cell` / `view_cell_bytes` | the next chain entry | the emitter already reproduces this (`cell = 0x38`, "unit 39's cell, as a full line leaves it") |
| **the framebuffer bytes for rows 81..116** (phase 1's own rows, measured) | **NOTHING** — measured, not argued: the poison test finds 0 of 1440 inverted bytes read back | ✅ **THE GATE IS OPEN. Own them outright — no write-back, no shadow.** |
| the framebuffer bytes for rows 117..132 (phase 2) | `plot_line_octant`'s undo save, entries 28..31 | owed when phase 2 is taken over, not now |
| the framebuffer bytes for rows 133..157 (phase 3) | `plot_line_octant` entries 32..35, **and `update_grip_limits` on row 149** | owed when phase 3 is taken over (Hazard 1 already names row 149) |

#### ✅ STEP 2 IS DONE — THE READER GATE IS OPEN, MEASURED (`f80557d`, 2026-09-15)

⛔ **First, a retraction: the gate's mechanism as written above was WRONG.** It claimed
`column_gap_walk` (phase 18) reads frame-buffer pixels. It does not. It walks the **source blocks**
at `$3000 + column*$80` and stores to `$0504` / `$4400` — not one of those addresses is inside the
frame buffer (`$5A80..$7B40`; `BBC_SCREEN_BASE 0x5A80`, `BBC_SCREEN_BYTES 8320`). The `NOUNITS=2`
observation the claim rested on (ph 18 at 5.88 ms against 10.18) is real and has a different cause:
NOUNITS=2 never **consumes** the sources, so they stay non-zero, and the gap walk — which replaces
every **zero** byte with `surface_colour_at`'s answer — finds fewer zeros to fill. Phase 18 is a
**producer for the sweep**, not a consumer of the frame buffer. `fill_dash_edge_columns` being a
producer is still true and still means it cannot simply be sequenced after; it is not a *reader*.

⭐⭐ **And the audit itself was asked as a MEASUREMENT, which is why it closes.** A code read cannot
close a reader audit on this project — the readers include the transliteration an expansion circuit's
hook re-enters (CLAUDE.md: "a `region_*` name is SHIPPING code until proven otherwise"), and no scan
of the native surface can see those. So `REVS_FB_POISON=<first>-<last>` (`shape_fb_poison`, hooked
right after `view_paint_lines()` returns) **inverts** every `mem[]` byte of the named display lines —
`^= 0xFF`, not a constant, so every poisoned byte provably differs from the pixels the sweep just
wrote — and the whole 64 KB is diffed at frame 300 against an unpoisoned run. **Every difference
inside the poisoned rows means nothing in the game read them**, and it covers the transliteration,
the track hooks and the 50 Hz body for free, because it asks the machine rather than the source.

Its positive control is built in and had to fire first: `update_grip_limits` reads `mem[$713D]` and
`mem[$7205]`, both on **display line 149** (cells 7 and 32: `$713D − $5A80 = 18*320 + 7*8 + 5`).

**Which rows each phase paints is measured too** — `shape_view_flat` now takes the screen pointer and
keeps a per-phase min/max display line, because the gate has to be evaluated against the rows the
takeover *owns*, not the whole band. They tile it exactly, and reproduce the 36/16/25 split:

| rows | phase | lines | differ | inside | **OUTSIDE** | the reader |
|---|---|---:|---:|---:|---:|---|
| **81..116** | **1 `view_paint_lines_core`** | 36 | 1440 | 1440 | **0** | ⭐ **NONE** |
| 117..132 | 2 `paint_lines_clipped` | 16 | 644 | 640 | 4 | `plot_line_octant`, undo 28..31 |
| 133..157 | 3 `paint_lines_short` | 25 | 2796 | 796 | 2000 | …undo 32..35 **+ row 149** |
| 149..149 | (the positive control) | 1 | 2325 | 34 | 2291 | `update_grip_limits` — the instrument works |

⭐⭐⭐ **So the takeover may own display lines 81..116 OUTRIGHT: no compatibility write-back, no shadow
copy, nothing.** All 1440 poisoned bytes also *survived* to the dump, so nothing overwrites those rows
after the sweep and no masking can be hiding a reader behind a rewrite. Sub-ranges agree: `81..101`
alone is clean (840/840 inside) and `102..116` alone is clean (600/600), so the answer is not an
average over a mixed band.

⭐⭐ **The reader in the other two phases is `plot_line_octant`, and it is not a pixel consumer.** It
saves the **original** byte at each address it plots into `MEM_plot_undo_byte` (`$0780`, `src/gen/mem.h`)
so the plot can be undone. The eight outside bytes are exactly entries 28..35, each the bit-inverse of
the reference, and they split 4/4 between rows 117..132 and 133..148 — i.e. by phase. A takeover of
phases 2/3 therefore owes the **undo table** real bytes, not the whole frame buffer. Phase 1 owes
nothing, which is where the takeover starts.

#### ✅ STEP 3a IS WRITTEN AND ITS ENDIAN HALF IS ORACLED — the stateless predicate (2026-09-15)

⭐⭐ **The predicate needs no state at all, and that is a THEOREM about `view_consume`, not a
heuristic.** The consume is RLE with a **destructive read**: a zero source means "same as my left",
a non-zero one is translated through `view_cell_bytes[source]` and then **`*srcp = 0`**. So a source
byte is non-zero at line entry *iff* a producer wrote it since the last sweep, and therefore

> **a line is one flat run ⟺ all forty of its sources are zero.**

That is **exact** where the ⛔ dirty map was merely conservative, it needs no priming (the map's
153-stale-byte first-sweep hazard cannot exist), and it deletes the whole `REVS_VIEW_MARKING`
surface from the producers — the +4.35 ms of inline bloat that killed the map is simply not spent.
`SPANFILL=3` is that arm; the map arm is kept verbatim beside it as the measured control.

⭐ **The scan is cheap because the LINE axis has stride 1.** Along the cell axis the forty sources
are 128 bytes apart (40 × `or.b d16(a0),d0` = 480 cyc), but four *consecutive lines* of one cell are
four consecutive bytes, so one `or.l d16(a0),d0` (18 cyc) tests four lines at once: 40 × 18 =
720 cyc per group of four = **180 cyc/line**, and `line` decrements through `$4F..$2C`, which is
exactly the consecutive descending order a group-of-four accumulator wants. Alignment is a theorem,
not a hope — the group base is `line & ~3`, and `MEM_view_src_blocks` (`$3000`) and `cell << 7` are
both multiples of 4, so an odd-address fault cannot arise. That is `SPANFILL=4`.

⚠⚠ **The longword read is the ONE argued exception to CLAUDE.md's endianness rule, and it is gated
by a counter rather than by the argument.** The longword is never a *value*: it is four independent
byte lanes whose only operation is OR, which is per-byte and lane-order-independent. What *is*
endian-dependent is the lane→line map, so that is written out per target (`VIEW_SCAN_LANE` under
`__BYTE_ORDER__`) and `REVS_SPAN_SCANCHECK` compares every lane against the byte scan's own answer
on every line of all three phases. A wrong map is otherwise **invisible** — it answers "flat" about
a *neighbouring* line, which paints a plausible picture rather than a broken one.

- host (little-endian), driving workload: **15392 lanes checked, 0 MISMATCH**.
- ⭐ **sabotaged five ways, all five caught** (of 6032 checks): lane order reversed 1120, lane index
  ignored 123, the big-endian map used on the host 1120, one of the forty cells dropped from the
  group scan 9, the byte reference's stride bent by 4 → 469. ⚠ The two *smallest* counts are the
  instructive ones: a defect that only sometimes disagrees is exactly what a counter catches and a
  rendered frame does not.
- ⏳ owed: the same check on the target (`make SPANFILL=4 SPANSCAN=1`) — the big-endian arm of the
  `#if` is the half the host cannot run.

**Order of work:** ✅ (1) the host flat-line count — **DONE, 57%, `3b79ebc`**; ✅ (2) the reader gate —
**DONE, OPEN for rows 81..116, `f80557d`**; ✅ (3a) the stateless predicate + the group-of-four scan
— **WRITTEN, endian-oracled on the host**; ⏳ (3b) the takeover's own line loop, which deletes the
905 cyc/line driver — oracled by the two in-process checks of §10n; (4) then stage A in full, then
B, C, D, E as §10j has them.
