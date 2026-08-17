#pragma once
/* Phase brackets for the engine's main loop — the Phase 4 "which functions are hot" answer.
 *
 * WHY BRACKETS AND NOT PC SAMPLING.  The engine's per-frame body ($1701-$1763) is a FLAT
 * sequence of about twenty-five JSRs.  That shape is unusually kind to bracketing: each
 * call is a phase, the phases are disjoint, and the boundaries are exact rather than
 * statistical — no sampling bias, no symbol-attribution guesswork through -O2 inlining
 * (which on the host collapsed the entire loop into one frame of `FUN_16dc`).
 *
 * ⚠ WHAT THESE NUMBERS ARE, AND ARE NOT.  docs/perf-method.md is explicit and it was
 * learned the hard way:
 *   - Use them for SHARES — where the time goes WITHIN ONE RUN.  That is what "name the
 *     hot functions" needs and it is what they are good for.
 *   - NEVER diff a per-iteration figure across builds.  FIXED_RNG pins the level, not the
 *     path driven, so each build times a different view; a no-code-change experiment moved
 *     one phase by the entire size of a filed "regression".
 *   - A bracket INCLUDES nested callees, so it is a subtree cost, not an own cost.
 *   - This is a PROBES build: it reads two chip registers per phase, so it is slower than
 *     the shipping build.  Never quote a framerate from it.
 *
 * Compiled to nothing at all unless REVS_PROBE is defined.
 */
/* Included from BOTH the generated C (revs_gen.c) and C++ backends, so it must not
   reach for <cstdint>; it needs no integer types of its own anyway. */

#ifdef REVS_PROBE

