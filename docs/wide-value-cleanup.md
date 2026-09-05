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
| `math_lo/hi` | $74/$75 | shared 16-bit SCRATCH accumulator — 388 operand refs in 84 routines (the count was 305; re-measured off the listing 2026-09-02, both the `0x74` and `0x0074` notations) | ✗ not B | **BLOCKED, settled** — see the eligibility row |
| `point_dist` | $7C/$7D | projected distance (render) | B | **ELIGIBLE and DECLINED** — legal, but scored negative site by site: shared render scratch, no wide arithmetic to amortise. §ELEVENTH LESSON |
| `hypot_max` | $7A/$7B | larger sorted ground-plane magnitude | B | **✅ B DONE** (`hypot_max_v`) |
| `hypot_min` | $78/$79 | smaller sorted ground-plane magnitude | B | **✅ B DONE** (`hypot_min_v`) |
| `bearing` | $8A/$8B | bearing_to_section output | B | **✅ B DONE** (`bearing_v`) |
| `plot_ptr`/`plot_ptr2`/`plot_ptr3` | $70/$71, $72/$73, $8E/$8F | screen write pointers | **A only** | ✗ **B blocked — the 6502 DEREFERENCES these cells** (`($70),Y` and friends). ⭐ But (A) is INCOMPLETE and this is the largest remaining byte-lane pocket of the seven — §THE SEVEN NAMED PAIRS |
| `point_delta`, `object_dist` | $80–$83, $55 | edge-walk scratch | B | **ELIGIBLE, unstarted** — re-scanned 2026-09-03 after twins #167-#172: every ref is in a native routine. Score them per §ELEVENTH before starting |
| `nearest_edge_bearing` | $5E/$5F | edge-walk scratch | B | ⚠ **shares `$5F` with `engine_note_target`** (`CPX 0x005f` at `$0E94`, non-native `engine_sound_update`). A different TENANT, so it does not block under the EIGHTH lesson — but the relocation must leave `$5F` in `mem[]` for the sound path |
| `steer_angle` | $62A2/$62A5 | the steering wheel's own angle, sign-magnitude (bit 0 of the LOW byte is the sign) | **A only** | **✅ A DONE (2026-09-04)** — `apply_steering_assist`'s `assist_from_selector` held the last two lane pairs on this path (`angLo`/`angHi` around the abs, and `lo`/`hi` around the re-sign); both are single `uint16_t`s now, with `& $FFFE` in place of "clear bit 0 of the low lane and keep the high one". B is DONE too: the whole `CAR_ANGLE` array is `car_angle_16[3]` |
| `SLIP_MAG` | $8E/$8F and $62DA/$62EA | slip magnitude — a TENANT of plot_ptr3, plus a second home as MODEL_STATE element $0A | **A only** | ✗ **B blocked twice over**: the $8E home is an indirect pointer base, the $62DA home is a strided SoA member. §THE SEVEN NAMED PAIRS |

⚠ **A FULL indexed/indirect re-audit of every campaign pair** (both `$xx,X`/`$xx,Y` notations and
`($xx)` bases, counted straight off `disasm/listing.txt`) — run after the EIGHTH lesson showed the
per-pair reasons could not be trusted:

| | $74 | $75 | $78 | $79 | $7A | $7B | $7C | $7D | $8E | $8F | $70 | $71 | $10 | $11 | $80 | $81 | $82 | $83 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| indexed | **3** | 0 | **1** | 0 | 0 | 0 | 0 | 0 | 0 | 0 | **1** | 0 | 0 | 0 | 0 | 0 | 0 | **4** |
| indirect base | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | **1** | 0 | **20** | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

Three of the four blocks that cited indexing were wrong or misattributed: `$78`'s one site belongs
to `update_grip_limits` (another tenant), `$8E` has **no** indexed site at all (it is an indirect
base), and `$74`'s three are a separate vector tenant. Only `$70` is blocked for the reason given,
and `$83` carries four indexed sites the aliasing note below never mentioned. ⭐ **Count the sites
and attribute each to its enclosing function; never carry a one-line block reason forward
unchecked.**

⚠ **Aliasing group — convert together:** `POINT_DELTA` $0080–$0083 is viewed simultaneously as
`MUL_SRC` ($80/$81) and `MUL_TERM` ($82/$83) — same zero-page bytes, three names (mul
multiplicand/multiplier). Convert as one unit or the shared storage corrupts.

✅ **FINDING (2026-08-30): the (B)-eligible Tier-1 render pairs — a rigorous scan.** For every
16-bit zp pair, both bytes must be (a) all-native (every *direct* operand ref lies in a function in
`VALIDATE_FUNCS ∪ NATIVE_FUNCS`) and (b) never indexed or used as an indirect base (`$xx,X`/`$xx,Y`
in **either** 2- or 4-hex-digit notation, `($xx),Y`). Verdicts:

| Pair | Addr | (B)-eligible? | Why |
|---|---|---|---|
| `hypot_max` | $7A/$7B | **✅ B DONE** | `hypot_max_v` (revs_native.c). All-native, unindexed, and CLEAN of both channel tests — producer `bearing_to_section_core` and consumer `point_distance_hypot_core` are both native and the shipping glue between them (`FUN_2a5f`) never touches $7A/$7B. The one other tenant, `plot_view_src_line`'s PVS_BYTE/PVS_MODE, keeps the cells — see the IN/OUT rule below |
| `point_dist` | $7C/$7D | ⚠ eligible ONLY with shim marshal-OUT (all 32 refs are native as of 2026-09-03; the gate is the differential, not a ref) | a SHIPPING transliterated reader on the patched arm — `region_23d8` (the FOURTH test below). Marshal out in `emit_edge_bearing_at_cursor()`, `emit_edge_bearing()` and `point_distance_hypot()`; proof needs a Brands frame-buffer differential, not a fixture |
| `bearing` | $8A/$8B | **✅ B DONE** (`bearing_v`) | same shape, lower risk: shipping `FUN_2a5f` (the car projector, $2A5F) calls native `bearing_to_section()` and then reads `bearing_lo`/`bearing_hi` into `object_bearing` ($0380/$0398). Not a patched arm, so `make determinism` DOES gate it |
| `model_accum_entry` | $38/$39 | **✅ B DONE** | `model_accum_entry_v` (revs_native.c). All-native, unindexed — but it HAD the oracle-glue channel below, resolved by shim marshalling |
| `edge_nearest` | $10/$11 | **✅ B DONE (2026-09-03)** | `edge_nearest_v` (revs_native.c); one 16-bit compare in `road_edge_walk_core`, marshalled at `road_edge_walk` (IN+OUT), `build_track_geometry` (IN+OUT) and `check_crash` (IN). ⚠ The init stays lane-preserving because $24FD arms only the HIGH lane — and the surviving low lane is DEAD IN PRACTICE: measured 2872 compares over the fixture with `distHi == $FF` in **zero** of them, which is why two sabotages survive. The blocker had been `$11` read by non-native `check_crash` ($111E); twin #169 made it native, and a strict re-scan finds all 7 refs in native routines. |
| `car_heading` | $0A/$0B | **✅ B DONE (2026-09-03)** | `car_heading_v`; seven call sites, marshalled at six shims. ⭐ `apply_driving_model` needs marshal-**OUT** although it is not the producer — its core tail-calls `integrate_car_position_core` core-to-core (the transitive-producer trap). The block had been **half instrument error**: `build_player_car` ($11CE) is native as of twin #170, and the *second* blocker, `loader_stub`, was never a routine — a stale `func` row at `$1200`, nine bytes into `build_player_car`, made that routine's OWN writes at `$11FE`/`$1205` read as non-native references. Retagged to `loader_image_entry`/`note`. |
| `math_lo/hi` | $74/$75 | ✗ **BLOCKED — settled 2026-09-02, do not re-open as a pair relocation** | The re-audit the EIGHTH lesson called for is DONE, and it cleared the indexing charge while confirming the block on other grounds. **Test 2 (indexing) PASSES:** all three indexed sites (`SBC/STA 0x74,X` $146E in `build_section_step_delta`, `ROR 0x74,X` $2B18 in `step_delta_halve` — *not* `draw_track_object`, which was an address-ordering misattribution) index the **step-delta vector** (lows $74/$75/$76, highs `point_delta_hi[0..2]` $83/$84/$85), a DIFFERENT TENANT, which under the EIGHTH lesson does not block a relocation. Tenancy now recorded on the cells themselves in `symbols.csv` (commit 67fb302). **Test 1 (all-native) FAILS by 6 refs in 4 routines**, each a self-contained local scratch use in its own tenant: `$262D` (an unnamed 6×256 DELAY LOOP, `DEC $74/BNE`), `$2B18` `step_delta_halve`, `$31D0` (a dash-code plotter loop, `$74` a row counter and `$75` its limit, `(plot_ptr),Y` store), `$49BB` (an unnamed seeder, `$74` scratch across 8 instructions). **But the decisive blocker is neither** — it is the HYBRID ORACLE GLUE of the 2026-08-30 FINDING below, which no amount of nativization removes: the math primitives (`mul8`/`mul8_noinit`/`div16by8`/`neg16_math`/`abs16_math`) are the pair's own operators and are already native, so every `__t6502` body that multiplies hands operands to a native child **through `mem[$74/$75]`**. ⭐ **The right mechanism here is not (B) at all** — see §`math_lo/hi` is a SCRATCH pair, so the win is PER-TWIN below |
| `hypot_min` | $78/$79 | **✅ B DONE** (`hypot_min_v`) | ⚠ **this row used to read "✗ blocked — `$78` indexed", and that was a MISREADING of the second test** (see the EIGHTH lesson). The one indexed access, `ADC 0x78,X` at $4C52, belongs to `update_grip_limits`' axle load terms — a DIFFERENT TENANT. A relocation moves ONE USE, so what test 2 has to ask is whether the *relocated use* is ever indexed, and the road-pass use never is |
| `plot_ptr` | $70/$71 | ✗ blocked | `$70` is an indirect pointer base (`($70),Y`) |
| `SLIP_MAG` | $8E/$8F | ✗ blocked | ⚠ reason CORRECTED: `$8E` is **not** indexed anywhere (0 sites in either notation) — it is an **indirect pointer base**. Right verdict, wrong reason; the old reason would have survived the EIGHTH lesson's tenant re-read and kept the block for nothing |

⚠⚠ **METHOD TRAP that produced a false "blocked as a class" reading first (corrected same day):**
the native-surface set and the indexed-exclusion set are BOTH easy to under-build. (1) `VALIDATE_FUNCS`
is a 165-entry `{...}` set in `transpile.py` — a naive non-greedy regex grabs a doc-comment
occurrence and truncates it to ~14 entries, so almost every render twin reads as "transliterated"
and every pair looks cross-world. Extract it by brace-counting from the real assignment (line ~557),
and remember `mul8`/`div16by8`/… ARE native (in the set). (2) zero-page indexed operands appear as
`0x74,X` (**2 hex digits**), not `0x0074,X` — a 4-digit-only regex misses every zp index and marks
indexed cells eligible. Scan with `0x0*7[456],[XY]`. With both fixed, the table above is stable.
**So (B) for zp scratch is NOT blocked as a class** — four genuine render/model pairs relocate now.

⚠⚠ **THIRD eligibility test, learned by relocating `model_accum_entry` $38/$39 (B DONE):
all-native + unindexed is NECESSARY, NOT SUFFICIENT — check the oracle-glue → native-core
channel.** A `__t6502` oracle body JSRs the *plain* (native) name of any sub-routine that is
itself a twin, not that sub-routine's `__t6502` (e.g. `apply_driving_model__t6502` calls native
`apply_drag_terms()`). If the relocated pair is how the transliterated PARENT hands a
value to the native CHILD — the parent still writes mem[$38/$39] at $46AE, the child now reads the
wide var — the oracle path silently reads a stale var and the fixture fails BROADLY (198/200,
scattered across model_state), not at the relocated cell. This is the SAME class recorded for
[[math_lo/hi]] (commit 2cbb2c0).
- **Resolvable** when the channel is *one-directional input to a single child*: make that child's
  6502-ABI **shim** marshal the cell in — `void apply_drag_terms(void) { model_accum_entry_v =
  (hi<<8)|lo; apply_drag_terms_core(); }`. The hot path stays core-to-core and var-only (the shim
  is validation-only); the oracle glue gets the value through the cell as before. Sabotaging the
  shim to `= 0` must FAIL the standalone fixture — that proves the channel is load-bearing, not
  dead code.
- **Hard block** when the value flows both ways through many native helpers (math_lo/hi: mul8 /
  div16by8 / … in dozens of `__t6502` bodies) — the shim trick would have to marshal in-and-out on
  every helper, and $74 is indexed anyway.
- **Producer's own fixture:** the transliterated parent still writes the cells, so its fixture
  `set_ignore`s them (native writes the var instead). `det_compare.py` skips the pair as
  no-longer-game-state (currently unexercised: $38/$39 read 0 at all three determinism dump
  frames — `make validate` is the byte-exact proof).
  ⚠⚠ **NO LONGER TRUE — both blunted gates have been removed** (see the FIFTH and SEVENTH lessons
  below). $38/$39 are published back by `apply_driving_model`'s shim, the fixture compares them, and
  `det_compare.py`'s skip list is empty. Marshal the pair at every ABI crossing; never skip it.

⚠⚠ **FOURTH eligibility test (2026-08-30): A TRANSLITERATED MULTI-ENTRY *REGION* IS SHIPPING CODE,
AND A CIRCUIT HOOK CAN RE-ENTER ONE.** The three tests above all ask about *functions*. But
`build_regions()` emits a `region_NNNN(entry)` for any 6502 loop whose segments tail-call each other
in a cycle, and a region is **not** a `VALIDATE_FUNCS` member and carries **no** `__t6502` suffix —
so a scan that classifies by "is the enclosing function native?" reads it as neither native nor
oracle and skips it, while a scan that classifies by "does the name end in `__t6502`?" reads it as
shipping but cannot tell whether anything reaches it.

