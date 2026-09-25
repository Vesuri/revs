# The `revs_native.c` read-through — findings ledger

⚠ **A QUEUE, not a log.** An applied fix is **deleted** from this file in the commit that
applies it, exactly like `docs/rename.md`. What a defect *cost* goes in the doc that was
wrong; the fix itself is in the git history. No DONE entries.

## What this is

`src/gen/revs_native.c` is 421 functions / ~17 800 lines of hand-written twins accumulated
over ~180 twin campaigns. Every audit of it so far has been a **grep**, and the grep-visible
idiom classes are closed (gotos 0, `FUN_`/`region_` 0, literal `mem[0xNNNN]` 1, `bus_read`
of RAM 0, unnamed addresses 0). What no grep can see is the rest of the standing bar:

* **badly written C** — a 6502 shape faithfully preserved where plain C would validate the same
* **comments that describe the 6502 instructions instead of what the code computes**
* **a `_core` that exists but is reached through its 6502-ABI shim with an ambient register**
  as its argument, instead of being called with that argument
* **`mem[]` re-read where a local would do**
* **names that contradict behaviour** → `docs/rename.md`

So the sweep is a **function-by-function read-through**, in ~2000-line batches, each batch's
findings ledgered here before any of them is applied.

## How a finding is closed

| class | gate |
|---|---|
| comment / naming only | `make -s` + the determinism family |
| a `_core` call replacing a shim call | `make validate FN=<callee>` + the determinism family |
| a `bus_write` → `mem[]` on a known-RAM path | `make validate FN=<owner>` + determinism + `make viewdiff` if it is on the view pipeline |
| anything touching an expansion-circuit hook path | **`make viewdiff` is the only gate** — determinism always races Silverstone |

## ⚠ The thing this sweep must not do

**Narrowing a fixture's `live=` mask is not a cleanup — it is a change to what "faithful"
means.** Many of the 6502 shapes still in this file are there because a fixture declares
A/X/Y/flags live at the exit and the shape is what makes that true (`line_is_last`,
`cpx_ge`/`arg_x`, `stop_unchanged`, `UPD_NZ`). Each carries its argument at the code. A
read-through **records** a suspicion that a live mask is over-broad; it does not act on one
without settling who actually reads the register, which is a claim about a subtree of
callers and not about the routine in hand.

---

# Batch 1 — lines 1..2000

## Open

*(nothing outstanding in batch 1)*

## Examined and closed — do not re-open without new evidence

### The paint passes' boundary stores are NOT a `bus_write` defect
The three per-line boundary-cell stores in `paint_lines_short` / `paint_lines_clipped` go
through `bus_write(view_screen_addr(plot_ptr_v, v->cell), …)`, and next to the **unit** loop
in the same routine — which hoists `view_span_is_ram()` out of the scan into `busSafe` — they
read like an unconverted site. They are not, for two independent reasons:

1. **`bus_write` is `static inline`, and under `REVS_PLATFORM_AMIGA` its whole body is one
   range test plus `mem[addr] = val`.** There is no call and no dispatch to remove — the
   hardware-window test *is* the only cost, and it is two compares.
2. **The test cannot be hoisted anywhere cheaper.** `step_scanline` rewrites `plot_ptr_v` and
   `plot_ptr2_v` on every line, so the pointer is only known per line, and one
   `view_span_is_ram(base)` per line replaces one inlined range test per store — at one to two
   stores per line, that is a wash or worse.

⭐ The general form, because this trap will recur in later batches: **CLAUDE.md's bus-call rule
is about the ~10 600 calls a frame the plotters' INDIRECT addressing modes make, where one
hoisted test covers forty cells.** A site that stores once or twice per scan line is not that
shape, and the volume in this routine — the unit loop — was converted already. Count the
stores the hoist would cover before calling a `bus_write` a defect.

## Recorded, deliberately not acted on

### The flag-shape KEEPs, and why each survives a read-through
Every one of these reads as a 6502 idiom and is one; each is load-bearing because a fixture
declares the register or flag live at the exit, with the argument written at the code:

| site | shape | why it stays |
|---|---|---|
| `line_is_last` (4 callers) | `cpu.X = line; CPX(last); return cpu.Z` | X **and** N/Z/C are all live at `view_paint_lines`' exit; its fixture declares `LIVE_A\|X\|Y\|S\|FLAGS` |
| `stop_unchanged` | `CPY` not `==` | C is live if the plant that follows traps out; **found by sabotage** — written as `==` it passed the legal cases and failed 35 illegal ones |
| ~~`cpx_ge` + `arg_x` in the two near-slot clamps~~ | compare, then `LDX #5` after it | ✅ RESOLVED — both exits are dead; see the near-slot section below. Both helpers are now deleted |
| `cpu.V = sbc_overflow(0xF1, …)` in `paint_lines_short` | one replayed flag | V reaches `view_paint_lines`' exit on paths where nothing below rewrites it — this is the *reduced* form already (the alternative is the full `SBC` macro's five flag stores) |
| `UPD_NZ(v->cell)` | the `LDY math_hi` reload's flags | same exit contract |
| `hold_a_for_irq_seam(0x9C)` in `race_frame_tail` (was `arg_a`) | `LDA #imm` kept out of the store | ✅ CONFIRMED LIVE — A stays live across the field wait, and the port's interrupt seam publishes A into `mos_irq_a` on every field, so the value reaches `mem[$FC]`. Renamed, because "argument" was exactly the wrong word for it |
| ~~`arg_a(0x00)` in the reset tail~~ | ditto | ✅ RESOLVED — dead. `scale_wing_settings` opens `LDX #1 / LDA wing_setting_front,X` and reads no entry register or flag, so the `LDA #0` is fully consumed by `STA state_flags`. Deleted |

⭐ The general lesson for the rest of the sweep: **in this file a `cpu.` reference on the
render/dashboard path is usually an exit-ABI obligation, and the thing to check is not "can
this be plain C" but "does the fixture's live mask have evidence behind it".**

---

# Batch 2 — lines 2000..4000, plus four FILE-WIDE class sweeps

The read reached line ~2560 before it became clear that three of the sweep's classes can be
settled for the **whole file** mechanically rather than batch by batch, because each has a
shape a scan can enumerate exactly. Doing that first means the remaining per-function reading
only has to look for the classes a scan genuinely cannot see (a comment that describes 6502
instructions, a `_core` that should take an argument, a repeated `mem[]` read).

## ✅ CLASS CLOSED FILE-WIDE — `goto`
Zero in the file.

## ✅ CLASS CLOSED FILE-WIDE — unnamed hex memory locations
One `mem[0x....]` in 17 836 lines, and it was the stack page in `engine_init_core`
(`mem[0x0100u + cpu.S]`), which every other site in the file spells `STACK_PAGE`. Fixed.
Every other memory reference goes through a `mem.h` name or a named `MEM_*` base.

## ✅ CLASS CLOSED FILE-WIDE — `cpu` struct usage inside `_core` bodies
25 `_core` bodies still reference `cpu`, and **every one is load-bearing**. They fall into
exactly four groups, and each group is mandated somewhere other than this file:

| Group | Members | Why it stays |
|---|---|---|
| **Circuit hook / SMC seam** | `horizon_half_width_at_core`, `update_camera_and_height_core`, `read_driving_controls_core`, `rebuild_walk_reversed_core`, `load_section_from_segment_core`, `fill_line_attr_core` | CLAUDE.md: *a hook/SMC seam must hand over every register the 6502 has live there*, derived from the surrounding instructions, not from what Silverstone's callee reads. Each writes `cpu` immediately before `revs_track_hook(target)` and reads back what the circuit's own code left. |
| **`cpu.D` for a BCD routine** | `add_tally_to_lap_total_core`, `tally_bcd_column_core`, `lap_complete_core`, `check_car_pair_core`, `sort_cars_by_key_core`, `tick_race_timers_core` | ✅ **CLOSED 2026-09-10** — see **The BCD routines** below. The arithmetic stays decimal but goes through `src/cpu/bcd.h`; every `cpu.D = 1` is gone, and the three surviving `cpu.D = 0` writes are the routines' architectural CLDs, not the idiom. |
| **MOS / OS-call ABI** | `shift_key_commands_core`, `kbd_test_key_core`, `engine_init_core` (`cpu.S`), `mul16_by_1_5_core` (`PHA` residue at `$0100+S`) | The harness compares registers at every OS-call boundary, and a `PHA`/`PLA` pair leaves a real byte in the stack page. |
| **A documented exit publish** | `emit_edge_width_offset_core`, `build_track_geometry_core`, `draw_road_core`, `clamp_and_store_steer_angle_core`, `scale_angle_in_section_core`, `enter_session_core` | The fixture declares the mask; the argument is written at the code. |

⭐ **The general form: in this file a `cpu.` inside a `_core` is nearly always one of those four,
and the productive question is which — not whether it can be deleted.**

## ✅ CLASS LARGELY CLOSED FILE-WIDE — `bus_read`/`bus_write` on a RAM path
56 call sites. 38 are the genuine hardware window (User/System VIA, CRTC, the Video ULA
palette, the IRQ1V vector pair — which must stay `bus_write` so the platform's shadow notify
sees the claim). Of the rest, five already sit on the `else` arm of a hoisted
`view_span_is_ram()` predicate (the intended shape), and three are the paint passes' boundary
stores settled in batch 1 above.

### ✅ MEASURED and CLOSED — the two per-cell pointer walks are not the plotters' shape

