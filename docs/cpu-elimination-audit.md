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
| 4 | Slip / sound (`clamp_slip_to_grip`, `derive_slip_reference`, `check_wheel_slip`, `store_slip_*`, `update_slip_sound`, `sound_queue`, `sound_stop_channel`, `sound_osword`) | 11 | ✅ |
| 5 | Sub-models / physics (`begin_spin_from_a`, `update_camera_and_drive_state`, `update_engine_revs`, `apply_driving_model`, `compute_car_angles`, `integrate_*`, `apply_drag_terms`, `update_grip_limits`, `rotate_*`, `stage_accum_delta`, `model_integrate_element`, `scale_by_track_gradient`, `apply_angle_term_at`, `rotate_state_pair`) | 16 | ✅ |
| 6 | Objects / signs (`build_road_sign`, `write_object_slot`, `scale_shape_vectors`, `build_sign_origin`, `note_object_contact`, `store_object_flags`, `reject_object_slot`, `draw_track_object`, `plot_object`) | 9 | ✅ |
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

## Cluster-4 lessons — inverted hysteresis, a refactor breaking unconverted callers, a per-shim exit ABI

Three catches, each found by reading the generated `__t6502` oracle and reasoning, not by guessing:

- **An inverted branch polarity passes nothing and fails cleanly.** `update_slip_sound`'s two-frame
  hysteresis is `LDA loop_counter; AND #$02; BEQ` — the C is `if ((loop_counter & 0x02u) != 0u)
  return;` (return when the bit is SET). I first wrote `== 0u`; it diverged at `sound_chan_state[3]`
  ($62C0) and in the MOS trace. The tell was a *symmetric* branch flip (sound-stop vs early-return),
  which is exactly what an AND/BEQ pair chooses between. Sabotaged deliberately (245 mismatch).

- **Refactoring a leaf's ABI silently breaks the NATIVE callers that reach it directly.** Moving
  `LDX sound_saved_x` (and the block-index ADC's C/V) out of `sound_osword_core`/`sound_queue_core`
  into the shims was correct for the shim path, but `begin_spin_from_a_core` (cluster 5, not yet
  converted) calls `sound_queue_core` DIRECTLY, so its native X stuck at the blockLow ($30) and its
  exit C/V went stale — 1000 mismatch on `begin_spin_from_a`, 2 on `update_grip_limits` above it.
  Fix: the unconverted native caller replays the shim's exit itself (`sound_queue_exit_abi(0x04u)`
  right after the direct `sound_queue_core` call). ⚠ **When a cluster's refactor narrows a leaf's
  ABI, grep for every OTHER (unconverted) caller of that leaf before believing validate is clean.**

- **Result-only relaxation proved by tracing the caller.** `clamp_slip_to_grip` and
  `update_slip_sound` are RESULT-ONLY (their whole product is mem[]): clamp's caller
  (`update_slip_sound` at $4795) does `LDA sound_chan_state[3]` next; update's callers ($46DA/$46FD)
  restore the model accumulator and reload registers before any read. `derive_slip_reference` keeps
  only `live=A+C` (the product high as a value, the declined carry both callers branch on); its N/Z/V
  are dead so the multiply is plain `revs_mulu16` with no V replay.

- **The PHP-residue recurrence now lands INSIDE a fixture, not just determinism.** `sound_stop_channel`
  (i==10) removes the caller-A PHA into a shim C local, but its ORACLE is the direct transliteration
  still executing the real PHA at $0E5A → writes $01FF (S rests at $FF). Handled with a targeted
  `stackIgnore = {0x01FF}` on that one fixture; every other oracle in the cluster enters native shims,
  so no other slot diverges. Determinism itself was byte-identical everywhere this cluster (the residue
  was overwritten before the frame-300 dump), so the $01B8..$01FF skip was not even exercised.