The case that matters: **`region_23d8` = the transliterated body of `road_edge_walk`.** Silverstone
never enters it — the native twin runs the whole walk — which is why it looks dead. On every
expansion circuit the SMC at `$248B` is the circuit's own `JMP`, the twin dispatches to
`revs_track_hook`, and the hook's `L_56c5` does `FUN_2490()` → `region_23d8(0x2490)` → `$24B5 goto
L_23d8` → **the rest of the edge walk runs transliterated**, calling the native shims per point and
reading `point_dist_lo`/`point_dist_hi` out of `mem[]` for the running-nearest compare
(`$23DB-$23ED`). So a shipping reader of the pair exists on five circuits and on none of the gates.

Consequences, and they generalise past this campaign:
- **Grep for the bare alias AND the `MEM_` form AND raw hex.** `revs_gen.c` writes named cells as
  bare lvalue aliases (`point_dist_lo = cpu.A`) *and* as `ASL_M(MEM_point_dist_lo)` / `INC_M(...)`
  for read-modify-write opcodes. A hex-only or `MEM_`-only scan of $7C/$7D returns **2 hits**; the
  alias scan returns **35**. Any one form alone understates the surface by an order of magnitude.
- **Classify every hit by enclosing function and then ask what REACHES it** — `region_*` and `FUN_*`
  names are shipping until proven otherwise, and "proven" cannot come from a Silverstone run.
- **Resolution is the same shim marshal, in the OUT direction**: the region calls the producer's
  6502-ABI shim (`emit_edge_bearing_at_cursor()`), so that shim writes the wide var back to the two
  cells. Hot Silverstone stays core-to-core and var-only; the expansion arm pays two byte stores per
  point, which is what it pays today.
- ⚠ **But the proof is different, and that is the real cost.** A fixture cannot cover it (the region
  is shipping, not an oracle), and `validate` / `determinism` / `-drive` all race Silverstone. The
  gate is a real-BBC frame-buffer differential on an expansion circuit over display lines 82+
  (`make refloop --park` + the view comparison), plus `make tracks` / `track-run` — the same
  apparatus the `fill_line_attr` hook-seam bug needed. Budget for it before starting such a pair.

⭐⭐ **AND IT IS A BOUNDED SET — measure it once, reuse it.** The circuit hooks
(`revs_track_hooks.c`) call exactly eleven names, five of them transliterated
(`FUN_12f3`, `FUN_140b`, `FUN_2490`, `FUN_253b`, `FUN_461b`). Closing that over the call graph,
stopping at every native name, the whole transliterated surface an expansion circuit can reach is
**ten functions**:

    FUN_12f3  FUN_140b  FUN_1420  FUN_1433  FUN_2490  FUN_24b8  FUN_253b  FUN_461b
    project_point  region_23d8

and the native shims they call — the marshalling surface, the only shims on a shipping hot path —
are `abs8`, `mul8`, `advance_dir_on_segment_flag`, `build_road_section`,
`emit_edge_bearing_at_cursor`, `emit_edge_width_offset`, `project_point_from`, `track_pos_advance`,
`track_pos_retreat`. Of those ten bodies **only `region_23d8` touches a named 16-bit zp pair at
all**, and it touches exactly three: `point_dist`, `edge_nearest` and `math_lo`/`hi` — which
independently re-confirms the existing block on the latter two. `hypot_max`, `hypot_min` and
`bearing` are clean on this test. So this test is cheap to apply: check the pair against those ten
bodies, not against the whole corpus.

⚠ For `point_dist` specifically the marshal is **both directions**: `region_23d8` also calls the
`project_point` shim, which READS the pair for its far clip and shifts the low byte IN PLACE
($22C5), so that shim needs the cells in *and* the shifted residue back out.

⭐⭐ **FIFTH — THE IN/OUT MARSHALLING RULE, and it makes the relocation STRICTLY CLEANER than
`set_ignore`** (learned relocating `hypot_max` $7A/$7B, the first render-path pair). The two earlier
relocations each had to blunt a gate: the producer's fixture `set_ignore`d the cells and
`det_compare.py` gained a skip. Neither is necessary. Decide each 6502-ABI shim by what its CORE
does with the value:

| The core… | The shim marshals | Why |
|---|---|---|
| CONSUMES it | **IN** | a transliterated parent (oracle or shipping) still hands it over in `mem[]`, as the 6502 did |
| produces it UNCONDITIONALLY | **OUT** | the cells are this routine's 6502 output; a transliterated caller reads them next |
| produces it **CONDITIONALLY** | **IN *and* OUT** | ⚠ this is the trap — see below |

⚠⚠ **AN UNCONDITIONAL MARSHAL-OUT ON A CONDITIONAL PRODUCER IS A REAL DEFECT, and it fails
SCATTERED.** `build_road_sign_core` reaches the bearing only on the path that gets past both
sign-table SMC traps; an early exit leaves `hypot_max_v` holding *the previous call's* value, which
an unconditional OUT then stamps into cells the 6502 never touched. The fixture failed on isolated
cases (3, 12) with nothing wrong at the relocated cell. **The marshal-IN is what makes the
marshal-OUT faithful:** read the cells first and every early-exit path writes back exactly what it
read — which is what the 6502 left there. Same shape in `road_edge_start` (the near point may
already be in range), `road_edge_walk` (every candidate may be rejected) and `build_track_geometry`.

⭐ **Let the differential FIND the set; do not reason it out a priori.** Four `make validate` runs
each named exactly one more parent as the cascade climbed the road pass
(`build_road_sign` → `road_edge_start`/`road_edge_walk` → `build_track_geometry`). Adding IN/OUT
where the harness points is faster and more complete than auditing shims by hand.

**Outcome, and this is the standard to hold the remaining pairs to:** every fixture stayed
byte-exact on $7A/$7B — **zero `set_ignore`, zero `det_compare.py` skip** — because a relocated pair
that is marshalled at every ABI crossing is still, at every frame boundary, exactly the game state
it always was. Cost is four accesses at a crossing and **nothing** core-to-core, which is where the
road pass's calls actually are. Gates: `validate` PASS, `determinism` + `-drive` 64K byte-identical,
`endian-lint` clean, `tracks` 6/6, `track-run` all hooks run, Amiga link muldiv + probe clean.
Six sabotages, all FAIL with distinct fixture sets and distinct counts (wrong sort arm; IN dropped;
OUT dropped; IN loses the high byte; OUT swaps lanes; one walk's IN+OUT removed).

⚠ The cells are **NOT freed** — $7A/$7B keep a second tenant, `plot_view_src_line`'s
`PVS_BYTE`/`PVS_MODE`, read back by its transliterated tail `FUN_1d94` (reachable only from two
oracles, never from a track hook). A relocation moves ONE USE of a pair, not the pair.



⭐⭐ **SIXTH — THE DRIVER BYPASSES THE SHIMS, AND THAT IS WHERE A RELOCATED PAIR GOES STALE**
(learned relocating `bearing` $8A/$8B). `race_main_loop` is the one `NATIVE_FUNCS` driver, and it
called `build_track_geometry_core(0x06, 0x2E)` and `build_road_sign_core()` **directly** — the
sensible thing when the shim exists only to marshal 6502 registers. But once a pair is relocated the
shim is also the *publisher*: it is the only place the wide var goes back into `mem[]`. Bypassing it
left $8A/$8B holding the previous frame's bearing for a whole frame.

- **It failed as ONE byte.** `make determinism` printed `$008B: ref=0x0B run=0xFF` and nothing else —
  `make validate` was green (fixtures enter through the shim), `tracks` / `track-run` green,
  `endian-lint` green. A single-byte determinism diff is the whole signal for this class.
- **The fix is structural, not another marshal call:** have the driver call the **shims**
  (`build_track_geometry()`, `build_road_sign()`) — their argument lists are the same constants, so
  there is nothing to duplicate, and the marshalling can never drift out of step again.
- ⚠ **`hypot_max` had the same hole and no gate could see it**, because $7A/$7B are rewritten every
  frame by their second tenant (`plot_view_src_line`'s PVS_BYTE/PVS_MODE) before anything reads them.
  Routing the driver through the shims closed it too. **So audit the driver's call sites for every
  relocated pair, whether or not a gate complains** — a masked hole is still a hole, and the mask is
  another routine's unrelated tenancy.

### `bearing` $8A/$8B — ✅ B DONE (`bearing_v`)

`bearing_to_section`'s whole output: the absolute angle from the camera to a section point, computed
once per edge point per frame. Producer `bearing_arm` / `bearing_diagonal` write one word; readers
`emit_edge_bearing_core` (which subtracts `car_heading` from it) and `build_road_sign` take it in a
register. Marshalling by the IN/OUT rule: OUT in `bearing_to_section_from` and
`emit_edge_bearing_at_cursor` (unconditional producers), IN in `emit_edge_bearing` (consumer), IN+OUT
in `road_edge_start` / `road_edge_walk` / `build_track_geometry` / `build_road_sign` (conditional).
`make validate` passed **first try** — the IN/OUT rule generalised without a search this time.

⚠ **The pair is far more multi-tenant than `hypot_max`'s and none of it moves**: `road_span_plot` /
`road_span_plot_2` park their DDA accumulator in $8A and carry the span's pixel byte in $8B (loaded
from `COLOUR_PATTERN`, not from a bearing); `plot_line_octant`, `plot_object`, `scale_shape_vectors`
and `interp_edge` each use the pair as scratch; `OBJ_VECTOR_END` is $8A by another name. Relocating
only the bearing is sound because the bearing chain is **tight** — a producer call is immediately
followed by its reader, with no tenant between — so no reader ever depended on a tenant's leftovers.

Gates: `validate` PASS, `determinism` + `-drive` 64K byte-identical, `endian-lint` clean, `tracks`
6/6, `track-run` all hooks, Amiga muldiv + probe-audit clean. Seven sabotages, all FAIL.


⭐⭐ **SEVENTH — PUBLISH THE PAIR; DO NOT BLUNT THE GATE. `det_compare.py`'s `RELOCATED` LIST IS NOW
EMPTY AND SHOULD STAY EMPTY.** The first two relocations each bought their way past the gates with a
`set_ignore` in the producer's fixture *and* an entry in `det_compare.py`'s skip list, on the
reasoning that a relocated pair is "no longer game state". That reasoning is wrong twice over: a
circuit hook can re-enter the transliteration and read the cells (the FOURTH test), and a skipped
byte is a byte the whole-corpus differential stops covering. Every relocated pair is now **published
back into `mem[]`** where the 6502 wrote it:

| Pair | Published at | Because |
|---|---|---|
| `hypot_max` $7A/$7B, `bearing` $8A/$8B | the producer's 6502-ABI **shim** | a shim exists, and the IN/OUT rule places it |
| `model_accum_entry` $38/$39 | `apply_driving_model`'s shim (+ the driver now calls that shim) | was IN-only; the producer never published |
| `band2_duration` $4F21/$4F22 | **the producer itself**, in band 1's arm | `irq1v_band_schedule` IS the 6502 entry point — `bbc_hw.cpp` calls it straight from interrupt context, so there is no shim to hang it on |

Both `set_ignore`s are gone, `RELOCATED = []`, and the arithmetic stays wide in every case — a
publish is two byte stores at one point, not a return to byte-lane handling.

⚠ **One of the four publishes is gated only by argument, and it is worth knowing which.** Dropping
or lane-swapping a publish fails its own fixture in every case (P1/P2 → `apply_driving_model`,
P3/P4 → `irq1v_band_schedule`). But the *driver call site* for `model_accum_entry` — core vs shim in
`race_main_loop` phase 4 — survives `validate`, `determinism` **and** `determinism-drive`, because
$38/$39 read `00/00` at both dump frames (checked in the goldens, not assumed). That is the
"no change at all" class, not a fixture gap: there is nothing for a differential to see. The same
site for `bearing` IS gated, because $8A/$8B are live there — which is the only reason the
driver-bypass bug was ever found. **So the bypass class needs an audit, not a gate**: check every
`_core` call in the driver against the relocated-pair list whenever a pair is added.

⚠ **Re-recording the drive reference was required, and here is how to tell that is legitimate.**
Unskipping $4F21/$4F22 made `determinism-drive` fail on exactly those two bytes (`ref=0x1064`,
`run=0x0FE4`) while parked passed. The check that settles it: stash the change, remove *only* the
skip on the committed HEAD, and re-run. It PASSED — which means the golden held the **unwritten
static-image value**, recorded after the relocation had already stopped anything writing the cells.
The new value is the computed one, and `make validate FN=irq1v_band_schedule` proves it byte-exact
against the 6502 oracle over 25 628 cases *including those cells*. Only then re-record. **Never
re-record to make a gate green without that isolation step** — it is the difference between a stale
golden and a real regression.

⚠⚠ **RE-RECORD ALL THREE GOLDENS, NOT THE TWO THE GATE LIST HABITUALLY NAMES.** That pass
re-recorded `determinism` and `determinism-drive` and left `determinism-crash`'s golden holding the
same stale `$4F21/$4F22` = `0x1064`, so the crash gate sat red until the next unrelated change ran
it and looked, at first, like that change's regression. ⭐ The cheap way to tell a stale golden from
a real divergence *without* a bisect is an **invariant on the differing cells**: `band1_duration +
band2_duration == $153C` by construction, the run summed to `$153C` and the golden to `$15BC`, and
`band1_duration` was byte-identical in both — so the trajectory never diverged and only the
published cell moved. `determinism-crash` is a longer trajectory (frame 1500) than either of the
other two, so it is the one a re-record pass forgets.

### hypot_min $78/$79 — ✅ B DONE (`hypot_min_v`)

`bearing_to_section`'s sort produces both ground-plane magnitudes and `point_distance_hypot`
consumes both, so this pair travels the road pass beside `hypot_max` and marshals at the same
seams — plus `note_object_contact()`, which the differential had to point out.

⚠⚠ **The consumer produces it back CONDITIONALLY *and* ASYMMETRICALLY**, which is the one thing a
relocation has to spell out here. The near arm's `>>3` keeps the low byte in `A` the whole way and
stores only the high one, so the low lane comes back UNCHANGED there, where the far arm's `>>1`
rewrites both. On a relocated value that is:

```c
if (d.farArm) hypot_min_v = d.min;
else          hypot_min_v = (uint16_t)((d.min & 0xFF00u) | (hypot_min_v & 0x00FFu));
```

Writing `d.min` whole on both arms is byte-exact arithmetic and a differential failure (sabotage D1
FAILs on it). Per the IN/OUT rule a conditional producer marshals **IN and OUT**.

⚠ The cells are NOT freed, and this is the most crowded pair the campaign has relocated — ten other
tenants, five named in `revs_native.c` itself (`MUL_SIGN`, `SLIP_SIGN`, `SLIP_OUT_INDEX`,
`PVS_COLOUR`, `PVS_COLOUR_P`), plus `update_grip_limits`' axle terms, `car_gap_tail`'s sign shift
register, `full_track_scan_rebuild`'s retreat-grid index and `menu_wait_key`'s remembered row.
Every one keeps reading and writing `mem[$78/$79]`.

⚠⚠ **FIFTH eligibility test (2026-09-04): THE VALUE MUST NOT BE ABLE TO WRITE ITSELF — and if it
can, the relocated global needs a LIVE MIRROR, not a marshal at the seam.** The four tests above
all ask *who else reads or writes these cells*. `plot_ptr` `$70/$71`, `plot_ptr2` `$72/$73` and
`plot_ptr3` `$8E/$8F` pass all four and still break the naive relocation, because they are
**pointers that are dereferenced**: an ascending span arm entered above its bound runs the long way
round, climbing the page byte through `$00`, and the plotter's own store then lands in zero page —
sometimes on `$70`/`$72` themselves. No static scan can see this; the pair is written by an address
computed at run time. Only the fixtures can, and only because they PLANT the case (one ascending
case in twelve starts above its bound).

The measured signature, and it is worth recognising: **`draw_span_shallow_fwd` 7/400 and
`draw_span_steep_fwd` 5/400, both `rev` arms clean**, with the diff naming the cell —
`$0070 ref=$B8 native=$00`. Ascending-only failure on a pointer pair means self-store.

⚠ **A resync-on-store guard is only HALF the fix, and the half that fails silently is the read.**
The obvious repair is to notice a store landing on a lane and fold it into the wide copy. That
handles the write direction. But the plotter also **reads** the cell it is about to merge —
`cell = mem[cellAddr]` — and when `cellAddr` aliases a pointer lane, a mirror that is only published
at the shim's exit hands that read the pointer's ENTRY value. The residue after the guard was
1/400 on each `fwd` arm, and the trace showed the same store site writing `$D7` in the oracle and
`$77` in the twin. **So the mirror must be LIVE: every mutation of the wide value writes its byte
lane through to `mem[]` as it happens.** That costs exactly what the 6502 paid (the page step writes
one high-byte lane per pointer per scan line, against the 6502's three `INC`s) and the win is
undisturbed, because it is at the ~20 REASSEMBLY sites — `mem[lo] | (mem[hi] << 8)` collapsing to
one word read — not at the step.

⭐ **And the marshal belongs at the routine every path goes through, not at the 6502-ABI shims.**
The span walk is reached both ways: through the four `draw_span_*` arms and core-to-core from
`draw_surface_spans_core`, which no shim covers (the transitive-producer trap again, in the IN
direction). With the mirror live, `plot_ptrs_marshal_in()` at the top of `span_walk` is correct
whoever last moved the pointer, and the four shims need nothing at all.

⚠ Two process notes from the same diagnosis, both of which cost real time:
- **`make` does not build the harness.** `make` alone builds `build/revs`; the fixture binary is
  `make build/validate_native`. A loop that rebuilds with `make` and then runs
  `./build/validate_native` measures whatever was linked last — here, a binary five days stale, so
  three successive "fixes" were never in the code under test. The tell was a `timeout` that did not
  change behaviour across three different source edits.
- **Instrument, don't reason, once a second theory fails.** Two rounds of correct-but-insufficient
  reasoning each identified a real bug and left the symptom standing. `sample`/`lldb` on the hung
  process named the frame in one shot, and a case-scoped store trace (a global set from
  `diff_run`'s case index, printed from both the oracle and the twin because both reach
  `span_plot_core`) named the diverging store in the next.
- ⚠⚠ **A SABOTAGE LOOP IS ONLY AS WIDE AS ITS `FN` LIST.** The five defects were run against
  `draw_span,road_span_plot_2,draw_surface_spans,draw_road,interp_edge`, and D4 — the two screen
  pointers in `SPAN_PLOT_2` swapped, so the merged colour cell and `bearing_hi`'s copy go through
  each other's pointer — was reported SURVIVED. It is not: `road_span_plot` catches it with **12
  diffs**. That name was missing from the list, and `road_span_plot_2` (which was in it) enters
  `span_plot_core` on a path the swap happens not to disturb. So the standing "three explanations
  for a surviving sabotage" have a fourth, cheaper one that must be ruled out first: **the fixture
  that covers the defect was not in the filter.** Re-run a survivor against the FULL corpus before
  spending a minute on an argument. ⚠ And the `span_walk` fixtures are genuinely blind to D4
  (`draw_span` reads 0/400 with the swap in place) even though they call `SPAN_PLOT_2` 133 516
  times — measured with a counter, not inferred; the arms' own coverage of that descriptor is
  weaker than the direct entry point's.

⭐⭐ **AND THE PLANT PAYS FOR ITSELF TWICE: it found a pre-existing faithfulness defect.**
Extending the FIFTH test to `plot_line_octant` ($5204, twin #164) meant teaching its fixture the
same plant the span arms carry — one case in eight seeds `plot_ptr_hi` in page $00, so the
plotter's own pixel store lands on the cells it is walking. The planted fixture failed **and it
failed identically on the UNCONVERTED byte-lane twin**, which is what identified it as a defect
rather than a regression: the twin cached `math_lo` in a local commented "(reader, invariant)" and
kept the pixel counter `math_hi` in a local too. Neither is invariant — the 6502 re-reads `math_lo`
at $521a on every pixel and decrements `math_hi` **in place** at $529c, and this plotter can store
ON $74/$75. The failing case walks `addr = $0074` twelve times, so the oracle's DDA increment
changes under it and the twin's does not.

⭐ **The general form, and it is wider than this campaign: "this cell cannot change under me" is a
claim about EVERY writer, and a self-modifying plotter is one of its own writers.** A local that
caches a `mem[]` cell across a loop is a hypothesis; the plant is the experiment. It is
unreachable from the real needle origins (the dial-origin tables are frame-buffer addresses and
the walk spans at most ±$1900), so nothing shipping was wrong — but the fixture had been
*constructed* to avoid the case (`plot_ptr_hi = $60 + …`, commented "off ZP/HW"), which is why
6800 cases had nothing to say about it.

⭐⭐ **EIGHTH lesson — THREE ways the eligibility tests get misread, all found on this one pair.**

**(1) An indexed access by another TENANT does not block a relocation.** Test 2 as written ("never
indexed") blocked this pair for a whole campaign on `ADC 0x78,X` at $4C52 — which is
`update_grip_limits` reaching $78 for axle 0 and $79 for axle 1, nothing to do with the hypot. A
relocation moves **one use**; the test must be applied to the *relocated use*, not to the pair.
Re-read the other blocked rows with that correction in hand before trusting them.

**(2) Reaching a tenant from a marshalling parent is not the test either — LIVENESS at the
marshalling point is, and `validate` is what answers it.** I nearly recorded a second false block
here: `build_track_geometry`, `road_edge_walk` and `build_road_sign` all reach `plot_view_src_line`,
a tenant of $78/$79, so an OUT at their exits *could* stamp over a live tenant value. What settled
it is that `hypot_max` already marshals OUT at those same three parents, with `plot_view_src_line`
as *its* documented second tenant, and passes every gate — because the tenant's value is dead by
the parent's exit. Reachability is cheap to compute and proves nothing; the per-fixture differential
over full `mem[]` decides. ⚠ And build the reachability instrument carefully if you build one at
all: mine first reported "reaches 0 functions" for all six parents (a definition regex that missed
cores with arguments), then reported false edges through `kbd_test_key_core` from names matched in
comments. Zero for all six was the tell.

**(3) The oracle-glue channel bites in the OTHER direction: an inner shim's OUT changes the
ORACLE's answer, so a parent's diff can flip sign between runs.** `build_road_sign__t6502` calls
its children by their plain names, which link to the native shims. With `note_object_contact`'s OUT
missing, the parent's diff read `native = ref>>1` (native shifted, oracle not); adding that OUT
moved the oracle and the same fixture then read `native = random input` (oracle right, native
unpublished). **Compare the REF value across runs, not just native's** — a ref that moves when you
edit a shim is telling you which side the defect is on. Both readings pointed at the same missing
OUT, and adding it plus restoring the parent's OUT made the fixture pass.

⭐ Once again the differential found the marshalling set (`note_object_contact` was the miss) — the
a-priori shim audit would not have caught it, since that core calls `point_distance_hypot_apply`
directly and nothing about its name says "runs a hypot".

**Driver-bypass audit** (now mandatory per the SIXTH lesson, and it paid immediately): every
hypot-producing phase call in `race_main_loop_core` already goes through its shim
(`build_track_geometry()`, `build_road_sign()`) thanks to the previous commit. The three bare
`_core` calls — `draw_road_core`, `draw_track_object_core`, `read_driving_controls_core` — produce
neither magnitude; `draw_track_object`'s subtree reaches `plot_view_src_line`, which is a *tenant*
using `mem[]` directly and so is unaffected.

Sabotages D1-D7 all FAIL. validate PASS / determinism PASS / determinism-drive PASS / endian-lint
clean / tracks 6-of-6 / track-run every hook runs / Amiga muldiv + probe audits clean.

### Tier 2 — persistent, adjacent (wide-local hoist now; relocation later)

| Cell(s) | Addr | Role | gen readers | Mechanism | Status |
|---|---|---|---|---|---|
| `car_heading` | $0A/$0B | 16-bit heading angle | 0 non-native | B | **✅ DONE (2026-09-03)** — `car_heading_v`; see the eligibility row |
| `lap_length` | $59FC/D | track-file lap distance | some | A | **✅ A DONE (2026-09-04)** — all three readers assemble the word: `track_pos_advance` already did, `car_gap_tail` ($27AB) is now one 16-bit subtract instead of a hand-carried borrow chain, and `track_pos_retreat` ($5F9C) one 16-bit decrement. ⚠ Deliberately **not** (B): track-file data an expansion circuit's hook can patch at runtime, so `mem[]` stays the authority. ⚠⚠ And the retreat's wrap predicate is a **sign** test on the decremented high lane, not a zero test on the pair — a high lane already ≥ $81 wraps too. Writing it as `dist == 0` diffed five fixture cases; see §The wrap test below |
| `band2_duration` | $4F21/2 | horizon band duration | 0 (only irq1v + its oracle) | **B DONE** | ✅ `band2_duration_v` (revs_native.c), published back in band 1's arm |

### Tier 3 — SoA state vectors, non-adjacent (relocate `lo_8[N]`/`hi_8[N]` → `value_16[N]`)

The largest and highest-value tier. Single-instruction win needs mechanism (B); (A) hoist helps
in the interim but the final store stays two non-adjacent byte writes until relocation.

| Base | lo/hi | Shape | gen readers | Mechanism | Status |
|---|---|---|---|---|---|
| `MODEL_STATE` | $62D0/$62E0 | 15×16-bit driving-model state (stride $10) — incl. `heading_step` (element 2, $62D2/$62E2) and `slip_magnitude` (element $0A, $62DA/$62EA) | ~47 | A now; B blocked | **`heading_step` ✅ A COMPLETE — nothing left to convert** (see below); **`slip_magnitude` ✅ A DONE (2026-09-04)** — `steer_demand_from_slip` ($15F4) and `store_slip_signed` ($4B51) held the last lane pairs; the rest of the elements are still TODO |
| `CAR_ANGLE` | $62A0/$62A3 | 3× (heading_sin/cos, `steer_angle` = element 2, $62A2/$62A5) | ~9 | **A + B DONE** | `car_angle_16[3]` |
| `CAR_DISTANCE` | $08D0/$08E8 | per-car (24) distance-round-lap | ~19 | **A + B DONE** | `car_distance_16[24]` |
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

⭐ For a pair with readers on the far side of a 6502-ABI shim, the template is this plus the **IN/OUT
marshalling rule** above — that is the current, cleaner standard.

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

### ⚠⚠ FINDING (2026-08-30): `math_lo/hi` fails the native-only (B) pattern — the disqualifier is PERVASIVE

All shipping *readers* of `math_lo/hi` ($74/$75) are now native (#134–#166), which the worklist
below treated as the last blocker. It is **not**. The band2 pattern (relocate to a native-only
`_v`, keep `mem[]` for the oracle) is [[disqualifier]]-broken here, and not by the "6 oracle-only
bodies" the ledger named — by the **math primitives**:

- `mul8` / `mul8_noinit` / `div16by8` / `neg16_math` / `neg16_math_noinit` / `abs16_math` are the
  universal math accumulator's own operators, and they are **already native**. The transpiler emits
  the **native** call (`mul8()`, not `mul8__t6502()`) *inside* `__t6502` oracle bodies — so every
  oracle that multiplies/divides/negates is a HYBRID whose glue hands operands to the native child
  **through `mem[$74/$75]`**. Proof: `scale_wing_settings__t6502` does `math_hi = cpu.A; mul8();`.
  A native-only `_v` would leave `mul8` reading a stale variable while the glue wrote `mem[]` →
  validate fails on downstream outputs (wing_grip_coeff, car angles, …) in ~dozens of parents.
- **Consequence:** `math_lo/hi` can only be relocated **globally** — the `mem[$74/$75]`
  representation replaced by a real variable *everywhere at once*: native cores, the math
  primitives' cores, AND the entire `__t6502` oracle corpus. Because every reader/writer then uses
  the same storage, the glue↔core channel is consistent and the disqualifier vanishes; validate
  `set_ignore`s $74/$75 and `det_compare` `RELOCATED`-skips them. This needs a **transpiler change**
  so `revs_gen.c` emits variable-operating code for $74/$75 across all forms: plain load/store
  (already the `math_lo`/`math_hi` macro — redefine it), indexed `mem[MEM_math_lo]`, and the **74
  RMW `_M(MEM_math_x)` sites** (`ASL/LSR/ROL/ROR/INC/DEC`, needing `_V` variable-operating variants
  that thread `cpu.C`). Plus converting the hand-written aliases in `revs_native.c`
  (`PVS_HALF`/`PVS_GAP_COL`/`STEER_SIGN`/`STEER_DEMAND`, raw `mem[0x74/0x75/MATH_LO]`).
- This is a materially bigger, riskier step than any prior (B): it **regenerates the validation
  oracle**. The guard is validate staying green (with $74/$75 ignored) and
  `determinism-drive`/`-crash` (goldens recorded BEFORE the change) diverging at **exactly** $74/$75
  and nothing else. No `(zp),Y` uses $74/$75 as a pointer base, so a scalar variable is legal.

### ⚠⚠⚠ HARDER BLOCKER (2026-08-30): $74/$75 are INDEXED with $76 — scalar relocation is impossible

The global approach above still assumed a *scalar* `math_lo`/`math_hi`. It cannot be, because
**$74/$75 are accessed by a runtime index that spans $74/$75/$76 as one 3-byte value** — a scalar
has no `[X]`. Two sites, both already validated native twins whose `__t6502` oracles emit
`mem[(uint8_t)(MEM_math_lo+cpu.X)]`:

- **`build_section_step_delta` ($146e, twin #141):** `LDX #2; …SBC $74,X; STA $74,X…` (X=2,1,0) — a
  24-bit negate of {math_lo, math_hi, shared_temp_76}.
