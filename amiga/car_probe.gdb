# ⭐⭐⭐ IS THE CAR'S SILHOUETTE STATIC?  — and it IS: byte-identical at vbi 606 and 902.
#
# The view sweep paints every line of display rows 117..157 as TWO RUNS, clipped to
# view_run_left_end ($3150) / view_run_right_start ($30D0), with the boundary cell composed at
# pixel precision through view_left_end_mask ($38D0) and its fill partner.  That clipping is
# what phases 2 and 3 cost 27 ms/frame for, and `fill_dash_edge_columns` (10.2 ms) exists to
# re-seed the source bytes it reads.
#
# This prints the three tables at two well-separated fields.  They do not move — the split is
# the DASHBOARD, not the road — so the pixels between the runs are a STATIC bitmap the terrain
# renderer can simply not paint over, and the boundary masks are constants.
#
# Run:  . ./env.sh && make STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 FPSCOUNT=1
#       EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=car_probe.gdb ./diag_run.sh 45
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 600
continue
printf "=== A vbi=%u\n", g_vbiCount
printf "leftEnd  :"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x3150 + $i]
  set $i = $i + 1
end
printf "\nrightStart:"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x30D0 + $i]
  set $i = $i + 1
end
printf "\nleftMask :"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x38D0 + $i]
  set $i = $i + 1
end
printf "\n"
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== B vbi=%u\n", g_vbiCount
printf "leftEnd  :"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x3150 + $i]
  set $i = $i + 1
end
printf "\nrightStart:"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x30D0 + $i]
  set $i = $i + 1
end
printf "\nleftMask :"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x38D0 + $i]
  set $i = $i + 1
end
printf "\n"
