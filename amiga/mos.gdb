# Which MOS calls does a RACE actually make?  The Phase 2 inventory was recovered by a
# nearest-preceding-`LDA #imm` heuristic, so any site whose reason code is COMPUTED is
# missing from it (that is how OSBYTE 0 was found).  mos.cpp counts unhandled calls instead
# of answering a plausible zero, so this run reads the counter after a race has started.
set pagination off
set confirm off
continue
printf "=== vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "=== mosUnknown=%lu  last entry=$%04x  A=$%02x   charDef=%lu\n", \
  g_mosUnknownCount, g_mosUnknownEntry, g_mosUnknownA, g_mosCharDefCount
printf "=== brk=%lu smc=%lu hwUnknown=%lu (addr $%04x)\n", \
  g_brkCount, g_smcUnhandled, g_hwUnknownReads, g_hwUnknownAddr
detach
quit