- ~~**`draw_track_object` ($2b16, twin #7):**~~ ⚠ **MISATTRIBUTED** — the site is `$2B18` inside
  **`step_delta_halve` ($2B0E)**, its own routine ending `RTS` at $2B1D, which had no `symbols.csv`
  row and so resolved to the preceding named function. `LDX #2; …ROR $83,X; ROR $74,X…` (X=2,1,0) —
  a per-component arithmetic shift-right of the STEP-DELTA vector, i.e. the *same tenant* as
  `build_section_step_delta` above and **not** the math accumulator. See §SETTLED below.

So the relocation unit is the **whole $74/$75/$76 window**, and it could only be an indexable
`uint8_t[3]` — which (a) forfeits the scalar register-allocation win that is the entire point of
(B), and (b) **drags in `shared_temp_76` ($76), the engine's busiest scratch cell** (~40 unrelated
writers, **120 transliterated refs** in `revs_gen.c` — nowhere near reader-native). Relocating that
window is not a "math_lo/hi step"; it is a `shared_temp_76` campaign an order of magnitude larger,
and one whose payoff is an array access, not a register.

**Conclusion — `math_lo/hi` ($74/$75) is NOT (B)-eligible.** The reader-nativization campaign
(#134–#166) that made every *direct* reader native was necessary bookkeeping but does **not** unblock
relocation: the real blockers are the indexed $74/$75/$76 coupling and $76's un-nativized surface
(and, independently, the hybrid-oracle mem[] channel of the native math primitives). The Ordering
item #1 below ("math_lo/hi first — highest ref count, the template for the rest") was premised on
ref-count and on "all readers native ⇒ unblocked"; both are refuted here. **The template cell for
the rest of the campaign must be a genuinely scratch-local one (Ordering item 2 — Tier-1 render
scratch), not the shared math accumulator.**

### ⭐⭐ SETTLED (2026-09-02): `math_lo/hi` is a SCRATCH pair, so the win is PER-TWIN, not a relocation

The re-audit the EIGHTH lesson demanded is done. Two corrections to the two sections above, then
the mechanism that actually applies.

**Correction 1 — the indexing charge is dropped.** The three indexed sites are `$146E`/`$1470` in
`build_section_step_delta` and `$2B18` in **`step_delta_halve` ($2B0E)** — a routine that had **no
`symbols.csv` row at all** and was being reported as `draw_track_object` ($2AD1) purely by address
ordering (see the §HARDER BLOCKER text above, which names `draw_track_object` for exactly that
reason). All three index the **step-delta vector** — lows $74/$75/$76, highs `point_delta_hi[0..2]`
($83/$84/$85) — built by `build_section_step_delta` and halved per component by `step_delta_halve`
on behalf of `place_car_world_coords`. That is a **different tenant** from the 16-bit accumulator,
and a tenant's indexed access does not block a relocation (EIGHTH lesson, part 1). The tenancy and
the `point_delta_hi` dual-low-half contradiction are now recorded on the cells in `symbols.csv`
(commit 67fb302). ⚠ The §HARDER BLOCKER conclusion "the relocation unit is the whole $74/$75/$76
window" therefore **does not follow** — it conflated two tenants of the same cells.

**Correction 2 — test 1 fails, but only just, and not where it matters.** Measured off the listing
(both notations; the earlier 305 was a partial count): **388 operand refs in 84 routines**, of which
only **6 refs in 4 routines** lie outside `VALIDATE_FUNCS ∪ NATIVE_FUNCS` — `$262D` (an unnamed
6×256 delay loop, `DEC $74/BNE $262F`), `$2B18` (`step_delta_halve`), `$31D0` (a dash-code plotter
loop: `$74` a row counter, `$75` its limit, storing through `(plot_ptr),Y`), and `$49BB` (an
unnamed seeder, `$74` scratch across eight instructions). Every one is **self-contained local
scratch in its own tenant**.

**The decisive blocker is the one in §FINDING (2026-08-30), and it is permanent.** The math
primitives (`mul8`, `mul8_noinit`, `div16by8`, `neg16_math`, `neg16_math_noinit`, `abs16_math`) are
this pair's *own operators* and are already native, so the transpiler emits the **native** call
inside `__t6502` bodies and every oracle that multiplies passes its operands to a native child
**through `mem[$74/$75]`**. Nativizing more readers cannot remove that channel; only replacing the
representation across the entire oracle corpus would, which regenerates the validation oracle
itself. **Do not re-open `math_lo/hi` as a pair relocation.**

⭐⭐ **NINTH LESSON — a SHARED SCRATCH cell is the wrong shape for mechanism (B), whatever its ref
count says.** (B) relocates *one use* of a pair; it pays off when that use is a **subsystem's
persistent value** with few tenants, so one marshal at the seam amortises over many wide ops.
`math_lo/hi` is the opposite: 84 tenants, each writing it, doing a few ops, and abandoning it. Its
huge ref count is a count of *tenants*, not of wide arithmetic — which is why it looked like the
campaign's biggest prize for three separate passes and was never eligible. **Rank candidates by
ops-per-marshal, not by ref count.** The corollary is that the win on a scratch pair is still
there, just under a different mechanism: **compute in a `uint16_t` local and store the two lanes
once at the end of the tenant's own use** (mechanism (A), per-twin, no relocation, no seam, no gate
blunting, oracle untouched). §FINDING (2026-08-27) records that this hoist is *already done* across
nearly all of `revs_native.c` — so for `math_lo/hi` the campaign's work is largely complete and was
mis-booked as pending. The 353 `math_lo`/`math_hi` references left in `revs_native.c` (90 functions)
are overwhelmingly single marshals into and out of the native math primitives, which is the ABI and
must stay.

