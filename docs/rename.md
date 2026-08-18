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
section and barely at all on a flat straight.**  See also the `$5400`/`$5500`/`$5600` entry below,
because the coefficient's own name currently says something else entirely.

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

## `patch_target_lo` (`$5400`) / `patch_byte_0` (`$5500`) / `$5600` — named for the INSTALLER, read as PER-SECTION TRACK DATA

`$5400` and `$5500` are named for what `ModifyGameCode` does with them at circuit-install time
(patch target addresses and the bytes to write).  But **six sites in the running engine read them as
a parallel triple of per-section tables**, indexed by a section index in Y and none of them inside
the installer:

| site | reads |
|---|---|
| `$144A`-`$145C` | all three, `$5400,Y` / `$5500,Y` / `$5600,Y` in one breath |
| `$2949`-`$2953` | all three again |
| `$4536`-`$4549` | `$5400,Y`, `$5600,Y` — `update_camera_and_drive_state`'s angular relation |
| `$4612`-`$4618` | `$5500,Y` twice — the coefficient `car_state_1` is multiplied by |

Both readings can be true at once, and that is the likely answer: the track file lands at `$70DB`
and the unpack's checksum-verified swap moves it to `$5300` (`docs/static-map.md`), so this is
CIRCUIT DATA that the installer reads a patch list out of and the engine then reads geometry out
of.  If so the names are not wrong, they are one tenant of two, and the engine's tenant is the one
every twin in the road pass and the driving model will meet.
**The cheap decider is a DUMP, not more reading** — the lesson from the `$61xx` entry: print
`$5400`-`$56FF` after a circuit installs and again mid-race for two different circuits, and see
whether the bytes the six sites read are patch-list entries (short, structured, one per patch) or
one value per section (256 entries, smoothly varying).  Sizes settle it: `ModifyGameCode` applies
`n+1` patches with `n` around 54-60 per circuit, so a patch list occupies ~60 bytes and a
per-section table occupies ~40 or 120.  ⚠ Suspect a THIRD name is needed for the region as a whole
rather than renaming either tenant.

⭐ **Twins #16-#24 put two firm boundaries on the region, which narrows the dump to run.**
`load_section_triple` ($1208) reads the track file as an 8-byte-per-segment record array in TWO
parallel halves — `track_segment_lo` at `$5900` and `track_segment_hi` at `$5300` — indexed by the
same byte index `segment_count_x8` wraps, so each half is about 250 bytes and the high half ends
around `$53FA`.  `$5400`/`$5500`/`$5600` therefore sit AFTER it, not inside it, and the region is at
least four tenants deep: segment records, whatever these three are, the patch list, and
`segment_len_tbl`/`segment_data`/`track_scale` up at `$5907`-`$5A14`.  The dump above should print
`$5300`-`$5A25` whole and be read as a MAP, not as three tables.

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

## `FUN_2D17` / `FUN_2D9A` / `FUN_2E20` / `FUN_2E99` / `FUN_2FC0` / `FUN_2FD7` — the span rasteriser's six unnamed arms

Every routine `draw_road` reaches has a name except these six, and they are 553 of the subtree's
1180 bytes — the whole span-plotting core under `interp_edge`.  What is `[DERIVED]` from the
listing:

* The four big ones are **arm variants of one unrolled span walk**, selected two ways at
  `$2CDF`-`$2CF9`: an earlier branch picks the PAIR (`$2CE3`/`$2CE9` vs `$2CF3`/`$2CF9`), and the
  sign of `$86` picks the member.  Each runs `road_span_plot` four times and `road_span_plot_2`
  four times; the sign of `$86` swaps which half goes FIRST (`$2D17` and `$2E20` plot-then-plot_2,
  `$2D9A` and `$2E99` the reverse), so `$86` is a direction and the arms are the same walk mirrored.
* They differ in the per-column table their patched `BCC` offset comes from: `$3E50,X` (`$2D17`),
  `$40D0,X` (`$2D9A`), `$3ED0,X` (`$2E20`), `$3ED8,X` (`$2E99`) — see `SMC_SITES` in
  `tools/transpile.py`.
* Only the first pair calls `$2FC0`/`$2FD7`, and those two are **whole routines switched between
  "compare and continue" and "return immediately"** by `interp_edge` at `$2CAA`-`$2CB9` (opcode
  slot `$E0`/`$60`).  They are not span plotters in their own right: each is a `CPX #$80` guard
  plus `road_span_advance`.  A name has to say that they are the CONDITIONAL continuation of the
  unrolled chain, not a fifth arm.
* `$2D9A` ends `JMP $2F12` — a tail jump into the MIDDLE of `$2E99`, which is why the transpiler
  emits `FUN_2F12` as a separate entry into the same region.  Any naming must keep that entry
  visible or the region loses its second door.

⇒ `[INFERRED]` naming shape, not yet applied because the direction/pair semantics are read off
control flow only: `span_walk_fwd` / `span_walk_rev` for one pair and the same with the other
pair's distinguishing role once it is known, plus `span_advance_guard_a` / `span_advance_guard_b`
for `$2FC0`/`$2FD7`.  **What would settle the pair question cheaply:** the two call sites are
reached from different branches at `$2CEF`; bracket each with a counter under
`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` and print which one runs for near vs far spans, rather than
inventing a name for the split.
