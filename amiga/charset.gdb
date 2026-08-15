# Is the RACE VIEW'S TEXT actually being drawn on the target?
#
#   cd amiga && . ./env.sh && make clean && make STRAIGHT_TO_RACE=1 && \
#       GDBSCRIPT=charset.gdb ./diag_run.sh 90
#
# ⚠ STRAIGHT_TO_RACE=1 is not optional, for the same reason sound.gdb needs it: the bitmap text
# is the race view's (lap times and the like), so a run that never leaves the MODE 7 front end
# asks for ZERO character definitions and every counter below reads a legitimate zero
# (docs/method-lessons.md — a probe whose stimulus is absent gives a coherent wrong answer).
# The front end draws through the SAA5050 instead; that is teletext.*, not this.
#
# What each counter separates:
#   mosCharDefCount       the engine reached OSWORD 10 at all       (game -> OS)
#   mosCharDefOutOfRange  ...asking for a code we have no glyph for (OS -> font)
# A real BBC over a 200-frame Silverstone practice race asks 88 times for 15 distinct codes, all
# inside $20..$74 (`make refloop-charset`).  So: a nonzero count and a ZERO out-of-range is the
# expected shape.  Out-of-range going nonzero means the drawn range $20-$7F is not the closed
# surface after all, and g_mosCharDefLastBad says which code proved it.
#
# ⚠ THE COUNTER IS NOT THE PICTURE.  It proves the font was consulted, not that glyphs landed in
# the frame buffer — dump and decode for that:
#   GDBSCRIPT=screen_dump.gdb ./diag_run.sh 90
#   python3 ../tools/amiga_ppm.py --planes=2 --height=208 ...
set pagination off
set confirm off
continue
printf "=== vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "=== engine running: $61=%d   MODE flag $64=$%02x (bit 7 clear = bitmap arm)\n", \
  mem[0x61], mem[0x64]
printf "=== OSWORD 10: served=%lu  outOfRange=%lu  lastBadCode=$%02x\n", \
  g_mosCharDefCount, g_mosCharDefOutOfRange, g_mosCharDefLastBad
# The control block itself, as vdu_char_def left it: byte 0 is the last code asked for and
# bytes 1..8 are the glyph we handed back.  Printing it is how a "the font is wired up but the
# bytes are zero" failure is told apart from "nothing ever called OSWORD 10".
printf "=== last block $62C3: code=$%02x '%c'  rows=%02x %02x %02x %02x %02x %02x %02x %02x\n", \
  mem[0x62c3], mem[0x62c3], \
  mem[0x62c4], mem[0x62c5], mem[0x62c6], mem[0x62c7], \
  mem[0x62c8], mem[0x62c9], mem[0x62ca], mem[0x62cb]
printf "=== plot cursor: col $62CC=%d  row $62CD=%d\n", mem[0x62cc], mem[0x62cd]
printf "=== unknown MOS calls (must stay 0): %lu  last entry=$%04x A=$%02x\n", \
  g_mosUnknownCount, g_mosUnknownEntry, g_mosUnknownA
detach
quit