- **Sabotage-harness trap: an `FN=` filter that doesn't match the fixture reads as a SURVIVED defect.**
  A `sound_queue` block-index sabotage "survived" under `FN=slip` — because `FN=slip` never runs
  `sound_queue` (name has no "slip"). It failed with 1500 mismatch under `FN=sound`. The five caught
  defects (hysteresis polarity, negate sign-bit, derive throttle-decline, sound block index, store
  exit-V bit) print distinct counts 245 / 1006 / 1000 / 1500 / 999. **A surviving sabotage's FIRST
  suspect is that its fixture ran at all.**

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

## Cluster-5 lessons — an SMC-TRAP is a compared path, exit registers by value, dead scratch survivors

The physics sub-models. All 16 cores are cpu-free; the only residual `cpu.` in the cluster is
`update_camera_and_drive_state_core`'s two documented seams (the `$45CB` per-circuit hook and
`begin_spin_from_a`'s MOS OSWORD), which stay by design.

- **A self-modifying-dispatch TRAP is a REAL compared channel, not an abort.** In
  `update_camera_and_drive_state` the `$45CB` site is an `ASL`/`ROL` pair that expansion circuits
  overwrite; when the fixture randomises those opcode bytes (1 case in 10) the port calls
  `platform_smc_unhandled` and **RETURNS with registers still live** — the harness compares A/X/Y/flags
  on that path too. My first core returned a zeroed exit struct on the trap → 378 mismatch. Fix: build
  a provisional `CameraExit preSmc` from the drive_state block's live registers (A = driveNew, X =
  car_section_cursor, Y = yScale, N/Z derived from driveNew, C/V per arm) and `return preSmc` on both
  trap paths. **Before treating any early-return as "abort," check whether the harness still compares
  it.**

- **Return the escaping registers BY VALUE; the shim replays them into `cpu`.** The camera core exits
  through a final `car_speed_scaled` add whose A/C/V/N/Z plus X (=player_car) and Y (=section cursor)
  are all live to the caller. Rather than write `cpu` inside the core, it returns
  `CameraExit {AddFlags acc; uint8_t x, y;}` and the thin shim does the five `cpu.*=` marshalls. Same
  pattern as cluster-4's `EngineExit`; it keeps the core a pure function and confines the ABI to the seam.

- **N/Z of a result are derivable; C/V of an ADC are not — reconstruct them per arm.** The provisional
  exit's N (`(driveNew>>7)&1`) and Z (`driveNew==0`) come straight from the byte the block produces, in
  every arm, cpu-free. C and V do not — they belong to whichever operation set them: the spin arm reads
  `begin_spin_from_a`'s exit C/V (its MOS boundary), the countdown arm carries `abs8`'s V (its CMP #5
  kills C on both arms, so C is a constant 0 there), the others carry the drive add's own C/V. Each arm
  sets `cArm`/`vArm` explicitly.

- **`abs16_math` on the render/physics path is a plain 16-bit negate (D=0).** `apply_driving_model`'s
  speed split becomes `if (hi & 0x80) { v = -(hi:lo); ... }` — one `uint16_t`. Its scratch write
  `math_hi = original_hi` is **faithful but provably dead**: `stage_accum_delta` (the next call) opens
  with `LDA` and overwrites `math_hi` before any read, so a sabotage of THAT byte alone is unobservable
  (0 mismatch — a "no change at all" survivor, kept per the scratch-write rule). The LIVE outputs of the
  same arm — `road_speed` (the negated high byte) and `math_lo` — ARE caught (99 mismatch each). When a
  scratch-write sabotage survives, prove it is overwritten-before-read and keep the faithful write; do
  not mistake it for a fixture gap.

- **Identical mismatch counts across two sabotages can be legitimate — read the INTERLEAVE.** The 5h
  gate's noabs and rshi both read 99 (both corrupt only the negative-speed arm, of which there are
  exactly 99 cases in the stream). That is the classic stale-object tell, but the five-run sequence
  **99 / 0 / 99 / 50 / 1** proves freshness on its own: a reused object repeats one number, whereas the
  interleaved 0/50/1 can only come from real rebuilds between each. The `rm .o + binary` guard did its
  job; the coincidence is in the game logic, not the harness.

