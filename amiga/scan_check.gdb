# ⭐⭐ THE SCAN IN 68000 ASM vs ITS C, EVERY SWEEP, ON THE SAME SOURCES
# (`make SCANCHECK=1 [LOWFULLCHECK=1] PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, SCANASM=1).
# at = (what << 8) | line-or-cell: 1 count, 2 source block (cell), 3 cursor, 4 list, 5 seed (line).
# ⚠ `checks` (sweeps) and `events` must be LARGE: zero is a vacuous pass.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 2500
continue
printf "=== SCAN sweeps=%lu mismatch=%lu at=%04x events=%lu (vbi=%u)\n", \
  g_scanChecks, g_scanMismatch, g_scanMismatchAt, g_viewEvents, g_vbiCount
printf "verdict: %s\n", \
  (g_scanChecks == 0 || g_viewEvents == 0) ? "VACUOUS — the check never ran" : \
  (g_scanMismatch ? "FAIL" : "PASS")
kill
quit
