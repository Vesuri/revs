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

## The driving model's fifteen state elements — what each is PHYSICALLY

Element **8** is settled (`car_lateral_speed`, measured 2026-09-08) and **9** (`car_speed`), **2**
(the angular rate) and **3/4/5** (the rates of 0/1/2) were already `[DERIVED]`.  That leaves
**0/1** (the same velocity vector in world axes, if element 8's rotation reading generalises),
**6/7**, **$0A..$0D** (per-axle, written by `check_wheel_slip` and halved twice a frame by
`damp_and_derive_loads`) and **14** (what `model_integrate_element` adds in).

⭐ **Element 8 settled in one run and the recipe generalises**: `--peek` the pair on the reference
loop over a plain `--drive` and a `--drive --hold-steer=left`, and read the STRAIGHT phase, not the
tail — a quantity that is identically `$0000` while the car runs straight and large the moment the
wheel goes over is a lateral one, and one that tracks `car_speed` is longitudinal.  ⚠ Read only the
frames before the car leaves the track: after the reset every element is wild and the comparison
inverts.


## `drive_state` (`$002D`) — three values written, and only the `>= 2` test is understood

0 is normal driving and `>= 2` is "not under power" (elements 5..7 of `model_state` are zeroed and
`update_slip_sound` silences sound channel 3), but the engine writes exactly three values — 0 and 1
from `update_camera_and_drive_state`'s own arms at `$45BF`-`$45C9`, `$7F` from both that routine and
`check_crash`'s crash arm at `$115C` — and `$4DCB` INCs it.  So 1 is a state that behaves like
normal driving and `$7F` counts upward from a crash.
`update_camera_and_drive_state`, `$49D2` and `apply_drag_terms` are the other readers.

⭐ **Half of it is now measured** (2026-09-08, reference loop, 600 + 1500 Silverstone frames, plain
`--drive` and `--drive --hold-steer=left`): the cell holds **0 on every one of those frames** and
takes `$7F` once, from `check_crash` — so 0 and `$7F` are settled as "driving" and "crashed", and
the steady state at `$45C9` is the zero-sum arm with `spin_countdown` at `$FC` exactly as
`symbols.csv` records.  **1 is a SPIN**, not a stall or the pits: it is written only at `$45BF`,
immediately after `begin_spin_from_a`, and `begin_spin`'s other caller is
`update_grip_limits`' changed-surface arm.

⇒ What is left is provoking one.  The changed-surface arm itself **does** fire (27 of those 600
frames), so the remaining gate on the unprompted spin is `section_jump_history` bit 7 — settle by
driving until that bit is set with `grip_disturbance` at 0, then `--peek=002d,0026,0028`.

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
