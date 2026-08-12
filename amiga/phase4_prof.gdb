set pagination off
set confirm off
# Phase shares of the engine's main loop.  SHARES WITHIN ONE RUN — never diff these across
# builds (docs/perf-method.md Rule 2).  This is a PROBES build: slower than shipping, so no
# framerate may be quoted from it.
tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== vbi=%u loopFrames=%lu brk=%lu ===\n", g_vbiCount, g_phaseFrames, g_brkCount
set $i = 0
set $tot = 0
while $i < 25
  set $tot = $tot + g_phaseTicks[$i]
  set $i = $i + 1
end
printf "total bracketed beam ticks: %lu\n", $tot
set $i = 0
while $i < 25
  printf "phase %2d  ticks=%9lu  calls=%7lu  share=%2d.%01d%%\n", \
     $i, g_phaseTicks[$i], g_phaseCount[$i], \
     (100*g_phaseTicks[$i])/$tot, ((1000*g_phaseTicks[$i])/$tot)%10
  set $i = $i + 1
end
detach
quit
