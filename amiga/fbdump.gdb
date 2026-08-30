# ⭐⭐ SPLIT "ENGINE" FROM "DECODE" IN ONE CAPTURE.
#
# The horizon artefact could live in either half, and the two need different fixes:
#   mem[] frame buffer   — what the ENGINE drew.  Byte-comparable against the real BBC's own
#                          frame buffer from `make refloop --park` (/tmp/realpark/bbc_fb_*.bin).
#   planes + copper      — what the PORT's decode made of it.
# Dumping both at ONE instant is the point: comparing an engine dump from one run against a
# picture from another cannot distinguish them.
#
# Run:  . ./env.sh && make STRAIGHT_TO_RACE=1 FILLWATCH=1 TRACK=n FIXED_RNG=1 \
#       && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=fbdump.gdb ./diag_run.sh 120
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 6000
continue

# $5A80..$7FFF — the 8320-byte screen AND the $7B00 dash-code page after it, which is what the
# refloop's own dump covers ($5a80 + $2580).  Same span both sides or the offsets do not line up.
dump binary memory .run/fb_mem.bin &mem[0x5A80] &mem[0x8000]
dump binary memory .run/fb_planes.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)
dump binary memory .run/fb_copper.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)
printf "fbdump vbi=%u front=%08x track=%u hookCalls=%lu flatLines=%u bandRejects=%lu\n", \
  g_vbiCount, g_screenFrontAddr, g_trackInstalled, g_trackHookCalls, \
  g_decodeFlatLines, g_bandRejects
printf "=== fbdump done\n"
detach
quit
