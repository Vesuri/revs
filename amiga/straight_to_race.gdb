# ⭐ DOES THE STRAIGHT_TO_RACE BUILD ACTUALLY REACH A MOVING RACE ON THE TARGET?
#
# A build that boots "into the race" can be wrong in four ways that all look the same from
# outside — still in the front end, in the race with the engine off, in the race idling in
# neutral, or in gear with nothing on the throttle.  The first three have all happened here.
# So sample the game's own state twice, far enough apart that a stationary car is unmistakable:
#
#   mem[$5F3B]  $FF = the practice branch of front_end_menus ($6401).  Not $FF ⇒ the menu
#               answer never landed and this is not a practice session at all.
#   mem[$0061]  starter/engine flag.  $FF once $4978 has seen 'T'; 0 = engine off.
#   mem[$003C]  engine revs.  $28 is the idle floor ($49B9); higher = under load.
#   mem[$0063]  road speed.  ⚠ 0 in neutral NO MATTER what the throttle does, which is the
#               failure the second sample exists to catch.
#
# Run it in BOTH configurations, because they prove different halves:
#
#   make clean && make STRAIGHT_TO_RACE=1            speed stays 0 — CORRECT.  The script hands
#     the keyboard back (autoRun.done()) and a headless run has no hand on the throttle.  What
#     this run proves is practice + engine + a rendered race, and `script: step` past the end.
#   make clean && make STRAIGHT_TO_RACE=1 FPSCOUNT=1  the script holds the throttle forever, so
#     speed MUST rise between t1 and t2.  This is the end-to-end input proof: script →
#     pressBbcKey → g_keyDown → RevsInput::keyDown → OSBYTE 129 → the game accelerating.
#
# ⚠ There is no third option where a gdb script pokes a key: `set var` DOES NOT REACH THIS
# EMULATED MACHINE (measured — docs/headless-fsuae.md).  Put the stimulus in the binary.
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol.  If this prints only its
# header, suspect a probe global missing from PROBE_SYMS, not a dead scene.
#
# ⚠⚠ `painted` IS PRINTED FOR LIVENESS AND IS NOT A FRAMERATE.  The conditional breakpoints
# below halt the emulated machine to evaluate their condition, which is precisely the defect
# that made fps_seg.gdb read 0.02 where the truth was 0.78 (CLAUDE.md §Performance).  Quote a
# framerate from fps_series.gdb or from nothing.
set pagination off
set confirm off

printf "=== straight_to_race: waiting for the scene\n"

tbreak Revs::render if g_vbiCount >= 300
continue
printf "=== t1 vbi=%u painted=%lu  practice$5F3B=%02x engine$61=%02x revs$3C=%02x speed$63=%02x\n", \
  g_vbiCount, g_fpsFrames, mem[0x5F3B], mem[0x61], mem[0x3C], mem[0x63]
printf "===    script step=%u polls=%lu\n", \
  s_platform->autoRun.m_step, s_platform->autoRun.m_polls

tbreak Revs::render if g_vbiCount >= 900
continue
printf "=== t2 vbi=%u painted=%lu  practice$5F3B=%02x engine$61=%02x revs$3C=%02x speed$63=%02x\n", \
  g_vbiCount, g_fpsFrames, mem[0x5F3B], mem[0x61], mem[0x3C], mem[0x63]
printf "===    script step=%u polls=%lu   (STRAIGHT_TO_RACE alone: step past the end and polls\n", \
  s_platform->autoRun.m_step, s_platform->autoRun.m_polls
printf "===    FROZEN is the handover — the player owns the keyboard from there)\n"
printf "=== unmapped keys=%lu (last $%02x)  brk=%lu  unknown MOS=%lu\n", \
  g_keyUnmapped, g_keyUnmappedCode, g_brkCount, g_mosUnknownCount

# The picture, decoded on the host by tools/amiga_ppm.py — see screen_dump.gdb.
dump binary memory .run/planes.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)
dump binary memory .run/copper.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*53)
printf "=== dumped\n"
detach
quit
