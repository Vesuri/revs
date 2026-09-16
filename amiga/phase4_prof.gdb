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
# ⭐⭐ PHASE 32 IS THE MAIN LOOP'S TAIL, split out of phase 24 (2026-08-17).  Phase 24 used to stay
# open from `JSR $7BE2` right through to the paint hook, so the body's tail after the sweep — the
# $178F path and its three JSRs — was charged to the view rasteriser.  With 32 in the table, phase
# 24 IS view_paint_lines.  See src/platform/probe.h §32.
#
# ⭐ SANITY CHECK, read it every time: `bracketed` must be ~100% of `elapsed`.  If it is not,
# the brackets are losing time and the shares are fiction — that was the state of this
# harness until 2026-08-13, when it accounted for 4%.  docs/perf-method.md.
set pagination off
set confirm off
continue
printf "=== vbi=%u loopFrames=%lu brk=%lu smc=%lu frozen=%lu ===\n", \
  g_vbiCount, g_phaseFrames, g_brkCount, g_smcUnhandled, g_probeFrozen
# ⭐⭐ `frozen=1` means this table describes exactly `make PROBEFIELDS=N`'s emulated window and may
# be compared arm to arm; `frozen=0` means the window was the WALL CLOCK, which under warp differs
# between arms by however much the host was loaded.  Never diff two arms unless both read 1.
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
while $i < 40
  set $tot = $tot + g_phaseTicks[$i]
  set $i = $i + 1
end
set $per = $tot / 1000
# ⚠⚠ THE PARTIAL-FREEZE TRAP.  Under `make PROBEFIELDS=N` the phase accumulators stop at the
# window's edge but the run does not, so g_beamEpoch and g_bodyTicks keep climbing and every row
# that mixes one of them with a phase tick is fiction — `ONE BODY TICK` read 379 us against a
# true 1313 this way, which is plausible enough to survive a review.  probe.cpp snapshots both
# at the freeze; take the snapshot whenever the window closed, and the live value otherwise.
set $wall = g_beamEpoch
set $body = g_bodyTicks
if g_probeFrozen > 0
  set $wall = g_probeFrozen
  set $body = g_probeFrozenBody
end
set $eper = $wall / 1000
# ...and the field count the window covered, for the same reason: g_vbiCount keeps climbing, so
# `ticks/field must be ~1` reads 0.27 against the live one where the truth is 0.99.
set $fields = g_vbiCount
if g_probeFrozen > 0
  set $fields = $wall / 80120
end
printf "loop ticks %lu of %lu elapsed  (accounted %d.%01d%% + phase 0 — MUST total ~100)\n", \
  $tot, $wall, ($tot/$eper)/10, ($tot/$eper)%10
# ⚠ calls MATTERS here.  Phase 0 is re-opened at L_1760, the ENGINE's own frame wait, which
# $1753 branches past whenever $62F6 is zero.  calls=0 ⇒ that wait is never entered and phase 0
# really is just boot; calls>0 ⇒ phase 0 is boot PLUS a per-frame engine wait and must not be
# read as a one-off.
printf "phase 0 (boot + engine wait at $1760, excluded): ticks=%lu calls=%lu = %lu fields\n", \
  g_phaseTicks[0], g_phaseCount[0], g_phaseTicks[0]/80120
# ⭐⭐ THE FRAME, AND THE ONLY WALL FIGURE THAT IS COMPARABLE ACROSS BUILDS.  Raw
# elapsed/loopFrames includes phase 0, and phase 0 is boot PLUS the engine's 2-second crash hold
# ($1753 spins at $1760 on field_countdown — exactly 100 fields a crash).  A FASTER build runs
# more game frames inside the same window, so it reaches MORE crashes and its raw wall frame reads
# as if the win partly evaporated: -2.29 ms for -5.01 ms of real compute, all of the gap one extra
# hold (docs/perf-method.md §the dash-edge walk).  ⚠ Compare the FIELD counts above between two
# runs: a ~100-field difference means they are not the same workload.
# This line must equal `loop ticks / loopFrames` above to ~0.1 ms — that identity is what proves
# the brackets account for the whole frame.
printf "FRAME = %lu ms  (wall %lu ms minus phase 0; compare THIS across builds, never wall)\n", \
  (($wall - g_phaseTicks[0])/g_phaseFrames)/4006, ($wall/g_phaseFrames)/4006
# ⭐⭐ PHASE 26 IS THE 50 Hz BODY, AND ITS SIZE IS A RATIO, NOT A ROUTINE.  It runs once per
# DISPLAY FIELD, so at ~1 painted FPS it runs ~50 times per painted frame — which is faithful (a
# BBC's User VIA fires regardless of how long the foreground takes) and is why it can dominate a
# per-frame table without any one call being slow.  The number that matters is therefore the cost
# of ONE tick against the 20 ms a tick has: print it, and check the port is not running more ticks
# than there were fields.
printf "body: ticks=%lu drains=%lu dropped=%lu pending=%u   fields=%lu  (ticks/field must be ~1)\n", \
  $body, g_bodyDrains, g_bodyTicksDropped, g_bodyPending, $fields
