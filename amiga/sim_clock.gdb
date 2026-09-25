# ⭐ DOES GAME TIME RUN AT REAL TIME?  (docs/open-work.md §FRAME-RATE-INDEPENDENT SIMULATION)
#
# Samples the simulation clock's counters every 250 EMULATED fields (5 s) from field 500 to 3000
# and prints them; tools/sim_clock_report.py turns each reset-free interval into steps and slow
# ticks per emulated second (a crash hold is ~2 s of real time with no steps, by design).  The slow tick is the engine's own 93.6 ms frame,
# so it must read 50 / 4.68 = 10.68 per second in every decoupled build; steps read
# 5000 / SIM_STEP_TENTHS (10.68 at h = 1, 25 at 25 Hz, 50 at 50 Hz).  A SIMLEGACY build reads
# steps = slow ticks = the painted framerate, which is the control.
# The race clock (driver 0, $06B4/$06CC/$06E4, BCD cs/s/min) is printed too: over 20 s it must
# advance ~20 s in a decoupled build.  ⚠ A crash reset clears it — reread if it went DOWN.
# ⚠ The two conditional breakpoints stop the machine; every number here is a ratio of emulated
# quantities, so that costs nothing (never quote a framerate from this — fps_series.gdb).
set pagination off
set confirm off

printf "=== sim_clock: waiting for the scene\n"
tbreak Revs::render if g_vbiCount >= 500
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 750
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 1000
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 1250
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 1500
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 1750
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 2000
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 2250
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 2500
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 2750
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
tbreak Revs::render if g_vbiCount >= 3000
continue
printf "sample: vbi=%u steps=%lu ticks=%lu clock=%02x:%02x.%02x\n", g_vbiCount, g_simSteps, g_simSlowTicks, *(unsigned char*)(mem+0x6E4), *(unsigned char*)(mem+0x6CC), *(unsigned char*)(mem+0x6B4)
printf "=== sim_clock: done\n"
