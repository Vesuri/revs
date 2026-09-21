# ⭐ §12d — the needle painter's gates.
#   outside/maskBad/overflow : the geometry, the mask tables, the list bound
#   baseMismatch             : `mem[]` under the needle never changes (the cached cockpit)
#   restoreMismatch          : the erase is complete — the column equals the clean cockpit
#                              again before a pixel of the new mark is drawn
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 1200
continue
printf "=== NEEDLE paints=%lu pixels/frame(last)=%u bases=%lu\n", \
  g_needlePaints, g_needlePixLast, g_needleBases
printf "    outside=%lu maskBad=%lu overflow=%lu\n", \
  g_needleOutside, g_needleMaskBad, g_needleOverflow
printf "    base    checks=%lu mismatch=%lu at=%04X\n", \
  g_needleBaseChecks, g_needleBaseMismatch, g_needleBaseMismatchAt
printf "    restore checks=%lu mismatch=%lu at=%04X\n", \
  g_needleRestoreChecks, g_needleRestoreMismatch, g_needleRestoreMismatchAt
if g_needleOutside == 0 && g_needleMaskBad == 0 && g_needleOverflow == 0 && g_needleBaseMismatch == 0 && g_needleRestoreMismatch == 0 && g_needleRestoreChecks > 20
  printf "    PASS\n"
else
  printf "    FAIL\n"
end
kill
quit
