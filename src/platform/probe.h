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
   plus id 0, plus slack.  ⚠ 40-43 are GEOSPLIT's sub-phases of build_track_geometry (see the
   bottom of this file), so the table must be sized past them. */
#define PROBE_PHASES 48

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

/* ⭐⭐ `make VIEWCAL=1` — THE CALIBRATION, and it is the only way to read any of the rows above as
 * cycles.  Every µs figure this routine produces is ~6x what its generated code can account for, so
 * either the beam brackets inflate or a 68000 instruction here costs far more than an instruction
 * count suggests.  probe_burn_cycles() runs EXACTLY 1000 x (`nop` 4 + `dbra` 10) = 14 000 cycles =
 * 1975 µs at 7.09 MHz, inside its own bracket at the same rate as the rest: phase 39 divided by its
 * call count IS the conversion, measured in the same run as the thing it calibrates. */
#define PROBE_PHASE_CAL 39
void probe_burn_cycles(void);

extern volatile unsigned long g_viewUnits[3], g_viewRuns[3], g_viewLines[3];
extern int g_viewPhaseIdx;
#define PROBE_VIEW_PHASE(i)   (g_viewPhaseIdx = (i))
#define PROBE_VIEW_UNITS(n)   (g_viewUnits[g_viewPhaseIdx] += (unsigned long)(n))
#define PROBE_VIEW_RUN(n)     do { PROBE_VIEW_UNITS(n); g_viewRuns[g_viewPhaseIdx]++; } while (0)
#define PROBE_VIEW_LINE()     (g_viewLines[g_viewPhaseIdx]++)

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

#define PROBE_PHASE(id) do { REVS_CANARY(id); probe_phase(id); } while (0)

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
#define PROBE_PHASE_DECODE    27
#define PROBE_PHASE_SPIN      28
#define PROBE_PHASE_BODYARM   29
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

#ifdef REVS_ROADSPLIT
#ifdef __cplusplus
extern "C" {
#endif
extern volatile unsigned long g_roadFrames;     /* draw_road calls (= main loop)            */
extern volatile unsigned long g_roadSpans;       /* interp_edge calls — spans handed to raster */
extern volatile unsigned long g_roadSpanLines;   /* span_walk outer iterations — DDA scan lines */
extern volatile unsigned long g_roadCols;        /* road_span_plot(_2) calls — the leaf column  */
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
