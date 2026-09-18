# ⭐⭐⭐ `make SPANPAINT=1` — STAGE A's ORACLE AND ITS ONE DESIGN NUMBER.
#
# The takeover's non-flat line painted from the GAME'S OWN ROAD RECORD (`surface_edge_0..3[line]`
# through `view_span_line`) instead of from the forty `$80`-spaced cell chains.  `revs_plot_spans`
# fills a group of four cells with ONE longword pair wherever the carried byte already equals the
# run's colour — a coarse test the span record STATES rather than tests, which is the whole reason
# the shape pays (a group-of-four-CELLS test that has to re-derive the condition prices at zero;
# `revs_plot_chain`'s own header carries that arithmetic).
#
# THE NUMBER THE DESIGN TURNS ON:
#   wide/group = g_plotSpansWide / (g_plotSpansLines * 10)   — 8 of 10 groups PREDICTED, from the
#     host span histogram (76% of line paints are a single colour span) and the 1.95 events/line
#     the composite model measured.  Much less than that is a workload with more road boundaries
#     in it, not a broken painter; ~0 means the wholesale arm is never selected and the painter is
#     doing the chain's work with extra tests on top.
#   nz/line    = g_plotSpansNZ / g_plotSpansLines           — the EVENTS: producer-written cells
#     the overlay must write and carry.  The host model measured 1.95; far above that means the
#     sources are not being consumed and every cell is taking the slow arm.
#
# CORRECTNESS (needs SPANVERIFY=1 DIRECTCHECK=1): the chain still runs and mirrors into mem[], so
# mem[] is a valid reference and `g_plotMismatch` must be 0 over a whole frame.  ⚠ With checks=0
# the zero mismatch means NOTHING — the window is the measurement.
# ⚠ The EXACTNESS of the painter's RULE is settled on the host, not here: `make SHAPE=1`'s
# composite model reads 0 misses in 1 280 208 cells.  This oracle covers the CODE.
#
# ⚠ DRIVING, not parked: a static scene's sources are clean everywhere and its road is straight,
# so a parked run reports a flattering wholesale rate for a scene nobody drives through
# ($63 below must be non-zero).
#
# Run (the oracle):
#   . ./env.sh && make clean && make SPANFILL=5 SPANPAINT=1 SPANVERIFY=1 DIRECTCHECK=1 \
#       STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBES=1 FIXED_RNG=1
#   EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=span_paint.gdb ./diag_run.sh 30
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "=== vbi=%u  bodyTicks=%lu\n", g_vbiCount, g_bodyTicks
printf "=== STAGE A: lines painted from the road record %lu  events %lu  runs %lu  wholesale groups %lu\n", \
  g_plotSpansLines, g_plotSpansNZ, g_plotSpansRuns, g_plotSpansWide
printf "=== SPAN EMIT: lines emitted as one span %lu   lines that ran the chain %lu\n", \
  g_spanEmitLines, g_spanEmitPaints
printf "=== plot: runs=%lu cells=%lu   last sweep: %u runs / %u cells   lines %u..%u\n", \
  g_plotRuns, g_plotCells, g_plotRunsLast, g_plotCellsLast, g_plotLineLo, g_plotLineHi
printf "=== dropped for want of a target: %lu\n", g_plotNoTarget
printf "=== ORACLE checks=%lu mismatch=%lu firstOff=%u   (mismatch MUST be 0)\n", \
  g_plotChecks, g_plotMismatch, g_plotMismatchOff
printf "=== engine $61=%02x $3C=%02x $63=%02x  (speed must be non-zero)\n", \
  mem[0x61], mem[0x3c], mem[0x63]
detach
quit
