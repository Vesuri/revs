# Does the port reach a COMPETITION race, and are the OTHER CARS being rendered?
#
#   cd amiga && . ./env.sh && make clean && make COMPETITION=1 && \
#       GDBSCRIPT=competition.gdb ./diag_run.sh 120
#
# ⚠ COMPETITION=1 is the whole point and it is not interchangeable with STRAIGHT_TO_RACE=1.
# PRACTICE runs the player ALONE on the circuit, so a clean practice frame says nothing whatever
# about competitor-car rendering — the stimulus is absent and every counter below would read a
# legitimate zero (docs/method-lessons.md).  Every frame this port had measured before this build
# existed was a practice frame.
#
# The cheapest check that the script took the branch it thinks it did:
#   $5F3B  practice flag — $FF on the practice branch, $04 on competition (measured on a real
#          BBC by `make refloop-comp`).  If this reads $FF the run is a practice session and
#          everything below is about the wrong session.
#
# Then the field itself.  The engine keeps 20 cars; car_lap_count ($04B4) and the car_order list
# are per-car arrays, and find_player_neighbours ($63A2) is what picks the cars near the player —
# which are the ones with any chance of being on screen.
#   $6F    the player's car index
#   $0040  the player's gear (proves the script got as far as driving)
set pagination off
set confirm off
continue
printf "=== vbi=%u painted=%lu\n", g_vbiCount, g_fpsFrames
printf "=== BRANCH: $5F3B=$%02x  (expect $04 COMPETITION, $ff = practice = WRONG SESSION)\n", \
  mem[0x5f3b]
printf "=== driving: engine $61=%d  gear $40=%d  player index $6F=%d\n", \
  mem[0x61], mem[0x40], mem[0x6f]
# The field, as the engine itself holds it.  A competition session should show cars at a spread
# of track positions; a field where every car shares one position is a field that was never
# initialised, which looks identical on screen to "the renderer is broken".
printf "=== lap counts  (car_lap_count $04B4, 20 cars):\n=== "
set $i = 0
while $i < 20
  printf "%d ", mem[0x04b4 + $i]
  set $i = $i + 1
end
printf "\n=== distance lo (car_distance_lo $08D0, 20 cars):\n=== "
set $i = 0
while $i < 20
  printf "%3d ", mem[0x08d0 + $i]
  set $i = $i + 1
end
printf "\n"
# ⭐⭐ THE 6502 STACK, which in Revs is a DATA-INTEGRITY measure and not bookkeeping.  Page 1's
# bottom holds eight 20-entry per-car arrays, so S must stay above $9F or pushes scribble the
# field (src/cpu/cpu.c has the whole mechanism).  Measured on a real BBC: entry S = $F8, and
# S = $F2 after 200 frames of a 20-car race — six bytes used, no drift.
printf "=== 6502 stack: S=$%02x  entry-S saved at $6B=$%02x  (real BBC: $F8 entry, $F2 mid-race)\n", \
  cpu.S, mem[0x6b]
printf "===   lowest S ever=$%02x  HIGHEST S ever=$%02x  pushes into the per-car arrays=%lu\n", \
  g_stackLow, g_stackHigh, g_stackTrespass
# ⚠ THE CEILING IS THE ONE THAT BIT.  S enters at $F8, so g_stackHigh above $F8 means an
# unbalanced PULL (or a TXS that raises S) — it walks to $FF, WRAPS to $00, and pushes then land
# on car_order at mem[$0100].  That arrives looking exactly like a downward leak, and chasing it
# as one wastes the run.  Both watermarks, always: $F3..$F8 is the healthy window here.
printf "=== OSWORD 10: served=%lu outOfRange=%lu   unknown MOS calls=%lu\n", \
  g_mosCharDefCount, g_mosCharDefOutOfRange, g_mosUnknownCount
printf "=== smc unhandled=%lu site=$%04x value=$%02x   brk=%lu\n", \
  g_smcUnhandled, g_smcSite, g_smcValue, g_brkCount
detach
quit
