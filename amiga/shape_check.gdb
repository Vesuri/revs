# ⭐⭐ scale_shape_vectors IN 68000 ASM vs scale_shape_vectors_core, EVERY CALL, ON THE SAME 64 KB
# (`make SHAPECHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, SHAPEASM=1; add RACEPROPER=1 for cars).
# at = the first differing mem[] address; $10000 = the return value.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 30000
continue
printf "=== SHAPE checks=%lu mismatch=%lu at=%05lx (vbi=%u)\n", g_shapeChecks, g_shapeMismatch, g_shapeMismatchAt, g_vbiCount
printf "=== SHAPEFUZZ cases=%lu mismatch=%lu abandoned=%lu\n", g_shapeFuzzCases, g_shapeFuzzMismatch, g_shapeFuzzAbandon
printf "verdict: %s\n", (g_shapeChecks == 0 || g_shapeFuzzCases == 0 || g_shapeFuzzAbandon == 0) ? "VACUOUS — a check never ran" : ((g_shapeMismatch || g_shapeFuzzMismatch) ? "FAIL" : "PASS")
kill
quit
