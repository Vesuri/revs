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

## 56 addresses a twin names and `symbols.csv` does not — rows to ADD, not renames

Each of these already has a working name and a derivation written at the twin that uses it; what
is missing is the row in the source of truth, which is why the name lives in exactly one place
and cannot reach `mem.h`.  ⚠ The action is to append a `symbols.csv` row carrying the twin's own
name and note, then delete the local `#define` — not to investigate.  `make determinism` +
`-drive` are the gate, because a new row changes the transpiler's labels too.

- **SMC operand bytes near a named seam** — `$1970` the patched low byte of `STA line_attr,Y`;
  `$1DDC` the colour substituted for a zero `surface_colour_at`; `$1DDE` that store's zero-page
  pointer number; `$23B3` `road_edge_start`'s `LDA #$07` horizon cap, patched per circuit at RUN
  time (⚠ this one is an undeclared-SMC risk, not just a naming gap).
- **`road_span_plot` / `road_span_plot_2`'s own patched operands** — `$2F18` the step cap,
  `$2F47`/`$2F60` and `$2F89`/`$2FA2` the two plotters' step-in/step-out slots, `$2F4F`/`$2F50`
  and `$2F91`/`$2F92` their destination-address operands.
- **The view cell chains' 22 SMC sites** ($7BD3-$7BDA, $7D23-$7D4D, $7EEE, $7F23-$7F9B) — the
  `STA`/`RTS` opcode slots and address operands `view_paint_restore` puts back, plus the three
  phase records ($7D24, $7F24, $7F7D) and the sweep terminator ($7EEE).
- **Frame-buffer cells with a fixed job** — `$6E85`/`$6E8A`/`$6FB2`/`$6FBD`/`$6FC0`/`$70F8` the
  six wheel-spin cells, `$77DB`/`$77DC`/`$77E3`/`$77E4` the four `poll_steering_assist` lamp
  bytes, `$7E85` the MODE 7 menu row attribute cell (+$50/row) and `$7FC5` its "nothing chosen
  yet" marker.
- **Tables and bases** — `$2000` the mirror shudder AND-mask table, `$6180` `reciprocal_table`
  reached biased (entry i = $8000/(i+$80)), `$5800` the across-track normal's Y component
  (race-time tenant of the same page `$5700` documents), `$00FA`/`$00FD` the section midpoint
  triple and the near-point stage.

## Two standing rules

Both learned from what this queue used to hold:

- ⚠ **Do NOT add a fact-shaped name to an unsettled cell.**  An `[INFERRED]` row that says what
  is unsettled is worth more than a confident wrong name; three entries here were open for weeks
  precisely because a plausible name had been written down first and then believed.
- ⭐ **Name the cheap run that would settle it, in the entry itself.**  Static evidence runs out;
  what closed the last entries was a reference-loop peek, a per-frame series, or — for
  `span_cap_surface_over`/`_fill` — reading the DATA the code indexes rather than the code.
