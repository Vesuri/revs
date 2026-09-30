#pragma once
/* Phase brackets for the engine's main loop — the Phase 4 "which functions are hot" answer.
 *
 * WHY BRACKETS AND NOT PC SAMPLING.  The engine's per-frame body ($1701-$1763) is a FLAT
 * sequence of about twenty-five JSRs.  That shape is unusually kind to bracketing: each
 * call is a phase, the phases are disjoint, and the boundaries are exact rather than
 * statistical — no sampling bias, no symbol-attribution guesswork through -O2 inlining
 * (which on the host collapsed the entire loop into one frame of `race_main_loop`).
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

/* ⭐⭐ THE PHASE CANARY (host, `make INK_WATCH=1`; src/platform/bbc_hw.cpp) — which frame-body
   STAGE changed a given byte range?  It rides the phase brackets rather than a write seam,
   because the port's third write path (a plain `mem[addr] = value`, which is what the transpiler
   emits for every constant address) reaches NO seam and so looks like "never written".
   Independent of REVS_PROBE: available in a plain host build, compiled to nothing otherwise. */
#ifdef REVS_INK_WATCH
#ifdef __cplusplus
extern "C"
#endif
void revs_canary(int phase);
#define REVS_CANARY(id) revs_canary(id)
#else
#define REVS_CANARY(id) ((void)0)
#endif

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

/* ⭐⭐ ISRSPLIT (`make ISRSPLIT=1 PROBES=1`) — WHAT THE VERTB HANDLER SPENDS ITS ~1.1 ms ON.
 *
 * The ISR is a FIXED TAX ON WALL CLOCK: it fires 50 times a second whatever the framerate is
 * doing, so a millisecond here is 5% of every second, permanently, and it is charged pro-rata to
 * whichever phase it preempted — no row of the phase table can see it.  PROBE_ISR_BEGIN/END
 * already price the whole handler; this splits it into the seven things it actually does.
 *
 * Same shape as probe_phase(): each call closes the open sub-bracket and opens `slot`, and
 * probe_isr_split(-1) closes the last one.  Slot 0 is the NULL CONTROL — a bracket around
 * nothing, on the same path at the same rate, so the floor under every other row is measured
 * rather than assumed (the same control that rescued the per-band table; probe.cpp
 * §probe_irq_null).  ⚠ Every row below is a bracket cost ABOVE that floor.
 *
 * ⚠ Its own cost is two beam reads per transition, ~8 transitions a field — so an ISRSPLIT
 * build's FPS and phase shares are void.  Read it for the SPLIT and nothing else. */
#define PROBE_ISR_SLOTS 10
#define PROBE_ISR_NULL     0   /* the empty-bracket control                                  */
#define PROBE_ISR_PROLOGUE 1   /* INTREQ clear, g_vbiCount, the FPS series sample            */
#define PROBE_ISR_MOUSE    2   /* PlatformAmiga::sampleMouse                                 */
#define PROBE_ISR_ENTRY    3   /* RevsScreen::noteVbiEntry (the beam-entry record)           */
#define PROBE_ISR_FLASH    4   /* tt_tick_flash (the SAA5050 field-rate flash phase)         */
#define PROBE_ISR_SCREEN   5   /* RevsScreen::vbiUpdate — buildBands + present, when ready   */
#define PROBE_ISR_AUDIO    6   /* revs_audio_vbi — two MOS sound ticks, Paula when it moved  */
#define PROBE_ISR_TAIL     7   /* the pending-tick accounting                                */
#define PROBE_ISR_SNDTICK  8   /* ...audio, split: the two MOS scheduler ticks               */
#define PROBE_ISR_PAULA    9   /* ...and program_paula, which BUSY-WAITS on a DMA restart    */
#ifdef REVS_ISRSPLIT
void probe_isr_split(int slot);
extern volatile unsigned long g_isrSplitTicks[PROBE_ISR_SLOTS];
extern volatile unsigned long g_isrSplitCount[PROBE_ISR_SLOTS];
#define PROBE_ISR_SPLIT(s) probe_isr_split(s)
#else
#define PROBE_ISR_SPLIT(s) ((void)0)
#endif
#define PROBE_IRQ_BEGIN() probe_irq_begin()
#define PROBE_IRQ_END(s)  probe_irq_end(s)
#define PROBE_IRQ_NULL()  probe_irq_null()

