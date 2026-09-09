# ⭐ THE POLL RATE IN THE FRONT END — the other half of the double-press diagnosis.
#
# A level-polled key can only be MISSED if the poll is rare compared with the press.  In the
# race there is no question (the body polls every frame), but the front end has two very
# different loops and only one of them is fast:
#   wait_dismiss ($34D2) spins on the keyboard and renders NOTHING, so it polls thousands of
#     times a second and cannot miss a tap.
#   menu_wait_key ($6577) calls platform_render_frame + tick_vbi + poll_events EVERY iteration
#     before it scans the key table, so its poll rate is the RENDER rate.  On the BBC that loop
#     costs nothing (screen RAM *is* the display); in this port each iteration converts a MODE 7
#     page to bitplanes.  If that leaves fewer than ~20 polls a second, a normal 60-80 ms tap
#     lands between two polls and is lost outright — which is exactly the reported defect.
#
# So the number to read is polls PER SECOND with nothing held.  Run a plain build (no
# STRAIGHT_TO_RACE) so the front end is up for the whole run, and no key at all:
#   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=spacerate.gdb ./diag_run.sh 30
# ⚠ Under warp the run is ~4.9x faster than the wall clock, so derive the seconds from
# g_vbiCount (50 Hz emulated), never from the wall time asked for.
set pagination off
set confirm off

continue
printf "=== front-end SPACE poll rate ===\n"
printf "vbi=%u  (= %d.%02d s emulated)\n", g_vbiCount, g_vbiCount/50, (g_vbiCount%50)*2
printf "polls=%lu answered=%lu edges=%lu\n", g_spacePolls, g_spaceAnswered, g_spaceEdges
printf "polls per second = %d\n", (g_spacePolls*50)/g_vbiCount
printf "polls per vbi    = %d.%03d\n", g_spacePolls/g_vbiCount, ((g_spacePolls*1000)/g_vbiCount)%1000
detach
quit
