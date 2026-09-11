# ⭐ DID THE SKIP ACTUALLY FIRE?  The control for any VIEWSKIP measurement.
#
# A `VIEWSKIP=1` build that quietly skips nothing reads exactly like a build whose skip buys
# nothing, and the second conclusion is the expensive one to get wrong.  Run this against the SAME
# binary the framerate came from and require a skipped:painted ratio near the host census (39%
# driving).  docs/direct-bitplane-plan.md §7i.
#
# ⚠ VIEWSKIP builds only — the counters do not exist otherwise.
# Build: make clean && make VIEWSKIP=1 STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=viewskip_count.gdb ./diag_run.sh 30
set pagination off
set confirm off

continue
printf "VIEWSKIP: skipped=%lu painted=%lu\n", g_viewSkipLines, g_viewSkipPaints
printf "vbi=%u frames=%lu\n", g_vbiCount, g_fpsFrames
detach
quit
