# ⭐ HOW EXPENSIVE IS ONE BBC HARDWARE ACCESS?  (`make HWTIME=1 PROBES=1`)
#
# The 50 Hz body is 51% of the frame and its five band arms are almost entirely `STA $FE20/$FE21` —
# a 4-cycle instruction on a 6502, and in this port a call through platform_hw_write into a switch.
# This counts them and times them.
#
# ⚠⚠ READ THE OBSERVER EFFECT BEFORE READING THE NUMBER.  The counters alone move the body tick
# from 10.5 ms to 12.3 ms; the timing takes it to 22.7 ms and the main loop to a crawl (5 iterations
# in 150 s).  So: use the COUNT as exact, treat the per-access microseconds as an upper bound, and
# never quote a share or a framerate from this build.  A separate flag exists for exactly this
# reason — docs/perf-method.md.
#
# Build: cd amiga && make clean && make PROBES=1 HWTIME=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=hwtime.gdb ./diag_run.sh 150
set pagination off
set confirm off
continue
printf "=== vbi=%u bodyTicks=%lu\n", g_vbiCount, g_bodyTicks
if g_bodyTicks > 0
  printf "hardware accesses: %lu writes + %lu reads = %lu per body tick\n", \
    g_probeHwWrites, g_probeHwReads, (g_probeHwWrites+g_probeHwReads)/g_bodyTicks
end
if g_probeHwWrites > 0
  printf "per write: %lu us (UPPER BOUND — includes ~18 us of instrument)\n", \
    (g_probeHwTicks/g_probeHwWrites)*1000/4006
  printf "so the seam is <= %lu ms of a %lu ms body tick\n", \
    ((g_probeHwTicks/g_bodyTicks)*1000/4006)/1000, \
    ((g_phaseTicks[26]+g_phaseTicks[29])/g_bodyTicks)*1000/4006/1000
end
detach
quit
