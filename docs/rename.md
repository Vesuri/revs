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

## 3. `$52A4 tick_wheel_spin` — the element is identified, the GATE is not

`mem[$0000]` gates the whole XOR (`$52B4 LDA $0000 / BEQ`), and `$0000` has no name.  Nothing else
in the queue depends on it, but a zero-page cell that can switch off a visible animation should not
stay anonymous.
