# ⭐ WHAT THE FRONT END SAYS — MODE 7 screen RAM as TEXT, 25 rows of 40.
#
# Run:  . ./env.sh && make PROBES=1 && GDBSCRIPT=mode7_text.gdb ./diag_run.sh 45
#
# The companion to mode7_dump.gdb: that one dumps the BITPLANES (what was decoded), this one
# dumps $7C00 (what the game WROTE).  Reading both is how a presentation bug is told apart from
# a paint bug — the wing page ($3C50) was byte-perfect in screen RAM for a whole wait while the
# display still showed the previous menu, because wait_dismiss ($34D2) polled without rendering.
#
# ⚠ mode7=0 in the header means the run left the front end before the breakpoint and the rows
# below are RACE-view memory, not a page.  That is a legitimate result (the front end completed),
# not a decode failure.
#
# ⚠ Raise the vbi threshold to reach a LATER page; the breakpoint takes the first Revs::render
# past it, so the page you get is whichever one was up then.
set pagination off
set confirm off
tbreak Revs::render if g_screenMode7 != 0 && g_vbiCount >= 900
continue
printf "=== vbi=%u  mode7=%u\n", g_vbiCount, g_screenMode7
set $r = 0
while $r < 25
  set $c = 0
  printf "%02d |", $r
  while $c < 40
    set $b = mem[0x7C00 + $r*40 + $c]
    if $b >= 32 && $b < 127
      printf "%c", $b
    else
      printf "."
    end
    set $c = $c + 1
  end
  printf "|\n"
  set $r = $r + 1
end
