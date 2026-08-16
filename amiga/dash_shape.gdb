# ⭐⭐ THE DASHBOARD SWEEP'S SHAPE, IN THE SAME RUN AS THE PHASE SHARE.
#
# Phase 6 item 0 step 2 (docs/direct-bitplane-plan.md §6/§7).  `$7BE2` is the biggest row in the
# profile, and "the dashboard is 36% and mostly static" was an assumption.  These two numbers,
# taken together, decide what to do about it:
#
#   g_shapeDashUnits / sweeps   how many of the 40 column units RAN (the dirty tests)
#   dirty - left  / sweeps      how many of them actually STORED a byte
#
# A large ratio between them means the cost is the SCAN, not the drawing — and then no amount of
# extra dirty-flag machinery (the predecessor project's 23x win) helps, because the game already
# tests per column: what helps is not scanning.  A ratio near 1 means the COUNT/START patching
# already skips the clean columns and the cost is per-unit, i.e. an asm/representation target.
#
# ⚠ Phase 24 is the one to read against these ($1748's `JSR $7BE2`), and the shares are only
# valid WITHIN this run — never diff a share across builds (docs/perf-method.md Rule 2).
# ⚠ No conditional breakpoint: on a PROBES build one gdb round trip per frame cripples the
# target and the table it prints is fiction.  diag_run.sh SIGINTs us at the delay.
#
# Build: cd amiga && make clean && make SHAPE=1 PROBES=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=dash_shape.gdb ./diag_run.sh 200
set pagination off
set confirm off
continue

printf "=== vbi=%u loopFrames=%lu sweeps=%lu\n", g_vbiCount, g_phaseFrames, g_shapeDashCalls
# ⚠⚠ IS THE CAR MOVING?  Every number below is a WORKLOAD, and a stalled or parked car is a
# different one — the dashboard stops changing, the road stops scrolling, and the shape reads as a
# static scene (docs/phases.md §the four defects: $61=00 $3C=00 $58=00 IS a real BBC's parked
# state, and a stuck gear key produces the same $61/$3C/$63 as "never stalls").  Print the engine
# state with the measurement so the two can never be separated.
printf "=== engine: $3C(revs)=%02x $61=%02x $63=%02x $58(gear key)=%02x $2D=%02x $09=%02x\n", \
  mem[0x3c], mem[0x61], mem[0x63], mem[0x58], mem[0x2d], mem[0x09]
set $n = g_shapeDashCalls
if $n > 0
  printf "units(tests)/sweep = %lu   stores/sweep = %lu   dirty cols/sweep = %lu of 40\n", \
    g_shapeDashUnits / $n, (g_shapeDashDirty - g_shapeDashLeft) / $n, g_shapeDashCols / $n
  printf "last sweep: tests=%u dirty=%u left=%u cols=%u\n", \
    g_shapeDashLastUnits, g_shapeDashLastDirty, g_shapeDashLastLeft, g_shapeDashLastCols
  printf "left after sweep (must be ~0, else the COUNT patch is dropping work) = %lu\n", \
    g_shapeDashLeft / $n
end

# The share of the frame phase 24 ($7BE2) actually takes, in this same run.
set $i = 1
set $tot = 0
while $i < 26
  set $tot = $tot + g_phaseTicks[$i]
  set $i = $i + 1
end
set $per = $tot / 1000
printf "phase 24 ($7BE2): ticks=%lu calls=%lu share=%d.%01d%%  %lu ms/frame\n", \
  g_phaseTicks[24], g_phaseCount[24], \
  (g_phaseTicks[24]/$per)/10, (g_phaseTicks[24]/$per)%10, \
  (g_phaseTicks[24]/g_phaseFrames)/4006
printf "phase 11 ($1A20 road draw): share=%d.%01d%%   phase  5 ($24F6 edge lists): share=%d.%01d%%\n", \
  (g_phaseTicks[11]/$per)/10, (g_phaseTicks[11]/$per)%10, \
  (g_phaseTicks[5]/$per)/10,  (g_phaseTicks[5]/$per)%10
printf "=== per-column dirty sweeps (0..39):\n"
set $i = 0
while $i < 40
  printf " %lu", g_shapeDashPerCol[$i]
  set $i = $i + 1
end
printf "\n"
detach
quit
