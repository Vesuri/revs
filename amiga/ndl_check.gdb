# ⭐⭐ plot_line_octant's NEEDLE DDA IN 68000 ASM vs ITS C LOOP, EVERY LINE, ON THE SAME 64 KB
# (`make NDLASMCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, NDLASM=1).
# at = the first differing mem[] address; $10000 = the pixel list, $10001 = the plot pointer.
set pagination off
set confirm off
tbreak Revs::render if g_ndlChecks >= 300
continue
printf "=== NDL checks=%lu mismatch=%lu at=%05lx (vbi=%u)\n", g_ndlChecks, g_ndlMismatch, g_ndlMismatchAt, g_vbiCount
printf "=== NDLFUZZ cases=%lu mismatch=%lu full=%lu outside=%lu\n", g_ndlFuzzCases, g_ndlFuzzMismatch, g_ndlFuzzFull, g_ndlFuzzOutside
printf "=== NDLFUZZ first bad: what=%lx count C/A=%lu/%lu (was %lu) outside C/A=%lu/%lu  in0=%08lx in1=%08lx\n", g_ndlBad[0], g_ndlBad[1], g_ndlBad[2], g_ndlBad[7], g_ndlBad[3], g_ndlBad[4], g_ndlBad[5], g_ndlBad[6]
printf "verdict: %s\n", (g_ndlChecks == 0 || g_ndlFuzzCases == 0 || g_ndlFuzzFull == 0 || g_ndlFuzzOutside == 0) ? "VACUOUS — a check never ran" : ((g_ndlMismatch || g_ndlFuzzMismatch) ? "FAIL" : "PASS")
kill
quit