Both were held open pending a call-count measurement, because CLAUDE.md's bus-call rule is about
volume and neither site's volume was known. Measured on the host over the `determinism-drive`
workload — `RELEASE=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, `REVS_FIXED_RNG=1`, 300 frames, a
counter at each site (verified by pinning one to zero: it read 0 while the other stayed put, so
the printout is that site and the run is deterministic):

| site | calls / 300 driving frames | per frame |
|---|---|---|
| `plot_line_octant_core`'s per-pixel `bus_read` + `bus_write` | 10 198 iterations | ~34 (so ~68 bus calls) |
| the `print_text_script` walk's per-byte `bus_read` | 407 | ~1.4 |

Against the ~10 600 bus calls a game frame, the needle plotter is **0.6%** and the text walk is
**0.01%**. Neither is the ~forty-cells-per-hoist shape the rule is about, and the octant site is
not even eligible: the plotter can write its own pointer cells (`plot_store_resync` is there for
exactly that), so the target can be zero page and the predicate is genuinely not constant across
the loop. ⭐ The general form, and the third time this sweep has hit it: **`bus_read` on a
per-cell walk is only a defect where the walk is LONG.** Count first.

The text walk stays hoistable in principle — its safety argument is already written at the code
(nothing the walk calls writes `plot_ptr2`) — but at 1.4 calls a frame there is nothing to buy.

---

# ✅ Closed front — THE BCD ROUTINES (user-raised; done 2026-09-10)

The `cpu.D` row in batch 2's table conflated two different claims and only the first was true:

* **True:** these six routines really do decimal arithmetic. BCD is the game's own
  representation for lap times, split times and the standings columns — not an artefact of the
  6502.
* **False, and this was the gap:** that therefore the *implementation* had to stay 6502-shaped.
  Keeping `cpu.D` set and routing the adds through the `ADC`/`SBC` macros so they consult it is
  exactly the "macros, helpers and use of the cpu struct" the sweep exists to remove. **Decimal
  mode being real behaviour is not a licence for the decimal-mode idiom.** These six were
  excluded from the cleanup on that reasoning and should not have been.

## What shipped

`src/cpu/bcd.h` — the BCD vocabulary, alongside `m68k_math.h`: `bcd_add(a, b, carryIn) ->
{val, carry, n, z, v}` and `bcd_sub(a, b, carryIn) -> {val, carry}` (carry 1 = no borrow, as on
the 6502). All **13** `adc_value`/`sbc_value` call sites in `revs_native.c` are gone, and with
them every `cpu.D = 1`:

| Routine | What changed |
|---|---|
| `add_tally_to_lap_total_core` | three `adc_value` → `bcd_add` |
| `tally_bcd_column_core` | the column loop's two `adc_value` → `bcd_add` |
| `lap_complete_core` | three `sbc_value` → `bcd_sub`; the base-60 fixup → `bcd_add(t, 0x60, 0)` |
| `tick_race_timers_core` / `add_frame_time_core` | the three race-clock columns → `bcd_add` |
| `check_car_pair_core` | the `$26DD` pass count → `bcd_add`; the `$275E` subtract → **plain binary C** |
| `sort_cars_by_key_core` | `sort_bcd_compare3`'s three `sbc_value` → `bcd_sub` |

**Gate, all green:** `make validate` (full, PASS), `make endian-lint` clean, and all five
determinism trajectories including `determinism-race`.

## ⭐⭐ The 68000's own opcodes do the digits

`ABCD` / `SBCD`, in inline asm under `__mc68000__`, with a C body for the host build. The
contract is **both operands are valid packed BCD**, which is a statement about the game, not
about the harness — and it is what licenses the opcode. Measured over all 256×256×2 inputs
against the 6502's decimal `ADC`:

* **0 disagreements on every valid-BCD input.**
* 10188 value / 1296 carry disagreements, all with a nibble in `$A..$F` (first: `$04 + $8F +
  C=1` → 6502 `$9A`/C=0, ABCD `$FA`/C=1).

⇒ on the data the engine holds, the opcode **is** the 6502. The `$A..$F` divergence is not a
reason to reimplement decimal `ADC` in software — it is a statement about inputs the game does
not produce. See §THE VALIDATION DOMAIN below for the rule that follows from it.

⚠ **The FLAGS cannot come from the opcode and don't.** An NMOS decimal `ADC` takes Z and V from
the BINARY sum and N from the PRE-correction high nibble; `ABCD` leaves N/V undefined and sets Z
from the decimal result. So `val`/`carry` come from the hardware and N/Z/V are replayed from the
operands in C. Two sites read them (`menu_wait_key`'s digit bump, `add_frame_time`'s overflow
test); everywhere else they fold away.

## ⚠⚠ ROR DOES NOT AFFECT X ON THE 68000 — and that cost the first version of this helper

`ABCD`/`SBCD` take their carry/borrow in **X**, so the helper has to get a C variable into X.
The obvious `ror.b #1,<reg>` **does not work**: ROR/ROL leave X untouched (only the shifts and
ROXR/ROXL touch it), so the carry-in never reached the opcode. It assembles, it disassembles to
exactly the instructions you meant, and it is wrong. **Use `lsr.b #1`.** Getting X back out is
`moveq #0,<d>` + `addx.b <d>,<d>` — MOVEQ leaves X alone, so it can sit anywhere before the ADDX.

## ⭐⭐ ...and the only reason that was caught: an ON-TARGET sweep

`make BCDSELFTEST=1 PROBES=1` compiles a startup sweep of all 100×100×2 valid-BCD triples
through both helpers, comparing against the digit algorithm written out independently, and
`amiga/bcd_selftest.gdb` reads the counters. **`g_bcdCases` must be 40000 and both fail counters
0.** The ROR bug read **4500 add / 10000 sub failures** — no host test could have seen it (the
host takes the C arm) and no objdump review did see it, because the emitted `abcd`/`sbcd` were
exactly right. Inline asm for this target is unverified until it has run on the target.

⚠ Its own false-zero, worth one line: a `.gdb` script for `diag_run.sh` **must `continue`**.
`diag_run.sh` attaches with the target halted and SIGINTs gdb after the delay, so a script
without `continue` reads memory before a single instruction has executed — which is
indistinguishable from a counter that was never incremented. The first run printed
`cases = 0`, and the control that settled it was reading `g_vbiCount`, which was also 0.

## ⭐⭐ THE VALIDATION DOMAIN: a fixture models the GAME, not the input space

The rule this front produced, and it is general (see `docs/validation-harness.md`):

> **A validated twin exists to be correct on the data the game produces.** `fill_random` is the
> right default for a byte the engine treats as arbitrary and the wrong one for a byte whose
> REPRESENTATION is constrained. Proving a twin correct on inputs the game cannot generate buys
> nothing, and it can cost real fidelity: here it was about to force a software reimplementation
> of NMOS decimal `ADC` in place of a one-instruction `ABCD`.

`validate_native.c` grew `rnd_bcd()` and `RND_BCD_ALL()` for it — the latter re-rolls all
seventeen BCD cells/arrays as valid digits and is called right after `fill_random` in all nine
BCD fixtures, so a new one cannot forget a table. If a real trajectory ever puts an invalid
digit in one of these, the defect is in whatever wrote it, and that is where to fix it.

## 📊 Measured: no framerate change, as expected

`STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1` + `fps_series.gdb`, 30 s warp, against a
**same-session control built from the stashed tree**:

| | row vector | non-outlier avg |
|---|---|---|
| BCD front | `4.49 4.58 4.49 4.58 [3.02] 4.58 4.49 4.58 4.58 [3.02] 4.49 4.58 4.49` | **4.54 FPS** |
| control | `4.49 4.58 4.49 4.58 [3.02] 4.58 4.49 4.58 4.58 [3.02] 4.58 4.49` | **4.54 FPS** |

Identical row for row. That is the right answer and not a disappointment: **none of these eight
routines is on the per-frame render path** — they are the race clock, the standings, the
lap-time bookkeeping and the front-end menu (`docs/static-map.md` §Decimal mode says so
explicitly). The front was a correctness-and-idiom job, and the framerate was never the claim.

## ⭐ NMOS decimal flag semantics, for the next reader

The 6502's decimal `ADC` does **not** derive its flags from the decimal result:

* **Z and V come from the BINARY sum**, not the corrected one.
* **N comes from the high nibble BEFORE its `+6` correction.**
* A decimal **`SBC`**'s carry-out is the plain **binary** borrow — decimal mode corrects only the
  accumulator's digits, never the borrow.

## What stayed, and why it is not the idiom

Three `cpu.D = 0` writes remain and are **architectural, not 6502 shape**: the `$66B4` CLD in
`add_tally_to_lap_total`, the `$0FB5` CLD in `sort_cars_by_key` and `tick_race_timers`' CLD after
the frame-time add. Each is decimal-mode state the routine *leaves for its caller* — a real side
effect. `validate_native.c` asserts the first one outright (`add_tally_to_lap_total left D set in
N cases` is a FAIL). The `SED` half has no such standing and is gone everywhere.

---

# Batch 3 — lines 4000..6000 (the span rasteriser, edge interpolation, surface colour)

## Open

*(nothing outstanding in batch 3 — all five findings applied)*

## Recorded, deliberately not acted on

- **The `$80-$88` window is multiply-tenanted, and `$0088` alone has FOUR tenants.**
  `build_track_geometry` sees `point_delta_lo/hi/sign`; the span pass sees
  `SPAN_DX/DY/BLOCK/ARM/YSTEP/CLIP/LINE_END`; `mark_line_surfaces_core` reads `$88` as a
  surface class; and `rotate_state_pair_core` (batch 4) parks the rotation's sign/mode byte
  there as `MODEL_ROT_MODE`.  The defines now spell each group off ONE base with a
  `_Static_assert` tying it to mem.h's own name for the cell, which is as far as a
  read-through can go — actually *separating* the tenants is a memory-map change, not a
  cleanup.

