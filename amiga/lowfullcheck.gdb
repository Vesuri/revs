# ⭐⭐ THE LOW BLOCK THROUGH THE FULL-WIDTH PAINTER vs THE RUN PAINTER, AS THE PLAYER SEES IT
# (`make LOWFULLCHECK=1 [LOWOWNCHECK=1] PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`).  Every PF1
# bit under a transparent PF2 pixel of display lines 117..157, every painted frame, with every
# owed cell poisoned first (RevsPlot.cpp §revs_plot_low_compare).
# ⚠ `g_lowFullChecks` must be LARGE (1640 a sweep): zero is a vacuous pass.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 2500
continue
printf "=== LOWFULL checks=%lu mismatch=%lu at=line %u cell %u noCock=%lu (vbi=%u)\n", \
  g_lowFullChecks, g_lowFullMismatch, g_lowFullMismatchAt >> 8, g_lowFullMismatchAt & 0xFF, \
  g_lowFullNoCock, g_vbiCount
printf "=== LOWOWN  checks=%lu mismatch=%lu at=line %u cell %u\n", \
  g_lowOwnChecks, g_lowOwnMismatch, g_lowOwnMismatchAt >> 8, g_lowOwnMismatchAt & 0xFF
printf "verdict: %s\n", \
  g_lowFullChecks == 0 ? "VACUOUS — the check never ran" : \
  ((g_lowFullMismatch || g_lowOwnMismatch) ? "FAIL" : "PASS")
kill
quit
