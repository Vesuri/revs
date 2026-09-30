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