- **`span_walk` must not hoist `mem[arm->addend]` / `mem[arm->subtrahend]`**, and
  `column_gap_walk_core` must not hoist its boundary-table pointer.  Both are MEASURED: the
  hoist fails 3 of 400 fixture cases on each `fwd` arm, and a boundary pointer of `$005D`
  (2 of 1200 randomised cases) makes the run cover `$0082` and `$0085` — the loop's own end
  line and the column it is filling.  Self-referential by construction; leave them alone.

# Batch 4 — lines 6000..8000 (the driving model's arithmetic, slip, the sound queue)

## ⭐⭐ `mul8` was documented as one `MULU.W` and was still an eight-iteration bit loop

The engine's most-called routine (28 call sites) carried a twin-group header claiming
"ALGORITHMIC COMPRESSION … one `MULU.W`", an `#include` comment naming `revs_mulu16` as
"the 68000 op mul8 stands in for" — and a body that simulated all eight `BCC`/`ADC`/`ROR`
iterations.  Its own comment said a closed form for the escaping V was "too fragile to
trust"; the header two screens up stated that closed form exactly.  **The header was right.**

V belongs to the LAST `ADC`, at the multiplier's top set bit *k*, where the accumulator holds
`(addend * (multiplier mod 2^k)) >> k`.  Checked against a replay of `$0C02-$0C46` over **all
65536 operand pairs** for product, V and `setV` — 0 mismatches.  So the body is now one
`revs_mulu16` for the product plus one replayed add for V, and `make validate FN=mul8_noinit`
is itself the exhaustive 65536-pair gate.

**Measured, statically:** `mul8_noinit` on the Amiga went 252 → 58 instructions, 0 → 2
`mulu.w`.  No framerate is claimed (docs/perf-method.md §under 3% is noise); the compression
is the quotable figure.

⚠ The stale prose also claimed a decimal arm ("the bit-for-bit replay below is kept for it").
There is not one and there never was in this twin — none of the 8 `SED` sites reach here and
the fixture pins `D = 0` citing that table.  Removed rather than left to mislead again.

## Open

*(nothing outstanding in batch 4)*

## Examined and closed — do not re-open without new evidence

