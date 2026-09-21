# ⭐ WHICH display lines are actually OWNED, as runs.  `g_decodeOwnLines` is a COUNT, and a count
# cannot say whether the 104 owned lines are the blocks you think they are — which is exactly the
# question that decides where the decode's remaining cost is.
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
printf "owned=%u  flat=%u  bands=%u   cells/frame=%lu\n", \
  g_decodeOwnLines, g_decodeFlatLines, g_decodeFlatBands, g_decodeCellsTotal
set $y = 0
set $run = -1
while $y < 208
  if g_plotOwn[$y] && $run < 0
    set $run = $y
  end
  if !g_plotOwn[$y] && $run >= 0
    printf "  OWNED %3d..%3d  (%d)\n", $run, $y-1, $y-$run
    set $run = -1
  end
  set $y = $y + 1
end
if $run >= 0
  printf "  OWNED %3d..207  (%d)\n", $run, 208-$run
end
kill
quit
