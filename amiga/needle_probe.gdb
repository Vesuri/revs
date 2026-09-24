# ⭐ §12d — the needle sprites' gates (`make PROBES=1 [NEEDLECHECK=1] [NEEDLEVERIFY=1]`).
#   outside/maskBad/overflow   : the geometry, the mask tables, the list bound
#   poolFull/tooWide/colourBad : an image not shown, clipped, or of two colours
#   renders                    : images built — rises to ~233 over a session, then stays flat
#   pens                       : the BBC pen each mark draws in (rev 2, steering 0)
# With NEEDLECHECK=1 and NEEDLEVERIFY=1 the two mismatch lines are the chain from the BBC's own
# plot (verify) to the shown sprite (check); both must be 0 and both checks must be large.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 2500
continue
printf "=== NEEDLE paints=%lu renders=%lu pixels/frame(last)=%u pens=%u,%u\n", \
  g_needlePaints, g_needleRenders, g_needlePixLast, g_needlePen[0], g_needlePen[1]
printf "    outside=%lu maskBad=%lu overflow=%lu poolFull=%lu tooWide=%lu colourBad=%lu\n", \
  g_needleOutside, g_needleMaskBad, g_needleOverflow, g_needlePoolFull, g_needleTooWide, \
  g_needleColourBad
printf "    sprite check checks=%lu mismatch=%lu\n", g_needleSpriteChecks, g_needleSpriteMismatch
printf "    BBC verify  checks=%lu mismatch=%lu at=%04X\n", \
  g_needleVerifyChecks, g_needleVerifyMismatch, g_needleVerifyMismatchAt
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
if $cbad == 0 && $cchk >= 4 && g_needleOutside == 0 && g_needleMaskBad == 0 && g_needleOverflow == 0 && g_needlePoolFull == 0 && g_needleTooWide == 0 && g_needleColourBad == 0 && g_needleSpriteMismatch == 0 && g_needleVerifyMismatch == 0 && g_needleSpriteChecks > 20 && g_needleVerifyChecks > 20
  printf "    PASS\n"
else
  printf "    FAIL\n"
end
kill
quit
