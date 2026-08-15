# ⭐ DO THE MOUSE BUTTONS READ AS RELEASED WHEN NOTHING IS PRESSED?
#
# The buttons are the pedals now (right = throttle, left = brake) plus the faithful fire button
# on the middle one.  Left is a CIA-A bit and has always worked; right and middle come from
# POTINP, and THAT read has a silent, inverted failure mode: the pot pins are only inputs after
# POTGO is cleared (RevsInput::initialize), and until they are, both buttons read as
# permanently HELD.  A stuck throttle AND a stuck brake is not a dead control — it is a car
# that behaves oddly for a reason nothing in a byte dump can show.
#
# A headless run cannot press a button, so this proves the half that breaks silently: with no
# hand on the mouse, the mask must be 0.
#
#   g_mouseBtnMask   the buttons right now: 1 = left, 2 = right, 4 = middle
#   g_mouseBtnSeen   sticky OR over the whole run — the honest one, because a single sample
#                    could land in the gap between two spurious reads
#
# EXPECTED on an untouched machine: both 0.  Anything else and POTGO is wrong.
#   6 (right|middle) is the specific signature of the pot pins still being outputs.
#
# ⚠⚠ WHAT THIS DOES **NOT** COVER, measured by sabotage rather than assumed.  Two sabotages were
# run against this script:
#   - removing the POTGO clear entirely  ->  still mask=0.  FS-UAE DOES NOT MODEL the pot-pin
#     direction, so the very failure this probe was written for is INVISIBLE here.  The clear
#     stays in RevsInput::initialize() because real hardware needs it, but nothing on this host
#     can prove it is there — only a real Amiga can.
#   - inverting the right/middle polarity ->  mask=6, seen=6.  So the plumbing, the sampling
#     point and this script all work; it is specifically the POTGO dependency that is unmodelled.
# Read a pass here as "the buttons are wired and read released", never as "POTGO is verified".
#
# ⚠ g_vbiCount is printed because a 0 mask proves nothing if sampleMouse() never ran — an
# unstarted VBI reads exactly like a correctly-released button.  Require it to be climbing.
#
#   . ./env.sh && make clean && make PROBES=1 STRAIGHT_TO_RACE=1
#   GDBSCRIPT=mousebtn.gdb ./diag_run.sh 40

# ⚠ `shell sleep` DOES NOT WORK HERE and its failure is the silent kind: gdb holds the emulated
# machine stopped, so both samples read vbi=0 and mask=0 — a perfect pass that measured nothing.
# The machine only advances while continued, so gate each sample on a breakpoint condition, the
# way straight_to_race.gdb does.
set pagination off
set confirm off

echo == mouse buttons: idle state ==\n

tbreak Revs::render if g_vbiCount >= 300
continue
printf "t1  vbi=%u  mask=%u  seen=%u\n", g_vbiCount, g_mouseBtnMask, g_mouseBtnSeen

tbreak Revs::render if g_vbiCount >= 900
continue
printf "t2  vbi=%u  mask=%u  seen=%u\n", g_vbiCount, g_mouseBtnMask, g_mouseBtnSeen
echo (vbi must climb between t1 and t2, or sampleMouse never ran and the zeros are noise)\n

detach
quit
