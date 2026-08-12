/* Revs — the Amiga application/scene.  See Revs.h; this is the bring-up skeleton. */
#include "Revs.h"
#include "PlatformAmiga.h"
#include "framework/AmigaHardware.h"
#include "framework/CopperList.h"

extern "C" volatile uint16_t g_vbiCount;
extern "C" volatile unsigned long g_fpsFrames;

// A minimal copper list: set the background colour, then wait forever.
//   COLOR00 = $DFF180.  Colour writes take effect immediately, so a colour-only poke is
//   safe on a live list (pointer writes are NOT — those are VBI-only).
static uint32_t s_copperData[] = {
    copperMove(0x180, 0x0123),      // COLOR00 — a recognisable non-black, so a booted
                                    // build is visibly distinguishable from a hang
    copperWait(255, 255),           // park the copper
    0xFFFFFFFEu,                    // end of list
};

void Revs::initialize()
{
    // ⚠ Install the copper list while the copper is halted (display DMA is off at this
    // point — PlatformAmiga::run() guarantees it).  Installing into a running copper is
    // how the Atari port lost its one-time register setup to stray OS-copper frames.
    copper = new CopperList(s_copperData, sizeof(s_copperData) / sizeof(s_copperData[0]));
    AmigaHardware::setCopperList(copper->data());
}

void Revs::shutdown()
{
    delete copper;
    copper = 0;
}

void Revs::render()
{
#ifdef REVS_FPSCOUNT
    // ⭐ The framerate numerator: exactly one increment per PAINTED frame, and nothing
    // else.  Keep it that way — the moment this build reads a chip register or multiplies,
    // it stops being the honest baseline.  (docs/perf-method.md)
    g_fpsFrames++;
#endif
    // TODO(phase: Amiga backend): mirror the 6502 screen RAM into bitplanes here.
    // Keep the work proportional to what CHANGED — a full-screen re-decode per frame is
    // affordable on a 7 MHz 68000 exactly once, and this is not the place to spend it.
}

void Revs::vbi()
{
    // TODO(phase: Amiga backend): the game's own 50 Hz interrupt body goes here, and so
    // does every copper bitplane POINTER swap.
}

void Revs::run()
{
    // TODO(phase: C transliteration): call the genuine entry chain here (the transpiled
    // 6502 entry point), whose spin-waits drive platform_render_frame().  Until it exists,
    // pump frames so the takeover, the VERTB handler and the copper list can all be
    // verified on the real machine — including headlessly (amiga/diag_run.sh).
    while (!platform->quit) {
        platform->renderFrame();
        platform->pollEvents();
    }
}
