# ⭐⭐ THE §10q A/B — phase 1's own line driver (`make VIEWFULL=1`) against `paint_cells`.
#
# Build BOTH arms the same way except the one flag, and bound the window in EMULATED time
# (CLAUDE.md: `make PROBEFIELDS=N`, and `frozen=` must be non-zero and equal on both arms):
#   make clean && make -j4 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 \
#                          PROBEFIELDS=3000 SPANFILL=5 VIEWFULL=0      # the control
#   ...and the same with VIEWFULL=1                                    # the driver
#   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=vf_ab.gdb ./diag_run.sh 90
#
# ⚠ Read the output from .run/gdb-out.log, never diag_run.sh's stdout (tail -40 drops it).
#
# WHAT TO COMPARE, in order:
#   1. `frozen=`/`build=`/`ph0=` first — a differing ph0 TICK COUNT means a different
#      workload, not a faster build (docs/perf-method.md, the dilution rule).
#   2. `ph24` — phase 1 IS view_paint_lines (probe.h §32), so this is the row the driver
#      changes.  `-ph28` is the frame size (a compute win partly reappears as the vblank spin).
#   3. The CENSUS: lines/units must be IDENTICAL across the arms.  A moved row with a moved
#      census is a different trajectory, not a shape win.
#   4. `stop-hoist DISAGREEMENTS` under `VIEWFULLCHECK=1` — the driver hoists `view_stop_from`
#      out of the line loop, and this counter is the ONLY gate on that (the host never runs
#      the driver, a cross-run picture diff is invalid, and SPANVERIFY needs the very chain
#      the driver deletes).  It must be 0 with `lines` at ~36 a frame.
set pagination off
set confirm off
continue
set $f = g_phaseFrames
if $f == 0
  set $f = 1
end
set $tot = 0
set $i = 1
while $i < 40
  set $tot = $tot + g_phaseTicks[$i]
  set $i = $i + 1
end
set $net = $tot - g_phaseTicks[28]
printf "=== vbi=%u loopFrames=%lu brk=%lu smc=%lu frozen=%lu build=%lx ===\n", \
  g_vbiCount, g_phaseFrames, g_brkCount, g_smcUnhandled, g_probeFrozen, g_probeBuildFlags
printf "  ph0=%lu (holds=%lu)   %lu fields\n", g_phaseTicks[0], g_phaseTicks[0]/80120, g_vbiCount
printf "  FRAME Sigma(1..39)=%lu.%02lu ms   -ph28=%lu.%02lu ms\n", \
  ($tot/$f)/4006, ((($tot/$f)%4006)*100)/4006, ($net/$f)/4006, ((($net/$f)%4006)*100)/4006
printf "  ph24 view_paint_lines=%lu.%02lu ms   ph27 decode=%lu.%02lu ms   ph32 tail=%lu.%02lu ms\n", \
  (g_phaseTicks[24]/$f)/4006, (((g_phaseTicks[24]/$f)%4006)*100)/4006, \
  (g_phaseTicks[27]/$f)/4006, (((g_phaseTicks[27]/$f)%4006)*100)/4006, \
  (g_phaseTicks[32]/$f)/4006, (((g_phaseTicks[32]/$f)%4006)*100)/4006
printf "  census/frame: lines %lu/%lu/%lu  units %lu/%lu/%lu\n", \
  g_probeFrozenLines[0]/$f, g_probeFrozenLines[1]/$f, g_probeFrozenLines[2]/$f, \
  g_probeFrozenUnits[0]/$f, g_probeFrozenUnits[1]/$f, g_probeFrozenUnits[2]/$f
# ⚠⚠ RAW TOTALS, NOT PER FRAME, AND THAT IS NOT A SHORTCUT: these three are NOT in the freeze
# snapshot (probe.cpp freezes g_phaseTicks / g_phaseFrames / g_probeFrozen{Units,Runs,Lines} and
# nothing else), so under PROBEFIELDS they keep climbing after the window shuts.  Dividing a LIVE
# numerator by the FROZEN g_phaseFrames is CLAUDE.md's "plausible lie" with the roles swapped —
# it read 274 span lines a frame where the true rate, from an unbounded run with both counters
# live, is 36.0.  For a per-frame rate run WITHOUT PROBEFIELDS and divide by that run's frames.
# ⚠ The two span counters also cover all three phases and five arms of `paint_cells`, so they are
# not an arm-split census either; the arm-comparable census is g_probeFrozenLines/Units above.
printf "  raw (LIVE, not /frame): span-arm lines=%lu  chain lines=%lu\n", \
  g_spanEmitLines, g_spanEmitPaints
printf "  VIEWFULL raw (LIVE): driver lines=%lu  stop-hoist DISAGREEMENTS=%lu\n", \
  g_viewFullLines, g_viewFullStopBad
detach
quit
