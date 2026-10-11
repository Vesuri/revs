# Splash-to-game Chip RAM lifetime regression.
# Build: make clean && make PROBES=1 NOAUTORUN=1 TAPTEST=1 NEEDLECHECK=1
# Run: GDBSCRIPT=splash_needles.gdb ./diag_run.sh 90
# TAPTEST traverses the real title and circuit menu before driving into practice.
# Do not add NEEDLEVERIFY: it disables the sprite cache this gate must exercise.
# The title must share the full needle pool; gameplay must produce checked sprites
# and matching colours without pool exhaustion. Also run the DIST exe under WHDLoad
# and inspect its pool allocation: diagnostic builds have a different memory footprint.
set pagination off
set confirm off
tbreak Revs::render if g_screenPlanes == 5
continue
printf "=== SPLASH pool=%p bitmap=%08x capacity=%lu bytes\n", s_ndlPool, g_screenFrontAddr, s_ndlPoolWords * 2
if s_ndlPool == 0 || (unsigned long)s_ndlPool != g_screenFrontAddr || s_ndlPoolWords != 20480
  printf "FAIL: splash does not share the complete needle pool\n"
  kill
  quit 1
end
tbreak Revs::render if g_vbiCount >= 1500
continue
printf "=== NEEDLE paints=%lu renders=%lu pixels/frame(last)=%u pens=%u,%u\n", \
  g_needlePaints, g_needleRenders, g_needlePixLast, g_needlePen[0], g_needlePen[1]
printf "    outside=%lu maskBad=%lu overflow=%lu poolFull=%lu tooWide=%lu colourBad=%lu\n", \
  g_needleOutside, g_needleMaskBad, g_needleOverflow, g_needlePoolFull, g_needleTooWide, \
  g_needleColourBad
printf "    sprite check checks=%lu mismatch=%lu\n", g_needleSpriteChecks, g_needleSpriteMismatch
# ⭐ THE COLOURS: in every raster band the rev needle's COLOR21 must equal that band's COLOR02
# (BBC pen 2) and the steering mark's COLOR25 its COLOR00 (BBC pen 0) — what the BBC's own plotted
# pixel would have shown on that line.  Walks the live copper list.
set $i = 0
set $c0 = -1
set $c2 = -1
set $cchk = 0
set $cbad = 0
while $i < g_screenCopperWords
  set $w = *(unsigned int *)(g_screenCopperAddr + 4 * $i)
  set $r = $w >> 16
  set $v = $w & 0xFFFF
  if $r == 0x180
    set $c0 = $v
  end
  if $r == 0x184
    set $c2 = $v
  end
  if $r == 0x1AA
    set $cchk = $cchk + 1
    if $v != $c2
      set $cbad = $cbad + 1
    end
  end
  if $r == 0x1B2
    set $cchk = $cchk + 1
    if $v != $c0
      set $cbad = $cbad + 1
    end
  end
  set $i = $i + 1
end
printf "    colours     checks=%d mismatch=%d\n", $cchk, $cbad
if $cbad == 0 && $cchk >= 4 && g_needleOutside == 0 && g_needleMaskBad == 0 && g_needleOverflow == 0 && g_needlePoolFull == 0 && g_needleTooWide == 0 && g_needleColourBad == 0 && g_needleSpriteMismatch == 0 && g_needleSpriteChecks > 20
  printf "    PASS\n"
else
  printf "    FAIL\n"
  kill
  quit 1
end
kill
quit
