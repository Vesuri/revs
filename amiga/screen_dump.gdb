# ⭐ WHAT THE TARGET IS ACTUALLY DISPLAYING — dump it and look at it.
#
# The port's visual ground truth cannot be the host build (it has no renderer, deliberately:
# src/platform/host/PlatformHost.h), and it cannot be an eyeball on an FS-UAE window either,
# because a band or decode bug an eye calls "looks a bit off" is a specific wrong number.
# So: break once the scene is steady, dump the DISPLAYED bitplane block and the copper list
# out of the emulated machine, and let tools/amiga_ppm.py decode them on the host.
#
# Run:  . ./env.sh && make FPSCOUNT=1 && GDBSCRIPT=screen_dump.gdb ./diag_run.sh 60
# Then: python3 tools/amiga_ppm.py amiga/.run/planes.bin amiga/.run/copper.bin tmp/amiga
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol.  If this prints only its
# header, suspect a probe global missing from PROBE_SYMS, not a dead scene.
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 600
continue

printf "=== vbi=%u painted=%lu front=%08x copper=%08x bytes=%u\n", \
  g_vbiCount, g_fpsFrames, g_screenFrontAddr, g_screenCopperAddr, g_screenBytes
printf "=== bands=%u rejects=%lu overflow=%u ula=%02x brk=%lu\n", \
  g_bandCount, g_bandRejects, g_bandOverflow, g_ulaControl, g_brkCount

dump binary memory .run/planes.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)
# ⚠ The length comes from the program (g_screenCopperWords = LIST_LENGTH), not from a
# constant retyped here.  A hard-coded word count silently truncates the moment the list
# layout grows — and a truncated copper dump decodes as "the bands are missing", which reads
# exactly like a band bug.  (docs/method-lessons.md: verify the instrument.)
dump binary memory .run/copper.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)
printf "=== dumped\n"
detach
quit
