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
# ⭐⭐ PHASES 25-28 ARE THE PAINT CALL, SPLIT (2026-08-16).  Phase 25 measured 60.9% of the frame —
# the biggest row by a factor of four — and "the paint plus the frame wait" is not actionable, so it
# is now four rows: 25 the call itself, 26 the 50 Hz body DRAIN (real engine work, and its share
# grows as the framerate falls), 27 the frame-buffer DECODE (pure port overhead), 28 the SPIN on the
# next vblank.  The old phase-25 row is the sum of the four.  See src/platform/probe.h.
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
#
# Phases 1-24 are the main loop's 24 top-level JSRs, in source order.  Phase 25 is
# PROBE_PHASE_FRAMEWAIT — platform_render_frame(), i.e. the paint plus the spin on the next
# real vblank.  It IS part of the loop's wall time so it counts toward the total (the
# accounted-for check would otherwise stop reaching ~100%), but it is not engine work: read
# it as the port's own overhead, not as a function to optimise.  Before 2026-08-13 it had no
# bracket and landed on phase 24.
set $i = 1
set $tot = 0
while $i < 29
  set $tot = $tot + g_phaseTicks[$i]
  set $i = $i + 1
end
set $per = $tot / 1000
set $eper = g_beamEpoch / 1000
printf "loop ticks %lu of %lu elapsed  (accounted %d.%01d%% + phase 0 — MUST total ~100)\n", \
  $tot, g_beamEpoch, ($tot/$eper)/10, ($tot/$eper)%10
# ⚠ calls MATTERS here.  Phase 0 is re-opened at L_1760, the ENGINE's own frame wait, which
# $1753 branches past whenever $62F6 is zero.  calls=0 ⇒ that wait is never entered and phase 0
# really is just boot; calls>0 ⇒ phase 0 is boot PLUS a per-frame engine wait and must not be
# read as a one-off.
printf "phase 0 (boot + engine wait at $1760, excluded): ticks=%lu calls=%lu\n", \
  g_phaseTicks[0], g_phaseCount[0]
# ⭐⭐ PHASE 26 IS THE 50 Hz BODY, AND ITS SIZE IS A RATIO, NOT A ROUTINE.  It runs once per
# DISPLAY FIELD, so at ~1 painted FPS it runs ~50 times per painted frame — which is faithful (a
# BBC's User VIA fires regardless of how long the foreground takes) and is why it can dominate a
# per-frame table without any one call being slow.  The number that matters is therefore the cost
# of ONE tick against the 20 ms a tick has: print it, and check the port is not running more ticks
# than there were fields.
printf "body: ticks=%lu drains=%lu dropped=%lu pending=%u   fields=%u  (ticks/field must be ~1)\n", \
  g_bodyTicks, g_bodyDrains, g_bodyTicksDropped, g_bodyPending, g_vbiCount
if g_bodyTicks > 0
  printf "   ONE BODY TICK = %lu us of its 20000 us budget  (phase 26 / body ticks)\n", \
    (g_phaseTicks[26]/g_bodyTicks)*1000/4006
end
set $i = 1
while $i < 29
  printf "phase %2d  ticks=%10lu  calls=%7lu  share=%2d.%01d%%  %4lu ms/frame\n", \
     $i, g_phaseTicks[$i], g_phaseCount[$i], \
     (g_phaseTicks[$i]/$per)/10, (g_phaseTicks[$i]/$per)%10, \
     (g_phaseTicks[$i]/g_phaseFrames)/4006
  set $i = $i + 1
end
printf "phase 25  ticks=%10lu  calls=%7lu  share=%2d.%01d%%  %4lu ms/frame  <- FRAME WAIT (not engine work)\n", \
   g_phaseTicks[25], g_phaseCount[25], \
   (g_phaseTicks[25]/$per)/10, (g_phaseTicks[25]/$per)%10, \
   (g_phaseTicks[25]/g_phaseFrames)/4006
detach
quit
