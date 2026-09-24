# ⭐⭐ THE TERRAIN PAINTER IN 68000 ASM — ITS TWO ORACLES AND ITS TWO ASSUMPTIONS
# (`make TERRAINCHECK=1 LOWFULLCHECK=1 PROBES=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`, TERRAINASM=1).
#   TERRAIN  every painted line, cell by cell, against its event list (RevsPlot.cpp §terrain_check_line)
#   LOWFULL  display lines 117..157 against the run painter on a POISONED buffer
#   rowBad   a line whose display row is not the contiguous one the asm walked to — MUST be 0
#   fallback blocks handed to the C painter — reads 0 (the drivers step one row a line)
# ⚠ `checks` must be LARGE on both: zero is a vacuous pass.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 2500
continue
printf "=== TERRAIN checks=%lu mismatch=%lu at=%04x rowBad=%lu fallback=%lu (vbi=%u)\n", \
  g_terrainChecks, g_terrainMismatch, g_terrainMismatchAt, g_terrainRowBad, \
  g_terrainAsmFallback, g_vbiCount
printf "=== LOWFULL checks=%lu mismatch=%lu at=line %u cell %u noCock=%lu\n", \
  g_lowFullChecks, g_lowFullMismatch, g_lowFullMismatchAt >> 8, g_lowFullMismatchAt & 0xFF, \
  g_lowFullNoCock
printf "verdict: %s\n", \
  (g_terrainChecks == 0 || g_lowFullChecks == 0) ? "VACUOUS — a check never ran" : \
  ((g_terrainMismatch || g_terrainRowBad || g_terrainAsmFallback || g_lowFullMismatch) ? "FAIL" : "PASS")
kill
quit
