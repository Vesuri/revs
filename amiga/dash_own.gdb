# ⭐⭐ THE BOTTOM BAND'S RECTANGLE-SET ORACLE (needs `make DASHCHECK=1`).
#
# `make DASHOWN=1` gives the renderer display lines 158..191 — the needles, the wing mirrors and
# the lamps — and re-expands six small rectangles of mem[] into the back buffer once per painted
# frame instead of letting the decode convert all forty cells of all 34 rows
# (docs/span-render-plan.md §12c).  The one thing that can be wrong is the rectangle SET: a
# writer with neither a rectangle nor a `REVS_PLOT_BYTE` at its store site leaves a byte stale.
#
# This reads the in-process differential that catches exactly that — every painted frame, all
# forty cells of the band re-expanded and compared against the buffer.  g_plotRectMismatch must
# be 0.  g_plotRectMismatchAt is (display line << 8) | cell of the first offender.
#
# ⚠⚠ SCOPE: a practice session has an EMPTY TRACK, so the two wing-mirror rectangles are never
# exercised — this proves they do no harm, not that their bounds are right.  Those come from the
# game's own six-segment tables (RevsPlot.cpp kDynRect).
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "=== DASHOWN  checks=%lu  mismatch=%lu  first=(line %u, cell %u)  (vbi=%u)\n", \
  g_plotRectChecks, g_plotRectMismatch, \
  (g_plotRectMismatchAt >> 8), (g_plotRectMismatchAt & 0xFF), g_vbiCount
printf "   first mismatch at check %lu, last at check %lu (of %lu)\n", \
  g_plotRectMissFirst, g_plotRectMissLast, g_plotRectChecks
set $i = 0
while $i < 34
  if g_plotRectMissRow[$i] > 0
    printf "   line %3d : %u\n", 158 + $i, g_plotRectMissRow[$i]
  end
  set $i = $i + 1
end
set $i = 0
while $i < 40
  if g_plotRectMissCell[$i] > 0
    printf "   cell %3d : %u\n", $i, g_plotRectMissCell[$i]
  end
  set $i = $i + 1
end
printf "verdict: %s\n", \
  g_plotRectMismatch ? "FAIL — a writer in 158..191 has no rectangle and no delta hook" : \
  (g_plotRectChecks ? "PASS" : "VACUOUS — no check ran, the band was never owned")
kill
quit
