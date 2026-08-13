# HONEST framerate with NO conditional breakpoints: one unconditional stop to sample the
# counters, then a single free run until diag_run.sh's SIGINT, then the deltas.
#
# ⚠ WHY THIS EXISTS ALONGSIDE fps_seg.gdb.  fps_seg re-arms a CONDITIONAL breakpoint
# (`tbreak Revs::render if g_vbiCount >= N`) per segment, so gdb stops at every call to
# evaluate it.  Measured 2026-08-13: on a build whose frame costs ~120 ms more, the same
# script reported 2 painted frames per 11571 vblanks (0.02 FPS) where an UNCONDITIONAL
# run of the same binary reported 40 per 3007 (0.66 FPS) — a 30x error, in the direction
# that looks like a catastrophic regression.  Segmented rows are still worth having (they
# catch a run that stops doing the work), but a headline number comes from here.
set pagination off
set confirm off

tbreak Revs::render
continue
set $v0 = (int)g_vbiCount
set $f0 = (long)g_fpsFrames
printf "=== start vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames

continue
# resumes here when diag_run.sh interrupts gdb
set $dv = (int)g_vbiCount  - $v0
set $df = (long)g_fpsFrames - $f0
printf "=== end   vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "=== +%d vbi  +%d painted  FPS=%d.%02d\n", $dv, $df, (50*$df)/$dv, ((5000*$df)/$dv)%100
printf "=== bands=%u rejects=%lu overflow=%u brk=%lu\n", \
  g_bandCount, g_bandRejects, g_bandOverflow, g_brkCount
detach
quit