/* Number of phases the table below can hold — one per top-level call in $1701-$1763,
   plus id 0, plus slack.  ⚠ 40-43 are GEOSPLIT's sub-phases of build_track_geometry, 44-49
   ROADSPLIT's of draw_road and 50-58 DECODESPLIT's of RevsScreen::prepareFrame() (see the bottom of
   this file), so the table must be sized past them. */
#define PROBE_PHASES 64

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
 *   27  PREPARE RevsScreen::prepareFrame() — the COPPER's band plan plus the painters that own
 *                the dashboard rows.  It was `DECODE`, the BBC frame buffer -> bitplanes pass, and
 *                that conversion now runs only on a frame where some line has no painter (i.e. the
 *                cold ones).  Still pure port overhead
 *               and the thing docs/span-render-plan.md is about.
 *   28  SPIN    waiting for the next real vblank after the paint.  Should be ~0 at 1 FPS; anything
 *               large here means the loop is waiting for the display rather than the reverse.
 *
 * Phase 25 keeps whatever is left (the call itself), so the old row is the sum of 25..28. */
/* 29 splits the 50 Hz body itself: `tick_wheel_spin`, the band-4 arm, which is the only part of the
 * IRQ1V band cycle that simulates and DRAWS (display lines 120-143).  Everything else in the
 * cycle just reloads the timer and rewrites the palette.  Bracketed at its own JSR ($4EF5) with
 * phase 26 reopened immediately after, so the split is exact and the enclosing drain keeps the
 * remainder — see tools/transpile.py PRE_INSN_HOOKS. */
#define PROBE_PHASE_BODYARM 29

/* ⭐ `make VIEWSPLIT=1` — phase 24 ($7BE2) split into its two halves, because "102 ms" does not
 * say whether to attack the 2093-unit chain or the ~77 lines of driver around it.
 *   30  the unit loop itself, bracketed per scan line inside src/gen/revs_native.c
 *   31  an EMPTY bracket at the same rate — the instrument's own cost, which must be read
 *       before 30 is believed (the band-arm average taught this the hard way,
 *       docs/perf-method.md).  Phase 24 keeps the remainder: the drivers.
 * ⚠ A measurement build only: two bracket transitions per scan line. */
#define PROBE_PHASE_VIEWUNITS 30
#define PROBE_PHASE_VIEWCTL   31
int probe_phase_current(void);

/* The epoch-corrected beam clock (probe.cpp), for timing a single event that may straddle a
   frame wrap.  Used by the crash-hold field-cost probe (REVS_CRASHPROBE + PROBES). */
unsigned long probe_beam_tick(void);

/* ⭐⭐ 32 — THE MAIN LOOP'S TAIL, WHICH PHASE 24 WAS SILENTLY CARRYING.  Phase 24 opens at
 * `JSR $7BE2` ($1748) and the next bracket is the paint hook at $1701, so everything the body
 * does AFTER the sweep was charged to the sweep: $174B-$1758, and then — whenever $62F6 is zero,
 * which is the usual path — the whole $178F tail, `JSR $0EE5`, `JSR $0E74`, `JSR $513A`.  (The
 * $1760 spin re-opens phase 0, so only the OTHER path leaked, which is why it was invisible.)
 * This bracket costs ONE transition a frame against an 82 ms row, so unlike VIEWSPLIT it is
 * quotable: with it, phase 24 IS view_paint_lines and nothing else. */
#define PROBE_PHASE_VIEWTAIL 32

/* ⭐⭐ 33/34 — THE VIEW SWEEP BY ITS THREE PAINTING PHASES, at TWO transitions a frame.
 * $7BE2's three phases paint progressively less of the line and phases 2 and 3 carry the whole
 * per-line DRIVER (the planted stop, the composed boundary cell, the second chain entry), so
 * splitting there is the split "unit loop vs drivers" was reaching for — and unlike VIEWSPLIT's
 * per-line brackets it costs nothing measurable, because each of the three runs ONCE per sweep:
 *   24  phase 1, $7BE2 — 36 full-width lines, 1440 units, no driver at all
 *   33  phase 2, $7D13 — 16 lines, two chain runs each, one planted stop
 *   34  phase 3, $7F18 — 25 lines, two planted stops and two computed entries
 * Each bracket includes its own chain runs, so these are subtree costs (as every phase row is). */
