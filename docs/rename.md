# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

---

## 1. `$0164,X` / `$0178,X` — the two per-driver quantities `place_player_in_section` computes

Named-by-address only, and one of the two is the car's position *along* its track section and the
other is *across* it — `[INFERRED]`, which of them is which is not settled.  The evidence to use:
`$0178,X` is compared BETWEEN cars at `$271E`-`$2762`, stepped by ±1 by the other-car AI at
`$28A5`-`$28DF`, seeded to `$50 EOR $FF` for all twenty drivers at `$1100`, and masked with `$7F`
into `$0114,X` when `$11AB` spins a car out; `$0164,X` is differenced between two cars at `$27A4`
and accumulated at `$2887`, and `reset_driving_variables` zeroes both.  A ±1 step per frame reads
like a discrete lane/segment index, which would make `$0178` the "across" one — the opposite of the
first guess.  Cheap way to settle it: park the car and drive it straight, and watch which of the
two moves.

## 2. `$76` has TWO owners — `plot_octant` and `fill_line_attr`'s range flag

`$0076` is named `plot_octant` after `dial_needle_angle`/`plot_line_octant`, which build
`(quadrant << 1) | mirror` in it.  But `edge_x_offscreen` (`$1933`) also rolls "is this edge point
within `$14` of the road centre" into its top bit, once per edge point, and `fill_line_attr` then
tests it with `BIT $76` — nothing to do with an octant.  Both uses are live in the same frame.
Settle which is the real owner (the dial runs from the body's own arm; the road walk runs from
`draw_road`) and then either split the name by owner or give it a neutral one — but do not leave a
twin reading `plot_octant` for a road flag.

## 3. `$8C` has THREE owners, and `surface_style_alt` only covers one

Named for what `draw_surface_spans` reads it as (the style index for spans nearer than
`road_split_index`), which is right for the road pipeline.  It is also a pixel MASK at
`$1CEE`-`$1D48` in the skid/contact plotter (`LDA $8C / STA $7D / EOR #$FF / AND $7A`) and is
zeroed at `$20A5` by the object plotter.  Either the three uses are genuinely one quantity — in
which case the name is too narrow — or the cell is scratch shared between subsystems, in which case
so is the name.  Same question for `$8E` (`shared_temp_8e`), which is named for the fact rather
than the meaning: `draw_road` zeroes it with no reader in its own pipeline, `$2012` loads it as a
shape index, and `$4B61`-`$4B79` uses it as a signed temporary in the driving model.

## 4. `$11` — `edge_nearest_hi`, and what `check_crash` reads it for

`$10`/`$11` is the nearest projected distance `road_edge_walk` keeps, and the name says so.  But
`check_crash` (`$111E`) reads `$11` after the frame's walk and does nothing at all while it is
under 2 — so the cell is doing double duty as "how close did the track come", and whether that is
the same quantity or an accident of ordering is not settled.  Settle it before anything relies on
the name: the walk's writer is `$23E7`-`$23ED`, and `place_player_in_section` reads `$10` at
`$4681`.
