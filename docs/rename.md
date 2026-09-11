# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

## Two standing rules

Both learned from what this queue used to hold:

- ⚠ **Do NOT add a fact-shaped name to an unsettled cell.**  An `[INFERRED]` row that says what
  is unsettled is worth more than a confident wrong name; three entries here were open for weeks
  precisely because a plausible name had been written down first and then believed.
- ⭐ **Name the cheap run that would settle it, in the entry itself.**  Static evidence runs out;
  what closed the last entries was a reference-loop peek, a per-frame series, or — for
  `span_cap_surface_over`/`_fill` — reading the DATA the code indexes rather than the code.

## `menu_row_attr` ($7E85) is also a CODE label — `make fbwrites` attributes view sweep stores to it

`$7E85` is named for the front end's MODE 7 menu attribute cells, and that reading is right for
the front end.  But the same 1 KB is `copy_dash_data`'s overlay at race time, and `make fbwrites`
attributes the frame-buffer stores at **`$7E31..$7EA8`** to this name — those are chain-B units of
`view_paint_lines`, not a menu routine.  Every attribution report of the RACE therefore reads as
if a menu routine were painting the viewport.

Wanted: a second, CODE-side symbol for the overlay's chain-B unit block (e.g.
`view_paint_unit_chain_b`) so the two tenants of the page are named separately, and a note on
`menu_row_attr` that it only means the menu while the front end is up.

⭐ The cheap run that settles the extent: `make fbwrites` and read the PC histogram rows in
`$7E00..$7F00` — the block's first and last unit addresses are in it directly.
