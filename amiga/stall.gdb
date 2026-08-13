set pagination off
set confirm off
tbreak Revs::render
continue
printf "=== first render vbi=%u\n", g_vbiCount
# Let it run a long while, then look at where it is and what the game thinks.
tbreak Revs::render if g_vbiCount >= 3000
continue
printf "=== reached vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "=== 4F43=%02x 62F6=%02x 62F7=%02x 05F4=%02x brk=%lu rejects=%lu\n", \
  mem[0x4F43], mem[0x62F6], mem[0x62F7], mem[0x05F4], g_brkCount, g_bandRejects
bt 8
detach
quit
