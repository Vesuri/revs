# ⭐ THE HONEST FRAMERATE: read the series the program sampled for itself.
#
# ONE stop, AFTER the run (diag_run.sh's SIGINT), so nothing gdb does lands inside the
# measurement window.  Each row is 512 real vblanks (10.24 s of emulated time) and is
# independently checkable — discard rows out of line with their neighbours, which is how a
# run that stopped doing the work shows up.
#
# ⚠ Every earlier framerate figure in this project came from a script that halted the
# machine at every frame to evaluate a breakpoint condition, and that biased the number by
# 3x on one build and 30x on another (see PlatformAmiga.cpp, g_fpsSeries).  Re-measure with
# this; do not quote those.
#
# Build: make clean && make FPSCOUNT=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=fps_series.gdb ./diag_run.sh 240
set pagination off
set confirm off

continue
printf "=== FPS series: N=%d (each row = 512 vblanks = 10.24 s) ===\n", (int)g_fpsSeriesN
set $i = 1
set $n = (int)g_fpsSeriesN
while $i < $n
  set $dv = (int)g_fpsSeriesVbi[$i] - (int)g_fpsSeriesVbi[$i-1]
  set $df = (int)g_fpsSeries[$i]    - (int)g_fpsSeries[$i-1]
  printf "vbi %5u  +%4d painted  FPS=%d.%02d\n", \
    g_fpsSeriesVbi[$i], $df, (50*$df)/$dv, ((5000*$df)/$dv)%100
  set $i = $i + 1
end
printf "=== total vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
detach
quit
