# ⭐⭐ HOW MUCH OF THE PICTURE ACTUALLY CHANGES, and WHO WRITES IT — on the TARGET.
#
# The measurement that gates the direct-bitplane layout choice (docs/direct-bitplane-plan.md §6
# step 3): `decode()` converts all 8320 frame-buffer bytes every painted frame, and if only a few
# hundred of them differ from the previous paint then a dirty-region pass captures most of what
# direct plotting would, for a fraction of the work.
#
# ⚠⚠ THIS CANNOT BE MEASURED ON THE HOST, and that is not a preference — it is the shape of the
# port.  The host runs the 50 Hz body ONCE per painted frame; the target drains ~30 body ticks per
# painted frame and **the body is what draws** (display lines 120-143, docs/amiga-arch.md).  A host
# run therefore shows a nearly frozen picture: measured 2 of 8320 bytes changed between frames 400
# and 410, cross-checked against two independent REVS_SCREEN_DUMPs.  Do not quote it.
#
# ⚠ And the run must have a MOVING CAR or the stimulus is absent — a parked car never redraws.
# Build with STRAIGHT_TO_RACE=1 FPSCOUNT=1 so the autorun holds the throttle instead of handing the
# keyboard over, and read `$61`/`$63` below: speed 0 in neutral means the window measured nothing.
#
# Build: cd amiga && make clean && make -j4 SHAPE=1 PROBES=1 STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=frame_shape.gdb ./diag_run.sh 200
#
# ⚠ A DIAGNOSTIC build: one 8320-byte compare per painted frame plus one per phase boundary.
# No framerate and no phase share may be quoted from it.
set pagination off
set confirm off
continue

printf "=== vbi=%u loopFrames=%lu paints=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_shapeFrameCalls
printf "engine: speed $61=%02X  $3C=%02X  gear $63=%02X   (all zero = the car never moved)\n", \
  mem[0x61], mem[0x3c], mem[0x63]

if g_shapeFrameCalls > 0
  printf "\n--- PER-PAINT DELTA (the number that prices dirty-region drawing) ---\n"
  printf "  mean %lu of 8320 bytes changed per paint   max %u   last %u over %u lines (%u..%u)\n", \
    g_shapeFrameBytes / g_shapeFrameCalls, g_shapeFrameMax, g_shapeFrameLast, \
    g_shapeFrameLines, g_shapeFrameFirstLine, g_shapeFrameLastLine
  printf "  = %lu per mille of the buffer the decode converts unconditionally\n", \
    (g_shapeFrameBytes / g_shapeFrameCalls) * 1000 / 8320
end

if g_shapeRoadCalls > 0
  printf "\n--- $1A20, phase 11 (called 'the road pass') ---\n"
  printf "  %lu calls, %lu of 8320 bytes changed per call over %lu lines  last %u bytes, %u..%u\n", \
    g_shapeRoadCalls, g_shapeRoadBytes / g_shapeRoadCalls, \
    g_shapeRoadLines / g_shapeRoadCalls, g_shapeRoadLastBytes, \
    g_shapeRoadFirstLine, g_shapeRoadLastLine
end

printf "\n--- WHO WRITES THE FRAME BUFFER, per main-loop phase ---\n"
printf "    (lines 18..81 are the flat-blue sky band: writes there are engine VARIABLES,\n"
printf "     not pixels, and a direct renderer never looks at them)\n"
set $i = 0
while $i < 40
  if g_shapePhaseFrames[$i] > 0
    printf "  phase %2d: %8lu bytes total, lines %3u..%3u, wrote in %lu closures\n", \
      $i, g_shapePhaseBytes[$i], g_shapePhaseFirst[$i], g_shapePhaseLast[$i], \
      g_shapePhaseFrames[$i]
  end
  set $i = $i + 1
end
printf "  ⚠ the 50 Hz BODY is not a main-loop phase — anything it draws lands in whichever\n"
printf "    phase it preempted, or outside every bracket.  Trust the per-paint delta for the total.\n"
detach
quit
