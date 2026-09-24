# ⭐ SINGLE-STEP TRACE of whole calls to one function — the ground truth when a phase bracket
# and the source disagree by an order of magnitude.  Skips the first $SKIP hits (boot, front end),
# then traces $CALLS consecutive calls instruction by instruction to their return address and
# prints every PC ("T <pc>"), with "C <n> <count>" closing each call.  Map it with
# tools/steptrace_report.py (same relocation as the PC sampler: the OFF line).
# ⚠ An instruction count is not a cycle count, and an interrupt taken mid-step shows up as
#   ISR PCs inside the call.  Edit the three TARGET lines (OFF's name, the break) and SKIP/CALLS
#   below; the report reads the target's name off the OFF line.
# ⚠ Give diag_run.sh a window long enough for the trace; the script ends in `quit`, so the wrapper
#   returns as soon as the trace is done.  Pass the trace's ELF to the report with --elf= if out/
#   has been rebuilt since.
set pagination off
set confirm off
printf "OFF %x interp_edge_core\n", &interp_edge_core
break interp_edge_core
ignore 1 4000
continue
set $call = 0
while $call < 24
  set $ret = *(unsigned int *)$sp
  set $n = 0
  while $pc != $ret && $n < 300000
    stepi
    printf "T %x\n", $pc
    set $n = $n + 1
  end
  printf "C %d %d\n", $call, $n
  set $call = $call + 1
  continue
end
detach
quit
