# ⭐ The terrain painter's gate: g_terrainMismatch MUST be 0 (make TERRAIN=1 TERRAINCHECK=1).
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "TERRAIN events=%lu  checks=%lu  MISMATCH=%lu at=%04x\n", \
  g_viewEvents, g_terrainChecks, g_terrainMismatch, g_terrainMismatchAt
