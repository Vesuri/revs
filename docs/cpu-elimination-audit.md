# cpu-struct elimination — the CONVERT ledger

**Goal (user decision, 2026-08-23):** the `cpu` struct, every 6502 op (`PHP`/`PLP`/`SEC`/`CLC`/`CMP`/
`LDA`/`AND`/`PHA`/…), and every 8-bit lo/hi-lane treatment of a value that is really 16-bit are to
be **gone from `src/gen/revs_native.c`** — the file becomes pure typed C. The scope chosen was the
**full** one: not only the `_core` bodies but the 6502-ABI shims relocate out, so the grep count
reaches literally 0.

This is the sequel to the helper-elimination campaign (`docs/helper-elimination-audit.md`), which
removed the arithmetic *helpers* (`adc_step`, `sbc_step`, …) but left the register/flag plumbing.

## The two layers (survey, 2026-08-23)

`grep -c 'cpu\.' src/gen/revs_native.c` = **819** at campaign start, in two layers:

1. **Inside `_core` bodies — 62 functions, ~603 refs.** Incompletely-converted *computation*: values
   threaded through `cpu.A/X/Y`, flags set/read for control flow, `CMP`/`LDA`/`AND`/`PHA`/`PHP` macros,
   lo/hi byte-lanes of 16-bit quantities. **Convert to typed C:** inputs become args, outputs become a
   return value or a small result struct, everything else becomes named locals. Any flag/register that
   is part of the routine's *exit ABI* (checked by a fixture or read by a 6502-ABI caller/oracle) moves
   to the **shim** and is reconstructed there from the core's typed outputs — never computed in the core.

2. **In the shims — ~103 functions, ~376 refs.** The 6502-ABI seam: `void foo(void)` reads `cpu.A/X/Y`
   set by a transliterated caller in `revs_gen.c`, calls `foo_core(...)`, writes results back to `cpu`.
   Confirmed load-bearing: the plain names ARE still called from the transliterated corpus
   (`fill_object_gap()`, `mark_line_surfaces()`, … in `revs_gen.c`) via register ABI. ⚠ This bucket also
   contains **computational helpers** that merely still use `cpu` (`abs8`, `mul8_accum`, `div16by8`,
   `scale16_by_y`, `mul16_by_1_5`, `horizon_half_width_at`, …) — those are layer-1 work, not seam, and
   convert in place; they do NOT relocate.

## The MOS boundary — a documented cpu exception (user decision, 2026-08-23)

The I/O cores reach the Acorn OS through `platform_mos_call(entry)`, whose ABI is the real BBC's
OSBYTE/OSWORD **register** contract (`cpu.A/X/Y` in, `cpu.A/X/Y` + C out). The user chose to keep a
**small, localized, commented `cpu` block at each of the 7 MOS sites** rather than introduce a typed
`mos_*` wrapper. So `revs_native.c` does NOT reach literally 0 `cpu.` refs: the MOS-ABI marshalling
stays, exactly like the flag-escape exceptions — a genuine hardware boundary, not interpreter residue.
Everything ELSE around it becomes typed C, so a caller of a MOS-wrapping leaf sees a typed signature
and holds no `cpu`. Mark each kept block `/* MOS ABI — documented cpu exception */`.

⚠ **Dependency reorders the clusters:** cluster 2 (drivers) sits ATOP cluster 3 (the text/adc/kbd
leaves), so the leaves convert FIRST. `adc_read`/`kbd_test_key` are called only by the cluster-2
drivers; the text tree (`mode5_addr`→`vdu_char_*`→`draw_gear_indicator`) is pure leaves.

## Order of work

**Cores first, seam relocation last.** Each cluster's cores + computational helpers are converted to
cpu-free typed C, pushing exit-ABI reconstruction into thin shims that stay in `revs_native.c` for now.
Only once every core is cpu-free does the mechanical seam move happen (all thin shims → a new
`revs_native_seam.c`, cores made non-static behind a shared header, both backend Makefiles updated,
`docs/faithfulness-seam.md` amended). That final move is a pure code relocation, gated by
`make determinism` + `make determinism-drive` being byte-identical (no behaviour change).

