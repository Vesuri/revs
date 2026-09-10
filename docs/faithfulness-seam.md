# The faithfulness seam — which side of the line does this code go on?

> ⭐ **Postmortem finding #5.**  The Atari port's split between validated, faithful twins and
> platform-specific lossy code was *good architecture arrived at late*: the rule for which side
> a function lands on got clarified only after churn.  It is a call you make hundreds of times,
> so it is written down here **before the first twin exists**.

## The three tiers

| File | Contract | Validated? | Linked into |
|---|---|---|---|
| `src/gen/revs_gen.c` | The generated 6502 transliteration. Faithful by construction. Never edited by hand. | is the oracle | both backends |
| `src/gen/revs_native.c` | **FAITHFUL native twins.** Byte-identical to the `__t6502` oracle. | ✅ `make validate` | **both** backends |
| `src/platform/sound.c` | ⭐ A WORKED EXAMPLE OF THE SEAM, and not a twin: the MOS's sound scheduler is OS behaviour, so it is faithful, shared, and validated against real hardware (`make sound`) even though no 6502 routine corresponds to it — the same argument as `teletext.cpp` and `bbc_hw.cpp`. Its Amiga half (`amiga/RevsAudio.cpp`) decides nothing about what sounds, only how Paula reproduces a chip state. When a subsystem lives BEHIND an OS call, that split is where the line goes. | ✅ is | both |
| `src/gen/revs_native.c`, listed in **`NATIVE_FUNCS`** | ⭐ A NATIVE DRIVER: same file, same style, same `__t6502` oracle — but **no fixture can exist**, so `make validate` only *reports* it and `make determinism` (+ `determinism-drive`) is the gate. Reserved for a routine whose first act on any pre-state is to run the rest of the engine; today that is `race_main_loop` alone. Do not use it to skip a fixture that is merely awkward — the reason must be structural and written beside the address in `tools/transpile.py`. `docs/validation-harness.md` §a DRIVER with no fixture. | ❌ `make determinism` instead | **both** |
| `src/platform/amiga/revs_native_amiga.cpp` | Genuinely Amiga-only code. Deliberately lossy — drops hardware-register writes, routes audio to Paula, frame-driven entry points. | ❌ cannot be | Amiga only |

## The rule

> **A faithful, pure-`mem[]` 6502 routine that merely needs a small Amiga variation does NOT
> move to the `.cpp`.  It stays a validated twin in `revs_native.c`, with the variation guarded
> by `#ifdef REVS_PLATFORM_AMIGA`.**

- `#ifdef REVS_PLATFORM_AMIGA` — Amiga-only additions: publishing a dirty region, skipping a
  BBC-hardware tail the copper has already handled.
- `#ifndef REVS_PLATFORM_AMIGA` — BBC-faithful behaviour the validation build must keep so the
  twin still matches its oracle byte for byte.

Amiga-only globals a twin writes belong in **that twin's own translation unit** (the writer's
TU), also under `#ifdef REVS_PLATFORM_AMIGA`.

## Why the rule points this way

The validated tier is the only tier with a *proof*.  Every routine that leaves it loses its
differential against the oracle permanently, and `make validate` becomes blind to it — which is
exactly how a class of bug that passes validation and breaks at runtime gets in.  So the
question is never "does this touch the Amiga?" but **"is this routine's behaviour still defined
by the 6502 original?"**  If yes, it stays validated and the platform difference is an `#ifdef`.

Corollary: prefer making the Amiga variation *smaller* over moving the routine.  A dirty-flag
publish is three lines under an `#ifdef`; re-hosting the routine costs its proof forever.

## The decision, as a checklist

Ask, in order:

1. **Is the observable result defined by the original 6502 code?**
   No → `revs_native_amiga.cpp`.  Yes → continue.
2. **Does it operate on `mem[]` (plus pure locals), rather than on Amiga structures?**
   No (it owns Bitmaps/CopperLists/Sprites) → `revs_native_amiga.cpp`.  Yes → continue.
3. **Can the Amiga difference be expressed as an `#ifdef`-guarded addition or omission,
   leaving the mem[] result identical on the validation build?**
   Yes → **`revs_native.c` + `#ifdef`.**  This is the answer far more often than it looks.
   No → `revs_native_amiga.cpp`, and say in a comment why the difference could not be an
   `#ifdef` — that sentence is what stops the same routine being re-litigated later.

## Writing one — the twin style rules, and why each exists

⚠⚠ **The deliverable is idiomatic C.  A transliteration with the macros left in is not a twin**,
even when it is byte-exact: the *point* of a twin is that the interpreter is gone, and 6502 macro
soup hides the structure the next optimisation has to see.  Twin #2 was rewritten on 2026-08-17
purely for this reason — same 0/700 differential, same framerate (465 vs 467 painted frames over
~9500 fields, a 0.4% difference), 573 lines that can now be read.

1. **Named locals and ordinary control flow.**  `for`/`if`/`switch`; no `LDA`/`STA`/`TAY` chains,
   no `goto` except a single cleanup label that writes threaded state back.
2. **A typed `_core(...)` that takes its inputs as arguments**, plus the thin `void <name>(void)`
   shim.  The core is the thing a future asm twin or representation change replaces.
3. **`mem.h` names for every cell that has one; a `symbols.csv` row for every one that does not.**
   An unnamed hex address left in a twin is a rename that was skipped — queue it in
   `docs/rename.md` *before* writing the twin, because `make gen` cannot re-rename hand-written C.
   Indexed tables get `table` rows plus a file-local `#define`, since only `var` rows become
   `mem.h` aliases.
4. **Comments say what it COMPUTES.**  `revs_gen.c` next door is the instruction-level record.
5. **Hot loops thread state through locals, not a struct pointer.**  `v->byte` inside a
   2093-iteration loop is a memory operand gcc cannot keep in a 68000 register.
6. **Hardware writes stay, `#ifdef`-guarded.**  `make validate` diffs the hardware-write SEQUENCE,
   so a dropped `$FE69` poke is a FAIL — and on the Amiga those writes become the copper's band
   records, so they are not dead stores there either.
