# ⭐⭐ WHY IS build_track_geometry (phase 5) ~16% of the frame?  (src/platform/probe.h §GEOSPLIT)
#
# Build: cd amiga && make clean && make -j4 GEOSPLIT=1 PROBES=1 FIXED_RNG=1 STRAIGHT_TO_RACE=1
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=geosplit.gdb ./diag_run.sh 30
#        cat .run/gdb-out.log
#
# TWO decompositions of the phase-5 row, in one run:
#   TIME   — sub-phases 40-43 (near-point reuse / walk 0 / walk 1 / horizon tail) + the driver
#            remainder left in phase 5.  These carve phase 5, so 5+40+41+42+43 IS the old row.
#   COUNTS — the tree's transforms and divides per frame: the "why" is (points) x (per-point cost),
#            and per-point cost is up to three div16by8 (8 restoring steps each).
#
# ⚠ SHARES WITHIN ONE RUN — never diff across builds (docs/perf-method.md Rule 2).  PROBES build:
# no framerate may be quoted from it.  Confirm the car is DRIVING (amiga/dash_state.gdb) — parked,
# the walks re-derive almost nothing and this table means something else.
set pagination off
set confirm off
continue
printf "=== vbi=%u loopFrames=%lu geoFrames=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_geoFrames

# --- TIME: the phase-5 sub-phases -------------------------------------------------------------
set $geo = g_phaseTicks[5] + g_phaseTicks[40] + g_phaseTicks[41] + g_phaseTicks[42] + g_phaseTicks[43]
set $per = $geo / 1000
if $per == 0
  set $per = 1
end
printf "\nbuild_track_geometry (phase 5 whole) = %lu ms/frame  (5 + brackets 40-43)\n", \
  ($geo/g_phaseFrames)/4006
printf "  %-22s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "road_edge_start (40)", \
  (g_phaseTicks[40]/g_phaseFrames)/4006, (g_phaseTicks[40]/$per)/10, (g_phaseTicks[40]/$per)%10, g_phaseCount[40]
printf "  %-22s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "walk side 0     (41)", \
  (g_phaseTicks[41]/g_phaseFrames)/4006, (g_phaseTicks[41]/$per)/10, (g_phaseTicks[41]/$per)%10, g_phaseCount[41]
printf "  %-22s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "walk side 1     (42)", \
  (g_phaseTicks[42]/g_phaseFrames)/4006, (g_phaseTicks[42]/$per)/10, (g_phaseTicks[42]/$per)%10, g_phaseCount[42]
printf "  %-22s %6lu ms/frame  %2d.%01d%% of the pass  (calls=%lu)\n", "horizon tail    (43)", \
  (g_phaseTicks[43]/g_phaseFrames)/4006, (g_phaseTicks[43]/$per)/10, (g_phaseTicks[43]/$per)%10, g_phaseCount[43]
printf "  %-22s %6lu ms/frame  %2d.%01d%% of the pass  (driver + return remainder)\n", "phase 5 remainder", \
  (g_phaseTicks[5]/g_phaseFrames)/4006, (g_phaseTicks[5]/$per)/10, (g_phaseTicks[5]/$per)%10

# --- COUNTS: the "why", per frame -------------------------------------------------------------
set $f = g_geoFrames
if $f == 0
  set $f = 1
end
set $pts = g_geoPoints[0] + g_geoPoints[1]
printf "\nper frame:\n"
printf "  edge points visited:  side0=%lu  side1=%lu  total=%lu\n", \
  g_geoPoints[0]/$f, g_geoPoints[1]/$f, $pts/$f
printf "  subdivisions:         %lu\n", g_geoSubdiv/$f
printf "  bearing_to_section:   %lu   project_point: %lu   point_distance_hypot: %lu\n", \
  g_geoBearing/$f, g_geoProject/$f, g_geoHypot/$f
printf "  div16by8 (the divide):%lu   (8 restoring steps each)\n", g_geoDiv/$f
if $pts > 0
  printf "  => %lu.%02lu div16by8 per edge point,  %lu.%02lu transforms (bearing+project) per point\n", \
    (g_geoDiv*100/$pts)/100, (g_geoDiv*100/$pts)%100, \
    ((g_geoBearing+g_geoProject)*100/$pts)/100, ((g_geoBearing+g_geoProject)*100/$pts)%100
end

# --- per-call time from the two walks (the bulk of the pass) ----------------------------------
# The walks own nearly every transform, so (walk 40+41 ticks) / (divides) prices one divide, and
# / (points) prices one fully-projected point.  A cross-check that the counts and the clock agree.
set $wt = g_phaseTicks[41] + g_phaseTicks[42]
if g_geoDiv > 0
  printf "\nwalks: %lu ms/frame over %lu divides/frame  =>  ~%lu us per div16by8 (incl. its callers)\n", \
    ($wt/g_phaseFrames)/4006, g_geoDiv/$f, (($wt/g_phaseFrames)*1000/4006)/(g_geoDiv/$f)
end
# ⚠⚠ THE PARTIAL-FREEZE TRAP, and it made this line read 4.7x LOW for as long as it existed:
# g_phaseTicks stops at the PROBEFIELDS freeze, g_geoPoints does NOT (probe.cpp has no shadow for
# it), so ticks/points divided a frozen numerator by a live denominator.  A 60 s run past a
# 3000-field window reported 199 us per edge point where the truth is ~930.
# ⇒ divide TICKS-PER-PAINTED-FRAME (both frozen) by POINTS-PER-CALL (both live).  Never mix.
if $pts > 0
  printf "walks: %lu us per edge point (bearing+hypot+project+emit)  [%lu cycles]\n", \
    (($wt/g_phaseFrames)*1000/4006)/($pts/$f), \
    ((($wt/g_phaseFrames)*1000/4006)/($pts/$f))*709/100
end
detach
quit
