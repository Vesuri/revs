# ⭐⭐ WHY IS draw_road (phase 11) ~16% of the frame?  (src/platform/probe.h §ROADSPLIT)
# ⚠⚠ TWO INSTRUMENT COSTS, AND NEITHER IS FREE.  (1) EVERY BRACKET TRANSITION COSTS ~107 us
# (758 cycles) — bracket 49 measures it directly, because 49's bracket contains NOTHING.  So
# correct every row by `instances x 107 us` before quoting it; doing that drives 49 itself to
# -0.4 ms ~ 0, which is the verification.  (2) `ROAD_COUNT` is a volatile 32-bit RMW (~40 cycles)
# and the DDA-step counter fires 232 times a frame, so even a floor-corrected sum over-reads a
# plain PROBES=1 phase 11 by ~15%.  ⇒ READ ROADSPLIT FOR THE RATIO, phase4_prof FOR THE ABSOLUTE.
#
# Build: cd amiga && make clean && make -j4 ROADSPLIT=1 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=roadsplit.gdb ./diag_run.sh 30
#        cat .run/gdb-out.log
#
# TWO decompositions of the phase-11 row, in one run:
#   TIME   — stage sub-phases 44-46 (fill_line_attr / draw_surface_spans / mark_line_surfaces),
#            summed over both road sides, + the clamp/driver remainder left in phase 11.  These
#            carve phase 11, so 11+44+45+46 IS the old row.
#   COUNTS — the leaf tallies: spans handed to the rasteriser (interp_edge), DDA scan lines
#            (span_walk), columns merged (road_span_plot — three bus accesses each), lines named
#            in the line->point map (fill_line_attr), points stamped (mark_line_surfaces).  The
#            span rasteriser is (spans) x (lines/span) x (columns/line); this is the "why".
#
# ⚠ SHARES WITHIN ONE RUN — never diff across builds (docs/perf-method.md Rule 2).  PROBES build:
# no framerate may be quoted from it.  Confirm the car is DRIVING (amiga/dash_state.gdb) — parked,
# the road pass is ~8x lighter and this table means something else.
set pagination off
set confirm off
continue
printf "=== vbi=%u loopFrames=%lu roadFrames=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_roadFrames

# --- TIME: the phase-11 stage sub-phases ------------------------------------------------------
set $road = g_phaseTicks[11] + g_phaseTicks[44] + g_phaseTicks[45] + g_phaseTicks[46] + g_phaseTicks[47] + g_phaseTicks[48] + g_phaseTicks[49]
set $per = $road / 1000
if $per == 0
  set $per = 1
end
printf "\ndraw_road (phase 11 whole) = %lu ms/frame  (11 + brackets 44-47)\n", \
  ($road/g_phaseFrames)/4006
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "fill_line_attr  (44)", \
  (g_phaseTicks[44]/g_phaseFrames)/4006, (g_phaseTicks[44]/$per)/10, (g_phaseTicks[44]/$per)%10, g_phaseCount[44]
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "  span SETUP+driver (45)", \
  (g_phaseTicks[45]/g_phaseFrames)/4006, (g_phaseTicks[45]/$per)/10, (g_phaseTicks[45]/$per)%10, g_phaseCount[45]
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "  span_walk DDA arms (47)", \
  (g_phaseTicks[47]/g_phaseFrames)/4006, (g_phaseTicks[47]/$per)/10, (g_phaseTicks[47]/$per)%10, g_phaseCount[47]
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "    span_plot_core (48)", \
  (g_phaseTicks[48]/g_phaseFrames)/4006, (g_phaseTicks[48]/$per)/10, (g_phaseTicks[48]/$per)%10, g_phaseCount[48]
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)  <- THE CONTROL, read first\n", "    EMPTY bracket (49)", \
  (g_phaseTicks[49]/g_phaseFrames)/4006, (g_phaseTicks[49]/$per)/10, (g_phaseTicks[49]/$per)%10, g_phaseCount[49]
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass\n", "draw_surface_spans 45..49", \
  ((g_phaseTicks[45]+g_phaseTicks[47]+g_phaseTicks[48]+g_phaseTicks[49])/g_phaseFrames)/4006, \
  ((g_phaseTicks[45]+g_phaseTicks[47]+g_phaseTicks[48]+g_phaseTicks[49])/$per)/10, \
  ((g_phaseTicks[45]+g_phaseTicks[47]+g_phaseTicks[48]+g_phaseTicks[49])/$per)%10
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "mark_line_surfaces (46)", \
  (g_phaseTicks[46]/g_phaseFrames)/4006, (g_phaseTicks[46]/$per)/10, (g_phaseTicks[46]/$per)%10, g_phaseCount[46]
