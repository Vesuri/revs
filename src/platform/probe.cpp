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

/* Read a monotonic-within-the-frame beam position.
   VPOSR bit 0 is vertical bit 8, VHPOSR packs (vpos low 8) << 8 | hpos.  Composing
   line * 256 + hpos gives a tick that increases through the frame and wraps once per
   frame; the wrap is handled by discarding negative deltas, which costs one bracket per
   frame out of hundreds and is far cheaper than reading a long-word clock. */
static inline unsigned long beamTick(void)
{
#if defined(REVS_PLATFORM_AMIGA)
    unsigned short vh = *vhposrPointer;
    unsigned short vp = *vposrPointer;
    return ((unsigned long)((vp & 1) << 8 | (vh >> 8)) << 8) | (vh & 0xFF);
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
