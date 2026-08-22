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

## `$0043` / `$0100` — two unnamed cells the place-player / spin-car twins touch

Surfaced making `place_player_in_section` and `spin_car_out` native (twins in the
`car_gap`…`tick_wheel_spin` group).  Both are still `[unnamed]` in `symbols.csv`; the twins
carry file-local defines so the code reads, but each needs a run before a global name is earned.
(A third cell in this group, `$5F40`, was settled statically and named `track_scale_saved` —
`compute_segment_scale` just holds the active track scale there for the restart path at `$6396`.)

* **`$0043`** — a one-bit sign/quadrant flag.  `place_player_in_section` `ROR`s the carry of
  `CMP #$40` into it (`$4630`, i.e. "did the section-relative angle fold past a quarter turn?")
  and later reads bit 7 with `BIT $43 / BMI` (`$466C`) to pick the sign of `car_state_1`.
  `build_track_geometry` also reads it (`BIT $43` at `$24E1`).  Two independent readers, so it is
  real shared state, not a local.  ⇒ **Settle on `make refloop`**: steer across a section and dump
  `$43`; if bit 7 tracks which side of straight-ahead the section runs, it is a direction sign and
  the name is `player_section_sign` (or similar); if it tracks a quadrant it is `..._quadrant`.

* **`$0100`** — a per-car byte array in the low part of page 1 (safe: the stack lives at
  `$01F3-$01F8`).  `spin_car_out` writes `$91` to `$0100,X` when a car is spun out; `$0F6B` writes
  the un-indexed `$0100`.  No `LDA $0100,X` reader found statically.  ⇒ **Settle by dumping
  `$0100-$0113` mid-race after a collision** (`make refloop`): watch which cars carry `$91` and for
  how long — it looks like a spin/penalty timer or state, but nothing reads it in the static map,
  so confirm there IS a reader before naming (it may be write-only per-car scratch).


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


## `$5F38` / `$5F39` / `$001D` — the front-end standings-tally control cells

Named cells `tally_bcd_column` (`$5A25`, twin #127) reads but whose meaning is not settled from the
static map:

* `$5F38` is loaded, `SEC; SBC #1`, and branched on: `==1` takes the low path (accumulate count =
  `$5F38`), otherwise it is used as a multiplier (`mul8` against the car index) or ASL'd.  It reads
  like a **standings mode / round selector** (1 = one lap? qualifying vs race?) but the three
  branches are not distinguished statically.
* `$5F39` is `CPY`'d against the car index `Y` (`BCS` = skip when `Y >= $5F39`) — a **car-count or
  cutoff** for which cars contribute to the tally.
* `$001D` (in `place_car_world_coords`'s tail, `$2A03`) is compared with `car_behind` (`$004D`) to
  gate the other-car AI branch — an **object/car index**, tenant of a zero-page scratch cell.

⚠ **Settle by dumping `$5F38`/`$5F39` on the standings screen** (`make trackmenu`, or a mid-front-end
dump) across a qualifying vs a race session and a 1-lap vs multi-lap setup: whichever setup toggles
`$5F38` names its mode, and `$5F39` should equal the number of cars shown.  `$001D`: dump it at
`$2A01` during a race with a car close behind.
