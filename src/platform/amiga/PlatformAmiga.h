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

    virtual uint8_t hwRead(uint16_t addr)               override;
    virtual void    hwWrite(uint16_t addr, uint8_t val) override;
    virtual void    renderFrame()                       override;  // present + wait for next VBI
    virtual void    pollEvents()                        override;  // poll quit (left mouse)
    virtual void    tickVBI()                           override;  // no-op: the ISR owns the clock
    virtual void    mosCall(uint16_t entry)             override;  // OSBYTE/OSWORD service
    virtual int     loadImage(const char* path)         override;  // embedded -> copies incbin
    virtual void    setInterrupt(void (*fn)(void))      override;  // real VBI -> no-op
    virtual int     framesPerSecond()                   override;  // 50 (PAL)
};
