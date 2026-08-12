# Misnamed functions — the rename backlog

**Convention:** whenever you encounter a function whose name clearly contradicts what it does,
**append a row here immediately.**  Do not rename piecemeal in generated files —
`disasm/symbols.csv` is the source of truth, and a batch rename via the transpiler is cheap.

Why immediately, and why this file exists before there is anything in it: on the Atari port this
backlog accumulated because renames were deferred, and **on a binary-only project the function
names are your map** — every wrong name taxes every later reasoning step (postmortem #1.2).  The
counter-measure is one concentrated naming pass up front (`docs/phases.md` Phase 2.4) plus this
file for everything found afterwards.

| Addr | Current name | What it actually does | Suggested name |
|---|---|---|---|
| `$7BBF` | *(none — absorbed into `lap_time_readout`, `$7B9C`)* | Nothing to do with lap times. It is the `$7B00` overlay's **restore routine**: puts `STA` (`$91`) back over the three `STA (zp),Y` slots the column sweep planted `RTS` on (addresses recovered from `$7D24`/`$7F24`/`$7F7D`) and `CPX` (`$E0`) back at `$7EEE`. Reached only by `JMP $7BBF` from `$7FB3`, i.e. it is the sweep's epilogue; it merely follows `$7B9C`'s `RTS` at `$7BBE` inside the same listing bracket. `docs/static-map.md` §Open items 10 | `dash_sweep_unpatch` |
| `$7C00` | `FUN_7c00` | Not a routine — the first of **two fully unrolled 16- and 24-unit column chains** (`$7C00-$7CFF`, `$7D56-$7EDD`), one 17-byte unit per dashboard/mirror column, entered at a computed offset and terminated by a planted `RTS`. Ghidra's function bracket is an artefact of the JSR targets, not a body | `dash_column_chain_a` |
| `$7D56` | *(absorbed into `FUN_7c00`)* | The second chain (24 units, tables `$3800…$4380`) | `dash_column_chain_b` |
| `$7E00` | `FUN_7e00` | Unit 10 of chain B, not a function start — it is a `JSR` target only because the computed call's fixed high operand byte is `$7E` | *(fold into the chain)* |
| `$7EF3` | `FUN_7ef3` | Advances the destination row pointers `$70/$71`,`$72/$73` and re-enters chain A at `$7BF7`; never returns to its caller by falling out, only through a planted `RTS` in a chain | `dash_next_row` |
