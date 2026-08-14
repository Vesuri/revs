#pragma once
/* Revs — the Amiga application/scene class.
 *
 * ⚠ SCAFFOLD.  Today this is the minimum that proves the machine is ours: one copper
 * list, one background colour, a run() loop that pumps frames until the left mouse
 * button quits.  It is deliberately the same shape the finished app will have, so
 * filling it in never means restructuring it.
 *
 * The Atari port's equivalent class (RescueOnFractalus) ended up owning: the per-scene
 * copper lists, the bitplane mirrors of the 6502 screen RAM, the sprite multiplexer, and
 * the dirty-region bookkeeping that keeps per-frame decode work proportional to what
 * actually changed.  Expect the same here, and keep run() as the single place that
 * drives the genuine 6502 entry chain.
 */
#include "framework/Util.h"
#include "RevsScreen.h"

// ⚠ Deliberately TRIVIALLY constructible + destructible (no user ctor/dtor, member
// initialised in-class).  A function-local `static Revs` with a non-trivial ctor pulls in
// __cxa_guard_acquire / atexit, which this -nostdlib freestanding build does not have.
// Bring-up state goes in initialize()/shutdown(), not in a constructor.
class Revs {
public:
    // Build bitmaps/copper lists and install our first copper list (COP1LC = ours).
    // Called with display DMA OFF — see PlatformAmiga::run().
    void initialize();
    void shutdown();

    // The whole game runs inside run(): eventually the genuine transpiled/native entry
    // chain, whose frame-wait spin loops each call platform_render_frame().  Returns when
    // the user quits.
    void run();

    // Called from PlatformAmiga::renderFrame(), main-loop context.
    void render();

    // Called from the VERTB ISR.  ⚠ Capped at one frame's work; all copper bitplane
    // POINTER swaps belong here and nowhere else.
    void vbi();

    // ⭐⭐ THE GAME'S 50 Hz BODY, and WHERE IT IS ALLOWED TO RUN.
    //
    // One call to runBandCycle() is one field of the game's own IRQ1V chain — the five
    // raster bands, the last of which is FUN_52a4, the simulation.  vbi() only COUNTS the
    // fields; drainTicks() runs them, from main-loop context, at points where the engine is
    // not drawing.  The reason is measured, not stylistic (docs/amiga-arch.md):
    //
    //   the body DRAWS.  It writes the frame buffer at $6E00-$70FF — display lines 120-143,
    //   the road just below the horizon — and on this port it ran ~50 times per PAINTED
    //   frame where a BBC runs it once.  So the scene changed under the rasteriser: two
    //   consecutive display lines came out with the road's left edge tens of pixels off
    //   (measured 14 -> 27 -> 13 cells at line 124), leaving grass green where the road
    //   belongs, and the decode read 327 line-instances that no longer matched mem[].
    //
    // Draining at the engine's own frame-wait ($1760, which is where the BBC's main loop
    // sits waiting for exactly this interrupt) keeps the tick rate at 50 Hz of wall clock
    // and makes the drawing atomic with respect to it.
    void drainTicks();

    static void runBandCycle();

private:
    // The BBC display, re-hosted: two bitplanes + the copper palette bands.  Held by
    // value so the scene stays trivially constructible (see the note above).
    RevsScreen screen;
};