## THE SEVEN NAMED PAIRS (asked 2026-09-03) — none is (B)-eligible, and only one has work left

A pass over the pairs still visible as `_lo`/`_hi` in the twins.  **Not one of them is a mechanism
(B) candidate**, and the reasons are structural rather than a matter of nativizing more readers, so
this list is the answer to "when do these get relocated": they don't.

**Group 1 — STRIDED SoA members. There is no adjacent pair to relocate.**

| pair | addrs | array |
|---|---|---|
| `heading_step` | $62D2 / $62E2 | `MODEL_STATE` element 2, stride $10 |
| `steer_angle` | $62A2 / $62A5 | `CAR_ANGLE` element 2, stride 3 |
| `slip_magnitude` | $62DA / $62EA | `MODEL_STATE` element $0A, stride $10 |

The lanes are $10 (or 3) apart because the engine stores these as parallel low/high arrays.  A
`uint16_t` here is only reachable as the **whole-array** `value_16[N]` relocation — every element
moving at once — which is a separate, much larger piece of work.  Until then these are mechanism
(A) only: compose a wide local inside the twin, store the lanes back once.  `draw_dash_needle_core`
is the worked example (it takes `uint16_t steer` while $62A2/$62A5 stay in `mem[]`).

**Group 2 — INDIRECT POINTER BASES. The 6502 dereferences the cells themselves.**

`plot_ptr` ($70/$71), `plot_ptr2` ($72/$73), `plot_ptr3` ($8E/$8F) and `SLIP_MAG`'s $8E/$8F home.
`LDA ($70),Y` / `STA ($70),Y` appear throughout the plotters, so the pointer must be readable *as
two bytes at $70* at every one of those sites; a relocation would marshal at every store, which is
the whole cost.  ⭐ **These are also where the real remaining byte traffic is, and (A) is NOT done
on them**: `revs_native.c:710-722` still advances the pair through two `adc_step` calls, and
`plot_line_octant` ($5243, $526B) hand-rolls the carry as `if (t > 0xFF) plot_ptr_hi++` twice.
That is exactly the "one `add.w` written as byte lanes" shape the campaign exists to remove, and
`plot_ptr` has 70 references in the twins — the largest pocket of the seven.  The store back to
$70/$71 must stay; what (A) removes is the carry propagation between the loads and that store.

**Group 3 — TRIED AND REVERTED.** `model_accum_delta` ($3A/$3B), with `model_accum_entry`
($38/$39), on 2026-08-27: the cells are `apply_driving_model__t6502`'s glue↔core channel and the
relocation broke it 200/200.  `model_accum_entry` was later recovered by shim marshalling (see the
inventory); `model_accum_delta` stayed in `mem[]` and should not be re-opened as a pair.

⭐⭐ **ELEVENTH LESSON (2026-09-03) — `point_dist` $7C/$7D is ELIGIBLE and STILL NOT WORTH DOING:
score the pair SITE BY SITE before writing a line of it.** Eligibility says a relocation is *legal*.
The NINTH lesson's metric says whether it *pays*, and `point_dist` is the first pair where the two
answers differ. Its four native sites, each scored as (wide ops gained) − (marshal ops added):

| site | what it does with the pair | gain | marshal | net |
|---|---|---|---|---|
| `point_distance_hypot_apply` ($0CB9/$0CBF) | the producer: two lane stores | 2 stores → 1 word store | OUT is **mandatory** (see below) = the same two stores | **0** |
| `project_point_core` ($22B0) | far clip, then `normalise_for_divide` shifts the LOW lane **in place** and reads it back | ~3 (one 16-bit compare) | IN+OUT ≈ 4 | **negative** |
| `road_edge_walk_core` ($23E3) | composes a wide local for one compare — already mechanism (A) | ~2 | IN ≈ 2 | **0** |
| `note_object_contact_core` ($2AB6-$2ABC) | `hi != 0`, then a **byte** `CPY` against the low lane | **0 — no wide arithmetic at all** | IN ≈ 2 | **negative** |

Nothing here is a subsystem's persistent value: every site loads the pair, does at most one 16-bit
compare, and drops it. And `mem[]` must stay authoritative at every seam, for three independent
reasons — the `region_23d8` shipping transliterated reader on the patched arm (the FOURTH test), the
`MODEL_TERM` tenant in `apply_driving_model` ($4874-$48DD), and the `PVS_KEEP`/`PVS_PREV_COL` tenants
in `plot_view_src_line` ($1C2C, $1CF4-$1D9A) — so marshal-IN cannot be dropped at any entry and the
gain is cancelled by construction. On top of that the patched arm is gated by **nothing** we run, so
proving it would cost a Brands real-BBC frame-buffer differential.

**Verdict: NOT STARTED, and not for lack of eligibility.** `point_dist` is the `math_lo/hi` shape
wearing a small ref count: a shared render scratch, not a value. The remaining win at these sites is
mechanism (A) — hoist into a `uint16_t` local within the tenant's own use — and `road_edge_walk_core`
and `project_point_core` already have it. ⭐ Build the Brands differential for CORRECTNESS (the hook
seams are gated by nothing at all), and if this pair ever becomes worth it, take it as a by-product.

⭐⭐ **TENTH LESSON (2026-09-03) — the ELIGIBILITY SCAN is an instrument, and it has produced a
wrong answer in three different ways now.** Each was caught only by the
[[revs_verify_the_instrument]] discipline of checking a KNOWN quantity in the output:

1. **The notation trap.** A pair is written both `0x74` and `0x0074` in the listing; scanning one
   notation undercounts. (Found 2026-09-02; the ref count went 305 → 388.)
2. **The extraction trap.** Pulling `VALIDATE_FUNCS` out of `transpile.py` with a non-greedy brace
   regex truncates the set at the first `}` in a comment, so already-native routines appear in the
   non-native column. **Extract by `index(name + ' = {')` … `index('\n}', i)`.** The tell was a
   routine known to be native showing up as a blocker.
3. **The immediate trap, and the STALE `func` ROW trap.** Matching a bare `$10` in the operand
   field also matches the immediate `#$10`, which made `edge_nearest` look blocked by seventeen
   unrelated routines. Match the four-digit zero-page form and skip `JMP`/`JSR`/branch operands.
   And once that was fixed, the last surviving blocker was a **stale symbol row**: attributing a
   ref to its owning routine by "last `func` row at or below this address" credits a false `func`
   row inside a real routine's extent with that routine's own references. `loader_stub` (`$1200`,
   nine bytes into `build_player_car`) blocked the `car_heading` pair for exactly that reason.

**The standing rule: before believing an eligibility verdict, check that a routine you KNOW is
native does not appear in the non-native column, and check that each surviving blocker is a real
routine and not a label.**

⭐ **And one fixture lesson from the same round, worth keeping here because it governs how a
relocation gets proved:** a **recomputed cell cannot be seeded**, so a coverage counter that counts
a seeded value is fiction. Two sabotages of twin #172 survived because `note_object_contact`
*recomputes* `point_dist` and writes `object_dist_hi` unconditionally — the fixtures' pre-seeded
arm counters were counting nothing. Force an arm through its INPUTS. This bites `point_dist`
directly: it is the pair whose (B) relocation still needs proof.

## ⭐⭐ THE CAMPAIGN IS NOW A DE-TRANSLITERATION CAMPAIGN, and `plot_ptr` $70/$71 is the target

**Read `tools/wide_eligibility.py` before adding a verdict to any table above.** Every eligibility
row on this page was derived by hand, and several had to be re-derived. The scanner attributes
every hit to its enclosing C function, parsed out of the generated source, and separates the three
populations the hand scans kept confusing: **native readers**, **`__t6502` oracles** (which read
`mem[]` by construction and block nothing) and **shipping transliterations** (which do).

