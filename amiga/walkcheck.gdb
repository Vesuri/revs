# ⭐⭐ THE SPAN WALK ASM GATE (needs `make WALKCHECK=1`, with SPANASM=1, the default).
#
# On every span the asm takes, src/gen/revs_native.c's span_walk_check runs the C loop and the
# asm on the same bytes and compares everything they write (the visited pages, the destination,
# and the y / pointers / block / abandon handed back).  A fake address above $FFFF in `first=`
# names a state field: 10000 abandon, 10001 y, 10002 block, 10003-10005 p1..p3.
# ⚠ `carryIns` is the carry derivation's own test (the C reference counts DDA adds with a non-zero
# carry-in) and must read 0.  `fallback` is spans the asm did not take (they ran the C loop).
# ⚠ A correctness arm: it snapshots pages per span, so its phase rows are void.
set pagination off
set confirm off
# stop after this many checked GAME spans (the self-test runs first and takes ~20000 fields)
set $spansEnd = 2000

tbreak Revs::render if g_walkCheckSpans >= $spansEnd
continue

printf "=== WALKCHECK spans=%lu mismatch=%lu fallback=%lu carryIns=%lu  (vbi=%u track=%u)\n", \
  g_walkCheckSpans, g_walkCheckMismatch, g_walkCheckFallback, g_walkCheckCarryIns, g_vbiCount, g_track
printf "all span_walk calls: shallow fwd=%lu rev=%lu steep fwd=%lu rev=%lu; guard refused (exact walk)=%lu\n", g_walkCheckArms[0], g_walkCheckArms[1], g_walkCheckArms[2], g_walkCheckArms[3], g_walkCheckExact
printf "fallback why: dest=%lu step=%lu blockwrap=%lu  unchecked(window)=%lu\n", g_walkCheckWhy[0], g_walkCheckWhy[1], g_walkCheckWhy[2], g_walkCheckWhy[3]
printf "first mismatch: addr=%lx C=%lx asm=%lx\n", g_walkCheckFirstAddr, g_walkCheckFirstC, g_walkCheckFirstAsm
printf "variants sfo+ sfo- sfi+ sfi- sro+ sro- sri+ sri- tf+ tf- tr+ tr-: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", \
  g_walkCheckVariants[0], g_walkCheckVariants[1], g_walkCheckVariants[2], g_walkCheckVariants[3], \
  g_walkCheckVariants[4], g_walkCheckVariants[5], g_walkCheckVariants[6], g_walkCheckVariants[7], \
  g_walkCheckVariants[8], g_walkCheckVariants[9], g_walkCheckVariants[10], g_walkCheckVariants[11]
printf "SELFTEST cases=%lu mismatch=%lu abandons=%lu  first: addr=%lx C=%lx asm=%lx\n", g_walkSelfCases, g_walkSelfMismatch, g_walkSelfAbandons, g_walkSelfFirstAddr, g_walkSelfFirstC, g_walkSelfFirstAsm
printf "self variants: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", \
  g_walkSelfVariants[0], g_walkSelfVariants[1], g_walkSelfVariants[2], g_walkSelfVariants[3], \
  g_walkSelfVariants[4], g_walkSelfVariants[5], g_walkSelfVariants[6], g_walkSelfVariants[7], \
  g_walkSelfVariants[8], g_walkSelfVariants[9], g_walkSelfVariants[10], g_walkSelfVariants[11]
printf "verdict: %s\n", (g_walkCheckMismatch || g_walkSelfMismatch || !g_walkSelfCases || g_walkCheckCarryIns || !g_walkCheckSpans) ? "FAIL" : "PASS"
kill
quit
