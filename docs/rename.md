# Misnamed functions — the rename backlog

**Convention:** whenever you encounter a function whose name clearly contradicts what it does,
**append a row here immediately.**  Do not rename piecemeal in generated files —
`disasm/symbols.csv` is the source of truth, and a batch rename via the transpiler is cheap.

Why immediately, and why this file exists before there is anything in it: on the Atari port this
backlog accumulated because renames were deferred, and **on a binary-only project the function
names are your map** — every wrong name taxes every later reasoning step (postmortem #1.2).  The
counter-measure is one concentrated naming pass up front (`docs/phases.md` Phase 2.4) plus this
file for everything found afterwards.

✅ **`$24F6` — DONE 2026-08-13.  It is `build_road_edge_lists`**, the frame's road-geometry
projection pass: it turns the track ahead into the two 40-point edge lists (`edge_x_lo/hi`,
`edge_y`) that `interp_edge` and the road rasteriser `$1A20` consume.  Evidence, names for its
whole subtree (`road_edge_start` `$22FF`, `road_edge_walk` `$23D2`, `project_point` `$2285`,
`road_edge_side` `$254A`) and for the state it produces (`horizon_extent` `$1F`, `horizon_index`
`$51`, `edge_cursor` `$12`, `horizon_half_width` `$62FC`) are in `disasm/symbols.csv`.
⭐ The payoff was not the name but the *adjacency*: phases 5 and 11 are the same subsystem —
**build then draw, 40% of the frame between them** — so the #3 cost was never an independent
third optimisation target.  `docs/perf-method.md` §Where the time goes.

⭐ **Next up: `$1C1C`, currently `project_geometry`.**  That name now looks wrong, and it is in the
way: `$2285` (`project_point`) is the engine's actual perspective divide, and `$1C1C` reads
`colour_pattern_tbl`, which a projection has no business touching.  Suspect a pixel-pattern /
column-shading computation.  Worth resolving before the name is leaned on, because two routines
called "project…" doing different things is exactly the tax this file exists to prevent.

| Addr | Current name | What it actually does | Suggested name |
|---|---|---|---|
| `$7BBF` | *(none — absorbed into `lap_time_readout`, `$7B9C`)* | Nothing to do with lap times. It is the `$7B00` overlay's **restore routine**: puts `STA` (`$91`) back over the three `STA (zp),Y` slots the column sweep planted `RTS` on (addresses recovered from `$7D24`/`$7F24`/`$7F7D`) and `CPX` (`$E0`) back at `$7EEE`. Reached only by `JMP $7BBF` from `$7FB3`, i.e. it is the sweep's epilogue; it merely follows `$7B9C`'s `RTS` at `$7BBE` inside the same listing bracket. `docs/static-map.md` §Open items 10 | `dash_sweep_unpatch` |
| `$7C00` | `FUN_7c00` | Not a routine — the first of **two fully unrolled 16- and 24-unit column chains** (`$7C00-$7CFF`, `$7D56-$7EDD`), one 17-byte unit per dashboard/mirror column, entered at a computed offset and terminated by a planted `RTS`. Ghidra's function bracket is an artefact of the JSR targets, not a body | `dash_column_chain_a` |
| `$7D56` | *(absorbed into `FUN_7c00`)* | The second chain (24 units, tables `$3800…$4380`) | `dash_column_chain_b` |
| `$7E00` | `FUN_7e00` | Unit 10 of chain B, not a function start — it is a `JSR` target only because the computed call's fixed high operand byte is `$7E` | *(fold into the chain)* |
| `$7EF3` | `FUN_7ef3` | Advances the destination row pointers `$70/$71`,`$72/$73` and re-enters chain A at `$7BF7`; never returns to its caller by falling out, only through a planted `RTS` in a chain | `dash_next_row` |
