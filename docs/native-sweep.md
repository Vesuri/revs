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
| **Circuit hook / SMC seam** | `horizon_half_width_at_core`, `update_camera_and_drive_state_core`, `read_driving_controls_core`, `rebuild_walk_reversed_core`, `load_section_from_segment_core`, `fill_line_attr_core` | CLAUDE.md: *a hook/SMC seam must hand over every register the 6502 has live there*, derived from the surrounding instructions, not from what Silverstone's callee reads. Each writes `cpu` immediately before `revs_track_hook(target)` and reads back what the circuit's own code left. |
| **`cpu.D` for a BCD routine** | `add_tally_to_lap_total_core`, `tally_bcd_column_core`, `lap_complete_core`, `check_car_pair_core`, `sort_cars_by_key_core`, `tick_race_timers_core` | ✅ **CLOSED 2026-09-10** — see **The BCD routines** below. The arithmetic stays decimal but goes through `src/cpu/bcd.h`; every `cpu.D = 1` is gone, and the three surviving `cpu.D = 0` writes are the routines' architectural CLDs, not the idiom. |
| **MOS / OS-call ABI** | `shift_key_commands_core`, `kbd_test_key_core`, `engine_init_core` (`cpu.S`), `mul16_by_1_5_core` (`PHA` residue at `$0100+S`) | The harness compares registers at every OS-call boundary, and a `PHA`/`PLA` pair leaves a real byte in the stack page. |
| **A documented exit publish** | `race_main_loop_core`, `emit_edge_width_offset_core`, `build_track_geometry_core`, `draw_road_core`, `clamp_and_store_steer_angle_core`, `scale_angle_in_section_core`, `enter_session_core` | The fixture declares the mask; the argument is written at the code. |

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

## THE `_core` FRONT IS CLOSED: every surviving `cpu` read is one of five argued classes

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

### The five classes, and why each one stays

Every remaining `cpu` read in `revs_native.c` is one of these, each with its argument written at
the code:

| Class | Sites | Why it is not an idiom |
|---|---|---|
| **Hook/SMC seam** | `horizon_half_width_at_core`, `scale_by_track_gradient`, `read_driving_controls_core`, `rebuild_walk_reversed_core`, `load_section_from_segment_core`, `fill_line_attr_core`, every `hook_*` | `revs_track_hook()` runs an expansion circuit's own 6502 code. The register file IS the calling convention — see CLAUDE.md's handover rule. Converting these would be a defect, not a cleanup |
| **ISR seam** | `irq1v_band_schedule` (+ `irq1v_chain_on` / `irq1v_return`) | the measured exit contract is A/X/Y restored as the interrupted code left them, A via `mos_irq_a`. Its `PUSH(cpu.X)` is a stack residue the differential compares |
| **Stack / `S`** | `engine_init_core`'s `top_level_stack = cpu.S`, `mul16_by_1_5_core`'s `mem[STACK_PAGE + cpu.S]` | C has no `S`, and the residue is compared |
| **Live flag chain** | `draw_road_core`'s `chainC`/`chainV`, `emit_edge_width_offset_core`'s `WidthExit` | the flag genuinely leaves the routine — the one sanctioned exception in CLAUDE.md |
| **6502-ABI oracle counterpart** | `mul16_signed`, `scale16_by_y`, `mul16_by_1_5`, `abs16_math`, `neg16_math`, `neg16_math_noinit`, `abs8` | ⭐ **the native path already bypasses these entirely.** `apply_angle_term_body` folds `mul16_signed`'s arithmetic into 16-bit C; `stage_lateral_speed_delta_core` uses `model_scale16` / `model_mul_1_5`; the three "native callers of `abs16_math`" are two oracle bodies plus one *comment*. They survive only so the transliteration's 6502 callers and the validation oracle still link |

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