7. ⭐ **The one place a 6502 macro survives: a live FLAG.**  C has no carry or overflow, and a
   twin's exit contract can include the flags — every twin here declares AXY(+S)+flags live, and
   every trap path is an exit.  Where a flag genuinely leaves the routine, wrap the operation in a
   small named helper (`load_a`, `adc_step`, `sub_from`, `stop_unchanged`) with the cpu.h macro
   *inside* it: the semantics stay the 6502's by construction, decimal mode included, and the
   caller still reads as C.
   ⚠ MEASURED, not theoretical: rewriting three `CPY`s and one `AND` as plain C comparisons kept
   `mem[]` byte-exact and broke `C`/`N`/`Z` on 35 of 700 cases — all of them trap paths.  Only the
   fixture's *illegal* cases caught it.
8. ⭐⭐ **A DRIVER TWIN BUYS LEGIBILITY, NOT MILLISECONDS — check which before you write it.**
   Twins #1 and #2 were leaf-heavy (the work and the interpreter in one routine) and paid +62% and
   +28%.  Twins #4 and #5 are ~100-byte DRIVERS over transliterated subtrees, and the framerate did
   not move at all (`docs/perf-method.md` §twins #4 and #5).  Both kinds are worth writing — a
   driver twin is how a subsystem's shared data structure gets named and how the next twin becomes
   possible — but quote the reason honestly when picking one, because "it is the biggest row in the
   profile" is not the same claim as "the cost is in this routine".
   ⭐ Twins #6, #7 and #8 (`apply_driving_model`, `draw_track_object`, `fill_dash_edge_columns` —
   136, 61 and 35 bytes) were written on that basis with **no performance claim made at all**, and
   what they bought is exactly what the rule predicts: the driving model's 16-bit state vector
   (`model_state_lo`/`_hi` and the hand-integrated `car_lateral_speed`), the object plotter's four-cell
   argument block (`plot_x` / `plot_line` / `proj_width` / `plot_shape`) and the 24 per-slot
   object arrays now have names, and `docs/rename.md` gained five well-evidenced open items
   instead of a shrug.  When a twin's honest reason is "this is how the subsystem gets named",
   say that in the commit and skip the FPS sentence.
   ⚠⚠ **"Driver" is NOT a byte count** — twins #9-#12 sharpened this.  `road_edge_walk` is 231 bytes
   and carries real arithmetic (the running nearest, a three-midpoint interpolation), and the
   framerate still did not move, because per road side its own body runs once while it calls four
   transliterated routines **up to 18 times each**.  The test is *instructions executed inside the
   routine versus inside its callees per invocation*, and a short loop around transliterated calls
   is a driver however long its body reads.  Which is also the constructive form of the rule: the
   twin worth writing next is the CALLEE, not the caller.
   ⭐ Twin #13 (`div16by8`, `$0C47`) is what that constructive form points at, and it is the opposite
   shape from every twin before it: 94 bytes, no callees at all, and **every byte of it is
   arithmetic**.  It is also the whole of `project_point`'s and `bearing_to_section`'s callee set —
   between them those two routines call exactly one function.  When looking for the next twin,
   enumerate the callee set FIRST: a subsystem whose leaves are one shared math routine is a much
   cheaper win than the size of its profile row suggests.
   ⭐⭐ Twins #14/#15 (`bearing_to_section_from` `$2147`, `project_point_from` `$2287`) are that
   enumeration cashed in, and they are the FIRST twins since #2 with no transliterated subtree
   underneath them at all: the callee set they share is `div16by8` alone, and #13 had already made
   it real C.  So the ordering rule has a corollary — **twin the leaf first, then its callers
   become leaf-heavy too.**
   ⚠ Enumerating the callee set is also what *sized* the job: `grep -E "JSR|JMP"` over the two
   address ranges is ten seconds and it said "one callee, three call sites" before a line was
   written.  Do that before estimating any twin.
   ⚠⚠ **AND THEY STILL DID NOT PAY — LEAF-HEAVY IS NECESSARY AND NOT SUFFICIENT.**  Measured: 371
   painted frames against a 383 control, and 349 before the `always_inline` fix below, i.e. the
   twin was 9% SLOWER than the transliteration it replaced.  The reason is the sharpest form of
   this whole section: **the win is ALGORITHMIC COMPRESSION, not being real C.**  Twins #1 and #2
   paid because most of what the interpreter did for them was bookkeeping around work that C does
   differently (a palette loop, a dirty scan).  These two compress nothing — every 6502 instruction
   is one C operation — **and all four exit flags are live at their call sites**, so the twin has to
   compute the same N/V/Z/C the interpreter computed.  Once the flags are in the contract there is
   no interpreter left to delete.
   ⇒ The question to ask before an arithmetic twin is **"what does the C version do in FEWER
   operations than the 6502 did?"**  A chain of `ADC`/`SBC` with live flags: nothing.  A byte-pair
   shift loop the 68000 does in one word op: something.  If the answer is nothing, write the twin
   for the NAMES and say so (as with #6-#8), or do not write it.
   ⭐⭐ **AND THE WAY BACK TO PARITY WAS FLAGS, NOT ARITHMETIC.**  A `cpu.h` macro writes FIVE cpu
   fields and a routine usually reads one: `SBC` stores A/N/V/Z/C at ~16-20 cycles a store and
   computes V through mask chains, so a subtract CHAIN pays it over and over for flags that are dead
   at the exit.  `div16by8` was doing that seven times a call, 60 calls a frame.  `sbc_value` (the
   same subtract, decimal mode included, without the bookkeeping) plus a one-shot `sbc_overflow`
   replay of the single V that escapes brought twins #14/#15 from -2.4% to **exact parity** with the
   transliteration.
   ⭐⭐ TWINS #16-#24 close the road-geometry pass — everything build_track_geometry reaches is now
   real C (19 routines, 776 6502 instructions), and they are the cleanest confirmation of the
   ordering rule so far: two of the nine COMPRESS (`point_distance_hypot`'s nine `LSR hi / ROR A`
   pairs are three 68000 word shifts; `emit_edge_width_offset`'s variable shift is a LOOP on the
   6502, up to 255 iterations, and one `lsl.w` plus a range test here) and the other seven were
   written because leaving one transliterated leaf inside a per-point loop is exactly what made
   twins #4/#5/#9/#10 driver-shaped.  ⚠ THE SAME FLAG TRAP CAUGHT BOTH OF THE ARITHMETIC ONES, in
   both directions: `emit_edge_width_offset` ends on `CMP`/`CPY`, so the V of the ADC eleven
   instructions earlier is its exit V (565 of 2000 cases, `mem[]` byte-exact); and
   `point_distance_hypot`'s two arms write DIFFERENT AMOUNTS of `hypot_min` — the near arm keeps
   the low byte in A the whole way and never stores it (829 of 2000).  Neither is visible in a
   reading of the listing that asks what the routine COMPUTES; both fell out of the differential
   in one run.
   ⇒ The general rule: **write the chain with values, and replay the ONE escaping flag from its
   operands.**  Work out WHICH flag escapes rather than assuming — `view_delta`'s V is observable
   only through `project_point`'s clip exit, because `bearing_to_section`'s 45-degree arm overwrites
   V with `BIT` and its octant arms with the closing `ADC`.
   ⚠ And a sabotage aimed at a fixture that cannot SEE the flag it breaks proves nothing: three of
   thirteen "survived" until they were pointed at `project_point_from` instead of
   `bearing_to_section_from`.
   ⚠⚠ **A flag helper left OUT OF LINE can make an arithmetic twin slower on its own.**  `sub_from`
   and friends are a few instructions each, but GCC keeps them out of line at -O3 (they write the
   global `cpu` and have many callers) — a `jsr` plus a `movem.l` pair per subtract.  They now carry
   `REVS_FLAG_OP` (`static inline __attribute__((always_inline))`).  **Grep the objdump for
   `jsr <sub_from>` before believing any arithmetic twin is fast**; `docs/perf-method.md`
   §twins #14/#15 has the four-way measurement.
   ⭐⭐ TWINS #25-#39 close the SPAN RASTERISER — everything `draw_road` reaches is real C, so the
   whole view pipeline from `build_track_geometry` to `view_paint_lines` has no transliteration in
   it.  Three things they added to this section:
   * **A DESCRIPTOR STRUCT IS AS EXPENSIVE AS AN OUT-OF-LINE FLAG HELPER.**  The two plotters are
     one routine taking a `const SpanPlotter*` and the four arms one taking a `const SpanArm*` —
     the right shape, because they really are one algorithm with constants swapped, and 4.2%
     slower than the transliteration until both carried `always_inline`.  Out of line GCC re-loads
     five fields per call in a routine that runs eight times a scan line.  Generalised: *a
     parameter that is a compile-time constant at every call site must be inlined, or it is a
     memory operand in the inner loop* (`docs/perf-method.md` §twins #25-#39).
   * **A COMPUTED JUMP INTO AN UNROLLED CHAIN is a declared coverage limit, like a patched branch
     offset.**  The four arms are entered partway through by a patched `BCC`, and the transpiler
     emits a switch over every instruction boundary in the chain (60 labels for one arm).  A twin
     enumerates the offsets the arm's TABLE holds — eight or sixteen — and traps on anything else.
     What makes that derived rather than a guess: those tables are static in the image, and
     although they live inside the $80-spaced source blocks they live in the blocks' TAILS (offset
     $50 and $58), above the $50 scan lines a plotter can reach through `($70),Y`.
   * ⚠ **THREE SABOTAGES THAT PASSED WERE NOT DEFECTS, AND ONE WAS.**  Planting `LDX #$80` at the
     top of a STEEP arm's loop, or calling an end marker there, changes nothing: X is always a
     column 0-3 in a steep arm, never the $80 that writes a terminator, and every path reloads it.
     The SHALLOW analogue *is* observable, because those arms' end markers test X — and the
     fixture only caught it after the marker opcode slots were given an unexecutable byte one case
     in six, which is what makes "this arm called a marker" visible at all.  Check whether a
     sabotage changes anything before steering the fixture, and check the sibling case before
     concluding it does not.

   ⭐⭐ TWINS #40-#43 close the VIEW/DASHBOARD SEAM — everything `fill_dash_edge_columns`
   reaches (`surface_colour_at`, `column_gap_walk`, `fill_column_gaps`, `fill_edge_column_run`)
   is real C.  Three things they add:
   * ⚠⚠ **HOISTING A POINTER OUT OF A LOOP IS A CORRECTNESS QUESTION FIRST.**  CLAUDE.md says to
     lift the `bus_*` range test to where the pointer is known, and that is right — but the 6502
     re-reads the zero-page PAIR at every `STA (zp),Y`, and `column_gap_walk`'s own stores can
     land on the cells that drive it: a randomised boundary-table pointer of `$005D` makes the
     run cover `$0082` and `$0085`, i.e. the loop's end line and the column being filled (2 cases
     of 1200, and they read as a value diff a hundred bytes away from the cause).  What is safe
     to hoist is the **range test**, not the pointer: one comparison per store instead of a
     `bus_read`/`bus_write` dispatch, with the pointer still read from `mem[]` each pass.
   * ⚠ **AN SMC TRAP MUST FIRE WHERE THE OPCODE IS, not where the byte is read.**  The walk's
     patched branch is only reached once a non-zero source byte turns up, so a column of zeroes
     never executes it: validating the offset at the top of the routine trapped 138 cases of 1200
     early, with A, Y and the flags all wrong.  Decode the byte where you like; trap at the site.
   * ⚠ **A FIXTURE'S ILLEGAL VALUES MUST LEAVE THE REGION, not merely the arms.**  A random branch
     offset is a legal branch to some other instruction boundary in the same code, and anything at
     or above `$1DC0` reloads Y from `span_line_cursor` — an infinite loop in the ORACLE, which
     hung the harness rather than failing it.  Pick illegal values that land outside the switch,
     and remember the same trap when SABOTAGING a loop: "advance the cursor by 0" made the twin
     itself non-terminating, so the defect had to be reshaped into one that still exits.

