# Does the input layer answer every key the game asks about?
#
# A plain build has no scripted input, so it parks in the front end POLLING keys — which is
# exactly the state that exercises the map.  g_keyUnmapped counts negative-INKEY codes the
# table does not carry: a non-zero count is a key that will silently never work, which is
# indistinguishable from a mis-wired one from the game's side.
#
# Run: . ./env.sh && make clean && make && GDBSCRIPT=input.gdb ./diag_run.sh 90
set pagination off
set confirm off
continue
printf "=== vbi=%u  keyEvents=%lu  unmapped=%lu (last code $%02x)\n", \
  g_vbiCount, g_keyEvents, g_keyUnmapped, g_keyUnmappedCode
printf "=== mode flag 05F5=%02x  05F4=%02x   steer=%02x\n", \
  mem[0x05F5], mem[0x05F4], s_platform->input.m_steer
printf "=== bands=%u rejects=%lu brk=%lu mosUnknown=%lu\n", \
  g_bandCount, g_bandRejects, g_brkCount, g_mosUnknownCount
detach
quit
