# ⭐⭐ DOES THE CIRCUIT MENU RUN ON THE TARGET?  Two questions, and they need different builds.
#
#   plain build (interactive):
#     . ./env.sh && make && GDBSCRIPT=trackmenu.gdb ./diag_run.sh 30
#     -> should be sitting in the MENU, phase 1 (TM_SELECT), waiting for a digit.  The page is
#        dumped so it can be diffed against the real BBC's:
#        python3 tools/trackmenu_check.py amiga/.run/trackmenu_page.bin
#
#   unattended build (the auto path `make TRACK=n` takes):
#     . ./env.sh && make clean && make STRAIGHT_TO_RACE=1 TRACK=1 && \
#         GDBSCRIPT=trackmenu.gdb ./diag_run.sh 40
#     -> phase 3 (TM_FINISHED), option 1, track 1, and g_trackInstalled = 1.
#
# ⚠ THE WINDOW IS THE MEASUREMENT, and this screen is the worst offender in the whole port: the
# menu's title page is up for 273 FIELDS before a single option is drawn, so a probe that stops at
# vbi 200 reads phase 0 and a page full of title art — which looks exactly like a menu that never
# painted.  Hence the break condition below is the menu's OWN progress, not a field count.
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol.  If this prints only its
# header, suspect a probe global missing from PROBE_SYMS, not a dead menu.
set pagination off
set confirm off

printf "=== waiting for the menu to leave its title dwell (273 fields ~ 5.5 s)\n"
# Stop as soon as the menu is PAST the title page — or, in an unattended build, as soon as it has
# finished.  Both are `g_tmPhase != 0`.  ⚠ On Revs::render, which the interactive menu reaches
# every frame; an auto build never paints a menu frame, so it also breaks in the race view, and
# the phase read below is still the menu's final state.
tbreak Revs::render if g_tmPhase != 0 && g_vbiCount >= 60
continue

printf "=== vbi=%u  menu: phase=%u option=%u track=%u fields=%lu\n", \
  g_vbiCount, g_tmPhase, g_tmOption, g_tmTrack, g_tmFields
# ⭐ THE HALF NO PAGE CAN SHOW.  g_tmMisrouted != 0 means an option names one circuit and installs
# another — a page that looks perfect and plays the wrong track.  g_tmRefusals is a choice
# revs_track_install() would not honour, and g_trackOverinstalls a second install over a live one
# (track.h: one boot image, one circuit).  All three must be 0.
printf "=== routing: misrouted=%u refusals=%u overinstalls=%u\n", \
  g_tmMisrouted, g_tmRefusals, g_trackOverinstalls
printf "=== circuit: installed=%u requested=%u unhonoured=%u first=$%04x hooksUnbuilt=%u\n", \
  g_trackInstalled, g_trackRequested, g_trackUnhonoured, g_trackUnhonouredAddr, g_trackHooksUnbuilt
# ⭐ The 50 Hz body must NOT have run while the menu was up: engine_main() has not been called, so
# every one of these is state the game has not created yet (Revs.h §setFrontEnd).  In an
# interactive run that has not left the menu, ticks/drains must be 0 and dropped must be 0 —
# dropped counts the 200-tick cap, which is exactly what the suppression exists to prevent.
printf "=== body: ticks=%lu drains=%lu pending=%u dropped=%lu\n", \
  g_bodyTicks, g_bodyDrains, g_bodyPending, g_bodyTicksDropped
printf "=== display: mode7=%u planes=%u height=%u  vdu bytes=%lu unknown=%lu  flash=%u/%lu\n", \
  g_screenMode7, g_screenPlanes, g_screenHeight, g_ttVduBytes, g_ttUnknownVdu, \
  g_ttFlashPhase, g_ttFlashToggles

# The page itself, for the byte diff against the real BBC's REVSMEN.
dump binary memory .run/trackmenu_page.bin ((char*)&mem[0x7c00]) ((char*)&mem[0x8000])
printf "=== dumped .run/trackmenu_page.bin\n"
detach
quit