#ifdef __cplusplus
extern "C" {
#endif

/* Close the phase that was open and open phase `id`.  id 0 is reserved for "outside the
   main loop" (the frame wait and whatever the interrupt does), so the shares add up to
   the whole frame instead of only to the instrumented part. */
void probe_phase(int id);

/* Cost-per-call timers for the two things a phase cannot separate — see probe.cpp. */
void probe_isr_begin(void);
void probe_isr_end(void);
void probe_irq_begin(void);
void probe_irq_end(int state);
void probe_irq_null(void);   /* the empty-bracket control — see probe.cpp */
#ifdef REVS_PROBE_HWTIME
void probe_hw_begin(void);
void probe_hw_end(void);
extern volatile unsigned long g_probeHwTicks;
extern volatile unsigned long g_probeHwWrites, g_probeHwReads;
#endif
extern volatile unsigned long g_probeIsrTicks, g_probeIsrCount;
extern volatile unsigned long g_probeIrqTicks, g_probeIrqCount;

/* ⭐⭐ THE BRACKET INSIDE THE HANDLER (2026-08-17).  irq1v_band_schedule is one row in the phase
 * table and five completely different jobs: four band arms that write a palette and reload a
 * timer, and band 4, which also runs $52A4.  A per-CALL average over the five therefore prices
 * a palette write and the band-4 arm as if they were the same thing, and that average ("~810 us
 * a call for sixteen stores") is what made the cost look unexplained.  Split by the band the
 * call SERVICES — mem[$4F43] read at entry, before the handler steps it.
 * ⚠ Bands 1->2->3 fall THROUGH inside one interrupt when a band has no height, so a call is
 * attributed to the band it entered on, and the count per band need not be equal.  Index 5 is
 * the $FF wrap arm and 6 the "not our interrupt" exit; 7 catches anything else. */
#define PROBE_BANDS 8
extern volatile unsigned long g_probeBandTicks[PROBE_BANDS], g_probeBandCount[PROBE_BANDS];

#define PROBE_ISR_BEGIN() probe_isr_begin()
#define PROBE_ISR_END()   probe_isr_end()
#define PROBE_IRQ_BEGIN() probe_irq_begin()
#define PROBE_IRQ_END(s)  probe_irq_end(s)
#define PROBE_IRQ_NULL()  probe_irq_null()

/* Number of phases the table below can hold — one per top-level call in $1701-$1763,
   plus id 0, plus slack. */
#define PROBE_PHASES 40

/* ⭐ The DISPLAY-frame wait, bracketed on its own.
 *
 * platform_render_frame() is injected at the top of the main loop ($1701, PRE_INSN_HOOKS)
 * and on the Amiga it paints and then spins until the VERTB ISR bumps g_vbiCount.  That
 * spin is not engine work, and until 2026-08-13 it was charged to whichever phase happened
 * to be open across the loop seam — phase 24 ($7BE2, the dashboard), because the engine's
 * own frame wait at $1760 is conditional and usually skipped.  At 1.4 FPS one game frame
 * spans ~34 display frames so the wait is only ~1 in 34, but it sat on top of the single
 * largest row in the table, which is exactly the row someone is about to optimise.
 *
 * It gets an id of its own rather than being folded into phase 0, so that "the boot cost"
 * and "the wait" stay separable.  Ids 1..N are the main loop's JSRs (N = 24 today), so
 * this must stay above the highest of them: tools/transpile.py mirrors the value and
 * `make gen` FAILS if the JSR count ever reaches it. */
#define PROBE_PHASE_FRAMEWAIT 25

/* ⭐⭐ AND ITS THREE PARTS, because at 60.9% of the frame (measured 2026-08-16) phase 25 is by far
 * the biggest row in the table and "the paint plus the wait" is not an answer anyone can act on.
 * The call it brackets does three things with completely different meanings:
 *
 *   26  DRAIN   the game's 50 Hz body, run from main-loop context at the engine's own frame hook
 *               (docs/amiga-arch.md).  At ~1 FPS that is ~50 body ticks per painted frame, and the
 *               body DRAWS — so this is real, faithful, non-negotiable engine work whose share
 *               GROWS as the framerate falls.  Optimising it is not the same as removing it.
 *   27  DECODE  RevsScreen::decode(), the BBC frame buffer -> bitplanes pass.  Pure port overhead
 *               and the thing docs/direct-bitplane-plan.md is about.
 *   28  SPIN    waiting for the next real vblank after the paint.  Should be ~0 at 1 FPS; anything
 *               large here means the loop is waiting for the display rather than the reverse.
 *
 * Phase 25 keeps whatever is left (the call itself), so the old row is the sum of 25..28. */
/* 29 splits the 50 Hz body itself: `body_tick_xor_anim`, the band-4 arm, which is the only part of the
 * IRQ1V band cycle that simulates and DRAWS (display lines 120-143).  Everything else in the
 * cycle just reloads the timer and rewrites the palette.  Bracketed at its own JSR ($4EF5) with
 * phase 26 reopened immediately after, so the split is exact and the enclosing drain keeps the
 * remainder — see tools/transpile.py PRE_INSN_HOOKS. */
#define PROBE_PHASE_BODYARM 29

#define PROBE_PHASE_DRAIN  26
#define PROBE_PHASE_DECODE 27
#define PROBE_PHASE_SPIN   28

/* ⭐ Beam ticks in one PAL display frame, in the units beamTick() composes
   (line * 256 + hpos, 313 lines).  The VERTB ISR adds this to g_beamEpoch once per
   frame, which is what makes the tick monotonic ACROSS frames.
   ⚠ It must be an ADDITION, never `frames * PROBE_BEAM_TICKS_PER_FRAME`: a 32-bit
   multiply emits __mulsi3 and the 68000 has none (amiga/Makefile muldiv-audit). */
#define PROBE_BEAM_TICKS_PER_FRAME 80128UL   /* 313 * 256 */

/* Accumulated whole display frames, in beam ticks.  Bumped by the VERTB ISR only. */
extern volatile unsigned long g_beamEpoch;

/* Call once per display frame from the VERTB ISR, before any game work. */
#define PROBE_VBI() (g_beamEpoch += PROBE_BEAM_TICKS_PER_FRAME)

#ifdef __cplusplus
}
#endif

#define PROBE_PHASE(id) probe_phase(id)

#else
#define PROBE_PHASE(id) ((void)0)
#define PROBE_VBI()     ((void)0)
#define PROBE_ISR_BEGIN() ((void)0)
#define PROBE_ISR_END()   ((void)0)
#define PROBE_IRQ_BEGIN() ((void)0)
#define PROBE_IRQ_END(s)  ((void)(s))
#define PROBE_IRQ_NULL()  ((void)0)
/* ⚠ The phase IDs are plain numbers and must exist in EVERY build: `make SHAPE=1` without PROBES
   passes them to the shape probe (src/platform/shape.h §WHO ACTUALLY DRAWS), and inside the
   PROBE_PHASE macro they were only ever unevaluated macro arguments — so a non-PROBES build
   compiled for a year without needing them and then failed to compile the moment something else
   used one.  Kept in sync with the definitions above by hand; `make gen` checks FRAMEWAIT. */
#define PROBE_PHASE_FRAMEWAIT 25
#define PROBE_PHASE_DRAIN     26
#define PROBE_PHASE_DECODE    27
#define PROBE_PHASE_SPIN      28
#define PROBE_PHASE_BODYARM   29
#endif
