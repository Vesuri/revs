#ifndef PLATFORM_H
#define PLATFORM_H

#if !defined(REVS_PLATFORM_AMIGA)
#include <cstdint>   /* Host build.  The Amiga build has no libstdc++ and gets the
                        integer types from the force-included framework/SASCCompat.h. */
#endif

/* Abstract platform base — one concrete subclass per target (SDL host, Amiga).
   The global singleton pointer is used by the C bridge layer so that the
   C-compiled 6502 transliteration can reach hardware emulation.

   ⚠ KEEP THIS INTERFACE GENERIC.  On the Atari port this class accumulated a
   dozen game-specific "region X changed" notifications (titleChanged,
   compassChanged, lockonChanged, cockpitDirty, …).  They earned their place —
   each one removed a full-screen re-decode — but they belong in the CONCRETE
   backend's own header, not in the portable base.  When Revs needs a dirty-region
   hook, prefer a single `regionDirty(addr, nCells)` over one method per widget. */

class Platform {
public:
    Platform();
    virtual ~Platform();

    /* ------------------------------------------------------------------ */
    /* Lifecycle                                                          */
    /* ------------------------------------------------------------------ */

    /* Run the game.  Each platform sets up whatever it needs (interrupts,
       display, signal handlers) and then drives the genuine entry chain.
       main() constructs the concrete PlatformClass and calls this; it returns
       when the user quits. */
    virtual void run() = 0;

    /* ------------------------------------------------------------------ */
    /* Frame / interrupt                                                  */
    /* ------------------------------------------------------------------ */

    /* Register the 50 Hz vsync handler (the BBC's System VIA CA1 vsync IRQ on
       the host; the real INTB_VERTB server on the Amiga). */
    virtual void setInterrupt(void (*fn)(void)) = 0;

    virtual int  framesPerSecond() = 0;

    /* Present the offscreen buffer. */
    virtual void renderFrame() = 0;

    /* Pump the OS event loop without rendering — keeps the window manager from
       marking the app unresponsive during spin-waits. */
    virtual void pollEvents() {}

    /* Fire one vsync tick if enough time has accumulated.  Call only from
       frame-boundary spin-waits, never from a raster-position wait (which the
       tick would reset, so the wait could never exit). */
    virtual void tickVBI() {}

    /* ------------------------------------------------------------------ */
    /* Hardware bus — called by bus_read / bus_write in bus.h              */
    /* ------------------------------------------------------------------ */

    /* Read a BBC I/O address ($FC00-$FEFF).  Default: 0x00. */
    virtual uint8_t hwRead(uint16_t addr);

    /* Write a BBC I/O address.  Default: no-op. */
    virtual void    hwWrite(uint16_t addr, uint8_t val);

    /* Notification that the game wrote an OS vector / page-2 cell
       ($0200-$02FF) — IRQ1V, EVNTV, BRKV etc. */
    virtual void    shadowWrite(uint16_t addr, uint8_t val);

    /* Register a BBC address → C function mapping for interrupt dispatch
       (the game's own IRQ1V / EVNTV handler bodies). */
    virtual void registerVBI(uint16_t /*addr*/, void (* /*fn*/)(void)) {}

    /* Runtime indirect JMP/JSR dispatch (JMP ($xx) vector patterns).  ⚠ On a
       6502 binary-only port this is load-bearing: the entry-point sweep
       (docs/entrypoint-sweep.md) enumerates every such site up front. */
    virtual void indirectJmp(uint16_t addr) { (void)addr; }

    /* A BRK executed at `pc` — a software interrupt through BRKV, not a no-op.  See
       platform_c.h; the default implementation reports it the same way smcUnhandled does. */
    virtual void brk(uint16_t pc);

    /* A self-modifying instruction held a value the transpiler's SMC_SITES table does
       not cover (an unlisted opcode byte, or a patched branch offset pointing outside
       the enclosing routine).  The generated C cannot express what the 6502 would
       execute next, so this is a BUG REPORT, not an event to absorb: the default
       implementation is deliberately noisy, and a backend must not quietly ignore it.
       A silent no-op here reads exactly like a rasteriser that runs and draws nothing.
       docs/transpiler.md §Self-modifying code. */
    virtual void smcUnhandled(uint16_t site, uint16_t value);

    /* ------------------------------------------------------------------ */
    /* MOS calls                                                          */
    /* ------------------------------------------------------------------ */

    /* Service an intercepted MOS entry ($FFCE-$FFF7).  `entry` is the ROM
       address JSR'd; A/X/Y come from the cpu struct and are updated in place.
       Unlike the Atari (whose OS the port simply replaced), Revs runs under the
       MOS: OSBYTE/OSWORD are how it reads the keyboard, the ADC and the disc.
       Default: no-op + carry clear.  See docs/bbc-hardware.md §MOS calls. */
    virtual void mosCall(uint16_t /*entry*/) {}

    /* ------------------------------------------------------------------ */
    /* Image loading                                                      */
    /* ------------------------------------------------------------------ */

    /* Load the post-load memory image into mem[].  Returns 0 on OK. */
    virtual int loadImage(const char* path) = 0;

    /* ------------------------------------------------------------------ */
    /* Shared state                                                       */
    /* ------------------------------------------------------------------ */
    bool quit;
};

extern Platform* platform;

#endif /* PLATFORM_H */
