# ⭐⭐ THE LOW BLOCK'S PLANE-ARM ORACLE (`make LOWOWN=1 LOWOWNCHECK=1`).  Every cell of both runs,
# on all 41 lines, against the run's own colour walk — the addressing, the two expansion tables,
# the interior fill, the composed end cell and the event threading between the runs.
# ⚠ `g_lowOwnChecks` must be LARGE: a zero is a vacuous pass, not a good one.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== LOWOWN checks=%lu mismatch=%lu at=line %u cell %u (vbi=%u)\n", \
  g_lowOwnChecks, g_lowOwnMismatch, g_lowOwnMismatchAt >> 8, g_lowOwnMismatchAt & 0xFF, g_vbiCount
printf "verdict: %s\n", \
  g_lowOwnChecks == 0 ? "VACUOUS — the check never ran" : \
  (g_lowOwnMismatch ? "FAIL" : "PASS")
kill
quit