## The gate (every cluster commit)

`make validate` **0 mismatch** (full, unfiltered) · ≥4 sabotages each FAIL (`rm` the `.o`+binary
before every build; `Edit` for real edits, `perl` only in throwaway sabotage scripts) · `make
determinism` + `make determinism-drive` both byte-identical vs the pre-change golden · commit staging
the touched files explicitly, no Co-Authored-By. Flag drops/replays decided by tracing the actual
caller and written at the code (V-escape rule + PHP-residue rule from the prior campaign still apply).

## Cluster checklist

| # | Cluster | cores | status |
|---|---|---|---|
| 1 | Steering / CAS (`steer_*`, `apply_steering_assist`, `poll_steering_assist`, `limit_steer_demand`, `clamp_and_store_steer_angle`, `apply_steer_demand`, `assist_from_selector`) | 10 | ✅ |
| 2 | Pedals / gears / driving-controls driver (`read_pedals_and_gears`, `read_driving_controls`) | 2 | ✅ |
| 3 | Text / screen-address (`mode5_addr`, `mode5_addr_for_cell`, `vdu_char_emit`/`_wide`/`_def`, `draw_gear_indicator`, `adc_read`) | 7 | ✅ |
| 4 | Slip / sound (`clamp_slip_to_grip`, `derive_slip_reference`, `check_wheel_slip`, `store_slip_*`, `update_slip_sound`, `sound_queue`, `sound_stop_channel`, `sound_osword`, `begin_spin_from_a`) | ~11 | ☐ |
| 5 | Sub-models / physics (`update_camera_and_drive_state`, `update_engine_revs`, `apply_driving_model`, `compute_car_angles`, `integrate_*`, `apply_drag_terms`, `update_grip_limits`, `rotate_*`, `stage_accum_delta`, `model_integrate_element`, `scale_by_track_gradient`, `apply_angle_term_at`, `rotate_state_pair`) | ~16 | ☐ |
| 6 | Objects / signs (`build_road_sign`, `write_object_slot`, `scale_shape_vectors`, `build_sign_origin`, `note_object_contact`, `store_object_flags`, `reject_object_slot`, `draw_track_object`, `plot_object`) | ~9 | ☐ |
| 7 | View pipeline (`fill_object_gap`, `plot_shape_edges`, `plot_view_src_line`, `mark_line_surfaces`, `fill_line_attr`, `fill_edge_column_run`, `column_gap_walk`, `surface_colour_at`, `view_paint_lines`, `edge_x_offscreen`, `shift_near_edge_points`, `emit_edge_width_offset`, `emit_edge_bearing`, `road_edge_walk`) | ~14 | ☐ |
| 8 | Computational helpers still on `cpu` (`abs8`, `abs16_math`, `mul8_*`, `mul16_by_1_5`, `scale16_by_y`, `div16by8`, `horizon_half_width_at`, `road_edge_side`, `derive_endpoint`, `place_car_world_coords`, `place_player_in_section`, `road_edge_walk_subdivide`, `paint_lines_short`, …) | ~20 | ☐ |
| 9 | **Seam relocation** — thin shims → `revs_native_seam.c`; shared header; Makefiles; seam doc | — | ☐ |

⚠ These groupings track the `validate_native.c` fixture groups; sub-commits split a group when it is
too large for one logical change. The counts are the survey's; they will drift as work lands.

## Cluster-1 lesson — a flag that ESCAPES may be load-bearing yet its VALUE untestable

`clamp_and_store_steer_angle_core` writes the CMP #$91 carry that leaks out through
`read_pedals_and_gears`' no_key exit as the chain's exit C. Two facts about it, both proven, that
look contradictory until you see the seam:

