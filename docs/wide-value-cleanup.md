# Wide-value cleanup — deleting 6502 byte-lane handling of 16-bit values

**Status: PLAN (no code touched). User-directed campaign, currently PAUSED pending confirmation
and the in-flight MOS-dispatcher session landing.**

A ledger, not a log. As each base/cell is converted, update its row's **Status** and delete it
from the "Remaining" reasoning once done. Same discipline as `docs/helper-elimination-audit.md`
and `docs/cpu-elimination-audit.md`.

## Goal

`revs_native.c` still handles many 16-/24-bit values as separate 6502 byte lanes — the
`_lo`/`_hi` split-and-recombine the transliteration left behind. Replace each with a proper
wide value so the 68000 does the arithmetic in one word op.

## Why this is SPEED, not clarity (read this before dismissing any tier)

Just count instructions. A 16-bit `+1` written in 6502 byte lanes is several `move.b` +
`lsl`/`lsr` + `or` + carry `branch`; the right 68000 code is **one** instruction —
`add.w #1,addr`. GCC cannot recover the wide op once the value is scattered across separate
`mem[]` byte cells, so the optimiser does not fix this for us. Clarity is a bonus.

⚠ Do **not** quote the dead-flag / de-macro campaign's "framerate moved 0 / cosmetic /
within-noise" results here — those are scoped to removing dead FLAG stores (which GCC elides
because `cpu` is a plain global). Byte-lane 16-bit *arithmetic* is a different mechanism: fewer
instructions AND fewer `mem[]` accesses. The one thing GCC can't elide is the `mem[]` traffic a
6502 idiom forced, and that is exactly what this campaign removes.

## The two mechanisms (and which applies where)

**(A) Wide-local hoist — keeps the bytes in `mem[]`.** Read the pair once into a `uintNN_t`,
compute in wide C, write the pair back once. Reduces redundant `mem[]` loads/stores and does the
arithmetic wide. **Compatible with still-transliterated co-readers** because the bytes still land
at their BBC addresses. This is the near-term, always-available win.

**(B) Full relocation to a real `uintNN_t` variable/array — drops the bytes from `mem[]`.** Only
this gives the single-instruction `add.w`/`add.l` form. Requires **every** reader to be native
(no raw-address reader left in `revs_gen.c`/`revs_manual.c`), the old byte cells added to the
differential ignore-list, and `determinism`/`-drive`/`-crash` proving nothing external observes
them. For internal scratch this is reachable now; for persistent game-state arrays it is gated on
de-transliteration and is a long-horizon milestone.

### The arithmetic must become plain C — this is the point, not a side effect

Widening the *storage* is only half the task. Once a value lives in a `uintNN_t`, the math on it
**must** be rewritten as ordinary C — `+`, `-`, `<<`, `>>`, `*`, `==`, `<`, `>=` — deleting the
6502-originated carry/borrow machinery that wrapped it:

- carry-threaded byte-pair chains (`adc_step`/`sbc_step`/`adc_value`/`sbc_value`, explicit
  `carry` in/out bools, `Adc`/`Sbc` structs) collapse to a single wide `+`/`-`;
- ripple shifts across lanes (`ASL lo` + `ROL hi`, `LSR hi` + `ROR lo`) become one `<<`/`>>`;
- multi-byte compares done as SEC/SBC-then-test become a single relational operator.

A conversion that widens the variable but leaves the carry idioms in place has done the
mechanical part and missed the instruction-count win — the wide op is exactly what the 68000
does in one instruction, and the carry chain is what it can't. **Treat de-carrying the arithmetic
as the deliverable; the wider variable is just what enables it.**

The one sanctioned exception is unchanged from the campaign so far: where a 6502 flag genuinely
**escapes** the routine (into an SMC dispatch, a sibling, or the differential's compared stack
residue), reconstruct that ONE flag at the boundary from the high-byte op — never re-introduce a
carry chain through the body to carry a flag that is dead at the exit.

⚠ Two hard constraints rule out a naive "just make it `uint16_t` in place":
- **Endianness** — `mem[]` is little-endian, Amiga is big-endian. Never alias a pair as
  `uint16_t*`/`uint32_t*` (renders garbage on target, `validate` stays green). `make endian-lint`
  guards it. The single-instruction win therefore requires mechanism (B), a *real* variable.
- **Non-adjacency (SoA)** — the state vectors are structure-of-arrays: all low bytes, then all
  high bytes, $3–$600 apart. There is no contiguous 16-bit cell to `add.w` even ignoring
  endianness. SoA arrays get the single-instruction win ONLY through relocation to `value_16[N]`.

## Step 0 — consolidate the SoA base `#define`s (pure code-motion, do first)

The array-base constants (`MODEL_STATE_LO` = $62D0, base of a 15-element vector, indexed `+i`)
are **not** in `mem.h` — `mem.h` is auto-generated from `symbols.csv` per named *cell* and has no
concept of an array base. They were hand-declared locally in `revs_native.c`, and the migration
to the shared hand-maintained header `src/gen/revs_native_seam.h` was started but left half-done:

- `POINT_DELTA_HI`, `SLIP_MAG_HI`, `SLIP_SIGN` already live in `revs_native_seam.h`.
- ~12 bases are declared **twice inside `revs_native.c`** (`MODEL_STATE`, `CAR_ANGLE`,
  `VIEW_ORIGIN`, `EDGE_X`, `SLIP_MAG`, `EDGE_OPP_X`, …), once per function that needed them.

**Action:** move every SoA base `#define` into `revs_native_seam.h`, one definition each, delete
the duplicates. This gives a single swap point when a base later becomes a `value_16[N]` array.
Gate: `make validate` 0-mismatch + `determinism`/`-drive`/`-crash` byte-identical (no behaviour
change). No sabotage needed (no fixture changes).

## Inventory

Cross-file reader counts are raw-address hits in `revs_gen.c` (the transliterated corpus) in each
array's byte range — an over-count to be refined per-base at execution, but they show the
relocation blast radius. High count ⇒ mechanism (B) is blocked; use (A) now.

### Tier 1 — zero-page scratch, adjacent, internal to a call-chain (relocatable soonest)

| Cell(s) | Addr | Role | Mechanism | Status |
|---|---|---|---|---|
| `math_lo/hi` | $74/$75 | shared 16-bit accumulator (305 refs — the big one; template) | A→B | TODO |
| `point_dist` | $7C/$7D | projected distance (render) | A→B | TODO |
| `hypot_min`/`hypot_max` | $78/$79, $7A/$7B | sorted ground-plane magnitudes | A→B | TODO |
| `bearing` | $8A/$8B | bearing_to_section output | A→B | TODO |
| `plot_ptr`/`plot_ptr2`/`plot_ptr3` | $70/$71,… | screen write pointers | A→B | TODO |
| `edge_nearest`, `point_delta`, `object_dist`, `nearest_edge_bearing` | $10/$11, $80–$83, $55, $5E | edge-walk scratch | A→B | TODO |
| `SLIP_MAG` | $8E/$8F | slip magnitude (adjacent; aliases plot_ptr3) | A→B | TODO |

⚠ **Aliasing group — convert together:** `POINT_DELTA` $0080–$0083 is viewed simultaneously as
`MUL_SRC` ($80/$81) and `MUL_TERM` ($82/$83) — same zero-page bytes, three names (mul
multiplicand/multiplier). Convert as one unit or the shared storage corrupts.

### Tier 2 — persistent, adjacent (wide-local hoist now; relocation later)

| Cell(s) | Addr | Role | gen readers | Mechanism | Status |
|---|---|---|---|---|---|
| `car_heading` | $0A/$0B | 16-bit heading angle | some | A (B blocked) | TODO |
| `lap_length` | $59FC/D | track-file lap distance | some | A | TODO |
| `band2_duration` | $4F21/2 | horizon band duration | few | A→B | TODO |

### Tier 3 — SoA state vectors, non-adjacent (relocate `lo_8[N]`/`hi_8[N]` → `value_16[N]`)

The largest and highest-value tier. Single-instruction win needs mechanism (B); (A) hoist helps
in the interim but the final store stays two non-adjacent byte writes until relocation.

| Base | lo/hi | Shape | gen readers | Mechanism | Status |
|---|---|---|---|---|---|
| `MODEL_STATE` | $62D0/$62E0 | 15×16-bit driving-model state (stride $10) | ~47 | A now; B blocked | TODO |
| `CAR_ANGLE` | $62A0/$62A3 | 3× (heading_sin/cos, steer_angle) | ~9 | A now; B blocked | TODO |
| `CAR_DISTANCE` | $08D0/$08E8 | per-car (20) distance-round-lap | ~19 | A now; B blocked | TODO |
| `OBJECT_BEARING` | $0380/$0398 | per-slot 16-bit track position | ? | A now; B blocked | TODO |
| `SECTION_COORD` (=`SECTION_CRD`) | $0900/$0A00 | section origin (stride $100) — **two names, one addr; dedupe** | ~62 | A now; B blocked | TODO |
| `EDGE_OPP_X` | $5E50/$5EA0 | opposite-boundary angle | ? | A→B | TODO |
| `MARKER_OFF` | $62B7/$62BA | marker offset | ? | A→B | TODO |
| `VIEW_ORIGIN` | $6280/$6283 | 3 components, stride 6, two origins | ~30 | A now; B blocked | TODO |
| `ROW_BASE` | $2B22/$2B1E | surface_edge buffer bases | ? | A→B | TODO |

### Excluded — NOT plain 16-bit binary (do not force into `value_16`)

