# ⭐⭐ THE RUN CENSUS OF view_paint_lines' SWEEP — what a SOURCE-EVENT consumer must visit.
#
# src/platform/shape.h §THE RUN CENSUS has the reasoning; docs/open-work.md item 1 is the lever.
# The per-LINE skip this project already shipped measured -0.4%, because phases 2 and 3 skip zero
# lines.  One granularity down is the CELL, and `view_consume`'s structure is why that is not
# hopeless: a non-zero source means "new colour here", a zero source means "same as my left", so
# a painted line is a RUN-LENGTH ENCODED colour and the store is idempotent wherever the cell
# already holds that colour.  A consumer driven by source events must visit a cell only if
#   (a) its source is non-zero — consume it, ZERO it, update the carried byte; or
#   (b) its store would change the byte already there — the picture genuinely moves.
# UNION is the size of (a) ∪ (b) and RUNS is how clustered it is.  Both are needed: forty
# scattered singletons is the present loop with extra book-keeping.
#
# ⚠⚠ WHY THIS RUNS ON THE TARGET EVEN THOUGH THE HOST CAN COUNT IT.  The host drains ONE 50 Hz
# body tick per main-loop frame; the target drains ~10 per painted frame, so the target's car
# advances ~10x further between consecutive paints and the CHANGED half — a frame-to-frame
# delta — is larger here than any host run can show.  The host's 11% union is therefore the
# optimistic bound and this is the honest one.  (EVENTS should be trajectory-independent: the
# road pass plots the same number of points whatever the speed.)
#
# ⚠⚠ AND THE CAR MUST BE MOVING.  A parked car repaints the same picture, so `changed` collapses
# to ~0 and the census says "skip everything" — true of the static scene, false of the game.
# The engine state is printed beside the numbers so the two can never be separated; $63 must be
# non-zero or every number below is fiction.
#
# Build: cd amiga && make clean && make SHAPE=1 FPSCOUNT=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=run_census.gdb ./diag_run.sh 30
set pagination off
set confirm off
continue

printf "=== vbi=%u  sweeps=%lu\n", g_vbiCount, g_shapeRunSweeps
printf "=== engine: $3C(revs)=%02x $61(engine)=%02x $63(speed)=%02x $40(gear)=%02x $58=%02x\n", \
  mem[0x3c], mem[0x61], mem[0x63], mem[0x40], mem[0x58]
printf "=== arm latch LOST (must be 0): %lu\n", g_shapeRunArmLost
set $n = g_shapeRunSweeps
set $st = g_shapeRunStores
if $n > 0 && $st > 0
  set $lines = g_shapeRunLinesAny + g_shapeRunLinesNone
  printf "stores/sweep = %lu   line paints/sweep = %lu\n", $st / $n, $lines / $n
  printf "events  (source non-zero)      : %8lu = %2lu%% of stores\n", \
    g_shapeRunEvents, g_shapeRunEvents * 100 / $st
  printf "changed (store moved the byte) : %8lu = %2lu%%\n", \
    g_shapeRunChanged, g_shapeRunChanged * 100 / $st
  printf "UNION   (must-visit cells)     : %8lu = %2lu%%  -> %lu.%02lu per line paint\n", \
    g_shapeRunUnion, g_shapeRunUnion * 100 / $st, \
    g_shapeRunUnion / $lines, (g_shapeRunUnion * 100 / $lines) % 100
  printf "in %lu contiguous RUNS (%lu.%02lu per line paint, %lu.%02lu cells per run)\n", \
    g_shapeRunRuns, g_shapeRunRuns / $lines, (g_shapeRunRuns * 100 / $lines) % 100, \
    g_shapeRunUnion / g_shapeRunRuns, (g_shapeRunUnion * 100 / g_shapeRunRuns) % 100
  printf "producer-EXTENT scan would visit %lu = %lu%% (first..last union cell)\n", \
    g_shapeRunExtent, g_shapeRunExtent * 100 / $st
  printf "line paints needing NOTHING: %lu of %lu\n", g_shapeRunLinesNone, $lines
  printf "union cells per line paint (0,1,2,3,4,5-8,9-16,17-32,33-40):"
  set $i = 0
  while $i < 9
    printf " %lu", g_shapeRunUnionHist[$i]
    set $i = $i + 1
  end
  printf "\nruns per line paint       (same buckets):"
  set $i = 0
  while $i < 9
    printf " %lu", g_shapeRunRunsHist[$i]
    set $i = $i + 1
  end
  printf "\nunion cells per source line (per sweep):"
  set $x = 3
  while $x <= 0x4F
    if g_shapeRunPerUnion[$x] > 0
      printf " $%02x:%lu", $x, g_shapeRunPerUnion[$x] / $n
    end
    set $x = $x + 1
  end
  printf "\n"
end