Two of its own bugs are worth carrying forward, because each produced a *coherent wrong verdict*
rather than an obvious failure, and both were caught only by calibrating against a pair whose
answer is already known:

- **Only the transliteration spells a cell as raw hex.** Native code uses the `mem.h` names and the
  seam header's SoA bases, so a hex-only scan reported **zero** native readers for a pair with
  fourteen. (This is the same lesson as the alias-vs-`MEM_`-vs-hex point above, from the other side.)
- **A bare hex literal is usually an IMMEDIATE.** `LDX(0x0A)` is the constant 10, not address $0A —
  and it gave `car_heading`, relocated and green, four shipping readers that never touch it. A raw
  *address* access in `revs_gen.c` is always spelled `mem[0xNNNN…]`.

### Measured verdicts (2026-09-03), replacing the estimates in the tables above

| Pair | native | oracles | **shipping** | verdict |
|---|---|---|---|---|
| `plot_ptr` $70/$71 | 20 fn / 81 ref | 17 | **4 fn / 15 ref** | ⭐ the target — see below |
| `math_lo/hi` $74/$75 | 89 / 329 | 84 | 9 / 36 | scratch; per-twin, not a relocation (§SETTLED) |
| `plot_ptr2` $72/$73 | 13 / 33 | 11 | 3 / 8 | second in line |
| `point_delta` + `object_dist` | 21 / 39 | 26 | 3 / 7 | |
| `point_dist` $7C/$7D | 11 / 19 | 6 | 5 / 8 | ELIGIBLE-with-marshal, **scored negative** (§ELEVENTH) |
| `nearest_edge_bearing` $5E/$5F | 3 / 6 | 3 | 2 / 2 | |
| `plot_ptr3` / `SLIP_MAG` $8E/$8F | 8 / 12 | 11 | **0** | ✅ eligible, **scores 0**: no site holds it as a 16-bit value — `plot_ptr3_lo` is a shape index and a Y, `plot_ptr3_hi` is inc/dec'd alone. Separate tenants, not a pair |
| `lap_length` $59FC/D | 3 / 6 | 3 | **0** | ✅ eligible and **scores positive** — read-only TRACK FILE data (no runtime writer at all), and all three sites want it as a `uint16_t`. Small but free of any marshal-out |

### Why `plot_ptr`, and the running count

It is the biggest remaining prize by a wide margin — and, unlike the shared scratch cells that made
`math_lo/hi` look like the prize for three passes (§NINTH LESSON), it survives the ops-per-marshal
test: it is a genuine **16-bit screen pointer** doing real wide arithmetic, with
`plot_line_octant_core` alone holding 21 refs in the hottest loop of the render pass.

| | shipping blockers | refs |
|---|---|---|
| start (2026-09-03) | `mirror_draw_car`, `region_7bf7`, `view_paint_lines_short`, `region_31d0`, `undraw_plot_lines`, `console_io` | 23 |
| after twin #165b (`undraw_plot_lines`) | five | 21 |
| after twin #165c (`mirror_draw_car`) | `region_7bf7`, `view_paint_lines_short`, `region_31d0`, `console_io` | 15 |

### ⭐⭐ ...AND THEN THREE OF THE FOUR TURNED OUT NOT TO EXIST: "not an oracle" ≠ "shipping"

The worklist above (`region_31d0`, `view_paint_lines_short`, `region_7bf7` — 1341 lines of
de-transliteration) was **the instrument's third bug**, not work. The scanner classified any
non-`__t6502` transliteration as shipping, and all three are reachable ONLY from an oracle:

| routine | its only callers | verdict |
|---|---|---|
| `region_31d0` | `FUN_3d68` (from `paint_fence_backdrop__t6502`) and the uncalled entry wrapper `FUN_31d0` | dead — twin #128 absorbs both loops |
| `view_paint_lines_short`, `view_paint_lines_clipped`, `region_7bf7` (the whole $7BF7-$7F16 cell chain) | `view_paint_lines__t6502` | dead — the view pipeline is already all-native |

⭐ **This is decidable, because the port has no dynamic dispatch**: `Platform::indirectJmp` is a
no-op on every backend, so every call in the shipping build is a static C call and reachability is
a closure over the C call graph. `wide_eligibility.py` now computes it — roots are the corpus names
referenced from outside `src/gen`, and an edge out of a `__t6502` body is not followed.

⚠ The one thing that closure CANNOT see is the FOURTH eligibility test — a circuit's hook JMPing
back into the transliteration (`FUN_2490`). Such a target is always an **uncalled `FUN_<addr>`
wrapper**, so those get their own bucket and a human verdict, never a silent "dead". `region_31d0`
is in that bucket and is settled: `$31D0`/`$3D68` appear in no `make track-smc` extent, are not in
`ghidra_scripts/entrypoints.csv`, and `docs/rename.md` already records both as provably-dead
labels.

Two more instrument bugs fell out of checking the first by hand, and both are the same shape —
**a parser gap reads as a finding**:

  * `functions()` skipped every ONE-LINER definition, and the transpiler emits every region entry
    wrapper as one (`void view_next_scanline(void) { region_7bf7(0x7EF3); }`). Those are exactly
    the edges the graph hangs on.
  * `external_roots()` matched `name(` and so missed every entry point passed as a function
    POINTER (`platform_register_vbi`). 17 roots became 49.

Sabotaged four ways before the verdict was believed (a shipping caller gains a call → SHIPPING; an
oracle gains one → no change; a wrapper-only path → the wrapper bucket; the one-liner support
removed → the original wrong answer). ⚠ Note the shape of the original error: the tool never
failed, it produced a coherent worklist of real functions.

### So `plot_ptr` is eligible NOW, and so are its two siblings

The only true shipping readers left are `console_io` ($6300, 2 refs) for `plot_ptr` and
`emit_driver_name` for `plot_ptr2` — and both are **write-only**: a front-end text routine parks A
and Y in the plotter's zero page on entry and never reads them back. Keeping those two stores is
the whole cost; there is no marshal-in anywhere.

| pair | native | shipping | verdict |
|---|---|---|---|
| `plot_ptr` $70/$71 | 21 fn / 83 refs | `console_io` (write-only) | ✅ eligible |
| `plot_ptr2` $72/$73 | 13 fn / 33 refs | `emit_driver_name` (write-only) | ✅ eligible |
| `plot_ptr3` $8E/$8F | 8 fn / 12 refs | none | ✅ eligible, but see below |

⭐⭐ **They must move together.** The span rasteriser's `SpanPlotter` descriptors carry their
pointer slots as `mem[]` ADDRESSES (`{MEM_plot_ptr2_lo, MEM_plot_ptr_lo}`,
`{MEM_plot_ptr_lo, MEM_plot_ptr3_lo}`) and `span_end_marker`/`fill_column_gaps_core`/`sw_marker`
select a slot at run time. Relocating one and not the others would force a marshalling temp inside
a routine that runs eight times per scan line — a cost, not a win. The unit of work is all three
slots becoming `uint16_t`s and the descriptors carrying pointers to them.

### The aliasing hazard: asked, and MEASURED

Before relocating a plotter pointer there is one question a call-graph scan cannot answer. The span
walk can climb `plot_ptr_hi` through page $00 and **store through its own pointer into zero page** —
that is not a theory, it is the measured reason `mem[arm->addend]`/`mem[arm->subtrahend]` may not be
hoisted out of `span_walk` (the hoist fails 3 of 400 fixture cases on each `fwd` arm, 2026-09-02). A
relocated `uint16_t` would silently miss such a self-write, so the question is whether a plotter
store ever lands on $70-$73 or $8E/$8F.

Measured with a temporary `bus_write` probe (page-0 histogram + `__builtin_return_address`
attribution) over both determinism trajectories, 300 frames each:

| | page-0 `bus_write`s | of which $70-$73 / $8E-$8F |
|---|---|---|
| parked (`make determinism`) | 54 723 | **0** |
| driving (`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`) | 429 294 | **0** |

#### ⚠⚠ ...AND THE PROBE WAS BLIND TO THE STORES THAT MATTER

**Retracted the same day it was written.** The probe hooked `bus_write`. The plotters are native
twins now, and a native twin stores with a raw `mem[addr] = v` — **356 raw stores in
`revs_native.c` against 17 `bus_write` calls**, and every plotter store is a raw one
(`span_plot_core`'s two stores included; the "both stores go through bus_read/bus_write" comment
above `SPAN_PLOT_1` is stale and describes code that no longer exists). So the table above counts
the *transliteration's* indirect stores and cannot see the plotter's.

It is the documented failure exactly: **an instrument that answers a narrower question than the one
asked, and answers it coherently.** The zero is real; it is a zero about the wrong population.

⭐ What survives is the calibration and the negative result about the transliteration — and the
knowledge that **the real gate was already in the tree.** The four span-arm fixtures plant the
pathological case deliberately: "one ascending case in twelve starts above its bound", which is
what makes the walk climb `plot_ptr_hi` through page $00. That plant is what failed the
`mem[arm->addend]` hoist 3 of 400 times on each `fwd` arm and 0 of 400 on each `rev` arm. So
`make validate FN=draw_span` is a *stronger* instrument than any probe here, because it
manufactures the hazard instead of waiting for a trajectory to produce it. Relocate, then run it —
and treat 400/400 on the two `fwd` arms as the evidence, not a histogram.

⚠⚠ The hazard is real, but it lands somewhere else entirely: **428 745 of the driving run's
429 294 page-0 indirect stores go to `$74`** (`math_lo`), all from one site in
`move_and_draw_cars`. That is a FIFTH independent reason to leave `math_lo/hi` alone, and it is
also the calibration that says the probe works — a probe that had read 0 everywhere would have
proved nothing.

⭐⭐ **And the measurement overturns the standing "do not widen" comment at
`plot_line_octant_core`** — the pair's biggest site (21 refs), which reads *"DO NOT widen these
four pointer steps into a uint16_t — it is a PESSIMISATION"*. That comment is **correct for
mechanism (A) and wrong for mechanism (B)**, and the difference is the premise: it costs out
`p = lo | hi<<8; p += 8; store both lanes` — 4 mem[] accesses where the byte path does 2. Once the
pointer is a real `uint16_t`, `p += 8` is one word read and one word write: 2 accesses on the
straight path (equal) and 2 on the carry path (better than 3). The site flips from decline to win.
⚠ Fix that comment in the same commit that relocates the pair, or the next pass will read it and
stop again.

⚠ `plot_ptr3` $8E/$8F is dual-tenanted with `slip_magnitude` (§`plot_ptr3` above scores it 0 *as a
pair*). That verdict stands for the SLIP_MAG tenant, which keeps `mem[]`; the plotter tenant moves
because the descriptor requires it. The two never overlap in time, and neither reads the other's
value — each writes before it reads.

⭐ **Nativizing a blocker pays twice.** `mirror_draw_car` also freed its own caller: twin #152 had
kept `mirrors_update`'s whole segment loop in the 6502-ABI shim *only* because this callee took its
inputs in A and Y. Check every caller's shim after removing a blocker.

### `plot_line_octant` ($5204): the lanes are load-bearing for the READ, not the store — and only near page $00

The dashboard line plotter (`dial_needle_angle`, `draw_dash_needles`, `undraw_plot_lines`) walks
`plot_ptr` `$70/$71`.  Converted: the four moves (`$5233` cell left, `$5243` cell right, `$5255`
row up, `$526B` row down) are one word add each, and the store address at `$5285` is
`plot_ptr_v + y` — the per-pixel reassembly (two byte reads, a shift, an or) is gone.  The
"⚠ DO NOT widen these four pointer steps" note that stood in the twin is **retracted**: it costed
the steps correctly and missed that the reassembly they were protecting was the larger bill.

**But the byte lanes cannot simply be dropped.** The FIFTH eligibility test names the store
direction — the plotter can write its own pointer, closed by `plot_store_resync`.  The direction
that actually bit here is the **read**: the pixel is a read-modify-write of `($70),Y`, so a pointer
in `$006A..$0071` fetches its own low or high lane *as the screen byte*.  Dropping the lanes
entirely fails the fixture.

The resolution is a page test, not unconditional bookkeeping.  `PLOT_PTR_ADD` refreshes the lanes
only while the pointer is below `$0100`, a conservative superset of the danger window.  Every
shipping caller plots into the frame buffer at `$3000` and up, so the steady state is one word add
and a not-taken branch — no RAM writes at all — and the pathological case stays byte-exact.

⭐ **General form: when a relocated value must keep a live `mem[]` mirror, ask whether the mirror is
needed EVERYWHERE or only in an address window.** A window test is a register compare; an
unconditional write-through is two RAM writes in the inner loop, which is the cost the campaign
exists to remove.

### The fixture had to be AIMED, not just randomised

Two of the eight sabotages — dropping the lane refresh on one of the four steps, and dropping the
final marshal-out — survived a fixture that seeded the pointer's low byte uniformly.  A uniform
byte reaches the `$006A..$0071` window 8 draws in 256, and only some of those cases then step in a
direction that reads the lane.  Biasing the planted low byte to `$50..$8F` caught both.
**A plant that makes the hazard *possible* is not a plant that makes it *likely*** — size the
planted distribution against the window it is meant to hit, or the plant is decoration.

### One sabotage that survives for a reason, written at the code

`if (oldLo >= 8u)` vs `> 8u` is the same program: at `oldLo == 8` the new low byte is `0`, and the
re-test asks `a >= 8`, which `0` fails — so the borrow arm falls through to `x_done` exactly where
the no-borrow arm jumps.  No-change-by-construction, the third of the standing three explanations.

### ⚠ And a note on the sabotage HARNESS itself

A first pass reported **all eight defects surviving**, including "store address +1", which is
impossible.  The cause was neither the twin nor the fixture: the sabotage driver was a shell
function with an inline heredoc, and it was building unpatched sources.  Re-run as a script file,
seven of eight were caught.  ⭐⭐ **A sabotage sweep in which nothing at all is caught is a report
about the sweep.** Before believing it, sabotage the sweep — patch one defect by hand, build it by
hand, and confirm the harness fails.  (And detect a failure by the harness's **exit status**: a
`grep` for `DIFF]` misses fixtures that report only a `N mismatch` line.)

### `plot_view_src_line` ($1C1C): the resync is DEAD here, and that is a property of the POINTER

