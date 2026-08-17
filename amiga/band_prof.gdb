# ⭐⭐ THE BRACKET INSIDE irq1v_band_schedule — what each of the five band arms actually costs.
#
# Build: cd amiga && make clean && make -j4 PROBES=1 STRAIGHT_TO_RACE=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=band_prof.gdb ./diag_run.sh 200
#
# WHY THIS EXISTS.  The phase table charges the 50 Hz drain ~26% of the frame and
# `phase4_prof.gdb` reports one number for irq1v_band_schedule: ~810 us a CALL.  But the five calls
# in a field are five different jobs — four write a palette and reload a timer, one also runs
# $52A4 — so that average prices sixteen inline byte stores and the band-4 arm as the same
# thing, and it is why the cost reads as "unexplained".  Split it before optimising it.
#
# ⚠ Attribution is by the band a call ENTERED on (mem[$4F43] before the handler steps it).
# Bands 1->2->3 fall through inside ONE interrupt when a band has zero height, so the counts
# per band are not equal and band 1's row may contain bands 2 and 3's work.  Compare the
# counts against `fields` before reading anything into a per-call figure.
#
# No conditional breakpoints (phase4_prof.gdb defect 1) and per-mille arithmetic only
# (defect 2): `100 * ticks` overflows gdb's 32-bit maths at these totals.
set pagination off
set confirm off
continue
printf "=== vbi=%u loopFrames=%lu  fields=%u  bodyTicks=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_vbiCount, g_bodyTicks
if g_probeIrqCount > 0
  printf "irq1v_band_schedule TOTAL: %lu calls, %lu us each\n", \
    g_probeIrqCount, (g_probeIrqTicks/g_probeIrqCount)*1000/4006
end
# 4006 beam ticks = 1000 us (80128 ticks per 20 ms field).
set $i = 0
set $sum = 0
while $i < 8
  set $sum = $sum + g_probeBandTicks[$i]
  set $i = $i + 1
end
printf "band  calls      ticks        us/call    share of irq total\n"
set $i = 0
while $i < 8
  if g_probeBandCount[$i] > 0
    printf "  %d  %8lu  %11lu  %8lu us   %2d.%01d%%\n", \
      $i, g_probeBandCount[$i], g_probeBandTicks[$i], \
      (g_probeBandTicks[$i]/g_probeBandCount[$i])*1000/4006, \
      (g_probeBandTicks[$i]/($sum/1000))/10, (g_probeBandTicks[$i]/($sum/1000))%10
  end
  set $i = $i + 1
end
# ⭐⭐ SLOT 6 IS THE EMPTY-BRACKET CONTROL — a bracket around NO WORK, on the same path at the
# same rate.  Read it FIRST: it is the floor under every other row, and if it is comparable to
# band 3's row then this table is measuring the instrument and not the handler.
#
# Slot 5 is the $FF wrap arm, 7 is "some other negative counter" (should be 0), and a call that
# found the User VIA T1 flag clear is charged to whatever band it entered on — it does no work.
#
# ⭐ Band 4 also runs $52A4, which phase 29 brackets separately.  Subtracting it from band 4's
# row is what says whether the arm is the palette write or the routine.
if g_bodyTicks > 0
  printf "phase 29 ($52A4, inside band 4): ticks=%lu calls=%lu -> %lu us per body tick\n", \
    g_phaseTicks[29], g_phaseCount[29], (g_phaseTicks[29]/g_bodyTicks)*1000/4006
  printf "phase 26 (drain, band cycle minus $52A4): %lu us per body tick\n", \
    (g_phaseTicks[26]/g_bodyTicks)*1000/4006
end
# ⭐ THE SHIM'S OWN COST: the drain phase minus everything the handler was measured doing.
# fireIrq1v does the IRQ1V gate, the P push, the $FC store, the A/X/Y and cpu_unwind contract
# checks and runBandCycle's loop — none of which is inside the PROBE_IRQ bracket.
printf "drain total %lu ticks; irq brackets %lu; shim+loop remainder %lu ticks\n", \
  g_phaseTicks[26]+g_phaseTicks[29], g_probeIrqTicks, \
  (g_phaseTicks[26]+g_phaseTicks[29]) - g_probeIrqTicks
# ⭐⭐ THE RECORD REUSE — read these two BEFORE anything above.  runs+skips must equal the
# number of fields the engine saw, and `skipped` must be large or the fast path never ran and
# every other row here describes the control build wearing the test build's name.
printf "band cycle: run %lu, skipped %lu  (BANDSKIP=0 makes skipped 0)\n", \
  g_bandRuns, g_bandSkips
detach
quit
