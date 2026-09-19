set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "LOW clipBad=%lu  events=%lu\n", g_terrainClipBad, g_viewEvents
