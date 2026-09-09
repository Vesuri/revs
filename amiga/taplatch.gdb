# ⭐⭐ Does the tap latch answer a press the level poll never saw?  (docs/controls.md §The double-press)
#   make clean && make PROBES=1 NOAUTORUN=1 TAPTEST=1   -> phase must reach 3, hits must climb
#   ...and again with NOLATCH=1                          -> phase must stall, hits must stay 0
# ⚠ ONE stop, at the end of the run: gdb cannot write this target's memory (RevsInput.cpp §THE
# PROOF), so everything here is a read and the injection happens inside the program.
set pagination off
set confirm off
continue
printf "=== tap latch ===\n"
printf "vbi=%u  mode7=%u\n", g_vbiCount, g_screenMode7
printf "taps injected=%lu   latch hits=%lu\n", g_tapInjected, g_keyLatchHits
printf "space: polls=%lu answered=%lu edges=%lu\n", g_spacePolls, g_spaceAnswered, g_spaceEdges
printf "circuit menu: phase=%d option=%d track=%d (3 = FINISHED)\n", g_tmPhase, g_tmOption, g_tmTrack
printf "session_is_race=%02x  engine=%02x\n", mem[0x006C], mem[0x0071]
detach
quit