`column_gap_walk` and `plot_line_octant` both need `plot_store_resync`, because their pointer is
seeded from a *runtime* zero-page cell and can therefore be aimed anywhere, including at its own
lanes. `plot_view_src_line`'s cannot: it is `VIEW_SRC_PAGE * $100 + column * $80` with the column
already clamped below `$28`, so the pointer spans `$3000..$4380` and nothing else, and neither of
its two fill loops can store on `$70..$73`.

So the resync calls that looked like due diligence were dead code, and the sabotage sweep said so:
dropping either one survived. **Both were removed and the argument written at the code.** The rule
this gives: *a self-write guard belongs where the POINTER's range is open, not wherever a pointer
is stored through* — and a surviving sabotage of such a guard is the "no case to show it in" third
explanation, which is free.

Two other results from that sweep are worth keeping. A first attempt at the address sabotage
patched `pointer_is_ram(base)` instead of the store address, and *survived* — an off-by-one in a
RAM-window test almost never changes its answer, so **the patch has to land on the value the
routine computes, not on a predicate near it.** And the setup's `marshal_out` is genuinely
load-bearing: dropping it is caught, because the routine's own exit `A` is the pointer's page.

### The wrap test: a 6502 sign test is not a zero test (`track_pos_retreat`, $5F9C)

`track_pos_retreat` steps a car one distance unit backwards and wraps to a full lap at the start
line. The 6502 spells the wrap as three lane steps: if the low byte is already zero, `DEC` the
high one, and if the high one comes out **negative**, reload both lanes from `lap_length` and
un-book a lap.

Rewritten wide, the tempting form is `if (dist == 0) dist = lap_length;`. **It is wrong, and it
is wrong in a way that only shows up on out-of-range distances**: the 6502 branches on the *sign*
of the decremented high lane, so a distance whose high byte is already `$81`-or-more wraps too.
The fixture found it in five cases. The faithful wide form keeps the sign:

```c
while ((dist & 0x00FFu) == 0u && ((uint16_t)(dist - 1u) & 0x8000u) != 0u) { dist = lap; ... }
```

⭐ **The general rule for this campaign: when collapsing lane steps into one wide op, check what
the 6502 BRANCHED on, not what the lanes end up holding.** `BPL`/`BMI` after a `DEC` is a sign
test on one byte, and the wide value's own bit 15 is only the same predicate when the value is
in range. The lanes agreeing is not the same as the predicates agreeing.

### `heading_step` ($62D2/$62E2): (A) is COMPLETE, and the reason is the LAYOUT

`heading_step` is `MODEL_STATE` element 2, and `MODEL_STATE` is a **plane-split SoA**: the low
bytes live at `$62D0..$62DF` and the high bytes at `$62E0..$62EF`. The two lanes of any one
element are therefore **$10 bytes apart**, so *no word load of the pair exists even in
principle* — not on the 68000, and not after a (B) relocation of one element, which the SoA rule
forbids anyway (every indexed access would have to move at once, and
`model_integrate_element` indexes both planes by element).

Two byte loads plus a shift and an or **is** the minimum here, and all three readers already do
exactly that into a `uint16_t` (`rebase_edge_point` $0BA7, `stage_accum_delta` $4729,
`integrate_heading` $4927). The remaining byte-wide uses are genuinely 8-bit and not lanes at
all: `$4DD4`'s `SEC / ROR heading_step_lo` is a one-byte rotate of the low byte on its own, and
`$1C0B` / `check_crash` use the **high byte alone** as the yaw rate.

⭐ **The lesson for the rest of this campaign: a lo/hi pair whose planes are not adjacent has no
"two word accesses beat three byte accesses" win available.** The win in the pair grind comes
from (a) collapsing lane-wise *arithmetic* into one wide op, and (b) turning two adjacent bytes
into one word access. A plane-split SoA offers (a) only, and once (a) is done the element is
finished — record it as complete rather than re-opening it looking for (b).

## ⭐⭐ CAMPAIGN STATUS (2026-09-04) — the by-function ledger, and the last verdicts

**Mechanism (A) — the wide-local hoist — is now COMPLETE across `revs_native.c`.** A sweep for
lane-wise arithmetic (a byte add/subtract with a hand-carried carry, a lane-by-lane negate, a
lane-by-lane shift) finds no remaining site: what is left of `adc_value` / `sbc_value` /
`adc_step` / `sub_from` is the **sanctioned BCD** surface (the standings add, the race clock, the
decimal-honoured 24-bit key compare) plus single-byte `adc_step` sites with the argument written
at the code. Those are the eight `SED` routines of `docs/static-map.md` §Decimal mode and they
must stay byte-wise.

Converted, by function (each landed with `make validate FN=…`, a 4+ defect sabotage sweep, and
the full gate set):

| Value | Function(s) taken off the byte lanes | What the routine does |
|---|---|---|
| `lap_length` | `car_gap_tail` ($27AB), `track_pos_retreat` ($5F9C) | the wrapped gap between two cars; backing a car's lap distance up over the start line |
| `car_distance` | `car_gap_tail` ($27AB) | `D = dist[Y] - dist[X]`, the raw signed gap, with the entry borrow |
| `car_speed` (frac:scaled) | `check_car_pair` ($2692) | the 16-bit speed difference feeding the overtaking arm |
| `steer_angle` | `apply_steering_assist` ($1F08) | Computer Assisted Steering: |angle|, the bias, and the two re-signs |
| `slip_magnitude` | `steer_demand_from_slip` ($15F4), `store_slip_signed` ($4B51), `store_slip_clamped` ($4B47) | the slip-derived steering demand and its clamp |
| `car_heading` | relocated to a `uint16_t` global (`bebe208`) | mechanism (B) |
| `edge_nearest` | relocated to a `uint16_t` global (`47f426e`) | mechanism (B) |
| `bearing`, `hypot_min`, `band2_duration` | relocated (B), earlier | |

Scored and **declined** — eligibility said legal, the ops-per-marshal metric said no (ELEVENTH
lesson: score a pair site by site before writing a line of it):

- **`point_dist` $7C/$7D** — all four native sites zero or negative; `note_object_contact` does no
  wide arithmetic with it at all, and three tenants keep `mem[]` authoritative.
- **`point_delta` $80–$83 / `object_dist` $55** — massively multi-tenanted scratch (SPAN_DX/DY,
  MUL_SRC/TERM, EDGE_COLUMN, OBJ_VECTOR_CURSOR, OBJ_EDGE_X/STYLE, the step-delta highs). Every
  camera-delta site already assembles a `uint16_t` and stores the lanes back **because the cells
  are the next consumer's input**. `object_dist_hi` has no low lane at all — it is an 8-bit
  level-of-detail band, not half of a word.
- **`band1_duration` $4F1F/$4F20** — (B)-eligible and *adjacent* (`band2_duration_v` is the
  precedent), but it scores ~zero: the bytes must be PUBLISHED to `mem[]` anyway, so the producer's
  cost is unchanged and the consumer saves one load **once per field**.

Complete **by layout** (THIRTEENTH LESSON — a plane-split pair has no word-access win to find):
`heading_step` $62D2/$62E2, `car_speed` $62D9/$62E9 and `model_accum` $62D8/$62E8 (MODEL_STATE,
planes $10 apart); `section_coord` $0900/$0A00 (planes $100 apart); `PLOT_UNDO_PTR` $07A8/$07D0
(planes 40 apart — and a real `uint16_t[40]` would need 40-word marshalling at two shims per call,
strictly negative against a ~6-byte needle line).

**Measured outcome: still the null result.** The fresh baseline after the 2026-09-04 conversions is
**4.27 FPS** (row-vector average of the 11 non-outlier rows, clean `STRAIGHT_TO_RACE=1 FPSCOUNT=1
FIXED_RNG=1` build under warp) against a stale ~4.15 — **+2.9%, inside noise and not a claimed
win**. Quote 4.27 as the baseline from here. The instruction-count win is real and the byte traffic
is gone; the frame is not spent there. **The next lever is the REPRESENTATION change**
(`docs/direct-bitplane-plan.md`), not another pair.

Still open, and neither is a wide-value conversion:
- `plot_object` uses `plot_ptr3_lo` as a **scalar** shape-edge index → `docs/rename.md`.
- `plot_view_src_line`'s `plot_ptr_lo = 0x00u;` before the wide setup — a lane write that is
  harmless but inconsistent with the twin's own idiom.

## ⭐⭐ NEXT STEPS (planned 2026-09-04) — mechanism (B) on Tier 3, in measured order

Mechanism (A) is finished; **Tier 3 — the SoA state vectors — is the campaign's remaining body of
work**, and it is mechanism (B): relocating a plane-split `lo_8[N]`/`hi_8[N]` into a real
`uint16_t value_16[N]`.

⚠ **Scope correction to the THIRTEENTH LESSON.** "A plane-split pair has no word-access win" is
true **of mechanism (A) only**. A (B) relocation *creates* the adjacency — that is the entire point
— so `heading_step`, `car_speed`, `model_accum` and `section_coord` are complete as (A) and
**still open as (B)**. The lesson's "record it complete rather than re-opening it" applies to the
hoist, not to the relocation, and the earlier phrasing was too broad.

### Step 0 — fix the eligibility scanner's FIFTH bug (a FALSE ZERO), before scoring anything

`wide_eligibility.py` resolves symbolic bases from `mem.h` and **the seam header only**. Several
SoA bases are declared by a **file-local `#define` inside `src/gen/revs_native.c`** —
`CAR_DISTANCE_LO` ($08D0), `SECTION_COORD_LO/HI` ($0900/$0A00), `EDGE_OPP_X_LO/HI` ($5E50/$5EA0),
`OBJECT_COORD_LO/HI` ($09FD/$0AFD), `CAR_SPEED_FRAC` ($3850) — and those are invisible to it.
Measured: the scanner reports **0 native readers for $08D0** while `CAR_DISTANCE_LO` has **10
references** in `revs_native.c`.

It is the same class as its documented bug #1, from the other side again, and it is the worst
possible direction: a false zero reads as *"nothing to gain here"*. ⇒ Teach it the native files'
own `#define`s, then **re-run the five calibration sabotages** before trusting a single re-scored
row.

### The measured worklist (scanner output, 2026-09-04)

| Base | native | shipping blockers | verdict / order |
|---|---|---|---|
| `MODEL_STATE` $62D0/$62E0 | **23 fn / 84 ref** | **1 fn / 1 ref, and it is a READ** | ✅ **(B) DONE** — `model_state_16[15]`, see below.  ⚠ the "5 fn / 7 ref" first reported here was a SCORING ERROR, see below |
| `CAR_ANGLE` $62A0/$62A3 | 12 fn / 32 ref | **none** | ✅ **(B) DONE** — `car_angle_16[3]`, see below |
| `VIEW_ORIGIN` $6280 | 5 fn / 14 ref | **none** | ✅ **(B) DONE** — `view_origin_16[9]`, see below |
| `MARKER_OFF` $62B7/$62BA | 2 fn / 6 ref | **none** | ✅ eligible, small — `append_corner_marker` / `draw_corner_markers` |
| `OBJECT_BEARING` $0380/$0398 | 6 fn / 6 ref | **none** | ❌ **DECLINED** — 0.42 refs per element, see §REFS PER ELEMENT |
| `SECTION_COORD` $0900/$0A00 | **10 fn / 38 ref** | **none** | ❌ **DECLINED, MEASURED** — 256 elements, 0.17 refs per element, a ~15× net loss |
| `CAR_DISTANCE` $08D0/$08E8 | 4 fn / 16 ref | **none** | ✅ **(B) DONE** — `car_distance_16[24]`, see below |
| `OBJECT_COORD` $09FD/$0AFD | 10 fn / 27 ref | **none** | ✅ eligible — 24-bit binary → `uint32_t`, `add.l`/`sub.l`/`cmp.l` only |
| `EDGE_OPP_X` $5E50/$5EA0 | 7 fn / 12 ref | **none** | ❌ **DECLINED** — 80 elements, 0.24 refs per element |

**Step 0 is DONE** (`af19737`): the scanner resolves a twin's own `#define`s, calibrated against
$08D0's ground truth (ten grep hits = one define + one comment + eight real refs; the tool reports
eight), and six sabotages re-verified. ⭐ The false zero had retired four real candidates — every
one of them turns out to have **zero shipping blockers**, so the whole re-scored group is eligible
and the only remaining question on each is ops-per-marshal.

⚠⚠ **`MODEL_STATE`'s "five shipping blockers" were an ARTEFACT OF A WRONG EXTENT, and they
retired the biggest base in the engine for three passes.** The scan had been run as
`--count 16`, which walks $62D0..$62DF and $62E0..$62EF — two bytes past each plane. $62DF is
`loop_counter_hi` and $62EF is a separate cell, both explicitly excluded from the vector by
`symbols.csv`, and they are exactly the cells `update_lap_timers` / `finish_race` /
`reset_driving_variables` / `tick_race_timers` touch. Re-run over the real extent
(`python3 tools/wide_eligibility.py 0x62D0-0x62DE 0x62E0-0x62EE --label MODEL_STATE`) it is
**23 native functions / 84 refs with ONE shipping reference** — `advance_player_section` taking
element 2's high byte — and that one is a READ, which the IN/OUT rule already permits. **The
lesson is the same one twice: an extent is a claim, and a scan's window IS the measurement**
(`docs/validation-harness.md`, memory `revs_verify_the_instrument`). Give the scanner the extent
`symbols.csv` states, not the round number the stride suggests.

### Per relocation, the standing template and gates

1. The (B) template of §`band2_duration`: authoritative `uint16_t value_16[N]`, `mem[]` retained as
   the 6502-ABI mirror, `marshal_in`/`marshal_out` at the shims, mirror cases in the `bus_write`
   switch for the whole range.
2. The **FIFTH test** — can the value write itself? For an SoA the question is whether any
   indirect/plotter store can land in the range; where it can, use `plot_line_octant`'s answer (a
   page/window test, never an unconditional write-through).
3. **Aim the fixture, don't just randomise it** — a plant that makes the hazard *possible* is not
   one that makes it *likely*.
4. 4+ sabotages from a **script file** that `rm`s the object and binary each iteration, detected by
   **exit status**.
5. Full gates: `validate`, `endian-lint`, `determinism` / `-drive` / `-crash`, `tracks`,
   `track-run`, and the Amiga link's `muldiv-audit` + `probe-audit`.

### Measure ONCE, at the end of Tier 3

