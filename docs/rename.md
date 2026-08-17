# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

---

## 1. `edge_y` ($5F20) and `view_line_surface` ($5F60) cannot both be right

Fallout from the 2026-08-17 axis correction (the 80-entry `$50`-stride family is now
`surface_edge_0..3` / `line_attr_0/1` / `surface_colour_at`, indexed by SCAN LINE).

`edge_y` is documented as one of `interp_edge`'s 2x40 coordinate arrays, which would make it 80
bytes at `$5F20..$5F6F` — but `$5F60` is read as an 80-entry per-scan-line table by BOTH
`surface_colour_at` and `view_paint_lines`, so the two overlap by sixteen bytes. Either `edge_y` is
40 entries, or `$5F60` is the tail of something else. Settle it from `$1A20`'s and `$24F6`'s inner
loops (`make fbwrites` as the check) and fix whichever note is wrong — `edge_x_lo/hi` + `edge_y` are
*point* lists and easy to conflate with the per-line buffers.

## 2. `$1C1C project_geometry` — the name is almost certainly wrong

`$2285` (`project_point`) is the engine's perspective divide; `$1C1C` reads `colour_pattern_tbl`,
which a projection has no business touching.  Suspect a pixel-pattern / column-shading step.
Two routines called "project…" doing different things is the exact tax this queue exists to
prevent.

## 3. `$52A4 body_tick_xor_anim` — [INFERRED]; the element is unidentified

Measured: the rate (`$62FA += $30 + $63`, proportional to road speed), the region (EORs at
`$6E85/$6FBD/$6FB2/$6FC0/$70F8/$6E8A`, display lines 120-143), the XOR draw/erase.  Not measured:
*what* it draws.

## 4. `view_paint_lines`' boundary tables are [PROVISIONAL]

`view_edge_mask_a/fill_a`, `view_edge_mask_b/fill_b`, `view_bnd_a_mask/fill`,
`view_bnd_b_mask/fill`, `view_bnd_a_src/b_src`, `view_edge_index_tbl`, `view_cell_bytes` (`$6000`).
They are named after the operator (mask, fill, index), not after what they mean.  They compose the
silhouette of the cockpit opening one cell at a time; rename after the silhouette once §1 settles
the axis vocabulary.

⚠ Explain while doing it: `view_stop_b_tbl` (`$3080`) **is cell column 1's source area**, so the
chain can zero a byte the driver is about to read.  Either the overlap is deliberate reuse or one
of the two readings is wrong.

## 5. `$7B9C lap_time_readout` — [PROVISIONAL]

It is the nearest preceding symbol to much of the `$7B00` overlay, which is how the overlay's
view-rasteriser code came to carry "dashboard" names.  Confirm `$7B9C` itself is the lap/best-time
readout before hanging anything else off it.