Gate (all sub-commits): full `make validate` 0-mismatch; ≥4 logic sabotages FAIL with distinct-behaviour
counts; `make determinism` + `make determinism-drive` byte-identical (the stack-scratch skip was not
exercised — no `PHA`/`PLA` removed this cluster).

## Cluster-6 lessons — a cross-cluster callee is a temporary cpu boundary, and the dependency DAG ≠ the cluster order

The object/sign chain (`store_object_flags`, `reject_object_slot`, `write_object_slot`,
`build_road_sign`, `note_object_contact`, `build_sign_origin`, `scale_shape_vectors`, `plot_object`,
`draw_track_object`) is now cpu-free typed C. All nine return their escaping state by value
(`SlotExit{a,x,y,n,z,v,c}`, `ContactExit`, `SignOriginExit`, `RejectExit`); the thin shims and every
not-yet-converted caller marshal cpu↔struct.

- **A callee in a LATER cluster is a documented cpu boundary you read at the call site, not a blocker.**
  `plot_object_core`'s two normal exits carry `plot_shape_edges`' exit Y and V, and that routine is
  **cluster 7** — still a cpu-writing `void`. So `plot_object_core` calls it and immediately captures
  `cpu.Y`/`cpu.V` at the boundary (`peY`, `v = cpu.V`), exactly as the MOS sites keep a small cpu block.
  The core is otherwise cpu-free; when cluster 7 gives `plot_shape_edges` a struct return, the boundary
  read is replaced, not rewritten. Don't reorder the whole campaign to chase a leaf — read the leaf's
  cpu exit where it is called and move on.

- **The dependency DAG (leaves→roots) is NOT the cluster numbering.** `plot_object` (cluster 6) depends
  on `plot_shape_edges` (cluster 7). Converting a root before its leaf just means the root keeps ONE
  boundary read until the leaf lands — acceptable and expected in an incremental campaign. What you must
  NOT do is assume the leaf is already a struct: grep the callee's signature before marshalling.

- **Entry registers that PASS THROUGH must become inputs.** `draw_track_object`'s empty-slot exit leaves
  entry V and C untouched (only A and the closing `arg_x` X/N/Z are written), and every path leaves entry
  Y untouched until `arg_x`. So the core takes `entryY, entryV, entryC` as args and threads them to the
  exit. The SMC-trap exit in `plot_object` is the same: `X=0`, A/N/Z from the CMP just made, C=0, and
  entry Y/V pass straight through — a COMPARED return (the SMC-random fixtures reach it), so its register
  set must be exact, not left to chance.

- **A hang is not a usable sabotage.** `plot_object`'s shape-9 re-entry sabotage (`shapeIdx = 9` instead
  of the unclamped `plot_shape`) makes the outer loop spin forever on a shape-9 case rather than diffing
  — it proves the path is reached but produces no mismatch line. Drop it; the other five logic sabotages
  (colour byte, CMP threshold, clamp bound, scale threshold, exit-C polarity) each FAIL distinct. The
  fixture excludes shape 9 for the SAME reason (the oracle spins on random shape tables) — a sabotage
  that reintroduces the spin defeats its own measurement.

- **`shift_pair_left` is a keeper.** The `ASL math_lo / ROL A` x4 idiom stays a small named helper with
  the macros inside it: it writes `math_lo` (a real scratch effect the oracle makes and the plotter
  shares) and its cpu.A/flags are dead at every draw_track_object exit. Calling it from a cpu-free core
  is the sanctioned narrow exception, like `adc_overflow`/`load_a`.

Gate (all sub-commits): full `make validate` 0-mismatch; ≥4 (here 5–6) logic sabotages FAIL with
distinct-behaviour counts (`rm .o + binary` before every build); `make determinism` +
`make determinism-drive` byte-identical (no `PHA`/`PLA` removed — the stack-scratch skip was not
exercised).
