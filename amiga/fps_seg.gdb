# HONEST framerate: painted frames per real 50 Hz vblank, sampled in SHORT segments so a
# crash or a cinematic inside the window cannot silently deflate the number.
#
# FPS = 50 * g_fpsFrames / g_vbiCount.  Both are plain integer counters and the ratio is
# frames-per-EMULATED-vblank, so host speed and the gdb stub's own slowness cancel out
# completely — this is safe to read under the remote debugger.
#
# TWO TRAPS this script exists to avoid.  Both produced badly wrong numbers on the Atari
# port before it was written, and both apply here unchanged:
#
#  1. INSTRUMENTATION.  Build with `make FPSCOUNT=1` and NOTHING else.  That adds only the
#     headless auto-run plus one increment per painted frame.  A PROBES build was 20-35%
#     slower there (its timing brackets read two chip registers plus a 16x16 multiply
#     several times per iteration) — and every framerate figure taken before this script
#     existed had been measured on one.
#
#  2. THE UNATTENDED RUN ENDING.  A headless run drives no input, so it eventually stops
#     doing the work being measured (there: flew into a mountain; here: expect a spin, a
#     crash, or a finished lap).  Rendering stops while g_vbiCount keeps ticking, so ANY
#     wide window straddling that under-reports badly.  Each row below is independently
#     checkable — discard rows whose frames/vbi is out of line with their neighbours, and
#     add a state check to the printf as soon as there is a scene/state variable to gate on.
#
# ⚠ Under ~3% is noise: this harness routinely OVER-reads a win.  Quote a static cycle
# count or an in-process differential ratio as the win, and an FPS row only as the
# standing baseline.  See docs/perf-method.md.
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol, from that line onward.
# If this prints only its header, suspect a renamed/deleted probe global, not a dead probe.
#
# Build: make clean && make -j4 FPSCOUNT=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=fps_seg.gdb ./diag_run.sh 200
set pagination off
set confirm off

define seg
  tbreak Revs::render if g_vbiCount >= $arg0
  continue
  set $dv = (int)g_vbiCount  - (int)$pv
  set $df = (int)g_fpsFrames - (int)$pf
  printf "vbi %5u  +%4d vbi  +%4d painted  f/vbi=0.%03d  FPS=%2d.%d\n", \
    g_vbiCount, $dv, $df, ($df*1000)/$dv, (50*$df)/$dv, ((500*$df)/$dv)%10
  set $pv = (int)g_vbiCount
  set $pf = (int)g_fpsFrames
end

# First checkpoint: past boot, once the scene is steady.  Raise this once the real entry
# chain is wired and its settle point is known.
tbreak Revs::render if g_vbiCount >= 200
continue
set $pv = (int)g_vbiCount
set $pf = (int)g_fpsFrames
printf "=== start: vbi=%u frames=%lu ===\n", g_vbiCount, g_fpsFrames

seg 400
seg 600
seg 800
seg 1000
seg 1200
seg 1400
seg 1600
seg 1800
seg 2000
detach
quit