9. **Sabotage before believing it.**  Four defects minimum, each must FAIL, and any sabotage that
   PASSES is a fixture gap to write down rather than a pass to enjoy (`view_paint_lines`' phase-3
   carry tail is unreachable and untested — recorded in the twin's own comment).
   ⚠ …**unless the sabotage is not a defect.**  `road_edge_start`'s horizon tie-break (`$2392 CPX
   $51 / BCC`) differs between `>=` and `>` only when the index is EQUAL — and there the store
   writes the values already in the cells, and neither `STA` nor `STX` sets a flag.  Provably
   observationally identical, so the case was replaced with a reversed test that *is* observable.
   Before steering a fixture harder to catch a sabotage, check that the sabotage changes anything.

### ⚠ Four ways a twin looks right and is not, all four measured on twins #9-#12

Each of these passed a reading of the listing and failed the differential.

* **`PHP` leaves a byte on the 6502 stack, and the differential sees it.**  `road_edge_walk`'s
  interpolation stashes the gap's sign across two `ROR`s (`$2423 PHP … $2427 PLP`).  The pair is
  flag-neutral, so a twin that computes the shift in C and skips it is value-correct — and 32 of
  200 cases differed at `$01FF` alone.  A `PHP(); PLP();` reproduces both the residue and the
  flags; keep it wherever the oracle pushes.
* **V escapes a routine that ends in `CMP`.**  `CMP`/`CPX`/`CPY` do not write V, so whatever the
  last `ADC`/`SBC` left is what the caller gets.  Both `road_edge_side` (one `ADC #$78`) and
  `road_edge_start` (five adds and subtracts on the exit path) failed on V alone, with `mem[]`
  byte-exact.  Rule: **on any path that can reach the exit, arithmetic goes through `adc_step` /
  `sub_from` / `sbc_step`** — plain C is only for a value whose flags are provably overwritten.
* **An SMC opcode test is UNCONDITIONAL, even inside a conditional branch.**  `$231A` is a `BEQ`
  whose offset is patched.  The transliteration tests the opcode whether the branch is taken or
  not, because a byte that is not a `BEQ` is an instruction the model cannot execute — so it traps
  on the first iteration regardless of the comparison.  Consulting it only on the equal path let
  the twin re-base four edge points the oracle never reached.
* **A branch on a flag is not a branch on the value.**  `abs8`'s `BPL` at `$3450` tests the
  CALLER's `N`, not bit 7 of `A`.  Every real caller has just computed `A` so the two agree; a
  randomised pre-state does not, and a twin written as `if (A & 0x80)` fails 117 of 800 cases.
  The fixture has to DECORRELATE the two deliberately or it never asks the question.
  ⚠⚠ **And "freshly computed" is not enough either — DECIMAL MODE decorrelates them on its own.**
  Twins #14/#15 opened with `SBC` / `SBC` / `BPL`, the standard 16-bit-negate test, so N *is* the
  sign of the value just computed… in binary mode.  `cpu.h`'s `SBC` follows the NMOS part: in
  decimal mode `A` receives the BCD-corrected byte while **N and Z come from the binary result**,
  so `if (stored & 0x80)` picks the other arm on a quarter of the cases and nowhere else.  76 of
  800 failures, all with `D` set.  Rule: **if the 6502 branched on a flag, the twin reads
  `cpu.N`** — never bit 7 of the byte, however obviously the two "must" agree.
* **`BIT` SETS V, from bit 6 of its operand.**  It is easy to read `BIT $86 / BPL` as "test bit 7"
  and write `mem[cell] & 0x80` — which is right about N and silently drops V.  In twins #14/#15
  the two octant arms `ADC` over the top of it and never notice; the 45-degree arm ends in
  `LDA #imm / STA / RTS`, so **V survives to the caller** and 645 of 4000 cases differed on V with
  `mem[]` byte-exact.  Same rule as the `CMP` bullet above, one instruction further out: a 6502
  instruction's flag side effects are part of it, so put it behind a named helper (`sign_bit7`)
  rather than paraphrasing it.  ⚠ Note both halves of the trap are needed to find it: without the
  fixture declaring flags live, this is invisible.

### ⚠⚠ A CALL THROUGH THE MOS CLOBBERS REGISTERS THE LISTING GIVES NO HINT ABOUT

Measured on twin #86.  `update_camera_and_drive_state` sets `Y` once, at `$452F`, and reads a
track table through it at `$457F` and again at `$45D8`.  Nothing between those points contains an
`LDY`, so "Y is still the section's direction index" reads as obviously true — and it is false on
one path: the arm at `$45B3` reaches `begin_spin_from_a`, which queues a MOS SOUND, and
`sound_osword` leaves the MOS's own `Y` behind.  A twin that cached the index in a local differed
in one case in six.

⭐ **The rule: when writing a twin, treat every `platform_mos_call` in the subtree as clobbering
A, X and Y unless the OS entry's contract says otherwise.**  It is the opposite of the interrupt
contract (where a real BBC preserves all three across an engine-context IRQ, `CLAUDE.md`) and it
is invisible in the 6502 listing, because the clobber happens two or three `JSR`s down.  Read a
register out of `cpu` at the point of use, not into a local at the point it was set — and if you
do cache it, the thing that catches you is the differential, so never cache it without a fixture
that reaches the MOS path.

### ⚠⚠ A HOOK SEAM MUST HAND OVER EVERY LIVE REGISTER, NOT THE ONES THE DEFAULT CALLEE READS

Measured on `fill_line_attr` (twin #7), and it shipped broken for months.

At `$1946` the routine calls Silverstone's `edge_x_offscreen`, or — on an expansion circuit —
whatever `ModifyGameCode` put there instead. The twin marshalled `cpu.X` and stopped, with a
comment that said *"both are 6502-ABI shims that read the start index in X"*. That was true of
`edge_x_offscreen`, which reads `X` and nothing else. It was false of all five expansion
circuits' hooks, which open `TYA` and walk `edge_y` **downward** with `DEY`.

Handed a stale `cpu.Y`, that walk can start below the horizon entry, so its only range exit
(`edge_y[Y] >= horizon_extent`) never fires; `Y` runs off the bottom of the table and wraps to
`$FF..$E0`, and `$5F20 + Y` is then `$601F..$6000` — the 32 bytes of `view_cell_bytes`, the
source-byte → screen-byte identity table every cell chain paints through. A table full of `$1F`
paints three red pixels and a green one wherever a nonzero source byte is carried. **The symptom
was horizontal red streaks around the wheel, cockpit edge and horizon on the expansion circuits
only, appearing after a while parked** — nowhere near a plotter, and nothing to do with the table
that was actually being overwritten.

⭐ **The rule: at an SMC/hook seam, seed every register the 6502 has live at that address —
derive the set from the surrounding instructions, not from what the unpatched callee happens to
read.** Here three neighbours settle it beyond doubt: `$1941 STY span_end_index`, `$1943 DEY`,
`$1944 STY math_hi`, so `Y = endCursor - 1`. (`$1949 LDY horizon_extent` then discards the hook's
exit `Y`, which is why the walk below re-seeds from `horizon_extent` — the *entry* obligation and
the *exit* one are separate questions.) This is the same lesson as the interrupt contract in
`CLAUDE.md` — an ISR shim must reproduce the OS entry's side effects, not just call the handler —
and it fails the same way: silently, plausibly, and invisibly to every existing gate.

⚠ **Why no gate caught it.** Every `make validate` fixture races Silverstone, so a seam's
*patched* arm is exercised by no fixture at all; `make determinism`/`-drive` are Silverstone too;
`make tracks` proves the circuit's bytes land and `make track-run` proves its code *runs*, and
both passed throughout — a hook can execute 1600 times and corrupt a table on the way. The gap is
structural: **the expansion-circuit arms of the ~13 hook seams are validated by nothing.** What
found it was the phase canary (`make INK_WATCH=1`) plus a real-BBC frame-buffer differential over
display lines 82..165, and what *proves* a fix is that differential going byte-identical.

### ⚠⚠ A TRAP ARM MUST TRAP AT THE 6502'S OWN INSTRUCTION BOUNDARY, NOT BEFORE THE STORES IT FOLLOWS

`platform_smc_unhandled(...); return` is how a twin bails out when a per-circuit SMC site holds an
opcode it does not model. The temptation is to test the opcode *first*, as a precondition — one
early return at the top of the axis loop, before any work. The 6502 does not work that way.

In `place_car_world_coords` ($2937) the mask site is $298D, and the instructions in front of it
include a **store**: $297F `ADC $0900,Y` / $2982 `STA $09FD,X` lands axis 1's low byte, and only
then does $2985-$298D reach the mask. So on the unmodelled-opcode path the real machine leaves
that byte written and the twin's early return left it stale. It diverged at exactly one cell,
`$09FE`, on the one fixture case in ten that arms the trap — and `make validate` was FAILING in the
tree for it.

Two rules fall out:

- **Place the bail-out where the 6502's PC would be, and let every store in front of it happen.**
  Write the low byte, then test the opcode. The wide store that follows the mask rewrites that
  byte with the value it already holds, so the wide idiom survives the ordering intact.
- **The trap arm is REACHABLE FIXTURE STATE even when it is unreachable on hardware.** All five
  circuits keep the `AND` opcode here, so nothing on a real disc takes this path — but the fixture
  arms it deliberately, which is the whole point of arming it. An arm that "cannot happen" is
  still a byte-exactness obligation, and a vacuity guard on it is what turns it into one.

### ⭐ NAMING AN SMC SEAM: one name for the seam, the operand as an offset off it

A per-circuit hook seam is an address IN CODE that an expansion circuit's `ModifyGameCode`
rewrites (`make track-smc` / `make track-patch`).  A twin that reaches one reads the opcode byte
and dispatches on it, with the operand at +1/+2.  So the seam gets **one** `symbols.csv` row and
the twin writes the operand as an offset off that name (`MEM_smc_object_coord_mask + 1u`) — the
operand halves get no rows of their own.  ⚠ These are code addresses, not variables: the name
says *which seam*, not what it holds.

### ⭐ A patched SMC BRANCH OFFSET is a narrower obligation than it looks

`$231A`'s offset can in principle name ~200 addresses inside `road_edge_start`, and the
transliteration emits a switch over all of them.  A twin cannot, and does not have to: `make
track-patch` says every circuit on this disc writes `$00` and Silverstone has `$0F`, so the twin
recognises those two and hands anything else to `platform_smc_unhandled`.  That is a *declared*
coverage limit, not a guess — and `make track-run` is what would catch a circuit that ever wrote a
third value.  The fixture then has to plant one of the two, because a random third byte sends the
two models to different addresses and measures the limit rather than the twin.

### ⭐⭐ A MATH twin: use the wider register, but the 68000 instruction is not always the answer

Twin #13 (`div16by8`) is the first twin whose whole body is arithmetic, and it splits the standing
"use real 68000 maths, not a reimplementation of the 6502's byte chain" rule into two halves that
point different ways:

* **The WIDTH is free and always right.**  The 6502 writes `ASL math_lo / ROL A` because it has no
  16-bit register; that pair is one 16-bit shift, and the twin shifts the remainder:dividend word as
  a word.  GCC unrolled the eight steps into `add.w`/`lsr.w`/`cmp.w` with no software helper, and
  the entire byte-at-a-time chain — plus its per-instruction flag bookkeeping — is gone.  Do this
  every time.
* **The INSTRUCTION can be blocked by a flag.**  `DIVU.W` *is* this routine, in one 140-cycle
  instruction — and it cannot be used, because the exit `V` flag belongs to the last of the seven
  conditional subtracts.  Recovering which step that was needs a bit scan plus a SECOND divide for
  that step's partial remainder, which is no faster than the loop and only valid when the dividend's
  high byte is below the divisor.  `DIVU` would also **trap** on the divisor-of-zero case the
  fixture feeds it.
  ⭐ The lesson is where the unlock lives: the flags are dead at all three call sites, so the
  blocker is not this routine but the fact that its CALLERS are still transliterated.  A hardware
  divide becomes provably legal the moment `project_point` and `bearing_to_section` are twins too.
  **Ask which of a leaf's outputs are genuinely observed before deciding the leaf cannot be fast.**

And the fixture gets easier, not harder, as a routine gets more arithmetic: `div16by8` has three
input bytes, so the pre-state that matters is tiny — which is why it is STEERED into four named
domains (the engine's own normalised shape, quotient overflow, divide-by-zero, uniform) rather than
left random.  ⭐ It is also the only fixture in the harness that **randomises decimal mode**, and it
can be: the subtract goes through the real `SBC`, so the twin agrees with the oracle even where
`SED` turns the divide into something else entirely.  A twin that computed the subtract in plain C
would pass 3000 binary-mode cases and fail the decimal ones — sabotage #5 confirms it does.

### ⭐⭐ CALL THE `_core`, NOT THE SHIM — except for the five cases where the shim does real work

When native code calls another twin, it should call the `_core` and pass the values it already
holds. Going through the `void name(void)` shim means marshalling live values out to `cpu`/`mem[]`
and then reconstructing exit registers the caller throws away. **But "always call the core" is
wrong**, and the exceptions are not stylistic — each is a correctness trap:

1. ⚠⚠ **The shim is the PUBLISHER of a relocated wide value.** Once a lo/hi pair becomes a real
   `uintNN_t` (mechanism (B)), the shim's `*_marshal_in` / `*_marshal_out` are the only thing
   keeping the wide variable and the `mem[]` cells agreeing. A bare `_core` call reads a stale
   wide value, or leaves `mem[]` stale for whatever transliterated code reads it next. This is the
   defect `fe379ed` found, and it is why `race_main_loop` calls shims on purpose.
   `check_crash` is in this class (`edge_nearest_marshal_in`), and it is the one that looks most
   like an oversight.

   ⚠⚠ **AND A MISSING PUBLISH CAN BE INVISIBLE TO EVERY GATE, because the value it drops is only
   ever non-zero on an arm the gates never take.** `race_main_loop` phase 3 called
   `read_driving_controls_core()`, so the frame's new steering angle went into `car_angle_16[2]`
   and nothing published it; phase 4's `apply_driving_model` shim then did its own
   `car_angle_marshal_in()` and restored the stale `mem[$62A2/$62A5]`. **The wheel could not turn
   at all, on either input path** — and `validate` compares the *shim* (so it passes), while
   `determinism` and `determinism-drive` hold the throttle and **never steer**, so the stale value
   equalled the fresh one in both, byte for byte, at frame 300. It took a player to find it.
   ⭐ The lesson is about the gates, not the seam: **a differential proves nothing about an input
   the differential never supplies.** `REVS_HOLD_STEER=l|r` (`src/platform/autorun.cpp`) exists so
   that a host run can hold a steering key, which is what made this reproducible off-target in one
   run — and the check that settles it is two runs with opposite keys: the angle must come back
   with *opposite sign bits*, because a single run showing a moving wheel cannot tell steering from
   the slip-cancelling self-drive demand.
2. **The shim carries a side effect the core cannot** — an interrupt fence and write order
   (`update_horizon_band`'s SEI/CLI), a store the core's signature does not cover
   (`copy_dash_data`'s `math_lo`), stack residue (`draw_starting_lights`), or instrumentation
   (`view_paint_lines`' `REVS_PLOT_CHECK`).
3. **The shim's exit ABI IS the caller's own**, because the 6502 reached it by a tail call
   (`begin_scrape`, `draw_gear_indicator`).
4. **The call is a HOOK SEAM.** The other arm is a real per-circuit hook that reads registers, so
   `cpu` must be handed over regardless — calling the core changes nothing but the spelling.
5. **A shim calling a shim inside `revs_native_seam.c`** is the 6502-ABI layer doing its job.

⭐ The test that separates (1) and (2) from a genuine pure marshal: **read the shim body and ask
what survives if you delete every line that only writes `cpu`.** If the answer is "nothing", the
call site should call the core. If anything else is left — a marshal, a store, a fence, a stack
write — it must not.

⚠ An audit for this cannot grep for `name()` alone: the shims live in `revs_native_seam.c`, not
beside the cores, and matches inside comments outnumber the real ones. Strip comments first, then
track brace depth to attribute each call to its enclosing function.

## ⭐⭐ TWINNING A PER-CIRCUIT HOOK BODY — the seam, and where its gate has a hole

The hook bodies in `src/gen/revs_track_hooks.c` are the last transliterated production code in
the port, and they are the reason the 6502 ABI is still alive at the seam. They can be twinned,
but not through `VALIDATE_FUNCS`: the code lives in a *circuit file*, occupies the same
`$5300-$5A25` addresses on every circuit, and is dispatched by address, so there is no `__t6502`
name for the harness to call.

**The mechanism** (`HOOK_TWINS` in `tools/transpile.py`, keyed by `(circuit, entry)`): the
transliterated body is still emitted — it is the oracle, and the only description of what the
circuit really does — and the dispatch prologue routes production past it:

```c
case 0x56C8: if (!g_hookOracle) { hook_horizon_clamp(); return; } goto L_56c8;
```

One binary carries both models; `g_hookOracle` picks. Production leaves it 0 (one global test per
hook call, ~110 a race second) and the Amiga link can fold it away. The twin itself goes in
`revs_native.c` like any other — `_core` plus a shim — and `HOOK_TWIN_NAMES` makes a fixture-less
hook twin fail `make validate`, the same anti-vacuity rule `VALIDATE_NAMES` gets.

⚠⚠ **Key it by circuit, never by address.** `$56C8` is byte-identical on Brands/Donington/Oulton
and *different code* on Snetterton, whose tail jumps back into its own transliterated body at
`$53DC` — an internal label, not a dispatch entry. Twinning Snetterton's copy would mean adding
`$53DC` as an entry and making `revs_track_hook_has` claim a hook body at an address nothing
patches. Leave it transliterated.

⚠ **Seed the SMC extents the circuit installs, not a random image.** `hook_horizon_clamp`'s
one-shot recomputes the road half-width through `$2542`, which Brands has rewritten to its own
`JSR $53F0` + `NOP`. A fixture that randomises those four bytes traps instead of taking the arm
the circuit actually runs.

⭐ **A hook with two arms needs TWO live masks, chosen per case.** `hook_edge_walk_limit`
(`$56BC`) either returns the section count with the compare's flags — a full `A/X/Y`+flags ABI —
or jumps into the resumed road walk, whose registers are `build_road_section`'s leavings and match
between the models only in `mem[]`. One mask over both arms is either a false failure or a gate
that checks nothing: `diff_run` takes the mask per call, so compute the arm in the fixture
(`stopArm = C && count >= $0A`) and pass the mask that arm really has. The same shape applies to
`hook_walk_back_gate` (`$55BD`): registers live when the gate closes, `LIVE_NONE` when it rebuilds.

⭐⭐ **A REPLAYED FLAG NEEDS A FIXTURE ARM THAT CAN MAKE IT DIFFER.** `hook_merge_horizon_edges`
(`$5772`) exits with V from a 16-bit subtract's high byte, whose borrow is the low byte's carry.
A wrong borrow (`>` for `>=`) changes that carry **only on a tie**, and a tie between two small
angles cannot overflow the high subtract — so the sabotage survived 4000 randomised cases twice,
not because the fixture missed the flag but because the flag was provably 0 on every case it
generated. The arm that fixes it ties the low bytes and randomises the high ones over the full
range; the defect then shows in 2 cases of 4000. **A byte range chosen for plausibility is a
coverage decision.**

### ⚠⚠ `make viewdiff` COVERS THE WALK AND NOT THE CLAMP — measured, 2026-09-07

`viewdiff` is the outer gate on every hook, and it is the right one, but it compares the finished
*picture*: it can only see the arms the recorded frames take. Two deliberate defects in
`hook_horizon_clamp` — dropping the object-ceiling store, and shifting every clamped `edge_y` by
+3 — **both survived it with 0 differing bytes on all five circuits.**

A counter in the twin says why: at frame 60 the hook is entered 110 times per circuit and walks
1100-1430 edge points, and **clamps zero of them.** The horizon is already monotonic in those
frames, so the arm that gives the routine its name never executes; only the walk and the
`$56F7` tail do.

So: **a hook twin needs a randomised fixture even though `viewdiff` exists**, and a `viewdiff`
PASS is evidence about the frames it recorded and nothing else. The reverse also holds — when a
sabotage survives `viewdiff`, instrument the arm before concluding anything, because "the picture
did not change" and "the code did not run" look identical from outside.

## ⭐⭐ `make transtrap` — DOES ANY TRANSLITERATION STILL RUN?  (measured 2026-09-08)

With the hook campaign finished (93 of 93 `(circuit, entry)` pairs twinned), the claim the whole
port is aimed at — *the interpreter is gone* — was still only a claim: reading the source cannot
see a rare arm, a per-circuit hook body or a self-modifying re-entry, and `revs_gen.c` still
defines ~49 non-oracle bodies that a call site somewhere could reach.

`make TRANS_TRAP=1` makes every generated body that is **not** a `__t6502` oracle record its own
entry (`src/platform/trans_trap.h`; the macro compiles to nothing without the flag), and
`make transtrap` drives nine scenarios — the front end, a 300-frame race, the 1500-frame crash
trajectory, and all six circuits — and FAILS if any of them reports one.

**Result: zero. No transliterated body executes in any of the nine.** Not `region_23d8`
(`road_edge_walk`'s body, which the expansion circuits' hooks used to re-enter at `$2490` — the
twins now run that walk), not the `$7BF7` view-cell chain, not `project_point`'s or
`bearing_to_section`'s two-byte short entries.

**Sabotaged both emission shapes before believing it:**
- drop `('NURBURG', 0x57AD)` from `HOOK_TWINS` → `trk_nurburg@57AD`, 55 entries on circuit 5;
- call `step_walk_one_segment()` from the main loop → `step_walk_one_segment`, 55 entries.

⚠ **Two honest limits, and the first one bit during the sabotage.**
1. **A body no scenario drives is UNPROVEN, not dead.** The first sabotage aimed at
   `('NURBURG', 0x59D9)` — the steering hook — and the gate reported *nothing*, because `$1593`'s
   hook is on the **joystick** arm and a host run drives the keyboard. The gate covers what it
   drives: qualifying, the pits and the unusual menus are outside it.
2. **A clean run must still write its log.** Arming `atexit` on the first hit makes a clean run
   indistinguishable from one that died before exit; the runtime arms it in a constructor instead,
   so "no log" means the run died and is a FAIL.

## ⭐⭐ WHAT THE SURVIVING `cpu` TRAFFIC ACTUALLY IS — measured on the linked target, 2026-09-07

The standing worry is that `revs_native.c`'s remaining `cpu.A/X/Y/C/N/V/Z` traffic is **oracle
overhead** — marshalling that exists only so `make validate` has something to compare, and that a
properly-native production build should not pay. Settled by looking at the target binary, not at
the source:

**The oracle costs the target NOTHING.** `src/gen/revs_gen.c` carries **239 `__t6502` bodies** and
is compiled and linked into the Amiga build, but **0 of them survive the link** — `--gc-sections`
drops every one, because the only references to them are from the harness, which is not in this
binary (525 KB of transliteration in, 320 KB shipped). So there is no oracle-only cost to remove.

**Every shim that DOES survive has a production caller**, and only four do. Dump them the same way
rather than guessing:

```
m68k-amiga-elf-objdump -d out/Revs.elf > /tmp/revs.dis
awk -v t="<abs8>" '/^[0-9a-f]+ </{fn=$2} $0 ~ ("jsr.*"t"$"){print fn}' /tmp/revs.dis | sort -u
```

| Surviving shim | Called from | Why the `cpu` handover is the contract |
|---|---|---|
| `abs8`, `scale_by_track_gradient` | **`trk_brands` / `trk_doning` / `trk_nurburg` / `trk_oulton` / `trk_snetter`** | the **per-circuit hook bodies**, still transliterated in `src/gen/revs_track_hooks.c` — 3066 lines, 454 `cpu.` refs. A hook calls these by 6502 address with live registers, so the shim's ABI is the seam's ABI |
| `read_pedals_and_gears` | `read_driving_controls` | native→native, but a genuine escaping flag: $162D's `CMP #$91` carry leaks out through the no-key exit |
| `sound_queue_exit_abi` | `check_crash` | native→native exit-ABI replay |

⭐ **The conclusion that matters: the last transpiled PRODUCTION code in this port is the
per-circuit hook file, and it is what keeps the 6502 ABI alive at the seam.** Not the oracle, and
not the twins. So "get rid of the cpu-struct stuff that only validation needs" has no work left in
it; the work is **twinning `revs_track_hooks.c`**, whose gate already exists and is green —
`make viewdiff`, every circuit's race view byte-exact over display lines 82..166. ⚠ And it is the
one campaign where the CLAUDE.md hook-seam rule bites hardest: these bodies run on four of the five
circuits and nothing but `viewdiff` can see them compute wrongly.

### …and the SOURCE-SIDE audit that finishes it: all 22 `_core` bodies, 2026-09-10

The section above settles the *cost* question from the binary. This one settles the *style*
question from the source: **76 `cpu.` references remain inside `_core` bodies, across 22
functions, and every one is in a class that has to keep it.** Enumerate them the same way rather
than grepping by hand (blank the comments to spaces, or every line number is wrong):

```
python3 - <<'EOF'
import re
raw=open('src/gen/revs_native.c').read()
s=re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]',' ',m.group(0)), raw, flags=re.S)
# ...attribute each `cpu.` line to the last column-0 function definition above it,
#    and keep only the owners whose name ends in `_core`.
EOF
```

| Class | Functions | Why it stays |
|---|---|---|
| **Hook / SMC seam** — hands a circuit's own 6502 code the whole register file | `emit_edge_width_offset_core`, `build_track_geometry_core`, `horizon_half_width_at_core`, `fill_line_attr_core`, `load_section_from_segment_core`, `rebuild_walk_reversed_core`, `read_driving_controls_core` | CLAUDE.md's hook-seam rule: the register set comes from the *surrounding instructions*, and an expansion circuit's hook reads registers Silverstone's callee does not. Gated only by `make viewdiff` |
| **SED/CLD at a sanctioned BCD site** — `adc_value` reads `cpu.D` | `add_tally_to_lap_total_core`, `lap_complete_core`, `check_car_pair_core`, `sort_cars_by_key_core`, `tally_bcd_column_core` | Decimal mode is inventoried in `docs/static-map.md` §Decimal mode; these are five of the eight `SED` sites and the flag is the mode, not a value |
| **An OS-call register contract the harness compares** | `shift_key_commands_core`, `kbd_test_key_core` | A/X/Y reach the MOS inside `sound_stop_all`, and the harness compares registers at every OS-call boundary. ⚠ `shift_key_commands_core`'s `cpu.X` at `$0F57` is genuinely ambient — the pause spin's own `kbd_test_key` leaves `$FF` in it on any frame that paused |
| **The frame driver's live ambient register file** | `race_main_loop_core` | MEASURED, see below |
| **A flag that genuinely escapes, or `cpu.S`** | `clamp_and_store_steer_angle_core`, `tick_race_timers_core`, `enter_session_core`, `draw_road_core`, `mul16_by_1_5_core`, `update_camera_and_drive_state_core`, `engine_init_core` | each argued at its own site — `update_camera_and_drive_state_core`'s ASL/ROL pair must stay bytes because a per-circuit hook runs *between* the two shifts, and `engine_init_core`'s `cpu.S` is the one cell that really is the 6502 stack pointer |

#### ⚠⚠ `race_main_loop_core`'s ambient X/Y is PRODUCTION state, not oracle plumbing

The frame driver looked like the biggest prize — a NATIVE_FUNCS driver threading raw `cpu.Y/V/C`
between phases. It is not removable. MEASURED with a per-phase drift probe (`cpu.X`/`cpu.Y`
compared against the previous phase's exit, sabotage-verified by perturbing the recorded value)
over 300 driving frames:

- **Twelve writers:** phases 1, 3, 4, 5, 7, 9, 12, 18, 20, 23, 24 and the frame tail each rewrite
  the ambient X or Y — phases 5, 18, 20 and 24 on essentially every frame.
- **Three consumers, and the values reach `mem[]`:** `update_lap_timers` (phase 8) hands ambient
  X/Y straight to `update_position_display_core` as a *text cursor* on the race arm;
  `engine_sound_update` (phases 9/12/20) and `check_crash` (phase 23) pass X to
  `sound_queue_core`, which stores it in `sound_saved_x`.
- **The producers are not removable either.** Phase 5's every-frame writer is
  `build_track_geometry`'s own **shim**, publishing `GeoExit` — the driver calls the shim
  deliberately, for the wide-value marshals. The rest are the sanctioned hook/SMC seams.

So threading it as a `FrameAmbient` struct through thirteen shims would move the same bytes
through a different container and buy nothing. ⭐ **The general form: a driver's `cpu` traffic is
the TAIL of a chain whose head is a shim or a hook seam publishing an exit ABI. Convert
bottom-up or not at all** — and check whether the value reaches `mem[]` before assuming it is
plumbing.

## What "validated" costs and buys

Buys: a byte-exact differential over randomised inputs, re-run in seconds, forever.
Costs: the twin must reproduce every `mem[]` write the oracle makes that anything can still
observe — including scratch cells, unless they are *proven* dead (no reader before the next
write).  Proven-dead cells go in the fixture's ignore list with the proof in a comment, never
just to make a test pass.

See `docs/validation-harness.md` for what the harness guarantees, and `CLAUDE.md` §Transpiler
for the mechanical steps of making a function native.
