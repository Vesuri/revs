# ⭐ TWELVE CONSECUTIVE DISPLAYED FRAMES, planes + copper + the BBC buffer they came from.
#
# screen_dump.gdb takes ONE settled frame; the residual fill artefact (a black OR GREEN run at
# the horizon, scattered frames) cannot be judged from one frame, and it needs the copper list
# next to the pixels because the suspect is the PAIRING of the palette bands with the frame —
# band 2's boundary IS the horizon and moves every game frame.
#
# Run:  . ./env.sh && make STRAIGHT_TO_RACE=1 FPSCOUNT=1 \
#       && GDBSCRIPT=fill_frames.gdb ./diag_run.sh 400
# Then: python3 tools/fill_frames.py            (renders each frame and reports the horizon)
set pagination off
set confirm off

break Revs::render if g_fpsFrames >= 8
continue

set $i = 0
while $i < 12
  eval "dump binary memory .run/ff_pl_%02d.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)", $i
  eval "dump binary memory .run/ff_cop_%02d.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)", $i
  eval "dump binary memory .run/ff_fb_%02d.bin &mem[0x5A80] &mem[0x5A80 + 8320]", $i
  printf "frame %d: vbi=%u painted=%lu bands=%u rejects=%lu overflow=%u irqClobber=%lu smc=%lu\n", \
    $i, g_vbiCount, g_fpsFrames, g_bandCount, g_bandRejects, g_bandOverflow, \
    g_irqClobberCount, g_smcUnhandled
  set $i = $i + 1
  continue
end
printf "=== dumped 12 frames\n"
detach
quit
