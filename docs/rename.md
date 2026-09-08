# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

⚠⚠ **Everything left in this file needs a MEASUREMENT, not more reading.**  The static evidence has
been taken as far as it goes: every remaining cell already carries an `[INFERRED]` name in
`symbols.csv`, and each entry below names the cheap run that would settle it.  Do NOT add a
fact-shaped name to any of these in the meantime — an `[INFERRED]` row that says what is unsettled
is worth more than a confident wrong name.

ℹ **The scoped-name decisions are SETTLED and gone** (arithmetic window `$0078`/`$0079`/`$008E`/
`$008F`, `math_lo`/`math_hi`, and the `point_delta` window).  There is only ever one global symbol
per address, so a "second set of scoped names" was never on the table: each cell keeps its primary
name, `symbols.csv` records every tenancy in the note, and the twins carry file-local defines
(`SPAN_*`, `SLIP_MAG_LO`, the `point_distance_hypot` back-to-front comment) so the code reads as
what it computes.  Nothing to do there.

---

## `section_curve` (`$0701`) — the CURVATURE reading and the `car_flags_0` one

Field 1 of the 3-byte per-section record at `$0700-$0777`, named 2026-08-18 with twins #98-#114
and `[INFERRED]` because its two readers disagree:

* `apply_steering_assist` (`$1F5E`) takes its **low seven bits as the section's curvature** —
  clamped to 2..7, shifted up four, and used as the CEILING on the assist's gain, so a bigger
  value means a tighter corner and a weaker assist.  That is the reading the name records.
* `$2931` copies **the whole byte into `car_flags_0`** for any car whose `car_speed_scaled` is at
  least `$32`.  A curvature is not a flag byte, and nothing explains why a fast car should take one.

The single writer (`$1575`) stores either an accumulated value or `$0016` with bit 7 optionally
flipped, so bit 7 is a sign or a direction and the low seven bits are the quantity.

⇒ **Cheap settlement: `make refloop`.**  Drive one lap and dump `$0700-$0777` at a known section;
the field should track the circuit's corners if the curvature reading is right.  Then read
`car_flags_0` for an overtaking car and see whether the byte that lands there behaves like a flag
set or like a number.  Until then the name is `[INFERRED]` and this entry says why.


## `car_state_1` (`$0164,X`) / `car_state_2` (`$0178,X`) — which is ALONG and which is ACROSS

The two per-driver quantities `place_player_in_section` computes, still named by address only, and
`[INFERRED]` in both rows.  The evidence is now full and still symmetrical:

* `car_state_1` is scaled and ADDED INTO THE CAMERA's coordinate 1 by
  `update_camera_and_drive_state` (`$45D5`-`$45ED`, into `$6281`/`$6284`), so it is an offset from
  the section's own origin along one axis; `$27A4` differences it between two cars, `$2887`
  accumulates it, and race init zeroes it.
* `car_state_2` is compared BETWEEN cars at `$271E`-`$2762`, stepped by ±1 per frame by the
  other-car AI at `$28A5`-`$28DF`, seeded to `$50 EOR $FF` for all twenty drivers at `$1100`, and
  masked with `$7F` into `car_flags_0` when `$11AB` spins a car out.

Both readings fit both axes: a ±1 step per frame is either a lane index (⇒ across) or a coarse
progress counter (⇒ along), and comparing it between cars is either a side-by-side collision test
or a who-is-ahead test.  **Settle it on the reference loop, not in the listing**: park the car and
drive it dead straight, and watch which of the two moves; then steer without moving and watch
again.  Whichever changes when the car translates is the ALONG one.

