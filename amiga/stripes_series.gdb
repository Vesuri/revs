# Ten CONSECUTIVE painted frames of the BBC frame buffer, for the stripe check.
#
# ⚠ One frame cannot answer this.  The horizon stripes appear on SCATTERED lines — wherever
# the 50 Hz VERTB happened to preempt the fill chain — so a single clean frame is as likely to
# be luck as a fix.  The reported symptom was "present in most frames from the very beginning
# of the race", so the honest test is a run of frames from the start, checked in bulk.
#
# Run:  . ./env.sh && make STRAIGHT_TO_RACE=1 FPSCOUNT=1 \
#       && GDBSCRIPT=stripes_series.gdb ./diag_run.sh 120
# Then: python3 tools/stripe_check.py amiga/.run/fb_*.bin
set pagination off
set confirm off

break Revs::render if g_fpsFrames >= 3
continue

set $i = 0
while $i < 10
  eval "dump binary memory .run/fb_%02d.bin &mem[0x5A80] &mem[0x5A80 + 8320]", $i
  # ⭐ The interrupt register contract, per frame.  Measured on real hardware (make refloop
  # --irq-abi): A, X and Y are preserved across every engine-context interrupt.  Any non-zero
  # count here is a foreground routine being resumed with a corrupted register, which is what
  # the horizon stripes were — so it is worth a line of output on every frame, not a footnote.
  printf "frame %d: vbi=%u painted=%lu irqClobber=%lu which=%02x smc=%lu\n", \
    $i, g_vbiCount, g_fpsFrames, g_irqClobberCount, g_irqClobberWhich, g_smcUnhandled
  set $i = $i + 1
  continue
end
printf "=== dumped 10 frames\n"
detach
quit
