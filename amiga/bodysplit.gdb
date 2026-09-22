# ⭐⭐ WHAT IS THE 50 Hz DRAIN SPENDING 12.5 ms A FRAME ON?  (src/platform/probe.h §BODYSPLIT)
#
# phases 26+29 are the third-largest block in the frame and the only one that never had an
# instrument.  ⚠⚠ THE ROW IS A PRODUCT: the drain runs ceil(frame/20ms) band cycles per painted
# frame, so ph26 = (cycles/frame) x (cost/cycle) and a faster frame shrinks it on its own.
# ⇒ QUOTE THE PER-CYCLE us COLUMN, never the ms/frame one, when sizing a change here.
#
# Build: cd amiga && make clean && make -j4 BODYSPLIT=1 PROBES=1 FIXED_RNG=1 \
#                                          STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=bodysplit.gdb ./diag_run.sh 60
#
# ⚠⚠ READ THE CONTROL (61) FIRST — its bracket holds nothing, so its ticks ARE one transition's
# cost, and 62 (two stores) is a second control that must land near it or the split is lying.
# ⚠ SHARES WITHIN ONE RUN — never diff across builds (docs/perf-method.md Rule 2).
set pagination off
set confirm off
continue
printf "=== vbi=%u loopFrames=%lu frozen=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_probeFrozen

set $f = g_phaseFrames
if $f == 0
  set $f = 1
end
# body ticks: the frozen shadow when the window has closed (probe.cpp PARTIAL-FREEZE TRAP)
set $bt = g_bodyTicks
if g_probeFrozen != 0
  set $bt = g_probeFrozenBody
end
if $bt == 0
  set $bt = 1
end
set $drain = g_phaseTicks[26] + g_phaseTicks[29] + g_phaseTicks[59] + g_phaseTicks[60] \
           + g_phaseTicks[61] + g_phaseTicks[62]

printf "\nbody ticks = %lu over %lu frames = %lu.%02lu cycles/frame\n", \
  $bt, $f, $bt/$f, (($bt%$f)*100)/$f
printf "THE DRAIN whole (26+29+59..62) = %lu.%02lu ms/frame = %lu us per band cycle\n\n", \
  ($drain/$f)/4006, (($drain/$f)%4006)*100/4006, ($drain/$bt)/4
printf "%-34s %7s %10s  %s\n", "part", "ms/frm", "us/cycle", "calls"
printf "%-34s %3lu.%02lu %9lu  %lu\n", "EMPTY control        (61)", \
  (g_phaseTicks[61]/$f)/4006, ((g_phaseTicks[61]/$f)%4006)*100/4006, \
  (g_phaseTicks[61]/$bt)/4, g_phaseCount[61]
printf "%-34s %3lu.%02lu %9lu  %lu\n", "bbc_begin_band_cycle (62)", \
  (g_phaseTicks[62]/$f)/4006, ((g_phaseTicks[62]/$f)%4006)*100/4006, \
  (g_phaseTicks[62]/$bt)/4, g_phaseCount[62]
printf "%-34s %3lu.%02lu %9lu  %lu\n", "reuse GATE, 43 bytes (59)", \
  (g_phaseTicks[59]/$f)/4006, ((g_phaseTicks[59]/$f)%4006)*100/4006, \
  (g_phaseTicks[59]/$bt)/4, g_phaseCount[59]
printf "%-34s %3lu.%02lu %9lu  %lu\n", "the game's IRQ1V chain (60)", \
  (g_phaseTicks[60]/$f)/4006, ((g_phaseTicks[60]/$f)%4006)*100/4006, \
  (g_phaseTicks[60]/$bt)/4, g_phaseCount[60]
printf "%-34s %3lu.%02lu %9lu  %lu\n", "tick_wheel_spin      (29)", \
  (g_phaseTicks[29]/$f)/4006, ((g_phaseTicks[29]/$f)%4006)*100/4006, \
  (g_phaseTicks[29]/$bt)/4, g_phaseCount[29]
printf "%-34s %3lu.%02lu %9lu  %lu\n", "remainder            (26)", \
  (g_phaseTicks[26]/$f)/4006, ((g_phaseTicks[26]/$f)%4006)*100/4006, \
  (g_phaseTicks[26]/$bt)/4, g_phaseCount[26]
printf "
(us/cycle = ticks/body-cycle / 4.006 ticks-per-us; subtract the control from each)
"
printf "\ncontrol-correct every row: subtract 61 before quoting it\n"
