# ⭐ THE MODE 7 DECODE'S ORACLE (make TTCHECK=1): the cell-granular decode against the old
# whole-page row loop, all 30000 bitmap bytes after every decode.  mismatch must be 0, runs > 0.
# Build: make clean && make TTCHECK=1 [COMPETITION=1 walks the menus; plain sits in the first]
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=ttcheck.gdb ./diag_run.sh 60
set pagination off
set confirm off
continue
printf "TTCHECK runs=%u mismatch=%u first=%u  rows=%u cells=%u  vbi=%u\n", \
  g_ttCheckRuns, g_ttCheckMismatch, g_ttCheckFirst, g_ttRowsDrawn, g_ttCellsDrawn, g_vbiCount
detach
quit
