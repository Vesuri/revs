# How long do the new render steps actually take, in real 50 Hz vblanks?
# `finish`-bracketed durations, so each number is one call, measured on the target.
set pagination off
set confirm off

tbreak Revs::render
continue
printf "=== first render at vbi=%u\n", g_vbiCount

# One whole main-loop iteration: render() to render().
set $v0 = (int)g_vbiCount
tbreak RevsScreen::prepareFrame
continue
printf "decode entered at vbi=%u (+%d since render)\n", g_vbiCount, (int)g_vbiCount-$v0
set $v1 = (int)g_vbiCount
finish
printf "DECODE took %d vblanks (%d ms)\n", (int)g_vbiCount-$v1, ((int)g_vbiCount-$v1)*20

tbreak RevsScreen::vbiUpdate
continue
set $v2 = (int)g_vbiCount
finish
printf "VBIUPDATE took %d vblanks\n", (int)g_vbiCount-$v2

tbreak Revs::render
continue
printf "=== next render at vbi=%u -> frame = %d vblanks\n", g_vbiCount, (int)g_vbiCount-$v0
printf "=== bands=%u rejects=%lu\n", g_bandCount, g_bandRejects
detach
quit
