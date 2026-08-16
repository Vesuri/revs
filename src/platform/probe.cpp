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

/* ⭐⭐ TWO SUB-FRAME TIMERS THAT ARE NOT PHASES, added 2026-08-16 to explain the 51%.
 *
 * The phase table charges the 50 Hz body drain 526 ms per painted frame, i.e. ~10.5 ms of every
 * 20 ms tick, and splitting the body's own arm ($52A4) off accounted for only 277 us of it.  So
 * the cost is either inside `irq1v_handler` (the five band arms) or in the VERTB ISR that happens
 * to run while the drain phase is open — and a PHASE cannot tell those apart, because the ISR
 * preempts whatever phase is current and its time lands there.
 *
 * These are separate accumulators with their own counts, so each gives a COST PER CALL rather than
 * a share, and neither disturbs the phase table.  Two beam reads per call.
 */
volatile unsigned long g_probeIsrTicks = 0;    /* the VERTB ISR, total  */
volatile unsigned long g_probeIsrCount = 0;
volatile unsigned long g_probeIrqTicks = 0;    /* irq1v_handler, one band arm per call */
volatile unsigned long g_probeIrqCount = 0;

/* ...and the same total SPLIT BY BAND, because the five arms are not the same job (probe.h). */
volatile unsigned long g_probeBandTicks[PROBE_BANDS] = {0};
volatile unsigned long g_probeBandCount[PROBE_BANDS] = {0};

static unsigned long s_isrMark = 0;
static unsigned long s_irqMark = 0;

void probe_isr_begin(void) { s_isrMark = beamTick(); }
void probe_isr_end(void)
{
    long d = (long)beamTick() - (long)s_isrMark;
    if (d >= 0) { g_probeIsrTicks += (unsigned long)d; g_probeIsrCount++; }
}
/* ⚠⚠ GATED BEHIND ITS OWN FLAG (`make HWTIME=1`), not plain PROBES, and the reason is a
   measurement: the counter pair alone moved the body tick from 10.5 ms to 12.3 ms, and adding the
   two beam reads per access took it to 22.7 ms — an observer effect of over 2x on the very row it
   was measuring.  A default PROBES table has to stay comparable with the ones already published. */
#ifdef REVS_PROBE_HWTIME
volatile unsigned long g_probeHwTicks = 0;
static unsigned long s_hwMark = 0;
void probe_hw_begin(void) { s_hwMark = beamTick(); }
void probe_hw_end(void)
{
    long d = (long)beamTick() - (long)s_hwMark;
    if (d >= 0) g_probeHwTicks += (unsigned long)d;
}
#endif

/* ⭐⭐ THE EMPTY-BRACKET CONTROL, slot 6.  The per-band split says a band arm costs ~600 us
   BEFORE it does any work — band 3 writes four palette bytes and reads 685 us against band 2's
   sixteen at 820 — so the fixed part is either the shim or THIS INSTRUMENT, and those two lead to
   opposite conclusions.  Measure a bracket around nothing, on the same path, at the same rate:
   whatever it reads is the floor under every other row in the table.
   (docs/method-lessons.md — the hardware counters in this same file already moved the row they
   were measuring by 2x, so the instrument is a first-class suspect, not a last resort.) */
void probe_irq_null(void)
{
    unsigned long a = beamTick();
    long d = (long)beamTick() - (long)a;
    if (d >= 0) { g_probeBandTicks[6] += (unsigned long)d; g_probeBandCount[6]++; }
}

void probe_irq_begin(void) { s_irqMark = beamTick(); }
/* `state` is mem[$4F43] as the handler FOUND it — see probe.h for why the attribution is by
   entry band and why the per-band counts are not equal. */
void probe_irq_end(int state)
{
    long d = (long)beamTick() - (long)s_irqMark;
    if (d >= 0) {
        g_probeIrqTicks += (unsigned long)d; g_probeIrqCount++;
        int slot = (state >= 0 && state <= 4) ? state : (state == 0xFF ? 5 : 7);
        g_probeBandTicks[slot] += (unsigned long)d; g_probeBandCount[slot]++;
    }
}

} /* extern "C" */

#endif /* REVS_PROBE */
