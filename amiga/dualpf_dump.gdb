# ⭐⭐ THE DUAL-PLAYFIELD LAYER, LOOKED AT — `make DUALPF=1` (Makefile §DUALPF).
#
# Dumps BOTH playfields, the BBC frame buffer they came from and the per-line silhouette the
# cockpit layer masked with, so tools/revs_dualpf.py can do the one check this step owes: the
# COMPOSITE of PF2 over PF1 must equal the plain expansion of mem[] — i.e. the decomposition
# is invisible.  It also prints the layer's own state, which is the A/B switch announcing
# itself: g_cockpitPen3 MUST be 0 (a car pixel at BBC pen 3 has no opaque PF2 pen) and
# g_cockpitBadSlot MUST be 0 (a silhouette table entry that did not decode to a chain slot).
#
# Run:  . ./env.sh && make clean && make PROBES=1 DUALPF=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 FPSCOUNT=1 \
#         && EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=dualpf_dump.gdb ./diag_run.sh 60
# Then: python3 tools/revs_dualpf.py
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 600
continue

printf "=== vbi=%u painted=%lu pf1(back)=%08x pf2=%08x bytes=%u\n", \
  g_vbiCount, g_fpsFrames, g_screenBackAddr, g_screenCockpitAddr, g_screenBytes
printf "=== cockpit cells=%u total=%lu runs=%lu pen3=%lu badslot=%lu\n", \
  g_cockpitCells, g_cockpitCellsTotal, g_cockpitRuns, g_cockpitPen3, g_cockpitBadSlot
printf "=== decode cells=%u modedirty=%u\n", g_decodeCells, g_decodeModeDirty

# ⚠ THE BACK BUFFER, NOT THE FRONT ONE: mem[] has moved on since the displayed buffer was
# decoded, and comparing the two reads a frame of staleness as a defect in the layer.
dump binary memory .run/pf1.bin    ((char*)g_screenBackAddr)    ((char*)g_screenBackAddr + g_screenBytes)
dump binary memory .run/pf2.bin    ((char*)g_screenCockpitAddr) ((char*)g_screenCockpitAddr + g_screenBytes)
dump binary memory .run/copper.bin ((char*)g_screenCopperAddr)  ((char*)g_screenCopperAddr + 4*g_screenCopperWords)
dump binary memory .run/fb.bin     ((char*)&mem[0x5A80])        ((char*)&mem[0x8000])
dump binary memory .run/cockrun.bin ((char*)&s_cockRun[0][0])   ((char*)&s_cockRun[0][0] + 41*4)

# ⭐⭐ THE DYNAMIC FOOTPRINT (DUALPFCHECK=1 only) — which cells of 117..157 are not static art,
# enumerated over the whole run rather than sampled.  Zero without the check build.
printf "=== change census over %lu frames\n", g_cockChangeFrames
dump binary memory .run/cockchange.bin ((char*)&g_cockChange[0][0]) ((char*)&g_cockChange[0][0] + 41*40*2)
printf "=== dumped\n"
detach
quit
