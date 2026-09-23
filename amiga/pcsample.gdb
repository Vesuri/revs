# ⭐⭐ THE STATISTICAL PC SAMPLER — "where does the time ACTUALLY go", at instruction granularity.
# Phase brackets say which CALL costs what; deletion arms price a part by removing it (and carry
# the trajectory difference between two pictures); neither can point at the instruction.  This
# stops the target at random emulated moments (./pcsample.sh sends this gdb a SIGINT every few
# ms, and only this gdb — never `pkill gdb`), prints the PC and the field count, and continues.
# tools/pcsample_report.py turns the log into a per-function and per-source-line histogram.
# ⚠ A sample is one stopped instruction, so a row's share is a SHARE OF WALL TIME including the
#   VERTB ISR and the drain; quote ms only as share x the phase-table frame.
set pagination off
set confirm off
printf "OFF %x\n", &interp_edge_core
set $n = 0
while $n < 100000
  continue
  printf "S %u %x\n", g_vbiCount, $pc
  set $n = $n + 1
end
