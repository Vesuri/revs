# Native engine maintenance

The native conversion and function-by-function review are complete. The former
CPU, helper and read-through campaign ledgers have been retired; their changes
remain in Git history. Use `make cpu-lint` and `make fatscan` to inspect the current
source rather than relying on old line numbers or counts.

## CPU and ABI boundaries

`src/gen/revs_native.c` contains typed computations. `revs_native_seam.c` holds
live 6502 ABI boundaries; `revs_native_abi.c` holds shims used only by the oracle.
`revs_native_seam.h` declares their shared types and interfaces. These files are
hand-maintained. The one-time seam extraction must not be repeated.

A core takes arguments and returns values or a result struct. A shim restores
registers and flags that its actual 6502 callers read. Moving the shim does not
make those outputs dead. Trace callers transitively, including expansion hooks
and the native main loop's remaining shim calls.

MOS wrappers carry `MosRegs` through the OS boundary. IRQ entry must preserve the
MOS register contract, including the interrupted foreground state. The target
asserts this with `g_irqClobberCount`.

Some CPU references are intentional: a stack pointer used as an address, a decimal
mode clear, or a documented hook boundary. The executable allowlist and stale-row
check in `tools/cpu_lint.py` are the authoritative inventory. Do not make grep
counts the objective or move computational bodies into an ABI file to hide them.

## Flag helpers and live masks

Reconstruct only an escaping flag at the boundary. An escape argument must identify
its reader, including paths through hooks; reaching an RTS or appearing in an old
fixture mask is insufficient. A plain-C core can still require exact exit flags in
its oracle-facing shim.

Narrowing a fixture's live mask changes what the test checks. Settle the caller
contract first and write the argument at the fixture. Dead stack residue can be
ignored only within that proven scope. Keep hardware writes, MOS call sequences,
SMC traps and meaningful stack values observable.

The view and near-slot live masks were narrowed after their callers were audited.
This does not license blanket register exemptions for other routines. BCD fixtures
must use reachable packed-decimal inputs, and 68000 `ROR`/`ROL` do not set X for
`ABCD`/`SBCD`; see [68000 optimisation](m68k-optimisation.md).

## Memory and call paths

Use `disasm/symbols.csv` names and typed locals. Shared scratch addresses are not
owned by the function currently being read. The geometry, plotters and circuit
hooks reuse zero-page cells; inspect every tenant before relocating one.

Direct native calls should bypass ABI shims only when their register, flag and
marshalling responsibilities are understood. Use the linked call graph for a
transitive audit. `tools/native_closure.py` and `tools/marshal_audit.py` support it.
See [wide values](wide-value-cleanup.md) for the memory boundary rules.

The screen plotters can overlap engine data and even zero page. Preserve wrap and
alias guards where the actual pointer domain requires them. A constant RAM target
needs no hardware dispatch, while a genuinely variable destination still does.
Do not move a cold fallback into a new call without measuring its effect on the
surrounding loop's memory traffic.

## Cell naming contracts

`disasm/symbols.csv` owns cell names; a name describes a value only within its
documented lifetime. A second reader is not evidence of a second tenancy.
Conversely, arithmetic scratch and overlapping arrays must not acquire a global
name from just one caller.

The cell audit resolves the five remaining provisional cell rows and the
misclassified `$0003` scratch byte:

| Address | Name | Contract |
| --- | --- | --- |
| `$0003` | `player_order_slot` | Cached position of `player_car` in `car_order`, refreshed by `find_player_neighbours`; may be stale during swaps. |
| `$0012` | `edge_cursor` | Output point cursor during a road-side walk, exclusive end index afterward. |
| `$001F` | `horizon_extent` | Maximum road scan line, with stale-horizon and viewport caps; also incremented before crash repaint. |
| `$0051` | `horizon_index` | Point supplying the maximum; folded into the first 40-point half only at the geometry tail. |
| `$62FC` | `horizon_width_threshold` | Width threshold used to clip distant objects at the horizon. Silverstone halves the magnitude; expansion circuits use their own scale. |
| `$7000` | `span_store_default_base` | Static operand placeholder, replaced with a surface-edge buffer address before road-span drawing. It is not a column table or the framebuffer base. |

The runtime listing's 7,098 instruction byte sequences were checked against
`revs_runtime.bin`. `$0003` has one direct writer, `$63AE`, and seven direct
reads: `$1841`, `$18B4`, `$2655`, `$2692`, `$279C`, `$63B5`, `$66DF`.
All consume the cached player order position. The indexed reset at `$1809`
also clears it. The dashboard overlay and generated expansion hooks contain
no reference to this cell. BBC competition drive-in checks on Silverstone
and Brands (`--frames=120 --competition --drive --watch=0003`, track 5 and 1)
recorded only `$63AE` writes after frame 40 (143 and 126), plus one reset clear
each. These windows corroborate the writer audit; they do not establish
coverage of every race branch.

The remaining neutral scratch names are intentional, with distinct producers:

| Cell | Examples of unrelated uses |
| --- | --- |
| `shared_counter_42` | Geometry point counter (`$2360/$23D6`), object slot (`$2A30/$2A39`), standings mode (`$65D6`). |
| `shared_temp_76` | Sort index (`$0F69`), projection normalisation (`$22D0`), previous camera pitch (`$4534`). |
| `shared_temp_77` | Saved section index (`$240E`), sign-origin component index (`$4CBE`), text field width (`$6304`). |
| `shared_temp_7e` | Bearing lookup result (`$21DF/$2257`), object coordinate (`$20DB`), span endpoint (`$2B6F`). |
| `shared_temp_84` | Step-delta component (`$1446`), projected height magnitude (`$22AE`), span delta (`$2B89`); also part of `point_delta_hi`. |
| `shared_temp_85` | Step-delta component (`$1448`), car across-position (`$2947`), span column cursor (`$2C7B`); also part of `point_delta_hi`. |
| `shared_temp_8c` | Road style (`$1A4C/$1A76`) and deferred object pixel mask (`$1D2F/$1D48`). |

Keep the established scoped aliases and tenancy notes for `math_*`, `hypot_*`,
`point_delta_*`, `point_dist_*` and the plot pointers. They describe specific
producer/consumer windows, not exclusive ownership of those addresses.
The remaining provisional routine descriptions are separate from cell naming;
an old provisional name mentioned in a historical note is not an unresolved
cell row.

## Verification

- Comments and documentation: check references and formatting.
- Native computations or ABI calls: validate the affected routines and determinism
  trajectories. Shared CPU or generator changes also require a pre-change baseline.
- Expansion hook paths: compare the affected circuit against the real BBC.
- Amiga assembly: run its in-process C differential with nonzero coverage.
- New or narrowed fixtures: establish that deliberate defects fail and that the
  inputs represent the game. Reset independent relocated state before both sides.

See [the validation harness](validation-harness.md),
[the fidelity boundary](faithfulness-seam.md) and [open work](open-work.md).
