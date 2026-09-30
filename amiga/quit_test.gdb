# CTRL-Q quits — the release's only way out (docs/controls.md).  Needs `make QUITTEST=<field>`:
# the VERTB ISR then holds CTRL and Q in the rawkey state from that field on (gdb cannot write
# this machine's memory, so the chord is in the binary).  PASS = the platform destructor runs,
# i.e. the quit poll fired and run() unwound to the restore (RevsInput::shutdown runs only there).
# ⚠ Break by ADDRESS on an out-of-line function: `break ~PlatformAmiga` resolved to three
# locations, one inside an inlined renderFrame(), and "passed" a build with no chord at all.
#   make clean && make QUITTEST=600 && GDBSCRIPT=quit_test.gdb ./diag_run.sh 120
set pagination off
set confirm off
break *&_ZN9RevsInput8shutdownEv
continue
# ⚠ diag_run.sh's timeout INTERRUPTS `continue` and gdb runs the next line anyway, so where it
# stopped is the verdict, not the fact that it stopped (a chord-less control "passed" that way).
if $pc == (unsigned long)&_ZN9RevsInput8shutdownEv
  printf "QUIT PASS: the game unwound to its restore at field %u\n", g_vbiCount
else
  printf "QUIT FAIL: no quit — stopped at pc %p, field %u\n", $pc, g_vbiCount
end
kill
quit
