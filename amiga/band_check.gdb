# ⭐⭐ THE ORACLE for the raster-band record reuse (src/platform/bbc_hw.cpp §fireIrq1vField).
#
# Build: cd amiga && make clean && make -j4 PROBES=1 BANDCHECK=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=band_check.gdb ./diag_run.sh 200
#
# This build always runs the real five-interrupt cycle and separately asks what the reuse
# predicate would have decided.  Every time it predicts "reuse", the record the real cycle then
# built must be identical to the one it would have reused.
#
# ⚠⚠ IT HAS TO RUN ON THE TARGET, and that is a measured requirement, not caution.  The same
# check on the host passes even when SABOTAGED, because a host run is ~36 fields and the horizon
# never moves in one — a skip that is never wrong on static inputs says nothing about the case
# the change exists for.  Only a driving run on the target moves update_horizon_band ($4F44).
#
# PASS = mismatch 0 AND checks large AND the run actually drove (gear/speed non-zero below).
set pagination off
set confirm off
continue
printf "=== vbi=%u  fields=%u  bodyTicks=%lu ===\n", g_vbiCount, g_vbiCount, g_bodyTicks
printf "band cycles run %lu   reuse PREDICTED %lu   MISMATCH %lu   (mismatch must be 0)\n", \
  g_bandRuns, g_bandCheckChecks, g_bandCheckMismatch
# ⭐⭐ THE STIMULUS, and it must be a COUNT over the run — not a state read at the end.
# A static horizon makes every reuse trivially correct, so a run with 0 here has proved nothing.
# ⚠ Do NOT substitute "is the car moving now?" for this: $61/$63/$3C read 00 at the end of a
# STRAIGHT_TO_RACE run because the car leaves the track after ~225 game frames and stalls there,
# which is the tail of a run that drove the whole way — and reading it as "parked" once already
# threw away a valid result.
printf "horizon moved in %lu fields  (0 = the test was trivial and the verdict is VOID)\n", \
  g_bandHorizonMoves
printf "end state (tail of the run, NOT the stimulus): $61=%02X $63=%02X $3C=%02X horizon=%02X%02X\n", \
  mem[0x61], mem[0x63], mem[0x3C], mem[0x4F20], mem[0x4F1F]
printf "band record: count=%u overflow=%u rejects=%lu\n", \
  g_bandCount, g_bandOverflow, g_bandRejects
detach
quit