⭐ **Twins #14/#15 narrowed this, and the question as posed may be malformed.**  Two things are now
`[DERIVED]`: (a) `view_origin` component 1 is the VERTICAL axis — `project_point` divides it by the
point's distance and the quotient is a scan ROW, while components 0 and 2 go to
`bearing_to_section` and become an azimuth, i.e. a column; (b) the `$45D5`-`$45ED` write is
`view_origin_lo[1] = section_coord[cursor + 1] + (car_state_1 scaled) + $AC + a term`, so
`car_state_1` reaches the camera's ELEVATION, not its ground position.
⇒ `[INFERRED]` the scale is what makes this ACROSS rather than along: `$4610` multiplies
`car_state_1` by `mem[$5500 + Y]` (a per-section byte, sign EORed with `track_direction`) through
`mul8`, and *offset × a per-section coefficient = a height change* is exactly CAMBER.  An
across-track offset on a banked section raises you; an along-track one does not.  That also makes
sense of `$27A4` differencing it between cars (side-by-side) and of the `$AC` constant reading as
nominal eye height.
⚠ Still not measured, and the reference-loop test above is still the decider — but it now has a
prediction to falsify: **`car_state_1` should change when the car is steered across a CAMBERED
section and barely at all on a flat straight.**  ⭐ The coefficient's own name is now settled and it STRENGTHENS the camber reading:
`mem[$5500 + Y]` is `track_dir_1`, MEASURED as the track's GRADIENT component — small and signed
where the two ground-plane components are ±$78 — so `$4610` (now `scale_by_track_gradient`) is
literally "an offset times the local slope".  The prediction to falsify is unchanged.

## `model_accum_lo`/`model_accum_hi` (`$62D8`/`$62E8`) — element 8 of what, physically?

Named for its ROLE (the one element `apply_driving_model` integrates by hand) rather than its
meaning, and the role is unusually visible: saved on entry, offset downward by `stage_accum_delta`
for the duration of four sub-models, restored, then advanced by 1.5x what was removed.  A quantity
that sub-models are shown at `x - v` while it really becomes `x + 1.5v` looks like a midpoint
integration of a position or a heading.  `check_wheel_slip` shifts it left 5 into
`model_state[$0A]`, `rotate_state_0_into_8` is what resolves it out of elements 0/1, and
`rotate_accum_by_steer` rotates the (8, 9) pair by the steering angle.

The same question covers the whole vector: **the fifteen sub-models all have names now, and not one
of the fifteen ELEMENTS does.**  Settle it the cheap way, one element at a time: park the car, drive
straight, then steer, and watch which elements track speed, which track heading and which are
per-axle.  `amiga/dash_state.gdb` is the pattern for the probe.

## `drive_state` (`$002D`) — three values written, and only the `>= 2` test is understood

0 is normal driving and `>= 2` is "not under power" (elements 5..7 of `model_state` are zeroed and
`update_slip_sound` silences sound channel 3), but the engine writes exactly three values — 0 and 1
from `update_camera_and_drive_state`'s own arms at `$45BF`-`$45C9`, `$7F` from both that routine and
`check_crash`'s crash arm at `$115C` — and `$4DCB` INCs it.  So 1 is a state that behaves like
normal driving and `$7F` counts upward from a crash.  Whether 1 means "stalled", "in the pits" or
"engine cranking" is unsettled, and the name is deliberately vague until it is.
`update_camera_and_drive_state`, `$49D2` and `apply_drag_terms` are the other readers.  Cheap
settlement: `make refloop`, and read the cell at the pit exit, at a stall and after a crash.

## `near_edge_first`/`near_edge_last`/`near_edge_shift` (`$0005`/`$0006`/`$0007`) are `[INFERRED]`

The reading is structural and consistent — slots 0..5 of each 40-point half are the near edge
points, both walks start at 6, `shift_near_edge_points` slides exactly five entries, `6` is the
"nothing to do" sentinel in both cells, and race init seeds both to 6 — but nothing has been
MEASURED.  Cheap confirmation on the reference loop: drive at a steady speed and watch how often
`$62F5` is set and what `$0007` reads when it is; it should equal the number of sections crossed.

## `$5700` / `$5800` — confirm the ACROSS-TRACK NORMAL reading with a dump

⭐⭐ **The `$5400`/`$5500`/`$5600` question is SETTLED and gone** — a 64 KB `REVS_MEM_DUMP` mid-race
on Silverstone (frame 300) and Brands (frame 120) proved the three pages are the track's forward
DIRECTION VECTOR per position, now `track_dir_0`/`track_dir_1`/`track_dir_2`.

The two remaining pages are `[INFERRED]` from the generator, not yet measured.  BRANDS' generator
(`$54C0`) writes `$5800,Y` as component 0 re-signed by `track_direction` and `$5700,Y` as
component 2 re-signed AND negated — a 90-degree rotation of the forward vector, i.e. the
**across-track NORMAL**.  Their readers are `$299D`-`$29A5` (into `point_delta_sign` components 0
and 2, inside the other-car projector `$2937`) and `$1391` (inside `$12F7`).

