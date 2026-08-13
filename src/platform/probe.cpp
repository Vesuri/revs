/* probe.cpp — the phase-bracket accumulator.  See probe.h for what the numbers mean. */
#include "probe.h"

#ifdef REVS_PROBE

#if defined(REVS_PLATFORM_AMIGA)
#include "framework/AmigaHardware.h"
#endif

extern "C" {

/* Accumulated beam ticks per phase, and how many times each phase was entered.
   ⚠ Both arrays must be in PROBE_SYMS (amiga/Makefile) or --gc-sections drops them and
   gdb prints instruction bytes as a value — a fake measurement, not an obvious zero. */
volatile unsigned long g_phaseTicks[PROBE_PHASES] = {0};
volatile unsigned long g_phaseCount[PROBE_PHASES] = {0};
volatile unsigned long g_phaseFrames = 0;      /* completed main-loop iterations */

/* Whole display frames elapsed, in beam ticks.  Bumped by the VERTB ISR (PROBE_VBI()). */
volatile unsigned long g_beamEpoch = 0;

/* Read a MONOTONIC beam position — across frames, not just within one.
   VPOSR bit 0 is vertical bit 8, VHPOSR packs (vpos low 8) << 8 | hpos.  Composing
   line * 256 + hpos gives a tick that increases through the frame and wraps once per
   DISPLAY frame (80 128 ticks, 20 ms).
 *
 * ⚠⚠ THE WRAP IS THE WHOLE POINT — read docs/perf-method.md §"the instrument loses ~95%
 * of the frame" before touching this.  This used to return the bare in-frame position and
 * let probe_phase() drop negative deltas, on the reasoning that a wrap "costs one bracket
 * per frame out of hundreds".  That is true only while a bracket is much shorter than a
 * display frame, and at 1.4 FPS one game-loop iteration spans ~34 of them: individual
 * phases were routinely longer than 20 ms, so a phase that straddled a wrap was discarded
 * WHOLE and one that straddled several silently lost multiples of 80 128.  Measured
 * effect: the brackets accounted for 3.9-5.7% of the frame, and three phases read exactly
 * zero forever.  Every share table taken before 2026-08-13 is void.
 *
 * The fix adds g_beamEpoch, which the VERTB ISR advances by exactly one frame's worth.
 * Two hazards, both handled:
 *   - the ISR can land between our two reads, pairing a new epoch with an old beam (or
 *     vice versa), so re-read the epoch and retry if it moved;
 *   - the beam wraps at line 0 and the ISR fires within a few microseconds of that, so a
 *     sample in the skew window can pair a wrapped beam with a not-yet-bumped epoch and
 *     appear to go backwards.  Clamp to the last value returned; the error is bounded by
 *     the ISR latency, not by a frame. */
static inline unsigned long beamTick(void)
{
#if defined(REVS_PLATFORM_AMIGA)
    unsigned long epoch, beam, e2;
    do {
        epoch = g_beamEpoch;
        unsigned short vh = *vhposrPointer;
        unsigned short vp = *vposrPointer;
        beam  = ((unsigned long)((vp & 1) << 8 | (vh >> 8)) << 8) | (vh & 0xFF);
        e2    = g_beamEpoch;
    } while (epoch != e2);

    static unsigned long s_prev = 0;
    unsigned long t = epoch + beam;
    if (t < s_prev) t = s_prev;      /* never run backwards across the wrap/ISR skew */
    s_prev = t;
    return t;
#else
    /* The host has no beam.  Count calls, so the shape of the instrumentation can be
       tested here even though the numbers mean nothing (PlatformHost.h). */
    static unsigned long t = 0;
    return ++t;
#endif
}

static int           s_phase = 0;
static unsigned long s_mark  = 0;

void probe_phase(int id)
{
    unsigned long now = beamTick();
    /* beamTick() is monotonic now, so `d` can only be negative if g_beamEpoch itself
       wrapped (2^32 ticks ≈ 18 minutes of run time).  Keep the guard as a backstop —
       it must never be the thing that makes a long phase readable. */
    long d = (long)now - (long)s_mark;
    if (d >= 0 && s_phase >= 0 && s_phase < PROBE_PHASES)
        g_phaseTicks[s_phase] += (unsigned long)d;
    if (id >= 0 && id < PROBE_PHASES) g_phaseCount[id]++;
    if (id == 1) g_phaseFrames++;          /* phase 1 is the top of the loop */
    s_phase = id;
    s_mark  = now;
}

} /* extern "C" */

#endif /* REVS_PROBE */
