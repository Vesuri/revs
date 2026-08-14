# ⭐⭐ DOES THE 50 Hz BODY WRITE THE FILL CHAIN'S INPUTS?  Needs `make ISRWATCH=1`.
#
# The reasoning is at the instrument (src/platform/amiga/Revs.cpp).  Short version: the remaining
# horizon artefact is a green or black run that lasts a whole PAINTED frame, the game's renderer
# paints runs in one pass and never clears, and two things decide a run — the $3000 column sources
# (a zero means "carry the previous cell's colour") and the 42 patched opcode slots in the
# $7B00-$7FFF overlay that end each span.  On a real BBC the T1 interrupt is raster-scheduled; here
# the whole band cycle fires in a burst and can preempt the main loop inside the chain.
#
# Run:  . ./env.sh && make clean && make -j4 STRAIGHT_TO_RACE=1 ISRWATCH=1 \
#       && GDBSCRIPT=isr_watch.gdb ./diag_run.sh 250
set pagination off
set confirm off

break Revs::render if g_vbiCount >= 2000
continue

printf "fields=%u samples=%lu   body wrote: code=%lu sources=%lu overlay=%lu\n", \
  g_vbiCount, g_isrSamples, g_isrWroteCode, g_isrWroteSrc, g_isrWroteOvl
printf "256-byte blocks of $1200-$7AFF the body ever wrote (the frame buffer holds the\n  engine's live variables from $5E40, incl. the road edge lists):\n"
set $b = 0
while $b < 105
  if g_isrCodeBlocks[$b] != 0
    printf "  $%04X\n", 0x1200 + $b*256
  end
  set $b = $b + 1
end
printf "column-source blocks ($3000-$43FF) the body ever wrote:\n"
set $b = 0
while $b < 20
  if g_isrSrcBlocks[$b] != 0
    printf "  $%04X\n", 0x3000 + $b*256
  end
  set $b = $b + 1
end
printf "ZERO PAGE the body ever wrote (%lu fields of %u wrote at least one byte):\n", \
  g_isrZpFields, g_vbiCount
set $z = 0
while $z < 256
  if g_isrZpWrites[$z] != 0
    printf " %02X", $z
  end
  set $z = $z + 1
end
printf "\n"
printf "overlay blocks ($7B00-$7FFF) the body ever wrote:\n"
set $b = 0
while $b < 5
  if g_isrOvlBlocks[$b] != 0
    printf "  $%04X\n", 0x7B00 + $b*256
  end
  set $b = $b + 1
end
detach
quit