A single relocation is far under the 3% noise floor, so per-relocation FPS readings would be
noise dressed as progress. Take one row-vector reading against the 4.27 baseline when Tier 3 is
complete. **If Tier 3 is also a null result, that is the campaign's answer** and the representation
change (`docs/direct-bitplane-plan.md`) takes over as the lever.

### Still owed, unrelated to Tier 3

- Confirm the two write-only shipping blockers are satisfied by shim marshal-in alone:
  `console_io` ($70/$71) and `emit_driver_name` ($72/$73).
- The **Brands real-BBC frame-buffer differential** — for CORRECTNESS, because the hook seams are
  gated by nothing, not for perf.

## Ordering

0. **Step 0 consolidation** (above) — clears the duplicate-define debt first.
1. ~~**`math_lo/hi`** — highest ref count; its threading pattern is the template for the rest.~~
   **✗ RETIRED (2026-09-02)** — not (B)-eligible and never was; see §SETTLED above and the NINTH
   lesson. Ranking it first was the ref-count error that lesson names.
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

`menu_wait_key` ($6571, #166 — the front-end MENU SELECTOR, and the **LAST shipping reader** in the
campaign: with it native, every runtime-reachable reader of `math_lo`/`math_hi` is idiomatic C).
The front-end chain ($63E0) calls it five times to read one menu answer. It renders/polls in a loop
until one of `menu_key_tbl[0..count]` ($39E0 — SPACE/1/2/3 negative-INKEY codes) is held, then
highlights the chosen row and returns the confirmed selection in X. `math_hi` ($75) reader-nat:
`count` arrives in X, is stored to `math_hi`, and is **read back twice** (the scan's start index and
the highlight loop's ceiling) *across* the child calls `FUN_3261` and `text_script_interp` — so the
twin reads/writes the cell **directly, never caching**, the #159-CRUX-safe form (a child that
overwrote $75 is then honoured identically). Index 0 (SPACE) confirms, but only once a non-zero row
has been shown (`shared_temp_77` latched to $1E on the first pick); it writes $98 to $7FC5 and
returns `hypot_min_lo - 1`. Exit X live — `LIVE_X`. ⚠ **Multi-frame poll needs a clock-driven
keyboard:** a static held-set can never "press SPACE *after* a pick", so the fixture drives a new
**mode-4 CLOCK SCHEDULE** backend (`platform_test_key_schedule` + the tick clock): which code reports
held is a function of the tick clock, which advances one per loop iteration, and the clock cell lives
in `mem[]` so `diff_run`'s per-run reset gives both models the identical phase sequence. Two hazards
pinned by construction: `FUN_3261` does TXS + re-enter `front_end_menus` when `mem[$1C]` bit 7 is
CLEAR (the SHIFT+f0 restart), so bit 7 is pinned SET (the `FUN_327d` no-op arm) and SHIFT ($FF)/f0
($86) are never scheduled held; and `text_script_interp($1E)` (run once on the first pick) is made a
no-op by pointing script $1E at a lone $FF byte. Fixture 4000 cases, count ∈ {2,3}, sel ∈ {1..count},
two schedule shapes (pick→confirm, and space-first→pick→confirm to exercise the $6591 "nothing shown"
redraw). 4 sabotages FAIL distinct — attr-swap ($7E85 $81↔$84), no-DEX (**REG** X off-by-one, which
also proves `LIVE_X` is checked), marker ($7FC5 $98→$00), stride ($7ED5 wrong cell). (Dropping the
entry `math_hi` store is also detected, but manifests as a *hang* — a garbage scan range never
confirms — so it is not part of the clean-count set.))

**All shipping readers of `math_lo`/`math_hi` are now native.** Next: the mechanism-(B) relocation
of `math_lo`/`math_hi` ($74/$75) out of `mem[]` into real `uint16_t` variables — keeping the cells
`mem[]`-visible to the 6 oracle-only bodies that still reference them (see the over-count note below).

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


---

## `CAR_ANGLE` $62A0-$62A5 → `car_angle_16[3]` — the first Tier-3 (B) relocation

The three-entry array the 6502 kept **plane-split**: low bytes $62A0-$62A2, high bytes $62A3-$62A5,
so element *i* cost two strided byte accesses each way.  Now three `uint16_t` in `revs_native.c`,
declared in `revs_native_seam.h` with `CAR_ANGLE_SIN` / `_COS` / `_STEER` naming the elements.
**Thirteen native sites, all converted in one commit** (the array is only useful whole):

| routine | what it does with the array |
|---|---|
| `compute_car_angles_core` | **producer** of elements 0/1 — builds each word directly (`res & $FFFE`, saturation `$FFFE`) instead of splitting into `lo`/`hi`, then ORs the quadrant sign into bit 0 of the word |
| `apply_angle_term_body` | the multiplier of every driving-model rotation: one word read, then `termLo`/`termHi` off the register |
| `clamp_and_store_steer_angle_core` | **producer** of element 2 — one word store where it was two byte stores |
| `assist_from_selector` | reads element 2 as a word (was composed from two lanes), plus the bit-0 sign test |
| `limit_steer_demand_core`, `apply_steer_demand_core`, `steer_demand_from_slip_core`, `steer_apply_with_assist_core`, `read_driving_controls_core`, `draw_dash_needles` | consumers of element 2 — high byte, low byte, sign bit or the whole word |

**⚠⚠ The packing is SIGN-MAGNITUDE, not two's complement.** Bit 0 of the low byte is the SIGN and
bit 7 is the value's own lowest bit — `compute_car_angles` builds `(magnitude << 1) | sign` with its
`ASL`/`ROL` pair, and `mul16_signed` reads bit 0 as its multiplier's sign.  That packing *is* exactly
a 16-bit word, which is why (B) applies at all; but no reader may treat one of these words as a
plain signed quantity.  Magnitude is `w >> 1`, sign is `w & 1`.  Sabotage S6 below is that trap.

### The boundary rule this one established

**mem[$62A0-$62A5] and `car_angle_16[]` agree at every 6502-ABI shim.** A shim whose core reads the
array marshals in; one whose core writes it marshals in *and* out — **in as well as out, because no
writer owns all three elements** (`compute_car_angles` owns the sin/cos pair, the steering path owns
element 2, and a marshal_out publishes all three).  Sixteen shims are on the boundary; a
read/write transitive closure over the two native files found them, and the four that the first pass
missed are exactly the ones `make validate` then failed: `apply_angle_term_at`, `apply_driving_model`
and the two `rotate_state_*` entries.  ⭐ Keeping mem[] authoritative at the boundary is also what
lets `det_compare.py`'s `RELOCATED` stay **empty** — all three determinism runs are 64K
byte-identical *including* these six cells, so an unknown transliterated writer of them would still
be picked up at the next shim.

### Gate

`make validate` 136 fixtures, 0 mismatch; `determinism`, `determinism-drive` and
`determinism-crash` 64K byte-identical; `tracks` 6/6; `track-run` every circuit's code executes;
`endian-lint` clean; the Amiga link clean (`muldiv-audit`, `probe-audit`).  Seven sabotages, each
`rm`-ing the object and the binary first, **all seven FAIL with seven different mismatch counts**
(1450 / 3000 / 1992 / 1951 / 1990 / 1546 / 2000 — distinct counts are the check that no iteration
reused a stale object):

| # | defect | detected by |
|---|---|---|
| S1 | drop the cos quadrant sign bit (`\|= 0x0000`) | `compute_car_angles` |
| S2 | `car_angle_marshal_out` swaps the two lanes | `compute_car_angles` |
| S3 | `car_angle_marshal_in` swaps the two lanes | `apply_angle_term` |
| S4 | `apply_angle_term_body` takes `termLo` from the high half | `apply_angle_term` |
| S5 | the clamp stores the steer word byte-swapped | `clamp_and_store_steer_angle` |
| S6 | the sign read as bit 15 instead of bit 0 (the packing trap) | `apply_steering_assist` |
| S7 | drop `car_angle_marshal_in` from one boundary shim | `apply_angle_term` |

## `CAR_DISTANCE` $08D0/$08E8 → `car_distance_16[24]` — and the harness hole it exposed

The per-car **distance round the lap**: a plane-split SoA pair, low bytes at $08D0, high bytes at
$08E8, one entry per car slot. **24 slots, not 20** — the twenty racing cars the field loops walk
($00-$13) plus the pace/reference car $17 that `stage_nearby_car` measures every gap against; the
array's own extent is the proof, since $08D0 + 24 = $08E8 is where the high plane starts.

Relocated to `uint16_t car_distance_16[24]` (`src/gen/revs_native.c`), `mem[]` kept as the
6502-ABI mirror. What the sites become:

| Routine | 6502 byte lanes | wide |
|---|---|---|
| `car_gap_tail` | two chained `SBC`s, borrow hand-carried between the lanes | `(int)dist[y] - (int)dist[x] - !carryIn` |
| `track_pos_advance` | `INC` low, `BNE`, `INC` high, then a two-lane compare against `lap_length` | `if (++dist == lap)` |
| `track_pos_retreat` | `DEC` low, borrow into high, sign-test the high lane, reload BOTH lanes from `lap_length` | one decrement and one assignment |
| `full_track_scan_rebuild` | `LDA lo / ORA hi / BNE` as the field-advance loop's exit test | `while (car_distance_16[0] != 0u)` |

⚠ **The marshal is PER ELEMENT, not per array.** The boundary shims here run once per car — up to
twenty times a frame — so `car_distance_marshal_in_one(x)` moves the one slot the index names.
Only the two shims that genuinely walk the whole field marshal the whole array: `check_car_pair`
(one 24-word round trip for a twenty-iteration pair walk) and `full_track_scan_rebuild` (one round
trip for hundreds of core-to-core word steps). Those two are where the win actually is; the
per-car shims roughly break even, and are converted because the campaign converts the traffic.

`full_track_scan_rebuild` also has to **publish before calling a nested boundary shim** —
`build_road_section()` marshals in, so the driver marshals out first or that shim reloads stale
mem[]. Same rule as `car_angle`'s.

### ⚠⚠ A FIXTURE'S RANDOM INDEX IS A DOMAIN CLAIM, and relocation makes it load-bearing

`mem[CAR_DISTANCE_LO + x]` is in range for **any** byte `x` — an out-of-domain index quietly read a
neighbouring array, in the twin *and* in the oracle, so the differential stayed green and nobody
noticed. `car_distance_16[x]` has exactly 24 entries, so the same index segfaults. Three fixtures
were seeding a car slot from unconstrained random memory: `track_pos_advance` and
`track_pos_retreat` took `player_car` ($6F, documented 0-19) straight out of `fill_random`, and
`stage_nearby_car` read `car_order[X]` with both the position and the entry random. All three are
pinned to their structural domains now, with the argument written at the fixture.

⇒ **A relocation converts a silent domain violation into a crash.** That is the relocation earning
its keep, not a fixture weakening — but expect to fix the fixture, and write down why the wider
domain is unreachable.

### ⚠⚠ THE HOLE: a relocated global is PROCESS state, so the two models can cover for each other

Sabotage S6 — drop `car_distance_marshal_in_one` from `stage_nearby_car`'s shim — **PASSED**.

The reason is not a fixture gap. Many transliterated oracles tail-call the **native** shim:
`stage_nearby_car__t6502` calls `car_gap_tail()`, which marshals. `diff_run` resets `mem[]` and
`cpu` between the two models but the relocated globals are plain process state, so whichever model
runs second inherits the value the first one marshalled — and a dropped marshal reads the right
word **by accident**. Exactly the class of `docs/validation-harness.md` §`cpu_unwind`.

⇒ `relocated_poison()` (`src/gen/revs_native.c`, called by `diff_run` before **each** model)
scribbles every relocated global with a non-zero, per-element-distinct pattern. With it in place S6
fails 5965/6000. **It found three genuine missing marshals in relocations that were already
"gated"**, none of which any differential could see before:

| Shim | missing | what it reads |
|---|---|---|
| `emit_edge_bearing_at_cursor` | `car_heading_marshal_in`, `bearing_marshal_in` | the heading every bearing is measured against |
| `road_edge_start`, `road_edge_walk`, `build_track_geometry` | `car_heading_marshal_in` | same, transitively through `emit_edge_bearing_core` |
| `place_player_in_section` | `edge_nearest_marshal_in` | `scale_angle_in_section_core` weights the angle by the running minimum |

On Silverstone every writer of those cells is native, which is why `determinism` was green too — a
transliterated or hook writer would have gone straight past the stale global. **Any future (B)
relocation must add its global to `relocated_poison`**, or its boundary sabotage cannot fail.

### Gate

`make validate` 136 fixtures, 0 mismatch (with the poison armed) · `determinism`,
`determinism-drive` and `determinism-crash` 64K byte-identical *including* $08D0-$08FF, so
`det_compare.py` needs no skip · `tracks` 6/6 byte-exact · `track-run` every circuit's code runs ·
`endian-lint` clean · Amiga link `muldiv-audit: clean`, `probe-audit: clean (132 symbols)`.

Six sabotages, six distinct first-diff signatures (the stale-object check), all FAIL:

| # | defect | caught by |
|---|---|---|
| S1 | `car_distance_marshal_in_one` swaps the two lanes | `track_pos_advance` |
| S2 | `car_distance_marshal_out_one` swaps the two lanes | `track_pos_advance` |
| S3 | `car_gap_tail_core` drops the incoming borrow | `car_gap_tail` |
| S4 | `track_pos_advance_core` does not zero the distance on a lap | `track_pos_advance` |
| S5 | `track_pos_retreat_core` steps back two units | `track_pos_retreat` |
| S6 | `stage_nearby_car`'s shim loses its `marshal_in` | `stage_nearby_car` (poison-dependent) |

## `MODEL_STATE` $62D0/$62E0 → `model_state_16[15]` — the biggest base, and the two new traps

The driving model's state vector: fifteen 16-bit elements held as a **plane-split SoA** (low bytes
$62D0..$62DE, high bytes $62E0..$62EE, element *i* at `+i` in each). Elements 0/1/2 are the body
angles and the frame's heading step, 3/4/5 their rates, 6/7 the axle loads, 8 the hand-integrated
accumulator, 9 the car's signed speed, $0A..$0D the per-axle slip cluster, 14 the per-frame
increment. **23 native functions, 84 references — the largest wide-value base in the engine**, and
it was parked behind five blockers that did not exist (see the scoring error above).

