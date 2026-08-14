# ⭐⭐ DOES THE BAND BOUNDARY AGREE WITH THE PIXELS, frame by frame?
#
# The artefact that survives the raster-race fix lasts a whole PAINTED frame (~1 s) and clears on
# the next, so it is this frame's data, not a beam race.  Band 3's first display line is where the
# ground starts, measured two independent ways: from the game's T1 latches (the plan) and from the
# game's own pixels (the topmost right/left-edge green cell).  They must agree.
#
# The suspect is snapshotBands() REJECTING a record it caught mid-cycle, which leaves the PREVIOUS
# frame's plan against this frame's pixels — one frame stale in a value that moves with the hills,
# and a frame here is a second of game time.  stale=1 marks those.
#
# ⚠ Needs `make FILLWATCH=1 HOLD_THROTTLE=1` — see amiga/fill_catch.gdb for why both.
#
# Run:  . ./env.sh && make -j4 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 FILLWATCH=1 \
#       && GDBSCRIPT=plan_log.gdb ./diag_run.sh 400
set pagination off
set confirm off

break Revs::render if g_planLogN >= 60
continue

printf "frames=%lu stale(rejected record)=%lu bandRejects=%lu\n", \
  g_planLogN, g_planStaleFrames, g_bandRejects
printf "  n  band2 band3 groundL groundR  d(L) d(R) stale\n"
set $i = 0
while $i < 60
  set $b3 = g_planLogBand3[$i]
  printf "%3d   %3u   %3u    %3u     %3u    %+3d  %+3d    %u\n", \
    $i, g_planLogBand2[$i], $b3, g_planLogGroundL[$i], g_planLogGroundR[$i], \
    g_planLogGroundL[$i] - $b3, g_planLogGroundR[$i] - $b3, g_planLogStale[$i]
  set $i = $i + 1
end
# ⭐ And the other half of the question: did the SOURCE move under the decode?  The BBC frame
# buffer is single-buffered and the VERTB handler's game body writes it while decode() reads it,
# so a row written behind the read is baked into the bitplanes for the whole painted frame.
printf "tear frames=%lu rows=%lu lastRow=%u\n", g_tearFrames, g_tearRows, g_tearLastRow
printf "character rows caught moving mid-decode (row: display lines, times):\n"
set $r = 0
while $r < 26
  if g_tearRowCount[$r] != 0
    printf "  row %2d: lines %3d..%3d  x%u\n", $r, $r*8, $r*8+7, g_tearRowCount[$r]
  end
  set $r = $r + 1
end
detach
quit
