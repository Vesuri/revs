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
   (`model_state_lo`/`_hi` and the hand-integrated `model_accum`), the object plotter's four-cell
   argument block (`plot_row` / `plot_column` / `plot_width` / `plot_shape`) and the 24 per-slot
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

### ⭐ A patched SMC BRANCH OFFSET is a narrower obligation than it looks

`$231A`'s offset can in principle name ~200 addresses inside `road_edge_start`, and the
transliteration emits a switch over all of them.  A twin cannot, and does not have to: `make
track-patch` says every circuit on this disc writes `$00` and Silverstone has `$0F`, so the twin
recognises those two and hands anything else to `platform_smc_unhandled`.  That is a *declared*
coverage limit, not a guess — and `make track-run` is what would catch a circuit that ever wrote a
third value.  The fixture then has to plant one of the two, because a random third byte sends the
two models to different addresses and measures the limit rather than the twin.

## What "validated" costs and buys

Buys: a byte-exact differential over randomised inputs, re-run in seconds, forever.
Costs: the twin must reproduce every `mem[]` write the oracle makes that anything can still
observe — including scratch cells, unless they are *proven* dead (no reader before the next
write).  Proven-dead cells go in the fixture's ignore list with the proof in a comment, never
just to make a test pass.

See `docs/validation-harness.md` for what the harness guarantees, and `CLAUDE.md` §Transpiler
for the mechanical steps of making a function native.
