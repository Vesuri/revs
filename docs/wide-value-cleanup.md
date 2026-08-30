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

## Step 0 — consolidate the SoA base `#define`s (pure code-motion, do first) — ✅ DONE (HEAD after 646d506)

18 duplicated base/table constants (`MODEL_STATE_LO/HI`, `CAR_ANGLE_LO/HI`, `VIEW_ORIGIN_LO/HI`,
`EDGE_X_LO/HI_TBL`, `TRACK_DIR_0/1/2`, `SURFACE_COLOURS_TBL`, `COLOUR_PATTERN_AND/KEEP`,
`CAR_FLAGS_SHAPE`, plus the already-in-header `VIEW_LEFT/RIGHT_START_SRC`, `DASH_BLOCK_STARTS`)
now have one home in `revs_native_seam.h`; 40 local copies deleted from `revs_native.c`. Pure
code-motion — validate + all 3 determinism byte-identical, both backends clean. (Role-specific
zero-page aliases like `MUL_SRC`/`SPAN_*`/`EDGE_COLUMN` over `POINT_DELTA` stay local by design;
they dedupe at their own conversion, per the aliasing note above.)

### (original plan, retained for reference)

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
| `band2_duration` | $4F21/2 | horizon band duration | 0 (only irq1v + its oracle) | **B DONE** | ✅ `band2_duration_v` (revs_native.c) |

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

### The mechanism (B) template (established on `band2_duration`, the first zero-reader relocation)

The concrete machinery, reusable for every (B) cell. The relocated var stores identically to the
two mem[] bytes it replaces; the **`__t6502` oracle still uses mem[]**, so validate and determinism
must be told the old cells are no longer state:

- **The var:** a file-scope `static uintNN_t <name>_v;` in `revs_native.c` (or `revs_native_seam.h`
  for an array shared across files). A *persistent* cell (written in one call, read in a later one)
  is faithful as a static — it persists exactly as the mem[] bytes did.
- **validate:** `set_ignore({old cells}, n)` around the fixture block, `set_ignore(0,0)` after. If
  the value is consumed in a *different* invocation than it is produced (so the oracle reads its
  pinned mem[] copy while the twin reads a stale static), add a tiny native-only setter
  (`set_<name>_v`) and seed it to match the pinned input in that arm. A within-one-call
  producer→consumer path needs no seeding (the twin writes then reads its own var).
- **`det_compare.py`:** add the cell range to `RELOCATED` (a fresh run leaves those bytes at
  unpack residue; the old-code golden still holds the computed value).
- ⚠⚠ **TRAP — the parked determinism frame can coincidentally match.** `make determinism` (parked)
  passed *without* the skip because at that frame the residue equalled the golden's value; only
  **`determinism-drive` and `determinism-crash` genuinely diverge** at the relocated cell. Always
  sabotage-verify the `RELOCATED` skip against **drive AND crash**, not the parked variant, and
  confirm the diff is scoped to *exactly* the relocated addresses (nothing else moved).
- **Amiga:** the setter is unreferenced there → dropped by `--gc-sections`; the relocation itself
  is pure-mem code compiled into both backends. Confirm `make` (amiga) links clean.

### ⚠⚠ (B) DISQUALIFIER — a cell that bridges a hybrid oracle's glue and its native sub-cores

"Zero transliterated readers in `revs_gen.c`" is necessary but **not sufficient** for mechanism (B).
A validated parent (in `VALIDATE_FUNCS`) gets a `__t6502` oracle that is a *hybrid*: its
transliterated glue calls the parent's **native** sub-cores, and any `mem[]` cell the glue uses to
pass a value to (or receive one from) those native children is the **data channel between them**.
Relocate that cell and the oracle breaks against itself: the glue writes/reads `mem[]` while the
native child now writes/reads the `_v` static the glue never touches — the *reference* is corrupted,
so validate fails on downstream outputs (not on the ignored cells) even though each sub-core passes
in isolation.

- **The tell:** parent validate FAILS on physics/state outputs while every relocated sub-cell is on
  the ignore-list AND each sub-core validates 0-mismatch standalone. The break is in the composed
  oracle's internal data flow, invisible to per-sub fixtures.
- **The disqualified case (2026-08-27):** `model_accum_entry` ($38/$39) + `model_accum_delta`
  ($3A/$3B). Every `revs_gen.c` reference is inside a `__t6502` oracle — but that oracle is
  `apply_driving_model__t6502`, whose glue does the $46AE entry-save into `mem[$38/$39]` and the
  $46DF restore from `mem[$3A/$3B]`, while calling native `stage_accum_delta` (writes the delta) and
  native `apply_drag_terms` (reads the entry high byte). The cells ARE the glue↔core channel.
  Relocation broke `apply_driving_model` 200/200; **reverted, kept in `mem[]`.**
