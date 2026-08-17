# ⭐⭐ THE PER-LINE CENSUS OF $7BE2's SWEEP — what it would be allowed to SKIP.
#
# src/platform/shape.h §THE PER-LINE CENSUS has the reasoning.  The unit loop is ~54 ms of the
# 84 ms row (`make VIEWSPLIT=1`) and it is bus bound at ~180 cycles a unit, so the only lever
# left is running FEWER units — and the unit of skipping is a scan line.  Three counts decide it:
#
#   REDUNDANT      units on lines where every store wrote the byte already there.  The prize.
#   CLEAN SOURCES  units on lines with no dirty source at sweep entry.  What a PRODUCER-side
#                  dirty flag could actually detect.
#   disagreement   lines where those two answers differ — if there are many, a dirty flag is
#                  not a sufficient predicate for the skip.
#
# ⚠⚠ THE CAR MUST BE MOVING.  A parked or stalled car repaints the same picture, so EVERY line
# reads redundant and the census says "skip everything" — which is true of the static scene and
# false of the game.  The host cannot produce the moving scene (its autorun never selects a gear,
# $63 stays 0), which is why this runs on the target.  The engine state is printed beside the
# numbers so the two can never be separated.
#
# Build: cd amiga && make clean && make SHAPE=1 FPSCOUNT=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=view_census.gdb ./diag_run.sh 30
set pagination off
set confirm off
continue

printf "=== vbi=%u  sweeps=%lu\n", g_vbiCount, g_shapeLineSweeps
printf "=== engine: $3C(revs)=%02x $61=%02x $63(gear)=%02x $58(gear key)=%02x $2D=%02x\n", \
  mem[0x3c], mem[0x61], mem[0x63], mem[0x58], mem[0x2d]
set $n = g_shapeLineSweeps
if $n > 0
  set $u = g_shapeLineUnits
  printf "lines painted/sweep = %lu   units/sweep = %lu\n", g_shapeLineVisited / $n, $u / $n
  printf "REDUNDANT     : %lu lines/sweep, %lu of %lu units = %lu%% of the scan\n", \
    g_shapeLineRedundant / $n, g_shapeLineUnitsRedundant, $u, \
    g_shapeLineUnitsRedundant * 100 / $u
  printf "CLEAN SOURCES : %lu lines/sweep, %lu units = %lu%%\n", \
    g_shapeLineCleanSrc / $n, g_shapeLineUnitsCleanSrc, g_shapeLineUnitsCleanSrc * 100 / $u
  printf "disagreement  : clean-but-changed %lu, dirty-but-unchanged %lu (lines, summed)\n", \
    g_shapeLineCleanButChanged, g_shapeLineDirtyNoChange
  printf "per line: line visits redundant units/visit\n"
  set $x = 3
  while $x <= 0x4F
    if g_shapeLinePerVisit[$x] > 0
      printf "  $%02x  %6lu %6lu  %3lu\n", $x, g_shapeLinePerVisit[$x], \
        g_shapeLinePerRedundant[$x], g_shapeLinePerUnits[$x] / g_shapeLinePerVisit[$x]
    end
    set $x = $x + 1
  end
end
