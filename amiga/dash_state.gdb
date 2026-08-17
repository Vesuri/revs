# ⭐⭐ THE DASHBOARD'S OWN STATE ON THE TARGET, sampled across a run — the port's side of the
# real-BBC readout `make refloop FRAMES=8` now prints ("[engine off, in the pits]" etc).
#
#   . ./env.sh && make clean && make STRAIGHT_TO_RACE=1 && \
#       GDBSCRIPT=dash_state.gdb ./diag_run.sh 90
#
# WHAT IT IS FOR.  Three reports off the target — a rev counter pinned to maximum with the engine
# OFF, a throttle that behaves as if held, and a steering indicator that never moves — are all
# claims about state the GAME keeps, and the game keeps it in zero page.  So read the same bytes
# the reference loop reads, at the same points in the session, and the question "port bug or
# faithful?" becomes a diff instead of an argument.
#
# MEASURED ON A REAL BBC (make refloop FRAMES=8, 2026-08-16):
#   engine off, in the pits    $05F5=00  $61=00  $3C=00  $63=00  $40=01
#   engine on, first gear      $05F5=00  $61=ff  $3C=2c  $63=00  $40=02
#   throttle held              $05F5=00  $61=ff  $3C=2c  $63=04  $40=02   (then $3C/$63 climb)
#   PARKED, nothing held       the engine STALLS: $61 -> 00 and $3C -> 00 within a second
#
# ⭐ $3E/$3F (the throttle/brake state $1681 writes: $3E = 1 throttle / 0 brake / $80 coasting,
# $3F = the amount) are in the row because "the engine never stalls" and "something is holding the
# throttle" produce the same $61/$3C/$63 and the pair separates them in one sample.  And the T2
# counters, because a CONSTANT $FE68 is what used to make the idle sit at $28 instead of $2C —
# a live counter and a dead one are indistinguishable from the dashboard alone.
#
# ⚠ $74/$75/$76 are NOT stable state and are deliberately not compared: the real-BBC run reads
# $76 as $8d/$08/$80 — values outside the 0..3 the steering path stores — because they are working
# bytes sampled at an arbitrary point in the frame.  Reading a transient at an arbitrary moment is
# how an instrument invents a finding.  What IS comparable is $05F5, $61, $3C, $63 and $40.
#
# ⚠ A gdb script ABORTS THE WHOLE FILE at the first unknown symbol.  Header-only output means a
# missing PROBE_SYMS entry, not a dead dashboard.
set pagination off
set confirm off

define dashrow
  printf "=== vbi=%-5u %s  $05F5=%02x  $61=%02x(engine)  $3C=%02x(revs)  $63=%02x(speed)  $40=%02x(gear)  $3E=%02x/$3F=%02x(throttle)  $2D=%02x $58=%02x  $05F4=%02x  T2 reads=%lu last=%02x\n", \
    g_vbiCount, $arg0, mem[0x5f5], mem[0x61], mem[0x3c], mem[0x63], mem[0x40], mem[0x3e], mem[0x3f], mem[0x2d], mem[0x58], mem[0x5f4], g_viaT2Reads, g_viaT2Last
end

# ⭐ The menu owns the first ~275 fields (trackmenu.h), so every sample below is AFTER it — a
# sample inside it would read a game that has not started, which is the window trap this project
# keeps paying for.  The first stop is the earliest frame the race view is up.
tbreak Revs::render if g_vbiCount >= 300
continue
dashrow "first race frame  "

tbreak Revs::render if g_vbiCount >= 600
continue
dashrow "later             "

tbreak Revs::render if g_vbiCount >= 1200
continue
dashrow "later still       "

tbreak Revs::render if g_vbiCount >= 2400
continue
dashrow "settled           "

# ⭐⭐ AND THE STATE WHEN THE MEASUREMENT WINDOW CLOSES, which is a different question from any
# sample above: a STRAIGHT_TO_RACE car eventually leaves the track and is reset to the pits, and a
# profile whose tail is a parked car is diluted with a workload nobody asked about
# (docs/perf-method.md §the 30-second rule).  `continue` runs until diag_run.sh SIGINTs us at the
# delay, so this row is the last thing the run was doing — read it beside every share table.
continue
dashrow "AT THE INTERRUPT  "

# ⭐ Did the key map answer anything at all?  A rev counter reading is only interesting once it is
# known which side of the starter the sample is on, and g_keyEvents is the cheapest witness that
# the input path is alive.  ⚠ AutoRun's own counters are NOT globals, so they are not readable
# here — asking for one aborts the whole script at that line and prints nothing after it.
printf "=== keys: events=%lu unmapped=%lu (last $%02x)\n", \
  g_keyEvents, g_keyUnmapped, g_keyUnmappedCode
detach
quit
