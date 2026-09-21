set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "line disp  runA      runB\n"
set $l = 3
while $l < 44
  printf " %2d  %3d  %2d..%2d   %2d..%2d  clip=%d\n", $l, 160 - $l, \
    s_lowA0[$l], s_lowA1[$l], s_lowB0[$l], s_lowB1[$l], s_lowClipped[$l]
  set $l = $l + 1
end
kill
quit
