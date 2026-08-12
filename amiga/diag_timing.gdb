# Default probe dump for diag_run.sh.  Needs an out/Revs.exe built with `make PROBES=1`.
#
# Edit this file freely — it is the scratchpad end of the headless loop.  `-g` is always on
# (CORE_CFLAGS), so every global is readable by name and `mem[0xNNNN]` reads the 6502 image.
# A `while $i < N ... end` loop dumps an array.
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol — and from that line
# onward, not just that column.  So when you delete or rename a probe global, grep
# amiga/*.gdb for it, and read "the trace stopped after the header" as a stale script
# rather than a dead probe.
#
# ⚠ A probe that reads the same source as the code under test proves nothing.  If a probe
# has never fired, suspect the probe first.
set pagination off
set confirm off

# ⚠ LOAD-BEARING: `continue` lets the program actually RUN.  diag_run.sh SIGINTs gdb after
# its delay, which breaks back in here and the prints below then read live state.  Without
# this line gdb executes the whole script at connect time, while the program is still
# halted at the trigger breakpoint, and every counter reads 0 — which looks exactly like a
# hung build.
continue
echo \n==== SIGINT ====\n

printf "=== Revs probe dump ===\n"
printf "vbi=%u  painted=%lu\n", g_vbiCount, g_fpsFrames

# The 6502 image is reachable by address — useful before any symbol map exists.
printf "mem[$1200..$1207] = %02x %02x %02x %02x %02x %02x %02x %02x\n", \
  mem[0x1200], mem[0x1201], mem[0x1202], mem[0x1203], \
  mem[0x1204], mem[0x1205], mem[0x1206], mem[0x1207]

# Add probe globals below as they are introduced (defs under #ifdef REVS_PROBE in
# PlatformAmiga.cpp).  Stamp g_vbiCount at milestones and print the deltas here: a
# pure-compute stretch shows up as a g_vbiCount delta because the real VBI keeps counting.

detach
quit
