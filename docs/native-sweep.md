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

### `paint_lines_short` / `paint_lines_clipped` — the boundary stores pay `bus_write`'s range test
`revs_native.c` — the three per-line boundary-cell stores
(`bus_write(view_screen_addr(plot_ptr_v, v->cell), …)` ×2 in `paint_lines_short`, ×1 in
`paint_lines_clipped`). The **unit** loop in the same file already hoists this exact test
(`const int busSafe = view_span_is_ram(base0) && view_span_is_ram(base1);`), so the pattern
and the predicate both exist — the boundary stores were simply never converted. ~50-58 calls
a sweep, on the view pipeline (54% of the frame). `view_span_is_ram(base)` covers `base+320`,
a superset of `base + v->cell` (`v->cell` is one byte), so the existing predicate is a sound
and conservative hoist. **Gate: `make validate FN=view_paint_lines` + determinism + `viewdiff`.**

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
