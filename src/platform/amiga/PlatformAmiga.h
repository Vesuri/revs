#pragma once
/* PlatformAmiga — the Amiga concrete implementation of the abstract Platform.
 *
 * ⚠ SCAFFOLD.  This is the day-one bring-up skeleton, not the finished backend: it
 * takes the machine over, installs the real VERTB interrupt, runs the Revs scene and
 * restores everything on exit.  It renders nothing yet.  Its purpose is postmortem
 * §4.1 — "get something running end-to-end on the real machine as early as possible"
 * — so that every later measurement is made on the target rather than argued about.
 *
 * Owns everything Amiga-hardware-specific below the Revs scene: the display takeover
 * (LoadView/DMACON/display window), the real INTB_VERTB interrupt, the keyboard, the
 * audio backend, and the frame pump.  It implements the Platform interface the
 * C-compiled 6502 transliteration reaches through platform_cbridge.cpp.
 *
 * Design rules inherited from the Atari port (docs/amiga-lessons.md) — do not
 * rediscover these:
 *   - Takeover, not OS-friendly: we need per-scanline copper rewrites.
 *   - Replace exec's VERTB IntVector wholesale rather than AddIntServer'ing onto its
 *     chain; the chain walk cost ~3.9% of all wall clock on the Atari port.  The
 *     handler must then clear INTREQ itself or level 3 re-triggers forever.
 *   - Copper bitplane POINTER swaps only in the VBI, never mid-frame.
 */
#include "platform.h"           // the abstract base (src/platform, on the build -I path)
#include "platform_c.h"         // the extern "C" bridge decls
#include "autorun.h"            // the scripted keyboard for unattended runs
#include "RevsInput.h"          // real mouse + keyboard (Phase 5)
#include "framework/Util.h"     // uint8_t, uint16_t, uint32_t

// main.cpp instantiates PlatformClass(image) without knowing the concrete type; the
// build define selects which header is included.
#define PlatformClass PlatformAmiga

class Revs;

class PlatformAmiga : public Platform {
public:
    explicit PlatformAmiga(const char* imagePath);   // imagePath ignored (image is embedded)
    virtual ~PlatformAmiga();

    // run(): the whole Amiga lifecycle — take over the display, install the VERTB
    // handler, load the boot image, run the Revs scene, restore the system.
    virtual void run() override;

    // hwRead/hwWrite are NOT overridden either — the two VIA flag bits Revs blocks on are
    // the machine, and src/platform/bbc_hw.cpp models them for both backends.  Phase 5
    // adds an override here that calls the base and then routes $FE20/$FE21 to the copper.
    virtual void    renderFrame()                       override;  // present + wait for next VBI
    virtual void    pollEvents()                        override;  // poll quit (CTRL + left mouse)
    virtual void    tickVBI()                           override;  // no-op: the ISR owns the clock
    // mosCall is deliberately NOT overridden — src/platform/mos.cpp owns the whole MOS
    // surface for both backends.  What this backend answers is the input, below.
    virtual bool     keyDown(uint8_t x)                 override;
    virtual uint8_t  rdch()                             override;  // ⭐ typed text (the wing/name fields)
    virtual void     flushKeyboard()                    override;
    virtual uint8_t  adcButtons()                       override;
    virtual uint16_t adcAxis(uint8_t channel)           override;
    virtual int     loadImage(const char* path)         override;  // no-op: the ctor read the disc
    virtual void    setInterrupt(void (*fn)(void))      override;  // real VBI -> no-op
    virtual int     framesPerSecond()                   override;  // 50 (PAL)

    // ⭐ The 1 MHz clock behind the User VIA's T2 counter ($FE68, bbc_hw.cpp) — the game's
    // only entropy source.  On this machine the cheap equivalent of a free-running timer is
    // the BEAM: VHPOSR advances every 280 ns and is as decorrelated from game code as T2 is
    // from 6502 code, for the same reason (it is a clock, not a PRNG).
    // ⚠ Under REVS_FIXED_RNG the base class's field-counted fallback is used instead, so a
    // perf run stays pinned — that flag exists precisely so two builds drive the same
    // simulation, and a beam-derived value would make the trajectory depend on frame timing.
    virtual uint32_t hwMicros() override;

    // ⭐⭐ The simulation clock (Platform::simStepTenths): steps are owed against REAL fields,
    // counted by the VERTB ISR.  `make SIMLEGACY=1` restores one step per painted frame.
    virtual unsigned simStepTenths() override;
    virtual unsigned simFields() override;

    // Called from the VERTB ISR: accumulate the mouse counter (see RevsInput::sampleMouse).
    void sampleMouse() { input.sampleMouse(); }

private:
    // ⭐⭐ THE CIRCUIT MENU (src/platform/trackmenu.h), driven here because this is the only place
    // that has both the real keyboard and the frame pump.  Returns false if the player quit.
    //
    // ⚠ IT MUST RUN BEFORE engine_main() AND AFTER input.initialize(), which is a narrower window
    // than it looks: revs_track_boot() installs a circuit into $5300-$5A25 before the scene even
    // exists, and the engine reads that window during init, so the menu's own install has to land
    // in between.  See the call site in run() for the ordering argument.
    bool runTrackMenu();

public:

protected:
    // Backs the System VIA vsync flag ($FE4D bit 1) hw_init's alignment spin blocks on:
    // here it is a REAL frame boundary, taken from the VERTB ISR's own counter.
    virtual bool    vsyncElapsed()                      override;

private:
    // ⭐ Scripted input (src/platform/autorun.h).  Compiled in under FPSCOUNT/PROBES,
    // where without it a headless run never leaves the front-end menus and the framerate
    // harness measures a menu spin — and under STRAIGHT_TO_RACE, where it answers the one
    // menu question a practice session has and then gets out of the player's way.
    // ⚠ REVS_COMPETITION MUST BE IN THIS LIST, and it was missing until 2026-08-15.  The
    // competition script is selected in autorun.cpp by REVS_COMPETITION alone, so a
    // `make COMPETITION=1` build COMPILED the script and then never instantiated the object
    // that runs it — the target sat in the front end for the whole run with $5F3B = $9D,
    // which reads as "the script's timing is wrong" and was documented as such.  Any new
    // AUTORUN_* build flag has to be added here and at the call site in the .cpp.
#ifdef REVS_AUTORUN_BUILD   // autorun.h — one predicate, not four flags per site
    AutoRun autoRun;
#endif
    // Real input: the CIA-A keyboard and the mouse, mapped onto the game's own two input
    // paths (RevsInput.h).  ⚠ Under FPSCOUNT/PROBES the SCRIPT wins for the whole run, so
    // an unattended measurement is bit-identical across builds no matter what the keyboard
    // does; under STRAIGHT_TO_RACE it wins only until autoRun.done().
    RevsInput input;
    uint16_t lastVsyncCount = 0;
    uint16_t lastSimFieldCount = 0;   // simFields' previous g_vbiCount
};
