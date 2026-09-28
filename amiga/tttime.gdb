# ⭐ THE MODE 7 FRONT END'S DECODE COST, in RASTER LINES (64 us each), measured in-program.
# Build: make clean && make TTTIME=1 [COMPETITION=1 for a walk through the menus]
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=tttime.gdb ./diag_run.sh 30
# Counts only decodes that PAINTED a cell; `cells` is the last one's painted-cell count.
# ⚠ Read in-program because a gdb `finish` through FS-UAE's stub reads the same beam delta for a
#   full-page decode as for an empty one.
set pagination off
set confirm off
continue
printf "TT last=%u lines (%u cells)  max=%u  total=%u over %u painting decodes  vbi=%u\n", \
  g_ttTimeLast, g_ttTimeCells, g_ttTimeMax, g_ttTimeTotal, g_ttTimeCalls, g_vbiCount
printf "TT worst: %u lines = decode %u over %u rows + the rest, %u cells painted\n", \
  g_ttTimeMax, g_ttTimeMaxDecode, g_ttTimeMaxRows, g_ttTimeMaxCells
detach
quit