- **The clean case for contrast:** `band2_duration` lives entirely inside one function
  (`irq1v_band_schedule`) — producer and consumer are both native code in the same core, no parent
  oracle marshals it through `mem[]`. That is what makes a cell a *true* zero-reader (B) candidate.
- **Refined criterion:** a cell is (B)-eligible only if it is scratch **local to a single native
  function** (produced and consumed within one `_core`), OR every function that touches it is native
  with no `__t6502` oracle-glue reading/writing it as a call-boundary channel. When a validated
  parent's oracle-glue passes the cell to a native child, the cell stays in `mem[]` until the parent
  itself goes native (`NATIVE_FUNCS`, gated by determinism not validate) — then the glue is gone.

## Ordering

0. **Step 0 consolidation** (above) — clears the duplicate-define debt first.
1. **`math_lo/hi`** — highest ref count; its threading pattern is the template for the rest.
2. **Tier 1 render scratch** (`point_dist`, `hypot_*`, `bearing`, `plot_ptr*`, `edge_*`) — the
   view pipeline is 54% of the frame; mechanism (B) reachable, biggest near-term FPS.
3. **Tier 3 via mechanism (A)** — wide-local hoist on `MODEL_STATE`/`CAR_ANGLE` etc. now; the
   full `value_16[N]` relocation (B) follows each array's readers going native.
4. **Tier 2** — as the adjacent persistent cells' readers convert.

## ⚠ FINDING (2026-08-27, on starting execution): mechanism (A) is largely ALREADY DONE

Surveying `revs_native.c` after Step 0: the prior **helper-elimination** and **cpu-elimination**
campaigns already rewrote nearly all the render/geometry/physics byte-lane arithmetic as wide C.
`MODEL_STATE`/`CAR_ANGLE`/`math_lo/hi`/etc. are read as `((hi<<8)|lo)`, computed in a `uint16_t`
local, and written back once — i.e. the mechanism-(A) hoist. The only byte-lane carry-chains that
remain in the cores are:

- **Documented flag-escapes that must stay** (a genuinely escaping V/C): `plot_ptr`'s `adc_step`
  chain (revs_native.c ~L690), `fill_line_attr`'s `farBase = adc_step(horizon_index,0x28,0)` on the
  SMC-early-return path, `bearing_to_section`'s `+0x78`. These are the sanctioned exception, not TODO.
- **The excluded BCD routines** (`lap_complete`, `tally_bcd_column` → `STANDINGS_BCD`,
  `RACE_CLOCK`/`CAR_LAP_START`) — `adc_value`/`sbc_value` under `cpu.D=1`, correctly kept.

**Consequence:** the near-term instruction-count win the campaign promises now lives almost
entirely in **mechanism (B)** — relocating a `lo_8[N]`/`hi_8[N]` pair to a real `value_16[N]`
array (or a scratch pair to a real `uint16_t` var), which turns each access into one
`move.w`/`add.w` and drops the `mem[]` byte traffic GCC can't elide. **(B) is blocked everywhere
by transliterated readers in `revs_gen.c`** (and, for zero-page scratch, by heavy address
aliasing). So the campaign is now gated on **de-transliteration** — nativizing the reader
functions below — before any single cell can be relocated.

### Nativization worklist — `math_lo/hi` ($74/$75) readers (unblocks its relocation)

The "410" in the plan was raw line-hits. At the function level, **90 functions** in `revs_gen.c`
reference `math_lo/hi`; **63 are `__t6502` oracles** of already-native routines (they stay — the
oracle is the validation twin). The **27 genuine plain transliterated readers** that pin the cell
in `mem[]`, and must go native before `math_lo/hi` can become a real `uint16_t` (mechanism B).
⚠⚠ **The "27 readers" over-counts: 6 are ORACLE-ONLY tail-bodies with NO shipping caller.**
The worklist method ("top-level def that references the cell, minus `__t6502`") counts every
transliterated body, but some are reached *only* from `__t6502` oracles of already-native
functions — the native versions reimplement that logic inline, so the transliterated body never
runs in the shipping binary. Nativizing one yields **zero perf win** (this is the reader-side
twin of the [[disqualifier]] finding). Classify each by whether a NON-oracle caller exists,
checking BOTH `revs_gen.c` AND `revs_native.c`/`_seam.c` (native drivers like `race_main_loop`
call transliterated routines from `revs_native.c`, invisible to a gen-only grep). Confirmed
oracle-only (drop from the perf worklist):

```
region_23d8  region_31d0   ← only via road_edge_walk__t6502 / paint_fence_backdrop__t6502
FUN_163b     FUN_1f11      ← only via clamp_and_store_steer_angle / read_driving_controls /
                             apply_steering_assist / steer_apply_with_assist  __t6502
FUN_4876     FUN_48a7      ← only via apply_angle_term / apply_angle_term_at /
                             add_signed_into_element  __t6502
```

