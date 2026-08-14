# The same twelve-frame capture as fill_frames.gdb, for a PLAIN STRAIGHT_TO_RACE build.
#
# ⚠ WHY A SECOND SCRIPT: the reported artefact appears in `make STRAIGHT_TO_RACE=1` with no
# other flag, i.e. with the autorun HANDING THE KEYBOARD OVER — the car is stationary.  An
# FPSCOUNT build holds the throttle, so it is a different scene, and 12 clean frames there do
# not answer the report.  This build has no g_fpsFrames increment, so the frame counter cannot
# be a break condition: gate on g_vbiCount and count frames in gdb instead.
#
# Run:  . ./env.sh && make clean && make -j4 STRAIGHT_TO_RACE=1 \
#       && GDBSCRIPT=fill_frames_plain.gdb ./diag_run.sh 300
# Then: python3 tools/fill_frames.py
set pagination off
set confirm off

break Revs::render if g_vbiCount >= 1200
continue
delete breakpoints
break Revs::render

set $i = 0
while $i < 12
  eval "dump binary memory .run/ff_pl_%02d.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)", $i
  eval "dump binary memory .run/ff_cop_%02d.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)", $i
  eval "dump binary memory .run/ff_fb_%02d.bin &mem[0x5A80] &mem[0x5A80 + 8320]", $i
  printf "frame %d: vbi=%u bands=%u rejects=%lu overflow=%u irqClobber=%lu which=%02x smc=%lu\n", \
    $i, g_vbiCount, g_bandCount, g_bandRejects, g_bandOverflow, \
    g_irqClobberCount, g_irqClobberWhich, g_smcUnhandled
  set $i = $i + 1
  continue
end
printf "=== dumped 12 frames\n"
detach
quit
