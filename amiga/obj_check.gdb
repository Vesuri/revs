# ⭐⭐ THE OBJECT PLOTTER'S LINE SIDE IN 68000 ASM vs plot_view_src_line_c, EVERY CALL, ON THE SAME 64 KB
# (`make OBJCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, OBJASM=1; RACEPROPER=1 for the cars).
# at = the first differing mem[] address; $10000 = plot_ptr_v, $10001 = plot_ptr2_v.
# modes = the game's calls per mode (0 the closing arm, 1 open, 2 close): a 0 is an arm never compared.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 20000
continue
printf "=== OBJ checks=%lu mismatch=%lu at=%05lx modes=%lu/%lu/%lu (vbi=%u)\n", g_objChecks, g_objMismatch, g_objMismatchAt, g_objModes[0], g_objModes[1], g_objModes[2], g_vbiCount
printf "=== OBJFUZZ cases=%lu mismatch=%lu at=%05lx painted=%lu\n", g_objFuzzCases, g_objFuzzMismatch, g_objFuzzMismatchAt, g_objFuzzPainted
printf "verdict: %s\n", (g_objChecks == 0 || g_objFuzzCases == 0 || g_objFuzzPainted == 0 || g_objModes[0] == 0 || g_objModes[1] == 0 || g_objModes[2] == 0) ? "VACUOUS — a check never ran" : ((g_objMismatch || g_objFuzzMismatch) ? "FAIL" : "PASS")
kill
quit