printf "  %-26s %6lu ms/frame  %2d.%01d%% of the pass  (clamps + return remainder)\n", "phase 11 remainder", \
  (g_phaseTicks[11]/g_phaseFrames)/4006, (g_phaseTicks[11]/$per)/10, (g_phaseTicks[11]/$per)%10

# --- COUNTS: the "why", per frame -------------------------------------------------------------
set $f = g_roadFrames
if $f == 0
  set $f = 1
end
printf "\nper frame:\n"
printf "  spans (interp_edge):        %lu\n", g_roadSpans/$f
printf "  DDA scan lines (span_walk): %lu\n", g_roadSpanLines/$f
printf "  columns (road_span_plot):   %lu   (3 bus accesses each: 2 writes + 1 read)\n", g_roadCols/$f
printf "  DDA steps (i-loop):         %lu   (8 per scan line; only a carry plots)\n", g_roadColSteps/$f
printf "  fill lines (line->point):   %lu\n", g_roadFillLines/$f
printf "  mark points (line->class):  %lu\n", g_roadMarkPts/$f
if g_roadSpans > 0
  printf "  => %lu.%01lu DDA lines per span,  %lu.%01lu columns per span\n", \
    (g_roadSpanLines*10/g_roadSpans)/10, (g_roadSpanLines*10/g_roadSpans)%10, \
    (g_roadCols*10/g_roadSpans)/10, (g_roadCols*10/g_roadSpans)%10
end
if g_roadSpanLines > 0
  printf "  => %lu.%01lu columns per DDA scan line\n", \
    (g_roadCols*10/g_roadSpanLines)/10, (g_roadCols*10/g_roadSpanLines)%10
end
# ⭐⭐ THE PER-SPAN PRICE OF EACH HALF — this is what the 45/47 split exists to print.  Setup is
# a fixed cost per span; the walk's is (lines x 8 column steps) + (plots x the out-of-line
# sw_plot_N).  Whichever number is big is the one a rewrite has to delete.
if g_roadSpans > 0
  printf "  => per span: SETUP %lu us, WALK %lu us   (cycles at 7.09 MHz: %lu / %lu)\n", \
    (g_phaseTicks[45]/g_roadSpans)*1000/4006, (g_phaseTicks[47]/g_roadSpans)*1000/4006, \
    ((g_phaseTicks[45]/g_roadSpans)*1000/4006)*709/100, ((g_phaseTicks[47]/g_roadSpans)*1000/4006)*709/100
end
if g_roadColSteps > 0
  printf "  => per DDA step (scaffolding only, 47): %lu us = %lu cycles\n", \
    (g_phaseTicks[47]/g_roadColSteps)*1000/4006, ((g_phaseTicks[47]/g_roadColSteps)*1000/4006)*709/100
end
if g_roadCols > 0
  printf "  => per plotted column: PLOT %lu us, control %lu us  =>  %lu cycles of real plot\n", \
    (g_phaseTicks[48]/g_roadCols)*1000/4006, (g_phaseTicks[49]/g_roadCols)*1000/4006, \
    (((g_phaseTicks[48]-g_phaseTicks[49])/g_roadCols)*1000/4006)*709/100
end

# --- per-column time from the span stage (the bulk of the pass) -------------------------------
# draw_surface_spans owns nearly every column, so (phase 45 ticks) / (columns) prices one
# road_span_plot with its three bus accesses — the number docs/perf-method.md asserted was small.
if g_roadFillLines > 0
  printf "fill stage: %lu ms/frame over %lu fill lines/frame  =>  ~%lu us per line->point store\n", \
    (g_phaseTicks[44]/g_phaseFrames)/4006, g_roadFillLines/$f, (g_phaseTicks[44]/g_roadFillLines)*1000/4006
end
detach
quit
