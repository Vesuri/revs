set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== vbi=%lu  engine $61=%02x $3C=%02x $63=%02x (speed MUST be non-zero: DRIVING)\n", g_vbiCount, mem[0x61], mem[0x3C], mem[0x63]
printf "=== takeover lines=%lu  nonzero sources=%lu   last sweep: nz=%u\n", g_plotChainLines, g_plotChainNZ, g_plotChainNZLast
printf "=== flat spans: lines=%lu paints=%lu\n", g_spanEmitLines, g_spanEmitPaints
printf "=== plot: runs=%lu cells=%lu  last %u runs / %u cells  lines %u..%u  noTarget=%lu\n", g_plotRuns, g_plotCells, g_plotRunsLast, g_plotCellsLast, g_plotLineLo, g_plotLineHi, g_plotNoTarget
detach
quit