**Done:** `print_spaces` ($3D50, #148), `draw_starting_lights` ($7B4A, #149),
`update_horizon_band` ($4F44, #150 — a `math_hi` reader/writer, the archetypal wide-value target:
the 6502's `clamp << 6` + sign-extend done as a PHP/PLP-threaded ROR pair across `math_hi:A`
collapses to `0x04D8 + (v<<6) + (c0?0xC000:0)`; `math_hi` still written with its 6502 exit value
until relocation),
`draw_corner_markers` ($1B12, #151 — the corner-marker consumer; both `math_lo` AND `math_hi` as a
16-bit pair. The 6502 doubles the 16-bit `marker_offset` (`ASL`/`ROL` across the lanes), 16-bit-adds
it onto the edge point's azimuth, then `<<6` (top byte, re-centred on `$50`) → `plot_x` and `<<3`
(`|hi byte|`) → `proj_width`; the core is one 16-bit expression per quantity. `math_lo/hi/$76` keep
their 6502 exit values until relocation. Result-only; feeds `plot_object`, so the fixture is the
`draw_track_object` pattern — `plant_plotter_chains` + `$1FE9` planted; `plot_shape` is hardcoded 6
so plot_object's shape-9 spin cannot arise. `draw_corner_markers` has NO SMC seam of its own, so
`diff_run`'s own SMC-count equality is what pins the deeper `$1DD4` trap the shape-6 draw path
reaches ~1/30 of drawn markers),
`mirrors_update` ($7B00, #152 — the once-per-frame wing-mirror update; a `math_lo` reader-nativization,
NOT a wide value. The half-height scratch (`object_width>>3`) was `math_lo`; the core lifts it to a
C local and brackets the block around the mirror centre line `$B6` (bottom = `$B6+half` →
`shared_temp_84`, top = `$B6-half` → `span_line_cursor`) and folds the heading selector
`(object_bearing_hi - car_heading_hi - 4)>>3` → `shared_temp_76`; `math_lo`/`$84`/`$7F` keep their
6502 exit values, written only on the drawable path. Result-only; the per-segment loop and the
`mirror_draw_car` plotter calls stay in the shim — the plotter is shared by both differential sides
so its writes cancel as long as the twin hands it the same `A`=value / `Y`=segment. ⚠ Fixture trap:
`mirror_draw_car`'s inner loop spins forever on a zero `$3EFA[seg]`, so the fixture plants
`$3EFA[0..5]` nonzero and points `$3B9E[seg]` at page `$80` to keep the plotter's own writes off
`$3EFA`. ⚠ Core arithmetic trap: the `-4` in the fold is an 8-bit `SBC #4` — a bare `- 4u` promotes
to `int` and mis-shifts a byte `< 4` (`$FE>>3` should be `$1F`, not `$FF`); wrap to `uint8_t` before
the `>>3`).
`dial_needle_angle` ($51A8, #153 — the rev-counter needle's arithmetic half; a `math_lo` reader-
nativization, NOT a wide value. The reading of `engine_revs` is clamped to a floor of `$1E`, taken
×0.75 (`revs + revs>>1` then `ROR`), rotated by `$4C` and reduced mod `$26` to a quadrant + remainder;
the remainder is folded about `$13` into an `offset` plus a mirror bit, and `(quadrant<<1)|mirror`
becomes the `octant` step-opcode selector. The `_core` returns those as a small struct; the shim
writes the 6502 exit cells (`math_lo`=offset, `$76`=octant, `$77`=step variant, `$83`/`math_hi`=DDA
length from `$3100[offset]`, `$70`/`$71`=origin from `$32FC`/`$397C[quadrant]`) and sets `A`/`X`/`Y`
before falling through into the shared SMC plotter `plot_line_octant`. Result-only; the plotter runs
under `REVS_SMC_CONTINUE=1` on both differential sides so its writes cancel. Named the 3 static dial
tables `dial_needle_dda_tbl`/`dial_needle_origin_lo_tbl`/`dial_needle_origin_hi_tbl`).
`driver_name_address` ($3CEB, #154 — was `FUN_3ceb`; a `math_lo` reader-nativization, NOT a wide
value. Maps a car/slot index in `X` → the `(lo,hi)` address of that driver's 12-char name in
`driver_name_table` ($4050): `hi = $40 + index/4`, `lo = $50 + (index&3)*12` (4 names per page at
12-char strides). `math_lo` holds the dead intermediate `(index&3)*4`; exit A=lo, Y=hi are live
(consumed by `emit_driver_name` $3250 as `plot_ptr2`), exit flags dead. ⚠ Its fixture is
memory-free — the twin reads no `mem[]`, so it calls NO `fill_random` and consumes zero PRNG draws;
a boilerplate `fill_random` loop shifted the global stream and surfaced a latent coverage case in
the unrelated `road_span_plot` twin, see docs/validation-harness.md §"THE GLOBAL PRNG IS ONE STREAM".
Named `driver_name_address` ($3CEB), `emit_driver_name` ($3250), `driver_name_table` ($4050)).

`draw_dash_needles` ($513A, #155 — the steering-wheel needle draw; a `math_lo` reader-nativization,
result-only fixture (`LIVE_NONE`, `REVS_SMC_CONTINUE=1`). The core `draw_dash_needle_core(steerLo,
steerHi, &DashNeedle)` computes the needle from `steer_angle` ($62A2/$62A5): sign=bit0,
doubled=`(hi<<1)|(lo>>7)`, a small/big branch (`doubled<0x26` direct vs a `~clamp+0x4C+cAdc`
mirror), the AA/XX small/big register role-swap, `rowSel=~((AA<<1)+4)`, origin `(sign?~XX:XX)+0x50`,
`subPos=(originBase<<1)&7`; the twin then writes the 6502 exit cells (`math_lo`/$76/$83), calls
`mode5_addr` and falls through into the shared `plot_line_octant` (native since #164) as a compared
channel. ⚠⚠ TWO bugs the harness caught: (1) the `small` selector was left UNINITIALIZED in the
big-path else branch → 2999/3000 mismatch (RULE: every path of a struct-filling core must assign
every selector); (2) the oracle's balanced PHP/PLP leaves a P-byte stack residue at $01FF the diff
compares → the shim writes the computed P byte to `mem[0x0100+cpu.S]`. Named `steer_needle_dda_tbl`
($3980)).

`menu_draw_gfx_bars` ($3A50, #156 — was `FUN_3a50`; a `math_lo` reader-nativization, result-only.
The front-end menu's two horizontal teletext graphics bars, drawn into the MODE 7 page at $7C79
from `front_end_menus` ($63ED): for each of two rows, `$97` (graphics white) at the start column
(`menu_bar_start_tbl` $3A6F), `$E2` at the next, then an `$E6` fill to the end column
(`menu_bar_end_tbl` $3A71). `math_lo` held the loop's CPX end-column target, a local now. Trivial
shim (no PHA/PHP → no stack residue; exit regs/flags dead — `front_end_menus` reloads X at once).
Named `menu_draw_gfx_bars` ($3A50), `menu_bar_start_tbl` ($3A6F), `menu_bar_end_tbl` ($3A71)).

`parse_two_digit_ascii` ($32D0, #157 — was `FUN_32d0`; a `math_lo` reader-nativization. The console
two-digit-ASCII→number parser, sole caller the numeric-entry loop `$3EE0`. Four exit arms
(char0-not-digit / char1-space single-digit / char1-not-digit / both-digits); `math_lo` ($74) is a
C local, and the twin writes $74's per-path 6502 exit value — untouched on the char0-not-digit arm,
`digit0` on single-digit, `digit0*10` on both two-digit arms. Fixture: exit C is the validity flag
the caller branches on, A reconstructed faithfully though dead (mask `LIVE_A|LIVE_C`); D pinned 0
(the *10 is ASL-based, binary by construction; not a SED site). ⚠ Sabotage caught a fixture gap:
the original case 4 (value 0..39) / case 5 (value 50..99) split never produced value 40/41, so
`CMP #$29`→`#$28` was invisible even though value==40 IS reachable via "40" — case 4 widened to
value 0..49 to span the boundary. Named `parse_two_digit_ascii` ($32D0)).

`seed_car_track_position` ($635D, #158 — was `FUN_635d`; a `math_lo` reader-nativization. The
per-car grid-position seeder, called on every (re)start by `FUN_4d4d` / `tick_race_timers` /
`console_io`: reduce a `USRVIA_T2CL` ($FE68) entropy byte to a small remainder, apply the raw byte's
sign (abs8), double, subtract `car_grid_base[x]`, scale by `race_class`, add `track_scale_saved`,
store `car_track_position[x]`, step `car_seed_index` back one (mod 20). `math_lo` ($74) is the
post-grid scratch (rotated on the Pro path) — a C local, the twin still writes its per-path exit
value. Fixture: `mask = LIVE_X` (the decremented cursor is the sole live exit, the caller loops on
it); D pinned 0 (the tail ADC is binary; the reset path is not a SED site); entropy steered through
`platform_test_via_t2` to cover the sign bit, the second reduction loop (raw&$7F ≥ 64) and zero; the
PHP/PLP residue byte reproduced in the shim (the oracle's JSRs are C calls that never touch the
emulated stack). ⚠ The `race_class` scaling fork is **DEY-N, not `>= 2`**: the 6502 BPL ($638C) tests
`(race_class - 1)`'s bit 7, so `race_class >= $81` takes the ASL (novice) arm, not Pro. In-game
`race_class` is only 0/1/2, but the oracle honours the full byte and validate randomises it — the
naive `>= 2` mismatched on the random-`race_class` cases. Named `seed_car_track_position` ($635D),
`car_track_position` ($0128), `car_grid_base` ($04A0), `car_seed_index` ($4A) [INFERRED]).

`FUN_27ed` ($27ED, #159 — the per-frame per-car **update engine**, called once a frame from the
driving loop ($117E/$2649) over cars 19..0, skipping `player_car`. For each car it picks a target
speed from the segment ahead, derives a braking-proximity gap, integrates gap×4 into the 16-bit car
speed `[car_speed_scaled:car_speed_frac]` (with a $BE overflow reset to 0), adds the speed into
`car_state_1` **twice** — each carry stepping the car one offset unit via `track_pos_advance` (#134,
which books a lap via `lap_complete` #136) — then steers `car_state_2` back toward centre. Two
genuine wide values de-carried to `uint16_t`: the $2861 `<<2` of `[math_hi:A]` and the $2867 speed
add. `math_lo`/`math_hi` ($74/$75) are written **inline** at their 6502 exit values — a mid-routine
lap wrap overwrites $74/$75 via the same native `lap_complete_core` both differential sides call, so
caching them would diverge (⚠ this is the crux of the reader-nativization for a routine that calls a
$74/$75-writing child). `shared_temp_76` ($76) is a pure loop counter → a C local, restored to its
$FF exit. `mask = LIVE_NONE` (both callers JSR $2692 next → mem[]-only compare); D=0 on this path.
⚠ Fixture: `fill_random` covers the speed/gap/steering trees, but the lap-wrap that overwrites
$74/$75 mid-routine is a compound-rare event random data never hits — so 2000/6000 cases **force**
it (one non-player car driven onto L_decel with a known moderate speed, `car_state_1`=$FF for a
guaranteed inner-loop overflow, distance one short of a full lap, `track_scan_active`/`flags_shape`
bit6/lap-count gates opened so `lap_complete` reaches its $74/$75 writes). Named
`car_race_flags` ($0100), `segment_pos_threshold` ($5305), `segment_speed_limit` ($5307),
`race_position_offset` ($5A1A) [INFERRED]; `$3850` dual-use (engine_init entry / `car_speed_frac`
table) noted, split queued in rename.md).

`sort_cars_by_key` ($0F64, #160 — the BCD bubble sort of the 21-entry car order array
(`car_order_prev` $13B; `car_order` $13C is the same array +1) by one of three 3-byte keys —
`car_best_lap` (keyA, ascending, Y-X), `car_lap` (keyB, descending, X-Y) or `car_lap_start`
(keyC, ascending, no tie handling) — selected by bits 6/7 of the A-register selector, repeated
until a pass makes no swap; ends with `find_player_neighbours`. ⚠ **ONE OF THE EIGHT SED SITES**:
`SED` at $0F66 / `CLD` at $0FB5 bracket the whole sort, so the compares stay `sbc_value`
(decimal-honoured) — this is emphatically **NOT** a de-carry-to-`uint16` site. The compare's
borrow-out is the plain binary borrow in both modes, so the ORDERING is exact regardless of D; only
the difference BYTES are BCD — which is exactly why the fixture must **not** pin D=0 (random,
mostly-invalid-BCD key bytes make a binary-diff-byte sabotage diverge). The reader-nat targets are
the scratch cells `math_lo` ($74, the swap-partner car index — written **only** on a swap, so an
already-in-order input leaves it untouched, matching the oracle), `math_hi` ($75, low diff byte)
and `hypot_min_hi` ($79, mid diff byte), all kept at their 6502 exit values in `mem[]`;
`hypot_min_lo` ($78) is the key selector (input A). `shared_temp_76` ($76, per-pass swap counter)
and `shared_temp_77` ($77, index) are C-tracked but keep their exit bytes. `mask = LIVE_NONE`
(CLD; JSR; RTS → mem[]-only compare). Fixture 6000 cases: selector cycles keyA/keyB/keyC; 1-in-5
forces the active key table equal → no swap, `math_lo` untouched, every compare a tie; 1-in-7
forces one adjacent pair equal → the tie-shift path; the order array is seeded with car slots
0..19 (21 entries from 20 values → frequent exact ties). 5 sabotages FAIL distinct
(1369/1930/4000/4000/4800). This twin also found the `LDA $0100,X` reader the rename.md `$0100`
entry said was missing (the tie-shift at $0FA1), settling `$0100` as dual-use — spin/penalty state
(`spin_car_out`) AND the sort's transient position scratch (`SORT_SCRATCH` file-local define).)

`shift_key_commands` ($0EE5, #161 — the SHIFT+f-key command handler from the 50 Hz body:
tests SHIFT held (`kbd_test_key_core($FF)`), scans `shift_key_tbl`[$0B..0] ($3DE2) top-down for
the first held key, applies its `shift_key_action_tbl` ($39D4) byte into `state_flags` (low nibble
= offset, high nibble = value), then services `pause_request` (spins on `clear_surface_buffers`
until $A6 is pressed when negative) and the even-frame `volume_change_request` step (ceiling $00 /
floor $F1, then `sound_envelope`). `math_lo` ($74) is a **pure loop-counter/scratch** reader → C
local, still written at each 6502 site. **NOT a decimal site** (D=0 pinned per static-map
§Decimal mode); the one `ADC` in the volume step is binary. `mask = LIVE_NONE`. MOS-boundary
register contract: the twin threads `cpu.Y=math_lo`, `cpu.X=idx`, `cpu.A=pr` so `sound_stop_all`/
`sound_envelope` enter with the oracle's A/X/Y. Needed a **new reusable mode-3 held-SET** in the
test keyboard backend (`platform_test_key_set_clear`/`_add`) — the fixture holds several keys at
once; `menu_wait_key` will reuse it. Fixture 6000 cases with deterministic `VOL_EDGE`/`VCR_SIGN`
edge tables (a random `sound_volume` hits the exact ceiling/floor edges ~0 times — a real coverage
gap caught by a surviving sabotage). 5 sabotages FAIL distinct (1000/291/230/227/3857). ⚠ The
"index 7 wins → writes negative pause → spins forever" scenario is **unreachable by construction**:
$A6 (=idx8) outranks $96 (=idx7) in the top-down scan, so idx7 can only win when $A6 is not held —
exactly when the spin cannot start. Documented at the fixture.)

`stage_nearby_car` ($28F2, #162 — was FUN_28f2; the per-nearby-car view stager, called twice by
`move_and_draw_cars`. For a `car_order` position X it saves the slot, forms the signed ring gap to
the reference car $17 via `car_gap_tail`, and rejects the object when the gap is far (C set), on the
wrong side (sign ^ `track_direction`), or |gap| >= $28 sections. Otherwise derives the view-section
cursor `Y = section_cursor - 3*|gap|` (wrapping +$78 when negative), copies that section's curve
into `car_flags_0` for a fast car in a normal state (`car_race_flags` bit4 clear AND
`car_speed_scaled` >= $32 — the $2931 AI look-ahead read), and projects via
`place_car_world_coords`. `math_lo` ($74) reader-nat: the abs8 gap byte is **consumed from
`car_gap_tail_core`'s return `.a`** (== `math_lo` on both near exits) instead of read back from
`mem[MATH_LO]`, but the twin still writes `math_lo = mag` at $290b — `place_car_world_coords`'
object-queue tail reads it. Result-only (`LIVE_NONE`): `move_and_draw_cars` reloads X/Y and reads
no exit flag after each call. **NOT a decimal site** (D=0 pinned). Fixture 6000 cases forces the
$298D SMC early-return in `place_car_world_coords` (`REVS_SMC_CONTINUE` + vacuity check), and drives
3/4 of cases through a controlled positive near gap that sweeps the $28 / $32 / direction
boundaries. 6 sabotages FAIL distinct (497/1116/941/3/783/2313).)

`check_car_pair` ($2692, #163 — the adjacent-pair overtake/collision resolver, called from the
freeze subtree over `car_order` positions X..X (full ring). For each pair (firstSlot at pos,
secondSlot one behind) it forms the signed ring gap via `car_gap`→`car_gap_tail` and dispatches:
**far** (C set) → nothing; **out of order, close behind** (N set, gap >= $F6) → swap the two
`car_order` entries, ROR the `position_swap_flag` ($62FE), and — if the player is one of the pair
and both are on the same lap — add $99 (player lost a place) or $01 (gained) into `pass_count_bcd`
via the routine's **one bracketed SED/CLD** BCD add; **positive small gap** (< 5) → derive a braking
magnitude into `$0083` and set `car_flags_0`/`car_race_flags` proximity bits, with a per-circuit SMC
compare at $2771. `math_lo` ($74) reader-nat: the gap byte is consumed from `car_gap_tail_core`'s
`.a`, but the twin writes `math_lo` inline at each 6502 store (**never cached** — the #159 CRUX:
`car_gap`/`car_order_swap` both overwrite $74 mid-routine). Result-only (`LIVE_NONE`). ⚠⚠ **The tail
write indexes on `cpu.X`, which the swap arm leaves as `secondSlot`, not `firstSlot`** — the 6502
`car_order_swap` returns X=oldY; a twin that always writes `car_race_flags[firstSlot]` diverges only
in the swap slice (caught as 2881 mismatches, all in `$0100..$0113`). Fixture 12000 cases across 4
slices (near / proximity / a wrapped-ring swap slice that seeds the player into the pair with
levelled laps for the BCD add / random), `REVS_SMC_CONTINUE` + vacuity check on the $2771 arm, D=0
pinned. 5 sabotages FAIL distinct (2669/302/2883/299/1169).)

`plot_line_octant` ($5204, #164 — the self-modifying octant line plotter, the shared leaf every
straight-line draw in the engine goes through: the rev-counter needle (`dial_needle_angle`'s
fall-through) and the steering-wheel mark (`draw_dash_needles`). A Bresenham DDA whose major- and
minor-axis step opcodes are patched into itself from the two octant tables ($3B86→$5220,
$3B8E→$529B) indexed by the octant in `shared_temp_76`; it ORs eight pixels through the ($70),Y
screen pointer, saving each background byte to the undo list ($0780/$07A8/$07D0, count $69) so
`undraw_plot_lines` can erase without a repaint. Reader-nat of BOTH `math_lo` ($74, the DDA
increment) and `math_hi` ($75, the pixel counter): here they become C locals `incr`/`count` and
**caching them across the loop is sound** — the routine is a LEAF (no child call) and the plot
pointer is always a screen address, so nothing in the loop rewrites zero page (the inverse of the
#159 CRUX, where a mid-routine child forced the inline write). The twin still writes `math_hi`'s
6502 exit value ($FF) at the end for byte-exactness until relocation. The two SMC seams
**dispatch on the cells $5220/$529B**, not a cached copy — a line that walked its own code would be
visible only that way — and the trap arm (an opcode that is not DEY/INY/DEX/INX) is real code in
both models, so it is compared rather than avoided. Result-only (`LIVE_NONE`). Fixture 6800 cases:
all 8 octants planted through `shared_temp_76` with valid step opcodes in both tables, bounded
count ($75 = 1..$14) and undo base (0..8) keeping the undo append inside the 40-entry tables,
varied minor delta ($0083) sweeping the carry/no-carry DDA paths, entry x/y and plot_ptr sweeping
the column/row wrap branches, `hypot_min_hi` 0 or 4 folding the MODE 5 pixel index onto both halves
of the keep/colour tables, and an 800-case illegal slice that plants a BRK in the always-executed
minor-step table entry so the trap-and-return unwind is compared (`REVS_SMC_CONTINUE`, both-way
vacuity check). D=0 pinned. 5 sabotages FAIL distinct (3305/236/5686/6740/6000). New symbol:
`plot_line_colour_tbl` ($34F8, the OR-mask counterpart to `pixel_keep_others_tbl`).)

`text_script_interp` ($4D7E, #165 — the on-screen text-script interpreter that walks a byte-coded
script and emits it: $00-$9F is a glyph (`vdu_char_def`, or `mos_oswrch` when `text_out_via_mos`
bit 7 is set — the OSWRCH arm lives in the shim, so the twin dispatches it itself), $A0-$C7 is a
run of N spaces (`print_spaces`, N=byte-$A0), $C8-$FD is a command that recurses into sub-script
index byte-$C8, $FE (=$C8+$36) runs `select_text_variant`, $FF ends. Scripts are pointed to by
`text_script_ptr_lo`/`_hi` ($3AD0/$3B50) indexed by the sub-script number. `math_lo` ($74)
reader-nat: the command operand `sub` becomes a C local, but the twin still writes `math_lo=sub`
where the oracle does ($4D97) for byte-exactness until relocation. ⚠⚠ **Table-overlap trap:**
`text_script_ptr_lo` at $3AD0 physically overlaps `char_row_addr_hi` at $3B06 ($3AD0+$36 == $3B06);
the engine only ever looks up indices 0..$35 so the overlap is dead in the real binary, but a
fixture that seeds a pointer-table byte at index >$35 lands it on `char_row_addr_hi[0..7]` — a $00
base then sends the glyph plotter (`vdu_char_emit` recomputes plot_ptr every emit from
`char_row_addr_hi[row>>3]`) into zero page, corrupting the script tables → runaway recursion →
stack overflow. The fixture keeps every seeded index ≤$35 and re-asserts `char_row_addr_hi` after
the fill. Result-only (`LIVE_NONE`). Fixture 3000 cases: a top script and a leaf reached by every
sub-index, char/space/command/variant all exercised, both the OSWRCH and bitmap emit arms
(`viaMos` on 7-of-8), D=0 pinned. ⚠ The `math_lo=sub` write needed a dedicated no-space leaf
(index $10 = `[char,char,$FF]`) in the fixture: every command that ends in a space run has its
`math_lo` overwritten by `print_spaces` (which always exits with `math_lo=0`), so without a leaf
whose *last* act is the command write, dropping `math_lo=sub` is a no-change survivor. 5 sabotages
FAIL distinct (46/3000/389/CRASH/2625).)

The remaining **1 genuine shipping reader**:

```
menu_wait_key
```

⚠ **Two more over-counts dropped (2026-08-29), both oracle-only** (no runtime-reachable caller,
zero perf win — the raw-address hit is inside a `__t6502` body only):
- `FUN_2f19` ($2F19) — the span-cap surface writer, **already native** as `span_cap_line`
  (revs_native.c). Its only callers, `FUN_2f12`/`FUN_2f7e`, are themselves reached only from the
  span-plotter oracles (`draw_span_*_rev__t6502`, `road_span_plot__t6502`, `road_span_plot_2__t6502`).
- `view_paint_lines_short` ($7F18) — the native `view_paint_lines` DRIVER reimplemented the whole
  subsystem in `view_paint_lines_core`, which references **none** of the cell-chain routines
  (`region_7bf7`, `view_cell_chain_*`, `view_next_scanline`, `view_paint_lines_clipped`,
  `view_paint_lines_short`, `FUN_7f17`) — they survive only as the `view_paint_lines__t6502` oracle.

⭐ **Reader-nativization pattern (from #148):** the counter/scratch becomes a C local, but the
twin still writes the cell's 6502 exit value so the routine stays byte-exact (validate compares
$74, no `set_ignore`) — the cell is only physically lifted in the final relocation step, once all
remaining shipping readers are native (the 6 oracle-only bodies above still reference the cell, so the
relocation step must keep it `mem[]`-visible to the oracles regardless — a separate concern). ⚠ Fixture trap: `print_spaces` with `count=0` runs 256 chars
through the bitmap emitter, whose per-char writes follow a `mem[]` cursor; a fully-random
char-row base (`char_row_addr_hi` $3B06) can point that at zero page and clobber `math_lo` — the
*oracle's own* loop counter — so the transliteration never terminates. The fixture pins a valid
screen char-row base ($58xx), which is the only state the real routine ever runs in.

⭐ **Paint-reader refinement (from #149, `draw_starting_lights`):** a reader whose exit regs/flags
are all dead at its (native) caller validates RESULT-ONLY (`mask = LIVE_NONE`). If the 6502 body
does a `PHA`/`PLA`, that leaves the pushed byte as **stack residue** at `$0100+S` which a full-mem
diff sees — reproduce it in the shim (`mem[0x0100+cpu.S] = pushedByte`) rather than `set_ignore`,
so the twin stays byte-exact with no ignore-list entry (determinism already skips `$01B8..$01FF`).
Have the core return the pushed value so the shim can replay it; return a sentinel (`-1`) on the
early-exit paths that push nothing.

⭐ **PHP residue is the exception that IS `set_ignore`d (from #150, `update_horizon_band`):** when
the push is a `PHP`/`PLP` of the whole **P register** (not a data byte), the pushed value is a
path-dependent function of N/V/Z/C at the push point — dead scratch the `PLP` pops right back, and
not worth reproducing bit-for-bit. `set_ignore` the one stack cell (`$0100+S`, `$01FF` with the
harness's `S=0xFF`) and note it; determinism already skips `$01B8..$01FF`, so this only affects
`make validate`. (Contrast #149, where the push was a known data byte the shim replays exactly.)
Two per-circuit **SMC seams** on this routine's positive-clamp path (`$4F54`/`$4F58`) are tested,
not avoided: the fixture randomises their opcodes ~1/16 under `REVS_SMC_CONTINUE=1` so the
trap-and-return arm is a *compared* channel, and asserts `g_smcUnhandled > 0` so it cannot pass
vacuously.

⚠ `math_lo/hi` is *shared* scratch, so its 27 readers span unrelated subsystems (menu, dash,
mirrors, starting lights, text, sorting) — a wide blast radius. A cell used by ONE subsystem is a
cheaper first relocation than `math_lo/hi`; consider relocating a narrowly-scoped internal
accumulator before the shared one, even though `math_lo/hi` has the highest raw ref count.
(Method to regenerate this list / do it for another cell: the enclosing-function scan in the
2026-08-27 session — parse top-level defs, exclude `__t6502`.)

## Coordination

- The MOS-dispatcher session has LANDED (HEAD `ba5e7da`); the merge-collision risk is cleared.
- Commit one cell/base per commit; gate each as above; keep this ledger's Status column current.
- Nativizing a reader function is itself a faithful-seam twin (per CLAUDE.md) with its own
  validate fixture + sabotage gate — record each on the cpu-elimination / this ledger as it lands.
