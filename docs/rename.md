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

## The DRIVING MODEL's unnamed callees — six routines and six cells, all reached from `apply_driving_model`

Opened 2026-08-18 while twinning that tree (twins #44-#66 took the multiply, the 16-bit
arithmetic and the rotations; the sub-models and the slip/sound cluster are still transliterated).
Each of these is about to be referenced by a hand-written twin, which is the last moment renaming
is cheap.  ⚠ Everything here is a READING OF THE LISTING, not a measurement — the note beside each
one says what would settle it.

**Routines** (all still `FUN_*`, so the C twin cannot be written without deciding):

| Address | What it does | Suggested | How to settle |
|---|---|---|---|
| `$4610` | `$75 = A`; `EOR $25` on `patch_byte_0[Y]` for a sign, `abs8`, `mul8`, then `abs8` again under the saved sign | `scale_section_byte` | it multiplies a PER-SECTION track byte by A — the same `$5400`/`$5500`/`$5600` question the entry below already has open |
| `$4DC9` / `$4DCB` | halve `road_speed` into `$26` and `$28`, `INC drive_state`, `SEC ROR heading_step_lo`, then `sound_queue_default(4)` | `enter_spin_from_speed` / `enter_spin` | watch `drive_state` and the audio on the reference loop while provoking a spin: sound 4 should be the tyre squeal |
| `$4B61` | `\|state[Y]\|` shifted left 5 with a `$7F` clamp, into `$8E`/`$8F` | `slip_magnitude` | it is check_wheel_slip's input; the clamp is the giveaway that it is a magnitude, not a coordinate |
| `$4B47` | clamp `(math_hi:math_lo)` against `$8E`/`$8F`, re-sign it through `abs16_math`, store into `state[Y]+$0A`/`+$1A` | `store_slip_clamped` | — |
| `$4B42` | `LDY pedal_mode; DEY; BEQ` → skip, else fall into `$4B47` | `store_slip_clamped_off_throttle` | the name only needs the polarity confirmed: `pedal_mode == 1` is the throttle |
| `$4B88` | `X + 2` into `$78`, then either the slip magnitude of element 9 scaled by `grip_limit`, or (off the throttle) `gear_index - 1` and `$3D`; returns a carry | `derive_slip_reference` | its carry is what `check_wheel_slip` branches on — bracket it and see which arm a spin takes |
| `$4AF7` | zeroes two elements, takes `$4B61`'s magnitude of element 8, flips the sign of `car_speed_hi`, then clamps through `$4B47`/`$4B88` | `settle_slip_state` | — |

**Cells** the same twins have to name:

* **`$0026` / `$0028`** — written together by `update_camera_and_drive_state` (`$44F5`-`$44F7`, both zeroed while driving normally) and by `enter_spin` (`road_speed/2` and `/4`); `$28` is added into the camera term at `$458A` and `$26` read at `$459F`/`$45B3`.  A spin's decaying camera shake is the obvious reading, and `$44EE`'s `DEC $28` twice per frame fits it.  ⇒ `spin_shake_a` / `spin_shake_b` [INFERRED] — settle by provoking a spin on the reference loop and watching which one drives the view.
* **`$0009`** — `AND`ed with the User VIA timer in `update_engine_revs`' starter poll (`$498F`) and set to 7 beside `engine_running` at `$4995`; also written at `$1160` and `$187D`.  A randomness MASK, not a value.  ⇒ `starter_random_mask` [INFERRED].
* **`$0022`** — an index: `LDX $22` then `LDY $0700,X` at `$452D`, and read the same way at `$11D4`/`$1F4E`/`$45DF`.  Whatever `$0700` is indexed BY (a car slot? a section?) is the question, and `$0700` has no row either.
* **`$003D`** — written by `update_engine_revs`' tail (`$4A87`) beside `engine_note_target`, read by `update_camera_and_drive_state` (`$4506`) and `$4B88` (`$4BBA`).  ⇒ a second rev-derived term; name it once the engine model is a twin.
* **`$62F0`** — `update_camera_and_drive_state` steps it by ±1 or ±2 and clamps it to `$FB..3` (`$44F9`-`$452A`), i.e. a small signed counter with hysteresis, driven by the pedals and the speed.  ⇒ `camera_pitch_bias` [INFERRED] — the reference loop settles it in one run: brake hard and watch the horizon.
* **`$62FB`** — `BIT $62FB` (bit 7) gates the unprompted grip loss at `$4C1C`.  ⇒ a per-session "surfaces can change" flag; find its writer first.
* ⚠ **`$008E`/`$008F` are `plot_ptr3_lo`/`_hi`, and `$4B61`/`$4B47` are a SECOND TENANT** — the same shape as the `point_delta` entry below.  The windows do not overlap (the road pass and the driving model run in different halves of the frame), but a twin that says `plot_ptr3_lo` while computing a slip magnitude is a lie; the twins use a file-local name and this is the note that says why.
* ⚠ **`$0078`** is `hypot_min_lo` to the road pass and the OUTPUT ELEMENT INDEX (1 then 0) to `damp_and_derive_loads`' second loop — the same second-tenant problem as `$0079` below, the same treatment, and it belongs in whatever decision settles the arithmetic window.
* ⚠ **`$0079`** is `hypot_min_hi` to the road pass and the SIGN/MODE byte to `mul16_signed` and `apply_angle_term` (bit 7 = negate the product, bit 6 = accumulate instead of store).  Same second-tenant problem, same treatment.


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
