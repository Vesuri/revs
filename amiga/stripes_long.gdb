# A LONG series of painted frames, for the residual fill artefact (black OR green runs).
#
# stripes_series.gdb takes ten frames from the very start of the race, which is what the
# original report described.  The residual — a fill run that is GREEN rather than black, and
# scattered frames rather than most of them — needs a bigger sample from a SETTLED scene, so
# this one starts later and takes 40 frames.
#
# Run:  . ./env.sh && make STRAIGHT_TO_RACE=1 FPSCOUNT=1 \
#       && GDBSCRIPT=stripes_long.gdb ./diag_run.sh 400
# Then: python3 tools/fill_check.py amiga/.run/fl_*.bin
set pagination off
set confirm off

break Revs::render if g_fpsFrames >= 12
continue

set $i = 0
while $i < 40
  eval "dump binary memory .run/fl_%02d.bin &mem[0x5A80] &mem[0x5A80 + 8320]", $i
  printf "frame %d: vbi=%u painted=%lu irqClobber=%lu which=%02x smc=%lu site=%04x val=%02x\n", \
    $i, g_vbiCount, g_fpsFrames, g_irqClobberCount, g_irqClobberWhich, \
    g_smcUnhandled, g_smcSite, g_smcValue
  set $i = $i + 1
  continue
end
printf "=== dumped 40 frames\n"
detach
quit
