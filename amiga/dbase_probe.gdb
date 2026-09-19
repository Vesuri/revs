# ⭐ IS THE DELTA BASE BEING RE-LAID EVERY SWEEP?  `plotDeltaBase` writes 34 rows x 40 cells x
# 4 plane bytes (~5 440 stores) and runs from `revs_plot_own_reset`, which is inside phase 24's
# bracket — so a band record that JITTERS makes `deltaKindRebuild` report a change every sweep
# and re-bases ~5 440 stores a frame inside the phase.  Healthy is a handful for the whole run
# (one per MODE 7 round trip); one per sweep is the defect.
set pagination off
set confirm off
continue
printf "=== sweeps=%lu  deltaBases=%lu  deltaBytes=%lu ===\n", \
  g_phaseFrames, g_plotDeltaBases, g_plotDeltaBytes
printf "    bases per sweep must be << 1  (a re-base is ~5440 plane stores)\n"
