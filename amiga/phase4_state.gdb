# Phase 4 state read-out: is the port actually doing the work, and did any trap fire?
#
# Every line here is a counter that exists because the alternative is a stub that returns
# something plausible and reads exactly like working code.  A run where all the "unknown"
# rows are 0 and `band` cycles 0-4 is a run whose findings can be trusted; a nonzero row is
# a finding, not noise.  See src/platform/mos.cpp, bbc_hw.cpp and Platform.cpp for what each
# one means, and docs/method-lessons.md for why they are counters and not silence.
#
# ⚠ Every symbol read here MUST be in PROBE_SYMS (amiga/Makefile) — --gc-sections drops an
# unreferenced counter and gdb then prints INSTRUCTION BYTES as its value.
#
# Run: . ./env.sh && GDBSCRIPT=phase4_state.gdb ./diag_run.sh 60
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 150
continue
printf "=== vbi=%u painted=%lu ===\n", g_vbiCount, g_fpsFrames
printf "brk:  count=%lu lastPC=$%04x\n", g_brkCount, g_brkPC
printf "smc:  count=%lu site=$%04x value=$%04x\n", g_smcUnhandled, g_smcSite, g_smcValue
printf "mos:  unknown=%lu entry=$%04x A=$%02x  chardef=%lu\n", g_mosUnknownCount, g_mosUnknownEntry, g_mosUnknownA, g_mosCharDefCount
printf "hw:   unknownReads=%lu lastAddr=$%04x  ula=$%02x t1=$%02x%02x\n", g_hwUnknownReads, g_hwUnknownAddr, g_ulaControl, g_userT1LatchHi, g_userT1LatchLo
printf "band: irq_band_state=%d\n", mem[0x4F43]
bt 12
detach
quit
