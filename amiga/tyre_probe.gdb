# ⭐⭐ THE TYRE SPRITE BUILD'S OWN REPORT (needs `make TYRESPRITE=1`).
#   g_tyrePeriodBad  ⚠⚠ MUST BE 0 — the EOR's mask is state-dependent (`if (r != 0) continue`),
#                    so "the animation has two states" is CHECKED here, not assumed.  Non-zero
#                    means two precomputed sprites cannot represent it.
#   g_tyreZeroAnim   animated pixels that are colour 0 in one of the states.  A sprite cannot
#                    draw colour 0 (it is transparent), so this constrains the step that owns
#                    these rows: the playfield must be colour 0 under exactly those pixels.
#   g_tyreBuilds     1 = the build ran.  0 is a VACUOUS pass, not a good one.
# ⚠ This says nothing about where the sprite APPEARS.  screen_dump.gdb dumps the bitplane buffer
# and a sprite is not in it — position and colour need a real screen.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== TYRES builds=%lu periodBad=%lu zeroAnim=%lu phase=%u (vbi=%u)\n", \
  g_tyreBuilds, g_tyrePeriodBad, g_tyreZeroAnim, g_tyrePhase, g_vbiCount
printf "verdict: %s\n", \
  g_tyreBuilds == 0 ? "VACUOUS — the build never ran" : \
  (g_tyrePeriodBad ? "FAIL — the EOR does not have period 2" : "PASS — two states suffice")
kill
quit
