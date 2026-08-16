# ⭐ Is the raster-band record reuse actually firing?  A NON-PROBES reader, deliberately.
#
# Build: cd amiga && make clean && make -j4 FPSCOUNT=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=bandstate.gdb ./diag_run.sh 40
#
# band_prof.gdb needs a PROBES build, and a PROBES build is not the one whose framerate anyone
# quotes.  So this exists to answer one question about the SHIPPING binary: did the fast path run?
# An A/B switch that cannot report its own state is how a control build once got measured as the
# test build (docs/method-lessons.md), and "skipped 0 with a correct fast path" is indistinguishable
# from "the change works" in every byte-differential this repo has.
#
# Expect RUN small, SKIPPED ~97% of fields, overflow 0, and rejects small — rejects counts fields
# where RevsScreen refused the record (g_bandCount != 5), which reuse must never cause.
# `make BANDSKIP=0` must read SKIPPED=0 here; if it does not, the control did not build.
#
# ⭐ --warp_mode=1 runs FS-UAE ~5x faster than real time and does NOT disturb any of this: every
# number here and in fps_series.gdb is a ratio of EMULATED quantities (painted frames per vblank,
# beam ticks per phase), so host wall-clock speed cancels.  Verified by running the same build with
# and without warp: identical per-segment FPS at matched vbi.
set pagination off
set confirm off
continue
printf "vbi=%u  band cycles RUN=%lu  SKIPPED=%lu  horizon moves=%lu  rejects=%lu overflow=%u\n", \
  g_vbiCount, g_bandRuns, g_bandSkips, g_bandHorizonMoves, g_bandRejects, g_bandOverflow
detach
quit