| Base | Addr | Why excluded |
|---|---|---|
| `RACE_CLOCK` | $06B4/$06E4 | 3-byte **BCD** (24-bit), per-car — an SED site |
| `CAR_BEST_LAP`, `CAR_LAP_START` | $06A0/$06D0, $0898/$04DC | per-car **BCD** lap times (bases live far apart) |
| `STANDINGS_BCD` | $3878/$39F8 | **BCD** standings |
| `TRACK_SEGMENT` | $5900/$5300 | track-file 8-byte segment records; lo/hi $600 apart |
| `CHAR_ROW` | $3FE0/$3B06 | screen row **address table** (not arithmetic) |
| `FENCE_PATTERN` | $3D78/$3D7C | 4-byte **dither pattern** (not arithmetic) |

The exclusion criterion is **BCD, not bit width.** Binary 24-bit values ARE in scope — see
below. The BCD 24-bit values (`RACE_CLOCK`, `CAR_BEST_LAP`, `CAR_LAP_START`, `STANDINGS_BCD`)
stay excluded because a `uint32_t` cannot do base-60 BCD arithmetic; they keep byte/BCD form at
the 8 SED sites.

### Binary 24-bit → `uint32_t` (in scope, same idiom extended)

| Base | Addr | Shape | Mechanism | Status |
|---|---|---|---|---|
| `OBJECT_COORD` | $09FD/$0AFD (+mid plane) | per-object 3-byte binary world coord | A→B | TODO |

The 68000 has single-instruction `add.l`/`sub.l`/`move.l`/`cmp.l`, so a binary 24-bit value in a
`uint32_t` is one op where three byte lanes were a carry-chain. Extend the wide-value idiom:
- Hold the value in a `uint32_t`; **mask `& 0xFFFFFF`** on store-back.
- Replay exit flags from the **top (3rd) byte** op, not the 32-bit result (6502 Z/N/C/V come from
  the high-byte op).
- ⚠ `add.l`/`sub.l`/`cmp.l` only. **Never** a 32-bit multiply/divide (`__mulsi3`/`__divsi3`/
  `__udivsi3` — the 68000 has none; `amiga/Makefile` muldiv-audit fails the link). A 24-bit path
  that multiplies stays on `src/cpu/m68k_math.h`'s 16-bit helpers.

## Per-variable procedure (convert all uses of one cell/base together)

1. **Map every producer and consumer** across `revs_native.c`, `revs_native_seam.c`,
   `revs_gen.c`, `revs_manual.c`. A single transliterated raw-address reader pins the cell in
   `mem[]` ⇒ mechanism (A) only for now; note it "blocked-on-reader".
2. **Mechanism (A):** read pair once → `uintNN_t` local → **plain-C arithmetic** → write pair
   once. Rewriting the math as ordinary C is required, not optional — delete the carry/borrow
   idioms (`adc_step`/`sbc_step`, carry bools, `Adc`/`Sbc` structs, ripple shifts) per "The
   arithmetic must become plain C" above. Keep every scratch `mem[]` write the oracle still makes.
   Replay any escaping flag from the **high-byte** op (6502 Z/N/C/V come from the high byte, not
   the wide result — see the wide-value idiom).
3. **Mechanism (B), when all readers are native:** declare a real `uintNN_t` variable/array in
   `revs_native_seam.h`, thread by value, delete the `mem[]` writes, add the old byte cells to the
   fixture ignore-list, prove dead at exit on every path.
4. **Gate every conversion:** `make validate FN=<affected>` 0-mismatch → **≥4 distinct sabotages
   FAIL** (rm the .o + binary before each rebuild) → `make determinism` + `determinism-drive` +
   `determinism-crash` byte-identical.
5. **Perf-size the hot ones** (Tier 1 render cells, Tier 3 arrays): in-process differential
   `make VERIFY=1 PROBES=1 FIXED_RNG=1`, quote the `fps_series.gdb` **row-vector** (not `total`),
   averaging non-outlier rows. Expect a measurable gain — this is the perf thesis under test.

## Ordering

0. **Step 0 consolidation** (above) — clears the duplicate-define debt first.
1. **`math_lo/hi`** — highest ref count; its threading pattern is the template for the rest.
2. **Tier 1 render scratch** (`point_dist`, `hypot_*`, `bearing`, `plot_ptr*`, `edge_*`) — the
   view pipeline is 54% of the frame; mechanism (B) reachable, biggest near-term FPS.
3. **Tier 3 via mechanism (A)** — wide-local hoist on `MODEL_STATE`/`CAR_ANGLE` etc. now; the
   full `value_16[N]` relocation (B) follows each array's readers going native.
4. **Tier 2** — as the adjacent persistent cells' readers convert.

## Coordination

- The MOS-dispatcher session touches shared files; `math`/`plot_ptr` are broadly referenced. Do
  not start until it has landed (avoid a mid-flight merge collision).
- Commit one cell/base per commit; gate each as above; keep this ledger's Status column current.
