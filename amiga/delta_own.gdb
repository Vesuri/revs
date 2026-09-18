# ⭐⭐⭐ THE DELTA DOMAIN — display lines 0..17 + 192..207, owned outright (§11b-§11d).
#
# The renderer keeps those 34 rows itself: `revs_plot_own_reset` expands them out of mem[] into
# BOTH plane buffers once (the BASE — a text row is nearly all cells nothing rewrites), then
# `vdu_char_emit` mirrors each byte as it changes it (the DELTA), and the decode stops converting
# them.  Two things this prints, and the second is the one that matters:
#
#   1. THE SHAPE.  g_plotDeltaBases must be SMALL — one at race entry, one per MODE 7 round trip.
#      A base per sweep means `deltaBaseStale` is firing every frame and the domain is costing
#      ~14 000 stores a sweep instead of ~4.  g_plotDeltaBytes is the delta traffic (the census
#      says ~3.7 a frame for these rows), and g_decodeOwnLines must be 34 higher than a
#      DELTAOWN=0 build's.
#   2. THE STALENESS ORACLE (needs DELTACHECK=1, or g_plotDeltaChecks stays 0 and the zero mismatch
#      means NOTHING).  Once per sweep, before any re-base, it re-expands every block of
#      `kDeltaBlock` from mem[] and compares the two plane buffers against them.
#      g_plotDeltaMismatch must be 0.
#      ⚠ That is the ONE thing a delta painter on an owned row owes: it says no writer reaches
#      these rows unmirrored.  An owned row is never re-expanded, so a byte written by a routine
#      with no `REVS_PLOT_BYTE` at its store site would sit there stale for ever — and a
#      lap/pit/race-only painter is absent from a practice window by construction (§11b).
#      g_plotDeltaMismatchY is (y << 8) | cell of the first offender — the y says which block and
#      the cell says which column.
#      ⚠ It is NOT revs_screen_convert_reference: that reads m_lineMode, which the carve has
#      zeroed on exactly the lines under test, so the reference would be blank there (§10k trap 1).
#      ⭐ It is DOMAIN-AGNOSTIC: widening `kDeltaBlock` widens the oracle with it, which is how
#      the needle block (158..191) was proven correct — bases=1, 1000 checks, 0 mismatch, 57.6
#      delta bytes a sweep — before §11d closed it on COST, not on correctness.
#
# ⚠⚠ WHAT THIS ORACLE CANNOT SEE, because a STRAIGHT_TO_RACE run never leaves the race: the
# MODE 7 ROUND TRIP, which is the only path on which the BASE's second-buffer write is
# load-bearing.  Sabotaging that write survives 497/497 here and the argument is at
# `plotDeltaBase` in RevsPlot.cpp.  The other four defects fire at 6720 / 196606 / 6888 / 42658.
#
# Run (the oracle — a full re-expansion per sweep, so never quote a timing from it):
#   . ./env.sh && make clean && make DELTACHECK=1 STRAIGHT_TO_RACE=1 PROBES=1 FIXED_RNG=1
#   EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=delta_own.gdb ./diag_run.sh 60
set pagination off
set confirm off

# ⚠ NO conditional breakpoint: gdb evaluates one on every render and that throttled the
# emulator to 0.4x real time — an oracle wants a LONG window, so let it run and let
# diag_run.sh's SIGINT break back in (the diag_timing.gdb model).  ⚠ LOAD-BEARING `continue`:
# without it gdb executes the prints at connect time, while the program is still halted at
# the trigger breakpoint, and every counter reads 0.
continue

printf "=== vbi=%u  bodyTicks=%lu\n", g_vbiCount, g_bodyTicks
printf "=== DELTA DOMAIN bases=%lu  delta bytes=%lu\n", g_plotDeltaBases, g_plotDeltaBytes
printf "=== decode owned lines=%u (span 36 + delta 34 expected)\n", g_decodeOwnLines
printf "=== ORACLE checks=%lu mismatch=%lu first=(y=%u cell=%u)\n", \
  g_plotDeltaChecks, g_plotDeltaMismatch, g_plotDeltaMismatchY >> 8, g_plotDeltaMismatchY & 0xff
# The stimulus, because a parked car repaints a static scene and proves much less.
printf "=== engine $61=%02x $3C=%02x $63=%02x\n", mem[0x61], mem[0x3c], mem[0x63]
detach
quit
