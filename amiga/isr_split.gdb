# ⭐⭐ WHERE THE VERTB HANDLER'S TIME GOES  (`make ISRSPLIT=1 PROBES=1`)
#
# The ISR is a FIXED TAX ON WALL CLOCK — 50 fires a second whatever the framerate does — and it is
# charged pro-rata to whichever phase it preempted, so no row of the phase table can see it.
# docs/perf-method.md has priced the WHOLE handler at ~1.1 ms; this says which of its seven jobs
# that is.  src/platform/probe.h §ISRSPLIT.
#
# ⚠ Slot 0 is the NULL CONTROL: a bracket around nothing, on the same path at the same rate.
# Every other row is only meaningful ABOVE it.  If a row reads at the control, it is free.
# ⚠ ~8 beam-read pairs per field: quote the SPLIT from this build, never an FPS or a phase share.
#
# Build: cd amiga && make clean && make PROBES=1 ISRSPLIT=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 FIXED_RNG=1
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=isr_split.gdb ./diag_run.sh 30
set pagination off
set confirm off
continue
printf "=== VERTB fires=%lu  handler total=%lu us/call\n", \
  g_probeIsrCount, (g_probeIsrCount ? (g_probeIsrTicks/g_probeIsrCount)*1000/4006 : 0)
printf "slot     calls   us/call   us/field   share   name\n"
set $i = 0
set $tot = 0
while $i < 10
  set $tot = $tot + g_isrSplitTicks[$i]
  set $i = $i + 1
end
set $i = 0
while $i < 10
  set $c = g_isrSplitCount[$i]
  set $t = g_isrSplitTicks[$i]
  printf "  %d   %8lu  %8lu   %8lu   %3lu%%   ", $i, $c, ($c ? ($t/$c)*1000/4006 : 0), (g_vbiCount ? ($t/g_vbiCount)*1000/4006 : 0), ($tot ? (100*$t)/$tot : 0)
  if $i == 0
    printf "NULL ctrl\n"
  end
  if $i == 1
    printf "prologue\n"
  end
  if $i == 2
    printf "mouse\n"
  end
  if $i == 3
    printf "beam entry\n"
  end
  if $i == 4
    printf "tt flash\n"
  end
  if $i == 5
    printf "screen\n"
  end
  if $i == 6
    printf "audio rest\n"
  end
  if $i == 7
    printf "tail\n"
  end
  if $i == 8
    printf "snd_tick x2\n"
  end
  if $i == 9
    printf "program_paula\n"
  end
  set $i = $i + 1
end
printf "--- fields=%u  bracketed total=%lu us/field  (audio: %lu ticks, %lu paula reprograms)\n", \
  g_vbiCount, (g_vbiCount ? ($tot/g_vbiCount)*1000/4006 : 0), g_audioTicks, g_audioUpdates
printf "--- audio: %lu restarts (%lu busy-wait rasterlines = %lu us/field)\n", \
  g_audioRestarts, g_audioWaitLines, (g_vbiCount ? (g_audioWaitLines*635)/(10*g_vbiCount) : 0)
printf "--- snd: %lu ticks, %lu chan visits, %lu program runs (+%lu memo skips), %lu env steps\n", \
  g_sndTicks, g_sndChanVisits, g_sndProgramRuns, g_sndProgramSkips, g_sndEnvSteps
printf "--- presents=%lu late=%lu   entries=%lu late=%lu\n", \
  g_beamPresents, g_beamPresentsLate, g_beamEntries, g_beamEntriesLate
detach
quit
