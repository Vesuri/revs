# ⭐ Main-loop phase shares.  SHARES WITHIN ONE RUN — never diff across builds
# (docs/perf-method.md Rule 2).  PROBES build: no framerate may be quoted from it.
#
# Build: cd amiga && make clean && make -j4 PROBES=1 FIXED_RNG=1
# Run:   . ./env.sh && GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 200
#
# ⚠ TWO defects this script exists to avoid, both of which produced confident wrong tables:
#
#  1. NO CONDITIONAL BREAKPOINT.  This used to open with
#         tbreak Revs::render if g_vbiCount >= 900
#     which makes gdb round-trip the remote stub on EVERY hit to evaluate the condition.  On
#     a PROBES build that crippled the target: two runs read a bit-identical loopFrames=2
#     where a plain `continue` over the same binary read 114.  diag_run.sh already SIGINTs us
#     at the delay, so just run and read the counters — the sample is then the whole run.
#
#  2. SHARES ARE COMPUTED IN PER-MILLE.  `100 * ticks` overflows 32-bit gdb arithmetic once
#     the tick totals are real (~8e8 for a 200 s run) and prints shares summing to ~14%.
#     Dividing by (tot/1000) keeps every intermediate small.
#
# ⭐ SANITY CHECK, read it every time: `bracketed` must be ~100% of `elapsed`.  If it is not,
# the brackets are losing time and the shares are fiction — that was the state of this
# harness until 2026-08-13, when it accounted for 4%.  docs/perf-method.md.
set pagination off
set confirm off
continue
printf "=== vbi=%u loopFrames=%lu brk=%lu smc=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_brkCount, g_smcUnhandled
# Phase 0 is a ONE-OFF: it accumulates from program start to the first bracket (boot + front
# end), so it is reported but excluded from the shares, which are shares of the LOOP.
set $i = 1
set $tot = 0
while $i < 25
  set $tot = $tot + g_phaseTicks[$i]
  set $i = $i + 1
end
set $per = $tot / 1000
set $eper = g_beamEpoch / 1000
printf "loop ticks %lu of %lu elapsed  (accounted %d.%01d%% + phase 0 — MUST total ~100)\n", \
  $tot, g_beamEpoch, ($tot/$eper)/10, ($tot/$eper)%10
printf "phase 0 (boot, one-off, excluded): %lu\n", g_phaseTicks[0]
set $i = 1
while $i < 25
  printf "phase %2d  ticks=%10lu  calls=%7lu  share=%2d.%01d%%  %4lu ms/frame\n", \
     $i, g_phaseTicks[$i], g_phaseCount[$i], \
     (g_phaseTicks[$i]/$per)/10, (g_phaseTicks[$i]/$per)%10, \
     (g_phaseTicks[$i]/g_phaseFrames)/4006
  set $i = $i + 1
end
detach
quit
