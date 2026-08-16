# ⭐⭐ THE DIRECT-TO-BITPLANE RUN PLOTTER — does it paint what the decode would?
#
# docs/direct-bitplane-plan.md §7e.  $7BE2 paints the 3D viewport, `A` carries between its units,
# so a scan line is one to three RUNS of identical bytes — contiguous in the Amiga's layout, strided
# by 8 in the BBC's.  This is the instrument for both halves of that claim:
#
#   1. THE SHAPE.  g_plotRuns / g_plotCells is the collapse ratio, and it is the whole point: cells
#      per run near 1 means there are no runs and the idea is dead; near 14 means ~2100 per-cell
#      iterations can become ~150 fills.  g_plotLineLo/Hi must bracket the viewport (80..157) — a
#      plotter aimed at the wrong lines would still show a healthy ratio.
#   2. THE ORACLE (needs DIRECTCHECK=1, or g_plotChecks stays 0 and the zero mismatch means
#      nothing).  Each sweep is bracketed: convert mem[] with the shipping decode and seed the
#      buffer, let the sweep plot over it, convert again, require the WHOLE buffer to match.
#      g_plotMismatch must be 0.
#
# ⚠ g_plotNoTarget counts runs thrown away for want of a buffer (before the first decode, or in
# MODE 7).  A large value means the measurement above describes almost nothing.
#
# Run (the oracle — two full conversions per sweep, so never quote FPS from it):
#   . ./env.sh && make clean && make DIRECTPLOT=1 DIRECTCHECK=1 STRAIGHT_TO_RACE=1 PROBES=1 FIXED_RNG=1
#   GDBSCRIPT=direct_plot.gdb ./diag_run.sh 150
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "=== vbi=%u  bodyTicks=%lu\n", g_vbiCount, g_bodyTicks
printf "=== runs=%lu cells=%lu   last sweep: %u runs / %u cells   lines %u..%u\n", \
  g_plotRuns, g_plotCells, g_plotRunsLast, g_plotCellsLast, g_plotLineLo, g_plotLineHi
printf "=== dropped for want of a target: %lu\n", g_plotNoTarget
printf "=== ORACLE checks=%lu mismatch=%lu firstOff=%u\n", \
  g_plotChecks, g_plotMismatch, g_plotMismatchOff
# The stimulus, because a parked car repaints a static scene and proves much less.
printf "=== engine $61=%02x $3C=%02x $63=%02x\n", mem[0x61], mem[0x3c], mem[0x63]
detach
quit
