#!/bin/bash
# ⭐⭐ Run the PC sampler (amiga/pcsample.gdb): diag_run.sh with a long window, plus a loop that
# interrupts THIS project's gdb (matched by the unique script name, never `pkill gdb`) every
# $PERIOD seconds.  Usage: . ./env.sh && ./pcsample.sh [seconds] [period]
SECS=${1:-90}; PERIOD=${2:-0.02}
GDBSCRIPT=pcsample.gdb ./diag_run.sh "$SECS" >/dev/null 2>&1 &
DR=$!
sleep 12
end=$((SECONDS + SECS - 14))
while [ $SECONDS -lt $end ]; do
  pid=$(pgrep -f "pcsample.gdb out/Revs.elf" | head -1)
  [ -n "$pid" ] && kill -INT "$pid" 2>/dev/null
  sleep "$PERIOD"
done
wait $DR
grep -c "^S " .run/gdb-out.log
