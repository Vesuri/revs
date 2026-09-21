# ⭐⭐ §12d — the painter against the decode, pixel for pixel (`make NEEDLEVERIFY=1`).
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 1200
continue
printf "=== NEEDLE VERIFY checks=%lu mismatch=%lu at=%04X   pixels(last)=%u\n", \
  g_needleVerifyChecks, g_needleVerifyMismatch, g_needleVerifyMismatchAt, g_needlePixLast
printf "    outside=%lu maskBad=%lu overflow=%lu\n", \
  g_needleOutside, g_needleMaskBad, g_needleOverflow
if g_needleVerifyChecks > 20 && g_needleVerifyMismatch == 0 && g_needleOutside == 0
  printf "    PASS\n"
else
  printf "    FAIL\n"
end
kill
quit
