# ⭐ SINGLE-STEP THE OTHER CARS IN THE RACE PROPER — steptrace.gdb's method, aimed at
# move_and_draw_cars_steps (drive_other_cars and draw_car_field are inlined into it, so one trace
# covers phase 17's five jobs; tools/steptrace_report.py splits them by source line).
# Build: make clean && make RACEPROPER=1 FIXED_RNG=1 HOLD_THROTTLE=1 SIMLEGACY=1 (no PROBES needed).
# The first break is conditional on session_is_race ($006C) bit 7, so it stops ~2000 times in
# qualifying and is then deleted; SKIP race frames later the trace starts (off the grid).
# ⚠ ~25 minutes of warp to reach the race: EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=race_steptrace.gdb
#   ./diag_run.sh 5400 (the script ends in `quit`, so the wrapper returns when the trace is done).
set pagination off
set confirm off
printf "OFF %x move_and_draw_cars_steps\n", &move_and_draw_cars_steps
break *move_and_draw_cars_steps if mem[0x6C] >= 0x80
continue
delete
printf "RACE vbi=%d\n", g_vbiCount
break *move_and_draw_cars_steps
ignore 2 150
continue
set $call = 0
while $call < 3
  set $ret = *(unsigned int *)$sp
  set $n = 0
  while $pc != $ret && $n < 400000
    stepi
    printf "T %x\n", $pc
    set $n = $n + 1
  end
  printf "C %d %d\n", $call, $n
  set $call = $call + 1
  continue
end
detach
quit
