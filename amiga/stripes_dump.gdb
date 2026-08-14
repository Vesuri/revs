# ⭐ THE STRIPES, AT THE START OF THE RACE — and the SOURCE bytes next to them.
#
# screen_dump.gdb answers "what is the target showing?".  That is not enough to place this
# bug, because the black horizontal runs above the horizon could come from either end of the
# display path:
#
#   the FILL — the game's own frame buffer in mem[] genuinely has zero bytes there, or
#   the DECODE — mem[] is clean and RevsScreen turns it into stripes on the way to bitplanes.
#
# Those need opposite fixes, and the two are told apart by dumping BOTH AT THE SAME INSTANT.
# The host build's frame buffer is clean in band 2 for every one of the first twelve race
# frames, so if the Amiga's mem[] is clean here too, the fill is not the problem.
#
# ⚠ Break EARLY.  screen_dump.gdb waits for g_vbiCount >= 600 (12 s) because it wants a
# settled scene; the stripes are reported from the very first race frames, and a late dump
# is a different scene.  g_fpsFrames counts PAINTED frames, so >= 3 is "the race has drawn
# a few times" without depending on wall-clock.
#
# Run:  . ./env.sh && make clean && make STRAIGHT_TO_RACE=1 FPSCOUNT=1 \
#       && GDBSCRIPT=stripes_dump.gdb ./diag_run.sh 90
# Then: python3 tools/stripes_diff.py
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol.  If this prints only its
# header, suspect a probe global missing from PROBE_SYMS, not a dead scene.
set pagination off
set confirm off

tbreak Revs::render if g_fpsFrames >= 3
continue

printf "=== vbi=%u painted=%lu front=%08x copper=%08x bytes=%u\n", \
  g_vbiCount, g_fpsFrames, g_screenFrontAddr, g_screenCopperAddr, g_screenBytes
printf "=== bands=%u rejects=%lu overflow=%u ula=%02x\n", \
  g_bandCount, g_bandRejects, g_bandOverflow, g_ulaControl
# ⭐ The unrolled fill chain in the $7B00-$7FFF overlay ends by having its STORE OPCODE
# patched to RTS ($60 vs $91) — that is how the right-hand end of each span is set.  The
# generated C dispatches on mem[$7C0F] etc. and calls platform_smc_unhandled() for any OTHER
# value, then RETURNS — abandoning the rest of the line.  A line abandoned part-way over a
# cleared buffer is exactly a black run to the right edge, so this counter is the first thing
# to read, not the last.
printf "=== smc unhandled=%lu site=%04x value=%02x\n", \
  g_smcUnhandled, g_smcSite, g_smcValue

# What the display path produced...
dump binary memory .run/planes.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)
dump binary memory .run/copper.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)
# ...and the BBC frame buffer it was produced FROM, at the same instant: $5A80 + 8320 bytes.
# ⚠ `&mem[N]`, not `(char*)mem + N`: mem is `volatile uint8_t[65536]`, and gdb rejects the
# cast form with "A syntax error in expression" — which aborts the whole script file, so the
# planes get dumped and this does not, leaving a half-dump that looks like a successful run.
dump binary memory .run/membuf.bin &mem[0x5A80] &mem[0x5A80 + 8320]
# ...and the WHOLE 64 KB, so a host/target diff can localise the cause instead of confirming
# the symptom.  The fill chain reads its columns from $3000-$4400, translates through $6000,
# and ends each span via SMC opcode slots inside its own $7B00-$7FFF code — all three are
# needed to tell "the columns arrived empty" from "the chain mishandled them".
dump binary memory .run/mem64k.bin &mem[0] &mem[0xFFFF]
printf "=== dumped\n"
detach
quit