The naming DECISION is closed the same way `track_dir` closed it: one global symbol per address, so
`$5700` keeps `ModifyGameCode` (it is a code entry point) and its note records the runtime
across-track-normal tenancy; `$5800` has no row of its own — it sits inside that function region —
so the tenancy is noted on `$5700`.  There is nothing to rename.

⇒ What is left is only the same class as the other entries: **confirm the normal reading with the
same mid-race dump `track_dir` used** — dump `$5700-$58FF` on a moving Silverstone frame and check
`(c0, c2)` there is the 90-degree rotation of `track_dir`'s ground-plane pair.  Until then the
note stays `[INFERRED from the generator]`.

## `$0100` (`car_race_flags`) — the SPIN value's reader, and a name that is still `[INFERRED]`

Surfaced making `place_player_in_section` and `spin_car_out` native (twins in the
`car_gap`…`tick_wheel_spin` group).  (`$0043` left this entry as `section_quad_flags`, derived from
its two readers; `$5F40` left it as `track_scale_saved`.)

`$0100` is a per-car byte array in the low part of page 1 (safe: the stack lives at
`$01F3-$01F8`), and it is **DUAL-USE**.  (1) `spin_car_out` writes `$91` to `$0100,X` when a car
is spun out; no reader of *that* value was found in the static map, so it may be write-only
spin/penalty scratch.  (2) `sort_cars_by_key` (`$0F64`, twin #160) reuses the SAME array as a
transient stable-position scratch: `$0F6B` clears `$0100`, `$0F75` writes `$0100,X = i`, and — the
reader the earlier note said was missing — the tie-shift at `$0FA1` reads `LDA $0100,X` to copy
the previous car's position down when two keys tie.  So there IS a reader, but it is internal to
the sort's own pass, not the spin state.  The two uses are time-disjoint (sort runs at a
lap/standings boundary, spin during racing), and the row records the sort's tenancy in its note
(like `$3850`); the twin carries a `SORT_SCRATCH` file-local define.  ⇒ The name
`car_race_flags` stays `[INFERRED]` until the spin use is settled: **dump `$0100-$0113` mid-race
after a collision** (`make refloop`) and watch which cars carry `$91` and for how long.


## `span_cap_surface_a` (`$0034`) / `span_cap_surface_b` (`$0033`) — what distinguishes them, beyond which one gets used

Both are per-scan-line surface codes `interp_edge` composes for the span it is about to walk, and
the pass number sits in bits 3-5 of each.  `$2F19` picks between them on `span_swapped`, i.e. on
whether the walk was reversed.  What is NOT derived is why the two are built so differently:
`span_cap_surface_a` takes bits 3-4 of `colour_pattern_tbl[0]` and ORs `$40`;
`span_cap_surface_b` takes two scattered bits of `colour_pattern_tbl[3]` and ORs `$80`.  Bit 7 vs
bit 6 of a `view_line_surface` entry is a real distinction — `view_paint_lines` reads that array
for the line's background — so the two codes mean two different kinds of line.

⚠ **What would settle it cheaply, and it is a rendered-thing diff, not more reading**: park the
car (`make refloop --park`) and dump `view_line_surface` on a frame where the road runs uphill and
one where it runs downhill.  The `a`/`b` split is the only thing in the pass that keys off walk
direction, so whichever visual feature swaps between those two frames is what bit 6 vs bit 7 names.


## `$8B` — the one `view_line_surface` class two circuits inherit downward

Donington's and Snetterton's `$2F23` hook (`hook_span_cap_slot_test`) tests the line below the
one being capped for **exactly** `$8B` and, on a match, refuses the stamp.  `$8B` is
`10001011`: bit 7 is `span_cap_surface_b`'s marker, bit 6 is clear, and the low bits are `11`
— the one combination `$2F35`'s flatten leaves alone.  So it is a *named class*, not a mask
(the sabotage that reads it as one is S107 in that twin), and the two circuits that install the
hook are saying "a line of THIS kind claims the line above it too".

⚠ **What would settle it**: `make viewdiff CIRCUITS="2 4"` already runs those two circuits, so
dump `view_line_surface` on a frame where the hook fires (`--watch=$5F60`) and look at which
feature the suppressed line belongs to.  A name for the class belongs in `surface_colours`'
neighbourhood, and it should say what the surface IS, not that it is `$8B`.


## `$001D` — is `place_car_world_coords`' reader the same tenant as `staging_order_index`?

