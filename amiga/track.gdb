# ⭐ WHICH CIRCUIT DID THE TARGET ACTUALLY INSTALL, and was any circuit refused?
#
#   . ./env.sh && make TRACK=n && GDBSCRIPT=track.gdb ./diag_run.sh 30
#
# ⚠ On the target the refusal has no stderr to go to, so these counters ARE the report.  A build
# asked for an expansion circuit falls back to Silverstone deliberately (src/platform/track.h) —
# g_trackUnhonoured is what tells that apart from "Silverstone is what I asked for".
set pagination off
set confirm off
# ⚠⚠ SAMPLE LATE, NOT AT THE FIRST RENDER.  The install counters are settled before the first
# frame, but g_trackHookCalls is not: the hooks live in the RACE, and this target paints at ~1 FPS,
# so a `tbreak Revs::render` stop lands at vbi≈38 — under a second in, with the autorun script
# still pressing keys.  It reported `hook calls=0` beside a perfect install, which reads as a dead
# seam and is really a window that never reached the race.  Same false negative as on the host
# (docs/phases.md §5c).  So skip the first 30 painted frames.
#   . ./env.sh && make PROBES=1 STRAIGHT_TO_RACE=1 TRACK=n && GDBSCRIPT=track.gdb ./diag_run.sh 150
break Revs::render
ignore 1 30
continue
printf "=== vbi=%u\n", g_vbiCount
printf "=== requested=%u  installed=%u   (differ => the build FELL BACK)\n", \
  g_trackRequested, g_trackInstalled
# ⚠ This describes the REQUESTED circuit, re-taken after the fallback — see revs_track_boot().
# A zero count beside a non-zero address would mean that re-take is broken again.
printf "=== requested circuit unhonoured bytes=%u  first=$%04x\n", \
  g_trackUnhonoured, g_trackUnhonouredAddr
printf "=== requested circuit hook bodies unbuilt=%u  first=$%04x\n", \
  g_trackHooksUnbuilt, g_trackHooksUnbuiltAddr
# ⭐⭐ THE ONE THAT SAYS THE CIRCUIT'S OWN CODE RAN.  Everything above can be perfect while the
# hooks are never reached — which is exactly what an expansion circuit silently running
# Silverstone's control flow looks like.  Expected: 0 for Silverstone (it is passive), > 0 for
# every other circuit, and missing == 0 always.
# ⚠ THE WINDOW IS THE MEASUREMENT: the hooks live in the race, not the front end, so read this on
# a STRAIGHT_TO_RACE build.  On a plain build a zero here means "never got to the race", not
# "the seam is dead" — that false negative cost a detour on the host (docs/phases.md §5c).
printf "=== hook calls=%lu  missing=%lu (first $%04x)\n", \
  g_trackHookCalls, g_trackHookMissing, g_trackHookMissingAddr
# The circuit NAME as the engine holds it, at $7808 — proof the block really went in, independent
# of the counters above.  13 chars covers the longest ("Donington Park").
# ⚠⚠ ONLY MEANINGFUL BEFORE THE RACE.  `copy_dash_data` ($18EA) drops the dashboard bitmap over
# $70DB-$7813, which CONTAINS $7808 — so in a race this prints garbage, faithfully.  Read it at the
# first render (or off a plain build); once the sampling point moved past 30 painted frames to catch
# the hook calls, this line stopped being evidence and started being noise.  mem[$5300]/mem[$5301]
# below are the in-race block check that still holds.
printf "=== name at $7808: "
set $i = 0
while $i < 15
  printf "%c", mem[0x7808 + $i]
  set $i = $i + 1
end
printf "\n"
# Two block bytes that differ per circuit, as a second independent check.
printf "=== mem[$5300]=$%02x mem[$5301]=$%02x   mem[$1248]=$%02x (patched circuits: $20)\n", \
  mem[0x5300], mem[0x5301], mem[0x1248]
detach
quit
