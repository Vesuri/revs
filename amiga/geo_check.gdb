# ⭐⭐ THE WALK'S WIDTH EMITTER IN 68000 ASM vs ITS C CORE, EVERY CALL, ON THE SAME 64 KB
# (`make GEOCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, GEOASM=1; TRACK=n per circuit).
# at = the first differing mem[] address; $10000 = the exit V.  ⚠ `checks` must be LARGE.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 30000
continue
printf "=== GEO checks=%lu mismatch=%lu at=%05lx (vbi=%u)\n", g_geoChecks, g_geoMismatch, g_geoMismatchAt, g_vbiCount
printf "=== FUZZ cases=%lu mismatch=%lu V-cases=%lu\n", g_geoFuzzCases, g_geoFuzzMismatch, g_geoFuzzV
printf "verdict: %s\n", (g_geoChecks == 0 || g_geoFuzzCases == 0 || g_geoFuzzV == 0) ? "VACUOUS — a check never ran" : ((g_geoMismatch || g_geoFuzzMismatch) ? "FAIL" : "PASS")
kill
quit
