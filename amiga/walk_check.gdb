# ⭐⭐ THE WALK'S POINT LOOP IN 68000 ASM vs ITS C LOOP, EVERY WALK, ON THE SAME 64 KB
# (`make GEOCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, GEOASM=1 WALKASM=1; TRACK=n per circuit).
# at = the first differing mem[] address; $10000 = the exit X, $10001 = one of the four C words.
# The emitter's own fuzzer runs first (FUZZ line); every counter on the verdict must be non-zero.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 50000
continue
printf "=== WALK checks=%lu mismatch=%lu at=%05lx (vbi=%u)\n", g_walkChecks, g_walkMismatch, g_walkMismatchAt, g_vbiCount
printf "=== WALKFUZZ cases=%lu mismatch=%lu diag=%lu deep=%lu cap=%lu\n", g_walkFuzzCases, g_walkFuzzMismatch, g_walkFuzzDiag, g_walkFuzzDeep, g_walkFuzzCap
printf "=== WALKFUZZ first bad: case=%lu xC=%02lx xA=%02lx asm-exit=%03lx hooks C/asm=%lu/%lu at=%05lx resume=%lu\n", g_walkFuzzBad[0], g_walkFuzzBad[1], g_walkFuzzBad[2], g_walkFuzzBad[3], g_walkFuzzBad[4], g_walkFuzzBad[5], g_walkFuzzBad[6], g_walkFuzzBad[7]
printf "=== FUZZ cases=%lu mismatch=%lu V-cases=%lu\n", g_geoFuzzCases, g_geoFuzzMismatch, g_geoFuzzV
printf "verdict: %s\n", (g_walkChecks == 0 || g_walkFuzzCases == 0 || g_walkFuzzDiag == 0 || g_walkFuzzDeep == 0 || g_walkFuzzCap == 0 || g_geoFuzzV == 0) ? "VACUOUS — a check never ran" : ((g_walkMismatch || g_walkFuzzMismatch || g_geoFuzzMismatch) ? "FAIL" : "PASS")
kill
quit
