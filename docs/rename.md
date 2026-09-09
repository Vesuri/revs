# The rename QUEUE — open items only

Names and cells that contradict what they do, or have no name yet.  Applied entries are DELETED
from this file in the same commit that applies them; `disasm/symbols.csv` carries the name and its
evidence afterwards.  The conventions that govern this queue are in `CLAUDE.md` §Working
conventions, not here.

⚠ **Cite an entry by its SUBJECT, never by a number.**  The entries used to be numbered and every
applied one renumbered the rest, so four references in `symbols.csv` and `docs/` were pointing at
the wrong entry by the time anyone read them.  Headings are the anchors now.

## (empty)

**The queue is EMPTY.**  Every name that contradicted its behaviour has been applied, and the
cells whose meaning was open were settled by measurement rather than by a plausible name — the
evidence lives in `symbols.csv`'s notes, tagged `[MEASURED <date>]` or `[DERIVED <date>]`.

Two standing rules for the next entry, both learned from what this queue used to hold:

- ⚠ **Do NOT add a fact-shaped name to an unsettled cell.**  An `[INFERRED]` row that says what
  is unsettled is worth more than a confident wrong name; three entries here were open for weeks
  precisely because a plausible name had been written down first and then believed.
- ⭐ **Name the cheap run that would settle it, in the entry itself.**  Static evidence runs out;
  what closed the last entries was a reference-loop peek, a per-frame series, or — for
  `span_cap_surface_over`/`_fill` — reading the DATA the code indexes rather than the code.
