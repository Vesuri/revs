# ⭐⭐ BOTH ENDS OF fill_dash_edge_columns IN 68000 ASM vs edge_run_flat, EVERY FRAME, ON THE SAME 64 KB
# (`make EDGECHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, EDGEASM=1; TRACK=0..5 per circuit).
# at = the first differing mem[] address; $10000 = the exit registers, $10001 = a plot pointer.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 30000
continue
printf "=== EDGE checks=%lu mismatch=%lu at=%05lx (vbi=%u)\n", g_edgeChecks, g_edgeMismatch, g_edgeMismatchAt, g_vbiCount
printf "=== EDGEFUZZ cases=%lu mismatch=%lu attr-limits=%lu\n", g_edgeFuzzCases, g_edgeFuzzMismatch, g_edgeFuzzAttr
printf "verdict: %s\n", (g_edgeChecks == 0 || g_edgeFuzzCases == 0 || g_edgeFuzzAttr == 0) ? "VACUOUS — a check never ran" : ((g_edgeMismatch || g_edgeFuzzMismatch) ? "FAIL" : "PASS")
kill
quit
