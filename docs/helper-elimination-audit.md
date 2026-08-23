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

### Batch A — objects/signs — ✅ APPLIED (HEAD after this commit)

All 13 sites converted. `make validate` 0-mismatch, 10 sabotages caught, `make determinism` +
`make determinism-drive` byte-identical against the pre-change golden (parked and driving).

⚠⚠ **THE AUDIT UNDER-COUNTED V ESCAPES — READ BEFORE AUDITING BATCH B/C.** Five of these sites
were marked "flags dead" and were NOT: their converted `ADC`/`SBC`'s **V** (and, once, **C**)
reaches an exit. **On the 6502 only `ADC`/`SBC`/`BIT`/`PLP`/`CLV` write V** — `CMP`/`AND`/`ORA`/
`LSR`/`ASL`/`INC`/`DEC`/`TAX`/`INY` do NOT. So the usual "the next CMP recomputes the flags"
argument kills N/Z/C but LEAVES V live. A converted arithmetic op whose N/Z/C are overwritten
before the exit can still leak its V. The corrected sites and their replays (all using the
sanctioned single-flag helpers `adc_overflow`/`sbc_overflow`, cpu untouched):

- `draw_track_object` $2AE1 subtract → exit V on the not-visible arm; $2AF1 `ADC #$50` → exit V
  on the drawn arm (plot_object leaves V alone). Both replayed.
- `write_object_slot` $2A7A `SBC #1` → exit **V and C** on the reject-line arm; $2A82 `SBC #9` →
  exit V on the drawn arm. Both replayed.
- `scale_shape_vectors` $206C/$207E/$208A ADCs → V kept live to both exits (abandon + loop-end).
- `plot_shape_edges` $20AE bottom-line ADC → exit **V and C** on the BMI reject; $20C1 top-line
  ADC → exit V on the no-height reject. Replayed.

**The general rule for B/C: when a converted `ADC`/`SBC` is followed only by `CMP`/`AND`/`ORA`/
shifts/`INC`/`DEC`/transfers before an exit, its V still escapes — replay it, or KEEP the helper.**

Also: the three † caveat sites were resolved by PINNING `c.D=0` in the two fixtures
(`test_road_sign` i≥3, `test_object_shape` all) citing `static-map.md §Decimal mode`, since the
object/sign path is always binary — NOT by relaxing liveMask. `determinism-drive` is the backstop.

### Batch B — IO/text — ✅ APPLIED (HEAD after this commit)

All 9 sites converted. `make validate` 0-mismatch, 9 sabotages caught + 1 provable non-defect,
`make determinism` + `make determinism-drive` byte-identical against the pre-change golden.

- `read_pedals_and_gears` ×3: the x1.5 pedal doubling `(mag<<1)+(mag>>1)+bit7(mag)` (C consumed
  locally by `if(sum<=0xFF)`), the self-drive amount `(engine_revs>>2)+5`, and the gear shift
  `gear_index+delta` — all with dead exit flags (the gears' `BIT`, then value-test `CMP`s and
  `draw_gear_indicator`, overwrite them; no caller reads the exit V/C).
- `mode5_addr_core` (the †): folded to one `uint16_t` `char_row_base + (offset<<1)`. Its second
  add's **C and V are dead at every caller** (nothing writes either after it returns), so they
  propagate as the whole text path's exit C/V; the game reads only plot_ptr / A / X / Y / N / Z.
  Resolved by DROPPING V and C from the fixture mask for the text/screen-address sub-cluster
  (mode5_addr, mode5_addr_for_cell, vdu_char_emit/_wide/_def, draw_gear_indicator, clamp_and_store)
  — NOT by keeping the helper. The misleading "C and V escape" comment was corrected in place.
- `vdu_char_emit_core`: folded the row-step to `plot_ptr − $0140` (SEC/SBC #$40 then SBC #$01 —
  **one MODE-5 character row, not $40**; the old comment was wrong). Subtract flags dead.
- `adc_read_core`: `reading + $80 ≡ ^$80`; only N is read locally (BPL), exit N/Z/C recomputed by
  `CMP #$0A`, and its **V is dead** (dropped from adc_read's mask; its C — the dead-zone flag — is
  a real output and stays).

