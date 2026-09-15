# ⭐⭐⭐ `make SPANFILL=1` — THE VALID §10e CHEAP CHECKPOINT (docs/span-render-plan.md §10L).
#
# What SPANEMIT's scaffold was MEANT to be.  Same predicate, same span; three deletions:
#   1. NO MIRROR — PLOT_UNIT and the phase-2/3 boundary cells compile to nothing, so an owned
#      line comes from its span alone and every other line from mem[] through the decode.  The
#      scaffold mirrored every chain store, which is the ⛔ mirror-each-store plotter (§10c).
#   2. OWNERSHIP IS PER DISPLAY LINE (`g_plotOwn` -> `m_lineMode[y] = 0`), not a whole-character-
#      row `g_plotLineLo/Hi` carve-out, so mem[] stays true everywhere the emitter did not paint.
#   3. The plot target is set in RevsScreen::present(), after the buffer flip.
#
# THREE numbers, and all three have to agree or the build is measuring nothing:
#   g_spanEmitLines  lines the emitter painted        (the census predicts ~26% of line paints)
#   g_decodeOwnLines display lines the LAST decode left to it   (must be ~= spans per sweep)
#   g_plotRunsLast   runs on the last sweep           (⭐ must now be ~20, not ~346: with the
#                    mirror gone the ONLY runs are the spans themselves)
# ⚠ g_decodeOwnLines == 0 with g_spanEmitLines large means the carve-out is not live — the decode
# is repainting mem[] over every span and the spans are pure extra work.
#
# ⚠ DRIVING, not parked: a static scene's sources are clean everywhere, so a parked run reports a
# flattering emit rate for a scene nobody drives through ($63 below must be non-zero).
#
# Run:
#   . ./env.sh && make clean && make SPANFILL=1 STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 \
#       PROBES=1 FIXED_RNG=1
#   EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=span_fill.gdb ./diag_run.sh 30
#
# CORRECTNESS takes TWO oracles, because the shipping arm has two halves and no single build
# can see both:
#   1. THE SPAN BYTES — `make SPANFILL=1 SPANVERIFY=1 DIRECTCHECK=1` + GDBSCRIPT=span_emit.gdb.
#      Verify mode keeps the chain running and the mirror live so mem[] is a valid reference, and
#      brackets the sweep with two conversions: g_plotMismatch must stay 0.  It proves the span
#      paints what the forty units would have painted.
#   2. THE CARVE-OUT — `make SPANFILL=1 DIRTYCHECK=1` and the oracle line below.  This is the
#      SHIPPING arm itself, mirror gone and ownership live, and the in-process re-decode is exact
#      for it BY CONSTRUCTION: an owned line is mode 0, so the reference conversion writes nothing
#      there and the scratch keeps the copy it started as, while every line the emitter did NOT
#      claim is fully re-expanded and byte-compared.  So a nonzero mismatch is the carve-out
#      skipping a line it does not own.
# ⚠⚠ And do NOT try to settle either half by dumping the planes of two DIFFERENT BUILDS at the
# same game frame.  The 50 Hz body runs on wall clock while the main loop runs at its own speed,
# so at equal game frames a faster build has taken MORE body ticks and the simulation has already
# diverged (measured: frame 40 was vbi=556 in the control and vbi=574 here).  Both oracles above
# are in-process and compare the frame against ITSELF, which is why they are the instruments.
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "=== vbi=%u  bodyTicks=%lu\n", g_vbiCount, g_bodyTicks
printf "=== SPAN EMIT: lines emitted as one span %lu   lines that ran the chain %lu\n", \
  g_spanEmitLines, g_spanEmitPaints
printf "=== CARVE-OUT: display lines the last decode left to the emitter %u  (0 = NOT LIVE)\n", \
  g_decodeOwnLines
printf "=== plot: runs=%lu cells=%lu   last sweep: %u runs / %u cells   lines %u..%u\n", \
  g_plotRuns, g_plotCells, g_plotRunsLast, g_plotCellsLast, g_plotLineLo, g_plotLineHi
printf "=== dropped for want of a target: %lu   (large = the target is not being aimed)\n", \
  g_plotNoTarget
printf "=== CARVE-OUT ORACLE checks=%lu mismatch=%lu firstOff=%u   (needs DIRTYCHECK=1: with\n", \
  g_decodeDirtyChecks, g_decodeDirtyMismatch, g_decodeDirtyMismatchOff
printf "===   checks=0 the zero mismatch means NOTHING — the window is the measurement)\n"
printf "=== engine $61=%02x $3C=%02x $63=%02x  (speed must be non-zero)\n", \
  mem[0x61], mem[0x3c], mem[0x63]
detach
quit
