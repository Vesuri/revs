# ⭐ THE FLAT-BAND SKIP — did it engage, and is the region it stopped decoding still uniform?
#
# Phase 6 item 0 step 1 (docs/direct-bitplane-plan.md §4a): decode() skips every display line
# inside a palette band whose four colour registers hold the same colour.  On the BBC that is
# band 1 — 63 lines, 18..81 — and those lines carry live engine code, so the whole optimisation
# rests on the claim "nobody can see what is in those planes".
#
# Two things this must show, and neither is optional:
#   1. g_decodeFlatLines = 63 in ONE band.  A zero says the skip never engaged and any FPS
#      difference measured alongside it is noise (docs/method-lessons.md: verify the instrument).
#   2. The dumped picture is still uniform over those lines.  Because tools/amiga_ppm.py takes
#      its palette from the copper list's own MOVEs, a flat band decodes to one colour whatever
#      the plane bytes are — so uniformity here is a check on the BAND, which is the part the
#      skip actually depends on.
#
# Run:  . ./env.sh && make clean && make STRAIGHT_TO_RACE=1 FPSCOUNT=1 FIXED_RNG=1
#       GDBSCRIPT=flatskip.gdb ./diag_run.sh 90
# Then: python3 tools/amiga_ppm.py amiga/.run/planes.bin amiga/.run/copper.bin tmp/flat
#       python3 tools/flat_check.py tmp/flat.ppm
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 600
continue

printf "=== vbi=%u painted=%lu  flatLines=%u flatBands=%u  rejects=%lu\n", \
  g_vbiCount, g_fpsFrames, g_decodeFlatLines, g_decodeFlatBands, g_bandRejects
printf "=== front=%08x copper=%08x bytes=%u words=%u mode7=%u\n", \
  g_screenFrontAddr, g_screenCopperAddr, g_screenBytes, g_screenCopperWords, g_screenMode7

dump binary memory .run/planes.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)
dump binary memory .run/copper.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)
printf "=== dumped\n"
detach
quit
