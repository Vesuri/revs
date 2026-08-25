# ⭐ WHY DOES A CRASH FREEZE THE GAME FOR ~5s?  (src/platform/probe.h, Revs.cpp §CRASH-FREEZE)
#
# race_frame_tail holds the picture for 100 field-countdown INCs (= 2 s at 50 Hz) after a crash.
# Those INCs come from tick_wheel_spin, run once per DRAINED field cycle (Revs::runBandCycle).
# If a field cycle costs > 20 ms the VERTB ISR generates PAL fields faster than we drain them,
# the surplus hits the s_pendingTicks cap and is DROPPED (never INCing field_countdown), so the
# 2 s hold stretches to whatever (100 drained ticks) x (real ms per drain) works out to.
#
# Build: cd amiga && make clean && make -j4 CRASHPROBE=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 FIXED_RNG=1
#        (add PROBES=1 for the DIRECT per-field beam cost g_fieldBeam*; near-shipping without it)
# Run:   . ./env.sh && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=crashprobe.gdb ./diag_run.sh 40
#        cat .run/gdb-out.log
#
# HOLD_THROTTLE keeps the throttle down so the car eventually leaves the track and crashes; a
# 40 s warp run provokes several.  Warp changes no ratio here (all quantities are emulated).
set pagination off
set confirm off
continue

printf "=== run: vbi=%u fields (%u.%01us wall)  field cycles drained=%lu ===\n", \
  g_vbiCount, g_vbiCount/50, (g_vbiCount%50)*2, g_fieldCycles

# --- THE CRASH HOLDS: is the hold longer than the intended 2 s? --------------------------------
if g_crashHolds == 0
  printf "\nno crash occurred in this window — extend the run or confirm the car is driving\n"
else
  printf "\ncrashes measured: %lu\n", g_crashHolds
  printf "  longest single hold : %lu wall fields  = %lu.%01lu s   (intended: 100 fields = 2.0 s)\n", \
    g_crashHoldVbiMax, (g_crashHoldVbiMax*20)/1000, ((g_crashHoldVbiMax*20)/100)%10
  printf "  mean per hold       : %lu wall fields,  %lu drained ticks,  %lu dropped\n", \
    g_crashHoldVbi/g_crashHolds, g_crashHoldTicks/g_crashHolds, g_crashHoldDrops/g_crashHolds
  # The decisive ratio: wall fields per drained tick.  ~1 => keeping up (faithful 2 s);
  # >1 with drops>0 => drain-starved, i.e. a field cycle really is costing > 20 ms.
  if g_crashHoldTicks > 0
    printf "  => %lu.%02lu wall fields per drained tick  =>  ~%lu.%01lu ms of real time per field cycle\n", \
      (g_crashHoldVbi*100/g_crashHoldTicks)/100, (g_crashHoldVbi*100/g_crashHoldTicks)%100, \
      (g_crashHoldVbi*20/g_crashHoldTicks), (g_crashHoldVbi*200/g_crashHoldTicks)%10
    if g_crashHoldDrops > 0
      printf "  VERDICT: drain-starved — fields are dropped during the hold, the theorem HOLDS\n"
    else
      printf "  VERDICT: no drops — the hold keeps up; the freeze is NOT the hold (look at the restart)\n"
    end
  end
end

# --- THE FREEZE LOCATOR: worst present->present gap -------------------------------------------
# The screen only repaints at renderFrame().  A ~5s freeze is ONE gap of ~250 fields, wherever
# it comes from (a hold with no present, a restart, a heavy frame).
printf "\nworst present->present gap: %lu PAL fields = %lu.%01lu s   (ended at vbi=%lu)\n", \
  g_renderGapMax, (g_renderGapMax*20)/1000, ((g_renderGapMax*20)/100)%10, g_renderGapMaxAt
printf "visible stalls (gap >= 25 fields / 0.5s): %lu\n", g_renderStalls

# --- ATTRIBUTE THE FROZEN INTERVAL: body + hold + reset ---------------------------------------
printf "\nfreeze breakdown (worst of each, PAL fields):\n"
printf "  crash-frame body (fence draw incl.): %lu fields = %lu.%01lu s\n", \
  g_crashBodyFieldsMax, (g_crashBodyFieldsMax*20)/1000, ((g_crashBodyFieldsMax*20)/100)%10
printf "  hold (field_countdown, meant 2.0s) : %lu fields = %lu.%01lu s\n", \
  g_crashHoldVbiMax, (g_crashHoldVbiMax*20)/1000, ((g_crashHoldVbiMax*20)/100)%10
printf "  session reset (worst of %lu)         : %lu fields = %lu.%01lu s   (last=%lu)\n", \
  g_resetCount, g_resetFieldsMax, (g_resetFieldsMax*20)/1000, ((g_resetFieldsMax*20)/100)%10, g_resetFieldsLast
# --- WHICH reset call eats the 3 s? (last reset's per-call field split) ------------------------
printf "  reset split (last reset, PAL fields):\n"
printf "    clear_race_clock       : %lu  (%lu ms)\n", g_resetSplit[0], g_resetSplit[0]*20
printf "    reset_driving_variables: %lu  (%lu ms)\n", g_resetSplit[1], g_resetSplit[1]*20
printf "    build_player_car       : %lu  (%lu ms)\n", g_resetSplit[2], g_resetSplit[2]*20
printf "    scale_wing_settings    : %lu  (%lu ms)\n", g_resetSplit[3], g_resetSplit[3]*20

# --- DIRECT per-field beam cost (PROBES only) --------------------------------------------------
# g_fieldBeam* are only written when built +PROBES (they need the g_beamEpoch the VERTB ISR bumps).
# 4006 beam ticks = 1 ms (80128 ticks / 20 ms frame).
if g_fieldBeamTicks > 0
  printf "\ndirect field-cycle cost (fireIrq1vField), %lu cycles timed:\n", g_fieldCycles
  printf "  mean = %lu us  (%lu.%01lu ms)   max = %lu us  (%lu.%01lu ms)\n", \
    (g_fieldBeamTicks/g_fieldCycles)*1000/4006, \
    (g_fieldBeamTicks/g_fieldCycles)/4006, ((g_fieldBeamTicks/g_fieldCycles)*10/4006)%10, \
    g_fieldBeamMax*1000/4006, g_fieldBeamMax/4006, (g_fieldBeamMax*10/4006)%10
  printf "  (one PAL field = 20 ms = 80128 beam ticks; > 20 ms mean means the theorem HOLDS)\n"
else
  printf "\n(direct beam cost not measured — rebuild with PROBES=1 for g_fieldBeam*)\n"
end
detach
quit
