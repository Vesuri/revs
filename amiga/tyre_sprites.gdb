# ⭐⭐ WHERE THE TYRE SPRITES ARE, WHAT COLOUR THEY ARE AND WHAT THEY CONTAIN (`make TYRESPRITE=1`).
#
# `tyre_probe.gdb` reports the BUILD (period-2, zeroAnim).  This reports the three things the
# build cannot see, each of which was wrong on the target once and none of which any headless
# byte differential can reach — all three read out of the COPPER LIST, so no access to
# RevsScreen's privates is needed:
#
#   SPRPAL   the three sprite pens, WITH THEIR REGISTER NUMBERS.  ⚠ COLOR16 is $1A0, so the
#            first sprite pen (COLOR17) is $1A2 — writing from $1A0 put every pen one register
#            low and the tread came out in PEN 3's colour (green instead of white).
#   ctl      SPRxPOS/SPRxCTL.  HSTART = ((POS & $FF) << 1) | (CTL & 1) and it must be $80 + x,
#            NOT $81: a sprite at $81 sits one lores pixel right of the playfield's first pixel,
#            which puts every doubled pair astride a MODE 5 pixel boundary.
#   L../R..  the eleven data word pairs of each image: w0 = plane 0, w1 = plane 1, so a MODE 5
#            colour-2 pixel is a PAIR of bits in w1 (each MODE 5 pixel is two lores pixels).
#
# ⭐ THE FULL CORRECTNESS CHECK IS OFF-TARGET AND IS WORTH RE-RUNNING AFTER ANY CHANGE HERE:
# dump the planes too (screen_dump.gdb), then compose `outline plane + image A` and
# `outline plane + image B` and require them to equal the game's own two dither states — state A
# is the `mem[]` bytes printed below and state B is state A with every ANIMATED pixel flipped
# between colour 0 and colour 2.  Both matched on all 16 (line, side) pairs at 2026-09-21.
#
# ⚠ AND THE PLAYFIELD MUST READ 0 UNDER EVERY ANIMATED PIXEL — that is `revs_tyres_outline`'s
# job, and it is the half a sprite cannot do (sprite colour 0 is transparent).
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== TYRES builds=%lu periodBad=%lu zeroAnim=%lu phase=%u\n", \
  g_tyreBuilds, g_tyrePeriodBad, g_tyreZeroAnim, g_tyrePhase
set $cop = (unsigned long*)g_screenCopperAddr
printf "SPRPAL  %03lx <- %04lx   %03lx <- %04lx   %03lx <- %04lx  (want 1a2/1a4/1a6)\n", \
  ($cop[28]>>16)&0x1fe, $cop[28]&0xffff, ($cop[29]>>16)&0x1fe, $cop[29]&0xffff, \
  ($cop[30]>>16)&0x1fe, $cop[30]&0xffff
set $s0 = (unsigned short*)((($cop[1] & 0xffff) << 16) | ($cop[2] & 0xffff))
set $s1 = (unsigned short*)((($cop[3] & 0xffff) << 16) | ($cop[4] & 0xffff))
printf "spr0 @ %p ctl %04x %04x  HSTART=%03x (want 080)\n", \
  $s0, $s0[0], $s0[1], (($s0[0] & 0xff) << 1) | ($s0[1] & 1)
printf "spr1 @ %p ctl %04x %04x  HSTART=%03x (want 1b0)\n", \
  $s1, $s1[0], $s1[1], (($s1[0] & 0xff) << 1) | ($s1[1] & 1)
set $i = 0
while $i < 11
  printf "  L%02d w0=%04x w1=%04x    R%02d w0=%04x w1=%04x\n", \
    $i, $s0[2 + $i*2], $s0[3 + $i*2], $i, $s1[2 + $i*2], $s1[3 + $i*2]
  set $i = $i + 1
end
printf "--- mem[] (state A), cells 0,1 and 38,39, display lines 130..140\n"
set $l = 0
while $l < 11
  set $y = 130 + $l
  set $a = 0x5A80 + ($y / 8) * 320 + ($y % 8)
  printf "  y=%d  c0=%02x c1=%02x  c38=%02x c39=%02x\n", $y, mem[$a], mem[$a+8], mem[$a+304], mem[$a+312]
  set $l = $l + 1
end
kill
quit