- **The write is load-bearing.** Remove it → ~260 fixtures fail. Once the core prefix is cpu-free
  it no longer defines C where the *transliterated oracle* prefix set it via CMP, so the leaked exit
  C diverges between the two models.
- **Its VALUE is not harness-distinguishable.** `1`, `(a >= 0x91u)`, `(a < 0x91u)` all PASS. This is
  the **oracle-shares-native-downstream cancellation**: once an oracle (`foo__t6502`) enters a native
  *shim*, the entire downstream — this core included — IS native, so whatever C it writes is applied
  identically to oracle and native and cancels in the diff. A value-sabotage there is a defect
  "unreachable by construction," not a fixture gap.

So the ≥4-sabotage gate is met by **logic** sabotages (the shift depth, the carry-in polarity, the
dispatch threshold, the STEER_KEYS test, a lamp shift) — five caught. The escaping-C value is set to
the faithful 6502 result by ARGUMENT, documented at the code, because no harness can police it. When
a sabotage on an escaping flag survives, first ask whether the oracle runs the same native code past
that point — if it does, the value cancels and the survival is expected.

## Cluster-3 lesson — eliminating a stack PUSH shifts `make determinism` by dead residue

`vdu_char_emit` preserved the caller's X/Y by **pushing them onto the 6502 hardware stack**
(`PUSH cpu.X; PUSH cpu.Y` at $509D, `PULL` at $50EF). The cpu-free core preserves them in a C local
in the shim instead, so that observable stack write vanishes. `make validate` and every observable
game state stay byte-exact, but `make determinism` (parked AND drive) diverged in **exactly one
byte** — `$01F6`, a free slot of the hardware stack — because the old push left `0x01` there and the
new path leaves an older `0x22`. It is provably dead (the 6502's own PULL pops it before any read),
but a 64K `cmp` sees it. Byte-identity is **infeasible** to restore: it needs `cpu.S/X/Y` back inside
the core (defeats the campaign) *and* the historical X/Y value, which the pure-C cores no longer track.

**Cluster-2 confirmed the recurrence.** The driver's steering-amplify decision was carried on the
6502 stack (`PHP`/`PLP` around the `$1581-$15C7` sign work); the cpu-free core holds it in the
`amplifyPressed` local instead, so the same stack-tail residue shift appeared — `make validate`
byte-exact, `make determinism`/`-drive` diverging only inside `$01B8..$01FF` and passing under the
skip. Nothing new to decide: the cluster-3 resolution below covers it verbatim. The five logic
sabotages (kbd polarity, pedal `$C8` threshold, dead-zone `$0A` carry, `STEER_SIGN` dir bit,
self-drive `revs/4+5` bias) each FAILED with distinct mismatch counts (500 / 1 / 31 / 1913 / 1375).

Resolution (user decision, 2026-08-23): **the determinism compare skips the hardware-stack scratch**
and byte-compares everything else. Page 1 is per-car data arrays up to `car_target_speed` ($01A4..
$01B7); nothing symboled lives above $01B7, so `$01B8..$01FF` is stack-only. `tools/det_compare.py`
skips that **fixed** 72-byte window (NOT an SP computation — `cpu.S` is not a reliable resting value
on the host: the transliteration calls through the C stack, so `cpu.S` only moves on explicit
PHA/PLA and reads 0 at the dump) and compares the other 65464 bytes — car tables, zero page, screen,
everything. A defect that lands only in stack-tail bytes affects no observable state by construction;
anything that matters lands in the bytes still compared. The tool is sabotaged (a diff at $0500 and
at the $01B7 boundary must FAIL; a diff inside $01B8..$01FF must be ignored). **This recurs for every
future cluster that eliminates a `PHA`/`PLA`/`PUSH`/`PULL` whose residue survives to the dump frame.**
⚠ Do NOT reach for the skip on a determinism failure until you have PROVEN the sole diff is inside
$01B8..$01FF — a diff at or below $01B7 is a real regression in live car-table / game state.
