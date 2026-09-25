# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

## Two standing rules

Both learned from what this queue used to hold:

- ⚠ **Do NOT add a fact-shaped name to an unsettled cell.**  An `[INFERRED]` row that says what
  is unsettled is worth more than a confident wrong name; three entries here were open for weeks
  precisely because a plausible name had been written down first and then believed.
- ⭐ **Name the cheap run that would settle it, in the entry itself.**  Static evidence runs out;
  what closed the last entries was a reference-loop peek, a per-frame series, or — for
  `span_cap_surface_over`/`_fill` — reading the DATA the code indexes rather than the code.

## The vertical model: `drive_state` / `spin_countdown` / `spin_shake` / `begin_spin*`
The measured "spin" trajectory in `symbols.csv` is a **jump**. `drive_state` ($2D) = height above
the track (the running sum, and ≥ 2 is airborne, which is why the model's loads are zeroed there).
`spin_countdown` ($26) = vertical speed (−4 a frame is gravity; |v| ≥ 5 at touchdown bounces through
`begin_spin_from_a`, which halves it). `spin_shake` ($28) = the jump's decaying pitch term. The
trigger is `update_grip_limits`' bump conjunction (`section_jump_history`), not a loss of grip.
Moxon's map agrees (heightAboveTrack / yGravityDelta / yJumpHeight). $7F from `check_crash` is the
crash marker in the same cell.
Suggested: `car_height`, `car_vertical_speed`, `jump_pitch_shake`, `begin_bounce[_from_a]`.
Apply in the frame-rate-independence Stage 1 split (`docs/open-work.md`), which touches every reader.

## Four comments in `revs_native.c` / `revs_native_seam.h` that describe time integration where there is none
- `MS_LATERAL_SPEED` "the hand-integrated accumulator": elements 8/9 are rebuilt every frame by
  `rotate_state_0_into_8`.
- `stage_lateral_speed_delta` "the shape of a midpoint integration": these are two lever arms — the
  rear axle is checked at x−s, the front at x+1.5s.
- The steer rotations "a rotation applied one frame at a time": a wheel-frame rotation of fresh
  values.
- `damp_and_derive_loads` "a per-frame decay of four": a scale on the ground, because
  `check_wheel_slip` rewrites 10..13 every frame. It decays only airborne and on the saturation
  path.

Fix with the names above.
