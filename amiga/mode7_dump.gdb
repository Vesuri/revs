# ⭐ WHAT THE TARGET SHOWS IN THE MODE 7 FRONT END — dump it and look at it.
#
# Run:  . ./env.sh && make && GDBSCRIPT=mode7_dump.gdb ./diag_run.sh 40
# Then: python3 tools/amiga_ppm.py amiga/.run/planes.bin amiga/.run/copper.bin tmp/m7amiga
#
# The decoder reads the SHAPE out of the program (g_screenPlanes / g_screenHeight /
# g_screenMode7) rather than assuming the race view's 320x208 two-plane layout, because the port
# now has two display configurations and a dumper that assumes one decodes the other as garbage.
#
# ⚠ Same blind spot as screen_dump.gdb, and it matters more here because MODE 7 has its OWN
# window and plane count: this dumps the bitplane BUFFER and the copper list, so it says nothing
# about DIWSTRT/DIWSTOP, DDFSTRT/DDFSTOP or BPLCON0's plane count.  A wrong DIWSTOP for the
# 250-line window leaves this dump byte-identical to a correct one.  Those are write-only; the
# 250-line encoding is derived once in RevsScreen::setDisplayWindow with the reasoning attached.
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol.  If this prints only its
# header, suspect a probe global missing from PROBE_SYMS, not a dead scene.
set pagination off
set confirm off

# Break once the front end has had time to draw.  ⚠ NOT on a fixed vbi count alone: the
# condition is that the MODE 7 configuration is actually on screen, so a run that never got
# there fails loudly here instead of dumping the race view and reading as a MODE 7 bug.
tbreak Revs::render if g_screenMode7 != 0 && g_vbiCount >= 200
continue

printf "=== vbi=%u mode7=%u planes=%u height=%u front=%08x copper=%08x bytes=%u words=%u\n", \
  g_vbiCount, g_screenMode7, g_screenPlanes, g_screenHeight, \
  g_screenFrontAddr, g_screenCopperAddr, g_screenBytes, g_screenCopperWords

# ⭐ THE DRIVER'S OWN HEALTH.  g_ttUnknownVdu must be 0: an unhandled VDU code still consumes
# the right parameter count, so the page can look plausible and be wrong further down.
# g_ttModeDisagree must be 0 too — it compares this port's hardware-derived mode against the
# GAME's own $64 flag, and a non-zero value means one of the two derivations is wrong.
printf "=== vdu bytes=%lu unknown=%lu (last $%02x) modeSwitches=%lu disagree=%lu\n", \
  g_ttVduBytes, g_ttUnknownVdu, g_ttLastUnknown, g_ttModeSwitches, g_ttModeDisagree
printf "=== flash phase=%u toggles=%lu\n", g_ttFlashPhase, g_ttFlashToggles
printf "=== ttAllocFailed=%u (bit0 bitmap, bit1 copper list)\n", g_ttAllocFailed
printf "=== mos: unknown=%lu charDef=%lu   brk=%lu   $64=$%02x\n", \
  g_mosUnknownCount, g_mosCharDefCount, g_brkCount, mem[0x64]

dump binary memory .run/planes.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)
dump binary memory .run/copper.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)
# ⭐ The teletext PAGE itself, so the target's screen RAM can be diffed against the real BBC's
# dumps (tmp/mode7/mode7_NN.bin) — the same fixture `make mode7` uses on the host.  A picture
# that looks wrong is then attributable to the driver or to the renderer, not to "MODE 7".
dump binary memory .run/mode7_page.bin ((char*)&mem[0x7c00]) ((char*)&mem[0x8000])
printf "=== dumped\n"
detach
quit
