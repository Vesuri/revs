# Misnamed functions — the rename backlog

**Convention:** whenever you encounter a function whose name clearly contradicts what it does,
**append a row here immediately.**  Do not rename piecemeal in generated files —
`disasm/symbols.csv` is the source of truth, and a batch rename via the transpiler is cheap.

Why immediately, and why this file exists before there is anything in it: on the Atari port this
backlog accumulated because renames were deferred, and **on a binary-only project the function
names are your map** — every wrong name taxes every later reasoning step (postmortem #1.2).  The
counter-measure is one concentrated naming pass up front (`docs/phases.md` Phase 2.4) plus this
file for everything found afterwards.

⭐⭐ **There is a DEADLINE on this backlog, and it is Phase 6 item 0c** (`docs/phases.md`): the queued
renames — routines *and* `mem[]` cells — must be resolved and batched through `symbols.csv` **before
the first native twin is written**, because a twin is hand-written and `make gen` cannot re-rename it.
Renaming is cheap right up to that moment and stops being cheap immediately after.

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

⭐ **`$7C00`/`$7D56` confirmed from a running machine (2026-08-14), plus the detail that mattered.**
`make refloop --fill=81-101` attributes every horizon-band frame-buffer write on a real BBC, and it
lands squarely on these chains — PCs marching at the 17-byte stride from `$7C11`, 315 writes each
over 15 frames, i.e. one element per screen CELL covering all 40 cells of every line. So the
"unrolled column chain" reading above is right, and `lap_time_readout` really is just the nearest
preceding symbol.

The detail the names do not carry, and the one that cost a visible artefact: **`A` is the live pixel
value threaded through the entire chain.** Each element is
`LDY src / BEQ skip / LDA #0 / STA src / LDA $6000,Y / skip: LDY #cell*8 / STA ($70),Y` — a zero
column source means "same as the previous cell", so the `BEQ` deliberately leaves `A` alone. Corrupt
`A` mid-chain and every later element with a zero source stores the corrupt byte, giving a run to the
RIGHT EDGE of that display line. That is what the port's horizon stripes were: `irq1v_handler`
returning with `A` = 0 because nothing wrote `$FC`. Full write-up: `docs/bbc-reference-loop.md`
§What it found.

## `FUN_52a4` (`$4EF5`'s callee, the band-4 arm) — it DRAWS, and the name says nothing about it

Called as the last arm of the IRQ1V band cycle, i.e. "the 50 Hz game body".  Measured on the Amiga
target (`make ISRWATCH=1`, 2223 fields): per field it writes **`$6200-$62FF`** (live variables in
the sky region) and **`$6E00-$70FF`, which is FRAME BUFFER — display lines 120-143, the road just
below the horizon**.  It writes nothing else: no engine code, no column sources, no `$7B00` overlay,
and exactly one zero-page byte (`$FC`, and that one is the port's own ISR shim, not the game).

That it draws at all is the fact that mattered: it makes the body and the main-loop rasteriser two
writers of the same screen rows, which is why the port had to stop running the body inside the VERTB
ISR (`docs/amiga-arch.md`).  Suggested name: something that says both halves, e.g.
`sim_tick_and_plot_horizon` — but the *what* it plots there is not yet identified, so this is
recorded as behaviour rather than as a rename.

## `FUN_5204` (`$5204`) — it is the DIAL/LINE PLOTTER, and its direction is SELF-MODIFYING

Found while fixing the rev counter (2026-08-16).  `$51A8` turns the engine revs `$003C` into an
angle and `$5204` draws the needle: a Bresenham line whose **major- and minor-axis step opcodes are
patched into itself** from two 8-byte octant tables (`$3B86` -> `$5220`, `$3B8E` -> `$529B`, indexed
by the octant `$0076`).  Names worth having, none of them in `symbols.csv` yet:

| Addr | Suggested | Evidence |
|---|---|---|
| `$51A8` | `dial_needle_angle` | reads `$3C`, clamps to `$1E`, `revs * 0.75`, rotates by `$4C`, decomposes mod `$98` into quadrant (`/$26`) + offset folded about `$13` |
| `$5204` | `plot_line_octant` | the two SMC step slots + `($70),Y` frame-buffer write; `$76` is its octant |
| `$3B86` | `octant_major_step_tbl` | `INX DEY INY INX DEX INY DEY DEX` |
| `$3B8E` | `octant_minor_step_tbl` | `DEY INX INX INY INY DEX DEX DEY` |
| `$0076` | `plot_octant` | `(quadrant << 1) | mirror`, built at `$51DB-$51E1` |
| `$003C` | `engine_revs` | already called "the rev counter" in the refloop's DASH table; range `$00`, idle `$28`-`$2C`, clamped max `$AA` |
| `$005A` | `engine_revs_prev` | the over-rev limiter's decay source (`$4A2C`, −2/call above `$6C`) |

⚠ `$5204` has callers other than the needle — anything that draws a straight line goes through it,
so the name should not say "needle".

## ⭐⭐ `$7BE2 dashboard_sweep` IS THE 3D VIEW RASTERISER, not the dashboard (2026-08-16)

Measured on a **real BBC** with a moving car (`make fbwrites` — every frame-buffer store attributed
to the PC that made it, `tools/bbc_refloop_race.mjs --fill=all`). The routine this project has
called "the dashboard sweep" since Phase 4, and which is its largest main-loop cost (131 ms, 24%),
writes **display lines 80..157** — and its store distribution is not a dashboard's:

| 8-line bucket (display line) | 80 | 88 | 96 | 104 | 112 | 120 | 128 | 136 | 144 | 152 |
|---|---|---|---|---|---|---|---|---|---|---|
| stores/frame, both chains | 280 | **320** | **320** | **320** | 284 | 212 | 178 | 98 | 60 | 26 |

320 is a **full bucket** — 8 lines × 40 cells. So lines 88..111 are repainted **edge to edge every
frame**, and below that the count tapers as each column's chain stops at its own row. That is a
horizon-down, column-major **scene rasteriser with a per-column silhouette**, and the taper is the
cockpit's upper edge cutting the columns off at different heights. The palette band called "the
dashboard" does not start until line 166, and **nothing writes lines 166..207 during driving at
all** except `vdu_char_def` (the digits) and four bytes from `engine_main`.

| Addr | Current | Actually | Suggested |
|---|---|---|---|
| `$7BE2` | `dashboard_sweep` | paints the 3D viewport, lines 80..157, 889 stores/frame | `view_paint_columns` |
| `$7D13` | `dashboard_sweep_phase2` | the same for the columns whose chain stops early, 1209 stores/frame | `view_paint_columns_clipped` |
| `$7F18` | `dashboard_sweep_phase3` | lines 133..157 only, 50 stores/frame — the bottom of the silhouette | `view_paint_columns_short` |
| `$7BBF` | `dashboard_sweep_restore` | unchanged in meaning | `view_paint_restore` |

⚠ **What the misnomer cost.** `docs/direct-bitplane-plan.md` §7a read this routine's shape as "a
dashboard of mostly-static instruments redrawn wholesale" and sized the **hardware-sprite** item
(§8) off it; §7b then concluded "the main loop barely draws". Both were reasoning about the wrong
subject. The name most likely comes from the routine living in the `$7B00` overlay, which also
carries the wing-mirror code — the *location* is dashboard-ish, the *job* is the view.