#define PROBE_PHASE_VIEWP2 33
#define PROBE_PHASE_VIEWP3 34

/* ⭐⭐ ...AND THE WORK EACH OF THE THREE DID, COUNTED, because a millisecond figure alone cannot
 * say whether phase 3 is expensive per LINE or simply painting more units.  Counted O(1) per
 * chain run out of the pointer difference — never per unit, which would perturb the loop the
 * table is about.  `units` is cells painted, `runs` chain-run segments, `lines` driver lines. */
/* ⭐ `make VIEWP3=1` — phase 3's per-line DRIVER split into its four pieces, because 1.7 ms a line
 * for eleven painted cells is not explained by anything in the C and "the drivers" is not a thing
 * to optimise.  25 lines a frame, five transitions each:
 *   35  chain A's planted stop (view_move_stop -> two view_plant)   36  chain A's entry + boundary cell
 *   37  chain B's planted stop                                      38  chain B's entry + boundary cell
 *   31  an EMPTY bracket at the same rate — THE CONTROL, and it is read first.  Phase 34 keeps the
 *       remainder (the scan-line step and the loop).
 * ⚠ A measurement build only, and its own rows are only quotable against phase 31. */
#define PROBE_PHASE_P3_STOPA 35
#define PROBE_PHASE_P3_CHAINA 36
#define PROBE_PHASE_P3_STOPB 37
#define PROBE_PHASE_P3_CHAINB 38

/* ⭐ `make CARSPLIT=1` — phase 17 (move_and_draw_cars) split by job, because in the RACE PROPER it
 * is the biggest row in the frame (PROBERACE) and "the other cars" is not a thing to optimise:
 *   40  drive_other_cars (the per-car AI)     41  check_car_pair (the overtaking pass)
 *   42  the six stage_nearby_car calls, with reject_all_object_slots / find_player_neighbours
 *   43  draw_car_field (the object plotter)   44  the car ahead's stage_nearby_car
 * Phase 17 keeps the entry (the practice test and the car-ahead un-reject). */
#define PROBE_PHASE_CAR_DRIVE 40
#define PROBE_PHASE_CAR_PAIR  41
#define PROBE_PHASE_CAR_STAGE 42
#define PROBE_PHASE_CAR_DRAW  43
#define PROBE_PHASE_CAR_AHEAD 44

/* ⭐⭐ `make VIEWCAL=N` — THE CALIBRATION, and it is the only way to read any of the rows above as
 * cycles.  probe_burn_cycles() runs EXACTLY 1000 x (`nop` 4 + `dbra` 10) = 14 000 cycles = 1975 µs
 * at 7.09 MHz, inside its own bracket at the same rate as the rest, and the row must scale LINEARLY
 * in N or the burn is not what is being measured.
 * ⭐⭐ IT HAS BEEN RUN, AND THE BRACKETS ARE HONEST — 1.048x raw from the two-point slope, 1.005x
 * once the VERTB ISR fires landing inside the open bracket are accounted for.  This retracts the
 * note that used to stand here ("either the beam brackets inflate or a 68000 instruction here costs
 * far more than an instruction count suggests"): the first disjunct is refuted, so the ~6x is in
 * THE CODE, and its mechanism is still open.  ⚠ It is not a fast-vs-chip-RAM effect — that
 * distinction buys nothing on a 68000 (docs/perf-method.md §The four view probes, which carries the
 * fit, the four differentials and why one row read in isolation says 1.64x).
 * ⚠ READ THE SLOPE, NOT ONE ROW: PROBE_PHASE *switches* phase rather than nesting, and the loop-top
 * re-arm is VIEWP3_PHASE(), compiled out unless REVS_VIEWP3 is defined — so in a VIEWCAL build
 * nothing re-arms phase 34 and phase 39 holds the burn PLUS one whole phase-3 line body.  That
 * contamination is a CONSTANT while the burn scales with N, which is exactly what the two-point fit
 * removes. */
#define PROBE_PHASE_CAL 39
void probe_burn_cycles(void);