if $body > 0
  # ⚠ 26 + 29: phase 29 is the body's own arm ($52A4), split out of 26.  Summing them is the
  # WHOLE body tick — reading 26 alone after the split would silently halve the headline number.
  printf "   ONE BODY TICK = %lu us of its 20000 us budget  (phases 26+29 / body ticks)\n", \
    ((g_phaseTicks[26]+g_phaseTicks[29])/$body)*1000/4006
  printf "   of which the arm ($52A4, phase 29) = %lu us, the rest of the band cycle = %lu us\n", \
    (g_phaseTicks[29]/$body)*1000/4006, (g_phaseTicks[26]/$body)*1000/4006
end
if g_probeIsrCount > 0
  printf "VERTB ISR: %lu calls, %lu us each  (copper + present + audio; charged to whatever phase it preempted)\n", \
    g_probeIsrCount, (g_probeIsrTicks/g_probeIsrCount)*1000/4006
end
if g_probeIrqCount > 0
  printf "irq1v_band_schedule: %lu calls (%lu per body tick), %lu us each\n", \
    g_probeIrqCount, g_probeIrqCount/$body, (g_probeIrqTicks/g_probeIrqCount)*1000/4006
end
set $i = 1
while $i < 40
  printf "phase %2d  ticks=%10lu  calls=%7lu  share=%2d.%01d%%  %4lu ms/frame\n", \
     $i, g_phaseTicks[$i], g_phaseCount[$i], \
     (g_phaseTicks[$i]/$per)/10, (g_phaseTicks[$i]/$per)%10, \
     (g_phaseTicks[$i]/g_phaseFrames)/4006
  set $i = $i + 1
end
# ⭐⭐ THE VIEW SWEEP'S THREE PAINTING PHASES, ms BESIDE THE WORK EACH DID (probe.h §PROBE_VIEW_*).
# us/unit is only meaningful against the phase-1 row, which has no driver at all: the difference
# between a phase's us/unit and phase 1's IS the per-line driver, and us/line prices it directly.
set $i = 0
while $i < 3
  set $ph = 24
  if $i == 1
    set $ph = 33
  end
  if $i == 2
    set $ph = 34
  end
  # the census counters are bumped per unit visit, so the freeze cannot stop them — read the
  # snapshot, or units/frame reads high by the ratio of the whole run to the window (5526 vs 1442).
  set $u = g_viewUnits[$i]
  set $r = g_viewRuns[$i]
  set $l = g_viewLines[$i]
  if g_probeFrozen > 0
    set $u = g_probeFrozenUnits[$i]
    set $r = g_probeFrozenRuns[$i]
    set $l = g_probeFrozenLines[$i]
  end
  printf "view phase %d (bracket %d, %4lu ms/frame): units=%4lu/frame runs=%3lu/frame lines=%3lu/frame", \
     $i + 1, $ph, (g_phaseTicks[$ph]/g_phaseFrames)/4006, \
     $u/g_phaseFrames, $r/g_phaseFrames, $l/g_phaseFrames
  if $u > 0
    printf "  %4lu us/unit", (g_phaseTicks[$ph]/$u)*1000/4006
  end
  if $l > 0
    printf "  %5lu us/line", (g_phaseTicks[$ph]/$l)*1000/4006
  end
  printf "\n"
  set $i = $i + 1
end
# ⭐⭐ THE CALIBRATION (make VIEWCAL=1): 14 000 known cycles a call, i.e. 1975 us at 7.09 MHz.  What
# this row reads instead IS the conversion factor for every other row in the view sweep.
if g_phaseCount[39] > 0
  printf "CALIBRATION: %lu calls of 14000 known cycles read %lu us each (1975 us = honest)\n", \
    g_phaseCount[39], (g_phaseTicks[39]/g_phaseCount[39])*1000/4006
end
printf "phase 25  ticks=%10lu  calls=%7lu  share=%2d.%01d%%  %4lu ms/frame  <- FRAME WAIT (not engine work)\n", \
   g_phaseTicks[25], g_phaseCount[25], \
   (g_phaseTicks[25]/$per)/10, (g_phaseTicks[25]/$per)%10, \
   (g_phaseTicks[25]/g_phaseFrames)/4006
# ⭐ The dash-edge walk's A/B switch printing its own state (CLAUDE.md §instruments): every walk
# that could not prove its stores private, or found an unmodelled $1DD5 operand, takes the cold
# re-reading copy.  This MUST read 0 — a non-zero is a real hazard in the GAME, not the fixture.
printf "gap-walk slow copy: %lu walks (must be 0)\n", g_gapWalkSlow
detach
quit
