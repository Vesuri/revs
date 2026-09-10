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
| `cpx_ge` + `arg_x` in the two near-slot clamps | compare, then `LDX #5` after it | the `LDX` overwrites N and Z while leaving C, so the exit flag set is not expressible as a C conditional |
| `cpu.V = sbc_overflow(0xF1, …)` in `paint_lines_short` | one replayed flag | V reaches `view_paint_lines`' exit on paths where nothing below rewrites it — this is the *reduced* form already (the alternative is the full `SBC` macro's five flag stores) |
| `UPD_NZ(v->cell)` | the `LDY math_hi` reload's flags | same exit contract |
| `arg_a(0x9C)` in `race_frame_tail` | `LDA #imm` kept out of the store | A stays live across the field wait, and the port's interrupt seam publishes A into `mos_irq_a` on every field — a reader the oracle's store-immediate peephole cannot see |
| `arg_a(0x00)` / `arg_a(0x20)` in the tail | ditto | the following code reads A ("either way A is now 0") |

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
| **`cpu.D` for a BCD routine** | `add_tally_to_lap_total_core`, `tally_bcd_column_core`, `lap_complete_core`, `check_car_pair_core`, `sort_cars_by_key_core`, `tick_race_timers_core` | Decimal mode is real *behaviour* here — six of the eight `SED` sites inventoried in `docs/static-map.md` §Decimal mode. ⚠⚠ But see **The BCD front** below: real behaviour does NOT license the 6502 *idiom*, and this row was used as if it did. |
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

### Open — the two per-cell pointer walks
Two sites are the shape CLAUDE.md's ~10 600-calls-a-frame rule is actually about, and neither
is hoisted:

* **`plot_line_octant_core` (~2896/2901)** — a `bus_read` **and** a `bus_write` per plotted
  pixel, through `plot_ptr_v + y`. ⚠ Not a simple hoist: the line immediately below is
  `plot_store_resync(addr, out)`, there because *the plotter can write its own pointer cells* —
  the target can be zero page, so the predicate is not constant across the loop.
* **`print_text_script` walk (~10669)** — `bus_read((plot_ptr2_v + y))` per script byte, with
  `plot_ptr2_v` reloaded per table entry. This one IS hoistable per entry.

⚠ Neither should be touched before its call volume is MEASURED — batch 1's retraction above is
exactly the trap. The needle plotter runs a handful of short lines a frame and the text walk is
front-end only, so both may be far off the plotters' volume shape.

---

# ⚠⚠ Open front — THE BCD ROUTINES (user-raised; do this as its own pass)

The `cpu.D` row in batch 2's table conflated two different claims and only the first is true:

* **True:** these six routines really do decimal arithmetic. BCD is the game's own
  representation for lap times, split times and the standings columns — not an artefact of the
  6502.
* **False, and this is the gap:** that therefore the *implementation* has to stay 6502-shaped.
  Keeping `cpu.D` set and routing the adds through the `ADC`/`SBC` macros so they consult it is
  exactly the "macros, helpers and use of the cpu struct" the sweep exists to remove. **Decimal
  mode being real behaviour is not a licence for the decimal-mode idiom.** These six were
  excluded from the cleanup on that reasoning and should not have been.

What the pass has to do:

1. **Use the 68000's own BCD instructions where they apply.** `ABCD` / `SBCD` (and `NBCD`) are
   packed-BCD byte add/subtract with the extend bit as the decimal carry — the direct hardware
   equivalent of a 6502 `ADC`/`SBC` with D=1. A BCD column add becomes one instruction, not a
   macro that recomputes five flags. ⚠ `ABCD`/`SBCD` use **X**, not C, as carry-in, and set C
   as carry-out — so a multi-column chain seeds X once and the C→X handoff is the thing to get
   right, not the flag soup.
2. **Where an opcode does not apply, add named BCD helpers** (`src/cpu/` alongside
   `m68k_math.h`) — a packed-BCD add, subtract, increment and the digit split/join — and write
   the twins against those, the same way the wide-value campaign replaced byte lanes with
   `uintNN_t`.
3. **Then delete `cpu.D` from these bodies**, which retires the whole fourth group of the
   `cpu`-in-a-`_core` table.

Constraints this pass inherits:

* ⚠ **A twin's flags still have to be right.** These routines are compare-and-branch heavy
  (`sort_cars_by_key`, `check_car_pair`) and the 6502's C after a decimal `ADC` is the decimal
  carry — which `ABCD`'s C matches, but which a C-level `if (sum > 0x99)` reimplementation gets
  subtly wrong at the invalid-digit inputs the fixtures generate. **Brute-force the helper over
  all 256x256x2 inputs against the oracle's decimal `ADC`**, exactly as `halve_signed_rounded`
  was proven over all 256.
* ⚠ The 6502's decimal `ADC` is *defined* on invalid BCD digits (`$0A`..`$0F` nibbles) and the
  fixtures' randomised `mem[]` will hand them to it. `ABCD` on the 68000 is **not** specified
  the same way there. So the helper — not a bare opcode — is the safe default, and an `ABCD`
  fast path is only legal where the inputs are provably valid BCD.
* The gate is `make validate FN=` for each of the six plus the determinism family;
  `tick_race_timers` and `lap_complete` additionally need `determinism-race`, which is the only
  trajectory that reaches a lap boundary.