The rest of this window — `fill_column_gaps` / `fill_edge_column_run`, the 16-bit model
arithmetic (`mul16_signed`, `scale16_by_y`, `mul16_by_1_5`, `model_integrate_element`,
`add_signed_into_element`, `apply_angle_term{,_at}`), the rotations and integrations
(`stage_lateral_speed_delta`, the two steer rotations, `derive_axle_loads`,
`rotate_state_pair`, `integrate_car_position`, `integrate_state_rates`), the slip/sound
cluster (twins #67-#78) and the sub-models down to `update_grip_limits` — is already
idiomatic C: wide values are `uint16_t`/`uint32_t` words, not byte lanes; the comments say
what the code computes; the surviving `mem[]` writes are OBSERVED 6502 outputs the
differential compares, not scratch a local could replace.  One nit found and fixed:
`rotate_state_pair_core` re-READ `mem[MODEL_ROT_MODE]` twice for a byte it had just written
from its own parameter (the store stays — the differential compares it — the reads are now
the local).

⚠ Do not "simplify" these by dropping a `math_lo` / `math_hi` / `shared_temp_7x` write.  Each
one that is left is argued at the code as an output the oracle leaves behind; the twins that
dropped their scratch say so explicitly and their fixtures ignore those cells by name.

# Batch 5 — lines 8000..10125 (the engine rev model, the camera, the object plotter, the text path)

The window: `update_engine_revs`, `update_camera_and_height`, `apply_drag_terms`, the road
sign + object-slot writer (twins #87-#92), the object plotter's shape and line sides (#93-#97),
the driving-controls cluster's text/screen-address leaves (#110-#114) and the text-script
interpreter (#148/#165/#166).

## Open

*(nothing outstanding in batch 5 — all four findings applied)*

## Recorded, deliberately not acted on

- **`text_script_interp`'s hoisted hardware-window test keeps its else arm on an ARGUMENT, not
  on a fixture case.**  Every script leaf the fixture plants is in low RAM, so the `bus_read`
  fallback is never entered by the harness — but `text_script_ptr_lo/hi` are DATA tables a
  per-circuit hook could repoint, which is exactly the class CLAUDE.md says a Silverstone run
  cannot rule out.  Do not delete the arm because no case covers it.

## Examined and closed — do not re-open without new evidence

The rest of the window reads clean, and two things are worth stating so they are not
"cleaned up" later:

- **Every surviving `cpu.` reference in it is one of the five argued classes.**  `cpu.A` at
  `update_camera_and_height_core`'s `$45CB` is the SMC/hook boundary (the circuit's own code
  runs on it); the rest are 6502-ABI shims (`compute_car_angles`, `scale_by_track_gradient`,
  `begin_jump{,_from_a}`, `store_object_flags`, `plot_object`, `scale_shape_vectors`,
  `plot_shape_edges`).  No `goto`, no unnamed `mem[0x…]`, and the two remaining `bus_read`s are
  the User VIA timer in the starter poll and the coast arm — real hardware.

- **`yScale` in `update_camera_and_height_core` is NOT the section index on every path.**
  The spin arm reaches `begin_jump_from_a`, which queues a MOS sound, and `sound_osword` leaves
  the MOS's own Y — so the second `scale_by_track_gradient_core` call scales by whatever entry Y
  now points at.  A twin that "knew" the index differed in one case in six.  The variable name
  and the ⚠ at the call site both exist for that; leave them.

# Batch 6 — lines 10125..12250 (the steering assist, the road builder, the object projector)

The window: the starting-lights / horizon / gear-indicator leaves, the ADC and the whole
steering-assist chain (`poll_steering_assist` .. `clamp_and_store_steer_angle`),
`read_driving_controls` and its pedal/gear tail, the session and best-lap resets, the BCD lap
tallies, `scale_wing_settings` / `compute_segment_scale`, the section walkers
(`build_road_section`, `cross_section_boundary`, `load_section_from_segment`,
`step_section_curve`), `place_player_in_section`, `process_car_contact`, `car_gap_tail`,
`place_car_world_coords`, `tally_bcd_column`, `paint_fence_backdrop` and the car-order leaves.

## Open

*(nothing outstanding in batch 6 — all six findings applied)*

## Recorded, deliberately not acted on

- **The three road-builder hook handovers are ARGUED from the disassembly, not exercised by a
  fixture.**  `build_road_section`'s and `cross_section_boundary`'s fixtures plant `$13DA` /
  `$13E0` (the unpatched targets) or an else-arm trap, so the `$53xx` arm that now receives
  X/Y/A is reached only on a real expansion circuit — `make viewdiff` is its gate and it is
  green on all five.  Do not "simplify" the handover away because `validate` does not cover it.

## Examined and closed — do not re-open without new evidence

- **Two direct `cpu.C` writes are live-flag-chain class and each already carries its argument
  at the code**: `clamp_and_store_steer_angle_core`'s (the write is load-bearing, its value is
  not harness-distinguishable, so it is kept correct at the real 6502 `(a >= 0x91u)`) and
  `read_pedal_demand`'s dead-zone mirror.  `read_pedals_and_gears`' `cpu.V`/`cpu.X`/`cpu.Y`
  writes are the same class — `BIT`'s V and the MOS's exit X/Y genuinely leak out of the
  routine through the no-key return.

- **The `_native` suffix on `place_player_in_section_native` and `process_car_contact_native` is
  not a naming defect.**  It is the documented marshal split (the 6502-ABI entry marshals, the
  `_native` entry is what native callers use); the argument is at `build_track_geometry` in
  `revs_native_seam.c`.  Nothing for `docs/rename.md`.

- **`place_player_in_section_native`'s `PUSH`/`PULL` pair stays.**  The magnitude really is
  written to page 1 and lives there below SP; determinism is byte-exact over `$0100-$01FF`.

# Batch 7 — lines 12250..14330 (the track-position steppers, the other-car AI, the session reset, the dashboard printers)

The window: `find_player_neighbours` / `clear_race_clock`, the two track-position steppers
(`track_pos_advance` / `track_pos_retreat`), `full_track_scan_rebuild`, `lap_complete`,
`reject_all_object_slots`, `move_and_draw_cars`, `draw_car_field`, `drive_other_cars` and its
per-car body, `reset_driving_variables`, `update_lap_timers`, `stage_nearby_car`,
`check_car_pair`, `sort_cars_by_key`, `shift_key_commands`, the crash/restart subtree
(`sound_stop_all`, `begin_scrape`, `check_crash`, `build_player_car`, `step_delta_halve`),
`project_object_slot`, `mirror_draw_car`, the number/name printers (#181-#184) and the
dashboard readouts that drive them (#185-#192).

## Open

*(nothing outstanding in batch 7 — all three findings applied)*

## Examined and closed — do not re-open without new evidence

- **`shift_key_commands`' no-match ambient Y is unreachable, and the argument is now at the
  code.**  The 6502 leaves `Y = $FF` there and the twin's cpu-sourced version read `$00`; it
  cannot matter, because `pause_request` is written by nothing but this routine's own scan-apply
  and cleared again at `$0F29`, so a negative value at `$0F11` implies the idx-7 match.  The
  fixture's SAFE class deliberately keeps `pause_request` non-negative, so do not read its PASS
  as coverage of that path.

- **`track_pos_retreat_core`'s distance decrement really is a LOOP, not an `if`.**  `$14E4-$1506`
  jumps back to `$14E4` after reloading both lanes from `lap_length`, so the `while` is the
  6502's shape and a `lap_length_lo` of 0 would re-enter it.  Left as written.

- **`reset_driving_variables_core`'s two wipes stay byte loops.**  `memset` over `mem[]` would
  let the 68000 clear long words, but this is a session-reset path that runs seven times per
  crash and never in a frame — no measurement would see it, and the loops are what the `$1805`
  disassembly says.

# Batch 8 — lines 14330..16400 (the frame timers, the front end, the walk direction cluster, the per-circuit hooks)

The window: `tick_race_timers`, `retire_car`, `finish_race`, the lap-value column printers
(#196) and the two standings leaves (#197), `abort_if_quit_keys` / the dismiss waiters (#198),
`print_standings_table` + `select_text_variant` (#199), `relocated_poison`, `enter_session` /
`front_end_menus` (#205), the status-row printers (#206), `console_io` (#207), the road walk's
direction cluster (#208-#212), `clear_surface_buffers` / `fill_line_surface` (#213-#214),
`advance_player_section` (#215), `abort_to_front_end` (#216), `engine_init` / `engine_main`
(#217-#218), `hw_init` (#219), and then the per-circuit hook twins — the four steering-response
curves, the monotonic-horizon clamps and their yaw guard, the span-cap inheritance, the horizon
recorder, the edge-walk limit, the walk-back gate, the three `$5772` merge bodies and the track
generator's cursor/direction-vector hooks.

The hook half needs nothing: every `cpu` read there is the hook/SMC seam class, each entry ABI
is derived from the call site with the argument written down, and each body carries its sabotage
ledger. `goto` 0, no 6502 flag macro, no unnamed hex address, and the only `bus_*` uses are
`hw_init`'s hardware programming and `console_io`'s hoisted `page_is_ram` else-arm.

## Open

*(nothing outstanding in batch 8 — all three findings applied)*

## Examined and closed — do not re-open without new evidence

- **⚠⚠ `clear_surface_buffers`' four fills CANNOT be widened unconditionally.**  The four
  surface-edge bases are $50, $5C and $50 apart, so an entry `horizon_extent` of $50 or more
  makes buffer 0's fill run into buffer 1 — and then the 6502's per-line interleave (edge_1,
  edge_3, edge_2, edge_0) is *observable*, because a later line's edge_0 store lands on a cell
  an earlier line's edge_1 store already wrote.  Four separate `memset`s reorder that and the
  fixture caught it at case 48 (5 bytes at $055A).  The byte loop is kept as the out-of-range
  arm.  ⭐ The general shape: widening a fill is only safe while the fill stays inside the one
  region, and "the value is the same byte" is not sufficient when the regions OVERLAP.

- **`tick_race_timers_core` threading `add_frame_time`'s C and V through `cpu` is required.**
  Its next call is `seed_car_track_position`, whose 6502 ABI captures the WHOLE flag byte as a
  `PHP` residue at `$0100+S` that the differential compares — so those two flags are observable
  output, not a leftover.  The live-flag-chain class.

- **`engine_init_core`'s ten-byte `state_flags` wipe stays a byte loop.**  Once per boot, ten
  bytes; `memset` would buy nothing measurable and the loop is what `$3862` says.

- **`console_io_core`'s `bus_write` else-arm is the hoisted hardware test, not a leak.**  One
  `page_is_ram(field)` for the whole field (the column only ever indexes $00..$FF off the base),
  with the hardware arm kept — exactly the shape CLAUDE.md's `bus_read`/`bus_write` rule asks
  for.

# Batch 9 — lines 16400..16979 (the track generator's hooks and the one-line hook bodies) — NOTHING TO APPLY

The window: `hook_gen_dir_vector`, `hook_gen_step`, `hook_seg_advance`, `hook_gen_seed`,
`hook_advance_gen_place` and their five per-circuit shims apiece, then the three cross-circuit
one-line bodies (`hook_horizon_half_width_scale` + Donington's abs variant,
`hook_steer_response_nurburg`, `hook_section_ahead_doning`, `hook_abs_by_track_direction`,
`hook_scale_entry_by_gradient`).

Read in full; nothing to fix.  `goto` 0, no unnamed hex address, no `bus_*`, no `FUN_*`/`region_*`
reference, and every 16-bit quantity (the generator's heading, the segment turn) is already one
`uint16_t`.  The six surviving macro uses are the two sanctioned stack seams — the `PHP` whose
byte at `$01FF` the tail's `PLP` pulls back, and the Nurburgring curve's `PHP`/`PHA` pair that
lets the ENGINE's Z choose between two curves — argued at the code with their sabotage counts.
The `mul8` / `mul8_noinit` / `abs8` / `scale_by_track_gradient_tail` calls are 6502-ABI shims on
purpose: the engine call site consumes that whole exit ABI (A, `math_lo`, N/Z/C/V), so the shim
is the one place it is computed, the same argument `hook_camera_scale_by_gradient` carries.

⭐ This closes the read-through: batches 1-9 cover all 16 979 lines of `src/gen/revs_native.c`.
What remains on this file is the user-parked COMMENT CONDENSATION, not a findings front.

# ⭐⭐ OPEN FRONT — THE `cpu` STRUCT MUST GO (user-raised, 2026-09-12)

> "I'm still seeing a lot of cpu references in the code.  It appears that many of them are kept
> alive by the validation harness.  Lately we determined that validation should only use values
> actually used by the game instead of random data.  We want the cpu struct gone."

⚠⚠ **This SUPERSEDES §The thing this sweep must not do for the live-mask half.** A `live=` mask is
a fixture domain claim exactly like `fill_random` is, and the same rule now governs both: the mask
must describe what the GAME's callers read, not what a register happens to hold. Narrowing one is
sweep work from here on. What does NOT change is the standard of proof — narrowing still needs the
caller audit (every arm, hook re-entries included) written at the code, because `validate` passing
is not evidence when the mask is what made it pass.

## The classification (356 `cpu` lines, and they are not one problem)

⚠ Do not scope this by `grep -c 'cpu\.'` per function: a one-line shim
(`void car_index_inc(void) { cpu.X = car_index_inc_core(cpu.X); }`) gets charged to whatever
`_core` was defined above it. Attribute by what the line IS.

| Class | Lines | Shipping? | What has to happen |
|---|---:|---|---|
| **6502-ABI shim marshalling** | ~142 | **no** — **56** of them have NO native call site (the survey said 26: it could not see a one-line shim, and the first audit asked the caller question one level deep) | ✅ moved to `src/gen/revs_native_abi.c`; `make cpu-lint` keeps `revs_native.c` from regaining `cpu` |
| **Hook/SMC entry ABI** | ~104 in 23 twins + 15 dispatch sites | **yes**, on the four expansion circuits | each twin gets a TYPED core and the entry ABI is passed as arguments; `revs_track_hook` carries an explicit register struct for the oracle fallback only. ⚠ `make viewdiff` is the only gate |
| **`_core` bodies** | ~40 real (the rest was the regex artefact) | **yes** | ambient register → argument, flag chain → return value; narrow the mask first where one is what keeps it alive |
| **ISR seam** | 7 | **yes** | a small explicit register struct; `g_irqClobberCount` stays the assertion |
| **`cpu.D = 0`** | several | **yes** | ⭐ STAYS. Architectural state the routine leaves its caller, and `validate_native.c` asserts it |

⚠ The hook-seam class is NOT harness generality and must not be swept as if it were: the six
`cpu` writes at `build_track_geometry_core`'s `$2538` site are a hook ENTRY ABI, and handing `Y`
over wrong there cost one wrong horizon scan line on Oulton AND Snetterton — a defect only a real
BBC could see. Passing the register file explicitly is the fix; assuming it is dead is not.

## Order of attack

1. ✅ **the `_core` bodies that are not hook seams — DONE.** Six conversions, each with its
   caller audit at the code: the steering lock stop's leaked carry, `draw_road`'s far-half add
   (it read back flags it had just written), the race's exit carry (a return value now, and it
   is the constant 1), `shift_key_commands` (X threaded as a local, A and Y were dead stores)
   and `kbd_test_key`'s INKEY residue (17 call sites' worth of stores, three fixture masks).
   What is LEFT in a `_core` body is all one of the argued classes: a hook entry ABI, `cpu.D`,
   `cpu.S`, or the sanctioned flag-escape helpers.
2. ✅ **the hook/SMC seam — DONE.** `revs_track_hook_regs(addr, HookRegs *)` carries the entry
   ABI as a value; all 52 twins take it typed (`HOOK_TWINS_TYPED`, `tools/transpile.py`); and
   every one of the 14 dispatch sites in `revs_native.c` now seeds the whole register file from
   its own values — `hook_cpu_to_regs` appears there zero times. `cpu` is marshalled only on the
   transliteration (oracle) path. Two of the conversions were structural rather than editorial:
   `horizon_half_width_at_core`/`hook_horizon_clamp_core` took the section byte as a parameter,
   and `build_track_geometry_native` → `place_player_in_section_native` →
   `advance_player_section_core` now hand their exit index registers along the body's own call
   chain by value (`GeoExit`, `EngineRegs`), which DELETED the live-mask `cpu.A/X/Y` writes that
   existed only to feed the next call. Gated by `make viewdiff` (0 differing bytes on the road
   view of all six circuits) plus all four determinism targets, `transtrap`, `tracks`,
   `track-run`.

   ⭐⭐ **Two rules this track paid for:**
   - **Where a field is genuinely not established by the routine, hand over an argued 0 and write
     the proof — never lift it out of `cpu`.** The proof has three parts: the seam's ACTUAL hook
     targets (from `disasm/track_hooks.txt`), what those hooks read before overwriting it, and
     what overwrites the flags immediately after. Six sites resolved this way.
   - ⚠⚠ **`disasm/track_hooks.txt` lists only the bytes that CHANGE.** Where the unpatched
     instruction is already a `JSR`, only the two operand bytes move, so the extent reads
     `$128A-$128B` and grepping the table for the seam address `$1289` finds NOTHING. Three
     seams ($1289, $13C9, $1426) were briefly written up as "patched by no circuit" on exactly
     that mistake; all three are patched on all four expansion circuits. Search the extents for
     `addr+1` as well, and remember the file is a patch-extent report, not a disassembly — the
     real hook code is `src/gen/revs_track_hooks.c`.

   ⭐⭐ **...and a third, from the driver that was supposed to be the exception.** The argued-0
   rule generalises off the hook seams to any PHASE BOUNDARY inside a driver: the frame body's
   15th call (`draw_track_object` for the sign slot, `$172B`) read `cpu.Y/V/C` and published
   seven fields, justified in-code by a MEASURED note that ambient X/Y are live across phase
   boundaries. The measurement was real and the conclusion was wrong, because it named the
   producers and consumers globally instead of auditing THIS boundary: phases 16 and 17 read no
   ambient register at all, and phase 18 (`fill_dash_edge_columns`) rewrites all seven fields
   before the next reader — so the whole publish was dead, and entry V/C survive only
   `draw_track_object`'s empty-slot arm, which returns them without touching `mem[]`.
   ⚠ Entry Y was worse than dead: the 6502 has `$17` there on every path (`write_object_slot`'s
   `LDY shared_counter_42`, and the reject arms reload the same cell), while the native path had
   phase 12's leftover, because phase 14's shim publishes no register. It never showed for the
   same reason the field is unobservable — but "stale" and "dead" are different claims and only
   the second one had been argued. `race_main_loop_core` is now down to `cpu.I` (the `$4F35 CLI`).
   ⭐ **The lesson: a frame-wide liveness measurement does not settle one call's entry ABI.**
   Audit the two phases either side of the site, and reach for the SLOT sabotage to prove the
   site is even exercised (falsifying the slot fails both determinism trajectories; falsifying
   entry Y, V or C passes all three — explanation three, which IS the argument).
   ⭐⭐ **...and a fourth: THE `PHP` CLASS CONVERTS TOO, and "all 50 twins are typed" was false
   for two of them.** This item was written up as closed while `hook_steer_response_nurburg` and
   `hook_scale_entry_by_gradient` were still `void (void)` — the generated dispatch is the
   evidence and it was never read: both arms came out as
   `hook_regs_to_cpu(_r); hook_x(); hook_cpu_to_regs(_r);`, i.e. `cpu` marshalled on the
   PRODUCTION path, on five of the six circuits' gradient scalers. ⚠ **Check what the transpiler
   EMITS, not what its `HOOK_TWINS_TYPED` set contains** — a substring grep for `nurburg` matches
   five other members and reads as a hit.
   What kept them untyped was the belief that a `PHP`/`PHA` twin cannot be: the pushes are real
   bytes at `$0100+S` the differential compares, so the macro must stay. It must — but only the
   push does. `P_pack_regs`/`P_unpack_regs` + `PHP_REGS`/`PLP_REGS`/`PHA_REGS`/`PLA_REGS`
   (`src/cpu/cpu.h`) move the same byte at the same address and assemble the P from the SEAM'S
   OWN register file. `cpu.S` stays, because the residue's address genuinely is the stack
   pointer; `cpu.D`/`cpu.I` stay in the packed byte, because `HookRegs` deliberately has neither
   and the pushed byte is compared bit for bit. The third site, `hook_gen_dir_vector_at`'s
   `hook_scale_by_gradient`, went the same way — its `cpu.C`/`cpu.V` write was the one place the
   ledger called `cpu` "still right".
   ⭐ **And the flag file between two pushes is LIVE STATE, not a per-call local.** That scaler
   runs twice and the second `PHP` stacks the flags the FIRST call's tail left (`$461B` closes
   with `mul8`, plus the `$4622 abs8`'s negate on the negative arm), not the octant chain's.
   Handing the octant pair to both calls validates green on arithmetic and fails ~12% of every
   circuit's cases on the residue byte — a real defect the fixture caught, which is the evidence
   that pair is live.
   Sabotage of the new machinery (each FAILS): the pushed N cleared (2005 nurburg / 2440 scaler),
   the pulled N dropped (2430 scaler — 0 on nurburg, whose pulled N both arms overwrite), a
   `PHA_REGS` of 0 (3477 nurburg — 0 on the scaler, which has no `PHA`), and the exit Y not the
   `LDY`'s k (4000 nurburg). Each defect is caught by at least one of the pair and every zero is
   accounted for.

3. ✅ **DONE: the oracle-only shims are out, and `make cpu-lint` keeps them out.**
   **48** `void <name>(void)` shims now live in `src/gen/revs_native_abi.c` — every one whose only
   callers are `revs_gen.c` / `revs_track_hooks.c` (the oracle, which `make transtrap` proves no
   scenario executes) and `validate_native.c` (which enters a twin through its 6502 ABI on
   purpose). `revs_native.c` is down to **5 functions** that speak `cpu`, in two argued classes
   enumerated *in the lint itself* (`tools/cpu_lint.py`): the hook/SMC seam, the ISR seam, `cpu.S`
   as an ADDRESS, `cpu.D = 0` (CLD), a documented flag forward, and a shim a native caller still
   uses. A new `cpu.` reference anywhere else fails the build.

   ⭐⭐ **The audit found 12 more shims than the survey did, and the reason is worth keeping: the
   survey's scan could not see a ONE-LINE shim.** `void mul8_accum(void) { ... cpu.A = e.a; ... }`
   puts the definition and the register write on the same line, so a scanner that resolves the
   enclosing function only *after* the line's brace opens attributes the write to the function
   ABOVE — which is exactly the miscount this doc's own §classification warns about, made again
   in the tool. With that fixed the lint reattributed a `cpu.D = 0` from
   `full_track_scan_rebuild_core` to its real owner `lap_complete_core`, i.e. the broken
   attribution had also mislabelled an allowlist row. **Both fixes are in `tools/cpu_lint.py`:
   resolve the name before the check, and allow a leading indent** (a `/* promoted ... */`
   definition line strips down to an indented one).

   ⭐ **A lint's allowlist is a hole unless it is self-policing.** Every row must still name a
   function that speaks `cpu` in the file, or `cpu-lint` FAILS as stale — so the list shrinks by
   itself as the campaign closes and cannot drift into a permission slip. Seventeen rows written
   from the survey's notes were wrong (the function lives in `revs_native_seam.c`, or does not
   exist) and the staleness check is what said so, not a reading.

   ⭐⭐ **THE CALLER AUDIT IS A TRANSITIVE CLOSURE, AND ASKING IT ONE LEVEL DEEP LEFT EIGHT
   SHIMS BEHIND.** Six allowlist rows said `shim: native callers` and the callers were
   themselves shims in `revs_native_abi.c`: `mul8` is called only by the oracle and by
   `mul8_noinit`'s own entry, `abs16_math` only by `scale16_by_y`, and `div16by8` /
   `mul16_by_1_5` / `apply_angle_term` are the math helpers the twin comments called universal —
   universal to the *6502*, whose native callers now go core-to-core. Asked properly (*is this
   reachable from anything BUT an oracle-only shim?*, iterated to a fixed point over
   `revs_native.c`, `revs_native_seam.c`, `revs_native_abi.c` and every `src/platform` TU) the
   answer moved **eight** more out — those six plus `mul8_noinit` and `scale_by_track_gradient` —
   and cost **no** promotions at all: `div16by8_core` and `apply_angle_term_core` stay `static`
   behind out-of-line `*_core_oracle` wrappers (the span-leaf precedent — `div16by8_core` is the
   road pass's hottest helper at 60 calls a frame driving, and the objdump confirms it is still
   inlined into `bearing_to_section`/`project_point` with no `jsr` to it anywhere in
   `revs_native.o`, so there is nothing to measure). **56** shims, **19** functions left
   speaking `cpu`. ⚠ The six rows were not a measurement that went stale;
   they were written from a grep for the shim's name and never checked, which is the same
   failure the staleness gate was added for one level up.

   ⚠⚠ **And two of the eight were invisible to the lint for a SECOND reason: they speak the
   register file through `hook_cpu_to_regs`/`hook_regs_to_cpu`, not through `cpu.`.** That pair
   copies all seven fields in and out through a `HookRegs` local, so it is a `cpu` reference in
   every sense the campaign cares about, and `SPEAKS` had no pattern for it — `mul8_noinit` and
   `scale_by_track_gradient` read as clean C. `SPEAKS` matches it now (sabotage: a
   `hook_cpu_to_regs` call planted in `model_state_marshal_in` FAILS and names it), which is
   also why `abs8` — the one member of that trio with a real native caller,
   `scale_by_track_gradient_tail` in `revs_native_seam.c` — now has a class-6 row it did not
   need before. **A lint that enumerates an idiom must enumerate every spelling of it.**

   Sabotage of the lint (each FAILS, with the right function named): a `cpu.A = 0` inside
   `plot_shape_edges_core`; a whole fake one-line shim appended to the file; and a deleted
   allowlist row. ⚠ The one-line sabotage is the one that matters — it is the case the survey's
   own scan got wrong.

   **The cost, measured and accepted (user decision).** Twenty-five symbols had to lose `static`
   for the move (20 cores, 3 more found by the audit, 2 `SpanArm` descriptors), and GCC had been
   inlining every one of the cores away — none had a symbol in `amiga/obj/revs_native.o`. Each is
   marked `/* promoted for revs_native_abi.c */` at its definition so the next reader knows the
   `static` was not dropped by accident. ⚠⚠ **The two exceptions are the span leaves.**
   `span_plot_core` and `span_walk` stay `static inline __attribute__((always_inline))` and the
   shims reach them through out-of-line `span_plot_oracle`/`span_walk_oracle` wrappers instead:
   their `SpanPlotter`/`SpanArm` descriptor is a compile-time constant at every native call site
   and letting it become a memory operand in the rasteriser's inner loop measured 2.6% of the
   frame (`docs/perf-method.md` §twins #25-#39). A call is free on the oracle side, which is not
   on any native path.

   **Measured: 4.564 FPS against a 4.574 control** (10 vs 11 non-outlier rows,
   `STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1`, `GDBSCRIPT=fps_series.gdb` under warp, clean
   builds both sides). −0.2% is a fifth of one row's resolution, so the out-of-lining is not
   resolvable and **nothing was reverted**. The cores are confirmed to have really been forced
   out of line — `m68k-amiga-elf-objdump -t obj/revs_native.o` now carries a symbol for each,
   against a `draw_road_core` control — so this is a null result, not a change that failed to
   take effect.
4. ✅ **DONE: the ISR seam — five of its twelve `cpu` lines went, and the other seven ARE the
   interrupt's ABI.**
   What went is `irq1v_band_schedule`'s band-4 arm, which reproduced a five-register entry ABI
   (`A` = the last palette byte, `X` = $FF from the loop's DEX, `N`/`Z` from that DEX, `C` = 1
   from the dispatching `CMP #3`) for its `tick_wheel_spin` call. That had been kept with the
   note that falsifying any of the five changes NOTHING in the differential, on the grounds that
   *"the callee does not read it today" is a claim about a 400-routine subtree*.
   ⭐⭐ **It was not a subtree, and that is the lesson: the note recorded the measurement and
   never went and looked.** `tick_wheel_spin` is twin #122 — a `void (void)` whose entire body is
   a field counter, a rate accumulator and five `mem[] ^=` pairs, **with no call of any kind in
   it**. There is nothing to be wrong about. The far end agrees independently: the arm falls
   through to the tail, and `irq1v_return` overwrites X from the PULL and A from `mos_irq_a` and
   pulls the flags with `PLP`, so all five were dead at the exit too. A null measurement plus an
   unread body is not an argument; a null measurement plus BOTH ENDS CLOSED is.

   The remaining seven lines stay, and the lint (`tools/cpu_lint.py` classes 1-4) is where the
   argument now lives, because they are not residue — they are the interrupt's own ABI and there
   is no caller to thread them from. The "caller" is whatever foreground code was preempted, and
   its register file IS `cpu`:
   - `PUSH(cpu.X)` / the `PULL` in `irq1v_return` — a real byte at `$0100+S` that the
     differential compares, and its value genuinely is the preempted X the `RTI` must restore.
   - `cpu.D = 0` — the `$4E7B CLD`, architectural state left to the interrupted code.
   - `irq1v_chain_on`'s `cpu.A = 0; cpu.N = 0; cpu.Z = 1` — handed to whoever owned IRQ1V before
     us, i.e. code this port does not own, on the arm where the interrupt is not ours.
   - `irq1v_return`'s `cpu.X` / `cpu.A = mos_irq_a` / `PLP` — the `PLA/TAX/LDA $FC/RTI` exit
     contract, measured over 2858 engine-context interrupts and **asserted at the seam**
     (`g_irqClobberCount`, which stays 0 on both backends).
   ⚠ A register STRUCT here would be a rename, not a removal: its values would have to be loaded
   from and stored back to `cpu` at the boundary, which is exactly where the assertion already
   reads them (`src/platform/bbc_hw.cpp`). The seam is the one place in the port where the
   ambient 6502 register file is the actual subject of the code.

## ✅ The five that remain, and why each one is not residue

`make cpu-lint` prints `5 functions in the argued classes`, and the classes live in
`tools/cpu_lint.py` — **FOUR of the original six are now EMPTY**: the ISR seam and the class-6
shims (see §THE `_core` FRONT IS CLOSED below for where they went), and then class 5, the flag
FORWARDS, which dissolved for a single reason worth stating on its own:

⭐⭐ **EVERY "FORWARDED FLAG" TURNED OUT TO HAVE A BALANCED `PHP`/`PLP` AS ITS ONLY DESTINATION.**
`finish_race_core` handed four ambient flag bits to `tick_race_timers_core`, which handed them two
calls further down to `seed_car_track_position`'s `$6362 PHP` — a push whose own `$637B PLP` pops
it back, leaving a byte at `$0100+S` below the stack pointer that nothing else can read. The same
shape sat in `draw_dash_needles_native` (`$5145`/`$5186`) and that one ran EVERY FRAME. Both went
under §THE RESULTS RULE with the reader audit written at the code, and the collapse ran upstream:
four parameters off `tick_race_timers_core`, the whole `seed_car_track_position_with_carry` entry
point, `race_main_loop_core`'s held `WingScaleExit`, and the `$4F35 CLI` — because
`draw_dash_needles_native`'s residue was the LAST reader of `cpu.I` anywhere in the port.

⭐ **The transferable lesson, and it is the useful half:** `finish_race_core`'s own comment said
the four bits were *unprovable* — the run-out loop re-enters from two branch-backs with different
carries and its V was last written inside a transliterated subtree. That comment was true, and it
was the wrong question. **A value you cannot prove is a prompt to ask what READS it**, because an
unprovable value with no reader is not a forward at all. Two sessions were spent threading those
bits through three functions to keep them exact.

Track 2 above argues class 1, the hook/SMC seam. This is the rest.

⚠⚠ **THE ARGUMENT THIS SECTION USED TO MAKE FOR CLASS 3 WAS WRONG, and it was the most
load-bearing wrong thing in this file.** It read: *"the 6502 parks a byte with `PHA`/`PHP` and
the harness compares all 64 KB, page 1 included, so replacing the push with a C local changes a
byte the differential reads — that is the whole reason they are not `uint8_t saved = ...`."*
That is an argument about the **instrument**, not about the game, and the user's restated
governing principle settles it the other way: *validate **results**, not implementation
details.* A byte the routine's own `PULL` pops back, below SP, inside `$01B8..$01FF` — which
`tools/det_compare.py` had **already** exempted on precisely this reasoning — is an
implementation detail of a three-register machine. Four of them are gone (a scoped `set_ignore`
in the fixture, with the reader audit written at each site), and with them
`update_lap_timers_core`'s whole status-byte composition, including a live `adc_overflow` call
that existed only to feed page 1. `docs/validation-harness.md` §THE RESULTS RULE.
⭐ The transferable form: **when the only thing forcing an idiom is the harness, the harness is
the thing to change.** Same shape as §THE DOMAIN RULE on the input side.

**Class 3 — `cpu.S` read as a VALUE** (2 functions). C has no stack pointer to drop these into,
and here `S` is genuinely the subject rather than a place a byte was parked:
- `span_abandon_chain`'s `cpu.S + 2u` is the `TSX/INX/INX/TXS` two-level return **modelled as a
  value**: X really does come back as S+2 and the `$2F23` seam inherits it.
- `engine_init_core`'s `top_level_stack = cpu.S` is the `$386D TSX` — the unwind target the
  abort path longjmps to. The *value* is the subject.
⚠ Neither can be removed by threading an argument, because no caller supplies them: the stack
pointer is machine state the 6502 code computes with. ⚠ And a PUSH whose byte is popped back is
**not** a member — that ground is gone.

**Class 4 — `cpu.D = 0`** (3 sites: `sort_cars_by_key_core`, `add_tally_to_lap_total_core`,
`lap_complete_core`). The routine's own `CLD`, and it **stays** — `docs/static-map.md` §Decimal
mode enumerates all 8 `SED` sites, `validate_native.c` asserts D on exit, and D is architectural
state the routine hands back to its caller. Deleting the write is not a simplification, it is a
faithfulness bug on the three routines that clear it. ⚠ The attribution fix in the lint moved one
of these from `full_track_scan_rebuild_core` to its real owner `lap_complete_core`; the value was
never wrong, the *label* was.

**Class 5 — a documented forward of a caller's live flag** (2 sites):
- `race_main_loop_core`'s closing `cpu.I = 0` is the `$4F35 CLI`. The interrupt-disable flag is
  the one register bit the port's own ISR seam consults, so it is real state, not residue.
- `finish_race_core`'s `tick_race_timers_core(cpu.C, cpu.V, cpu.D, cpu.I)` is **the one place a
  core still reads four ambient flags, and the argument is written at the call**: unlike the
  frame body's `$1701`, this loop re-enters from two branch-backs with different carries
  (`$1196 BCC`, `$11A5 BCS`) and its V was last written somewhere inside
  `drive_other_cars`/`check_car_pair` — a transliterated subtree, not a value this routine
  establishes. They are *passed as arguments* (the core's signature is typed), which is the
  campaign's shape; what `cpu` supplies is the ambient value, and there is no caller to get it
  from because the producer is the transliteration.

**Class 6 — a 6502-ABI shim a NATIVE caller still uses** (5 functions, 35 of the 57 sites, the
whole remainder's bulk). These cannot move to `revs_native_abi.c` because something on the
native path enters them through the 6502 ABI:
- `state_flags_bit6` and `surface_colour_apply` — called from `revs_native_seam.c`
  (`race_main_loop_core`'s restart predicate; the `$2F23` surface seam).
- `abs8` — the `HookRegs` entry, called from `scale_by_track_gradient_tail`.
- `store_slip_exit_abi` (×3) and `sound_queue_exit_abi` (×5 + 2 in the abi TU) — **exit** ABIs,
  not entry ones: a twin whose 6502 tail is a `JMP` into another routine's publish sequence, and
  the seam replays it for the arm it took.
⭐ These 35 sites are where the campaign continues if it continues: each retires when its
`revs_native_seam.c` caller is itself converted to call the typed core, which is per-site work in
`docs/helper-elimination-audit.md`, not a file-wide pass. ⚠ And nothing may be *added* here
without the caller audit as a transitive closure — the lint's own six wrong rows are the
precedent.

## ⭐ What track 1 taught, and it decides how tracks 2-4 are argued

**The tell for a harness-only register is that ONLY registers diverge.** Remove the write and
run the fixture unnarrowed: if the failures are X/Y (or C) alone across thousands of cases with
**not one `mem[]` byte** differing, nothing computed anything from it — that is ABI residue.
When a `mem[]` byte DOES move, the value is live in-game and the answer is to thread it as a
local, not to narrow the mask: `shift_key_commands` failed 570 cases at `$0B46` with `live=none`
already declared, which is how its X was caught as a real dependency.

⚠ **Narrow the mask and remove the write in ONE commit.** Either half alone fails, and that is
what makes it a domain correction instead of a loosening.

⚠ **A `live=none` PASS is not proof a register is dead** — argue it from the caller. Two of
`shift_key_commands`' three writes were justified in-code by "the harness compares registers at
every OS-call boundary", which was simply not true: every callee took its arguments explicitly.

⚠ **Some sabotages cannot be run.** Inverting `kbd_test_key`'s `CPX #$FF` spins the pause loop
forever — the hang hazard that fixture's own header documents. Say so and put the unnarrowed
divergence counts in its place; do not quietly skip the step.

# Open front — THE FIXTURE LIVE MASKS (its own campaign, not part of the read-through)

ℹ ✅ **CLOSED.** `view_paint_lines`, the three NEAR-SLOT routines (which retired `cpx_ge` and
`arg_x` entirely) and the `arg_a` class are all done — see the three sections below.

The read-through kept running into the same wall: a 6502 shape that is load-bearing **only
because a fixture declares a register or flag live at the exit**. The batch-1 table lists seven
of them (`line_is_last`, `stop_unchanged`, `cpx_ge`/`arg_x`, the replayed `V` in
`paint_lines_short`, `UPD_NZ`, the two `arg_a`s). Each is correct given its mask. What none of
them has is evidence that the mask is *tight*.

⚠⚠ **This is deliberately NOT sweep work, and the sweep must keep not doing it.** Narrowing a
mask is a change to what "faithful" means for that routine, and it is a claim about a whole
subtree of callers — who actually reads A/X/Y/N/V/Z/C after this returns — not about the routine
in hand. Get that wrong and `validate` still passes while the port silently diverges on an arm
no fixture drives.

How the front would have to work, one routine at a time:

1. Enumerate the callers from the listing, not from the twin — including the hook re-entries,
   because an expansion circuit's hook can jump back into the transliteration
   (`docs/faithfulness-seam.md`) and read a register Silverstone's path never touches.
2. Show that no caller reads the register before overwriting it, on **every** arm.
3. Only then narrow the mask, and only then simplify the shape it was propping up.
4. `make viewdiff` is the gate for anything on the view pipeline — determinism always races
   Silverstone.

Expected payoff: each narrowed mask retires a `cpu.`/`arg_a`/`UPD_NZ` shape that the idiom sweep
is otherwise obliged to keep. Expected cost: the caller audit dominates, so this is worth doing
per hot routine, never as a file-wide pass.

## ✅ Done: `view_paint_lines` — `AXYS+flags` → `S`, and its whole tree is now cpu-free

The first routine through the procedure, and it took **five** of the seven ledgered shapes with
it. `view_paint_lines_core`'s tree (`view_commit`, `view_compose`, `line_is_last`,
`stop_unchanged`, `step_scanline`, `paint_cells`, `paint_lines_short`, `paint_lines_clipped`) now
contains **no `cpu.` write, no `UPD_NZ` and no `CPX`/`CPY`** — the two compares are `==`, and
`view_commit` is deleted outright because publishing A/X/Y *was* the whole function.

**The audit (step 1-2).** Exactly two callers, `$16E6` and `$1748`, taken from the listing; no
circuit's `ModifyGameCode` patches either address or `$7BE2` (`disasm/track_smc.txt` has no extent
there), so the caller set is the same on all five circuits and the audit is circuit-independent.
On every arm out of both, the first thing reached redefines what it would read:

| Register | Why it is dead |
|---|---|
| A | `$174B LDA $4F43` at caller 2; at caller 1 `BIT $05F4` only branches on V and both arms then `LDA`/`LDX` |
| X | `$16EE LDX #0`; `reset_driving_variables` `LDX #$68`; `kbd_test_key` `LDX #$FF`; `sound_stop_all` `LDX #3`; `$177B LDX #$30` |
| Y | `$178F LDY #$0B` before `$0EE5` (the one callee that reads Y, and it is redefined); `build_player_car` `LDY $22`; `finish_race` `LDY #0`; `text_script_interp` `LDY #0` |
| C | `scale_wing_settings` `CLC`/`ASL` before both `ADC`s; `kbd_test_key`, `sound_stop_channel`, `print_message_pair` read no carry |
| N/V/Z | overwritten by the `BIT`/`LDA` at each caller's first instruction |

**The empirical cross-check.** Poisoning all seven at the shim's exit
(`cpu.A=$A5, X=$5A, Y=$3C, N=V=Z=C=1`) left all five determinism trajectories **and** `make
viewdiff` on all five circuits byte-identical. ⚠ Its first positive control — `mem[0x6700] ^= 0xFF`
after the paint — **failed determinism but PASSED viewdiff**, because the frame repaints that cell
before the dump; a wrong first scan line (`$50` instead of `$4F`) fails both, and is the control
that verified the gate. *A control that the instrument erases is not a control.*

⚠ And the edit that narrows the mask must be anchored on the enclosing `test_<name>` — a
first-occurrence replace of `unsigned liveMask = LIVE_A | …` in `validate_native.c` lands on
`irq1v_case` at line 537, which then passes vacuously while the routine you meant keeps its mask.
The tell was `live=S` printing beside `[REG DIFF] … (declared live)` for X/Y/V/C.

The four value-path sabotages re-run after the narrowing (a wrong last line, `stop_unchanged`
always true, `view_compose` dropping the fill, a `$0139` row step) all FAIL, with distinct
mismatch **and** trap counts.

---

## ✅ Done: the three NEAR-SLOT routines — `AXYS+flags` → `S`, and `cpx_ge`/`arg_x` are retired

`shift_near_edge_points` ($12A0), `clamp_near_edge_window` ($12C8) and
`clamp_near_edge_cursor` ($12DC) all answer **entirely in mem[]** — the three near-slot cells
`near_edge_first` / `near_edge_last` / `near_edge_cursor`. Every register and flag at their exits
is dead, and each has exactly ONE 6502 caller, so the audit is short:

| Routine | Its one caller | What redefines the exit state |
|---|---|---|
| `shift_near_edge_points` | `$2304`, in `road_edge_start` | `$2307 LDA #0 / STA $62F5 / LDY near_edge_first / CPY #6` — A, Y and N/Z/C, before any read |
| `clamp_near_edge_window` | `$12C4`, in `shift_near_edge_points` | `$12C7 RTS` — its exit **is** `shift_near_edge_points`' exit, so the row above covers it |
| `clamp_near_edge_cursor` | `$23AF`, in `road_edge_start` | `$23B2 LDA #7 / CMP $52` — A and N/Z/C |

X is read nowhere on any arm out of `$2307` or `$23B2` before `road_edge_start`'s own
`LDX near_edge_cursor` at `$235E` writes it, and the whole road pass `$2145-$2B62` contains no
`BVS`/`BVC`, so V is dead too. `road_edge_start`'s own fixture already declared `LIVE_S`, so the
clamps were never propping up its exit — only their own masks. No circuit's `ModifyGameCode`
patches these spans (`disasm/track_smc.txt`) and no hook re-entry point (`$2490`/`$253B`/`$461B`)
lands in them, so the caller set is the same on all five circuits.

What that bought: both clamps are now plain `<`/`>=` conditionals, `shift_near_edge_points_core`
returns `void` instead of publishing an exit A the caller overwrites two instructions later, and
**`cpx_ge` and `arg_x` are deleted outright** — these five sites were their only users.

Five sabotages; three FAIL with distinct mismatch counts (61 / 254 / 1000). The two that PASS are
the **no-change** category, not a fixture gap, and the argument is written at the code: relaxing
either `<`/`>=` clamp boundary by one only adds the case where the clamp stores the value the cell
already holds. Each one's SIBLING clamp, in the same routine, does fail.

---

## ✅ Done: the `arg_a` class — one of four sites was really live, and it is not an argument

`arg_a` existed to keep a `LDA #imm` out of the store it precedes, on the claim that A is read
after the call. Audited at all four sites:

| Site | Verdict |
|---|---|
| `race_main_loop`'s crash hold, `$9C` | **LIVE.** A sits in A across the 100-field hold and the MOS IRQ entry stows it into `mos_irq_a` ($FC) every field, so it reaches `mem[]` |
| `plot_line_at_row`'s shim, `$06` | **LIVE**, and measured: dropping it moved `$00FC` from `$06` to `$00` under `make determinism` |
| `race_main_loop`'s reset tail, `$00` | **DEAD** — `scale_wing_settings` opens `LDX #1 / LDA wing_setting_front,X`, reading no entry register or flag. Deleted |
| `race_main_loop`'s shim prologue, `$00` | **AN ACTUAL ARGUMENT** — it was `copy_dash_data`'s direction flag, passed through A. Now `copy_dash_data_core(0x00u)`, and the `view_paint_lines` shim that followed it (reading `cpu.Y` = `copy_dash_data`'s exit Y) is now `view_paint_lines_core` with that offset passed explicitly |

⭐ The lesson in the naming: two of the four were genuinely live, and for a reason that has nothing
to do with argument passing — **A is an OUTPUT on this target, because the interrupt seam writes it
into `mem[$FC]`**. The helper is now called `hold_a_for_irq_seam`, which is what it does. The other
two were an argument and a dead store.

### ⚠⚠ ...and NEITHER of these two changes is gated by `make determinism`. Two controls PROVED it.

The five trajectories all pass with the change in, but that means nothing until a control fails,
and **both controls PASSED**:

| Control | Gate | Result | Why |
|---|---|---|---|
| the prologue's `view_paint_lines_core` entry cell, `+1` | `determinism` (frame 300) | **PASS — vacuous** | it is the ONE-TIME prologue paint; 300 frames of repaint erase it long before the dump |
| ...the same control | `viewdiff` (frame 60) | **PASS — vacuous** | the frame-60 dump is BYTE-FOR-BYTE the baseline's: the same 119 differing bytes and the same 15 diff-detail lines. So the third gate cannot see the entry cell either. ⚠⚠ I first read this control as "moves pixels at text/sky lines 10..17" by comparing a `tail`-TRUNCATED baseline listing against a full control listing — those lines are PRE-EXISTING ungated residuals, present in both. See `docs/validation-harness.md` §the truncated-listing trap |
| the crash hold's `hold_a_for_irq_seam(0x9C)` deleted | `determinism-crash` (frame 1500) | **PASS — vacuous** | `mem[$FC]` differs only DURING the ~100-field hold; 1000+ later frames rewrite it before the dump. (Its `$06` sibling in `plot_line_at_row` DOES fail, because that one is on the per-frame path, so `$FC` is `$06` at every frame boundary) |

That is the same shape as the `view_paint_lines` live-mask probe — **a control the instrument
erases is not a control** (`docs/validation-harness.md`) — and the honest consequence is that
these two rest on ARGUMENT, not on a gate:

* the crash-hold `LDA #$9C` is **kept**, precisely because no gate can see it: `mem[$FC]` provably
  differs mid-hold, so deleting it would be an unfaithfulness no run would report.
* the prologue refactor is **textually identical by construction** — the expression passed as the
  entry cell, `mem[MEM_dash_block_starts + (DASH_BLOCK_COUNT - 1)]`, is the very expression
  `copy_dash_data`'s shim assigned to `cpu.Y`, evaluated at the same point (after the core
  returns), and BUILD mode only reads that table. ⚠ That by-construction argument is ALL there
  is: the `+1` control passes all three gates and perturbs the frame-60 dump not at all, so no
  measurement here establishes the value is live. The refactor is safe because it is the same
  expression at the same point, not because anything checked it.
* the A/X/N/Z/C the old shim also published are dead **by the audit already completed one item
  earlier**: `copy_dash_data`'s exit state flows only into `view_paint_lines` and then into
  `$16E9 BIT state_flags`, and `view_paint_lines` is now `live=S` — the per-register table above
  is exactly the proof that every register and flag is redefined there before any read
  (`$16EE LDX #0` / `$16F9 LDA #0`, and `BIT` itself rewrites N/V/Z).

**The fixture-live-mask front is now CLOSED.**

## THE `_core` FRONT IS CLOSED: every surviving `cpu` read is one of two argued classes

The governing sweep's second bullet — *"add the `_core` function to properly pass in arguments if
needed and use the `_core` when calling the function"* — is **done**, and the survey that says so
is worth keeping because the naive version of it OVERCOUNTS BADLY.

⚠ **Do not scope this front by grepping for `<name>_core`.** A first pass looking for
`void <name>(void)` functions with no matching `<name>_core` reported **24** candidates. Nearly all
were false: a shim is just as converted when its core is *shared* or *differently named* —
`road_span_plot` / `road_span_plot_2` both delegate to `span_plot_core`, `span_end_marker_p1` /
`_p2` to `span_end_marker`, and `scale16_by_y`'s cpu-free core is `model_scale16`. Scope it instead
by asking **which function bodies READ `cpu` without delegating to a cpu-free callee**, and then
attribute every such read to its enclosing function. That reduces the front to a handful.

### The classes, and why each one stays

⚠⚠ **THE CLASSES AND THEIR MEMBERSHIP NOW LIVE IN `tools/cpu_lint.py`, AND THE LINT IS THE
AUTHORITY** — `make cpu-lint` fails the build on any `cpu` reference outside them and fails
again when a row goes stale, so the list cannot rot the way this table did. Read
§*The five that remain* above for the argument per class. The table below is kept for the
reasoning it records, **not** for its membership: every entry in its last row has since MOVED to
`src/gen/revs_native_abi.c` (tracks 3's transitive-closure audit), and `scale_by_track_gradient`
went with them.

⭐⭐ **AND THE COUNT IS NOW FIVE, IN TWO CLASSES — four of the six emptied.** Two separate
arguments closed them:

- **THE RESULTS RULE emptied the stack-residue ground.** Four `mem[STACK_PAGE + cpu.S]` /
  `PUSH`/`PULL` pairs survived on the grounds that *"the differential compares the pushed byte"*,
  and under the user's restated principle — validate **results**, not implementation details — a
  byte the routine's own `PULL` pops back and nothing outside the twin reads is not a result.
  `tools/det_compare.py` had already exempted `$01B8..$01FF` on exactly that argument; only
  `make validate`'s full-`mem[]` diff was forcing them. What earns a **Stack / `S`** row now is
  `cpu.S` read as a VALUE (`engine_init_core` hands it to `top_level_stack`, `span_abandon_chain`
  passes `cpu.S + 2u`) — not a push the oracle happens to make.
- **THE TU ROLES emptied the other two.** *"It has a native caller, so it cannot go to
  `revs_native_abi.c`"* was never an argument for it living in `revs_native.c`, whose job is
  CORES; the answer is the **third** file. The ISR seam (`irq1v_band_schedule`'s chain-on, X save
  and `PLA/TAX/LDA $FC/RTI`) and all five class-6 shims (`state_flags_bit6`,
  `surface_colour_apply`, `abs8`'s `cpu` entry, `store_slip_exit_abi`, `sound_queue_exit_abi`)
  now live in `revs_native_seam.c` beside their callers. What stayed behind is
  `irq1v_band_schedule_core`, the raster-band state machine, which touches no register at all.
  ⚠ `surface_colour_apply` could not simply move — its core and `EDGE_COLUMN` are both `static`
  to `revs_native.c`, and dropping `static` to reach a core from another TU is forbidden. So the
  **replay** moved instead: it is now the cpu-free `surface_colour_at_line_core`, and its shim
  `surface_colour_at` marshals the `SlotExit` onto `cpu`. That is the general fix when a shim
  body is pinned by file-private state.

Every remaining `cpu` read in `revs_native.c` is one of these, each with its argument written at
the code:

| Class | Sites | Why it is not an idiom |
|---|---|---|
| **Hook/SMC seam** | `horizon_half_width_at_core`, `scale_by_track_gradient`, `read_driving_controls_core`, `rebuild_walk_reversed_core`, `load_section_from_segment_core`, `fill_line_attr_core`, every `hook_*` | `revs_track_hook()` runs an expansion circuit's own 6502 code. The register file IS the calling convention — see CLAUDE.md's handover rule. Converting these would be a defect, not a cleanup |
| ~~**ISR seam**~~ | ~~`irq1v_band_schedule`~~ | ⭐ **EMPTY** — moved to `revs_native_seam.c`. The measured exit contract (A/X/Y restored as the interrupted code left them, A via `mos_irq_a`) is unchanged and still asserted by `g_irqClobberCount`; it is just no longer in a core file |
| **Stack / `S`** | `engine_init_core`'s `top_level_stack = cpu.S`, `span_abandon_chain`'s `cpu.S + 2u` | C has no `S`, and here it is an ADDRESS the routine computes with. ⚠ **NOT** "the residue is compared" — that ground is gone (THE RESULTS RULE) |
| **Live flag chain** | `draw_road_core`'s `chainC`/`chainV`, `emit_edge_width_offset_core`'s `WidthExit` | the flag genuinely leaves the routine — the one sanctioned exception in CLAUDE.md |
| ~~**6502-ABI oracle counterpart**~~ | ~~`mul16_signed`, `scale16_by_y`, `mul16_by_1_5`, `abs16_math`, `neg16_math`, `neg16_math_noinit`, `abs8`~~ ⭐ **EMPTY** — in `revs_native_abi.c` or `revs_native_seam.c` | ⭐ **the native path already bypasses these entirely.** `apply_angle_term_body` folds `mul16_signed`'s arithmetic into 16-bit C; `stage_lateral_speed_delta_core` uses `model_scale16` / `model_mul_1_5`; the three "native callers of `abs16_math`" are two oracle bodies plus one *comment*. They survive only so the transliteration's 6502 callers and the validation oracle still link |

⭐ **The consequence for `scale16_by_y`'s `PHP`/`PLP`:** that pair looked like the last real idiom
worth retiring, and retiring it would have meant loosening the differential to ignore a stack byte
the 6502 genuinely writes — a faithfulness cost. **It is moot.** Nothing native calls
`scale16_by_y`; `model_scale16` is the live path. Leave the pair alone: it is the oracle being an
oracle.

⚠ And two sites are argued the other way round — kept BECAUSE `cpu` is the honest source:
`race_main_loop_core` threads ambient X/Y across phase boundaries (measured over 300 driving
frames: three consumers read it, one storing it into `sound_saved_x` so it reaches `mem[]`), and
`shift_key_commands_core`'s `sound_envelope_core(0, cpu.X)` reads an X the pause spin's own
`kbd_test_key` left at `$FF`. In both, a threaded struct would move the same bytes and buy nothing.
