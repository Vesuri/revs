# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

⚠⚠ **Almost everything left in this file needs a MEASUREMENT, not more reading.**  The static
evidence has been taken as far as it goes on the first five: each one names the cheap run that
settles it.  Do not add a fact-shaped name to any of those cells in the meantime — an `[INFERRED]`
row that says what is unsettled is worth more than a confident wrong name.  (The last entry, the
`math_lo`/`math_hi` ordering, is the exception: it is settled fact and an open naming DECISION.)

---

## The ARITHMETIC WINDOW's second tenants — `$0078`/`$0079`/`$008E`/`$008F`

What is left of the driving-model entry that twins #44-#86 emptied.  **Every routine and every
plain cell it listed now has a name in `disasm/symbols.csv`** — `$4610` is
`scale_by_track_gradient`, `$4DC9`/`$4DCB` are `begin_spin`/`begin_spin_from_a`, and `$0009`
`$0022` `$0026` `$0028` `$003D` `$62F0` `$62FB` are `starter_random_mask`,
`car_section_cursor`, `spin_countdown`, `spin_shake`, `engine_torque`, `camera_pitch_bias` and
`section_jump_history`.  ⚠⚠ **The score for that entry's prose was 4 wrong premises out of 13
rows** — two arms recorded backwards, one routine's evidence belonging to another, and `$44D5`
attributed to `update_camera_and_drive_state` when it is inside `compute_segment_scale`.  Every
one of the four was settled in a minute by a hex dump or a loop-bound count.  **Decide, don't
re-read.**

What genuinely remains is the SECOND-TENANT question, which is a naming DECISION and not a
missing fact:

* ⚠ **`$008E`/`$008F` are `plot_ptr3_lo`/`_hi`**, and `slip_magnitude`/`store_slip_clamped` are a
  second tenant.  The windows do not overlap (the road pass and the driving model run in
  different halves of the frame), but a twin that says `plot_ptr3_lo` while computing a slip
  magnitude is a lie; the twins use a file-local name and this is the note that says why.
* ⚠ **`$0078`** is `hypot_min_lo` to the road pass, the OUTPUT ELEMENT INDEX to
  `damp_and_derive_loads`' second loop, and the NEGATED LOAD TERM to `update_grip_limits` —
  where `$4C52`'s `ADC $78,X` reaches it for axle 0 and `$0079` for axle 1.  Three tenants.
* ⚠ **`$0079`** is `hypot_min_hi` to the road pass, the SIGN/MODE byte to `mul16_signed` and
  `apply_angle_term` (bit 7 = negate the product, bit 6 = accumulate instead of store), and that
  same load term to `update_grip_limits`.

⇒ The decision to make once, for this window and for the `point_delta` window below: a second
set of names SCOPED to each pass, or notes that record every tenancy.  Not a per-cell call.


## Seven unnamed routines in two NAMED call trees — `draw_track_object`, `read_driving_controls`

Enumerated 2026-08-18 while walking those trees; **this is an inventory, not a set of readings** —
nothing here has been looked at yet, so no name is suggested and none should be invented from the
address alone.

| Tree | Still `FUN_*` |
|---|---|
| `draw_track_object` ($2AD1) | `$1E38` |
| `read_driving_controls` ($1579) | `$15F4` (+ its `$160D` entry), `$1612` (+ `$162D`), `$1EE9`, `$1EFA`, `$1F9B`, `$63C5` |

⭐ `read_driving_controls`' cluster is chained by **tail `JMP`s across four separate regions**
($15xx → $1EE9 → $15F4 → $1EFA → $1612), which is why its tree is deep and its routines have no
obvious boundaries: naming them is one pass over that whole chain, not six independent decisions.

⚠⚠ **AND A WARNING ABOUT CALL TREES ON THIS CORPUS.** Two ways of computing them disagree, and
both are right about different things:

* the **6502 static graph** (`JSR` + tail `JMP` out of the region) is the game's call structure;
* the **generated C** has strictly more edges, from two causes — the transpiler splits a routine
  at every mid-function entry (so an internal fall-through becomes a call), and an **SMC branch
  target becomes a `switch` over every reachable label**, which turns into a call whenever one of
  those labels lives in another region.

Measured example: the C shows `column_gap_walk → $1DE5`, which makes `draw_track_object` look as
though it reaches `draw_gear_indicator` and the ADC.  It does not — `$1DE4` is an `RTS`; the edge
is the self-modified branch offset at `$1DD5` enumerating `$1DE5` as one of its 30-odd possible
targets.  **Quote the 6502 graph for "what calls what", and the C graph only for "what could the
port execute".**

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

## `$5700` / `$5800` — the LAST unnamed pages of the track-data region

⭐⭐ **The `$5400`/`$5500`/`$5600` question is SETTLED, and by the dump this entry asked for.**
`make gen` + a 64 KB `REVS_MEM_DUMP` mid-race on Silverstone (frame 300) and on Brands
(frame 120) says it in one look: the three pages are **the track's forward DIRECTION VECTOR at
each track position**, 256 entries each, and they are now `track_dir_0` / `track_dir_1` /
`track_dir_2` in `disasm/symbols.csv`.  Components 0 and 2 are the ground-plane pair, scaled so
`|(c0,c2)| = $78 = 120` entry by entry; component 1 is the small signed gradient.  Both the
installer reading and the geometry reading were true at once, exactly as this entry guessed —
`ModifyGameCode` reads the first ~20 entries of the first two pages as its patch list, and the
circuit's own hook then GENERATES the tables (BRANDS' generator is at `$54C0`, writing
`$5400,Y`/`$5500,Y`/`$5600,Y`/`$5700,Y`/`$5800,Y`).  Silverstone, being passive, ships all 256
entries as data — which is why its pages are smooth for all 256 and Brands' are smooth for 39
and 6502 code after that.