extern volatile unsigned long g_viewUnits[3], g_viewRuns[3], g_viewLines[3];
extern int g_viewPhaseIdx;
#define PROBE_VIEW_PHASE(i)   (g_viewPhaseIdx = (i))
#define PROBE_VIEW_UNITS(n)   (g_viewUnits[g_viewPhaseIdx] += (unsigned long)(n))
#define PROBE_VIEW_RUN(n)     do { PROBE_VIEW_UNITS(n); g_viewRuns[g_viewPhaseIdx]++; } while (0)
#define PROBE_VIEW_LINE()     (g_viewLines[g_viewPhaseIdx]++)

#define PROBE_PHASE_DRAIN  26
#define PROBE_PHASE_PREPARE 27
#define PROBE_PHASE_SPIN   28
/* ⭐⭐ THE CRASH/SESSION RESET, excluded from the frame like phase 0.  The crash hold ($1760 on
   field_countdown) can only end inside a drained 50 Hz tick — tick_wheel_spin is what moves the
   counter — so it always exited with phase 26 OPEN, and everything after it (race_resume_point and
   the whole session reset: reset_driving_variables, build_player_car, the track rebuilds) was
   billed to "the drain" until the next frame's first bracket.  That was ~5.7 of the drain's
   6.9 ms/frame.  Single-stepped, a band cycle is 119 instructions.  Id 63 sits above everything. */
#define PROBE_PHASE_RESET  63
/* ⭐ `make DUALPF=1`: the cockpit layer's own bracket, carved out of the decode's.  Its own row
   because ph27 alone cannot say whether a move is the terrain conversion or the second layer —
   the same reason the rectangle painter got one (§12c). */
#define PROBE_PHASE_COCKPIT 30

/* ⭐ Beam ticks in one PAL display frame, in the units beamTick() composes
   (line * 256 + hpos, 313 lines).  The VERTB ISR adds this to g_beamEpoch once per
   frame, which is what makes the tick monotonic ACROSS frames.
   ⚠ It must be an ADDITION, never `frames * PROBE_BEAM_TICKS_PER_FRAME`: a 32-bit
   multiply emits __mulsi3 and the 68000 has none (amiga/Makefile muldiv-audit). */
#define PROBE_BEAM_TICKS_PER_FRAME 80128UL   /* 313 * 256 */

/* Accumulated whole display frames, in beam ticks.  Bumped by the VERTB ISR only. */
extern volatile unsigned long g_beamEpoch;

/* ⭐⭐ Non-zero once `make PROBEFIELDS=N`'s window has closed — the proof the arms of an A/B
   really covered the same EMULATED window and not just the same wall-clock one.  The value is
   the BEAM TICK at which it closed, so it also serves as the window's exact length wherever a
   still-climbing g_beamEpoch would be wrong.  Always defined, so every phase script may read it.
   ⚠⚠ The four shadows are the PARTIAL-FREEZE TRAP's fix: the freeze stops the phase
   accumulators but not the counters bumped elsewhere, so a script must read these — not the live
   g_bodyTicks / g_viewUnits / g_viewRuns / g_viewLines — whenever g_probeFrozen is set, or it
   divides a live numerator by a frozen denominator and prints a plausible lie.  probe.cpp has
   the reasoning and the two numbers that caught it. */
extern volatile unsigned long g_probeFrozen;
extern volatile unsigned long g_probeFrozenBody;
extern volatile unsigned long g_probeFrozenUnits[3], g_probeFrozenRuns[3], g_probeFrozenLines[3];

/* Call once per display frame from the VERTB ISR, before any game work. */
#define PROBE_VBI() (g_beamEpoch += PROBE_BEAM_TICKS_PER_FRAME)

#ifdef __cplusplus
}
#endif

#define PROBE_PHASE(id) do { REVS_CANARY(id); probe_phase(id); } while (0)

/* ⭐ The BODYSPLIT brackets (§BODYSPLIT).  A no-op unless `make BODYSPLIT=1`, so the shipping
   band cycle carries no transitions and the host build is untouched. */
#ifdef REVS_BODY_SPLIT
#define BODY_PHASE(id) PROBE_PHASE(id)
#else
#define BODY_PHASE(id) ((void)0)
#endif

