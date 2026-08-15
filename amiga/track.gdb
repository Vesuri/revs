# ⭐ WHICH CIRCUIT DID THE TARGET ACTUALLY INSTALL, and was any circuit refused?
#
#   . ./env.sh && make TRACK=n && GDBSCRIPT=track.gdb ./diag_run.sh 30
#
# ⚠ On the target the refusal has no stderr to go to, so these counters ARE the report.  A build
# asked for an expansion circuit falls back to Silverstone deliberately (src/platform/track.h) —
# g_trackUnhonoured is what tells that apart from "Silverstone is what I asked for".
set pagination off
set confirm off
tbreak Revs::render
continue
printf "=== vbi=%u\n", g_vbiCount
printf "=== requested=%u  installed=%u   (differ => the build FELL BACK)\n", \
  g_trackRequested, g_trackInstalled
# ⚠ This describes the REQUESTED circuit, re-taken after the fallback — see revs_track_boot().
# A zero count beside a non-zero address would mean that re-take is broken again.
printf "=== requested circuit unhonoured bytes=%u  first=$%04x\n", \
  g_trackUnhonoured, g_trackUnhonouredAddr
# The circuit NAME as the engine holds it, at $7808 — proof the block really went in, independent
# of the counters above.  13 chars covers the longest ("Donington Park").
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