⚠ `MODEL_STATE_FRAC` ($62AE, 3 bytes) is a *separate* 24-bit fraction extension for elements 0..2
and **stays in `mem[]`**: a rate element is the top 16 bits of `(element << 8) | frac`, so
`integrate_state_rates_core` combines the relocated word with the mem[] byte into a `uint32_t`.

### What the conversion bought, per routine

| Routine | 6502 byte traffic before | after |
|---|---|---|
| `model_integrate_element_core` | two lane loads, `ADC`/`ADC`, two lane stores | `model_state_16[slot] + model_state_16[MS_INCREMENT]`, one store |
| `add_signed_into_element_core` | load/add/store per lane | one `+=` |
| `apply_angle_term_body` | source split into two lanes, product re-split on store | `(int16_t)model_state_16[source]`, one store |
| `stage_accum_delta_core` | heading step and accumulator each rebuilt from two lanes | two word reads, one word store |
| `apply_driving_model_core` | the speed split rebuilt `car_speed_hi:lo`, the accumulator restore re-split | `uint16_t speed = model_state_16[MS_SPEED]`, one store |
| `integrate_car_position_core` | doubling loop on two lanes | one word read per element |
| `integrate_state_rates_core` | rate rebuilt from two lanes, 24-bit sum re-split three ways | one word read + one byte, one word store + one byte |
| `slip_magnitude_core` | element rebuilt from two lanes | one word read |
| `store_slip_signed_core` | two lane stores | one word store |
| `clamp_slip_to_grip_core`, the zero-out arms, the crash reset loop | per-lane zero stores | one word store each; the reset is a 15-iteration word loop |

Where the 6502 genuinely handles ONE lane — a sign test or a magnitude off the high byte, or a
store that must leave the other lane alone because an early return can land between the two halves
(`apply_axle_slip`'s saturation bail-out: a slip leaves element 10's low lane **stale**) — the
named lane accessors `ms_lo`/`ms_hi`/`ms_set_lo`/`ms_set_hi` say so at the site instead of
open-coding a shift and a mask.

### Trap 1 — a whole-array publish can UNDO a write the routine really made

`dial_needle_angle` and `draw_dash_needles` read the vector to draw the needles, and they **plot**.
With a fixture-random plot pointer a needle line lands inside $62D0..$62EE; a whole-array
`marshal_out` then writes the entry values back over it. Both were caught by `make validate` (3 of
3000 cases) and are **import-only** — the general rule is that a whole-array publish belongs only
to a shim that actually writes the array, and never to one that plots.

### Trap 2 — the covering shim is not always the obvious one

The closure tool over `revs_native.c` + `revs_native_seam.c` missed four shims whose *transitive*
callee writes the vector, and the poisoned differential found every one:

| Shim | who writes the vector under it |
|---|---|
| `update_grip_limits` | `begin_spin_from_a_core`'s heading-step nudge ($4DD4 `SEC`/`ROR`) |
| `update_camera_and_drive_state` | the same core, via its spin arm ($45B9) |
| `begin_spin` / `begin_spin_from_a` | that nudge directly |
| `begin_scrape` | the crash tail's yaw kick ($1C0B) |

⭐ Note the shape: **every one of them was a shim that never mentions the vector**, reached through
a `_core` two or three levels down. Derive the marshal set from the transitive closure, then let
the poisoned harness prove it — a hand-read of the shim body sees nothing.

### Trap 3 — a fixture's random byte is a domain claim, again

`SLIP_OUT_INDEX` ($0078) *names a state element*, and its domain is structural: its only writers
are `derive_slip_reference` (axle + 2, so 2 or 3) and `clamp_slip_to_grip` (axle, so 0 or 1), every
store landing at element 10 + it. `fill_random` left it a free byte, which before the relocation
merely wrote **past** the vector into $63xx in *both* models — agreeing, so invisible for the life
of the fixture. With the vector a 15-entry `uint16_t` array the same case is out of bounds and the
harness **segfaulted inside `platform_mos_call`**, a corrupted vtable pointer three subsystems away
from the cause. Third instance of this: a relocation converts a silent domain violation into a
crash, which is the relocation doing the harness a favour.

### The gates it passed

`make validate` 136 fixtures, 0 mem mismatch, with `relocated_poison()` armed (`model_state_16` is
in it). `make determinism`, `make determinism-drive` and `make determinism-crash` all 64K
byte-identical including $62D0-$62EE — references re-recorded from a stashed HEAD build.
`make tracks` 6/6 byte-exact, `make track-run` every circuit's code runs, `make endian-lint` clean,
Amiga link `muldiv-audit: clean` + `probe-audit: clean (132 symbols)`.

Six sabotages, six distinct first-diff signatures, every one detected:

| # | Defect | Covering fixture |
|---|---|---|
| S1 | `update_grip_limits`'s shim loses its `marshal_in` | `update_grip_limits` (**not** `apply_driving_model` — the FOURTH standing explanation for a surviving sabotage, live again: the covering fixture was outside the `FN` filter) |
| S2 | `begin_scrape`'s shim loses its `marshal_out` | `check_crash` |
| S3 | `model_integrate_element_core` reads element 13 as the increment | `model_integrate_element` |
| S4 | `integrate_state_rates_core` drops the 24-bit fraction byte | `integrate_state_rates` |
| S5 | `slip_magnitude_core` byteswaps the element | `slip_magnitude` |
| S6 | `ms_set_lo` clobbers the high lane | `begin_spin` |

## ⭐⭐ THE METRIC THAT DECIDES A (B) RELOCATION IS **REFS PER ELEMENT**, not ref count

`SECTION_COORD` ($0900/$0A00) is the largest remaining base by references — 10 functions, 43 refs —
and it was next in the planned order. **Measured, it is a ~15× net LOSS**, and the measurement is
what produced the metric.

A whole-array marshal costs `2N` byte reads plus `N` word writes on entry and the same on exit, at
**every shim in the closure**. What it buys is the byte-lane arithmetic removed at each *reference*.
So the payoff ratio is

> **refs per element**, weighted by how often each shim is entered

and the ref count on its own says nothing: a 256-entry table with 43 refs pays a 512-byte marshal to
save 43 lane pairs. Direct instrumentation of a driving frame (`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`)
put it at **3.7 shim entries per frame against 98 `view_delta` reads per frame** — the marshal moves
~1900 bytes to save ~98 lane pairs.

| Base | elements | refs | refs/element | verdict |
|---|---|---|---|---|
| `MODEL_STATE` $62D0/$62E0 | 15 | 84 | **5.6** | ✅ done |
| `VIEW_ORIGIN` $6280/$6283 | 6 | 14 | **4.7** (2.3 counting the 9 array slots) | ✅ done |
| `MARKER_OFF` $62B7/$62BA | 3 | 8 | **2.7** | ✅ eligible — next |
| `CAR_DISTANCE` $08D0/$08E8 | 24 | 30 | 1.3 | ✅ done, but **only because it marshals ONE SLOT** (`car_distance_marshal_in_one`) — a per-slot marshal is not scored by this table |
| `OBJECT_BEARING` $0380/$0398 | 24 | 10 | **0.42** | ❌ declined |
| `EDGE_OPP_X` $5E50/$5EA0 | 80 | 19 | **0.24** | ❌ declined |
| `SECTION_COORD` $0900/$0A00 | 256 | 43 | **0.17** | ❌ declined — measured, see above |

⭐ **A large TABLE has no cheap marshal boundary, because its readers index it arbitrarily** — there
is no window narrower than the whole array that a shim can be shown to touch. The three declined
bases hold **72 of the remaining 94 refs**, so mechanism (B) as practised cannot finish the tier and
the tables need a different mechanism. The options, none yet chosen:

1. **array-is-home, `mem[]` a write-through mirror** — no marshal at all, but `make validate`'s
   oracle writes `mem[]` directly, so the gate becomes `determinism` rather than `validate`;
2. **mechanism (A) at each site** — keep the bytes in `mem[]` and express every reference as one
   wide read/write through a named accessor. Removes the lane arithmetic at **zero** marshal cost
   and keeps every gate; does not create the adjacency, so the win is smaller per site;
3. **narrow per-shim windows** — fails on the indexing, as above.

**This is an architecture/fidelity decision and belongs to the user, not to the campaign.**

## `VIEW_ORIGIN` $6280/$6283 → `view_origin_16[9]` — the camera, and an extent that is not 3

The view origin is what every bearing and every projection in the frame is measured from, and there
are **two** of them six bytes apart: origin 0 is the camera, origin 6 the road sign's own viewpoint
(`build_sign_origin` derives it). Each holds three 16-bit components.

⚠ **The extent is $6280..$628B — six elements, not three.** The stride-6 second origin is part of the
base, and scoring only $6280..$6282 would have missed `build_sign_origin` and `build_road_sign`
entirely. Third instance of *an extent is a claim, and a scan's WINDOW is the measurement*.

**Indexed by the 6502's own byte offset.** `view_origin_16[origin + component]` with `origin` ∈ {0, 6}
is exactly what the addressing modes write, so every converted site kept its index expression
verbatim and nothing had to be remapped. The price is that elements 3..5 are unused and
**unmarshalled, and must stay that way**: their "low" bytes $6283..$6285 *are* elements 0..2's high
bytes, so marshalling them would alias two values onto the same cells. Three wasted words buy a
mechanical substitution.

⚠ `view_origin_frac` ($62B1) stays in `mem[]`. Components 0 and 2 of origin 0 carry a further 8-bit
fraction there, so a camera component is the top 16 bits of `(element << 8) | frac`, and
`integrate_car_position` advances it at 24-bit precision.

### What it removed

| Routine | before | after |
|---|---|---|
| `view_delta` | two byte loads + a shift + an OR per call, 98 calls a driving frame | one word load |
| `integrate_car_position_core` | three-lane 24-bit add spelled as lane/carry steps | `uint32_t` add, one word store + the fraction byte |
| `update_camera_and_drive_state_core` | a four-term add with **three PHP-saved carries** folded into the high byte | one truncating 16-bit `+` of four values |
| `build_sign_origin_core` | camera component read as two lanes, difference stored as two | one word read, one word store |
| `build_player_car_core` | component-wise lane copy of the whole world coordinate | word store per axis (the source is still `SECTION_COORD`, which is not relocated) |

The three-PHP chain at $45E1-$45FB is the clearest case in the campaign: the 6502 adds three terms
into the low byte, saves each carry with a `PHP`, folds all three into the high byte and then
**discards** the high byte's own carry-outs with the `PLP`s. That is one truncating 16-bit add, and
D = 0 on this path (`docs/static-map.md` §Decimal mode).

### The fixture domain it narrowed

`bearing_to_section_from` and `project_point_from` drew a fully random origin one case in eight. In
`mem[]` that merely read past the twelve bytes into $63xx **in both models**, so it compared equal;
with the array relocated the same case is out of bounds. All eleven production call sites pass
either 0 or `VIEW_ORIGIN_STRIDE`, so the arm was dropped and the draw kept (`(void)xs();`) to hold
the shared PRNG stream aligned. Third instance of *a fixture's random argument is a domain claim*,
and a relocation is what converts a silent domain violation into a crash.

### Sabotages (six, six distinct signatures, all FAIL)

| # | defect | signature |
|---|---|---|
| S1 | `marshal_in` swaps the lanes | 7 fixtures, `bearing_to_section_from` 4000/4000 |
| S2 | `marshal_out` publishes 5 slots of 6 | only the two sign fixtures: `build_sign_origin` 1010/3000 |
| S3 | `road_edge_start`'s import dropped | `build_track_geometry` 399/400 + `road_edge_start` 165/200 |
| S4 | the sign origin written to the camera slot | `build_sign_origin` 3000/3000 alone |
| S5 | the 24-bit split shifts by 7 | `integrate_car_position` 2000/2000 alone |
| S6 | `view_delta` ignores the component | the two projection fixtures only |

## ⭐⭐ DERIVE THE MARSHAL CLOSURE FROM `objdump -dr`, NEVER FROM READING THE C

Two campaigns in a row a hand- or regex-derived closure was wrong, and a missing marshal is a silent
wrong answer that only a poisoned differential catches:

- **MODEL_STATE**: four shims that never mention the vector needed marshals because a `_core` two or
  three levels down nudged it (all via `begin_spin_from_a_core`).
- **VIEW_ORIGIN**: a source regex matched function names inside **comments** and invented edges; the
  hand-checked list it produced then omitted three real shims (`build_track_geometry`,
  `road_edge_start`, `road_edge_walk`) and `make validate` failed with
  `road_edge_start 165/200 mismatch`.

`tools/native_closure.py` answers it from the compiler's own relocations on the built objects — it
cannot invent an edge that is not a call, nor miss one that is.

⭐ **Cut the closure at shim boundaries.** A shim that reaches the global only by calling *another*
shim is already bracketed, because the inner shim marshals for itself. Without that cut the tool
accuses `process_car_contact` and `race_main_loop` of missing `model_state` marshals; both reach it
through `begin_scrape()`, the shim, which `process_car_contact` tail-calls at $1C18.

⚠ **Blind spot:** it sees symbol references, so it cannot see a write that reaches the mirrored
`mem[]` range through a **plot pointer** rather than a symbol.

### `tools/marshal_audit.py` — the whole campaign, checked in one command

`python3 tools/marshal_audit.py` runs that closure for **every** relocated base and diffs it against
the marshal calls actually present in each shim's body (read textually, because within a translation
unit the compiler may inline a marshal and leave no relocation). It found one real gap in committed
code:

> **`race_main_loop` imported `view_origin` and nothing else.** Its core calls a good many `_core`
> functions **directly** — `read_driving_controls_core`, `check_crash`'s and `mirrors_update`'s
> bodies, `draw_track_object_core` — so those calls cross no 6502-ABI shim and pick up no marshal-in.
> Everything the engine set up in `mem[]` before the race would be read from a zero-initialised
> global on the first frame. Fixed by importing `model_state`, `car_angle`, `car_heading` and
> `edge_nearest` at the driver's entry; once per race, so the cost is nothing.

The remaining rows it prints are `produces` — a shim that writes and publishes a **scalar** it never
reads (`build_player_car`/`car_heading`, the three `hypot_max`/`hypot_min`/`bearing` producers). That
pattern is legitimate exactly because there is no partially-written array to import first.
