# ⛔⛔ RETIRED AS AN INSTRUMENT (2026-09-15).  THIS COMPARISON IS UNSOUND AND CANNOT BE MADE SOUND
# BY PINNING THE GAME FRAME.  Read the ⚠⚠ block below and then do not use this script.
#
# The reasoning that built it is right about one thing — `g_vbiCount` is wall clock, so it is the
# wrong trigger — and wrong about the fix.  `g_phaseFrames` pins the MAIN LOOP's iteration count,
# but the game's 50 Hz body is drained on WALL CLOCK (CLAUDE.md: the body runs at the engine's own
# frame hook, 50 fires a second whatever the framerate does).  So at the same game frame a FASTER
# build has taken FEWER body ticks per frame and the simulation has already diverged: measured,
# frame 40 was vbi=556 in the control and vbi=574 under SPANFILL=1.  Different scene, different
# pixels, and the diff says nothing about the carve-out.
# ⭐ This is what produced the unexplained "lines 133..140, plane 2, outer cells" diff that stood
# open for a session.  It was trajectory divergence, not a plotter defect.
#
# ⭐⭐ THE REPLACEMENTS ARE IN-PROCESS AND COMPARE A FRAME AGAINST ITSELF, which is the only shape
# that cannot be defeated this way — see span_fill.gdb:
#   1. THE SPAN BYTES  `make SPANFILL=1 SPANVERIFY=1 DIRECTCHECK=1` + span_emit.gdb
#   2. THE CARVE-OUT   `make SPANFILL=1 DIRTYCHECK=1`             + span_fill.gdb
# Kept, not deleted, because the reasoning below is the trap written out and the next cross-build
# pixel comparison will be proposed the same way.
#
# ── the original text ─────────────────────────────────────────────────────────────────────────
# ⭐⭐⭐ THE SHIPPING ARM'S OWN ORACLE: `make SPANFILL=1` vs a clean control, PIXEL FOR PIXEL.
#
# `SPANFILL=1 SPANVERIFY=1 DIRECTCHECK=1` proves the span BYTES are right, and it cannot prove
# the shipping arm at all — verify mode keeps the chain running and the mirror live, which is
# exactly the two things the shipping arm deletes.  What is left unproven is the CARVE-OUT: that
# `decode()` really leaves the owned lines alone, and that nothing else needed those mem[] bytes.
#
# So dump the DISPLAYED buffer from two builds and byte-compare it:
#   . ./env.sh
#   make clean && make SPANFILL=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBES=1 FIXED_RNG=1
#   EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=span_fill_dump.gdb ./diag_run.sh 60
#   cp .run/planes.bin .run/planes-spanfill.bin
#   make clean && make STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 PROBES=1 FIXED_RNG=1
#   EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=span_fill_dump.gdb ./diag_run.sh 60
#   cmp .run/planes.bin .run/planes-spanfill.bin        # must be IDENTICAL
#
# ⚠⚠ THE TRIGGER IS THE GAME FRAME, NOT THE FIELD, AND THAT IS THE WHOLE POINT.  screen_dump.gdb
# breaks on `g_vbiCount >= N`, which is WALL CLOCK: a faster build has driven further by then, so
# the two dumps show different scenes and the comparison is worthless.  `g_phaseFrames` counts
# completed main-loop iterations, so both builds are stopped at the SAME game frame on the same
# FIXED_RNG trajectory — and then the pixels must agree exactly.
# ⚠ Under HOLD_THROTTLE the trajectory diverges once the car leaves the track and resets, so keep
# the frame number well inside the run (~40 frames is ~2 s of driving at this baseline).
set pagination off
set confirm off

tbreak Revs::render if g_phaseFrames >= 40
continue

printf "=== frame=%lu vbi=%u front=%08x bytes=%u\n", \
  g_phaseFrames, g_vbiCount, g_screenFrontAddr, g_screenBytes
printf "=== engine $61=%02x $3C=%02x $63=%02x  (the trajectory: both builds must agree)\n", \
  mem[0x61], mem[0x3c], mem[0x63]
dump binary memory .run/planes.bin ((char*)g_screenFrontAddr) ((char*)g_screenFrontAddr + g_screenBytes)
dump binary memory .run/copper.bin ((char*)g_screenCopperAddr) ((char*)g_screenCopperAddr + 4*g_screenCopperWords)
printf "=== dumped\n"
detach
quit