`$001D` carries the name `staging_order_index` (the `car_order` position `move_and_draw_cars` is
staging, written at `$2669`), but `place_car_world_coords`' tail (`$2A03`) compares it with
`car_behind` (`$004D`) to gate the other-car AI branch. That reads like the SAME index — but a zero
page scratch cell with two readers is exactly the trap, so it is an assumption until measured.
⚠ Settle it by dumping it at `$2A01` during a race with a car close behind.
⚠⚠ **The obvious cheap run does NOT settle it and was tried** (2026-09-08): over frames 40-180 of
`bbc_refloop_race --watch=001d` the ONLY write is `reset_driving_variables`' zero at `$1809` —
`move_and_draw_cars`' `$2669` never fires, because the reference loop drives a PRACTICE session
with no traffic. This needs a race with opponents staged, not another watch on the same run.

(`$5F38` and `$5F39` left this queue with twin #203: `front_end_menus` derives both statically —
`human_car_first` is the lowest human-driven car index and `human_driver_count` is `$14` minus it.)


## `$3850` — `engine_init` is only half the story (dual-use code/data overlay)

`$3850` carries ONE symbols.csv name (`engine_init`, the `JMP $3850` target at `$63BD`) but is used
two ways: as that code entry, AND as a 20-entry per-car table — `$3850,X` is the fractional/low byte
of the 16-bit car speed (high byte = `car_speed_scaled` `$0150,X`), accumulated by drive_other_cars
(`$2868`/`$286B`/`$2879`) and zeroed at race init (`$186A`). The init code's bytes are reused as the
table once init has run (a classic BBC overlay). Twin #159 references the table role via a
`CAR_SPEED_FRAC 0x3850u` #define and the row now documents both.

⚠ **The transpiler applies one name per address**, so transliterated code that indexes the table
(`$186A`, `$26ED`, `$26F1`) renders as `engine_init[X]` — misleading but not wrong (same bytes).
**What would settle a clean split**: if the code-entry bytes and the table never overlap in time
(init runs once, before any race frame), the address could carry the *table* name with a note that
`$63BD` jumps into it — verify by dumping `$3850..$3863` at `$63BD` (should be init code) vs mid-race
(should be per-car speed fractions), then rename to `car_speed_frac` with the JMP-target as the note.


## `$7B`'s second tenant, and `$85`/`$7C` inside `project_geometry`'s edge tail

Opened while naming revs_native.c's callees: the last address-shaped label in the file was `$1D94`,
and it is **not** an unnamed routine — it is a second entry into `project_geometry`
(`$1C1C-$1DEE`), reached by the `JMP $1D94` at `$1C8F`.  It joins the same tail `$1D6F` does:
`$7B != 1`, then `$7C < $28` clamps `$85` to `$28`, and `$1D86` takes `$85 - $7C` as the run to
draw.  [DERIVED] from the listing, 2026-09-06.

What that leaves open is **cell tenancy, not a function name**:

* `$7B` — `hypot_max`'s high byte in the road pass, `PVS_MODE` in `plot_view_src_line`, and this
  arm's own mode test.  The third reader is why `hypot_max`'s relocation could not free `$7A/$7B`
  (the `⚠⚠` over `hypot_max_v` in `src/gen/revs_native.c` records the split).  ⇒ A `var` note
  listing the three tenants.
* `$85` — a **run length clamped to `$28`** here, i.e. 40 units, which is the viewport's column
  count.  That is the first concrete reading of a cell the `$0C`/`$85`/`$87` entry above knew only
  as "component 1 of `shared_temp_84`'s window".  ⇒ It is the row that entry was waiting for.
* `$7C` — compared against `$28` and force-set to `$FF`, so a column index here, not
  `point_dist`'s low byte.  ⇒ Same treatment: a tenancy list, not a rename.

## The track generator's cursor blocks — `$53F8`/`$53F9`/`$53FD`, `$53FA`/`$53FB`/`$53FF`, `$5728`

Found while twinning `$5582`/`$557F` (the geometry generator's cursor step, all five expansion
circuits).  The hook walks a **(place, offset) cursor over a per-circuit run-length table**: the
block's byte 0 is the place, byte 1 the number of places, byte 5 the offset within the current
place, and `$5728 + place` holds how many offsets that place has.  Forward advances the offset
until it reaches that length and then steps the place, wrapping at the count; backward retreats
and, on underflow, steps the place back (masking bit 7 off it, wrapping onto the count) and lands
on the previous place's *last* offset.  `track_direction` bit 7 picks the sense.
[DERIVED] from the circuit bodies + `make track-patch`, 2026-09-07.

