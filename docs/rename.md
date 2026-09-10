# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

## `$306E edge_style_tbl` and `$5EE0 edge_style` — the `_tbl` suffix is on the wrong one

`$5EE0` is the 40+40-entry PER-POINT array the road walk writes and `edge_style_far` ($5F08) is
its second half; `$306E` is the 8-entry LOOKUP, indexed by the low 3 bits of a point's masked
`section_flags`, that supplies the byte written into it.  So the array is `edge_style` and the
lookup is `edge_style_tbl` — backwards, and the two names are one character apart.  Two twins
independently invented crossed local defines for them (`EDGE_STYLE_TBL` for $5EE0,
`EDGE_STYLE_SEL` for $306E) before the mem.h pass forced the question.

Suggested: `$306E` -> `edge_style_by_feature`.  ⭐ Settles from the DATA, not a run: eight bytes
at $306E against `edge_style`'s written values over one `make refloop` frame.

## Two standing rules

Both learned from what this queue used to hold:

- ⚠ **Do NOT add a fact-shaped name to an unsettled cell.**  An `[INFERRED]` row that says what
  is unsettled is worth more than a confident wrong name; three entries here were open for weeks
  precisely because a plausible name had been written down first and then believed.
- ⭐ **Name the cheap run that would settle it, in the entry itself.**  Static evidence runs out;
  what closed the last entries was a reference-loop peek, a per-frame series, or — for
  `span_cap_surface_over`/`_fill` — reading the DATA the code indexes rather than the code.
