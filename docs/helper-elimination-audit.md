# 6502-math-helper elimination — the site-by-site audit

The standing campaign: rewrite every `revs_native.c` twin that calls a 6502 flag-helper
(`adc_step`, `sub_from`, `adc_value`, `sbc_step`, `adc_overflow`, `sbc_value`, `sbc_overflow`,
`lsr_a`, `ror_a`) as plain C — **except** where a flag genuinely escapes the routine. This doc
records the escape-vs-dead verdict for every remaining call-site so the judgment is made **once**.

**The rule** (from `faithfulness-seam.md` §Writing one, and CLAUDE.md): a 6502 math macro writes
five `cpu` fields; a routine usually reads one. Keep the helper only where a produced flag (C/V/N/Z)
is read by a caller, pushed by a `PHP` that the differential compares, or rolled by a following
`ROR`/branch into the real output. Everywhere else the flags are dead — on the render/geometry path
D=0 (`static-map.md` §Decimal mode), so an `adc`/`sbc` byte chain is just a binary `+`/`-`.

Audited 2026-08-23 at HEAD `c5f9b73` (60 sites, 31 functions). **16 KEEP, 44 CONVERT.**

---

## Part 1 — KEEP: the flag genuinely escapes (16 sites) — PERMANENT LEDGER, do not re-litigate

These were confirmed by this audit and, for the render path, by the earlier `0c67002` / `f643922`
audits. **Do not convert them** — the argument is written at each site in the source.

| function | line(s) | helper | why it stays |
|---|---|---|---|
| `step_scanline` | 733, 735, 736 | adc_step | 16-bit `plot_ptr` carry chain; 736's carry → `*carry_out`, read by `paint_lines_short`; may run D=1 |
| `emit_edge_width_offset_core` | 2132 | adc_overflow | exit V — already the minimal "replay only V" idiom (565/2000) |
| `road_edge_side_core` | 2226 | adc_step | far-side arm's V (`ADC #$78`) leaks to caller before RTS |
| `draw_road_core` | 2788 | adc_step | exit V on the SMC-early-return path (28/200) |
| `mark_line_surfaces_core` | 4874, 4876 | adc_step | 4876's last V is the routine's exit V (162/800); 4874's N is intertwined — keep the pair |
| `edge_x_offscreen_core` | 4547 | adc_step | V escapes + the CMP-carry is ROR'd into `shared_temp_76` (the real output) |
| `copy_dash_data` | 3189 | adc_step | exit V of `ADC #$80` (value is identity, but V replay still needs the helper) |
| `sound_queue_core` | 6418 | adc_step | C and V both live at exit; nothing below writes either |
| `plot_view_src_line_core` | 8248 | adc_step | V live at *every* exit below |
| `place_player_in_section` | 9196 | sub_from | result A **and sign N** are live inputs to the `abs8()` leaf hook two lines down |
| `tally_bcd_column` | 9605, 9607 | adc_value | genuine BCD inside the `cpu.D=1` region — one of the 8 SED sites; **keep `adc_value`** |

---

## Part 2 — CONVERT queue (44 sites) — delete a row when applied

Delete each row in the commit that converts it (this is a queue, like `rename.md`). Group by the
suggested batch order (safest first).

### Batch A — objects/signs (13 sites; all dead-flag/local except the one †)

| function | line(s) | idiom |
|---|---|---|
| `scale_shape_vectors_core` | 7779 | carry-in 0; V overwritten by the `BIT` next line — value only |
| `scale_shape_vectors_core` | 7782 | carry-in 0 → `a`; flags dead into the halving block |
| `scale_shape_vectors_core` | 7792 | `a + carry` rounding add; flags dead (store follows) |
| `scale_shape_vectors_core` | 7807 | negation `A+1`; flags dead (INC/INY follow). NB the two return arms already set C/N/Z explicitly — leave those |
| `draw_track_object_core` | 3012+3013 | fold to one `uint16_t` bearing subtract; sign = `deltaHi&0x80`; keep the `math_lo=` scratch write |
| `draw_track_object_core` | 3022 | carry-in 0; column's own LDA flags dead |
| `build_road_sign_core` | 7591 | `nibble + C` (C from the equal cmp, an input); output flags overwritten by `AND #$0F` |
| `build_road_sign_core` | 7626 | carry-in 0; `(tableByte&7)+7` → plot_shape value; flags dead |
| `build_road_sign_core` | 7645 | feeds `abs8`; inline abs-after-subtract `(d&0x80)?(uint8_t)(0u-d):d` (N = bit7) |
| `build_sign_origin_core` † | 7453+7454 | fold to one `uint16_t` origin subtract — ⚠ verify fixture liveMask is result-only (comment claims exit flags; the twin caller discards them) |
| `write_object_slot_core` | 7533 | `projectedLine-1`, N consumed locally by the BMI at 7534 (bit7) |
| `write_object_slot_core` | 7539 | `proj_width_shift-9` then DEX; N/Z die at the DEX; the exit-C replay lower down is already plain C |
| `plot_shape_edges_core` | 7853, 7861 | carry-in 0; N read locally (bit7), value clamped after |

### Batch B — IO/text (9 sites; two clean uint16 folds, one †)

