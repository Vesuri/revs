# ⭐⭐ WHAT IS THE HALF OF phase 27 THAT CONVERTS NOTHING?  (src/platform/probe.h §DECODESPLIT)
#
# The 208-row ledger (docs/span-render-plan.md §11) prices the decode by ROWS — `make
# VIEWCARVE=<lo>-<hi>` claims a block of display lines and ph27's delta is that block's
# conversion cost.  Summed over the whole picture the blocks account for only HALF the row:
# carving all 208 still leaves ~12 ms and stubbing decode() outright leaves 0.13, so ~12 ms is
# work inside decode() that converts nothing and NO amount of ownership can reach it.
#
# Build: cd amiga && make clean && make -j4 DECODESPLIT=1 PROBES=1 FIXED_RNG=1 \
#                                          STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBEFIELDS=3000
# ⚠ SIX brackets a frame now, so the control (52) is subtracted from each row before quoting.
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=decodesplit.gdb ./diag_run.sh 45
#
# ⚠⚠ READ THE CONTROL (52) FIRST.  Its bracket contains NOTHING, so its ticks are one
# transition's own cost; three transitions a frame means every row below is inflated by about
# `calls x that`.  Correct before quoting, exactly as roadsplit.gdb's phase 49 requires.
# ⚠ SHARES WITHIN ONE RUN — never diff across builds (docs/perf-method.md Rule 2).
set pagination off
set confirm off
continue
printf "=== vbi=%u loopFrames=%lu frozen=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_probeFrozen

set $dec = g_phaseTicks[27] + g_phaseTicks[50] + g_phaseTicks[51] + g_phaseTicks[52] \
         + g_phaseTicks[53] + g_phaseTicks[54] + g_phaseTicks[55]
set $per = $dec / 1000
if $per == 0
  set $per = 1
end
set $f = g_phaseFrames
if $f == 0
  set $f = 1
end
printf "\nRevsScreen::decode() whole = %lu.%02lu ms/frame  (27 + brackets 50-54)\n", \
  ($dec/$f)/4006, (($dec/$f)%4006)*100/4006
printf "  %-30s %3lu.%02lu ms/frame  %2d.%01d%%  (calls=%lu)  <- THE CONTROL, read first\n", \
  "EMPTY bracket        (52)", \
  (g_phaseTicks[52]/$f)/4006, ((g_phaseTicks[52]/$f)%4006)*100/4006, \
  (g_phaseTicks[52]/$per)/10, (g_phaseTicks[52]/$per)%10, g_phaseCount[52]
printf "  %-30s %3lu.%02lu ms/frame  %2d.%01d%%  (calls=%lu)\n", \
  "snapshotBands        (50)", \
  (g_phaseTicks[50]/$f)/4006, ((g_phaseTicks[50]/$f)%4006)*100/4006, \
  (g_phaseTicks[50]/$per)/10, (g_phaseTicks[50]/$per)%10, g_phaseCount[50]
printf "  %-30s %3lu.%02lu ms/frame  %2d.%01d%%  (calls=%lu)  <- SURVIVES the end state\n", \
  "buildLineModes       (53)", \
  (g_phaseTicks[53]/$f)/4006, ((g_phaseTicks[53]/$f)%4006)*100/4006, \
  (g_phaseTicks[53]/$per)/10, (g_phaseTicks[53]/$per)%10, g_phaseCount[53]
printf "  %-30s %3lu.%02lu ms/frame  %2d.%01d%%  (calls=%lu)\n", \
  "own/carve loops      (54)", \
  (g_phaseTicks[54]/$f)/4006, ((g_phaseTicks[54]/$f)%4006)*100/4006, \
  (g_phaseTicks[54]/$per)/10, (g_phaseTicks[54]/$per)%10, g_phaseCount[54]
printf "  %-30s %3lu.%02lu ms/frame  %2d.%01d%%  (calls=%lu)  <- §12c, DASHOWN only\n", \
  "dynamic rectangles   (55)", \
  (g_phaseTicks[55]/$f)/4006, ((g_phaseTicks[55]/$f)%4006)*100/4006, \
  (g_phaseTicks[55]/$per)/10, (g_phaseTicks[55]/$per)%10, g_phaseCount[55]
printf "  %-30s %3lu.%02lu ms/frame  %2d.%01d%%  (calls=%lu)\n", \
  "convertRace          (51)", \
  (g_phaseTicks[51]/$f)/4006, ((g_phaseTicks[51]/$f)%4006)*100/4006, \
  (g_phaseTicks[51]/$per)/10, (g_phaseTicks[51]/$per)%10, g_phaseCount[51]
printf "  %-30s %3lu.%02lu ms/frame  %2d.%01d%%\n", \
  "phase 27 remainder", \
  (g_phaseTicks[27]/$f)/4006, ((g_phaseTicks[27]/$f)%4006)*100/4006, \
  (g_phaseTicks[27]/$per)/10, (g_phaseTicks[27]/$per)%10

# The conversion's own census, so the ms have a denominator (src/platform/amiga/RevsScreen.cpp).
printf "\ncells converted/frame = %lu   (max %u, full frames %lu)\n", \
  g_decodeCellsTotal/$f, g_decodeCellsMax, g_decodeFullFrames
printf "owned lines (last frame) = %u   flat-band lines = %u in %u bands\n", \
  g_decodeOwnLines, g_decodeFlatLines, g_decodeFlatBands