#else
#define PROBE_PHASE(id) REVS_CANARY(id)
#define PROBE_VBI()     ((void)0)
#define PROBE_ISR_BEGIN() ((void)0)
#define PROBE_ISR_END()   ((void)0)
#define PROBE_ISR_SPLIT(s) ((void)0)
#define PROBE_IRQ_BEGIN() ((void)0)
#define PROBE_IRQ_END(s)  ((void)(s))
#define PROBE_IRQ_NULL()  ((void)0)
/* ⚠ The phase IDs are plain numbers and must exist in EVERY build: `make SHAPE=1` without PROBES
   passes them to the shape probe (src/platform/shape.h §WHO ACTUALLY DRAWS), and inside the
   PROBE_PHASE macro they were only ever unevaluated macro arguments — so a non-PROBES build
   compiled for a year without needing them and then failed to compile the moment something else
   used one.  Kept in sync with the definitions above by hand; `make gen` checks FRAMEWAIT. */
#define PROBE_PHASE_FRAMEWAIT 25
#define PROBE_PHASE_VIEWUNITS 30
#define PROBE_PHASE_VIEWCTL   31
#define PROBE_PHASE_VIEWTAIL  32
#define PROBE_PHASE_VIEWP2    33
#define PROBE_PHASE_VIEWP3    34
#define PROBE_VIEW_PHASE(i)   ((void)0)
#define PROBE_VIEW_UNITS(n)   ((void)0)
#define PROBE_VIEW_RUN(n)     ((void)0)
#define PROBE_VIEW_LINE()     ((void)0)
#define PROBE_PHASE_DRAIN     26
#define PROBE_PHASE_PREPARE    27
#define PROBE_PHASE_RESET     63
#define PROBE_PHASE_COCKPIT   30
#define PROBE_PHASE_SPIN      28
#define PROBE_PHASE_BODYARM   29
#define BODY_PHASE_GATE       59
#define BODY_PHASE_IRQ        60
#define BODY_PHASE_NULL       61
#define BODY_PHASE_BEGIN      62
#define BODY_PHASE(id)        ((void)0)
#endif

/* ===========================================================================
 * ⭐⭐ `make GEOSPLIT=1 PROBES=1` — WHY IS build_track_geometry (phase 5) ~16% OF THE FRAME?
 * ---------------------------------------------------------------------------
 * Phase 5's whole call tree is native C already, so the ordinary levers are spent
 * (docs/perf-method.md) and the standing conclusion is "fewer POINTS / fewer ACCESSES in the
 * producers" — an ALGORITHMIC question a single phase-5 row cannot answer.  This instrument
 * decomposes that row two ways in one run:
 *
 *   TIME  — four beam brackets carve phase 5 into its sub-phases (ids 40-43), so the ms land
 *           where the work is: the near-point reuse pass vs the two distance walks vs the
 *           horizon tail.  FOUR transitions a frame, so — like the VIEWTAIL/VIEWP2/P3 splits
 *           (probe.h §32/§33) and unlike the per-line VIEWSPLIT — it costs nothing measurable
 *           and phase 5 keeps only the driver remainder.
 *   COUNTS — how many edge points each side visits, and how many times the two coordinate
 *           transforms (bearing_to_section, project_point), the engine's divide (div16by8) and
 *           the distance approximation (point_distance_hypot) run per frame.  This is the "why":
 *           the pass is (points) x (per-point transform cost), and each point costs up to three
 *           div16by8 (two in the bearing, one in the projection) of eight restoring steps each.
 *           Platform-independent — reads nothing but its own counters — so `make GEOSPLIT=1` on
 *           the HOST counts too (no beam there; the TIME split is Amiga-only).
 *
 * ⚠ A measurement build only.  Ids 40-43 sit above every real phase (PROBE_PHASES was bumped to
 * hold them); a plain phase4_prof.gdb loop that stops at 40 will under-count the accounted% on a
 * GEOSPLIT build — read it with amiga/geosplit.gdb, which sums through 43. */
#define GEO_PHASE_START 40   /* road_edge_start — the near-point reuse pass          */
#define GEO_PHASE_WALK0 41   /* road_side_walk(0) — one distance walk                */
#define GEO_PHASE_WALK1 42   /* road_side_walk(0x80) — the other                     */
#define GEO_PHASE_TAIL  43   /* the horizon record (index/extent/half-width)         */

