# ⭐⭐⭐ THE SPAN EMITTER — §10j step 1, the cheap checkpoint on the span architecture.
#
# docs/span-render-plan.md.  A scan line that no producer wrote and that carries no
# planted stop is ONE run of its background byte, so the sweep emits it as a single span straight
# into the bitplanes instead of running forty store units.  Two questions, and they need two
# different builds:
#
#   1. IS IT RIGHT?  (`SPANEMIT=1 SPANVERIFY=1 DIRECTCHECK=1`)  Verify mode keeps the chain
#      running, so mem[] stays true and the shipping decode is a valid reference, and turns the
#      per-unit run accumulator OFF so every plane byte on an emitted line comes from the emitter
#      alone.  g_plotMismatch must be 0 with g_plotChecks large.
#      ⚠⚠ WITHOUT SPANVERIFY THE ORACLE IS MEANINGLESS IN BOTH DIRECTIONS: with PLOT_ONLY's
#      carve-out mem[] goes stale and the reference is last frame's picture; with the accumulator
#      left on the chain re-plots the same forty cells over the span, so a WRONG span compares
#      equal.  A green check from a plain `SPANEMIT=1 DIRECTCHECK=1` build proves nothing.
#
#   2. WHAT DOES IT BUY?  (`SPANEMIT=1 PROBES=1`, no verify)  g_spanEmitLines vs g_spanEmitPaints
#      is the A/B switch printing its own state — a build that emits nothing reads identically to
#      an emitter that buys nothing.  The census predicts ~20 of phase 1's 36 lines qualify, so
#      phase 1's 22 ms should fall towards ~12.  Read phase 24's row from phase4_prof.gdb.
#
# ⚠ DRIVING, not parked: a static scene's sources are clean everywhere, so a parked run reports a
# flattering emit rate for a scene nobody drives through ($63 below must be non-zero).
#
# Run (the oracle):
#   . ./env.sh && make clean && make SPANEMIT=1 SPANVERIFY=1 DIRECTCHECK=1 \
#       STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBES=1 FIXED_RNG=1
#   EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=span_emit.gdb ./diag_run.sh 120
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "=== vbi=%u  bodyTicks=%lu\n", g_vbiCount, g_bodyTicks
printf "=== SPAN EMIT: lines emitted as one span %lu   lines that ran the chain %lu\n", \
  g_spanEmitLines, g_spanEmitPaints
printf "=== plot: runs=%lu cells=%lu   last sweep: %u runs / %u cells   lines %u..%u\n", \
  g_plotRuns, g_plotCells, g_plotRunsLast, g_plotCellsLast, g_plotLineLo, g_plotLineHi
printf "=== dropped for want of a target: %lu\n", g_plotNoTarget
printf "=== ORACLE checks=%lu mismatch=%lu firstOff=%u   (mismatch MUST be 0)\n", \
  g_plotChecks, g_plotMismatch, g_plotMismatchOff
# The stimulus, because a parked car repaints a static scene and proves much less.
printf "=== engine $61=%02x $3C=%02x $63=%02x  (speed must be non-zero)\n", \
  mem[0x61], mem[0x3c], mem[0x63]
detach
quit
