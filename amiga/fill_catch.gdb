# ⭐⭐ CATCH THE HORIZON FILL ARTEFACT IN THE GAME'S OWN FRAME BUFFER, and print the evidence.
#
# Sampling frames and looking at them is a lottery — the artefact is rarer than a dozen frames, and
# 24 dumped frames were all clean while the screen was visibly broken.  So the check lives in the
# program (RevsScreen::prepareFrame) and latches the horizon neighbourhood the first time it trips:
#
#   above the ground line, every cell of band 2 must be SKY ($0F).  A run of $00 (black) or $FF
#   (green) there is the artefact, in both of its reported colours.
#
# What the answer MEANS:
#   bad frames > 0  ->  the bytes are wrong in the game's buffer.  The 6502 side wrote them, so the
#                       cause is in the emulation/interrupt model, not the Amiga display path.
#   bad frames = 0  ->  the buffer is clean and the artefact is downstream (decode, copper, DMA).
#
# ⚠ Needs `make FILLWATCH=1` — the checks live inside decode() and re-read the whole frame buffer,
# so they are not in a default build (and never quote a framerate from one that has them).
# ⚠ And `HOLD_THROTTLE=1`: with the keyboard handed over the car is PARKED and the horizon is
# byte-identical frame after frame, so the fill under test never runs and this reports a clean.
#
# Run:  . ./env.sh && make clean && make -j4 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 FILLWATCH=1 \
#       && GDBSCRIPT=fill_catch.gdb ./diag_run.sh 400
# Then: grep -E 'EDGE JUMPS|decode mismatch|tear frames|BAD=' amiga/.run/gdb-out.log
# ⚠ diag_run.sh's own summary is `tail -40`, which cuts these headers — read the log, not stdout.
set pagination off
set confirm off

break Revs::render if g_edgeJumpFrames > 3 || g_planLogN >= 240
continue

printf "painted frames=%lu   BAD=%lu   groundLine=%u\n", \
  g_planLogN, g_fillBadFrames, g_fillGroundLine
printf "unwind: pending=%lu touched=%lu  stack imbalance=%lu  irqClobber=%lu  smc=%lu\n", \
  g_irqUnwindPending, g_irqUnwindTouched, g_irqStackImbalance, g_irqClobberCount, g_smcUnhandled
printf "tear frames=%lu (rows moved under the decode)  bandRejects=%lu\n", \
  g_tearFrames, g_bandRejects
# ⭐ The two exact checks: how much of the horizon moved between painted frames (a stationary car
# should give ~0, so a one-frame outlier is the artefact), and whether the decoded bitplanes still
# match mem[] afterwards (the only check that separates "the game wrote it" from "we drew it wrong").
printf "horizon change: max=%lu cells, frames over 40=%lu\n", \
  g_horizonChangeMax, g_horizonChangeBig
printf "decode mismatch=%lu firstLine=%u\n", g_decodeMismatch, g_decodeMismatchLine
# ⭐ THE ARTEFACT ITSELF: the road's left edge jumping right for a line or two while the right edge
# marches on — measured off the user's screenshot (left 80/136/186/34, right 199/201/213/217).
printf "body: ticks=%lu drains=%lu pending=%u dropped=%lu\n", \
  g_bodyTicks, g_bodyDrains, g_bodyPending, g_bodyTicksDropped
printf "EDGE JUMPS=%lu  first at line %u: left cell %u -> %u -> %u\n", \
  g_edgeJumpFrames, g_edgeJumpLine, g_edgeJumpPrev, g_edgeJumpHere, g_edgeJumpNext
printf "per-frame horizon change (last 64 painted frames):\n"
set $i = 0
while $i < 64
  printf " %u", g_horizonChangeSeries[$i]
  set $i = $i + 1
end
printf "\n"

if g_fillBadFrames > 0 || g_edgeJumpFrames > 0
  printf "first bad frame %lu: line %u, %u cells of %02X from cell %u\n", \
    g_fillBadFrameN, g_fillBadLine, g_fillBadRun, g_fillBadValue, g_fillBadCell
  printf "the horizon as the game left it ('.'=00 b=0F w=F0 '#'=FF +=mixed):\n"
  # 74..115 inclusive, 40 cells each — dumped raw so tools/fill_check.py can render it too.
  dump binary memory .run/fill_evidence.bin &g_fillEvidence[0] &g_fillEvidence[93*40]
  set $y = 0
  while $y < 93
    printf "%3d ", 74 + $y
    set $c = 0
    while $c < 40
      set $v = g_fillEvidence[$y*40 + $c]
      if $v == 0
        printf "."
      else
        if $v == 0x0F
          printf "b"
        else
          if $v == 0xF0
            printf "w"
          else
            if $v == 0xFF
              printf "#"
            else
              printf "+"
            end
          end
        end
      end
      set $c = $c + 1
    end
    printf "\n"
    set $y = $y + 1
  end
end
detach
quit
