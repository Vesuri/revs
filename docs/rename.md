# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

⚠⚠ **Everything left in this file needs a MEASUREMENT, not more reading.**  The static evidence has
been taken as far as it goes: every remaining cell already carries an `[INFERRED]` name in
`symbols.csv`, and each entry below names the cheap run that would settle it.  Do NOT add a
fact-shaped name to any of these in the meantime — an `[INFERRED]` row that says what is unsettled
is worth more than a confident wrong name.

ℹ **The scoped-name decisions are SETTLED and gone** (arithmetic window `$0078`/`$0079`/`$008E`/
`$008F`, `math_lo`/`math_hi`, and the `point_delta` window).  There is only ever one global symbol
per address, so a "second set of scoped names" was never on the table: each cell keeps its primary
name, `symbols.csv` records every tenancy in the note, and the twins carry file-local defines
(`SPAN_*`, `SLIP_MAG_LO`, the `point_distance_hypot` back-to-front comment) so the code reads as
what it computes.  Nothing to do there.

---

## `span_cap_surface_a` (`$0034`) / `span_cap_surface_b` (`$0033`) — what distinguishes them, beyond which one gets used

Both are per-scan-line surface codes `interp_edge` composes for the span it is about to walk, and
the pass number sits in bits 3-5 of each.  `$2F19` picks between them on `span_swapped`, i.e. on
whether the walk was reversed.  What is NOT derived is why the two are built so differently:
`span_cap_surface_a` takes bits 3-4 of `colour_pattern_tbl[0]` and ORs `$40`;
`span_cap_surface_b` takes two scattered bits of `colour_pattern_tbl[3]` and ORs `$80`.  Bit 7 vs
bit 6 of a `view_line_surface` entry is a real distinction — `view_paint_lines` reads that array
for the line's background — so the two codes mean two different kinds of line.

⚠ **What would settle it cheaply, and it is a rendered-thing diff, not more reading**: park the
car (`make refloop --park`) and dump `view_line_surface` on a frame where the road runs uphill and
one where it runs downhill.  The `a`/`b` split is the only thing in the pass that keys off walk
direction, so whichever visual feature swaps between those two frames is what bit 6 vs bit 7 names.
