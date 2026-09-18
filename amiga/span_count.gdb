# ⭐ THE DENOMINATOR, and nothing else.  Stage A's A/B sized both halves per FRAME; the decision
# needs them per LINE PAINTED, and the wholesale rate in a build where the painter CONSUMES
# (the oracle's 72% was measured with consumption suppressed).
set pagination off
set confirm off
continue
printf "=== loopFrames=%lu frozen=%lu build=%lx\n", g_phaseFrames, g_probeFrozen, g_probeBuildFlags
printf "=== span-painted lines %lu   flat-span lines %lu   chain lines %lu\n", \
  g_plotSpansLines, g_spanEmitLines, g_plotChainLines
printf "=== events %lu   runs %lu   wholesale groups %lu\n", \
  g_plotSpansNZ, g_plotSpansRuns, g_plotSpansWide
printf "=== engine $61=%02x $63=%02x\n", mem[0x61], mem[0x63]
detach
quit