**What is still open is the other two pages.**  ⚠ CORRECTED 2026-08-18: this entry used to say
only `draw_track_object`'s tree reads them.  Neither reader is in any of the campaign's trees —
`$299D` is inside the OTHER-CAR projector (`$2937`) and `$1391` inside `$12F7` — so the decision
has no group to ride along with and has to be made on its own.

| site | reads | as |
|---|---|---|
| `$299D`-`$29A5` | `$5700,Y`, `$5800,Y` | `point_delta_sign` components 0 and 2 |
| `$1391` | `$5700,Y` | (not yet read out) |

BRANDS' generator gives the shape away: `$5800,Y` is component 0 re-signed by
`track_direction` and `$5700,Y` is component 2 re-signed AND negated — a 90-degree rotation of
the forward vector, i.e. the **across-track NORMAL**.  ⇒ `track_normal_0` / `track_normal_2`
[INFERRED from the generator].  ⚠ `$5700` cannot simply take that name: it is also
`ModifyGameCode`'s ENTRY POINT, and one address gets one row.  **The region needs a third name
for itself, not a rename of either tenant** — decide it while twinning `draw_track_object`,
which is the tree that reads these two pages.

## `math_lo`/`math_hi` (`$0074`/`$0075`) — `point_distance_hypot` fills them BACK TO FRONT

Both rows say what the names say: `$74` is the low byte of the 16-bit math accumulator and `$75`
the high byte, and that is how `mul8`, `abs16_math` and `div16by8` use them.  `point_distance_hypot`
does not.  Its far arm ($0CC6-$0CD5) stages an eighth of the larger component with **the HIGH byte
in `math_lo` and the LOW byte in `math_hi`**, and then subtracts the pair back off in that order
($0CE7 `SBC math_hi` for the low half, $0CED `SBC math_lo` for the high) — it is consistent, and it
is the opposite of the names.  `emit_edge_width_offset` uses the same two cells the ordinary way
round for the width offset, so within one road pass the pair means both things.

This is a naming question with no cheap measurement behind it, which is why it is a queue entry and
not a rename: the honest fix is probably a THIRD pair of names scoped to the road pass rather than
re-tagging the general accumulator, and that decision is worth making once — when `mul8`,
`abs16_math` and the `$2C00`-`$2FFF` span plotters are twinned and every user of the pair is
visible at once.  Until then twin #22's comment carries the warning at the point of use.

⚠ Not a defect: the transliteration and the twin agree byte for byte (`make validate
FN=point_distance_hypot`, and sabotage "the far arm forgets the -max/8 term" catches a swap).

## `point_delta_lo` / `point_delta_hi` / `point_delta_sign` (`$0080`-`$0088`) — the span rasteriser is a SECOND tenant of all nine bytes

The three arrays are named for what `build_track_geometry` puts in them: a camera-relative
delta vector, three components each.  `interp_edge` and the four span arms then reuse the same
nine bytes for something with no relation to it at all, and the notes do not say so:

| cell | as the delta vector | as the span rasteriser uses it |
|---|---|---|
| `$0082` | `point_delta_lo[2]` | the span's END scan line |
| `$0083` | `point_delta_hi[0]` | the DDA's major delta (dx) |
| `$0084` | `point_delta_hi[1]` | the minor delta (dy) |
| `$0085` | `point_delta_hi[2]` | the source-block index, 0..$2C, into `dash_block_starts` |
| `$0086` | `point_delta_sign[0]` | which of the four arms — its bit 7 picks forward or reverse |
| `$0087` | `point_delta_sign[1]` | the plotters' Y step, which becomes the `INY`/`DEY` opcode |
| `$0088` | `point_delta_sign[2]` | a two-bit rolling clip history, one bit `ROR`ed in per call |

The windows do not overlap — `build_track_geometry` finishes before `draw_road` starts — so this
is the same safe arrangement as `shared_temp_8c`, and the twins carry file-local `SPAN_*` defines
so the code reads as what it computes.  ⚠ `$0088` has a THIRD owner: `mark_line_surfaces` parks
its surface class there for the whole of its walk (`$1A98`) — and a FOURTH, found while writing
twins #58-#66: `rotate_state_pair` (`$48CB`) keeps the rotation's sign/mode byte there across its
four `apply_angle_term_at` calls, because A is needed for the mode itself.  Four owners on one
zero-page byte is the strongest argument in this file for scoped names.

⇒ The open decision is whether the nine cells get a second set of names scoped to the span pass
(as `docs/rename.md`'s `math_lo`/`math_hi` entry proposes for the arithmetic window) or whether
the notes simply record both tenancies.  Worth deciding ONCE, for both windows, rather than
twice — and the same commit should settle it for `$0074`/`$0075`.

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
