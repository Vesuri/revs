# src/cpu/bcd.h's ABCD/SBCD paths, swept on the real 68000 at startup.
# Needs `make BCDSELFTEST=1 PROBES=1`.  g_bcdCases must be 40000 and both fail counts 0.
set pagination off
set confirm off

# ⚠ `continue` first: diag_run.sh attaches with the target HALTED and SIGINTs gdb after the
# delay, so a script without it reads the state before a single instruction has run — which
# looks exactly like a zeroed counter.
continue
printf "--- bcd.h ABCD/SBCD self-test on the target ---\n"
printf "vbi      = %u\n", g_vbiCount
printf "cases    = %lu (expect 40000)\n", g_bcdCases
printf "addFails = %lu (expect 0)\n", g_bcdAddFails
printf "subFails = %lu (expect 0)\n", g_bcdSubFails
printf "firstBad = %06lx  (a<<16 | b<<8 | carryIn)\n", g_bcdFirstBad
detach
quit