⭐ THE WHOLE CLUSTER RUNS AT D=0 (fixture now pins `binaryPath=1` for all 17): every routine here
is race-time input/UI (steering, pedals, gears, analogue axis, dashboard text) — none is among the
8 SED sites (`static-map.md §Decimal mode`).

⚠ NON-DEFECT confirmed (the "no change at all" class, `validation-harness.md §FIFTEENTH): the
dropped-ASL-carry sabotage on the x1.5 doubling SURVIVED — `bit7(mag)` is provably 0 because
`adc_read` folds both sides of centre to a magnitude in 0..$7F, so the ASL never carries. The term
is kept as the faithful idiom and annotated; the SIBLING (the `sum>$FF` overflow test) IS reachable
(mag=$7F → $13D) and its sabotage was caught.

### Batch C — small render leaves + accumulator (remaining sites)

⚠ Sub-commit 1 (geometry group) APPLIED: `build_track_geometry_core` wrap, `horizon_half_width_at`
(subtract→abs8→LSR), `draw_road_core` pass-1 base. `build_track_geometry`'s exit N/V/Z/C are
byproducts (caller at $1710 opens LDA/SEC/SBC), so they were DROPPED from the fixture mask, not
reproduced. The subtract feeds `abs8` so `|a-b|==|b-a|`: the table-SWAP sabotage is a provable
non-defect (the "no change" class), but a table-OFFSET (magnitude) error is caught — that is the
sibling. `make validate` 0-mismatch, determinism byte-identical parked + driving.

⚠ Sub-commit 2 (object-line group) APPLIED: `halve_signed_rounded`, `derive_endpoint`,
`fill_object_gap_core` (5 helper calls), and `plot_view_src_line_core`'s gap subtract. Both
fixtures now pin `c.D=0` (static-map §Decimal mode — the object plotter is never decimal) and drop
V/C: MEASURED that all four callers of plot_view_src_line ($20F1/$20F5) open `BIT` before reading a
flag and never read exit C, so its (and fill_object_gap's) exit V/C are byproducts — N/Z stay
compared. Resolves the `plot_view_src_line_core` † caveat below. `make validate` 0-mismatch, 7
sabotages caught + 1 provable non-defect (`fill_object_gap`'s low-pointer carry `>=` vs `>` is
unreachable: lowBase∈{0,$80}, bias∈[$30,$7F]), determinism byte-identical parked + driving.

⚠ Sub-commit 3 (render leaves + accumulator) APPLIED: `paint_lines_short`, `shift_near_edge_points_core`,
`apply_driving_model_core` (16-bit accumulator fold), `column_gap_walk_core` (ASL/ROR → `plot_ptr =
0x3000 + column*0x80`), and — now dead — the `lsr_a`/`ror_a` helper definitions were deleted. **The
V-escape trap struck twice more:** `paint_lines_short`'s `SEC/SBC $F1` V reaches `view_paint_lines`'
exit on 61/700 cases (replayed via `sbc_overflow`), and `column_gap_walk`'s `ADC #$60` is the last op
to write V before the loop (all LDA/CMP) so its V escapes too (replayed via `adc_overflow`, and
symbols.csv already documented it). `shift_near_edge_points`' subtract V is a genuine DEAD byproduct —
its sole caller road_edge_start reads near_edge_first with an immediate CMP — so it was DROPPED from the
fixture mask (A kept live; D=0 pinned since the SBC is now binary). `make validate` 0-mismatch, 9
sabotages caught, determinism byte-identical parked + driving.

| function | line(s) | idiom |
|---|---|---|
| `road_edge_walk_subdivide` | 2487+2488 | fold to one `uint16_t` gap subtract — ⚠ N feeds `cpu.C=cpu.N` then a **`PHP` whose stack byte is in the differential**; replay `N=(delta&0x8000)!=0`, keep the PHP faithful |

### The remaining † liveMask-caveat sites

`build_sign_origin_core` 7454 and `mode5_addr_core` 8527 were resolved in Batches A and B
respectively (D=0 pin / V-C drop). `plot_view_src_line_core`'s gap subtract was resolved in
sub-commit 2 above (V/C measured dead at every caller — they open `BIT`).

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
