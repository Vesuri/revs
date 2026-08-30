# ⚠ DID THE BUILD ACTUALLY GET THE CIRCUIT IT ASKED FOR?
#
# track.h: "An out-of-range or REFUSED choice falls back to 0 and leaves g_trackUnhonoured set."
# A fallback build races SILVERSTONE while every flag and filename says otherwise — so any
# per-circuit measurement has to print these five numbers before its own result is worth reading.
#
#   g_trackRequested  what -DREVS_TRACK_DEFAULT asked for
#   g_trackInstalled  what actually went in (0xFF = none yet; != requested = FALLBACK)
#   g_trackHookCalls  the circuit's OWN code running (0 for passive Silverstone, non-zero for an
#                     expansion circuit — this is the CODE path, which no byte diff can see)
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "requested=%u installed=%u hookCalls=%lu unhonoured=%u hooksUnbuilt=%u overinstalls=%u\n", \
  g_trackRequested, g_trackInstalled, g_trackHookCalls, \
  g_trackUnhonoured, g_trackHooksUnbuilt, g_trackOverinstalls
printf "flatLines=%u flatBands=%u bandRejects=%lu beamPresentsLate=%lu\n", \
  g_decodeFlatLines, g_decodeFlatBands, g_bandRejects, g_beamPresentsLate
detach
quit
