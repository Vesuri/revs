#pragma once
/* AutoRun — a deterministic scripted keyboard for unattended runs.
 *
 * WHY IT EXISTS.  `docs/perf-method.md` Rule 3 names "the unattended run ending" as one of
 * the two ways a framerate figure comes out wrong: a headless run drives no input, so it
 * eventually stops doing the work being measured while the vblank counter keeps ticking.
 * Revs makes that failure *total* rather than gradual — with no keyboard the front end
 * never leaves menu_wait_key ($6571), so an uninstrumented headless run measures a menu
 * spin and reports it as the game's framerate.  This walks the front end and then holds
 * the throttle, so the measurement window contains the driving loop.
 *
 * ⭐ THE CLOCK IS POLL COUNT, NOT TIME.  Each answered keyDown() advances the script by
 * one.  That makes the input sequence bit-identical across builds no matter how fast they
 * render — the same reason `FIXED_RNG=1` exists.  A wall-clock or vblank-driven script
 * would re-introduce exactly the trajectory confound perf-method.md §Rule 2 warns about:
 * a faster build would reach a different point on the circuit and be timed on a different
 * workload.
 *
 * It is NOT a substitute for real input (Phase 5, mouse + keyboard) and never renders a
 * gameplay judgement — it exists so a number can be measured.
 */
/* ⭐ ONE PREDICATE FOR "THIS BUILD DRIVES ITSELF", and it exists because the four-flag test below
 * was written out by hand at each site and one of them went stale: `make COMPETITION=1` compiled
 * the script and never instantiated the object that runs it, so the target sat in the front end
 * for a whole run and it was documented as a timing bug (PlatformAmiga.h has the full note).
 * Every new place that must behave differently in an unattended run tests THIS, so adding a flag
 * is one edit rather than N.
 */
#if defined(REVS_FPSCOUNT) || defined(REVS_PROBE) || defined(REVS_STRAIGHT_TO_RACE) || \
    defined(REVS_COMPETITION)
#define REVS_AUTORUN_BUILD 1
#endif

/* ⚠ Same include dance as platform.h: the Amiga build is freestanding with no libstdc++,
   and gets its integer types from the force-included framework/SASCCompat.h. */
#if !defined(REVS_PLATFORM_AMIGA)
#include <cstdint>
#endif

class AutoRun {
public:
    /* Answer OSBYTE 129 for the raw X register (the 256-n negative-INKEY form). */
    bool keyDown(uint8_t x);

    /* True once the script has run out AND this build hands the keyboard back to the
       player — a REVS_STRAIGHT_TO_RACE build, whose whole point is to be driven.  A
       measurement build never reports done: its steady state (throttle held) is part of
       the script, because the window has to have the same key set in every build.
       ⚠ A caller that pushes the script's answers into the real key state must stop doing
       so here, or it clears the keys the player is actually holding. */
    bool done() const;

    /* How many key polls the script has answered — the script's clock, and a useful
       liveness read from gdb: if it stops advancing, the game stopped asking. */
    unsigned long polls() const { return m_polls; }

    /* Index of the script step currently in force.  Past the last step the script is in
       its steady state (throttle held), which is where a measurement window belongs. */
    unsigned stepIndex() const { return m_step; }

private:
    unsigned long m_polls    = 0;   /* total answered polls */
    unsigned long m_stepAt   = 0;   /* m_polls when the current step began */
    unsigned      m_step     = 0;   /* index into the script table */
    unsigned      m_hits     = 0;   /* times the current step's key was answered HELD */
    /* REVS_HOLD_STEER=l|r — a steering key held alongside the throttle in the steady state.
       Resolved once, on the first poll, because the Amiga build is freestanding. */
    unsigned char m_holdSteer = 0;
    bool          m_steerRead = false;
};