#ifdef REVS_GEOSPLIT
#ifdef __cplusplus
extern "C" {
#endif
extern volatile unsigned long g_geoFrames;      /* build_track_geometry calls (= main loop) */
extern volatile unsigned long g_geoPoints[2];    /* edge points visited, per road side       */
extern volatile unsigned long g_geoSubdiv;       /* road_edge_walk_subdivide calls           */
extern volatile unsigned long g_geoBearing;      /* bearing_to_section_core calls            */
extern volatile unsigned long g_geoProject;      /* project_point_core calls                 */
extern volatile unsigned long g_geoDiv;          /* div16by8_core calls — the engine's divide */
extern volatile unsigned long g_geoHypot;        /* point_distance_hypot calls               */
extern int g_geoSide;                            /* which side the current walk is emitting  */
#ifdef __cplusplus
}
#endif
#define GEO_COUNT(c)     (++(c))
#define GEO_SIDE_SET(s)  (g_geoSide = (s))
#define GEO_POINT()      (++g_geoPoints[g_geoSide & 1])
#ifdef REVS_PROBE
#define GEO_PHASE(id)    probe_phase(id)
#else
#define GEO_PHASE(id)    ((void)0)
#endif
#else
#define GEO_COUNT(c)     ((void)0)
#define GEO_SIDE_SET(s)  ((void)0)
#define GEO_POINT()      ((void)0)
#define GEO_PHASE(id)    ((void)0)
#endif /* REVS_GEOSPLIT */

/* ===========================================================================
 * ⭐⭐ `make ROADSPLIT=1 PROBES=1` — WHY IS draw_road (phase 11) ~16% OF THE FRAME?
 * ---------------------------------------------------------------------------
 * The exact sibling of GEOSPLIT, for the view pipeline's SECOND producer.  draw_road's whole
 * call tree is native C already, so — as with build_track_geometry — the row is not "another
 * twin" and a single phase-11 figure cannot say WHERE the ~16% is.  Two decompositions in one
 * run:
 *
 *   TIME  — three beam brackets carve phase 11 into its three stage types (ids 44-46), summed
 *           across both road sides, so the ms land on the stage that owns them: the per-line
 *           map (fill_line_attr) vs the span rasteriser (draw_surface_spans -> interp_edge ->
 *           the DDA arms -> road_span_plot) vs the surface-class stamp (mark_line_surfaces).
 *           SIX transitions a frame (three stages x two sides), so — like GEOSPLIT — it costs
 *           nothing measurable and phase 11 keeps only the clamp/driver remainder.
 *   COUNTS — the leaf tallies that explain the shares.  The rasteriser is (spans) x (scan
 *           lines per span) x (columns per line), and the column is where road_span_plot does
 *           its three bus accesses; the map is (lines filled); the stamp is (points).  This is
 *           the "why", the way GEOSPLIT's point/divide counts were.  Platform-independent, so
 *           `make ROADSPLIT=1` on the HOST counts too (no beam there; TIME is Amiga-only).
 *
 * ⚠ A measurement build only.  Ids 44-46 sit above every real phase (PROBE_PHASES holds them);
 * read the TIME split with amiga/roadsplit.gdb, which sums 11 + 44 + 45 + 46. */
#define ROAD_PHASE_FILL  44   /* fill_line_attr    — line -> edge point map (both sides) */
#define ROAD_PHASE_SPANS 45   /* draw_surface_spans — the span rasteriser (all four passes) */
#define ROAD_PHASE_MARK  46   /* mark_line_surfaces — line -> surface class (both sides)  */
/* ⭐⭐ 47 CARVES THE SPAN RASTERISER IN TWO, and it exists to settle one contradiction: the
   per-frame leaf counts (43 spans, 49 DDA scan lines, 60 plotted columns) cannot fill 37 ms in
   the WALK, yet a pre-flattening bracket read ~24 ms there and only ~13 ms in setup.  With 47
   in the table, phase 45 is interp_edge_core's per-span SETUP plus the driver, and 47 is the
   four `span_walk` specialisations — 45 + 47 is the old row.  docs/perf-method.md §the span
   kernel.  86 transitions a frame, so the instrument is free at this rate. */
