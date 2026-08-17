# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

---

## `$0164,X` / `$0178,X` — the two per-driver quantities `place_player_in_section` computes

Named-by-address only, and one of the two is the car's position *along* its track section and the
other is *across* it — `[INFERRED]`, which of them is which is not settled.  The evidence to use:
`$0178,X` is compared BETWEEN cars at `$271E`-`$2762`, stepped by ±1 by the other-car AI at
`$28A5`-`$28DF`, seeded to `$50 EOR $FF` for all twenty drivers at `$1100`, and masked with `$7F`
into `$0114,X` when `$11AB` spins a car out; `$0164,X` is differenced between two cars at `$27A4`
and accumulated at `$2887`, and `reset_driving_variables` zeroes both.  A ±1 step per frame reads
like a discrete lane/segment index, which would make `$0178` the "across" one — the opposite of the
first guess.  Cheap way to settle it: park the car and drive it straight, and watch which of the
two moves.

## `$8C` has THREE owners, and `surface_style_alt` only covers one

Named for what `draw_surface_spans` reads it as (the style index for spans nearer than
`road_split_index`), which is right for the road pipeline.  It is also a pixel MASK at
`$1CEE`-`$1D48` in the skid/contact plotter (`LDA $8C / STA $7D / EOR #$FF / AND $7A`) and is
zeroed at `$20A5` by the object plotter.  Either the three uses are genuinely one quantity — in
which case the name is too narrow — or the cell is scratch shared between subsystems, in which case
so is the name.  Same question for `$8E` (`shared_temp_8e`), which is named for the fact rather
than the meaning: `draw_road` zeroes it with no reader in its own pipeline, `$2012` loads it as a
shape index, and `$4B61`-`$4B79` uses it as a signed temporary in the driving model.

## `$11` — `edge_nearest_hi`, and what `check_crash` reads it for

`$10`/`$11` is the nearest projected distance `road_edge_walk` keeps, and the name says so.  But
`check_crash` (`$111E`) reads `$11` after the frame's walk and does nothing at all while it is
under 2 — so the cell is doing double duty as "how close did the track come", and whether that is
the same quantity or an accident of ordering is not settled.  Settle it before anything relies on
the name: the walk's writer is `$23E7`-`$23ED`, and `place_player_in_section` reads `$10` at
`$4681`.

## `apply_driving_model`'s sub-model chain — eleven of fifteen callees are still `FUN_xxxx`

