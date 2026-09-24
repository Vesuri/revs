# ⭐⭐ THE SPAN PASS ASM GATE (needs `make SETUPCHECK=1`, with SPANASM=1 SETUPASM=1, the defaults).
#
# On every pass draw_surface_spans_core hands to the asm, src/gen/revs_native.c's span_pass_check runs
# the C pass (with the C walk) and the asm pass on the same state and compares ALL 64 KB of mem[],
# the three plot-pointer words and the cpu struct.  Before the first game pass a 2000-pass fuzzer runs
# through the same comparison.  A fake address above $FFFF in `first=` names a state field:
# 10001-10003 plot_ptr_v / plot_ptr2_v / plot_ptr3_v, 10100+n byte n of the cpu struct.
# `variants` counts the C reference's walks by span_asm_variant's numbering: a 0 is a shape the check
# never compared.  ⚠ `smcUnhandled` counts the fuzzer's planted bad entry offsets (C and asm each
# report one), so it is non-zero by design in this build.
# ⚠ A correctness arm: it copies 64 KB three times a pass, so its phase rows are void.
set pagination off
set confirm off
# stop after this many checked GAME passes
set $passesEnd = 400

tbreak Revs::render if g_setupCheckPasses >= $passesEnd
continue

printf "=== SETUPCHECK passes=%lu mismatch=%lu  (vbi=%u track=%u)\n", g_setupCheckPasses, g_setupCheckMismatch, g_vbiCount, g_track
printf "first mismatch: pass=%lu addr=%lx C=%lx asm=%lx\n", g_setupCheckFirstPass, g_setupCheckFirstAddr, g_setupCheckFirstC, g_setupCheckFirstAsm
printf "game variants sfo+ sfo- sfi+ sfi- sro+ sro- sri+ sri- tf+ tf- tr+ tr-: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", \
  g_setupCheckVariants[12], g_setupCheckVariants[13], g_setupCheckVariants[14], g_setupCheckVariants[15], \
  g_setupCheckVariants[16], g_setupCheckVariants[17], g_setupCheckVariants[18], g_setupCheckVariants[19], \
  g_setupCheckVariants[20], g_setupCheckVariants[21], g_setupCheckVariants[22], g_setupCheckVariants[23]
printf "SELFTEST cases=%lu spans=%lu mismatch=%lu  first: case=%lu addr=%lx C=%lx asm=%lx  smcUnhandled=%lu\n", \
  g_setupSelfCases, g_setupSelfSpans, g_setupSelfMismatch, g_setupSelfFirstCase, g_setupSelfFirstAddr, g_setupSelfFirstC, g_setupSelfFirstAsm, g_smcUnhandled
printf "self variants: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", \
  g_setupCheckVariants[0], g_setupCheckVariants[1], g_setupCheckVariants[2], g_setupCheckVariants[3], \
  g_setupCheckVariants[4], g_setupCheckVariants[5], g_setupCheckVariants[6], g_setupCheckVariants[7], \
  g_setupCheckVariants[8], g_setupCheckVariants[9], g_setupCheckVariants[10], g_setupCheckVariants[11]
printf "verdict: %s\n", (g_setupCheckMismatch || g_setupSelfMismatch || !g_setupSelfCases || !g_setupCheckPasses) ? "FAIL" : "PASS"
kill
quit
