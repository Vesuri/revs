# Is the port actually MAKING SOUND on the target?
#
#   cd amiga && . ./env.sh && make clean && make STRAIGHT_TO_RACE=1 && \
#       GDBSCRIPT=sound.gdb ./diag_run.sh 60
#
# ⚠ STRAIGHT_TO_RACE=1 is not optional: Revs's only sounds are the engine and the tyre squeal, so
# a run that never starts the engine measures silence and every counter below reads a legitimate
# zero.  With it, autorun holds the throttle and the rev counter climbs, which is exactly the
# stimulus (docs/method-lessons.md — a probe whose STIMULUS is absent gives a coherent wrong
# answer).
#
# What each counter separates, in the order a sound has to travel:
#   sndCommands   the engine reached OSWORD 7 at all              (game -> OS)
#   sndTicks      the 100 Hz scheduler is being driven            (ISR -> scheduler)
#   sndChipWrites the scheduler changed the chip state            (scheduler -> chip)
#   audioUpdates  Paula was reprogrammed from that state          (chip -> hardware)
# A zero at any one of them says which link is broken, which is the whole point of having four.
set pagination off
set confirm off
continue
printf "=== vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "=== engine running: $61=%d   rev target $5F=%d   rev now $60=%d\n", \
  mem[0x61], mem[0x5f], mem[0x60]
printf "=== OS calls:  sound=%lu  envelope=%lu  flush=%lu\n", \
  g_sndCommands, g_sndEnvelopes, g_sndFlushes
printf "=== scheduler: ticks=%lu  chipWrites=%lu  badEnvelope=%lu\n", \
  g_sndTicks, g_sndChipWrites, g_sndBadEnvelope
printf "=== unmeasured paths (must stay 0): sync=%lu hold=%lu queued=%lu drops=%lu\n", \
  g_sndSyncRequests, g_sndHoldRequests, g_sndQueued, g_sndQueueDrops
printf "=== paula:     updates=%lu  restarts=%lu  waitLines=%lu  allocFailed=%lu\n", \
  g_audioUpdates, g_audioRestarts, g_audioWaitLines, g_audioAllocFailed
printf "=== chip now:  tone=[%u %u %u] noise=%u vol=[%u %u %u %u]\n", \
  g_sndChip.tone[0], g_sndChip.tone[1], g_sndChip.tone[2], g_sndChip.noise, \
  g_sndChip.vol[0], g_sndChip.vol[1], g_sndChip.vol[2], g_sndChip.vol[3]
# ⚠ The SHADOW, not the registers: AUDxPER/AUDxVOL are write-only, so reading $DFF0A6 back would
# print whatever the chip bus happens to float and look like a measurement.
printf "=== paula:     per=[%u %u %u %u]  vol=[%u %u %u %u]  DMACONR=$%04x\n", \
  g_audioPaulaPer[0], g_audioPaulaPer[1], g_audioPaulaPer[2], g_audioPaulaPer[3], \
  g_audioPaulaVol[0], g_audioPaulaVol[1], g_audioPaulaVol[2], g_audioPaulaVol[3], \
  *(unsigned short*)0xdff002
detach
quit