The physics driver is a twin now (`src/gen/revs_native.c`, twin #6) and reads as C, but eleven of
the fifteen routines it calls have no name, so the twin's body is a list of addresses.  Only
`compute_car_angles` ($0D01), `integrate_car_position` ($48EF) and `abs16_math` are settled.
What is already known about the rest, as the starting map:

| addr | size | what it touches |
|---|---|---|
| `$48B9` | 8 | an alternate entry into `$48C1`'s body at `$48C7` (Y=0, A=8, X=$C0 instead of 6, 3, $40) |
| `$4729` | 42 | scales `model_state[2]` by Y=$58, SUBTRACTS it from `model_accum`, and leaves 1.5x it in `model_accum_delta_lo/hi` (`$4765` is the x1.5) |
| `$4BCF` | 146 | reads `car_speed_hi` (`$4B98`) and `$8E` as a signed temporary |
| `$49CE` | 195 | the only reader of `road_speed_frac`; also reads `drive_state` |
| `$4779` | 44 | called TWICE, X=1 then X=0.  With `drive_state` >= 2 it stops sound channel 3 and returns; otherwise `$4A91` shifts `model_accum` left 5 into `model_state[10]`/`[11]` |
| `$47A5` | 32 | two `$4874` calls with ($79=$80, A=$0E) and ($79=$40, A=$09), then the generic integrator `$47E5` on element 8 |
| `$47C5` | 32 | the same shape with ($79=0, A=$0E) and ($79=$C0, A=$0C), then `$47E5` on element $0A |
| `$47F9` | 116 | — |
| `$4C65` | 63 | reads `model_accum_entry_hi`, i.e. the accumulator's value BEFORE this frame's sub-models |
| `$48C1` | 46 | the body at `$48C7`: four `$486D` calls stepping `$7F`/`$7C` and flipping `$88`'s sign — a four-corner sweep |
| `$4937` | 65 | — |
| `$44EA` | 294 | the tail.  With `drive_state` 0 it steps `$62F0` up or down, clamped to $FB..3; otherwise DECs `$28` twice.  Writes `drive_state` itself at `$45C9` (0, 1 or $7F).  Carries two SMC sites, `$44D5` and `$45CB` |

⭐ `$47E5` is the one that should be named first — it is the model's generic 16-bit integrator
(`model_state[X] += model_state[14]`) and naming it settles what element 14 is.

## `model_accum_lo`/`model_accum_hi` ($62D8/$62E8) — element 8 of what, physically?

Named for its ROLE (the one element `apply_driving_model` integrates by hand) rather than its
meaning, and the role is unusually visible: saved on entry, offset downward by `$4729` for the
duration of four sub-models, restored, then advanced by 1.5x what was removed.  A quantity that
sub-models are shown at `x - v` while it really becomes `x + 1.5v` looks like a midpoint
integration of a position or a heading.  `$4A91` shifts it left 5 into `model_state[10]`/`[11]`,
and `$47A5` also adds element 14 into it.  Settle it the cheap way: park the car, drive straight,
then steer, and watch whether it tracks the heading or the distance.

## `drive_state` ($002D) — three values written, and only the `>= 2` test is understood

0 is normal driving and `>= 2` is "not under power" (elements 5..7 of `model_state` are zeroed and
sound channel 3 is stopped), but the engine writes exactly three values — 0 and 1 from `$44EA`'s
own arms at `$45BF`-`$45C9`, `$7F` from both `$44EA` and `check_crash`'s crash arm at `$115C` —
and `$4DCB` INCs it.  So 1 is a state that behaves like normal driving and `$7F` counts upward
from a crash.  Whether 1 means "stalled", "in the pits" or "engine cranking" is unsettled, and the
name is deliberately vague until it is.  `$44EA`, `$49D2` and `$4C18` are the other readers.

## `saved_slot_index` ($0045) — one cell, six unrelated owners

A one-byte save slot for a caller's object/driver index, written and read back by six routines
that have nothing to do with each other (`check_crash`, `$28F5`/`$29F9`/`$2A4D`, the `$2ACB` entry
to `draw_track_object`, `$2B5C`/`$2B91`, `build_road_sign`, `front_end_menus`).  ⚠ It is a real
exit contract, not scratch: `draw_track_object` ends with `LDX saved_slot_index` on all three
paths, and when the main loop enters at `$2AD1` the cell still holds whatever the PREVIOUS owner
left — so X on the way out of the body's 15th call comes from another subsystem entirely.  Either
the six uses are one quantity (in which case the name should say which index) or the cell is a
shared spill slot (in which case so should the name), and until that is settled nothing should
rely on the value crossing that call.

## `car_flags_1` ($018C) — the name covers the flags and misses the shape

Named for the bits `lap_complete` tests, which is right as far as it goes, but the low NIBBLE of
the same byte is the object's SHAPE index: `draw_track_object` loads it, ANDs `#$0F` and hands it
to `plot_object` as `plot_shape`, and the slot writer at `$2AA1` ORs a new shape back in under a
`#$70` mask.  So one byte carries two unrelated things and the name only names one of them —
either split the name by nibble or widen it.  `$04DC` and `$01A4` are the neighbouring per-driver
arrays `reset_driving_variables` seeds alongside it, and both are still unnamed.

## ⭐⭐ `player_pos_lo`/`player_pos_hi` ($000A/$000B) — it is a HEADING, not a position

The strongest item in this queue, because the name is wrong rather than narrow, and three
independent pieces of evidence say so — all found while twinning `build_track_geometry`'s callees:

* `bearing_to_section` (`$2145`) is an ARCTAN: it divides the smaller of two camera-relative
  section deltas by the larger, indexes the table at `$6100`, and adds a quadrant base of
  `$20`/`$60`/`$A0`/`$E0` (or `$40`/`$C0` on the degenerate arm).  Its output is unambiguously a
  16-bit ANGLE, in `bearing_lo`/`bearing_hi`.
* `emit_edge_bearing` (`$23C0`) stores `bearing - $0A/$0B` into `edge_x_lo`/`edge_x_hi`.  Subtracting
  a track position from a bearing does not type-check; subtracting a heading does, and it makes
  `edge_x` the point's AZIMUTH relative to where the car points.
* `$4927` advances `$0A/$0B` by `model_state[2]` once a frame, and `rebase_edge_point` (`$0BA2`)
  subtracts *the very same* `$62D2`/`$62E2` from every already-emitted `edge_x` — i.e. the cell and
  the edge arrays are the same kind of quantity, and the delta is an angular RATE.

So `$000A`/`$000B` should be `car_heading_lo`/`car_heading_hi`, and the same question falls on
`object_pos_lo`/`object_pos_hi` (`$0380`/`$0398`), which `build_player_car` copies into it at
`$11FB`-`$1205` while EORing the high byte with `track_direction`.  ⚠ Renaming these touches the
`draw_track_object` twin and `edge_x_lo`'s own note, so do it in one pass.  Cheap confirmation:
park the car, turn the wheel without moving, and see whether `$0A/$0B` changes.

## `$5E50`/`$5EA0` — a second angle list whose extent contradicts the first

`emit_edge_width_offset` (`$2565`) is the only writer and `$1AB9`-`$1AC9` inside `draw_road` the
only reader: it stores `edge_x ± (plot_width << shift)` at `edge_x_lo + $10` / `edge_x_hi + $10`
indexed by `edge_cursor`, which reads as the point's OTHER road boundary.  ⚠ But the base overlaps
the 2x40 edge arrays that `$253B` proves are contiguous (`$5E90,Y` differenced against `$5EB8,Y` =
`$5E90 + $28` for Y = 0..39), and at `edge_cursor` = `$40` the write lands on `$5EE0`, which is a
different array again.  So either the lists are sixteen entries and `$253B`'s stride reading is
wrong, or something not yet found bounds `edge_cursor` below `$40`.  **Resolve before the
representation change** — `docs/direct-bitplane-plan.md` §7a rearranges exactly these arrays.

## `$0CA5` — the distance blend, and what `$7E` gates it on

Unnamed, and every edge point goes through it: `emit_edge_bearing` tail-calls it and it leaves the
point's scaled distance in `point_dist_lo`/`point_dist_hi`.  Two arms, chosen on `$7E` against
`$67`: `a + b/8`, or `b/2 + a - a/8`.  `$7E` is written only by `bearing_to_section` (`$21DF`,
`$2257`, and `$FF` on the degenerate arm) as the RAW arctan table byte — so the blend is switching
on how oblique the point is, which is what a field-of-view term looks like.  The expansion circuits
carry a `HookFieldOfView` (`docs/reference-sources.md`), which is the obvious place to check.

## Named-for-the-fact scratch: `shared_counter_42` ($0042), `point_dist_*` ($7C/$7D), `shared_temp_77`

All three were named while twinning the road walk because a twin may not carry a bare hex address,
and all three are named for the observation rather than the meaning:

* `$0042` has ten writers and no one meaning; in the road pass it is the walk's point counter.
* `$7C`/`$7D` has two live readings AT ONCE — `$0CA5` writes the current point's distance,
  `road_edge_walk` compares it against the running nearest, and `project_point` (`$22B0`) reads the
  pair as its FAR CLIP, which means the clip a point is tested against is the PREVIOUS point's
  distance.  That is either the algorithm or an accident; settle it.  Six further writers
  (`$1C2C` `$1CF4` `$1D1A` `$1D39` `$1D84` `$4874` `$48C9`) are unrelated.
* `$0077` is scratch in the `$74`-`$79` window, shared with `emit_edge_width_offset` and `$134F`.

## ⭐ `plot_width` ($002A) and the unnamed `$002B` — they are `project_point`'s SECOND output

Found while twinning `div16by8`, which is what made the pair visible: `project_point` normalises the
far clip, and *before* dividing it stores the `$6180,Y` table byte in `$2A` and the shift count `Y`
in `$2B` (`$22D2`-`$22D8`) — **neither of which it reads again**.  The consumer is
`emit_edge_width_offset` (`$2589`-`$25A9`): it subtracts a per-side constant from `$2B`, then shifts
`$2A`/`math_hi` left or right by that many places.  So the two cells are a *mantissa and exponent*
— the road's apparent WIDTH SCALE at the point just projected — handed from the projection to the
width emitter across a routine boundary, and `$2A82`/`$2A88` reads the same pair for track objects.

`plot_width` is therefore right for the object plotter (`draw_corner_markers` at `$1B6B`,
`draw_track_object` at `$2B06`) and wrong here: the road pass never plots with it.  `$2B` has no
name at all.  Either split the two owners or name the pair for the scale it carries — and note that
`$1FE0`/`$1FF4`/`$1FF8` write BOTH cells too, so a third reading may be hiding.

## The whole `$61xx` page is unnamed — the arctan table and `project_point`'s scale table

`$6100` is the 128-entry ARCTAN `bearing_to_section` indexes with the ratio `div16by8` returns
(`$21DC`, `$2254`), and its raw byte is what `$0CA5` gates the distance blend on.  `$6180` is the
table `project_point` indexes with the *normalised divisor* to build the width scale above
(`$22D5`).  Neither has a `symbols.csv` row, so every twin in the road pass will have to name them
— they are the last two bare addresses in that pipeline.  Cheap to settle by dumping the bytes: an
arctan ramp and a reciprocal/mantissa curve look nothing alike.

## `near_edge_first`/`near_edge_last`/`near_edge_shift` ($0005/$0006/$0007) are `[INFERRED]`

The reading is structural and consistent — slots 0..5 of each 40-point half are the near edge
points, both walks start at 6, `shift_near_edge_points` slides exactly five entries, `6` is the
"nothing to do" sentinel in both cells, and race init seeds both to 6 — but nothing has been
MEASURED.  Cheap confirmation on the reference loop: drive at a steady speed and watch how often
`$62F5` is set and what `$0007` reads when it is; it should equal the number of sections crossed.