#define ROAD_PHASE_WALK  47   /* span_walk — the four inlined DDA arms and their plotters   */
/* ⭐⭐ 48/49 CARVE THE WALK AGAIN, and 49 IS THE CONTROL — the same shape as PROBE_PHASE_P3's
   empty phase 31.  Three transitions per plotted column: 49 opens (closing the walk), 48 opens
   (closing 49, whose bracket contains NOTHING, so phase 49's ticks ARE the probe's own cost at
   this rate), and 47 reopens (closing 48 = one span_plot_core).  Read 49 FIRST: 48 is only
   quotable as 48 − 49, and if 49 is comparable to 48 the instrument is the measurement.
   ~59 columns a frame, so 177 transitions — the same order as the view drivers' splits. */
#define ROAD_PHASE_PLOT  48   /* span_plot_core — one column of one span merged into a cell  */
#define ROAD_PHASE_NULL  49   /* THE CONTROL: an empty bracket at exactly the plot rate      */

#ifdef REVS_ROADSPLIT
#ifdef __cplusplus
extern "C" {
#endif
extern volatile unsigned long g_roadFrames;     /* draw_road calls (= main loop)            */
extern volatile unsigned long g_roadSpans;       /* interp_edge calls — spans handed to raster */
extern volatile unsigned long g_roadSpanLines;   /* span_walk outer iterations — DDA scan lines */
extern volatile unsigned long g_roadCols;        /* road_span_plot(_2) calls — the leaf column  */
extern volatile unsigned long g_roadColSteps;    /* span_walk's i-loop iterations — the DDA steps */
extern volatile unsigned long g_roadFillLines;   /* fill_line_attr inner-loop line writes       */
extern volatile unsigned long g_roadMarkPts;     /* mark_line_surfaces points stamped           */
#ifdef __cplusplus
}
#endif
#define ROAD_COUNT(c)    (++(c))
#ifdef REVS_PROBE
#define ROAD_PHASE(id)   do { REVS_CANARY(id); probe_phase(id); } while (0)
#else
#define ROAD_PHASE(id)   REVS_CANARY(id)
#endif
#else
#define ROAD_COUNT(c)    ((void)0)
/* ⭐ The canary still rides these brackets in a non-ROADSPLIT build: they are the only sub-stage
   boundaries draw_road has, and `make INK_WATCH=1` needs them to attribute a stray write inside
   the road pass without also turning on the ROADSPLIT counters. */
#define ROAD_PHASE(id)   REVS_CANARY(id)
#endif /* REVS_ROADSPLIT */

/* ===========================================================================
 * ⭐⭐ `make DECODESPLIT=1 PROBES=1` — WHAT IS THE HALF OF phase 27 THAT CONVERTS NOTHING?
 * ---------------------------------------------------------------------------
 * `RevsScreen::prepareFrame()` is the port's own cost with no BBC counterpart, and the 208-row
 * ledger (docs/span-render-plan.md §11) sizes it by ROWS: claiming a block of display lines as
 * `m_lineMode = 0` deletes that block's conversion, and `make VIEWCARVE=<lo>-<hi>` prices any
 * block on one scale.  Summed over the whole picture that accounts for only HALF the row:
 * carving all 208 lines still leaves ~12 ms, and stubbing decode() outright leaves 0.13 — so
 * ~12 ms is real work inside decode() that converts nothing and no amount of ownership can
 * reach.  This carves it:
 *
 *   50/53/54  the PROLOGUE, carved three ways — snapshotBands(), buildLineModes() and the
 *       ownership/carve loops.  Everything before a source byte is read, and per FRAME rather
 *       than per cell, so a large row here is a fixed tax the row ledger cannot see.
 *       ⭐ 53 is the one that SURVIVES the end state: `m_plan` is the COPPER's palette
 *       schedule, so snapshotBands + buildLineModes still have to run every frame even when
 *       the painter owns all 208 rows and decode() is never called.
 *   51  convertRace() — the conversion proper (the per-cell dirty scan + the expands).
 *   52  THE CONTROL, and it must be read first: its bracket contains NOTHING, so its ticks ARE
 *       one transition's cost (the same shape as ROADSPLIT's phase 49).  SIX brackets a frame
 *       here, so subtract one control's worth from every row before quoting it.
 *
 * Phase 27 keeps the remainder (the MODE 7 test, the bitmap lookup, the counters).
 *
 * ⭐⭐ WHAT IT ANSWERED, control-corrected, and the split CLOSES (24.29 against the base build's
 * 24.20 ms): control 0.10 / snapshotBands 0.77 / buildLineModes 1.64 / own+carve 1.75 /
 * convertRace 17.09 / remainder 3.04.  ⇒ THE "12 ms FLOOR" WAS MOSTLY CODE SHAPE, NOT A FLOOR:
 * ownership can only delete convertRace's per-ROW conversion, and everything else here goes
 * with the CALL — which is why the end state is two steps (own all 208 rows, THEN stop calling
 * decode()), and why only snapshotBands + buildLineModes survive it.
 * ⚠ A measurement build only; read it with amiga/decodesplit.gdb, which sums 27 + 50..54. */
