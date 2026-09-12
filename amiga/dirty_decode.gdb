# ⭐⭐ THE DIRTY-REGION DECODE — did it engage, and does it produce the SAME PICTURE?
#
# Phase 6 item 0 step 2's payoff (docs/direct-bitplane-plan.md §7b): only 4.9% of the BBC frame
# buffer changes per painted frame, so decode() converts only the cell columns whose eight source
# bytes moved since THIS buffer was last decoded. Shipping discovers them with two byte shadows;
# `CHANGEDIRTY=1` tests the writer-maintained maps from §7j.
#
# Three numbers, and each one can fail on its own:
#   1. g_decodeCells — cell columns converted, of 1040.  A run that reads 1040 every frame has
#      the dirty test failing OPEN: exactly as fast as no feature at all, and identical on screen.
#      A run that reads 0 has it failing CLOSED, which is a frozen picture, not a fast one.
#      `make DIRTY=0` is the control and reads 1040 by construction.
#   2. g_decodeDirtyMismatch / g_decodeDirtyChecks — the ORACLE, and it needs `make DIRTYCHECK=1`
#      or checks stays 0 and the zero mismatch means nothing (the window IS the measurement:
#      docs/method-lessons.md).  An unconditional re-decode of the same frame must agree byte for
#      byte.  ⚠ That build converts twice per frame: never quote FPS from it.
#   3. The engine state, because a parked car changes nothing and would make a broken dirty test
#      look perfect.  $61 (throttle) and $3C/$63 must show the car under power.
#
# Run (the oracle):
#   . ./env.sh && make clean && make CHANGEDIRTY=1 DIRTYCHECK=1 STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1
#   GDBSCRIPT=dirty_decode.gdb ./diag_run.sh 120
# Run (the map's shape, diagnostic build; never use its FPS as the shipping baseline):
#   . ./env.sh && make clean && make CHANGEDIRTY=1 STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1
#   GDBSCRIPT=dirty_decode.gdb ./diag_run.sh 120
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "=== vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "=== cells last=%u max=%u total=%lu of 1040/frame   fullFrames=%u modeDirtyRows=%u\n", \
  g_decodeCells, g_decodeCellsMax, g_decodeCellsTotal, g_decodeFullFrames, g_decodeModeDirty
printf "=== oracle checks=%lu mismatch=%lu firstOff=%u\n", \
  g_decodeDirtyChecks, g_decodeDirtyMismatch, g_decodeDirtyMismatchOff
printf "=== flat lines=%u bands=%u   rejects=%lu\n", \
  g_decodeFlatLines, g_decodeFlatBands, g_bandRejects
# The stimulus: throttle, gear and speed.  A stalled car ($61=00) means the picture was static
# and none of the above is evidence of anything.
printf "=== engine $61=%02x $3C=%02x $63=%02x  drains=%lu ticks=%lu\n", \
  mem[0x61], mem[0x3c], mem[0x63], g_bodyDrains, g_bodyTicks
detach
quit
