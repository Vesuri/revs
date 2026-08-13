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

/* Number of phases the table below can hold — one per top-level call in $1701-$1763,
   plus id 0, plus slack. */
#define PROBE_PHASES 40

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
#endif