| function | line(s) | idiom |
|---|---|---|
| `read_pedals_and_gears` | 8882 | ADC-no-CLC doubling; C consumed by the next `if(!cpu.C)` — replay from `sum>0xFF` |
| `read_pedals_and_gears` | 8901 | carry-in 0; section flags all overwritten before exit → `(engine_revs>>2)+5` |
| `read_pedals_and_gears` | 8946 | carry-in 0; following $FF/$07 compares recompute flags → `cpu.A+gear_index` |
| `mode5_addr_core` † | 8526+8527 | fold to one `uint16_t` screen address (char-row base + doubled) — ⚠ verify the #113/#114 fixture isn't asserting C/V, then relax liveMask |
| `vdu_char_emit_core` | 8604+8605 | fold to one `uint16_t` screen pointer `− $40`; exit N/Z come from `mem[VDU_CHAR_BLOCK]` (A returns live), subtract flags dead |
| `adc_read_core` | 8648 | carry-in 0; N consumed locally by `if(cpu.N)`, exit C recomputed by CMP($0A); `+$80 ≡ ^$80` (bit7), replay N via `(a&0x80)` |

### Batch C — small render leaves + accumulator (22 sites)

| function | line(s) | idiom |
|---|---|---|
| `draw_road_core` | 2808 | `(uint8_t)(horizon_index+0x28)`; spans overwrites flags immediately |
| `build_track_geometry_core` | 2686 | guarded by `cmp_ge(horizonPoint,0x28)` → no borrow; flags dead. `horizonPoint - 0x28` |
| `horizon_half_width_at` | 2642 | subtract feeds `abs8`; replay `cpu.N=(d>>7)&1` then keep `abs8` callee |
| `horizon_half_width_at` | 2647 | `cpu.A >> 1`; C not in the exit contract (contract = A + Y) |
| `paint_lines_short` | 1063 | `(uint8_t)(0xF1 - mem[...])`; value recomputed at 1066, no flag read |
| `shift_near_edge_points_core` | 1797 | value → near_edge_first; ⚠ verify `clamp_near_edge_window_core` ignores entry flags first |
| `apply_driving_model_core` | 2917+2918 | fold to one `uint16_t` accumulator restore `acc = entry + delta`; exit flags dead (next call opens with LDA) |
| `halve_signed_rounded` | 8079 | `(uint8_t)(rotated + (value&1))`; return value only, flags dead |
| `derive_endpoint` | 8087 | carry-in 0; LSR twice overwrites flags → `halved + plot_x` |
| `fill_object_gap_core` | 8110→8113 | local carry chain (not escape): `bias = 0x7F-cursor; carry=(0x7F>=cursor); floor = bias+top+carry` |
| `fill_object_gap_core` | 8119 | carry-in 0; the following LSR consumes the value |
| `fill_object_gap_core` | 8126→8132 | local carry: `carry = (lowBase >= bias)` |
| `fill_object_gap_core` | 8148 | value + carry-in from the preceding `cmp_ge`; N/Z recomputed at 8150-51 |
| `column_gap_walk_core` | 5132+5133 | the ASL/ROR pair builds `plot_ptr = 0x3000 + column*0x80` — write as one `uint16_t` |
| `road_edge_walk_subdivide` | 2487+2488 | fold to one `uint16_t` gap subtract — ⚠ N feeds `cpu.C=cpu.N` then a **`PHP` whose stack byte is in the differential**; replay `N=(delta&0x8000)!=0`, keep the PHP faithful |
| `plot_view_src_line_core` † | 8399 | `column - previous - 1`; Z/N consumed locally by `if(cpu.Z||cpu.N)return`; ⚠ V/C claimed live by comment but caller discards — verify liveMask, then convert with local Z/N replay |

### The three † liveMask-caveat sites

`build_sign_origin_core` 7454, `mode5_addr_core` 8527, `plot_view_src_line_core` 8399 each carry a
comment asserting their SBC/ADC exit flags escape, **but the twin caller discards them** (an
`LDY`/loop follows without reading C/V). This is the oracle-calls-native trap (`revs_decisions`
campaign log): the faithful move is value + relax the fixture's `liveMask` to result-only — but
**confirm the fixture isn't currently asserting those flags before dropping them.**

---

## Method reminders for the conversions

- Every conversion is `make validate FN=<name>` **0 mismatch** + ≥4 sabotages (each FAIL; `rm` the
  `.o` and binary before each build; use `Edit`, not `perl -0pi`), then full unfiltered `make
  validate` (PRNG stream-shift), then `make determinism` + `make determinism-drive` byte-identical.
- ⚠ `determinism-drive` is the backstop `validate` cannot be: a removed net-neutral `PHP`/`PLP`
  stops writing dead stack residue, which lands at the **race's** SP (~`$01F6`), not the fixture's
  (`$01FF`) — a single isolated stack-page byte with no cascade over 300 frames is dead residue, not
  a bug. Confirm by re-recording both goldens from the new build (0 diffs).
- Expect **zero FPS change** — GCC already dead-store-eliminates dead flag writes (`cpu` is a plain
  global). The perf lever is deleting `mem[]` traffic (`PHP`/`PLP`, scratch round-trips), not
  de-macroing flags. Readability/faithfulness pass. (`perf-method.md`.)
