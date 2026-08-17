# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

---

## 1. `view_paint_lines`' boundary tables are [PROVISIONAL]

`view_edge_mask_a/fill_a`, `view_edge_mask_b/fill_b`, `view_bnd_a_mask/fill`,
`view_bnd_b_mask/fill`, `view_bnd_a_src/b_src`, `view_edge_index_tbl`, `view_cell_bytes` (`$6000`),
plus the three per-line offset tables `$3050` / `$30D0` / `$3150`.  They are named after the
operator (mask, fill, index), not after what they mean.

What the machinery does is now settled, and a name should come out of it: per scan line the driver
plants an `RTS` **inside** the unrolled cell chain to stop it (`$3150,X` patched into the
`STA $7Cxx` at `$7D2E`, restored by `view_paint_restore`), enters the chain part-way through by
patching the `JSR` operand at `$7F68` with `$F1 - $3080,X`, and then composes the one PARTIAL cell
at each end of the run with an AND/ORA pair (`$38D0`/`$3350` for chain A, `$3950`/`$33D0` for
chain B).  So the mask/fill pairs are the sub-byte EDGE of the run, and the offset tables are its
start and stop — i.e. the tables describe **where the road's silhouette cuts each line**.

⚠ Do not name them until this is answered: `$3080` is simultaneously **cell column 1's source
area** (`view_src_blocks + $80`, which the chain ZEROES as it consumes) and the table the driver
reads at `$7F54` for the entry offset — and `$3050`/`$30D0`/`$3150` sit in the same block address
space.  Either the reuse is deliberate (the read order is: entry offset first at `$7F54`, chain,
then `LDY $3080,X` again at `$7F72` — which by then reads the zero the chain just stored), or one of
the two readings is wrong.  Settle that before the vocabulary, because a wrong name here would be
copied into every Phase 6 note.

## 2. `$62A2` / `$62A5` — the 16-bit angle behind the steering-wheel mark, unnamed

`draw_dash_needles` plots its second mark at this angle, and the view pipeline reads the same pair
(`$1F19`, `$1F1F`, `$1F2D`, `$1F8A-$1FA4`).  `$1612` builds it: `$62A5` is the magnitude, clamped
to `$91` with a `#$C8` wrap test, `$62A2` carries a sign in bit 0 and a low-order bit in bit 7
(`$513A` recombines them with `ASL $74 / ROL A`).  `$0D42`/`$0D7C` write `$62A3,X`, so it sits in a
short array, and `$4885` reads it back.  Suggested `steer_angle` / `steer_angle_flags`, but confirm
against `adc_read`'s path (`$1646`) first — the alternative reading is a heading relative to the
road, which is what the view pipeline would want.

## 3. ⭐⭐ `race_main_loop`'s BODY — 17 of the 24 per-frame calls have no name

Twin #3 made the main loop idiomatic C and the calls now read as `FUN_5052()`, `FUN_7b4a()`,
`FUN_1579()`, `FUN_46a1()`, `FUN_4626()`, `FUN_24b9()`, `FUN_0ffe()`, `FUN_1a20()`, `FUN_4ca4()`,
`FUN_2ad1()`, `FUN_1b12()`, `FUN_2637()`, `FUN_1e15()`, `FUN_4f44()`, `FUN_1bb9()`, `FUN_111e()`
— plus `FUN_1805()`, `FUN_11ce()`, `FUN_0b77()` in the session reset and `FUN_17fc()`, `FUN_1163()`
in the session end.  **These are the engine's twenty top-level subsystems.**  Naming them is the
single highest-value naming pass left on the project, and it is now cheap: the body is a FLAT
sequence, so slot *n* of our list is subsystem *n* of anyone else's.

What is already established, from behaviour and from the surrounding code:

| slot | addr | evidence in hand |
|---|---|---|
| 1 | `$5052` | the race clock / time processing (`clear_race_clock` is `$5011` next door) |
| 2 | `$7B4A` | in the `$7B00` overlay; the starting-light sequence |
| 3 | `$1579` | reads the driving keys — `$163B` is its steering read, gated on `session_end_countdown` |
| 4 | `$46A1` | the driving model; `$46CD` inside it is what writes `wheel_spin_rate` |
| 5 | `$24F6` | ⚠ **currently named `build_road_edge_lists` and that may be one slot off** — see below |
| 6 | `$4626` | moves the player along the track |
| 7 | `$24B9` | moves the player between track segments |
| 8 | `$0FFE` | the lap/session timers: `$106F` compares `qualify_minutes` and arms `session_end_countdown` |
| 11 | `$1A20` | draws the road into the `$3000` source blocks (the producer half of the view pipeline) |
| 14 | `$4CA4` | builds a road sign |
| 15 | `$2AD1` | draws a car or a sign; entered with X = `$17` = 23, the object-slot count |
| 16 | `$1B12` | draws the corner markers |
| 17 | `$2637` | moves and draws the other cars — returns immediately on the practice branch |
| 18 | `$1E15` | copies the tyre/dash edges |
| 21 | `$4F44` | moves the horizon: it is what writes `band1_duration`, which `irq1v_band_schedule` splits |
| 22 | `$1BB9` | contact/collision processing |
| 23 | `$111E` | the crash check: `$1138` is the arm that sets `crash_flag` |
|  — | `$1805` | zeroes `$00-$68` and `$6280-$62FF`: the per-lap reset |
|  — | `$11CE` | rebuilds the player's car and the driver tables from `player_car` |
|  — | `$0B77` | scales the wing settings from `$5F3D,X` |
|  — | `$17FC` | prints one message through `$4D70`/`$4D74`; X selects the token |
|  — | `$1163` | races the remaining drivers to the finish so the results table is complete |

Do the pass in one sitting, deriving each name from its own body (the table above is where to
start, not what to write), and cross-check the slot alignment against
<https://revs.bbcelite.com/deep_dives/program_flow_of_the_main_game_loop.html>, whose account of
the same 24-call body lines up one-for-one with ours — including all five slots we had already
named independently (`engine_sound_update`, `clear_surface_buffers`, `fill_line_surface`,
`mirrors_update`, `view_paint_lines`), which is what makes the alignment trustworthy.

⚠ **And settle `$24F6` while you are there.**  Our `build_road_edge_lists` says "turns the track
ahead into the two 40-point edge lists"; the reference has slot 5 getting the track section and the
corner markers, with the drawing at slot 11 (`$1A20`).  Both readings fit `docs/perf-method.md`'s
"the first two PRODUCE source bytes", so this is a name that may be describing slot 11's job while
sitting on slot 5's address.  It is `[DERIVED]`, it is referenced from four docs, and a wrong name
here propagates into every Phase 6 note.
