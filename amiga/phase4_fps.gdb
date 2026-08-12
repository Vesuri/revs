# ⭐ THE FRAMERATE BASELINE.  Segmented, so a stall inside the window cannot silently
# deflate the number, and each row carries the state that says whether the run was still
# doing the work being measured:
#   band  = irq_band_state ($4F43); cycling 0-4 means the 50 Hz body is alive
#   62F7  = the frame counter the 50 Hz body decrements; CHANGING between rows is the
#           liveness check that fps_seg.gdb's header asks for
#   brk   = platform_brk() hits.  ⭐ Expected **0** since the $7B00 overlay was ingested
#           (docs/static-map.md §Open items 6/10).  It used to climb by ~3 per game frame,
#           which was the wing mirrors and the dashboard not running; a non-zero reading now
#           means a REGRESSION, not a known gap.
#
# Build: cd amiga && make clean && make -j4 FPSCOUNT=1 FIXED_RNG=1     (NOT a PROBES build)
# Run:   . ./env.sh && GDBSCRIPT=phase4_fps.gdb ./diag_run.sh 200
# See docs/perf-method.md §THE BASELINE for the numbers this produced and their caveats.
set pagination off
set confirm off
# Segmented framerate + the state that says whether the run is still doing the work.
define seg
  tbreak Revs::render if g_vbiCount >= $arg0
  continue
  set $dv = (int)g_vbiCount  - (int)$pv
  set $df = (int)g_fpsFrames - (int)$pf
  printf "vbi %5u  +%4d vbi  +%4d painted  FPS=%2d.%d   band=%3d brk=%lu 62F6=%02x 62F7=%02x\n", \
    g_vbiCount, $dv, $df, (50*$df)/$dv, ((500*$df)/$dv)%10, \
    mem[0x4F43], g_brkCount, mem[0x62F6], mem[0x62F7]
  set $pv = (int)g_vbiCount
  set $pf = (int)g_fpsFrames
end
tbreak Revs::render if g_vbiCount >= 300
continue
set $pv = (int)g_vbiCount
set $pf = (int)g_fpsFrames
printf "=== start: vbi=%u painted=%lu ===\n", g_vbiCount, g_fpsFrames
seg 400
seg 500
seg 600
seg 700
seg 800
seg 900
seg 1000
seg 1100
seg 1200
detach
quit