#define DEC_PHASE_PRE     50   /* snapshotBands — the band record, 95 volatile reads         */
#define DEC_PHASE_CONVERT 51   /* convertRace — the scan and the expands                     */
#define DEC_PHASE_NULL    52   /* an EMPTY bracket: the transition cost itself               */
#define DEC_PHASE_MODES   53   /* buildLineModes — us -> display lines, 208 mode bytes       */
#define DEC_PHASE_OWN     54   /* the ownership/carve loops over the 208 display lines       */
#define DEC_PHASE_RECTS   55   /* §12c's dynamic-rectangle re-expand over the owned band     */
/* ⭐ ...AND THE THREE THAT CARVE THE `phase 27 remainder` ROW, which was 3.14 ms and the biggest
   unattributed block left in the decode once the conversion stopped being called.  Everything in
   decode() is inside SOME bracket now, so 27 keeps only the render()-level code around the call
   and the switches' own cost — i.e. a 27 that does NOT fall to ~0 is the instrument, not code. */
#define DEC_PHASE_ENTRY   56   /* decode() entry: the teletext test and the bitmap fetch     */
#define DEC_PHASE_POST    57   /* after the conversion: the cockpit counters and the oracles */
#define DEC_PHASE_TAIL    58   /* after the rectangles: DIRTYCHECK / FILLWATCH and m_ready   */

/* ===========================================================================
 * ⭐⭐ `make BODYSPLIT=1 PROBES=1` — WHAT IS THE 50 Hz DRAIN (phases 26+29) SPENDING 12.5 ms ON?
 * ---------------------------------------------------------------------------
 * The third-largest block in the frame and the only one that has never had an instrument.  Its
 * shape makes a single row actively misleading: the drain runs `ceil(frame / 20 ms)` band cycles
 * per painted frame — 8.78 of them at a 154 ms frame — so `ph26` is a PRODUCT of the frame time
 * and the per-cycle cost, and neither factor is visible in it.  ⚠ That also means a win here is
 * NOT proportional to the row: halving the frame halves the tick count too, so the row falls
 * whether or not the cycle got cheaper.  ⭐ Quote the PER-CYCLE µs from this split, never ph26.
 *
 * What one cycle does, and the split follows it exactly (src/platform/bbc_hw.cpp):
 *   59  GATE   band_inputs_unchanged() — the 43-byte digest of the band record's inputs, copied
 *              into a local and compared.  Pure port machinery: the BBC re-ran the cycle.
 *   60  IRQ    the bounded `fireIrq1v()` loop — the game's OWN IRQ1V band chain, i.e. the only
 *              part of the cycle the 6502 also paid.  Runs only when the gate says "changed".
 *   61  NULL   THE CONTROL, read first: an empty bracket at the same rate as the gate.
 *   62  BEGIN  bbc_begin_band_cycle() — zeroes the record and advances the 1 MHz field clock.
 *              Two stores, so it is really a second control; if it is not ~= 61 the split lies.
 * Phase 26 keeps the remainder (drainTicks' loop, the A/X/Y save, the IRQ1V claim test) and
 * phase 29 stays `tick_wheel_spin`, the one part of the cycle that SIMULATES and DRAWS.
 *
 * ⚠ An in-code comment on Revs::runBandCycle has claimed "96% of this row is machinery, 233 us a
 * field is game work" for months with no doc and no run behind it; this instrument exists to
 * replace it with a measurement.  Read it with amiga/bodysplit.gdb.
 * ⚠ A measurement build only: up to four extra transitions per band cycle. */
#define BODY_PHASE_GATE  59
#define BODY_PHASE_IRQ   60
#define BODY_PHASE_NULL  61
#define BODY_PHASE_BEGIN 62
