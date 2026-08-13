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

private:
    // The BBC display, re-hosted: two bitplanes + the copper palette bands.  Held by
    // value so the scene stays trivially constructible (see the note above).
    RevsScreen screen;
};
