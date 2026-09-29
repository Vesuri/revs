/* probe.cpp — the phase-bracket accumulator.  See probe.h for what the numbers mean. */
#include "probe.h"

#ifdef REVS_PROBE

#if defined(REVS_PLATFORM_AMIGA)
#include "framework/AmigaHardware.h"
#endif
#if defined(REVS_PROBE_RACE)
#include "../cpu/mem_decl.h"
extern "C" MEM_QUAL uint8_t mem[65536];   /* session_is_race opens the window */
#endif

extern "C" {

/* Accumulated beam ticks per phase, and how many times each phase was entered.
   ⚠ Both arrays must be in PROBE_SYMS (amiga/Makefile) or --gc-sections drops them and
   gdb prints instruction bytes as a value — a fake measurement, not an obvious zero. */
volatile unsigned long g_phaseTicks[PROBE_PHASES] = {0};
volatile unsigned long g_phaseCount[PROBE_PHASES] = {0};
volatile unsigned long g_phaseFrames = 0;      /* completed main-loop iterations */

/* ⭐⭐⭐ `make PROBEFIELDS=N` — FREEZE THE WHOLE TABLE AFTER N DISPLAY FIELDS, so two arms
   cover the SAME EMULATED WINDOW instead of the same wall-clock window.
 *
 * ⚠⚠ Why this exists: diag_run.sh bounds a run with `sleep`, i.e. HOST seconds, and under warp
 * the host's throughput varies with whatever else the machine is doing.  Two arms of one A/B
 * then cover different amounts of GAME time — measured: 75 s against 144 s of emulated time
 * from the same 30 s wall window — and a `STRAIGHT_TO_RACE` run leaves the track and resets, so
 * the longer arm is diluted with a different scene mix.  The tells were `ONE BODY TICK` moving
 * 1313 -> 1417 us with the 50 Hz body untouched, phase 0 holding 1 crash hold against 3, and two
 * phases whose unit/run/line CENSUS was identical moving by 4%.  docs/perf-method.md.
 *
 * ⭐ The cap counts FIELDS, not loop frames.  A faster build drains fewer 50 Hz body ticks per
 * loop frame, so capping on PAINTED frames would give the arms different amounts of SIM time and
 * hence different scenes; fields are emulated real time, so the sim trajectory, the scene
 * sequence and the crash holds are all identical at the cap and the arms differ only in how many
 * frames they PAINTED inside it — which is the thing being measured.
 *
 * ⚠ The run keeps going after the freeze (the window is over, not the program), so the wall
 * delay must merely be long enough for the SLOWEST arm to reach N: `g_probeFrozen` is the proof
 * it did, and a table read with g_probeFrozen == 0 is a wall-clock sample like any other.
 * ⚠ g_vbiCount is a uint16_t, so N must stay under 65536 (21.8 minutes).
 *
 * ⚠⚠ THE PARTIAL-FREEZE TRAP, and it is the reason for the five shadows below.  Closing the
 * window stops the PHASE accumulators, but the run keeps going, so every counter bumped from
 * somewhere else — the body drain, the ISR, the view census, g_vbiCount, the beam epoch — keeps
 * climbing.  Any row that divides one of those by a frozen quantity is then FICTION, and it does
 * not look like fiction: `ONE BODY TICK` read 379 us against a true 1313, and the view census
 * read 5526 units/frame against a true 1442, both by exactly the ratio of the whole run to the
 * window.  A plausible wrong number in a table nobody flagged is the failure this project pays
 * most for, so the freeze SNAPSHOTS what it cannot stop, once, and the phase script reads the
 * snapshot whenever g_probeFrozen is set.
 *
 * ⭐ Snapshotting is the right shape here and gating the counters is NOT: the census macros run
 * per UNIT VISIT (thousands a frame), so testing g_probeFrozen inside them would add a load to
 * the very loop whose unit count is being A/B'd — the instrument would move the measurement.
 * One copy of nine longwords at the window's edge costs nothing measurable.
 *
 * ⭐ g_probeFrozen holds the BEAM TICK at which the window closed, not a bare 1, so it is both
 * the flag ("this table is comparable") and the window's exact length — which is what the
 * accounted-for check and the FRAME row need in place of the still-climbing g_beamEpoch.  It
 * cannot read 0 while set: the window closes at N >= 1 fields, i.e. at >= 80120 ticks. */
volatile unsigned long g_probeFrozen      = 0;   /* beam ticks at the freeze (0 = still open) */
volatile unsigned long g_probeFrozenBody  = 0;   /* g_bodyTicks there                         */
volatile unsigned long g_probeFrozenUnits[3] = {0, 0, 0};   /* g_viewUnits there              */
volatile unsigned long g_probeFrozenRuns[3]  = {0, 0, 0};   /* g_viewRuns  there              */
volatile unsigned long g_probeFrozenLines[3] = {0, 0, 0};   /* g_viewLines there              */

/* ⭐⭐ `make PROBERACE=1` (with RACEPROPER=1 PROBEFIELDS=N) — THE WINDOW OPENS AT THE RACE START.
 * A RACEPROPER run reaches the grid ~100 000 fields in, through a qualifying session in which the
 * player is alone on track exactly as in practice, so a window counted from boot measures
 * qualifying (and g_vbiCount, a uint16_t, has wrapped by then).  Here nothing accumulates until
 * session_is_race ($006C) has bit 7 set; at that moment every phase row and count is zeroed and
 * the counters the freeze snapshots take a base, and the window then closes N fields later —
 * counted with a wrap-safe 16-bit delta, so N is again bounded by the window, not the run.
 * g_probeFrozen is then the window's LENGTH in beam ticks, which is what phase4_prof.gdb already
 * treats it as.  ⭐ g_probeRaceOpenedAt prints the build's state: 0 in any other build, and the
 * beam tick of the race start once it has opened (so 0 in a PROBERACE build means "not yet"). */
volatile unsigned long g_probeRaceOpenedAt = 0;

/* ⭐⭐⭐ THE BUILD'S OWN A/B STATE, AS A NUMBER THE PROBE SCRIPT PRINTS — CLAUDE.md's
 * "an A/B switch must PRINT its own state", made structural rather than per-flag.
 *
 * ⚠⚠ IT EXISTS BECAUSE A NO-OP FLAG READS AS A NULL RESULT, NOT AS A MISTAKE.  `VIEWOWN=1`
 * was once written into `amiga/Makefile` as `CFLAGS +=` where that file uses `EXTRA_DEFINES`,
 * so the define never reached the cross-compiler; the two arms then produced BIT-IDENTICAL
 * phase tables (+0.000 ms on every row), which is a perfectly readable "the change did
 * nothing" if you do not happen to notice that two different builds cannot agree to the tick.
 * One word in the header line turns that silent failure into a visible 0.
 *
 * ⭐ It is a BITMASK, not one global per flag, so a new switch costs one line here and no
 * `PROBE_SYMS` edit — and it is composed from the SAME `#ifdef`s the code reads, in a
 * translation unit the define has to reach the same way, which is what makes it a test of
 * the build rather than a restatement of the Makefile.
 *
 * ✅ SABOTAGE-EQUIVALENT ALREADY RUN, and it is the discriminating one: the two arms of the
 * `VIEWOWN` A/B printed `build=0` and `build=1` on the same source tree, which a flag-blind
 * instrument cannot do.  Both cases, both directions, no extra run. */
enum {
    PROBE_BUILD_VIEW_OWN_SHORT = 1u << 0,   /* VIEWOWN=1    — phases 2/3 own their runs     */
    PROBE_BUILD_VIEWSKIP       = 1u << 1,   /* VIEWSKIP=1   — the all-clean line skip       */
    PROBE_BUILD_SPAN_EMIT      = 1u << 2,   /* SPANFILL=*   — the direct-span arm is built  */
    PROBE_BUILD_SPAN_STATELESS = 1u << 3,   /* SPANFILL>=3  — ...with the stateless predicate */
    PROBE_BUILD_SPAN_TAKEOVER  = 1u << 4,   /* SPANFILL=5   — ...and phase 1's line loop    */
    PROBE_BUILD_NO_UNIT_WORK   = 1u << 5,   /* NOUNITS=1/2  — ⚠ picture wrong by construction */
    PROBE_BUILD_NO_UNIT_LOOP   = 1u << 6,   /* NOUNITS=2    — ⚠ likewise                      */
    PROBE_BUILD_VIEWP3         = 1u << 7,   /* VIEWP3=*     — ⚠ likewise                      */
    PROBE_BUILD_BODY_IN_ISR    = 1u << 8,   /* BODY_IN_ISR=1 — the rejected 50 Hz model      */
    PROBE_BUILD_VIEW_CARVE     = 1u << 9,   /* VIEWCARVE=1  — ⚠ picture wrong: the ph27 ceiling */
    PROBE_BUILD_VIEW_OWN_FULL  = 1u << 10,  /* VIEWFULL=1   — phase 1 owns its LINE LOOP too   */
    PROBE_BUILD_FASTMEM        = 1u << 11,  /* FASTMEM=1    — longword memset/memcpy/memmove   */
    PROBE_BUILD_SPAN_ASM       = 1u << 12,  /* SPANASM=1    — the span walk in 68000 asm       */
    PROBE_BUILD_SETUP_ASM      = 1u << 13   /* SETUPASM=1   — the whole span pass in 68000 asm */
};

volatile unsigned long g_probeBuildFlags =
#ifdef REVS_FASTMEM
    PROBE_BUILD_FASTMEM |
#endif
#if defined(REVS_SPAN_ASM) && !defined(REVS_ROADSPLIT) && !defined(REVS_SHAPE) && !defined(REVS_VIEWSKIP) \
    && !defined(REVS_SRC_EVENTS) && !defined(REVS_SRC_EVENTS_CHECK) && !defined(REVS_ROAD_ARM)
    PROBE_BUILD_SPAN_ASM |      /* ⚠ the same condition as REVS_SPAN_ASM_ON in revs_native.c */
#endif
#if defined(REVS_SPAN_ASM) && defined(REVS_SETUP_ASM) && !defined(REVS_WALKCHECK) && !defined(REVS_ROADSPLIT) \
    && !defined(REVS_SHAPE) && !defined(REVS_VIEWSKIP) && !defined(REVS_SRC_EVENTS)                         \
    && !defined(REVS_SRC_EVENTS_CHECK) && !defined(REVS_ROAD_ARM)
    PROBE_BUILD_SETUP_ASM |     /* ⚠ the same condition as REVS_SETUP_ASM_ON in revs_native.c */
#endif
#ifdef REVS_VIEW_OWN_SHORT
    PROBE_BUILD_VIEW_OWN_SHORT |
#endif
#ifdef REVS_VIEWSKIP
    PROBE_BUILD_VIEWSKIP |
#endif
#ifdef REVS_SPAN_EMIT
    PROBE_BUILD_SPAN_EMIT |
#endif
#ifdef REVS_SPAN_STATELESS
    PROBE_BUILD_SPAN_STATELESS |
#endif
#ifdef REVS_SPAN_TAKEOVER
    PROBE_BUILD_SPAN_TAKEOVER |
#endif
#ifdef REVS_NO_UNIT_WORK
    PROBE_BUILD_NO_UNIT_WORK |
#endif
#ifdef REVS_NO_UNIT_LOOP
    PROBE_BUILD_NO_UNIT_LOOP |
#endif
#ifdef REVS_VIEWP3
    PROBE_BUILD_VIEWP3 |
#endif
#ifdef REVS_BODY_IN_ISR
    PROBE_BUILD_BODY_IN_ISR |
#endif
#ifdef REVS_VIEW_CARVE
    PROBE_BUILD_VIEW_CARVE |
#endif
#ifdef REVS_VIEW_OWN_FULL
    PROBE_BUILD_VIEW_OWN_FULL |
#endif
    0u;

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
#if defined(REVS_PROBE_FIELDS) && defined(REVS_PLATFORM_AMIGA)
    extern volatile unsigned long g_bodyTicks;
    extern volatile uint16_t g_vbiCount;
#if defined(REVS_PROBE_RACE)
    /* The window has not opened: track the phase, accumulate nothing (PROBERACE above). */
    static unsigned long s_openBody, s_openUnits[3], s_openRuns[3], s_openLines[3], s_fields;
    static uint16_t      s_lastVbi;
    if (!g_probeRaceOpenedAt) {
        if (!(mem[0x006C] & 0x80u)) { s_phase = id; s_mark = now; return; }   /* session_is_race */
        int i;
        for (i = 0; i < PROBE_PHASES; i++) { g_phaseTicks[i] = 0; g_phaseCount[i] = 0; }
        g_phaseFrames = 0;
        s_openBody = g_bodyTicks;
        for (i = 0; i < 3; i++) {
            s_openUnits[i] = g_viewUnits[i]; s_openRuns[i] = g_viewRuns[i]; s_openLines[i] = g_viewLines[i];
        }
        s_lastVbi = g_vbiCount; s_fields = 0;
        g_probeRaceOpenedAt = now;
    }
    s_fields += (uint16_t)(g_vbiCount - s_lastVbi);  s_lastVbi = g_vbiCount;
    const unsigned long fields = s_fields, openTick = g_probeRaceOpenedAt;
#else
    enum { s_openBody = 0 };
    static const unsigned long s_openUnits[3] = {0, 0, 0}, s_openRuns[3] = {0, 0, 0},
                               s_openLines[3] = {0, 0, 0};
    const unsigned long fields = g_vbiCount, openTick = 0;
#endif
    /* The window is closed: stop accumulating ticks, counts AND frames, so every printed
       number describes exactly the first REVS_PROBE_FIELDS fields of the window.  Returning
       here also freezes phase 0, which is the comparison's validity fingerprint. */
    if (fields >= (unsigned long)(REVS_PROBE_FIELDS)) {
        if (!g_probeFrozen) {
            /* Snapshot the counters the freeze cannot stop — see the trap above.  The flag
               is published LAST so no reader can see a half-built snapshot. */
            int i;
            g_probeFrozenBody = g_bodyTicks - s_openBody;
            for (i = 0; i < 3; i++) {
                g_probeFrozenUnits[i] = g_viewUnits[i] - s_openUnits[i];
                g_probeFrozenRuns[i]  = g_viewRuns[i]  - s_openRuns[i];
                g_probeFrozenLines[i] = g_viewLines[i] - s_openLines[i];
            }
            g_probeFrozen = now - openTick;
        }
        return;
    }
#endif
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

/* Which phase is open right now.  A twin that wants to bracket one of its OWN inner loops
   has to reopen whatever phase it interrupted, and only this file knows what that is
   (src/gen/revs_native.c, the $7BE2 unit-loop split behind REVS_VIEWSPLIT). */
int probe_phase_current(void)
{
    return s_phase;
}

/* ⭐ The MONOTONIC beam clock, exposed for one-off timing outside the phase table — the
   crash-hold field-cost probe (Revs::runBandCycle) reads it to price ONE field cycle, which
   can straddle a frame wrap when a field is slow, so the epoch-corrected value is required.
   Only meaningful in a PROBES build, where the VERTB ISR advances g_beamEpoch (PROBE_VBI). */
unsigned long probe_beam_tick(void)
{
    return beamTick();
}

/* ⭐⭐ TWO SUB-FRAME TIMERS THAT ARE NOT PHASES, added 2026-08-16 to explain the 51%.
 *
 * The phase table charges the 50 Hz body drain 526 ms per painted frame, i.e. ~10.5 ms of every
 * 20 ms tick, and splitting the body's own arm ($52A4) off accounted for only 277 us of it.  So
 * the cost is either inside `irq1v_band_schedule` (the five band arms) or in the VERTB ISR that happens
 * to run while the drain phase is open — and a PHASE cannot tell those apart, because the ISR
 * preempts whatever phase is current and its time lands there.
 *
 * These are separate accumulators with their own counts, so each gives a COST PER CALL rather than
 * a share, and neither disturbs the phase table.  Two beam reads per call.
 */
volatile unsigned long g_probeIsrTicks = 0;    /* the VERTB ISR, total  */
volatile unsigned long g_probeIsrCount = 0;
volatile unsigned long g_probeIrqTicks = 0;    /* irq1v_band_schedule, one band arm per call */
volatile unsigned long g_probeIrqCount = 0;

/* ...and the same total SPLIT BY BAND, because the five arms are not the same job (probe.h). */
volatile unsigned long g_probeBandTicks[PROBE_BANDS] = {0};
volatile unsigned long g_probeBandCount[PROBE_BANDS] = {0};

/* The view sweep's own workload, per painting phase — probe.h §PROBE_VIEW_*.  These are COUNTS,
   not times: they exist so the ms rows for phases 24/33/34 can be divided by the work each did. */
volatile unsigned long g_viewUnits[3] = {0, 0, 0};
volatile unsigned long g_viewRuns[3]  = {0, 0, 0};
volatile unsigned long g_viewLines[3] = {0, 0, 0};
int g_viewPhaseIdx = 0;

/* Exactly 14 000 68000 cycles: 1000 iterations of `nop` (4) + `dbra` taken (10).  See probe.h —
   this is a measuring stick, and its whole value is that the count is known rather than estimated. */
void probe_burn_cycles(void)
{
#if defined(REVS_PLATFORM_AMIGA)
    __asm__ volatile ("move.w #999,%%d0\n"
                      "0:\n\t"
                      "nop\n\t"
                      "dbra %%d0,0b\n"
                      : : : "d0");
#endif
}

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

#ifdef REVS_ISRSPLIT
/* ⭐⭐ The VERTB handler's own split — probe.h §ISRSPLIT.  A mark/close chain exactly like
   probe_phase(), but with its own accumulator so the phase table stays comparable with the
   tables already published.  probe_isr_split(-1) closes the last sub-bracket. */
volatile unsigned long g_isrSplitTicks[PROBE_ISR_SLOTS] = {0};
volatile unsigned long g_isrSplitCount[PROBE_ISR_SLOTS] = {0};
static int           s_isrSlot = -1;
static unsigned long s_isrSplitMark = 0;

void probe_isr_split(int slot)
{
    unsigned long now = beamTick();
    long d = (long)now - (long)s_isrSplitMark;
    if (d >= 0 && s_isrSlot >= 0 && s_isrSlot < PROBE_ISR_SLOTS)
        g_isrSplitTicks[s_isrSlot] += (unsigned long)d;
    if (slot >= 0 && slot < PROBE_ISR_SLOTS) g_isrSplitCount[slot]++;
    s_isrSlot = slot;
    s_isrSplitMark = now;
}
#endif

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

/* ⭐⭐ GEOSPLIT counters — the "why phase 5 is 16%" call tallies (probe.h §GEOSPLIT).  Defined
   independently of REVS_PROBE so `make GEOSPLIT=1` on the HOST counts without the beam machinery;
   the beam TIME split reuses probe_phase and so needs REVS_PROBE (i.e. PROBES=1) on the Amiga. */
#ifdef REVS_GEOSPLIT
extern "C" {
volatile unsigned long g_geoFrames = 0;
volatile unsigned long g_geoPoints[2] = {0, 0};
volatile unsigned long g_geoSubdiv = 0;
volatile unsigned long g_geoBearing = 0;
volatile unsigned long g_geoProject = 0;
volatile unsigned long g_geoDiv = 0;
volatile unsigned long g_geoHypot = 0;
int g_geoSide = 0;
}
#endif

/* ⭐⭐ ROADSPLIT counters — the "why phase 11 is 16%" tallies (probe.h §ROADSPLIT).  Defined
   independently of REVS_PROBE so `make ROADSPLIT=1` on the HOST counts without the beam
   machinery; the beam TIME split reuses probe_phase and so needs REVS_PROBE (PROBES=1). */
#ifdef REVS_ROADSPLIT
extern "C" {
volatile unsigned long g_roadFrames = 0;
volatile unsigned long g_roadSpans = 0;
volatile unsigned long g_roadSpanLines = 0;
volatile unsigned long g_roadColSteps = 0;
volatile unsigned long g_roadCols = 0;
volatile unsigned long g_roadFillLines = 0;
volatile unsigned long g_roadMarkPts = 0;
}
#endif