⚠ The reason this is a queue entry and not three `symbols.csv` rows: **the slot assignment differs
per circuit.** Brands Hatch, Oulton and Snetterton put the cursor at `$53F8`; Donington and the
Nurburgring put it at `$53FA` — and `$53FA` on Brands Hatch is a 24-bit coordinate accumulator, so
a single global name for either address would be wrong on two circuits.  ⇒ The rows owed are
`var` **tenancy** notes naming both readings per address, plus one `table` row for `$5728`
(per-circuit run lengths, indexed by place).  The twin carries a local `GEN_CURSOR_RUNS` define
and takes the block base as an argument until those exist.

## The generator's octant sine table — `$57BF`/`$58BF` (and `$5472`'s per-circuit scale)

Found while twinning `$5472` (the geometry generator's direction-vector store, one body in all five
expansion circuits).  The hook takes the generator's running 16-bit heading, splits it into an
**octant** (bits 6..8) and a **within-octant index 0..$40**, and reads a component pair from
`$57BF + i` and `$58BF + i`.  Those 65-entry runs are a **sine/cosine octant table of radius
`$78` = 120**: `$57BF[i] = round(120 sin(i·π/256))`, `$58BF[i] = round(120 cos(i·π/256))`, so
`i = $40` is 45° and both read `$55`, and `hypot` is exactly 120 at every entry checked.  120 is
the same radius `symbols.csv` records for `track_dir_0`'s ground-plane pair, which is what the
hook writes.  The bytes are **identical on all five circuits** — unlike `$5472`'s other two
parameters.  [MEASURED] from `src/gen/revs_tracks.c`, 2026-09-07.

⚠ These are the **tails of the `$5700`/`$5800` pages**, whose live tenants are the across-track
normal pair the same hook writes at index `dir` (≲ `$26`).  So `$5700`/`$5800` now has a *third*
reading, and the rows owed are two `table` rows at `$57BF`/`$58BF` (suggested
`gen_octant_sin` / `gen_octant_cos`) plus a tenancy note on the existing `$5700`/`$5800` entry
saying the page tail from `$BF` is constant data, not generated state.  The twin carries local
`GEN_DIR_TBL_A`/`_B` defines until they exist.

⚠⚠ `$5472`'s **other** two parameters DO differ per circuit and neither has a name: the generator
state block is `$53FA` on Brands Hatch, Oulton and Snetterton and `$53FC` on Donington and the
Nurburgring (+0/+1 the heading, +2 the gradient), and the gradient multiplier fed to
`scale_by_track_gradient_tail` is a per-circuit constant — `$88`, `$80`, `$84`, `$86`, `$9A`
respectively, i.e. the circuit's overall vertical scale.  Reading that constant off one circuit and
sharing it is exactly the mistake the hook seam invites; the byte differential caught it on the
second circuit's first case.  ⇒ owed: a `var` tenancy note per state-block address and a named
per-circuit scale constant in `src/platform/track.h` rather than five literals in the twin.

## The generator's per-segment turn/climb table — `$5428`/`$5528`/`$5628`

Found while twinning `$55C4` (one step of the track generator, on Brands Hatch, Donington, Oulton
and Snetterton).  Indexed by the *place* cursor, the three pages hold one segment's shape:
`$5428[place]`:`$5528[place]` is a **16-bit heading delta** (high byte at `$5428`, low at `$5528`)
and `$5628[place]` an **8-bit gradient delta**; `track_direction` bit 7 negates both, so driving
the circuit the other way round reverses every turn and every climb.  [DERIVED] from the circuit
bodies, 2026-09-07.

⚠ These are offsets `$28` into the `track_dir_0`/`_1`/`_2` pages, whose race-time tenants are the
*generated* direction basis indexed by `segment_dir_index` — so this is another **tenancy** on
those three pages, not a rename of them: on an expansion circuit the region from `$28` is the
circuit's own source data, read by the generator to produce the entries below it.  ⇒ owed: three
`table` rows (suggested `gen_seg_turn_hi` / `gen_seg_turn_lo` / `gen_seg_climb`) plus a tenancy
note on the `track_dir_0` row saying which index range is generated and which is source.  The twin
carries local `GEN_SEG_TURN_HI`/`_LO`/`GEN_SEG_CLIMB` defines until they exist.
