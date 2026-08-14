# ⭐⭐ WHERE THE BEAM IS WHEN THE COPPER LIST AND THE BITPLANE POINTERS ARE REWRITTEN.
#
# The rule is that a bitplane POINTER swap happens in the VBI and never mid-frame.  vbiUpdate()
# is called from the VERTB handler, so the rule looks satisfied — but the handler also runs the
# game's entire 50 Hz body first, and that takes hundreds of milliseconds at this baseline.  So
# the swap lands wherever the beam happens to be, and this reads the numbers rather than
# arguing about them (RevsScreen.cpp, beamLine()).
#
# Display window: kDisplayTop = 0x2C = 44 .. 44+208 = 252.  A present at line 44..251 raced the
# beam; anything below 44 or at/after 252 is in the blank, which is where it belongs.
#
# Run:  . ./env.sh && make clean && make -j4 STRAIGHT_TO_RACE=1 \
#       && GDBSCRIPT=beam_watch.gdb ./diag_run.sh 200
set pagination off
set confirm off

break Revs::render if g_vbiCount >= 2500
continue

printf "vbi=%u presents=%lu late(in display)=%lu\n", \
  g_vbiCount, g_beamPresents, g_beamPresentsLate
printf "beam line at present: last=%u min=%u max=%u   (display window is 44..251)\n", \
  g_beamPresentLine, g_beamPresentMin, g_beamPresentMax
printf "VERTB entries=%lu late(in display)=%lu last=%u\n", \
  g_beamEntries, g_beamEntriesLate, g_beamEntryLine
printf "bands=%u rejects=%lu overflow=%u irqClobber=%lu\n", \
  g_bandCount, g_bandRejects, g_bandOverflow, g_irqClobberCount
# ⭐ The C-only half of the interrupt contract: the two-level-RTS flag is the 6502's STACK POINTER,
# which real hardware saves across an interrupt and a C global does not.  Pending only says
# interrupts do land inside the drop window; Touched and Imbalance are the bugs and must be 0.
printf "unwind: pending=%lu touched=%lu   stack imbalance=%lu\n", \
  g_irqUnwindPending, g_irqUnwindTouched, g_irqStackImbalance
detach
quit
