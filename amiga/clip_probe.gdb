set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 600
continue
printf "leftEnd:"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x3150 + $i]
  set $i = $i + 1
end
printf "\nrightStart:"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x30D0 + $i]
  set $i = $i + 1
end
printf "\nrightEnd:"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x3080 + $i]
  set $i = $i + 1
end
printf "\nedgePhase:"
set $i = 3
while $i <= 43
  printf " %02x", mem[0x3050 + $i]
  set $i = $i + 1
end
printf "\n"
